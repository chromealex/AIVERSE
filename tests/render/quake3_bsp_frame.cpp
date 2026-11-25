// Optical regression for the unmodified OpenArena Community Map Pack 1 level Egyptian Trouble.
#include "engine/platform/Platform.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/BspVisibility.h"
#include <chrono>
#include "engine/render/CameraPng.h"
#include "engine/render/RenderTargetPool.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>

using namespace aiverse;
using namespace aiverse::render;
namespace {
void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}
}
int aiverseMain(int,char**) {
    const auto* path=std::getenv("AIVERSE_OPENARENA_BSP");
    if (!path)return 77;
    constexpr int w=800,h=500;
    Renderer3D renderer;
    CameraRenderTargetPool targets;
    std::unique_ptr<Model> level;
    std::vector<Material> plain;
    std::array<std::vector<uint8_t>,3> captures;
    gfx::Device* device=nullptr;
    int frame=0,mode=0; bool pending=false,failed=false,complete=false;
    platform::AppConfig cfg;cfg.title="OpenArena BSP optical regression";cfg.width=w;cfg.height=h;cfg.highDpi=false;
    platform::AppCallbacks app;
    app.init=[&](gfx::Device& d){
        try{
            device=&d;
            require(d.info().backend==gfx::Backend::Metal && renderer.init(d),"Metal initialization failed");
            renderer.setShadowQuality(ShadowQuality::Off);
            renderer.setMaterialQuality(MaterialQuality::Low);
            renderer.lighting.tonemap=false; renderer.lighting.fogDensity=0;
            const auto beforeLoad=std::chrono::steady_clock::now();
            level=loadModel(path,[](const std::string& file,std::string& dst){
                std::ifstream in(file,std::ios::binary);if(!in)return false;
                dst.assign(std::istreambuf_iterator<char>(in),{});return !in.bad();
            });
            const double loadMs=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-beforeLoad).count();
            const auto& visibility=*level->bspVisibility;
            const int cluster=visibility.cameraCluster({-280,632,24});
            size_t admitted=0;
            for(size_t i=0;i<visibility.primitiveClusters.size();++i)admitted+=visibility.visible(cluster,i);
            std::printf("BSP import %.2f ms; PVS cluster %d admits %zu/%zu surfaces\n",loadMs,cluster,admitted,visibility.primitiveClusters.size());
            require(cluster>=0 && admitted>0 && admitted<visibility.primitiveClusters.size(),"authored viewpoint should cull geometry");
            plain=level->materials;
            for(auto& mat:plain)mat.baseTex=-1;
            require(uploadModel(d,*level),"model upload failed");
            require(targets.init(d)&&targets.acquire(1,w,h),"target allocation failed");
            std::printf("BSP surfaces=%zu textures=%zu\n",level->meshes.front().primitives.size(),level->images.size());
        }catch(const std::exception& e){std::fprintf(stderr,"BSP init: %s\n",e.what());failed=true;}
    };
    app.frame=[&](gfx::Device& d,double){
        try{
            if(failed || complete){platform::requestQuit();return;}
            require(++frame<300,"readback timeout");
            if(mode==3) {
                size_t diffuse=0,baked=0;
                for(size_t i=0;i<captures[0].size();i+=4) {
                    for(int j=0;j<3;++j)if(std::abs(int(captures[0][i+j])-int(captures[1][i+j]))>3){++diffuse;break;}
                    for(int j=0;j<3;++j)if(std::abs(int(captures[0][i+j])-int(captures[2][i+j]))>3){++baked;break;}
                }
                std::printf("BSP changed pixels of %d: diffuse=%zu lightmap=%zu\n",w*h,diffuse,baked);
                require(diffuse>w*h/100 && baked>w*h/100,"expected both diffuse textures and lightmaps to affect frame");
                if(auto* out=std::getenv("AIVERSE_BSP_FRAME_PNG"))
                    require(cameraWritePngRgba8(out,w,h,captures[0]),"could not write frame");
                complete=true;platform::requestQuit();return;
            }
            if(pending)return;
            // Use the level author's intermission camera and spectate target. Q3 is Z-up.
            Camera cam;cam.target={224.f/32,632.f/32,-128.f/32};
            const float3 eye{-280.f/32,632.f/32,24.f/32};
            const auto offset=eye-cam.target;cam.distance=length(offset);cam.pitch=0;
            cam.yaw=std::atan2(offset.x,offset.z);cam.fovY=1.25f;
            renderer.setBakedQuality(mode==2?BakedQuality::Off:BakedQuality::High);
            renderer.beginScene(cam,w,h);
            ModelDraw draw{};draw.model=level.get();draw.world=mat4::scale({1.f/32,1.f/32,1.f/32});
            if(mode==1)draw.materials=&plain;
            renderer.drawModel(draw);
            const auto* target=targets.find(1);
            gfx::PassDesc pass;pass.colorTarget=target->color;pass.depthTarget=target->depth;
            renderer.render(pass);d.endPass();
            if(frame<8)return;
            pending=true;const int index=mode;
            require(d.captureTexture(target->color,[&,index](int width,int height,std::vector<uint8_t> data){
                if(width!=w || height!=h || data.size()!=size_t(w*h*4)){failed=true;return;}
                captures[index]=std::move(data);pending=false;++mode;
            }),"GPU readback failed");
        }catch(const std::exception& e){std::fprintf(stderr,"BSP frame: %s\n",e.what());failed=true;}
    };
    app.shutdown=[&]{if(device){device->waitForResourceIdle();targets.shutdownAfterGpuIdle();if(level)releaseModel(*device,*level);renderer.shutdown();}
        if(failed||!complete)std::exit(1);};
    platform::run(cfg,app);
    return complete&&!failed?0:1;
}
