// Stage 12: Real Metal sun-shadow Depth32Float and main RGBA A/B parity
// across two independent camera views. Preserves original Stage 11 test.
// Never enables a production Strata draw route or a second geometry pool.
#include "engine/render/Renderer3D.h"
#include "engine/render/StrataRenderRouting.h"
#include "engine/render/GlobalGeometryPool.h"
#include "engine/platform/Platform.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>
#include <vector>

using namespace aiverse;
using namespace aiverse::render;

namespace {
constexpr int kWidth = 800, kHeight = 600;
constexpr int kViews = 2, kModes = 4, kSlots = kViews*kModes;
constexpr int kModeFrames = 11;
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
size_t slot(int view, Mode mode) { return size_t(view*kModes + int(mode)); }

struct Fixture {
    gfx::Device* device = nullptr;
    Renderer3D renderer;
    Model source, fine, coarse;
    strata::ModelAsset asset;
    strata::GpuMetadataScene residency;
    Mesh floor{};
    uint32_t firstIndex = 0;
    int frame = 0;
    bool failed = false;
    std::string failure;
    std::array<std::vector<uint8_t>,kSlots> captures{};
    std::array<bool,kSlots> received{};
    std::array<std::vector<uint8_t>,kSlots> depthCaptures{};
    std::array<bool,kSlots> depthReceived{};
    std::array<uint32_t,kSlots> shadowDraws{};
    std::array<uint32_t,kSlots> opaqueDraws{};
    std::array<uint32_t,kSlots> modelSubmissions{};

    void fail(const std::string& msg) {
        if (failed) return;
        failed = true;
        failure = msg;
        std::fprintf(stderr, "[strata-raster] FAIL: %s\n", msg.c_str());
    }

