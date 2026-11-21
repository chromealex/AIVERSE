// Stage 13: real planar-reflection RTT, main RGBA, and sun shadow A/B.
// Two cameras and two independently textured opaque materials with a UV seam.
// Exclusive per-frame source alias, original Renderer3D, one pooled GPU source.
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
    Mesh floor{}, water{};
    std::array<gfx::TextureHandle,2> materialTextures{};
    uint32_t firstIndex = 0;
    int frame = 0;
    bool failed = false;
    std::string failure;
    std::array<std::vector<uint8_t>,kSlots> captures{};
    std::array<bool,kSlots> received{};
    std::array<std::vector<uint8_t>,kSlots> reflectionCaptures{};
    std::array<bool,kSlots> reflectionReceived{};
    std::array<std::vector<uint8_t>,kSlots> depthCaptures{};
    std::array<bool,kSlots> depthReceived{};
    std::array<uint32_t,kSlots> shadowDraws{};
    std::array<uint32_t,kSlots> opaqueDraws{};
    std::array<uint32_t,kSlots> modelSubmissions{};

    void fail(const std::string& msg) {
        if (failed) return;
        failed = true;
        failure = msg;
        std::fprintf(stderr, "[strata-reflect] FAIL: %s\n", msg.c_str());
    }

    void makeSource() {
        source.path="strata-reflection-two-material-quads";
        source.batching.autoLod=false;
        // Two 4-vertex opaque panels share the exact x=0 edge, but
        // DUPLICATE the seam vertices. They have distinct materials,
        // texture images and UV transforms: this tests a real UV seam,
        // not two unrelated disconnected panels.
        const float left[2]={-1.12f,0.f};
        const float right[2]={0.f,1.12f};
        source.vertices.resize(8);
        source.indices.resize(12);
        MeshDef mesh;
        for (uint32_t material=0;material<2;++material) {
            const uint32_t v=material*4, first=material*6;
            for (uint32_t k=0;k<4;++k) {
                auto& vert=source.vertices[v+k];
                vert.pos={(k&1)?right[material]:left[material],
                          (k&2)? .75f:-.75f,0.f};
                vert.nrm={0,0,1};
                vert.uv={(k&1) ? 1.3f:.12f,(k&2) ? .88f:.08f};
            }
            source.indices[first+0]=v+0;
            source.indices[first+1]=v+1;
            source.indices[first+2]=v+2;
            source.indices[first+3]=v+1;
            source.indices[first+4]=v+3;
            source.indices[first+5]=v+2;
            Material mat;
            mat.name=material==0?"stone-pattern":"copper-pattern";
            mat.doubleSided=true;
            mat.castShadow=true;
            mat.vertexColors=true;
            mat.baseTex=int(material);
            mat.baseColor=material==0?float4{.94f,.63f,.33f,1.f}:
                                      float4{.32f,.82f,.96f,1.f};
            mat.roughness=material==0?.73f:.21f;
            mat.metallic=material==0?.08f:.42f;
            mat.uvOffset=material==0?float2{.12f,.07f}:float2{.29f,.16f};
            mat.uvScale=material==0?float2{1.3f,1.f}:float2{.82f,1.6f};
            source.materials.push_back(mat);
            Primitive p{};
            p.material=int(material);
            p.firstIndex=first;p.indexCount=6;
            p.firstVertex=v;p.vertexCount=4;
            mesh.primitives.push_back(p);
        }
        source.meshes.push_back(std::move(mesh));
        Node node{};node.mesh=0;source.nodes.push_back(node);
        source.finalize();

        // Each opaque material has its own independent parent and two
        // child triangles. Parent is lossless, for EXACT A/B comparison.
        for(uint32_t material=0;material<2;++material){
            strata::ModelPrimitiveAsset primitive{};
            primitive.mesh=0;primitive.primitive=material;
            primitive.material=material;
            auto& a=primitive.geometry;
            const uint32_t v=material*4;
            a.indices={v+0,v+1,v+2,v+1,v+3,v+2,
                       v+0,v+1,v+2,v+1,v+3,v+2};
            a.roots={0};
            a.nodes.resize(3);
            a.bounds.resize(3);
            a.nodes[0].firstIndex=0;a.nodes[0].indexCount=6;
            a.nodes[0].firstChild=1;a.nodes[0].childCount=2;
            a.nodes[0].geometricError=.25f;
            a.nodes[1].firstIndex=6;a.nodes[1].indexCount=3;
            a.nodes[2].firstIndex=9;a.nodes[2].indexCount=3;
            for(uint32_t i=0;i<3;++i){
                a.nodes[i].resident=true;
                a.nodes[i].materialGroup=material;
                a.bounds[i]={{left[material],-.75f,-.02f},
                             {right[material],.75f,.02f}};
            }
            asset.primitives.push_back(std::move(primitive));
        }
    }

    bool makeTextures(gfx::Device& d) {
        source.images.resize(2);
        for(size_t material=0;material<2;++material){
            std::array<uint8_t,4*4*4> rgba{};
            for(int y=0;y<4;++y)
                for(int x=0;x<4;++x){
                    const bool light=((x+y+int(material))&1)==0;
                    const size_t i=size_t(y*4+x)*4;
                    rgba[i+0]=uint8_t(material? (light?16:222):(light?248:44));
                    rgba[i+1]=uint8_t(material? (light?148:28):(light?70:196));
                    rgba[i+2]=uint8_t(material? (light?238:44):(light?26:48));
                    rgba[i+3]=255;
                }
            gfx::TextureDesc td{};
            td.width=4;td.height=4;
            td.format=gfx::PixelFormat::RGBA8;
            td.filter=gfx::Filter::Nearest;
            td.repeat=true;
            td.data=rgba.data();
            materialTextures[material]=d.createTexture(td);
            if(!materialTextures[material].valid())return false;
            source.images[material].tex=materialTextures[material];
            source.images[material].borrowedTexture=true;
        }
        return true;
    }

    void makeAlias(Model& alias,bool leaves){
        alias.path=leaves?"strata-reflect-children":"strata-reflect-parents";
        alias.batching.autoLod=false;
        alias.vertices=source.vertices;
        alias.materials=source.materials;
        alias.images=source.images;  // borrowed source-owned GPU textures
        alias.nodes=source.nodes;
        alias.meshes=source.meshes;
        alias.meshes[0].materialBatches.clear();
        alias.meshes[0].shadowBatch={};
        alias.meshes[0].primitives.clear();
        alias.indices.clear();
        uint32_t prefix=0;
        for(size_t material=0;material<asset.primitives.size();++material){
            const auto& a=asset.primitives[material].geometry;
            alias.indices.insert(alias.indices.end(),a.indices.begin(),a.indices.end());
            for(uint32_t n=leaves?1u:0u;n<(leaves?3u:1u);++n){
                Primitive p{};
                p.material=int(material);
                p.firstIndex=prefix+a.nodes[n].firstIndex;
                p.indexCount=a.nodes[n].indexCount;
                p.firstVertex=uint32_t(material)*4;p.vertexCount=4;
                alias.meshes[0].primitives.push_back(p);
            }
            prefix+=uint32_t(a.indices.size());
        }
        alias.finalize();
        // Never upload or release the aliases. Their only GPU VB and IB are
        // borrowed from the original GlobalGeometryPool source page.
        alias.vb=source.vb;alias.ib=source.ib;
        alias.gpuFirstIndex=firstIndex;
        alias.gpuBaseVertex=source.gpuBaseVertex;
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
        renderer.lights.setQuality(LightQuality::High);
        renderer.lights.setWaterReflections(true);
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
        if (!makeTextures(d)) {
            fail("two distinct GPU material textures not created");return false;
        }
        if (!uploadModelToGeometryPool(d, source)) {
            fail("source not admitted to existing shared GlobalGeometryPool");
            return false;
        }
        if (!residency.stage(d, "reflection", source, asset)) {
            fail("v2 Strata index sidecar staging failed");
            return false;
        }
        strata::GpuMetadataScene::IndexBinding b;
        if (!residency.indexBinding("reflection", source, b) ||
            !b.valid() || b.buffer != source.ib ||
            b.baseVertex != int32_t(source.gpuBaseVertex)) {
            fail("v2 index sidecar is not backed by the original GPU page");
            return false;
        }
        firstIndex = b.firstIndex;
        makeAlias(fine, true);
        makeAlias(coarse, false);
        floor=MeshBuilder().box({0,-1.23f,0},{8,.18f,8}).upload(d);
        water=MeshBuilder().box({0,-1.0f,0},{8,.06f,8}).upload(d);
        if(!floor.vb.valid()||!floor.ib.valid()||
           !water.vb.valid()||!water.ib.valid()){
            fail("floor or planar reflection water mesh GPU upload failed");
            return false;
        }
        return true;
    }

    bool checkSelectedRanges(const RTSCamera& cam,bool leaves){
        (void)cam;
        // Exactly the mirrored transform used in Renderer3DReflect.cpp,
        // with the same selected water plane y=-1.0.
        mat4 mirror=mat4::identity();
        mirror.at(1,1)=-1.f;mirror.at(1,3)=-2.f;
        strata::ViewConfig view{};
        view.objectToClip=mat4::scale({-1,1,1}) * renderer.viewProj() * mirror;
        view.width=kWidth/2;view.height=kHeight/2;
        view.clipDepthZeroToOne=renderer.clipDepthZeroToOne();
        view.errorThresholdPixels=leaves?.01f:10000.f;
        // Only test material/index mapping in this fixture. Real reflection
        // culling remains the same Renderer3D mirror-view culler.
        view.frustumCull=false;
        const Model& alias=leaves?fine:coarse;
        size_t dest=0;
        for(uint32_t material=0;material<2;++material){
            strata::RenderCandidate c;
            std::string why;
            if(!strata::prepareRenderCandidate(source,asset,residency,
                    "reflection",0,material,
                    strata::RenderPass::PlanarReflection,view,c,&why)){
                fail("reflection pass Strata selection rejected material "+
                     std::to_string(material)+": "+why);
                return false;
            }
            const size_t count=leaves?2:1;
            if(c.ranges.size()!=count){
                fail("reflection selection did not match material-cluster count");
                return false;
            }
            for(const auto& r:c.ranges){
                if(dest>=alias.meshes[0].primitives.size()){
                    fail("reflection alias primitive count overflow");return false;
                }
                const auto& p=alias.meshes[0].primitives[dest++];
                if(r.firstIndex!=firstIndex+p.firstIndex||
                   r.indexCount!=p.indexCount||
                   r.material!=material||
                   r.baseVertex!=int32_t(source.gpuBaseVertex)){
                    fail("reflection source-sidecar index or material binding mismatch");
                    return false;
                }
            }
        }
        if(dest!=alias.meshes[0].primitives.size()){
            fail("reflection alias missing one material cluster");return false;
        }
        return true;
    }

    void capture(gfx::Device& d, Mode mode, int view) {
        const size_t idx = slot(view,mode);
        std::printf("[strata-reflect] RGBA requested view=%d mode=%s frame=%d shadow=%u opaque=%u\n",
                    view,modeName(mode),frame,shadowDraws[idx],opaqueDraws[idx]);
        d.captureNextFrame([this,idx,view,mode](int w,int h,std::vector<uint8_t> rgba) {
            if (w!=kWidth || h!=kHeight || rgba.size()!=size_t(w*h*4)) {
                fail("invalid actual Metal main-camera screenshot dimensions");
                return;
            }
            std::string prefix="strata-reflect";
            if (const char* p=std::getenv("AIVERSE_STRATA_REFLECTION_CAPTURE_PREFIX")) prefix=p;
            std::ofstream ppm(prefix+"-view"+std::to_string(view)+"-"+modeName(mode)+".ppm",
                              std::ios::binary);
            ppm<<"P6\n"<<w<<" "<<h<<"\n255\n";
            for(size_t i=0;i<rgba.size();i+=4)
                ppm.write(reinterpret_cast<const char*>(rgba.data()+i),3);
            if (!ppm) {fail("cannot save real Metal RGB PPM");return;}
            captures[idx]=std::move(rgba);
            received[idx]=true;
            std::printf("[strata-reflect] RGBA received view=%d mode=%s callback_frame=%d bytes=%zu\n",
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
                std::string prefix="strata-reflect";
                if (const char* p=std::getenv("AIVERSE_STRATA_REFLECTION_CAPTURE_PREFIX")) prefix=p;
                std::ofstream raw(prefix+"-view"+std::to_string(view)+"-"+modeName(mode)+
                                  "-shadow.f32",std::ios::binary);
                raw.write(reinterpret_cast<const char*>(bytes.data()),
                          std::streamsize(bytes.size()));
                if (!raw) {fail("cannot write raw Metal sun shadow depth samples");return;}
                depthCaptures[idx]=std::move(bytes);
                depthReceived[idx]=true;
                std::printf("[strata-reflect] DEPTH received view=%d mode=%s callback_frame=%d samples=%zu\n",
                            view,modeName(mode),frame,depthCaptures[idx].size()/sizeof(float));
            });
        if(!accepted)fail("Metal Depth32Float shadow-map readback was not scheduled");
    }

    void captureReflection(Mode mode,int view){
        const size_t idx=slot(view,mode);
        if(!renderer.lights.stats().reflection){
            fail("Renderer3D did not execute actual planar reflection pass");return;
        }
        const bool accepted=renderer.requestPlanarReflectionColorReadback(
            [this,idx,mode,view](int w,int h,std::vector<uint8_t> rgba) {
                if(w!=kWidth/2||h!=kHeight/2||
                   rgba.size()!=size_t(w*h*4)){
                    fail("GPU RTT callback is not a 400x300 RGBA8 reflection");
                    return;
                }
                std::string prefix="strata-reflect";
                if(const char* p=std::getenv("AIVERSE_STRATA_REFLECTION_CAPTURE_PREFIX"))
                    prefix=p;
                std::ofstream ppm(prefix+"-view"+std::to_string(view)+"-"+
                                  modeName(mode)+"-reflection.ppm",std::ios::binary);
                ppm<<"P6\n"<<w<<" "<<h<<"\n255\n";
                for(size_t i=0;i<rgba.size();i+=4)
                    ppm.write(reinterpret_cast<const char*>(rgba.data()+i),3);
                if(!ppm){fail("could not save actual Metal planar RTT PPM");return;}
                reflectionCaptures[idx]=std::move(rgba);
                reflectionReceived[idx]=true;
                std::printf("[strata-reflect] RTT received view=%d mode=%s callback_frame=%d pixels=%zu\n",
                            view,modeName(mode),frame,
                            reflectionCaptures[idx].size()/4);
            });
        if(!accepted)fail("real Metal reflection RTT readback unavailable, no screen fallback");
    }

    void verify(){
        if(failed)return;
        auto diff=[](const auto& x,const auto& y)->size_t{
            if(x.size()!=y.size())return size_t(-1);
            size_t n=0;
            for(size_t i=0;i<x.size();++i)n+=x[i]!=y[i];
            return n;
        };
        for(int view=0;view<kViews;++view){
            const size_t e=slot(view,Mode::Empty);
            const size_t l=slot(view,Mode::Legacy);
            const size_t f=slot(view,Mode::Fine);
            const size_t c=slot(view,Mode::Coarse);
            for(size_t idx:{e,l,f,c})
                if(!received[idx]||!depthReceived[idx]||!reflectionReceived[idx]){
                    fail("not all true Metal main/depth/reflection readbacks arrived");return;
                }
            if(captures[l]==captures[e]||
               depthCaptures[l]==depthCaptures[e]||
               reflectionCaptures[l]==reflectionCaptures[e]){
                fail("real main/depth/reflection Legacy baseline == Empty in view "+
                     std::to_string(view));return;
            }
            const size_t mainF=diff(captures[l],captures[f]);
            const size_t mainC=diff(captures[l],captures[c]);
            const size_t shadowF=diff(depthCaptures[l],depthCaptures[f]);
            const size_t shadowC=diff(depthCaptures[l],depthCaptures[c]);
            const size_t reflF=diff(reflectionCaptures[l],reflectionCaptures[f]);
            const size_t reflC=diff(reflectionCaptures[l],reflectionCaptures[c]);
            std::printf("[strata-reflect] view=%d main_fine=%zu main_coarse=%zu shadow_fine=%zu shadow_coarse=%zu reflection_fine=%zu reflection_coarse=%zu\n",
                        view,mainF,mainC,shadowF,shadowC,reflF,reflC);
            if(mainF||mainC||shadowF||shadowC||reflF||reflC){
                fail("Legacy/Strata true Metal main, shadow or reflection RTT differs");
                return;
            }
            if(shadowDraws[e]||opaqueDraws[e]||
               shadowDraws[l]<2||opaqueDraws[l]<2||
               shadowDraws[f]<4||opaqueDraws[f]<4||
               shadowDraws[c]<2||opaqueDraws[c]<2||
               modelSubmissions[e]!=0||modelSubmissions[l]!=1||
               modelSubmissions[f]!=1||modelSubmissions[c]!=1){
                fail("two-material Renderer3D pass counters invalid");return;
            }
            std::printf("[strata-reflect] view=%d 2-materials textures=yes reflection_pixels=%zu\n",
                        view,reflectionCaptures[l].size()/4);
        }
        if(captures[slot(0,Mode::Legacy)]==captures[slot(1,Mode::Legacy)]||
           reflectionCaptures[slot(0,Mode::Legacy)]==
           reflectionCaptures[slot(1,Mode::Legacy)]){
            fail("two camera views produced identical main/reflection RTT images");
            return;
        }
        std::puts("[strata-reflect] PASS: actual planar reflection RTT nonblank and byte-identical across Legacy/Fine/Coarse");
        std::puts("[strata-reflect] PASS: two independent cameras, two opaque UV-textured materials");
        std::puts("[strata-reflect] PASS: sun-shadow and main RGBA strict pixel parity, one pooled source and one ModelDraw");
    }

    void runFrame(gfx::Device& d) {
        ++frame;
        if(failed){platform::requestQuit();return;}
        if(frame>120){fail("Metal screenshot/shadow GPU readback timeout");platform::requestQuit();return;}
        if(frame>kViews*kModes*kModeFrames){
            bool all=true;
            for(size_t i=0;i<kSlots;++i)
                all=all&&received[i]&&depthReceived[i]&&reflectionReceived[i];
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
        // Required trigger for the ACTUAL LightSystem reflection RTT. The
        // planar source is translucent; opaque model primitives reflect.
        renderer.drawMesh(water,{}, {.16f,.27f,.44f,.58f},
                          kDrawWaterSurface,.98f);
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
        if(sample&&!failed){
            captureDepth(mode,view);
            captureReflection(mode,view);
        }
    }

    void shutdown() {
        if (!device) return;
        device->destroyBuffer(floor.vb);
        device->destroyBuffer(floor.ib);
        device->destroyBuffer(water.vb);
        device->destroyBuffer(water.ib);
        renderer.shutdown();
        device->setScreenReadable(false);
        residency.shutdown(*device); // releases sidecar BEFORE source page
        releaseModelGeometryPool(*device,source);
        for(auto tex:materialTextures)
            if(tex.valid())device->destroyTexture(tex);
        // Never release borrowed fine/coarse model GPU handles.
        std::printf("[strata-reflect] status=%s views=%d modes=%d frames=%d\n",
                    failed ? "FAILED" : "PASSED",kViews,kModes,frame);
    }
};
}

int aiverseMain(int,char**) {
    // Existing CPU MDI admission switch only: GPU-authored draws remain OFF.
    if (setenv("AIVERSE_METAL_CPU_MDI","1",1) != 0) return 2;
    Fixture fixture;
    platform::AppConfig cfg;
    cfg.title="Strata real Metal reflection RTT A/B, two cameras and materials";
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
    for (bool got : fixture.reflectionReceived) if (!got) return 1;
    return 0;
}
