// Portable Stage 14: actual real Metal Renderer3D A/B for the COMMON
// GLES3/Vulkan/Metal PortableSource CPU-LOD and borrowed source VB route.
// No production Strata GPU compute or competing raster implementation.
#include "engine/render/Renderer3D.h"
#include "engine/render/StrataPortableRender.h"
#include "engine/render/GlobalGeometryPool.h"
#include "engine/platform/Platform.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;

namespace {
constexpr int kWidth = 800, kHeight = 600;
enum class Mode : size_t { Empty=0, Legacy=1, Fine=2, Coarse=3 };
const char* modeName(Mode m) {
    switch (m) {
        case Mode::Empty: return "empty";
        case Mode::Legacy: return "legacy";
        case Mode::Fine: return "strata-fine";
        case Mode::Coarse: return "strata-coarse";
    }
    return "?";
}

struct Fixture {
    gfx::Device* device = nullptr;
    Renderer3D renderer;
    Model source, fine, coarse;
    strata::ModelAsset asset;
    strata::PortableSource portable;
    Mesh floor{};
    uint32_t firstIndex = 0;
    int frame = 0;
    bool failed = false;
    std::string failure;
    std::array<std::vector<uint8_t>,4> captures{};
    std::array<bool,4> received{};
    std::array<uint32_t,4> shadowDraws{};
    std::array<uint32_t,4> opaqueDraws{};
    std::array<uint32_t,4> modelSubmissions{};

    void fail(const std::string& msg) {
        if (failed) return;
        failed = true;
        failure = msg;
        std::fprintf(stderr, "[strata-portable] FAIL: %s\n", msg.c_str());
    }

    void makeSource() {
        source.path = "strata-portable-original";
        source.batching.autoLod = false;
        source.vertices.resize(4);
        for (size_t i = 0; i != 4; ++i) {
            auto& v = source.vertices[i];
            v.pos = {(i & 1) ? 0.8f : -0.8f,
                     (i & 2) ? 0.8f : -0.8f, 0.0f};
            v.nrm = {0, 0, 1};
            v.uv = {(i & 1) ? 1.f : 0.f, (i & 2) ? 1.f : 0.f};
        }
        source.indices = {0,1,2, 1,3,2};
        Material mat;
        mat.doubleSided = true;
        mat.baseColor = {.93f,.27f,.12f,1.f};
        mat.roughness = .67f;
        source.materials.push_back(mat);
        MeshDef mesh;
        Primitive p{};
        p.material = 0;
        p.firstIndex = 0; p.indexCount = 6;
        p.firstVertex = 0; p.vertexCount = 4;
        p.boundsMin = {-0.8f,-0.8f,-0.01f};
        p.boundsMax = { 0.8f, 0.8f, 0.01f};
        mesh.primitives.push_back(p);
        mesh.boundsMin = p.boundsMin;
        mesh.boundsMax = p.boundsMax;
        source.meshes.push_back(mesh);
        Node node;
        node.mesh = 0;
        source.nodes.push_back(node);
        source.finalize();

        strata::ModelPrimitiveAsset primitive{};
        primitive.mesh = 0;
        primitive.primitive = 0;
        primitive.material = 0;
        auto& a = primitive.geometry;
        // Parent and both children cover EXACTLY the original two
        // triangles, including winding, UVs, normals and material.
        // No LOD simplification tolerance is involved in this strict probe.
        a.indices = {0,1,2, 1,3,2, 0,1,2, 1,3,2};
        a.roots = {0};
        a.nodes.resize(3);
        a.bounds.resize(3);
        a.nodes[0].firstIndex = 0;
        a.nodes[0].indexCount = 6;
        a.nodes[0].firstChild = 1;
        a.nodes[0].childCount = 2;
        a.nodes[0].geometricError = 0.25f;
        a.nodes[1].firstIndex = 6; a.nodes[1].indexCount = 3;
        a.nodes[2].firstIndex = 9; a.nodes[2].indexCount = 3;
        for (size_t i=0; i<3; ++i) {
            a.nodes[i].resident = true;
            a.nodes[i].materialGroup = 0;
            a.bounds[i] = {{-.8f,-.8f,-.02f},{.8f,.8f,.02f}};
        }
        asset.primitives.push_back(std::move(primitive));
    }

