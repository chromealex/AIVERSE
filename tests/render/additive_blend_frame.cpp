// Real GPU pixels, independent expected blend arithmetic, shared render owners.
#include "engine/platform/Platform.h"
#include "engine/render/Prisma.h"
#include "engine/render/RenderTargetPool.h"
#include "engine/render/CameraPng.h"
#include "engine/fx/ParticleSystem.h"
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <iostream>

using namespace aiverse;
using namespace aiverse::render;
namespace {
float encode(float x) { return x<=.0031308f?12.92f*x:1.055f*std::pow(x,1.f/2.4f)-.055f; }
float decode(float x) { return x<=.04045f?x/12.92f:std::pow((x+.055f)/1.055f,2.4f); }
void require(bool value,const char* message) { if(!value)throw std::runtime_error(message); }
struct Case { const char* name; AlphaMode mode; bool particle=false,display=false,hidden=false,graph=false; float fog=0; bool resize=false; int geometry=0; bool textureGraph=false; };
const Case cases[]={
    {"model-weighted",AlphaMode::Additive},
    {"model-unweighted",AlphaMode::AdditiveUnweighted},
    {"model-alpha",AlphaMode::Blend},
    {"model-weighted-again",AlphaMode::Additive},
    {"particle-linear",AlphaMode::Opaque,true},
    {"particle-display",AlphaMode::Opaque,true,true},
    {"particle-display-occluded",AlphaMode::Opaque,true,true,true},
    {"particle-display-graph",AlphaMode::Opaque,true,true,false,true},
    {"particle-display-fog",AlphaMode::Opaque,true,true,false,false,.05f},
    {"particle-display-resize",AlphaMode::Opaque,true,true,false,false,0,true},
    {"particle-display-mesh",AlphaMode::Opaque,true,true,false,false,0,false,1},
    {"particle-display-mesh-occluded",AlphaMode::Opaque,true,true,true,false,0,false,1},
    {"particle-display-ribbon",AlphaMode::Opaque,true,true,false,false,0,false,2},
    {"particle-display-trail",AlphaMode::Opaque,true,true,false,false,0,false,3},
    {"particle-display-graph-texture",AlphaMode::Opaque,true,true,false,false,0,false,0,true},
    {"particle-display-graph-texture-occluded",AlphaMode::Opaque,true,true,true,false,0,false,0,true}
};
}
int aiverseMain(int,char**) {
    const auto capture=std::getenv("AIVERSE_ADDITIVE_CAPTURE");
    Renderer3D renderer;
    Prisma post;
    CameraRenderTargetPool targets;
    fx::ParticleSystem particles;
    gfx::TextureHandle white;
    std::unique_ptr<Model> background,foreground;
    gfx::Device* device=nullptr;
    int current=0,frames=0;
    bool failed=false,pending=false;
    constexpr int count=int(sizeof(cases)/sizeof(cases[0]));
    constexpr float encoded=.25f;
    const float base=decode(encoded);
    platform::AppConfig app;app.title="Additive blend pixels";app.width=256;app.height=192;app.highDpi=false;
    platform::AppCallbacks cb;
    cb.init=[&](gfx::Device& gpu) {
        device=&gpu;
        try {
            require(renderer.init(gpu)&&post.init(gpu)&&targets.init(gpu),"render owners init");
            renderer.setShadowQuality(ShadowQuality::Off);
            renderer.lights.setQuality(LightQuality::Off);
            renderer.lighting.fogDensity=0;renderer.lighting.tonemap=false;
            PrismaSettings settings;settings.hdr=HdrMode::On;post.setSettings(settings);post.setQuality(PrismaQuality::High);
            MeshBuilder plane;plane.quad({-10,-10,0},{10,-10,0},{10,10,0},{-10,10,0});
            Material material;material.shading=ShadingModel::Unlit;material.vertexColors=false;material.castShadow=false;
            material.baseColor={base,base,base,1};
            background=modelFromParts("background",{{"plane",&plane,material,{}}});
            material.baseColor={.5f,.5f,.5f,.5f};material.alpha=AlphaMode::Additive;
            foreground=modelFromParts("foreground",{{"plane",&plane,material,{}}});
            require(uploadModel(gpu,*background)&&uploadModel(gpu,*foreground),"model upload");
            const uint8_t pixel[]={255,255,255,255};gfx::TextureDesc desc;desc.width=desc.height=1;desc.data=pixel;
            white=gpu.createTexture(desc);require(white.valid(),"white particle texture");
            particles.setQuality(fx::FxQuality::High);require(particles.init(&gpu),"particle init");
            particles.setTextureLoader([&](const std::string&,fx::FxTexture& out){out.tex=white;return true;});
            particles.setMeshLoader([](const std::string&,fx::FxMesh& mesh) {
                mesh.pos={{-1,-1,0},{1,-1,0},{1,1,0},{-1,1,0}};
                mesh.nrm.assign(4,{0,0,1});mesh.uv.assign(4,{.5f,.5f});
                mesh.idx={0,1,2,0,2,3};return true;
            });
            if(capture)std::filesystem::create_directories(capture);
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';failed=true;}
    };
    cb.frame=[&](gfx::Device& gpu,double) {
        try {
            if(failed||current==count){platform::requestQuit();return;}
            require(++frames<600,"GPU capture timeout");
            if(pending)return;
            const int index=current;const auto test=cases[index];
            const int width=test.resize?254:192,height=test.resize?130:128;
            require(targets.acquire(index+1,width,height),"output allocation");
            const auto target=targets.find(index+1);
            Camera camera;camera.ortho=true;camera.orthoHalfHeightOverride=2;camera.target={0,0,0};camera.pitch=0;camera.yaw=0;camera.distance=10;
            renderer.beginScene(camera,test.resize?width/2:width,test.resize?height/2:height);
            ModelDraw bg;bg.model=background.get();renderer.drawModel(bg);
            particles.clear();
            if(test.particle) {
                fx::EffectDef effect;effect.emitters.emplace_back();auto& emitter=effect.emitters.back();
                emitter.texture="white";emitter.burst=1;emitter.life={10,10};emitter.speed={0,0};emitter.size={2,2};
                emitter.color={{0,{encoded,encoded,encoded,1}},{1,{encoded,encoded,encoded,1}}};
                emitter.blendSpace=test.display?fx::FxBlendSpace::Display:fx::FxBlendSpace::Linear;
                if(test.graph)emitter.shaderGraph=Json::parse(R"({"version":1,"domain":"particle","nodes":[{"id":"input","op":"input"}],"outputs":{"color":"input"}})");
                if(test.textureGraph)emitter.shaderGraph=Json::parse(R"({"version":1,"domain":"particle","nodes":[{"id":"uv","op":"uv"},{"id":"tex","op":"sampleTexture","inputs":["uv"],"texture":"white.png"},{"id":"input","op":"input"},{"id":"product","op":"multiply","inputs":["input","tex"]}],"outputs":{"color":"product"}})");
                if(test.geometry==1)emitter.meshes={"plane"};
                if(test.geometry>=2) {
                    emitter.hidden=true;emitter.trail.enabled=true;emitter.trail.texture="white";
                    emitter.trail.blendSpace=fx::FxBlendSpace::Display;
                    emitter.trail.width={{0,.5f},{1,.5f}};emitter.trail.sizeAffectsWidth=false;
                    emitter.trail.color={{0,{1,1,1,1}},{1,{1,1,1,1}}};
                    if(test.geometry==2){emitter.trail.ribbon=true;emitter.burst=0;emitter.perMeter={1,1};emitter.loop=true;}
                    else emitter.velocity={0,200,0}; // effect +Y follows the play direction
                }
                particles.setView(camera,renderer.viewProj(),width,height);
                const int definition=particles.define(test.name,effect);require(definition>=0,"particle definition");
                const float z=test.hidden?-1.f:1.f;
                auto handle=particles.play(definition,{test.geometry>=2?-2.f:0.f,0,z},
                    test.geometry==3?float3{1,0,0}:float3{0,1,0});
                if(test.geometry==2)particles.move(handle,{2,0,z});
                particles.update(.01f);
                if(test.geometry==3) {
                    particles.update(.02f);
                    std::vector<fx::FxParticleInfo> snapshot;particles.snapshot(snapshot);
                    require(snapshot.size()==1 && snapshot[0].pos.x>0 && std::abs(snapshot[0].pos.z-z)<1e-5f,"trail fixture must cross the camera center");
                }
            }else {
                foreground->materials[0].alpha=test.mode;
                ModelDraw fg;fg.model=foreground.get();fg.world=mat4::translate({0,0,1});renderer.drawModel(fg);
            }
            gfx::PassDesc pass;pass.colorTarget=target->color;pass.depthTarget=target->depth;
            renderer.render(pass,&post);require(post.linearScene(),"HDR path must be active");
            if(test.particle){fx::FxFog fog;fog.density=test.fog;particles.renderScene(renderer,fog);}
            renderer.resolvePrisma();gpu.endPass();
            float expected=0;
            if(test.particle) {
                const float value=64.f/255; // vertex UNorm8 quantization of 0.25
                expected=test.hidden?encoded:(test.display?encoded+value*std::exp(-9.f*test.fog):encode(base+decode(value)));
            }else expected=encode((test.mode==AlphaMode::Blend?base*.5f:base)+(test.mode==AlphaMode::AdditiveUnweighted?.5f:.25f));
            pending=true;
            require(gpu.captureTexture(target->color,[&,index,test,expected](int w,int h,std::vector<uint8_t> pixels) {
                try {
                    require(pixels.size()==size_t(w*h*4)&&w>8&&h>8,"invalid readback");
                    float actual=0;
                    for(int y=h/2-2;y<h/2+2;++y)for(int x=w/2-2;x<w/2+2;++x)for(int c=0;c<3;++c)actual+=pixels[size_t(y*w+x)*4+c];
                    actual/=48*255.f;
                    std::cout<<test.name<<" got="<<actual<<" expected="<<expected<<'\n';
                    if(capture)require(cameraWritePngRgba8((std::filesystem::path(capture)/(std::string(test.name)+".png")).string(),w,h,pixels),"capture write");
                    require(std::abs(actual-expected)<4.f/255,"blend pixel mismatch");
                }catch(const std::exception& e){std::cerr<<test.name<<": "<<e.what()<<'\n';failed=true;}
                current=index+1;pending=false;
            }),"capture submission");
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';failed=true;platform::requestQuit();}
    };
    cb.shutdown=[&] {
        if(device&&device->waitForResourceIdle()) {
            particles.shutdown();if(white.valid())device->destroyTexture(white);
            if(foreground)releaseModel(*device,*foreground);if(background)releaseModel(*device,*background);
            post.shutdown();targets.shutdownAfterGpuIdle();renderer.shutdown();
        }else failed=true;
        if(failed||current!=count){std::fflush(nullptr);std::exit(1);}
    };
    platform::run(app,cb);
    return failed||current!=count?1:0;
}
