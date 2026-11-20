// Deterministic, asset-free visual fixture shared by GL and Metal runners.
#pragma once
#include "engine/render/Prisma.h"
#include "engine/render/PrismaScreenLighting.h"
#include "HdrShaders.h"
#include "engine/world/PhysisSkyBridge.h"
#include "engine/render/graph/ShaderGraph.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <fstream>

namespace aiverse::test {
using namespace render;
struct HdrScene;
void initContactShadowScene(HdrScene&);
void drawContactShadowScene(HdrScene&);
struct HdrScene {
    Renderer3D renderer;
    Prisma post;
    PrismaScreenLighting screen;
    bool advanced = false, ownsFrame = true, materialGallery = false;
    PostProcess* pipeline() { return advanced ? static_cast<PostProcess*>(&screen) : static_cast<PostProcess*>(&post); }
    RTSCamera camera;
    Mesh floor, cube, water;
    std::unique_ptr<Model> spheres;
    int frame = 0, probeIndex = 0, gridProbe = -1;
    bool verify = false;
    std::unique_ptr<Model> probeBg, probeFg;
    bool captured = false, failed = false, temporalVerify = false;
    bool prismaMultiViewVerify = false; // opt-in Stage 97 real-GPU camera lifetime probe
    bool prismaStressVerify = false; // Stage 98 repeated camera churn + actual fence drain
    bool prismaGhostVerify = false; // Stage 98 moving actor after camera cut, GPU pixel readback
    struct GhostRedSample {
        int width=0, count=0;
        double sumX=0;
        std::vector<int> columns;
        bool ready=false;
    };
    std::mutex ghostSampleMutex_;
    GhostRedSample ghostBefore_,ghostAfter_;
    int ghostFrameWait_=0;
    int prismaStressCycle = 0, prismaStressTicks = 0, prismaStressWait = 0;
    uint64_t prismaStressSubmittedFence = 0;
    uint64_t prismaStressMaxLiveBytes = 0;
    float temporalMinX=1e9f,temporalMaxX=-1e9f,temporalMinY=1e9f,temporalMaxY=-1e9f;
    std::string path;
    gfx::PassDesc output;
    std::function<void(gfx::Device::CaptureFn)> readOutput;
    void init(gfx::Device& d) {
        std::printf("GPU: %s; RGBA16F=%d\n", d.info().name.c_str(), d.supports(gfx::PixelFormat::RGBA16F));
        if (!renderer.init(d) || !post.init(d)) { failed = true; return; }
        temporalVerify=std::getenv("AIVERSE_TAA_VERIFY")!=nullptr;
        prismaMultiViewVerify=std::getenv("AIVERSE_PRISMA_MULTIVIEW_VERIFY")!=nullptr;
        prismaStressVerify=std::getenv("AIVERSE_PRISMA_STRESS_VERIFY")!=nullptr;
        prismaGhostVerify=std::getenv("AIVERSE_PRISMA_GHOST_VERIFY")!=nullptr;
        advanced = temporalVerify || std::getenv("AIVERSE_SCREEN_LIGHTING") != nullptr;
        const int prismaModes=int(prismaMultiViewVerify)+int(prismaStressVerify)+
                              int(prismaGhostVerify);
        if (prismaModes>1 || (prismaModes && (temporalVerify ||
            std::getenv("AIVERSE_HDR_VERIFY") ||
            std::getenv("AIVERSE_SCREEN_LIGHTING")))) {
            std::fprintf(stderr, "PRISMA multiview: conflicting GPU fixture mode\n");
            failed = true;
            return;
        }
        if (prismaMultiViewVerify || prismaStressVerify || prismaGhostVerify) {
            // A test must explicitly turn on canonical temporal; the legacy
            // ScreenLighting TAA would test a different history owner.
            const auto enabled=[](const char* key) {
                const char* value=std::getenv(key);
                return value && std::strcmp(value,"1")==0;
            };
            if (!enabled("AIVERSE_PRISMA_TEMPORAL") ||
                !enabled("AIVERSE_PRISMA_JITTER")) {
                std::fprintf(stderr, "PRISMA multiview: TEMPORAL and JITTER must be 1\n");
                failed=true;
                return;
            }
        }
        if (advanced && !screen.init(d,post)) { failed=true; return; }
        screen.setEnabled(advanced);renderer.setModelReflectionsEnabled(advanced);
        renderer.setShadowQuality(ShadowQuality::High);
        renderer.setMaterialQuality(MaterialQuality::High);
        renderer.setTraceEnabled(true);
        renderer.lights.setQuality(LightQuality::High);
        renderer.lighting.fogDensity = 0;
        renderer.lighting.sunIntensity = 2;
        camera.target = {0, 0.8f, 0}; camera.distance = 17; camera.pitch = 0.7f; camera.yaw = 0.18f;
        floor = MeshBuilder().box({0,-0.16f,0}, {18,0.2f,14}).upload(d);
        cube = MeshBuilder().box({0,0.5f,0}, {1,1,1}).upload(d);
        water = MeshBuilder().quad({-5,0,-1},{-5,0,4},{5,0,4},{5,0,-1}).upload(d);
        MeshBuilder sphere; sphere.sphere({0,0,0},0.7f,24,32);
        std::vector<ModelPart> parts;
        for (int i=0;i<5;++i) {
            Material m; m.baseColor={0.7f,0.38f,0.12f,1}; m.metallic=i/4.0f; m.roughness=0.1f+i*0.19f;
            parts.push_back({"sphere",&sphere,m,mat4::translate({float(i-2)*2.0f,0.75f,-2.5f})});
        }
        if(const char* library=std::getenv("AIVERSE_MATERIAL_LIBRARY")) {
            materialGallery=true;parts.clear();auto materials=Json::parseFile(library)["materials"];
            int i=0;for(const auto& [name,data]:materials.members()){
                Material m;m.name=name;m.vertexColors=false;m.baseColor={1,1,1,1};
                m.shaderGraph=std::make_shared<graph::Program>(graph::compile(data["shaderGraph"],"material"));
                parts.push_back({name,&sphere,m,mat4::translate({float(i%3-1)*2.6f,.75f,float(i/3-1)*2.6f})});
                std::printf("Material gallery %d: %s\n",i++,name.c_str());
            }
            camera.target={0,.4f,0};camera.distance=14;camera.pitch=.9f;camera.yaw=0;
            renderer.lighting.sunColor={1,1,1};renderer.lighting.skyColor={.5f,.55f,.65f};
        }
        spheres=modelFromParts("hdr material chart",parts); uploadModel(d,*spheres);
        post.configure(Json::parse(R"({"hdr":"auto","effects":[{"type":"bloom","threshold":0.9,"intensity":0.35},{"type":"fxaa"},{"type":"colorGrade","exposure":-1}]})"),Json());
        post.setQuality(Prisma::defaultQualityFor(d,false));
        if (prismaMultiViewVerify || prismaStressVerify || prismaGhostVerify) {
            post.setSettings(parsePrisma(Json::parse(R"({"hdr":"on","effects":[{"type":"temporalResolve","historyWeight":0.86,"jitterPixels":0.5,"minQuality":"high"}]})"),Json()));
            post.setQuality(PrismaQuality::High);
        }
        verify = std::getenv("AIVERSE_HDR_VERIFY") != nullptr;
        if (verify || temporalVerify) {
            if (!checkHdrShaders(d)) { failed=true; return; }
#ifdef _WIN32
            _putenv_s("AIVERSE_HDR", "");
#else
            unsetenv("AIVERSE_HDR");
#endif
            MeshBuilder plane; plane.quad({-100,-100,0},{100,-100,0},{100,100,0},{-100,100,0});
            Material m; m.shading=ShadingModel::Unlit;m.vertexColors=false;m.castShadow=false;
            probeBg=modelFromParts("probe background",{{"plane",&plane,m,{}}});
            m.alpha=AlphaMode::Blend;
            probeFg=modelFromParts("probe foreground",{{"plane",&plane,m,{}}});
            Image normals;normals.width=normals.height=256;normals.repeat=true;normals.rgba.resize(256*256*4);
            for(int y=0;y<256;++y)for(int x=0;x<256;++x){size_t i=size_t(y*256+x)*4;normals.rgba[i]=x%2?204:51;normals.rgba[i+1]=128;normals.rgba[i+2]=230;normals.rgba[i+3]=255;}
            probeBg->images.push_back(std::move(normals));
            Image occlusion;occlusion.width=occlusion.height=1;occlusion.rgba={0,0,0,255};probeBg->images.push_back(std::move(occlusion));
            for(auto& v:probeBg->vertices)v.color=(v.color&0xffffffu)|(64u<<24);
            uploadModel(d,*probeBg);uploadModel(d,*probeFg);
            renderer.setShadowQuality(ShadowQuality::Off);renderer.lights.setQuality(LightQuality::Off);
            camera.target={0,0,0};camera.pitch=0;camera.yaw=0;camera.distance=10;
        }
        if(std::getenv("AIVERSE_CONTACT_SHADOW_TEST")) initContactShadowScene(*this);
        if(const char* skyCase=std::getenv("AIVERSE_PHYSICAL_SKY_TEST")) {
            advanced=false;renderer.setModelReflectionsEnabled(false);
            post.configure(Json::parse(R"({"hdr":"on","physicalSky":{"enabled":true},"effects":[{"type":"fxaa"}]})"),{});
            post.setQuality(PrismaQuality::High);post.setPhysicalSkyTime(0);
            camera.target={0,5,0};camera.distance=20;camera.pitch=-.12f;camera.yaw=0;
            renderer.lighting.sunDir={.15f,.22f,-1};renderer.lighting.sunIntensity=1;
            renderer.lighting.sunColor={1,1,1};
            auto sky=post.physicalSky();sky.coverage=.53f;
            const std::string which=skyCase;
            if(which=="clear")sky.clouds=false;
            if(which=="medium")post.setQuality(PrismaQuality::Medium);
            if(which=="ortho")camera.ortho=true;
            if(which=="night")renderer.lighting.sunDir={.15f,-.3f,-1};
            if(which=="no-rays")sky.godRays=false;
            if(which=="sunset")renderer.lighting.sunDir={.15f,.025f,-1};
            if(which=="inside"){camera.target.y=2200;camera.pitch=.05f;}
            if(which=="above"){camera.target.y=4000;camera.pitch=.35f;}
            post.setPhysicalSky(sky);
            if(which=="physis-dry"||which=="physis-storm") {
                World weatherWorld;physis::Settings config;config.width=config.height=1;
                physis::initialize(weatherWorld,config);
                const Vec3 location{1_fx,0_fx,1_fx};
                auto& atmosphere=weatherWorld.get<physis::Atmosphere>(physis::cellAtPosition(weatherWorld,location));
                atmosphere.humidity=which=="physis-dry"?0_fx:1_fx;
                atmosphere.rain=which=="physis-dry"?0_fx:1_fx;
                atmosphere.wind={10_fx,-2_fx};
                world::PhysisSkyBinding binding;
                world::applyPhysisSkyFrame(post,binding.update(weatherWorld,location,1,1.0/30,sky));
            }
        }
        if(const char* fogCase=std::getenv("AIVERSE_LOCAL_FOG_TEST")) {
            advanced=false;renderer.setModelReflectionsEnabled(false);
            post.configure(Json::parse(R"({"hdr":"on","effects":[{"type":"fxaa"}]})"),{});
            post.setQuality(PrismaQuality::High);
            LocalFogSettings settings;settings.enabled=true;post.setLocalFog(settings);
            const std::string which=fogCase;
            FogVolume volume;volume.id=1;volume.region.center={0,1,0};volume.region.radius=7;
            volume.region.feather=2;volume.density=.3f;volume.color={.65f,.78f,.95f};
            if(which=="obb"||which=="obb-reveal") {
                volume.region.shape=FogShape::Obb;volume.region.halfExtents={6,3,2};
                volume.region.orientation={0,.38268343f,0,.92387953f};
            }
            if(which=="behind"){volume.region.center={0,-4,0};volume.region.radius=1;volume.region.feather=.1f;}
            if(which=="inside"){camera.target={0,1,0};camera.distance=1;camera.pitch=.1f;}
            if(!post.localFog().upsert(volume)){failed=true;return;}
            if(which=="reveal"||which=="obb-reveal"||which=="move") {
                FogReveal reveal;reveal.id=1;reveal.region=volume.region;reveal.region.feather=0;post.localFog().upsert(reveal);
            }
            if(which=="off") { settings.enabled=false;post.setLocalFog(settings); }
            if(which=="medium")post.setQuality(PrismaQuality::Medium);
            if(which=="ortho")camera.ortho=true;
            if(which=="combined"){PhysicalSkySettings sky;sky.enabled=true;post.setPhysicalSky(sky);post.setPhysicalSkyTime(0);}
        }
        const char* p=std::getenv("AIVERSE_HDR_CAPTURE"); path=p?p:"/tmp/hdr-scene.ppm";
    }
    void draw(gfx::Device& d, int w, int h) {
        if (failed) return;
        if (prismaMultiViewVerify) { drawPrismaMultiView(d,w,h); return; }
        if (prismaStressVerify) { drawPrismaStress(d,w,h); return; }
        if (prismaGhostVerify) { drawPrismaGhostPixels(d,w,h); return; }
        if (temporalVerify) { drawTemporal(d,w,h); return; }
        if (verify) { drawProbe(d,w,h); return; }
        if(const char* fogCase=std::getenv("AIVERSE_LOCAL_FOG_TEST"); fogCase&&frame==3&&std::string(fogCase)=="move") {
            FogReveal reveal=post.localFog().reveals()[0];reveal.region.center={100,0,0};
            if(!post.localFog().upsert(reveal)){failed=true;return;}
        }
        if(ownsFrame)d.beginFrame(w,h);
        renderer.beginScene(advanced?screen.jittered(camera,w,h):camera,w,h);
        renderer.drawMesh(floor,{}, {0.34f,0.36f,0.4f,1});
        if(std::getenv("AIVERSE_CONTACT_SHADOW_TEST")) drawContactShadowScene(*this); else {
        ModelDraw md; md.model=spheres.get(); renderer.drawModel(md);
        if(!materialGallery){
        // Same albedo, increasing radiance: LDR clamps before bloom, HDR retains differences.
        for(int i=0;i<5;++i) renderer.drawMesh(cube,mat4::translate({float(i-2)*2,0,-4.5f}),{0.6f,0.24f,0.06f,1},kDrawUnlit,0,float(1<<i));
        renderer.drawMesh(cube,mat4::translate({-2.8f,0,1}),{0.08f,0.5f,0.95f,0.5f},kDrawUnlit,0,0);
        renderer.drawMesh(cube,mat4::translate({-2.3f,0,1.3f}),{0.95f,0.15f,0.1f,0.5f},kDrawUnlit,0,0);
        renderer.drawMesh(water,mat4::translate({0,0.12f,0}),{0.08f,0.16f,0.22f,0.65f},kDrawNoCastShadow,0.9f);
        renderer.lights.add({2,1.5f,0.8f},4,{0.15f,0.5f,1},3);
        }
        }
        gfx::PassDesc pass = output; pass.clearColor[0]=0.09f;pass.clearColor[1]=0.11f;pass.clearColor[2]=0.15f;
        renderer.render(pass,pipeline());
        const bool hdr=post.linearScene();
        renderer.resolvePrisma();
        if(frame==5) {
            std::printf("scene=%s, post passes=%d, ssr=%d, reflection=%d\n",hdr?"HDR":"LDR",post.stats().passes,renderer.ssrActive(),renderer.lights.stats().reflection);
            auto receive = [this](int cw,int ch,std::vector<uint8_t> px){
                if(cw<=0||ch<=0||px.size()!=size_t(cw*ch*4)) { failed=true; return; }
                std::ofstream f(path,std::ios::binary); f<<"P6\n"<<cw<<" "<<ch<<"\n255\n";
                for(size_t i=0;i<px.size();i+=4) f.write(reinterpret_cast<const char*>(px.data()+i),3);
                captured=bool(f); std::printf("capture: %s\n",path.c_str());
            };
            if (readOutput) readOutput(receive); else d.captureNextFrame(receive);
        }
        d.endPass();
        if(ownsFrame)d.endFrame();
        ++frame;
        if (!renderer.modelError().empty() || !post.lastError().empty() || !screen.lastError().empty()) {
            std::fprintf(stderr,"renderer: %s; post: %s\n",renderer.modelError().c_str(),post.lastError().c_str()); failed=true;
        }
    }
    // Stage 97: exercise real Renderer3D + canonical PRISMA GPU passes for two
    // stable camera IDs, equal-sized shared cache, cut, resize, and GPU-fenced
    // retirement. Checks describe encoded passes and CPU-visible RHI metadata;
    // they are NOT a per-pixel moving-object/ghosting or fence-completion proof.
    void drawPrismaMultiView(gfx::Device& d,int w,int h) {
        constexpr uint64_t a=1001,b=2002;
        const int step=frame;
        // The offscreen GL runner owns its frames. On macOS Metal the
        // Platform host owns beginFrame/endFrame for the callback.
        if (ownsFrame) d.beginFrame(w,h);
        auto fail=[this](const char* msg) {
            std::fprintf(stderr,"PRISMA multiview FAIL step=%d: %s\n",frame,msg);
            failed=true;
        };
        auto ownsHistory=[this](uint64_t id,int width,int height) {
            const auto memory=post.memoryStats();
            for(const auto& view:memory.views)
                if(view.viewId==id)
                    return view.width==width && view.height==height &&
                        view.ownedTemporalTextures==2;
            return false;
        };
        auto encoded=[this](uint64_t id,int width,int height,uint64_t sequence) {
            const auto* report=post.effectReport(id);
            if(!report || report->viewId!=id || report->sequence!=sequence ||
               report->width!=width || report->height!=height ||
               !report->sceneReady || !report->hdr)
                return false;
            for(const auto& effect:report->effects)
                if(effect.type==PrismaType::TemporalResolve)
                    return effect.requested && effect.pipelinePrepared &&
                        effect.encoded;
            return false;
        };
        const uint64_t epochA=post.objectMotionEpoch(a);
        const uint64_t epochB=post.objectMotionEpoch(b);
        if(step==3) {
            // Both renderer deformation and PRISMA camera history see the cut.
            post.resetMotionHistory(a);
            renderer.resetObjectMotionHistory(a);
            if(post.objectMotionEpoch(a)<=epochA ||
               post.objectMotionEpoch(b)!=epochB)
                fail("cut did not invalidate only camera A");
        }
        auto renderView=[&](uint64_t id,int width,int height,float x) {
            if(failed)return;
            post.setViewId(id);
            const auto size=post.renderSizeForOutput(width,height);
            if(size.first!=width || size.second!=height) {
                fail("unexpected temporal render-size fallback");return;
            }
            auto cam=camera;
            cam.yaw+=x;
            auto renderCam=post.jitteredCamera(cam,size.first,size.second);
            renderer.beginScene(renderCam,size.first,size.second);
            renderer.drawMesh(cube,mat4::translate({x,0,0}),
                              {0.7f,0.32f,0.16f,1},kDrawUnlit);
            auto pass=output;
            pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;
            renderer.render(pass,&post);
            renderer.resolvePrisma();
            d.endPass(); // resolve leaves the presentation pass open
            if(!post.stats().temporalResolved ||
               !post.lastError().empty() || !renderer.modelError().empty()) {
                std::fprintf(stderr,"PRISMA multiview render error: %s; model: %s\n",
                             post.lastError().c_str(),renderer.modelError().c_str());
                fail("real temporal GPU pass not encoded");
            }
        };
        if(step<=4) {
            renderView(a,640,480,0.f);
            renderView(b,step>=4?512:640,step>=4?384:480,0.1f);
            if(!failed && (!encoded(a,640,480,static_cast<uint64_t>(step+1)) ||
                !encoded(b,step>=4?512:640,step>=4?384:480,
                         static_cast<uint64_t>(step+1))))
                fail("per-view temporal effect reports not isolated");
            if(!failed && (!ownsHistory(a,640,480) ||
                !ownsHistory(b,step>=4?512:640,step>=4?384:480)))
                fail("private camera temporal history missing/incorrect dimensions");
            const auto retire=post.retirementStats();
            if(!failed && (retire.temporalViews!=2 || retire.activeSizes<(step>=4?2u:1u)))
                fail("two camera histories or size caches not tracked");
            if(step==3 && !failed && post.objectMotionEpoch(b)!=epochB)
                fail("camera A cut changed B's object-motion epoch");
            if(step==4 && !failed)
                std::printf("PRISMA multiview: resize B 640x480 -> 512x384; "
                            "fenced=%zu quarantined=%zu\n",
                            retire.fencedTextures,retire.unfencedTextures);
        } else if(step==5) {
            renderView(a,640,480,0.f);
            const size_t retired=post.retireUnregisteredViews({a});
            const size_t sizes=post.retireUnreferencedSizeCaches();
            const auto stats=post.retirementStats();
            if(retired<1 || sizes<1 || stats.temporalViews!=1 ||
               post.effectReport(b)!=nullptr || !ownsHistory(a,640,480))
                fail("closing B affected A or failed to retire its unique size");
            std::printf("PRISMA multiview: retire B=%zu sizes=%zu; "
                        "fenced=%zu quarantined=%zu (not GPU-completed proof)\n",
                        retired,sizes,stats.fencedTextures,stats.unfencedTextures);
        } else if(step==6) {
            post.retireUnregisteredViews({});
            post.retireUnreferencedSizeCaches();
            post.collectGpuRetirements(); // query real completed fence, nonblocking
            const auto stats=post.retirementStats();
            if(stats.temporalViews!=0 || stats.activeSizes!=0 ||
               post.effectReport(a)!=nullptr || !post.memoryStats().views.empty())
                fail("all cameras closed but active history/cache remained");
            if(!failed) {
                std::printf("PRISMA multiview: PASS cameras=2 cut=1 resize=1 "
                            "lifecycle=2 fenced=%zu quarantined=%zu "
                            "completedFence=%llu\n",
                            stats.fencedTextures,stats.unfencedTextures,
                            static_cast<unsigned long long>(stats.lastGpuCompletedFence));
                captured=true;
            }
        } else {
            fail("unexpected frame beyond test completion");
        }
        if (ownsFrame) d.endFrame();
        ++frame;
    }