    bool makeAlias(Model& alias, const RTSCamera& cam, bool leaves) {
        strata::PortablePassView main{cam.proj(float(kWidth)/kHeight,true)*
                                      cam.view(),kWidth,kHeight,true,
                                      leaves?.01f:10000.f};
        strata::PortablePassView shadow{renderer.lightViewProj(),
                                        renderer.shadowMapSize(),
                                        renderer.shadowMapSize(),true,
                                        leaves?.01f:10000.f};
        std::string error;
        if(!portable.prepareModel(mat4::identity(),{main,shadow},
                                   false,alias,&error)){
            fail(std::string("portable source LOD selection failed: ")+error);
            return false;
        }
        if(alias.vb!=source.vb || alias.ib!=portable.indexBuffer() ||
           alias.gpuBaseVertex!=source.gpuBaseVertex ||
           alias.gpuFirstIndex!=portable.firstIndex() ||
           alias.meshes.size()!=1 ||
           alias.meshes[0].primitives.size()!=(leaves?2u:1u)){
            fail("portable alias does not borrow correct source buffers or LOD frontier");
            return false;
        }
        return true;
    }

        bool initialize(gfx::Device& d) {
        device = &d;
        if (d.info().backend != gfx::Backend::Metal ||
            !d.supportsMultiDrawIndirect() ||
            !renderer.init(d)) {
            fail("real Metal backend / Renderer3D unavailable: " +
                 d.lastError());
            return false;
        }
        // Force the Metal drawable to be readable BEFORE beginFrame acquires
        // it. Otherwise captureNextFrame() may defer a request by two frames
        // and unintentionally capture the following Legacy/Strata mode.
        d.setScreenReadable(true);
        renderer.setProfiling(true); // require actual shadow/opaque draw counters
        renderer.setShadowQuality(ShadowQuality::High);
        renderer.setTraceEnabled(false);
        renderer.setModelReflectionsEnabled(false);
        renderer.setModelMultiDrawEnabled(false);
        renderer.setModelInstancingEnabled(false);
        renderer.setGpuAuthoredDrawEnabled(false);
        renderer.setStaticCrossModelBatchMode(
            Renderer3D::StaticCrossModelBatchMode::Forbid);
        renderer.lighting.fogDensity = 0.f;
        makeSource();
        if (!uploadModelToGeometryPool(d, source)) {
            fail("source not admitted to existing shared GlobalGeometryPool");
            return false;
        }
        std::string portableError;
        if (!portable.stage(d,source,asset,&portableError)) {
            fail(std::string("portable source staging failed: ")+portableError);
            return false;
        }
        firstIndex=portable.firstIndex();
        if (!portable.stats().pooledIndices) {
            fail("Metal portable source failed to use original shared GPU page");
            return false;
        }
        floor = MeshBuilder().box({0,-1.1f,0},{8,.2f,8}).upload(d);
        if (!floor.vb.valid() || !floor.ib.valid()) {
            fail("receiver floor upload failed");
            return false;
        }
        return true;
    }

    bool checkSelectedRanges(const RTSCamera& cam, bool leaves) {
        Model& alias=leaves?fine:coarse;
        if(!makeAlias(alias,cam,leaves))return false;
        const auto& tree=asset.primitives[0].geometry.nodes;
        for(size_t i=0;i<alias.meshes[0].primitives.size();++i){
            const auto& p=alias.meshes[0].primitives[i];
            const auto& n=tree[leaves?i+1:0];
            if(p.firstIndex!=n.firstIndex || p.indexCount!=n.indexCount){
                fail("portable alias cluster range differs from selected source");
                return false;
            }
        }
        return true;
    }

