// Verify submission, shading and readback using the same project asset as the demo.
#include "engine/platform/Platform.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/RenderTargetPool.h"
#include "engine/render/CameraPng.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

using namespace aiverse;
int aiverseMain(int argc, char** argv) {
    if (!std::getenv("AIVERSE_MATERIAL_TECHNIQUE_CAPTURE")) return 77;
    if (argc != 2) return 2;
    const std::filesystem::path root(argv[1]);
    constexpr int size=192;
    render::Renderer3D renderer;
    render::CameraRenderTargetPool targets;
    std::unique_ptr<render::Model> model;
    gfx::Device* device=nullptr;
    bool failed=false;
    int frame=0,completed=0;
    std::array<std::vector<uint8_t>,3> images;
    auto require=[](bool ok,const char* message) {if(!ok)throw std::runtime_error(message);};
    platform::AppConfig app;
    app.title="Material techniques";app.width=size;app.height=size;
    platform::AppCallbacks callbacks;
    callbacks.init=[&](gfx::Device& gpu) {
        device=&gpu;
        try {
            require(gpu.info().backend==gfx::Backend::Metal,"expected Metal");
            require(renderer.init(gpu),"renderer init");
            renderer.setShadowQuality(render::ShadowQuality::Off);
            renderer.lighting.fogDensity=0;renderer.lighting.tonemap=false;
            require(targets.init(gpu),"target pool");
            for(int i=0;i<3;++i)require(targets.acquire(i+1,size,size),"target allocation");
            model=render::loadModel("content/crystal.gltf",[&](const std::string& name,std::string& bytes) {
                std::ifstream file(root/name,std::ios::binary);
                if(!file)return false;
                bytes.assign(std::istreambuf_iterator<char>(file),{});
                return true;
            });
            require(render::uploadModel(gpu,*model),"upload crystal");
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';failed=true;}
    };
    callbacks.frame=[&](gfx::Device& gpu,double) {
        try {
            if(failed){platform::requestQuit();return;}
            require(++frame<180,"GPU readback timed out");
            if(frame==1) {
                for(int i=0;i<3;++i) {
                    render::Camera camera;
                    camera.ortho=true;camera.orthoHalfHeightOverride=1.45f;
                    camera.distance=i==0?8:32;camera.pitch=.3f;camera.yaw=.4f;
                    renderer.setMaterialScheme(i==2?"inspection":"default");
                    renderer.beginScene(camera,size,size);
                    render::ModelDraw draw;draw.model=model.get();
                    renderer.drawModel(draw);
                    auto target=targets.find(i+1);
                    gfx::PassDesc pass;pass.colorTarget=target->color;pass.depthTarget=target->depth;
                    renderer.render(pass);gpu.endPass();
                    require(gpu.captureTexture(target->color,[&,i](int w,int h,std::vector<uint8_t> pixels) {
                        if(w!=size||h!=size)failed=true;
                        images[i]=std::move(pixels);++completed;
                    }),"submit capture");
                }
            }
            if(completed==3) {
                require(!failed,"capture dimensions changed");
                const int channels[]={0,2,1};
                const char* names[]={"near","far","inspection"};
                const std::filesystem::path output(std::getenv("AIVERSE_MATERIAL_TECHNIQUE_CAPTURE"));
                for(int i=0;i<3;++i) {
                    auto& pixels=images[i];
                    require(pixels.size()==size*size*4,"empty readback");
                    size_t matching=0;
                    for(size_t j=0;j<pixels.size();j+=4) {
                        int c=channels[i];
                        if(pixels[j+c]>pixels[j+(c+1)%3]+30 && pixels[j+c]>pixels[j+(c+2)%3]+30)++matching;
                    }
                    require(render::cameraWritePngRgba8((output/(std::string(names[i])+".png")).string(),size,size,pixels),"write capture");
                    std::cout<<names[i]<<": "<<matching<<" selected-material pixels\n";
                    require(matching>800,"incorrect material in rendered frame");
                }
                platform::requestQuit();
            }
        } catch(const std::exception& e){std::cerr<<e.what()<<'\n';failed=true;platform::requestQuit();}
    };
    callbacks.shutdown=[&] {
        if(device && device->waitForResourceIdle()) {
            if(model)render::releaseModel(*device,*model);
            targets.shutdownAfterGpuIdle();renderer.shutdown();
        }else failed=true;
        if(failed||completed!=3){std::fflush(nullptr);std::exit(1);}
    };
    platform::run(app,callbacks);
    return failed||completed!=3?1:0;
}