    // Stage 98: two independent 24-frame camera churn cycles, each followed
    // by real backend GPU completion and ZERO pending resources. No CPU frame
    // number is used as proof of GPU completion; this is still a metadata/
    // allocation test, not a physical VRAM residency query.
    void drawPrismaStress(gfx::Device& d,int w,int h) {
        constexpr uint64_t a=31001, b=32002;
        if(ownsFrame) d.beginFrame(w,h);
        const auto fail=[this](const char* msg) {
            std::fprintf(stderr,"PRISMA stress FAIL cycle=%d tick=%d: %s\n",
                         prismaStressCycle,prismaStressTicks,msg);
            failed=true;
        };
        if(prismaStressTicks<24 && !prismaStressSubmittedFence) {
            auto drawView=[&](uint64_t id,int width,int height,float offset) {
                post.setViewId(id);
                const auto dims=post.renderSizeForOutput(width,height);
                if(dims.first!=width || dims.second!=height) {
                    fail("temporal render dimensions unavailable");return;
                }
                auto cam=camera;
                cam.yaw+=offset;
                renderer.beginScene(post.jitteredCamera(cam,width,height),width,height);
                renderer.drawMesh(cube,mat4::translate({offset,0,0}),
                                  {0.7f,0.24f,0.12f,1},kDrawUnlit);
                gfx::PassDesc out=output;
                out.clearColor[0]=out.clearColor[1]=out.clearColor[2]=0;
                renderer.render(out,&post);
                renderer.resolvePrisma();
                d.endPass();
                if(!post.stats().temporalResolved || !post.lastError().empty())
                    fail("GPU temporal pass not encoded");
            };
            if(prismaStressTicks%5==0) {
                post.resetMotionHistory(a);
                renderer.resetObjectMotionHistory(a);
            }
            if(prismaStressTicks%7==0) {
                post.resetMotionHistory(b);
                renderer.resetObjectMotionHistory(b);
            }
            drawView(a,640,480,0.f);
            constexpr int widths[4]={512,640,384,768};
            constexpr int heights[4]={384,480,288,512};
            const int choice=prismaStressTicks%4;
            if(!failed) drawView(b,widths[choice],heights[choice],0.1f);
            const auto retired=post.retireUnreferencedSizeCaches();
            post.collectGpuRetirements();
            renderer.collectCompletedObjectMotion(d.gpuCompletedFence());
            const auto mem=post.memoryStats();
            prismaStressMaxLiveBytes=std::max(prismaStressMaxLiveBytes,
                                             mem.liveKnownBytes);
            const auto active=post.retirementStats();
            if(!failed && (active.temporalViews!=2 || active.activeSizes>3))
                fail("unbounded active camera or shared size-cache count");
            if(!failed && prismaStressTicks==23) {
                const auto views=post.retireUnregisteredViews({});
                renderer.retireUnregisteredObjectMotionViews({});
                post.retireUnreferencedSizeCaches();
                prismaStressSubmittedFence=d.gpuSubmissionFence();
                if(!prismaStressSubmittedFence || views!=2)
                    fail("no GPU submission fence or missing registered views");
                std::printf("PRISMA stress: cycle=%d retirementFence=%llu "
                            "maxKnownLive=%llu pending=%zu\n",prismaStressCycle,
                            static_cast<unsigned long long>(prismaStressSubmittedFence),
                            static_cast<unsigned long long>(prismaStressMaxLiveBytes),
                            post.retirementStats().fencedTextures);
            }
            ++prismaStressTicks;
        } else {
            // Empty submissions allow the previous GPU work to complete. The
            // backend token must advance through the LAST retired frame.
            post.collectGpuRetirements();
            renderer.collectCompletedObjectMotion(d.gpuCompletedFence());
            const auto stats=post.retirementStats();
            const auto motion=renderer.motionRetirementStats();
            const auto memory=post.memoryStats();
            const bool completed=prismaStressSubmittedFence!=0 &&
                d.gpuCompletedFence()>=prismaStressSubmittedFence;
            const bool drained=completed && stats.retiredSizes==0 &&
                stats.fencedTextures==0 && stats.unfencedTextures==0 &&
                stats.graphRetiredTextures==0 && stats.knownPendingBytes==0 &&
                stats.activeSizes==0 && stats.temporalViews==0 &&
                motion.fencedTextures==0 && motion.unfencedTextures==0 &&
                motion.liveViews==0 &&
                memory.views.empty() && memory.liveKnownBytes==0;
            if(drained) {
                std::printf("PRISMA stress: drained cycle=%d completedFence=%llu "
                            "liveBytes=%llu pendingBytes=%llu\n",prismaStressCycle,
                            static_cast<unsigned long long>(d.gpuCompletedFence()),
                            static_cast<unsigned long long>(memory.liveKnownBytes),
                            static_cast<unsigned long long>(stats.knownPendingBytes));
                ++prismaStressCycle;
                prismaStressTicks=0;
                prismaStressWait=0;
                prismaStressSubmittedFence=0;
                prismaStressMaxLiveBytes=0;
                if(prismaStressCycle==2) {
                    std::printf("PRISMA stress: PASS cycles=2 gpuFenceDrained=2 "
                                "liveKnownBytes=0 pendingKnownBytes=0\n");
                    captured=true;
                }
            } else if(++prismaStressWait>120) {
                std::fprintf(stderr,
                    "PRISMA stress pending: completed=%llu needed=%llu "
                    "fenced=%zu unfenced=%zu size=%zu motion=%zu "
                    "knownPending=%llu live=%llu\n",
                    static_cast<unsigned long long>(d.gpuCompletedFence()),
                    static_cast<unsigned long long>(prismaStressSubmittedFence),
                    stats.fencedTextures,stats.unfencedTextures,
                    stats.retiredSizes,motion.fencedTextures,
                    static_cast<unsigned long long>(stats.knownPendingBytes),
                    static_cast<unsigned long long>(memory.liveKnownBytes));
                fail("retired textures not reclaimed after confirmed GPU fence");
            }
        }
        if(ownsFrame) d.endFrame();
        ++frame;
    }

