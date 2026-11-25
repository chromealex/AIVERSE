// Verify submission, shading and readback using the same project asset as the demo.
#include "engine/platform/Platform.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/RenderTargetPool.h"
#include "engine/render/CameraPng.h"
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <new>
#include <chrono>
#include <array>
#include <algorithm>
#include <atomic>
static std::atomic<unsigned long long> news{};
void* operator new(std::size_t n){auto p=std::malloc(n?n:1);if(!p)throw std::bad_alloc();++news;return p;}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t n){return ::operator new(n);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}
using namespace aiverse;
int aiverseMain(int argc, char** argv) {
    if (!std::getenv("AIVERSE_MATERIAL_TECHNIQUE_CAPTURE")) return 77;
    if (argc != 2) return 2;
    const std::filesystem::path root(argv[1]);
    constexpr int size=512;
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
            if(!std::getenv("AIV_ENABLED"))model->materialTechniques={};
            require(render::uploadModel(gpu,*model),"upload crystal");
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';failed=true;}
    };
    constexpr int total=90,warmup=30;
    std::array<double,total-warmup> cpu{},dev{},alloc{};
    callbacks.frame=[&](gfx::Device& gpu,double) {
        if (failed){platform::requestQuit();return;}
        const unsigned long long startAlloc=news.load();
        const auto start=std::chrono::steady_clock::now();
        aiverse::render::Camera camera;camera.ortho=true;camera.orthoHalfHeightOverride=18;
        camera.distance=38;camera.pitch=.70f;camera.yaw=.45f;
        renderer.beginScene(camera,512,512);
        for(int z=0;z<20;++z)for(int x=0;x<20;++x) {
            aiverse::render::ModelDraw draw;
            draw.model=model.get();
            draw.world=aiverse::render::mat4::translate({(x-9.5f)*1.45f,0,(z-9.5f)*1.45f});
            renderer.drawModel(draw);
        }
        auto target=targets.find(1);
        aiverse::gfx::PassDesc pass;pass.colorTarget=target->color;pass.depthTarget=target->depth;
        renderer.render(pass);gpu.endPass();
        const auto finished=std::chrono::steady_clock::now();
        if(frame>=warmup) {
            size_t i=size_t(frame-warmup);
            alloc[i]=double(news.load()-startAlloc);
            cpu[i]=std::chrono::duration<double,std::milli>(finished-start).count();
            dev[i]=gpu.gpuFrameMs();
        }
        if(++frame==total){
            auto m=renderer.residentMemoryStats();
            std::sort(cpu.begin(),cpu.end());std::sort(dev.begin(),dev.end());std::sort(alloc.begin(),alloc.end());
            std::cout<<"instances=400 warmup=30 samples=60 draws="<<renderer.drawCalls()
                     <<" cpu_median_ms="<<cpu[30]<<" cpu_p95_ms="<<cpu[56]<<" gpu_median_ms="<<dev[30]<<" gpu_p95_ms="<<dev[56]
                     <<" renderer_texture_bytes="<<m.liveTextureKnownBytes<<" renderer_buffer_bytes="<<m.liveBufferKnownBytes
                     <<" cxx_new_median="<<alloc[30]<<" cxx_new_p95="<<alloc[56]<<std::endl;
            completed=3;platform::requestQuit();
        }
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
