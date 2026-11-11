// Deterministic, asset-free visual fixture shared by GL and Metal runners.
#pragma once
#include "engine/render/PostFx.h"
#include "engine/render/ScreenLighting.h"
#include "HdrShaders.h"
#include "engine/render/graph/ShaderGraph.h"
#include <cstdio>
#include <cstdlib>
#include <fstream>

namespace aiverse::test {
using namespace render;
struct HdrScene {
    Renderer3D renderer;
    PostFx post;
    ScreenLighting screen;
    bool advanced = false, ownsFrame = true, materialGallery = false;
    PostProcess* pipeline() { return advanced ? static_cast<PostProcess*>(&screen) : static_cast<PostProcess*>(&post); }
    RTSCamera camera;
    Mesh floor, cube, water;
    std::unique_ptr<Model> spheres;
    int frame = 0, probeIndex = 0, gridProbe = -1;
    bool verify = false;
    std::unique_ptr<Model> probeBg, probeFg;
    bool captured = false, failed = false;
    std::string path;
    gfx::PassDesc output;
    std::function<void(gfx::Device::CaptureFn)> readOutput;
    void init(gfx::Device& d) {
        std::printf("GPU: %s; RGBA16F=%d\n", d.info().name.c_str(), d.supports(gfx::PixelFormat::RGBA16F));
        if (!renderer.init(d) || !post.init(d)) { failed = true; return; }
        advanced = std::getenv("AIVERSE_SCREEN_LIGHTING") != nullptr;
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
        post.setQuality(PostFx::defaultQualityFor(d,false));
        verify = std::getenv("AIVERSE_HDR_VERIFY") != nullptr;
        if (verify) {
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
            uploadModel(d,*probeBg);uploadModel(d,*probeFg);
            renderer.setShadowQuality(ShadowQuality::Off);renderer.lights.setQuality(LightQuality::Off);
            camera.target={0,0,0};camera.pitch=0;camera.yaw=0;camera.distance=10;
        }
        const char* p=std::getenv("AIVERSE_HDR_CAPTURE"); path=p?p:"/tmp/hdr-scene.ppm";
    }
    void draw(gfx::Device& d, int w, int h) {
        if (failed) return;
        if (verify) { drawProbe(d,w,h); return; }
        if(ownsFrame)d.beginFrame(w,h);
        renderer.beginScene(advanced?screen.jittered(camera,w,h):camera,w,h);
        renderer.drawMesh(floor,{}, {0.34f,0.36f,0.4f,1});
        ModelDraw md; md.model=spheres.get(); renderer.drawModel(md);
        if(!materialGallery){
        // Same albedo, increasing radiance: LDR clamps before bloom, HDR retains differences.
        for(int i=0;i<5;++i) renderer.drawMesh(cube,mat4::translate({float(i-2)*2,0,-4.5f}),{0.6f,0.24f,0.06f,1},kDrawUnlit,0,float(1<<i));
        renderer.drawMesh(cube,mat4::translate({-2.8f,0,1}),{0.08f,0.5f,0.95f,0.5f},kDrawUnlit,0,0);
        renderer.drawMesh(cube,mat4::translate({-2.3f,0,1.3f}),{0.95f,0.15f,0.1f,0.5f},kDrawUnlit,0,0);
        renderer.drawMesh(water,mat4::translate({0,0.12f,0}),{0.08f,0.16f,0.22f,0.65f},kDrawNoCastShadow,0.9f);
        renderer.lights.add({2,1.5f,0.8f},4,{0.15f,0.5f,1},3);
        }
        gfx::PassDesc pass = output; pass.clearColor[0]=0.09f;pass.clearColor[1]=0.11f;pass.clearColor[2]=0.15f;
        renderer.render(pass,pipeline());
        const bool hdr=post.linearScene();
        renderer.resolvePostFx();
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
        if(ownsFrame)d.endFrame(); ++frame;
        if (!renderer.modelError().empty() || !post.lastError().empty() || !screen.lastError().empty()) {
            std::fprintf(stderr,"renderer: %s; post: %s\n",renderer.modelError().c_str(),post.lastError().c_str()); failed=true;
        }
    }
    void drawProbe(gfx::Device& d, int w, int h) {
        const bool legacy = probeIndex==3 || probeIndex==4;
        PostFxSettings settings;
        settings.hdr=probeIndex==3?HdrMode::Off:HdrMode::On;
        if(probeIndex==1 || probeIndex==6) settings=parsePostFx(Json::parse(R"({"hdr":"on","effects":[{"type":"colorGrade","exposure":-2}]})"),Json());
        if(probeIndex==6) settings=parsePostFx(Json::parse(R"({"hdr":"on","effects":[{"type":"bloom","threshold":2,"intensity":1},{"type":"colorGrade","exposure":-2}]})"),Json());
        post.setSettings(settings);post.setQuality(probeIndex==4?PostFxQuality::Low:PostFxQuality::High);
        renderer.lighting.tonemap=probeIndex==1 || probeIndex==6;
        const float value=(probeIndex==1 || probeIndex==6)?4.0f:0.25f;
        probeBg->materials[0].baseColor={value,value,value,1};
        probeFg->materials[0].baseColor={0.75f,0.75f,0.75f,0.5f};
        if(probeIndex>=7){
            renderer.lighting.sunIntensity=0;renderer.lighting.skyColor={0,0,0};renderer.lighting.groundColor={0,0,0};
            auto& material=probeBg->materials[0];material.shading=ShadingModel::Pbr;material.baseColor={1,1,1,1};material.roughness=1;material.metallic=0;
            renderer.lights.setQuality(LightQuality::High);
            if(gridProbe!=probeIndex){gridProbe=probeIndex;PointLight light;light.pos={0,0,probeIndex==8?3.0f:1.0f};light.radius=2;light.color={1,1,1};light.intensity=1;
                std::vector<StaticPointShadow> shadows;
                if(probeIndex>=9){
                    StaticShadowGeometry geometry;float x=probeIndex==9?0:4;
                    geometry.triangles={{{x-1,-1,.5f},{x+1,-1,.5f},{x+1,1,.5f}},{{x-1,-1,.5f},{x+1,1,.5f},{x-1,1,.5f}}};
                    geometry.build();shadows.push_back(geometry.bake(light.pos,light.radius,32));
                }
                if(!renderer.lights.setStaticPointLights({light},1,shadows.empty()?nullptr:&shadows)){failed=true;return;}}
        }
        if(ownsFrame)d.beginFrame(w,h);
        if(probeIndex==5) { // alternate view sizes in the same frame, as the editor does
            renderer.beginScene(camera,w/2,h/2);
            ModelDraw small;small.model=probeBg.get();renderer.drawModel(small);
            renderer.render(output,pipeline());renderer.resolvePostFx();d.endPass();
        }
        renderer.beginScene(advanced?screen.jittered(camera,w,h):camera,w,h);
        ModelDraw bg;bg.model=probeBg.get();renderer.drawModel(bg);
        if(probeIndex==2) {ModelDraw fg;fg.model=probeFg.get();fg.world=mat4::translate({0,0,1});renderer.drawModel(fg);}
        renderer.render(output,pipeline());renderer.resolvePostFx();
        if(frame==5) {
            const int test=probeIndex;
            float expected=probeIndex==2?0.5f:0.25f;
            if(probeIndex==1) expected=(2.51f+0.03f)/(2.43f+0.59f+0.14f);
            if(probeIndex==6) expected=(1.5f*(2.51f*1.5f+0.03f))/(1.5f*(2.43f*1.5f+0.59f)+0.14f);
            if(probeIndex==7||probeIndex==10)expected=0.28125f*(1+0.04f*0.64f); // N.L + rough specular at d=1, radius=2
            if(probeIndex==8||probeIndex==9)expected=0; // same XY, outside the 3D radius
            expected=legacy?std::sqrt(expected):(expected<=0.0031308f?12.92f*expected:1.055f*std::pow(expected,1/2.4f)-0.055f);
            auto receive=[this,test,expected](int cw,int ch,std::vector<uint8_t> px) {
                if(cw<=0||ch<=0||px.size()!=size_t(cw*ch*4)) {failed=true;return;}
                float avg=0;for(int y=ch/2-2;y<ch/2+2;++y)for(int x=cw/2-2;x<cw/2+2;++x)for(int c=0;c<3;++c)avg+=px[size_t(y*cw+x)*4+c];
                avg/=48*255.0f;
                bool ok=std::fabs(avg-expected)<3/255.0f;
                std::printf("HDR GPU probe %d: got %.5f expected %.5f %s\n",test,avg,expected,ok?"PASS":"FAIL");
                if(!ok)failed=true;
                ++probeIndex;frame=0;
                if(probeIndex==11)captured=true;
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