    // Stage 98: a visible unlit cube moves from one side of the camera to
    // the other across an explicit camera cut. Read actual backend output
    // pixels before and after the cut; residual red coverage in the old
    // location must be bounded. This is a cut/ghost test, NOT certification
    // for every motion-vector, transparency or disocclusion scenario.
    void drawPrismaGhostPixels(gfx::Device& d,int w,int h) {
        constexpr uint64_t cameraId=41001;
        if(ownsFrame) d.beginFrame(w,h);
        const int tick=frame;
        if(tick<=5) {
            if(tick==3) {
                post.resetMotionHistory(cameraId);
                renderer.resetObjectMotionHistory(cameraId);
            }
            post.setViewId(cameraId);
            const auto dims=post.renderSizeForOutput(w,h);
            if(dims.first!=w || dims.second!=h) {
                std::fprintf(stderr,"PRISMA ghost FAIL: render dimensions unavailable\n");
                failed=true;
            } else {
                auto cam=camera;
                cam.yaw=0.f;cam.pitch=0.2f;cam.distance=12.f;
                cam.target={0,0.5f,0};
                renderer.beginScene(post.jitteredCamera(cam,w,h),w,h);
                const float x=tick<3?-2.f:2.f;
                renderer.drawMesh(cube,mat4::translate({x,0,0}),
                                  {1.f,0.015f,0.01f,1.f},kDrawUnlit);
                gfx::PassDesc out=output;
                out.clearColor[0]=0.015f;
                out.clearColor[1]=0.025f;
                out.clearColor[2]=0.045f;
                renderer.render(out,&post);
                renderer.resolvePrisma();
                if(!post.stats().temporalResolved || !post.lastError().empty()) {
                    std::fprintf(stderr,"PRISMA ghost FAIL: temporal GPU pass missing: %s\n",
                                 post.lastError().c_str());
                    failed=true;
                }
                if(tick==2 || tick==5) {
                    const bool before=tick==2;
                    auto receive=[this,before](int cw,int ch,std::vector<uint8_t> pixels) {
                        GhostRedSample sample;
                        sample.width=cw;
                        if(cw>0 && ch>0 &&
                           pixels.size()==size_t(cw)*size_t(ch)*4) {
                            sample.columns.assign(size_t(cw),0);
                            for(int y=ch/4;y<3*ch/4;++y)
                                for(int x=cw/8;x<7*cw/8;++x) {
                                    const size_t i=(size_t(y)*size_t(cw)+size_t(x))*4;
                                    const int red=pixels[i];
                                    const int green=pixels[i+1];
                                    const int blue=pixels[i+2];
                                    if(red>95 && red>green+45 && red>blue+45) {
                                        ++sample.count;
                                        sample.sumX+=double(x);
                                        ++sample.columns[size_t(x)];
                                    }
                                }
                        }
                        sample.ready=true;
                        std::lock_guard<std::mutex> lock(ghostSampleMutex_);
                        if(before) ghostBefore_=std::move(sample);
                        else ghostAfter_=std::move(sample);
                    };
                    if(readOutput) readOutput(receive);
                    else d.captureNextFrame(receive);
                }
                d.endPass();
            }
        } else {
            // The CPU must not assume that an asynchronous Metal capture
            // callback has completed immediately after endFrame.
            GhostRedSample oldSample,newSample;
            {
                std::lock_guard<std::mutex> lock(ghostSampleMutex_);
                if(ghostBefore_.ready && ghostAfter_.ready) {
                    oldSample=ghostBefore_;newSample=ghostAfter_;
                }
            }
            if(oldSample.ready && newSample.ready) {
                const double oldX=oldSample.count>0 ?
                    oldSample.sumX/double(oldSample.count) : 0.0;
                const double newX=newSample.count>0 ?
                    newSample.sumX/double(newSample.count) : 0.0;
                int residual=0;
                const int radius=oldSample.width/22;
                for(int x=0;x<newSample.width;++x)
                    if(std::abs(double(x)-oldX)<double(radius))
                        residual+=newSample.columns[size_t(x)];
                const bool ok=oldSample.width==newSample.width &&
                    oldSample.count>30 && newSample.count>30 &&
                    std::abs(newX-oldX)>double(oldSample.width)*0.05 &&
                    residual<std::max(16,oldSample.count/10);
                std::printf("PRISMA ghost: oldPixels=%d newPixels=%d "
                            "centroidDelta=%.2f residualOld=%d %s\n",
                            oldSample.count,newSample.count,
                            std::abs(newX-oldX),residual,ok?"PASS":"FAIL");
                captured=ok;failed=!ok;
            } else if(++ghostFrameWait_>120) {
                std::fprintf(stderr,"PRISMA ghost FAIL: GPU capture callbacks missing\n");
                failed=true;
            }
        }
        if(ownsFrame) d.endFrame();
        ++frame;
    }