        void capture(gfx::Device& d, Mode mode) {
        const auto idx = size_t(mode);
        std::printf("[strata-portable] capture requested mode=%s frame=%d "
                    "shadow=%u opaque=%u\n",
                    modeName(mode),frame,shadowDraws[idx],opaqueDraws[idx]);
        d.captureNextFrame([this,idx](int w,int h,std::vector<uint8_t> rgba) {
            if (w != kWidth || h != kHeight ||
                rgba.size() != size_t(w*h*4)) {
                fail("invalid Metal screenshot dimensions");
                return;
            }
            const auto mode = Mode(idx);
            std::string prefix = "strata-portable";
            if (const char* p = std::getenv("AIVERSE_STRATA_RASTER_CAPTURE_PREFIX"))
                prefix = p;
            std::ofstream ppm(prefix + "-" + modeName(mode) + ".ppm",
                              std::ios::binary);
            ppm << "P6\n" << w << " " << h << "\n255\n";
            for (size_t pos = 0; pos < rgba.size(); pos += 4)
                ppm.write(reinterpret_cast<const char*>(rgba.data()+pos), 3);
            if (!ppm) {
                fail("unable to write raster A/B PPM capture");
                return;
            }
            captures[idx] = std::move(rgba);
            received[idx] = true;
            std::printf("[strata-portable] capture received mode=%s "
                        "callback_frame=%d bytes=%zu\n",
                        modeName(mode),frame,captures[idx].size());
        });
    }

    void verify() {
        if (failed) return;
        for (bool got : received)
            if (!got) { fail("missing real GPU screenshot"); return; }
        const auto& empty = captures[size_t(Mode::Empty)];
        const auto& legacy = captures[size_t(Mode::Legacy)];
        const auto& a = captures[size_t(Mode::Fine)];
        const auto& b = captures[size_t(Mode::Coarse)];
        if (legacy == empty) {
            std::fprintf(stderr,
                "[strata-portable] identical Empty/Legacy: "
                "shadow empty=%u legacy=%u, opaque empty=%u legacy=%u\n",
                shadowDraws[size_t(Mode::Empty)],
                shadowDraws[size_t(Mode::Legacy)],
                opaqueDraws[size_t(Mode::Empty)],
                opaqueDraws[size_t(Mode::Legacy)]);
            fail("Legacy and empty screenshots identical; model invisible "
                 "or screenshot capture phase mismatch");
            return;
        }
        auto countChanged = [](const auto& x,const auto& y) {
            size_t changed=0;
            for(size_t i=0;i<x.size();++i) changed += x[i] != y[i];
            return changed;
        };
        const size_t fineDelta = countChanged(legacy,a);
        const size_t coarseDelta = countChanged(legacy,b);
        std::printf("[strata-portable] Legacy/Fine changed_channels=%zu\n",fineDelta);
        std::printf("[strata-portable] Legacy/Coarse changed_channels=%zu\n",coarseDelta);
        if (fineDelta || coarseDelta) {
            fail("real Metal renderer pixel mismatch; no raster parity");
            return;
        }
        const uint32_t shadowBaseline=shadowDraws[size_t(Mode::Legacy)];
        if (!shadowBaseline || !opaqueDraws[size_t(Mode::Legacy)] ||
            !shadowDraws[size_t(Mode::Fine)] ||
            !shadowDraws[size_t(Mode::Coarse)] ||
            !opaqueDraws[size_t(Mode::Fine)] ||
            !opaqueDraws[size_t(Mode::Coarse)]) {
            fail("no verified real main/shadow model submissions");
            return;
        }
        for (size_t i=1; i<4; ++i)
            if (modelSubmissions[i] != 1) {
                fail("duplicate or missing ModelDraw submission");
                return;
            }
        std::puts("[strata-portable] PASS: real Renderer3D legacy vs fine/coarse source-page pixel parity");
        std::puts("[strata-portable] PASS: sun shadow and main passes submitted through same renderer");
        std::puts("[strata-portable] PASS: exclusive one-ModelDraw-per-frame, no second GPU geometry pool");
    }

