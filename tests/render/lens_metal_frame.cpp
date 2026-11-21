// Native Metal acceptance: one prepared LENS frame, two independent camera views.
#include "engine/platform/Platform.h"
#include "engine/render/CameraModelSnapshot.h"
#include "engine/render/RenderTargetPool.h"
#include "modules/rts/view/RtsView.h"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <stdexcept>
using namespace aiverse;
namespace {
void require(bool ok,const char* message) {if(!ok)throw std::runtime_error(message);}
}
int aiverseMain(int,char**) {
    if(!std::getenv("AIVERSE_LENS_METAL_VALIDATE"))return 77;
    constexpr int width=160,height=120;
    gfx::Device* device=nullptr;
    render::Renderer3D renderer;game::RtsView view;
    render::CameraRenderTargetPool targets;render::CameraModelSnapshot snapshot;
    game::Rules rules;game::MapGrid grid;game::RtsScene scene;
    grid.init(8,8);for(auto& f:grid.flags)f=game::kCellGround;
    grid.corner.assign(81,0);
    for(int y=1;y<5;++y)for(int x=1;x<4;++x)grid.corner[size_t(y*9+x)]=24;
    std::vector<uint8_t> vision(64,2);
    std::array<std::vector<uint8_t>,3> pixels;
    int frame=0,completed=0;bool failed=false,done=false;
    platform::AppConfig config;config.title="LENS Metal frame validation";
    config.width=width;config.height=height;config.highDpi=false;
    platform::AppCallbacks callbacks;
    callbacks.init=[&](gfx::Device& d) {
        device=&d;
        try {
            require(d.info().backend==gfx::Backend::Metal,"native Metal backend required");
            require(renderer.init(d),"renderer initialization");
            renderer.setShadowQuality(render::ShadowQuality::Off);
            renderer.setMaterialQuality(render::MaterialQuality::Low);
            renderer.lighting.fogDensity=0;renderer.lighting.tonemap=false;
            require(view.init(d,rules,[](const std::string&,std::string&){return false;}),"view initialization");
            view.buildTerrain(d,grid);view.setCameraPreparedCapture(&snapshot);
            require(targets.init(d),"target pool initialization");
            for(int i=0;i<3;++i)require(targets.acquire(uint64_t(i+1),width,height),"target allocation");
        }catch(const std::exception& e){std::fprintf(stderr,"FAIL init: %s\n",e.what());failed=true;}
    };
    callbacks.frame=[&](gfx::Device& d,double) {
        try {
            if(failed || done){platform::requestQuit();return;}
            require(frame++<120,"GPU capture timeout");
            targets.collectCompleted();view.collectRetiredModels();
            if(frame==1) {
                render::RTSCamera camera;camera.target={4,0,4};camera.distance=15;camera.pitch=1;
                renderer.beginScene(camera,width,height);
                require(view.prepareTerrainAtomicFrame(d,grid,renderer,scene,1,&vision),"topology staging");
                {
                    render::CameraModelSnapshot::CaptureGuard capture(snapshot,renderer);
                    view.draw(renderer,scene,1,&vision,grid);
                    renderer.triangle({2,2,2},{4,2,2},{2,2,4},0xff3070ffu);
                }
                const auto epoch=view.publishedVisualFrame();require(epoch.ready(),"primary publication");
                for(int i=0;i<3;++i) {
                    if(i) {
                        if(i==2)camera.yaw=1.4f;
                        renderer.beginScene(camera,width,height);
                        view.drawPreparedTerrainFog(renderer,grid,&vision,1,epoch.id);
                        view.drawPreparedSprites(renderer,epoch.id);snapshot.submit(renderer,uint64_t(i));
                    }
                    const auto* target=targets.find(uint64_t(i+1));
                    gfx::PassDesc pass;pass.colorTarget=target->color;pass.depthTarget=target->depth;
                    renderer.render(pass);d.endPass();
                    require(d.captureTexture(target->color,[&,i](int w,int h,std::vector<uint8_t> data){
                        if(w!=width || h!=height || data.size()!=size_t(width*height*4))failed=true;
                        pixels[size_t(i)]=std::move(data);++completed;
                    }),"texture capture submission");
                }
                require(view.publishedVisualFrame().sourceFrame==epoch.sourceFrame,"secondary advanced source frame");
                require(view.publishedVisualFrame().id==epoch.id,"secondary changed publication");
            }
            if(completed==3) {
                require(!failed,"capture dimensions");
                size_t mismatch=0,differentView=0,nonuniform=0;
                for(size_t i=0;i<pixels[0].size();++i) {
                    if(std::abs(int(pixels[0][i])-int(pixels[1][i]))>2)++mismatch;
                    if(std::abs(int(pixels[0][i])-int(pixels[2][i]))>2)++differentView;
                    if(pixels[0][i]!=pixels[0][i%4])++nonuniform;
                }
                std::printf("LENS Metal pixels: primary/replay mismatch=%zu other-camera changes=%zu nonuniform=%zu\n",mismatch,differentView,nonuniform);
                require(nonuniform>100,"blank frame");require(mismatch==0,"prepared replay pixel parity");
                require(differentView>100,"independent camera projection");
                done=true;platform::requestQuit();
            }
        }catch(const std::exception& e){std::fprintf(stderr,"FAIL frame: %s\n",e.what());failed=true;platform::requestQuit();}
    };
    callbacks.shutdown=[&] {
        if(!device)return;
        try {
            require(device->waitForResourceIdle(),"shutdown completion");
            snapshot.clear();view.setCameraPreparedCapture(nullptr);view.shutdown(*device);
            targets.shutdownAfterGpuIdle();renderer.shutdown();
        }catch(const std::exception& e){std::fprintf(stderr,"FAIL teardown: %s\n",e.what());failed=true;}
        // AppKit terminate exits instead of returning from platform::run.
        // Report failure here so a failed native callback cannot exit zero.
        if(failed || !done){std::fflush(nullptr);std::exit(1);}
        std::puts("PASS LENS native Metal prepared frame and independent camera pixels");
    };
    platform::run(config,callbacks);
    if(done && !failed)std::puts("PASS LENS native Metal prepared frame and independent camera pixels");
    return done && !failed?0:1;
}