    void drawTemporal(gfx::Device& d,int w,int h) {
        PrismaSettings settings;settings.hdr=HdrMode::On;post.setSettings(settings);post.setQuality(PrismaQuality::High);
        screen.setDiffuseBounceEnabled(false);renderer.lighting.tonemap=false;
        probeBg->materials[0].baseColor={1,1,1,1};
        if(ownsFrame)d.beginFrame(w,h);
        renderer.beginScene(screen.jittered(camera,w,h),w,h);
        ModelDraw bar;bar.model=probeBg.get();bar.world=mat4::translate({.123f,.057f,0})*mat4::scale({.01f,.02f,.01f});renderer.drawModel(bar);
        auto pass=output;pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;
        renderer.render(pass,pipeline());renderer.resolvePrisma();
        if(frame>=40&&frame<56){
            const int sample=frame;
            auto receive=[this,sample](int cw,int ch,std::vector<uint8_t> px){
                auto linear=[](float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);};
                double sx=0,wx=0,sy=0,wy=0;int transition=0;
                for(int x=0;x<cw;++x){float v=linear(px[size_t((ch/2)*cw+x)*4]/255.0f);sx+=(x+.5)*v;wx+=v;if(v>.05f&&v<.95f)++transition;}
                for(int y=0;y<ch;++y){float v=linear(px[size_t(y*cw+cw/2)*4]/255.0f);sy+=(y+.5)*v;wy+=v;}
                if(wx<=0||wy<=0){failed=true;return;}
                float x=float(sx/wx),y=float(sy/wy);temporalMinX=std::min(temporalMinX,x);temporalMaxX=std::max(temporalMaxX,x);temporalMinY=std::min(temporalMinY,y);temporalMaxY=std::max(temporalMaxY,y);
                if(transition>8)failed=true;
                if(sample==55){float dx=temporalMaxX-temporalMinX,dy=temporalMaxY-temporalMinY;
                    bool ok=dx<.25f&&dy<.25f&&!failed;std::printf("TAA stationary centroid drift: x=%.4f y=%.4f pixels, edge transition=%d: %s\n",dx,dy,transition,ok?"PASS":"FAIL");failed=!ok;captured=true;}
            };
            if(readOutput)readOutput(receive);else d.captureNextFrame(receive);
        }
        d.endPass();if(ownsFrame)d.endFrame();++frame;
    }
    void drawProbe(gfx::Device& d, int w, int h) {
        const bool legacy = probeIndex==3 || probeIndex==4;
        PrismaSettings settings;
        settings.hdr=probeIndex==3?HdrMode::Off:HdrMode::On;
        if(probeIndex==1 || probeIndex==6) settings=parsePrisma(Json::parse(R"({"hdr":"on","effects":[{"type":"colorGrade","exposure":-2}]})"),Json());
        if(probeIndex==6) settings=parsePrisma(Json::parse(R"({"hdr":"on","effects":[{"type":"bloom","threshold":2,"intensity":1},{"type":"colorGrade","exposure":-2}]})"),Json());
        post.setSettings(settings);post.setQuality(probeIndex==4?PrismaQuality::Low:PrismaQuality::High);
        renderer.lighting.tonemap=probeIndex==1 || probeIndex==6;
        const float value=(probeIndex==1 || probeIndex==6)?4.0f:0.25f;
        probeBg->materials[0].baseColor={value,value,value,1};
        probeFg->materials[0].baseColor={0.75f,0.75f,0.75f,0.5f};
        if(probeIndex>=7){
            renderer.lighting.sunIntensity=0;renderer.lighting.skyColor={0,0,0};renderer.lighting.groundColor={0,0,0};
            auto& material=probeBg->materials[0];material.shading=ShadingModel::Pbr;material.baseColor={1,1,1,1};material.roughness=1;material.metallic=0;
            renderer.lights.setQuality(probeIndex==15?LightQuality::Medium:LightQuality::High);
            if(probeIndex>=29){renderer.setMaterialQuality(probeIndex==29?MaterialQuality::High:MaterialQuality::Low);renderer.lighting.skyColor={.5f,.5f,.5f};renderer.lighting.groundColor={.5f,.5f,.5f};}
            if(probeIndex==34||probeIndex>=36){renderer.lighting.sunDir={0,0,1};renderer.lighting.sunColor={1,1,1};renderer.lighting.sunIntensity=.25f;}
            if(gridProbe!=probeIndex){gridProbe=probeIndex;PointLight light;light.pos={0,0,probeIndex==8?3.0f:1.0f};light.radius=2;light.color={1,1,1};light.intensity=1;
                std::vector<StaticPointShadow> shadows;
                if(probeIndex>=9){
                    StaticShadowGeometry geometry;float x=probeIndex==9?0:4;
                    geometry.triangles={{{x-1,-1,.5f},{x+1,-1,.5f},{x+1,1,.5f}},{{x-1,-1,.5f},{x+1,1,.5f},{x-1,1,.5f}}};
                    geometry.build();shadows.push_back(geometry.bake(light.pos,light.radius,32));
                    if(probeIndex>=11)shadows[0].emitterNormal={0,0,probeIndex==11?1.0f:-1.0f};
                }
                StaticDiffuseField diffuse;
                if(probeIndex>=13){
                    light.pos={0,0,.2f};light.radius=.5f;light.intensity=0;
                    diffuse.origin={-1.6f,-1.6f,-1.6f};diffuse.spacing=1;diffuse.nx=diffuse.ny=diffuse.nz=3;
                    if(probeIndex>=21&&probeIndex<=26){const float origins[6][3]={{-1.1f,-1.3f,-1.7f},{-1.1f,-1.7f,-1.3f},{-1.3f,-1.1f,-1.7f},{-1.3f,-1.7f,-1.1f},{-1.7f,-1.1f,-1.3f},{-1.7f,-1.3f,-1.1f}};const auto& o=origins[probeIndex-21];diffuse.origin={o[0],o[1],o[2]};}
                    diffuse.probes.resize(27);
                    for(auto& probe:diffuse.probes){for(auto& coefficient:probe.irradiance)coefficient={.2f,0,0,0};
                        for(auto& c:probe.sun)c={.1f,0,0,0};
                        for(auto& c:probe.sky)c={.05f,0,0,0};
                        probe.visibility.resolution=4;probe.visibility.distances.assign(96,(probeIndex==14||probeIndex==16)?0.0f:5.0f);probe.visibility.normals.resize(96);}
                    if(probeIndex>=21&&probeIndex<=26)for(int z=0;z<3;++z)for(int y=0;y<3;++y)for(int x=0;x<3;++x){auto p=diffuse.origin+float3{x+.5f,y+.5f,z+.5f};float value=.2f+.01f*p.x+.02f*p.y+.03f*p.z;for(auto& c:diffuse.probes[size_t(x+3*(y+3*z))].irradiance)c={value,0,0,0};}
                }
                const int planeProbe=probeIndex==41?27:probeIndex==42?28:probeIndex;
                if(planeProbe>=27&&planeProbe<=28)for(int z=0;z<3;++z)for(int y=0;y<3;++y)for(int x=0;x<3;++x){
                    auto p=diffuse.origin+float3{x+.5f,y+.5f,z+.5f};auto n=normalize(p*-1.0f);float plane=length(p)*(planeProbe==27?.5f:1.5f);
                    auto& vis=diffuse.probes[size_t(x+3*(y+3*z))].visibility;
                    for(int face=0;face<6;++face)for(int py=0;py<4;++py)for(int px=0;px<4;++px){auto ray=StaticShadowGeometry::cubeDirection(face,2*(px+.5f)/4-1,2*(py+.5f)/4-1);float denom=dot(n,ray);size_t i=size_t(face*16+py*4+px);vis.distances[i]=denom>0?plane/denom:100;vis.normals[i]=denom>0?n:float3{};}
                }
                if(probeIndex>=29){diffuse.occludeAmbient=probeIndex!=33;
                    for(auto& p:diffuse.probes){p.irradiance={};if(probeIndex==35)for(auto& c:p.irradiance)c={.2f,0,0,0};p.sun={};p.sky={};p.skyVisibility=probeIndex==30?float4{.5f,0,.5f,0}:probeIndex==31?float4{.125f,0,.125f,0}:float4{};}
                }
                if(probeIndex>=41){diffuse.occludeAmbient=false;for(auto& p:diffuse.probes)for(auto& c:p.irradiance)c={.2f,0,0,0};}
                // The nearest visibility texel is blocked, but one corner of the footprint is open.
                // A second, dark unoccluded probe makes the fractional weight observable.
                if(probeIndex==43){
                    diffuse.origin={-1,-.5f,-.5f};diffuse.spacing=1;diffuse.nx=2;diffuse.ny=diffuse.nz=1;diffuse.probes.resize(2);
                    for(size_t i=0;i<2;++i){auto& p=diffuse.probes[i];p.irradiance={};p.sun={};p.sky={};p.skyVisibility={};
                        if(i==0)for(auto& c:p.irradiance)c={.2f,0,0,0};
                        p.visibility.resolution=4;p.visibility.distances.assign(96,i==0?0.0f:5.0f);p.visibility.normals.assign(96,{});
                        if(i==0)for(int face=0;face<6;++face)p.visibility.distances[size_t(face*16+5)]=5;
                    }
                }
                if(probeIndex==45){
                    for(auto& p:diffuse.probes)p.visibility.distances.assign(96,0);
                    // Only a corner outside the selected tetrahedron sees the receiver.
                    diffuse.probes[22].visibility.distances.assign(96,5);
                }
                // Moment visibility: 52 open (High), 53 fully blocked (Mobile floor, crush and fallback).
                if(probeIndex>=52)for(auto& p:diffuse.probes){float mean=probeIndex==52?5.0f:0.0f;
                    p.visibility.distances.assign(96,mean);p.visibility.squares.assign(96,mean*mean);p.visibility.normals.clear();}
                renderer.lights.setIndirectQuality((probeIndex==17||probeIndex==32)?IndirectQuality::Off:((probeIndex>=15&&probeIndex<41)||probeIndex==43||probeIndex==45||probeIndex==53)?IndirectQuality::Mobile:IndirectQuality::High);
                renderer.lights.setStaticRadiance(probeIndex==18?float3{.5f,.5f,.5f}:float3{1,1,1},probeIndex==19?float3{2,2,2}:float3{},probeIndex==19?float3{1,1,1}:float3{});
                if(probeIndex==20)renderer.lights.setStaticRadiance({0,0,0},{1,1,1},{0,0,0});
                if(probeIndex==44){
                    for(auto& p:diffuse.probes){for(auto& c:p.sun)c={.1f,0,0,0};for(auto& c:p.sky)c={.05f,0,0,0};}
                    StaticLightGrid combined;combined.build(std::vector<PointLight>{light},1,shadows.empty()?nullptr:&shadows,&diffuse);
                    combined.combineEnvironment({2,2,2},{1,1,1});renderer.lights.setIndirectQuality(IndirectQuality::Mobile);
                    renderer.lights.setStaticRadiance({1,1,1},{1,1,1},{});
                    if(!renderer.lights.setStaticLightGrid(combined)){failed=true;return;}
                }
                if(probeIndex==48){
                    StaticLightGrid grazing;grazing.build(std::vector<PointLight>{light},1,shadows.empty()?nullptr:&shadows,&diffuse);
                    for(int z=0;z<3;++z)for(int y=0;y<3;++y)for(int x=0;x<3;++x){
                        auto delta=(diffuse.origin+float3{x+.5f,y+.5f,z+.5f})*-1.0f;
                        auto normal=delta*(1/dot(delta,delta));size_t offset=size_t(grazing.texels[size_t(x+3*(y+3*z))].z);
                        for(size_t t=4;t<100;++t)grazing.texels[offset+t]={.01f,normal.x,normal.y,normal.z};
                    }
                    if(!renderer.lights.setStaticLightGrid(grazing)){failed=true;return;}
                }
                if(probeIndex!=20&&probeIndex!=44&&probeIndex!=48&&!renderer.lights.setStaticPointLights({light},1,shadows.empty()?nullptr:&shadows,diffuse.probes.empty()?nullptr:&diffuse)){failed=true;return;}}
        }
        // Rough receiver has no SSR contribution: the opaque redraw must preserve sun visibility.
        if(probeIndex>=36){renderer.setShadowQuality(ShadowQuality::High);renderer.setShadowBounds({-2,-2,-2},{2,2,2});renderer.setModelReflectionsEnabled(true);}
        if(probeIndex>=37){probeBg->materials[0].castShadow=true;probeBg->materials[0].doubleSided=true;}
        if(probeIndex>=39){
            auto& m=probeBg->materials[0];m.normalTex=0;m.normalScale=1;
            m.uvScale=probeIndex==40?float2{.0001f,.0001f}:float2{1,1};m.uvOffset={65.5f/256,.25f};
            renderer.setMaterialQuality(MaterialQuality::Medium);
        }
        if(probeIndex>=41){renderer.lighting.sunIntensity=0;renderer.lighting.skyColor={};renderer.lighting.groundColor={};probeBg->materials[0].normalTex=-1;renderer.setMaterialQuality(MaterialQuality::Low);}
        if(probeIndex>=46){probeBg->materials[0].occlusionTex=probeIndex>=48?-1:1;renderer.setMaterialQuality(MaterialQuality::High);}
        if(probeIndex==47||probeIndex==50)renderer.lighting.sunIntensity=.25f;
        if(probeIndex>=49){const bool ao=probeIndex<52;probeBg->materials[0].vertexOcclusion=ao;probeBg->materials[0].vertexColors=ao;}
        if(probeIndex==51)renderer.setMaterialQuality(MaterialQuality::Low);
        if(ownsFrame)d.beginFrame(w,h);
        if(probeIndex==5) { // alternate view sizes in the same frame, as the editor does
            renderer.beginScene(camera,w/2,h/2);
            ModelDraw small;small.model=probeBg.get();renderer.drawModel(small);
            renderer.render(output,pipeline());renderer.resolvePrisma();d.endPass();
        }
        renderer.beginScene(advanced?screen.jittered(camera,w,h):camera,w,h);
        ModelDraw bg;bg.model=probeBg.get();
        if(probeIndex==37)bg.world=mat4::rotate(quat::axisAngle({0,1,0},3.14159265f));
        renderer.drawModel(bg);
        if(probeIndex==2) {ModelDraw fg;fg.model=probeFg.get();fg.world=mat4::translate({0,0,1});renderer.drawModel(fg);}
        renderer.render(output,pipeline());renderer.resolvePrisma();
        if(frame==5) {
            const int test=probeIndex;
            float expected=probeIndex==2?0.5f:0.25f;
            if(probeIndex==1) expected=(2.51f+0.03f)/(2.43f+0.59f+0.14f);
            if(probeIndex==6) expected=(1.5f*(2.51f*1.5f+0.03f))/(1.5f*(2.43f*1.5f+0.59f)+0.14f);
            if(probeIndex==7||probeIndex==10)expected=0.28125f*(1+0.04f*0.64f); // N.L + rough specular at d=1, radius=2
            if(probeIndex==8||probeIndex==9||probeIndex==11)expected=0;
            if(probeIndex==12)expected=.28125f; // hemisphere facing receiver, diffuse only // same XY, outside the 3D radius
            if(probeIndex==13)expected=.2f;
            if(probeIndex==14||probeIndex==16||probeIndex==17)expected=0;
            if(probeIndex==15)expected=.2f;
            if(probeIndex==18)expected=.1f;
            if(probeIndex==19)expected=.45f;
            if(probeIndex==20)expected=.1f;
            if(probeIndex>=21)expected=.2f;
            if(probeIndex==27||probeIndex==29)expected=0;
            if(probeIndex==30||probeIndex==32||probeIndex==33)expected=.21404114f;
            if(probeIndex==31)expected=.21404114f*.25f;
            if(probeIndex==34||probeIndex>=36)expected=.25f*(1+.04f*9*.125f);
            if(probeIndex==35)expected=.2f;
            // Medium GGX at N=L=V: specular=F0/4. Near: preserve the sampled normal tilt.
            if(probeIndex>=39){float nz=1;
                if(probeIndex==40){float x=204.0f/255*2-1,y=128.0f/255*2-1,z=230.0f/255*2-1;nz=z/std::sqrt(x*x+y*y+z*z);}
                float vis=.25f/((nz*.5f+.5f)*(nz*.5f+.5f));
                expected=.25f*nz*(1+.04f*vis);
            }
            if(probeIndex>=41)expected=probeIndex==41?0:.2f;
            if(probeIndex==43)expected=.04f;
            if(probeIndex==44)expected=.45f;
            if(probeIndex==45)expected=.2f;
            if(probeIndex==46)expected=0;
            if(probeIndex==47)expected=.25f*1.01f;
            if(probeIndex==48)expected=.2f;
            if(probeIndex>=49&&probeIndex<52)expected=.2f*(64.0f/255)+(probeIndex==50?.25f*1.01f:0);
            if(probeIndex>=52)expected=.2f;
            expected=legacy?std::sqrt(expected):(expected<=0.0031308f?12.92f*expected:1.055f*std::pow(expected,1/2.4f)-0.055f);
            auto receive=[this,test,expected](int cw,int ch,std::vector<uint8_t> px) {
                if(cw<=0||ch<=0||px.size()!=size_t(cw*ch*4)) {failed=true;return;}
                float avg=0;for(int y=ch/2-2;y<ch/2+2;++y)for(int x=cw/2-2;x<cw/2+2;++x)for(int c=0;c<3;++c)avg+=px[size_t(y*cw+x)*4+c];
                avg/=48*255.0f;
                bool ok=std::fabs(avg-expected)<3/255.0f;
                std::printf("HDR GPU probe %d: got %.5f expected %.5f %s\n",test,avg,expected,ok?"PASS":"FAIL");
                if(!ok)failed=true;
                ++probeIndex;frame=0;
                if(probeIndex==54)captured=true;
            };
            if(readOutput)readOutput(receive);else d.captureNextFrame(receive);
        }
        d.endPass();if(ownsFrame)d.endFrame();++frame;
    }
    void shutdown(gfx::Device& d) {
        if(probeBg)releaseModel(d,*probeBg);
        if(probeFg)releaseModel(d,*probeFg);
        if(spheres) releaseModel(d,*spheres);
        for(auto* m:{&floor,&cube,&water}) {d.destroyBuffer(m->vb);d.destroyBuffer(m->ib);}
        screen.shutdown();post.shutdown(); renderer.shutdown();
    }
};
}

#include "ContactShadowScene.h"
