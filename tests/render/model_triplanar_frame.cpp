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

using namespace aiverse;
using namespace aiverse::render;
namespace {
constexpr int width=512,height=288;
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
    callbacks.frame=[&](gfx::Device& gpu,double) {
        try {
            if(failed){platform::requestQuit();return;}
            check(++frame<180,"readback timed out");
            if(frame==1) for(int i=0;i<3;++i) {
                Camera camera;camera.ortho=true;camera.orthoHalfHeightOverride=1.62f;
                camera.distance=8;camera.pitch=.48f;camera.yaw=.45f;
                renderer.beginScene(camera,width,height);
                for(size_t m=0;m<models.size();++m) {
                    const float offset=m?1.35f:-1.35f;
                    ModelDraw draw;draw.model=models[m].get();draw.materials=&materials[i][m];
                    draw.world=mat4::translate({offset*std::cos(camera.yaw),0,-offset*std::sin(camera.yaw)});
                    if(!m)draw.world=draw.world*mat4::rotateY(.5f)*mat4::scale({.93f,1.14f,.8f});
                    renderer.drawModel(draw);
                }
                auto target=targets.find(i+1);
                gfx::PassDesc pass;pass.colorTarget=target->color;pass.depthTarget=target->depth;
                renderer.render(pass);gpu.endPass();
                check(gpu.captureTexture(target->color,[&,i](int w,int h,std::vector<uint8_t> pixels){
                    if(w!=width || h!=height)failed=true;
                    images[i]=std::move(pixels);++captured;
                }),"capture submission");
            }
            if(captured==3) {
                const char* names[]={"projected","colour-only","repeat"};
                for(size_t i=0;i<images.size();++i) {
                    check(images[i].size()==width*height*4,"readback size");
                    check(cameraWritePngRgba8((std::filesystem::path(folder)/(std::string(names[i])+".png")).string(),
                                              width,height,images[i]),"PNG write");
                }
                struct Region {const char* name;int left,top,right,bottom;};
                const Region regions[]={
                    {"OBJ",0,0,width/2,height},{"CSG",width/2,0,width,height},
                    {"cut back",390,85,438,141},{"cut wall",350,93,364,137},{"cut floor",379,172,427,188}
                };
                for(const auto& region:regions) {
                    int normalChanges=0,repeatChanges=0,warm=0,cool=0;
                    for(int y=region.top;y<region.bottom;++y)for(int x=region.left;x<region.right;++x) {
                        size_t k=size_t(y*width+x)*4;
                        int n=0,s=0;
                        for(int c=0;c<3;++c) {
                            n+=std::abs(int(images[0][k+c])-images[1][k+c]);
                            s+=std::abs(int(images[0][k+c])-images[2][k+c]);
                        }
                        normalChanges+=n>12;repeatChanges+=s>35;
                        warm+=images[0][k]>images[0][k+2]+35;
                        cool+=images[0][k+2]>images[0][k]+35;
                    }
                    std::cout<<region.name<<" warm="<<warm<<" cool="<<cool
                             <<" normalDelta="<<normalChanges<<" repeatDelta="<<repeatChanges<<'\n';
                    check(warm>120 && cool>120 && normalChanges>120 && repeatChanges>120,"projected surface mismatch");
                }
                platform::requestQuit();
            }
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';failed=true;platform::requestQuit();}
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