    void makeSource() {
        source.path = "strata-raster-original";
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

    void makeAlias(Model& alias, bool leaves) {
        alias.path = leaves ? "strata-raster-leaves" : "strata-raster-parent";
        alias.batching.autoLod = false;
        alias.vertices = source.vertices; // CPU metadata only; NOT uploaded
        alias.indices = asset.primitives[0].geometry.indices;
        alias.materials = source.materials;
        alias.nodes = source.nodes;
        alias.meshes = source.meshes;
        alias.meshes[0].materialBatches.clear();
        alias.meshes[0].shadowBatch = {};
        alias.meshes[0].primitives.clear();
        const auto& tree = asset.primitives[0].geometry.nodes;
        for (uint32_t i = leaves ? 1u : 0u;
             i < (leaves ? 3u : 1u); ++i) {
            Primitive p{};
            p.material = 0;
            p.firstIndex = tree[i].firstIndex;
            p.indexCount = tree[i].indexCount;
            p.firstVertex = 0; p.vertexCount = 4;
            p.boundsMin = {-0.8f,-0.8f,-.01f};
            p.boundsMax = { .8f, .8f, .01f};
            alias.meshes[0].primitives.push_back(p);
        }
        alias.finalize();
        // Borrow ONLY the already uploaded original pooled VB/IB. The two
        // aliases own no RHI resources and must NEVER call releaseModel().
        alias.vb = source.vb;
        alias.ib = source.ib;
        alias.gpuFirstIndex = firstIndex;
        alias.gpuBaseVertex = source.gpuBaseVertex;
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
        if (!renderer.setShadowMapSize(512)) {
            fail("failed to allocate 512x512 sun-shadow depth texture");
            return false;
        }
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
        if (!residency.stage(d, "raster", source, asset)) {
            fail("v2 Strata index sidecar staging failed");
            return false;
        }
        strata::GpuMetadataScene::IndexBinding b;
        if (!residency.indexBinding("raster", source, b) ||
            !b.valid() || b.buffer != source.ib ||
            b.baseVertex != int32_t(source.gpuBaseVertex)) {
            fail("v2 index sidecar is not backed by the original GPU page");
            return false;
        }
        firstIndex = b.firstIndex;
        makeAlias(fine, true);
        makeAlias(coarse, false);
        floor = MeshBuilder().box({0,-1.1f,0},{8,.2f,8}).upload(d);
        if (!floor.vb.valid() || !floor.ib.valid()) {
            fail("receiver floor upload failed");
            return false;
        }
        return true;
    }

    bool checkSelectedRanges(const RTSCamera& cam, bool leaves) {
        strata::ViewConfig view{};
        view.objectToClip = cam.proj(float(kWidth)/kHeight, true) *
                            cam.view() * mat4::identity();
        view.width = kWidth;
        view.height = kHeight;
        view.clipDepthZeroToOne = true;
        view.errorThresholdPixels = leaves ? .01f : 10000.f;
        strata::RenderCandidate c;
        std::string why;
        if (!strata::prepareRenderCandidate(
                source, asset, residency, "raster", 0, 0,
                strata::RenderPass::MainOpaque, view, c, &why)) {
            fail(std::string("stage-9 pooled selection rejected: ") + why);
            return false;
        }
        const Model& alias = leaves ? fine : coarse;
        if (c.ranges.size() != alias.meshes[0].primitives.size()) {
            fail("Strata primitive selection and raster alias disagree");
            return false;
        }
        for (size_t i = 0; i < c.ranges.size(); ++i) {
            const auto& p = alias.meshes[0].primitives[i];
            if (c.ranges[i].firstIndex != firstIndex + p.firstIndex ||
                c.ranges[i].indexCount != p.indexCount ||
                c.ranges[i].baseVertex != int32_t(source.gpuBaseVertex)) {
                fail("Strata raster draw index/base mapping is not source-aligned");
                return false;
            }
        }
        return true;
    }

    void capture(gfx::Device& d, Mode mode, int view) {
        const size_t idx = slot(view,mode);
        std::printf("[strata-depth] RGBA requested view=%d mode=%s frame=%d shadow=%u opaque=%u\n",
                    view,modeName(mode),frame,shadowDraws[idx],opaqueDraws[idx]);
        d.captureNextFrame([this,idx,view,mode](int w,int h,std::vector<uint8_t> rgba) {
            if (w!=kWidth || h!=kHeight || rgba.size()!=size_t(w*h*4)) {
                fail("invalid actual Metal main-camera screenshot dimensions");
                return;
            }
            std::string prefix="strata-depth";
            if (const char* p=std::getenv("AIVERSE_STRATA_DEPTH_CAPTURE_PREFIX")) prefix=p;
            std::ofstream ppm(prefix+"-view"+std::to_string(view)+"-"+modeName(mode)+".ppm",
                              std::ios::binary);
            ppm<<"P6\n"<<w<<" "<<h<<"\n255\n";
            for(size_t i=0;i<rgba.size();i+=4)
                ppm.write(reinterpret_cast<const char*>(rgba.data()+i),3);
            if (!ppm) {fail("cannot save real Metal RGB PPM");return;}
            captures[idx]=std::move(rgba);
            received[idx]=true;
            std::printf("[strata-depth] RGBA received view=%d mode=%s callback_frame=%d bytes=%zu\n",
                        view,modeName(mode),frame,captures[idx].size());
        });
    }

    void captureDepth(Mode mode,int view) {
        const size_t idx=slot(view,mode);
        const int size=renderer.shadowMapSize();
        const bool accepted=renderer.requestSunShadowDepthReadback(
            [this,idx,view,mode,size](int w,int h,std::vector<uint8_t> bytes) {
                if(w!=size || h!=size || bytes.size()!=size_t(w*h)*sizeof(float)) {
                    fail("invalid asynchronous Depth32Float GPU readback");
                    return;
                }
                std::string prefix="strata-depth";
                if (const char* p=std::getenv("AIVERSE_STRATA_DEPTH_CAPTURE_PREFIX")) prefix=p;
                std::ofstream raw(prefix+"-view"+std::to_string(view)+"-"+modeName(mode)+
                                  "-shadow.f32",std::ios::binary);
                raw.write(reinterpret_cast<const char*>(bytes.data()),
                          std::streamsize(bytes.size()));
                if (!raw) {fail("cannot write raw Metal sun shadow depth samples");return;}
                depthCaptures[idx]=std::move(bytes);
                depthReceived[idx]=true;
                std::printf("[strata-depth] DEPTH received view=%d mode=%s callback_frame=%d samples=%zu\n",
                            view,modeName(mode),frame,depthCaptures[idx].size()/sizeof(float));
            });
        if(!accepted)fail("Metal Depth32Float shadow-map readback was not scheduled");
    }

    void verify() {
        if(failed)return;
        auto changed=[](const auto& a,const auto& b)->size_t{
            if(a.size()!=b.size())return size_t(-1);
            size_t n=0;for(size_t i=0;i<a.size();++i)n+=a[i]!=b[i];
            return n;
        };
        for(int view=0;view<kViews;++view){
            const size_t e=slot(view,Mode::Empty);
            const size_t l=slot(view,Mode::Legacy);
            const size_t f=slot(view,Mode::Fine);
            const size_t c=slot(view,Mode::Coarse);
            if(!received[e]||!received[l]||!received[f]||!received[c]||
               !depthReceived[e]||!depthReceived[l]||!depthReceived[f]||!depthReceived[c]){
                fail("not all real GPU RGBA and depth captures completed");return;
            }
            if(captures[e]==captures[l]){
                fail("Legacy main-camera RGBA identical to Empty in view "+std::to_string(view));return;
            }
            if(depthCaptures[e]==depthCaptures[l]){
                fail("Legacy sun-shadow depth identical to Empty in view "+std::to_string(view));return;
            }
            const size_t rgbaF=changed(captures[l],captures[f]);
            const size_t rgbaC=changed(captures[l],captures[c]);
            const size_t depthF=changed(depthCaptures[l],depthCaptures[f]);
            const size_t depthC=changed(depthCaptures[l],depthCaptures[c]);
            std::printf("[strata-depth] view=%d color_fine=%zu color_coarse=%zu depth_fine_bytes=%zu depth_coarse_bytes=%zu\n",
                        view,rgbaF,rgbaC,depthF,depthC);
            if(rgbaF||rgbaC||depthF||depthC){
                fail("real Metal Legacy-vs-Strata main OR shadow depth pixel mismatch");
                return;
            }
            size_t nonclear=0;
            for(size_t i=0;i<depthCaptures[l].size();i+=sizeof(float)){
                float d=0;
                std::memcpy(&d,depthCaptures[l].data()+i,sizeof(float));
                if(!std::isfinite(d)||d<0.f||d>1.f){
                    fail("invalid actual Metal sun-shadow depth value");return;
                }
                nonclear+=d<.999999f;
            }
            if(!nonclear){fail("raw sun-shadow map contains clear depth only");return;}
            std::printf("[strata-depth] view=%d nonclear_shadow_samples=%zu\n",view,nonclear);
            if(shadowDraws[e]||opaqueDraws[e]||
               !shadowDraws[l]||!opaqueDraws[l]||
               !shadowDraws[f]||!opaqueDraws[f]||
               !shadowDraws[c]||!opaqueDraws[c]||
               modelSubmissions[e]!=0 || modelSubmissions[l]!=1 ||
               modelSubmissions[f]!=1 || modelSubmissions[c]!=1){
                fail("wrong Renderer3D shadow/opaque/ModelDraw submission counts");
                return;
            }
        }
        if(captures[slot(0,Mode::Legacy)]==captures[slot(1,Mode::Legacy)]){
            fail("two actual camera views produced identical Legacy RGBA");
            return;
        }
        std::puts("[strata-depth] PASS: two independent camera views");
        std::puts("[strata-depth] PASS: exact main RGBA Legacy/Fine/Coarse parity in each view");
        std::puts("[strata-depth] PASS: exact sun-shadow Depth32Float Legacy/Fine/Coarse parity");
        std::puts("[strata-depth] PASS: nonconstant shadow content and exclusive model submission");
    }

    void runFrame(gfx::Device& d) {
        ++frame;
        if(failed){platform::requestQuit();return;}
        if(frame>120){fail("Metal screenshot/shadow GPU readback timeout");platform::requestQuit();return;}
        if(frame>kViews*kModes*kModeFrames){
            bool all=true;
            for(size_t i=0;i<kSlots;++i)all=all&&received[i]&&depthReceived[i];
            if(all){verify();platform::requestQuit();}
            return;
        }
        const int block=(frame-1)/kModeFrames;
        const int view=block/kModes;
        const Mode mode=Mode(block%kModes);
        const size_t idx=slot(view,mode);
        RTSCamera cam;
        cam.target={0,0,0};
        cam.distance=view==0?5.f:6.f;
        cam.pitch=view==0?.2f:.55f;
        cam.yaw=view==0?0.f:.7f;
        renderer.beginScene(cam,kWidth,kHeight);
        renderer.drawMesh(floor,{}, {.6f,.6f,.6f,1.f});
        const Model* drawn=mode==Mode::Legacy?&source:
                           mode==Mode::Fine?&fine:
                           mode==Mode::Coarse?&coarse:nullptr;
        if(drawn){
            if((mode==Mode::Fine||mode==Mode::Coarse)&&
                !checkSelectedRanges(cam,mode==Mode::Fine)){
                platform::requestQuit();return;
            }
            ModelDraw draw{};
            draw.model=drawn;
            renderer.drawModel(draw);
            modelSubmissions[idx]=1;
        }
        gfx::PassDesc pass{};
        renderer.render(pass);
        const auto profile=renderer.profile();
        shadowDraws[idx]=profile.draws[0];
        opaqueDraws[idx]=profile.draws[1];
        const bool sample=((frame-1)%kModeFrames)==3;
        if(sample)capture(d,mode,view);
        if(!renderer.modelError().empty())fail(renderer.modelError());
        d.endPass();
        // Main pass closed, sun depth pass closed earlier in render().
        // Blit depth using the exact same current GPU command buffer.
        if(sample&&!failed)captureDepth(mode,view);
    }

    void shutdown() {
        if (!device) return;
        device->destroyBuffer(floor.vb);
        device->destroyBuffer(floor.ib);
        renderer.shutdown();
        device->setScreenReadable(false);
        residency.shutdown(*device); // releases sidecar BEFORE source page
        releaseModelGeometryPool(*device,source);
        // Never release borrowed fine/coarse model GPU handles.
        std::printf("[strata-depth] status=%s views=%d modes=%d frames=%d\n",
                    failed ? "FAILED" : "PASSED",kViews,kModes,frame);
    }
};
}

int aiverseMain(int,char**) {
    // Existing CPU MDI admission switch only: GPU-authored draws remain OFF.
    if (setenv("AIVERSE_METAL_CPU_MDI","1",1) != 0) return 2;
    Fixture fixture;
    platform::AppConfig cfg;
    cfg.title="Strata real Metal Renderer3D sun-shadow depth and multiview A/B";
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
    for (bool got : fixture.depthReceived) if (!got) return 1;
    return 0;
}
