// Exercise the ordinary material binding with an imported UV-free OBJ and a freshly
// exported Modeling boolean. Both models share a world-space projection.
#include "engine/platform/Platform.h"
#include "engine/render/CameraPng.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/RenderTargetPool.h"
#include "engine/render/graph/TriplanarMaterial.h"
#include "engine/view/AppearanceAuthoringService.h"
#include "tools/modeling/Modeling.h"
#include <array>
#include <cstdlib>
#include <filesystem>
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
using namespace aiverse::render;
namespace {
constexpr int width=512,height=512;
void check(bool ok,const char* message) { if(!ok) throw std::runtime_error(message); }

std::unique_ptr<Model> import(const std::string& name,const std::string& bytes) {
    return loadModel(name,[&](const std::string& path,std::string& data){
        if(path!=name) return false;
        data=bytes;
        return true;
    });
}

Image texture(bool normal) {
    Image img;
    img.name=normal?"detail.png":"colour.png";
    img.width=img.height=32;
    for(int y=0;y<img.height;++y) for(int x=0;x<img.width;++x) {
        // An asymmetric two-axis pattern makes projection scale/orientation visible.
        const bool mark=x<7 || (y<5 && x<23);
        const std::array<uint8_t,4> texel=normal
            ? std::array<uint8_t,4>{uint8_t(mark?207:75),uint8_t(y<16?175:92),222,255}
            : (mark?std::array<uint8_t,4>{235,98,28,255}:std::array<uint8_t,4>{60,146,210,255});
        img.rgba.insert(img.rgba.end(),texel.begin(),texel.end());
    }
    return img;
}
}

int aiverseMain(int,char**) {
    const auto folder=std::getenv("AIVERSE_TRIPLANAR_CAPTURE");
    if(!folder)return 77;
    Renderer3D renderer;
    CameraRenderTargetPool targets;
    std::array<std::unique_ptr<Model>,2> models;
    std::array<std::vector<uint8_t>,3> images;
    std::array<std::array<std::vector<Material>,2>,3> materials;
    gfx::Device* device=nullptr;
    int captured=0,frame=0;
    bool failed=false;
    platform::AppConfig app;app.width=width;app.height=height;app.title="Triplanar materials";
    platform::AppCallbacks callbacks;
    callbacks.init=[&](gfx::Device& gpu) {
        device=&gpu;
        try {
            check(renderer.init(gpu) && targets.init(gpu),"renderer initialization");
            renderer.setShadowQuality(ShadowQuality::Off);
            renderer.lighting.fogDensity=0;renderer.lighting.tonemap=false;
            renderer.lighting.sunDir=normalize(float3{.4f,.85f,.6f});
            for(int i=0;i<3;++i) check(targets.acquire(i+1,width,height),"target allocation");
            models[0]=import("faceted.obj",
                "v 0 1.15 0\nv 0 -1.15 0\nv -1 0 0\nv 0 0 1\nv 1 0 0\nv 0 0 -1\n"
                "f 1 3 4\nf 1 4 5\nf 1 5 6\nf 1 6 3\nf 2 4 3\nf 2 5 4\nf 2 6 5\nf 2 3 6\n");
            auto document=Json::parse(R"({"version":1,"nodes":[
                {"id":"body","construction":true,"geometry":{"kind":"box","size":[1.8,1.8,1.8]}},
                {"id":"tool","construction":true,"geometry":{"kind":"box","size":[1.6,1.6,1.6],"modifiers":[{"kind":"transform","translation":[0.65,0.65,0.65]}]}},
                {"id":"cut","geometry":{"kind":"boolean","a":"body","b":"tool","operation":"subtract"}}
            ]})");
            auto generated=modeling::build(document);
            models[1]=import("cut.glb",modeling::exportGlb(*generated.model));
            for(size_t m=0;m<models.size();++m) {
                auto& model=*models[m];
                check(!model.vertices.empty(),"model contains no vertices");
                for(auto& vertex:model.vertices) vertex.uv={0,0};
                const int colour=int(model.images.size());
                model.images.push_back(texture(false));
                model.images.push_back(texture(true));
                for(int i=0;i<3;++i) {
                    Json recipe=graph::makeTriplanarMaterial("colour.png",i==1?"":"detail.png",i==2?.95:.46);
                    Json library=Json::object();library.set("stone",recipe);
                    materials[i][m]=model.materials;
                    for(auto& material:materials[i][m]) {
                        view::AppearanceAuthoringService::applyMaterial(material,library,Json::string("stone"),
                            [=](const std::string& path){return path=="colour.png"?colour:colour+1;});
                        check(bool(material.shaderGraph),"material graph binding");
                    }
                }
                check(uploadModel(gpu,model),"model upload");
            }
        } catch(const std::exception& e){std::cerr<<e.what()<<'\n';failed=true;}
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
            int m=(x+z)%2;draw.model=models[m].get();draw.materials=&materials[std::getenv("AIV_ENABLED")?0:1][m];
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
            captured=3;platform::requestQuit();
        }
    };
    callbacks.shutdown=[&] {
        if(device && device->waitForResourceIdle()) {
            for(auto& model:models) if(model)releaseModel(*device,*model);
            targets.shutdownAfterGpuIdle();renderer.shutdown();
        }else failed=true;
        if(failed || captured!=3){std::fflush(nullptr);std::exit(1);}
    };
    platform::run(app,callbacks);
    return failed || captured!=3;
}
