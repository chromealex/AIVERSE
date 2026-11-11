#include "engine/physics/World.h"
#include "engine/platform/Platform.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/i18n/Localization.h"
#include "imgui.h"
#include <algorithm>
#include <cstdio>
using namespace aiverse;
using namespace aiverse::render;
namespace {
Json content(const std::string& path){std::string s;if(!platform::readContentFile(path,s))throw JsonError("Cannot read "+path);return Json::parse(s);}
struct Sandbox {
    Json scene;
    std::unique_ptr<physics::World> world;
    Renderer3D renderer;
    ImGuiLayer gui;
    RTSCamera camera;
    i18n::Localization loc;
    std::vector<Mesh> meshes;
    gfx::Device* device=nullptr;
    bool paused=false;
    double accumulator=0;
    std::string snapshot,error,language;
    void init(gfx::Device& d){
        device=&d;scene=content(std::string(AIVERSE_SOURCE_DIR)+"/examples/physics/scene.json");
        world=std::make_unique<physics::World>(scene);
        for(auto lang:{"en","ru"})loc.addTable(lang,content(std::string(AIVERSE_SOURCE_DIR)+"/engine/assets/lang/"+lang+".json"));
        loc.setLanguage(language.empty()?i18n::negotiate(i18n::systemLanguages(),{"en","ru"},"en"):language);
        renderer.init(d);renderer.setShadowQuality(ShadowQuality::Medium);gui.init(d,AIVERSE_SOURCE_DIR "/engine/assets/fonts/ui.ttf",16);
        camera.target={0,2,0};camera.distance=25;camera.pitch=.55f;camera.yaw=.25f;
        for(auto& o:world->objects()){
            MeshBuilder m;m.color(rgbaf({o.color.x,o.color.y,o.color.z,1}));
            if(o.shape=="box")m.box({0,0,0},{o.size.x,o.size.y,o.size.z});
            else if(o.shape=="sphere")m.sphere({0,0,0},o.size.x,16,24);
            else {m.cylinder({0,-o.size.y,0},o.size.x,o.size.y*2,24);m.sphere({0,-o.size.y,0},o.size.x,12,24);m.sphere({0,o.size.y,0},o.size.x,12,24);}
            meshes.push_back(m.upload(d));
        }
    }
    bool button(const char* key){return ImGui::Button(loc.tr(key).c_str());}
    void frame(gfx::Device& d,double dt){
        // Presentation accumulator is local to the sandbox, never RTS game logic.
        if(!paused){accumulator+=std::min(dt,.1);while(accumulator>=1.0/world->tickRate()){world->step();accumulator-=1.0/world->tickRate();}}
        d.beginFrame(platform::framebufferWidth(),platform::framebufferHeight());gui.newFrame(dt);
        renderer.beginScene(camera,d.framebufferWidth(),d.framebufferHeight());
        for(size_t i=0;i<meshes.size();++i){auto t=b3Body_GetTransform(world->objects()[i].body);renderer.drawMesh(meshes[i],mat4::trs({t.p.x,t.p.y,t.p.z},{t.q.v.x,t.q.v.y,t.q.v.z,t.q.s},{1,1,1}),{1,1,1,1});}
        auto a=b3Body_GetPosition(world->find("anchor")->body),b=b3Body_GetPosition(world->find("pendulum")->body);
        renderer.line({a.x,a.y,a.z},{b.x,b.y,b.z},rgba(255,210,110));
        gfx::PassDesc pass;pass.clearColor[0]=.07f;pass.clearColor[1]=.09f;pass.clearColor[2]=.13f;renderer.render(pass);
        ImGui::SetNextWindowPos({16,16},ImGuiCond_Once);ImGui::SetNextWindowSize({320,0},ImGuiCond_Once);
        ImGui::Begin(loc.tr("physicsDemo.title").c_str(),nullptr,ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::TextUnformatted(loc.tr("physicsDemo.description").c_str());
        ImGui::TextUnformatted(loc.tr("physicsDemo.tick",i18n::Args().num("tick",world->tick())).c_str());
        if(button(paused?"physicsDemo.resume":"physicsDemo.pause"))paused=!paused;
        ImGui::SameLine();if(button("physicsDemo.reset")){world=std::make_unique<physics::World>(scene);accumulator=0;}
        if(button("physicsDemo.launch"))world->impulse("ball",{1500,5000,0});
        if(button("physicsDemo.save"))snapshot=world->save();
        ImGui::SameLine();ImGui::BeginDisabled(snapshot.empty());if(button("physicsDemo.restore")){world->restore(snapshot,&error);accumulator=0;}ImGui::EndDisabled();
        ImGui::SliderFloat(loc.tr("physicsDemo.orbit").c_str(),&camera.yaw,-3.14f,3.14f);
        ImGui::SliderFloat(loc.tr("physicsDemo.zoom").c_str(),&camera.distance,15,40);
        if(!error.empty())ImGui::TextUnformatted(error.c_str());
        ImGui::End();gui.render();d.endPass();d.endFrame();
    }
    void shutdown(){for(auto& m:meshes){device->destroyBuffer(m.vb);device->destroyBuffer(m.ib);}gui.shutdown();renderer.shutdown();world.reset();}
};
Sandbox demo;
}
int aiverseMain(int argc,char** argv){
    if(argc==3&&std::string(argv[1])=="--lang")demo.language=argv[2];
    platform::AppConfig cfg;cfg.title="AIVERSE — Box3D";cfg.width=1280;cfg.height=800;
    platform::AppCallbacks cb;cb.init=[](gfx::Device& d){demo.init(d);};cb.frame=[](gfx::Device& d,double dt){demo.frame(d,dt);};
    cb.event=[](const platform::Event& e){demo.gui.handleEvent(e);};cb.shutdown=[](){demo.shutdown();};
    return platform::run(cfg,cb);
}