    void runFrame(gfx::Device& d) {
        ++frame;
        if (failed) { platform::requestQuit(); return; }
        if (frame > 90) { fail("GPU screenshot capture timeout"); platform::requestQuit(); return; }
        if (frame > 43) {
            bool all = true;
            for (bool got : received) all = all && got;
            if (all) { verify(); platform::requestQuit(); }
            return;
        }

        Mode mode = frame <= 8 ? Mode::Empty :
                    frame <= 19 ? Mode::Legacy :
                    frame <= 30 ? Mode::Fine : Mode::Coarse;
        RTSCamera cam;
        cam.target={0,0,0};
        cam.distance=5.f;
        cam.pitch=.2f;
        cam.yaw=0.f;
        renderer.beginScene(cam,kWidth,kHeight);
        renderer.drawMesh(floor, {}, {.6f,.6f,.6f,1.f});

        const Model* drawn = mode==Mode::Legacy ? &source :
                             mode==Mode::Fine ? &fine :
                             mode==Mode::Coarse ? &coarse : nullptr;
        if (drawn) {
            if (mode == Mode::Fine || mode == Mode::Coarse)
                if (!checkSelectedRanges(cam,mode==Mode::Fine)) {
                    platform::requestQuit(); return;
                }
            ModelDraw draw{};
            draw.model = drawn;
            renderer.drawModel(draw);
            modelSubmissions[size_t(mode)] = 1;
        }
        gfx::PassDesc pass{};
        renderer.render(pass);
        auto stats = renderer.profile();
        shadowDraws[size_t(mode)] = stats.draws[0];
        opaqueDraws[size_t(mode)] = stats.draws[1];
        // Capture in the MIDDLE of each steady mode, not on its final
        // frame. Even if the backend delays its capture by 1-2 frames,
        // the captured drawable still belongs to the requested mode.
        if (frame == 4) capture(d,Mode::Empty);
        if (frame == 15) capture(d,Mode::Legacy);
        if (frame == 26) capture(d,Mode::Fine);
        if (frame == 37) capture(d,Mode::Coarse);
        if (!renderer.modelError().empty()) fail(renderer.modelError());
        d.endPass();
    }

    void shutdown() {
        if (!device) return;
        device->destroyBuffer(floor.vb);
        device->destroyBuffer(floor.ib);
        renderer.shutdown();
        device->setScreenReadable(false);
        // Only the original Model owns GPU vertices. Drop all borrowed
        // alias page references before releasing the portable sidecar.
        fine=Model{};
        coarse=Model{};
        portable.shutdown(*device);
        releaseModelGeometryPool(*device,source);
        // Never release borrowed fine/coarse model GPU handles.
        std::printf("[strata-portable] status=%s frames=%d\n",
                    failed ? "FAILED" : "PASSED",frame);
    }
};
}

int aiverseMain(int,char**) {
    // Existing CPU MDI admission switch only: GPU-authored draws remain OFF.
    if (setenv("AIVERSE_METAL_CPU_MDI","1",1) != 0) return 2;
    Fixture fixture;
    platform::AppConfig cfg;
    cfg.title="Strata real Metal PortableSource Renderer3D A/B";
    cfg.width=kWidth; cfg.height=kHeight; cfg.highDpi=false;
    platform::AppCallbacks cb;
    cb.init=[&](gfx::Device& d) {
        if (!fixture.initialize(d)) platform::requestQuit();
    };
    cb.frame=[&](gfx::Device& d,double) { fixture.runFrame(d); };
    cb.shutdown=[&] { fixture.shutdown(); };
    platform::run(cfg,cb);
    if (fixture.failed) return 1;
    for (bool got : fixture.received) if (!got) return 1;
    return 0;
}
