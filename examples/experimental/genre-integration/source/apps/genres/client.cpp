#include "modules/genres/BoardModule.h"
#include "modules/genres/MotionModule.h"
#include "engine/platform/Platform.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/i18n/Localization.h"
#include "imgui.h"
#include <cstdio>
#include <set>
using namespace aiverse;
namespace {
render::float3 visual(Vec3 p){return {float(p.x.toDouble()),float(p.y.toDouble()),float(p.z.toDouble())};}
struct App {
    std::unique_ptr<Simulation> sim;
    genres::BoardModule* board=nullptr;
    genres::MotionModule* motion=nullptr;
    render::Renderer3D renderer;
    render::ImGuiLayer gui;
    render::Mesh cube;
    i18n::Localization lang;
    std::set<platform::Key> keys;
    std::string saved,language="en";
    int selected=-1;
    bool paused=false,automatic=false;
    double accumulated=0;
    void init(gfx::Device& device) {
        renderer.init(device);renderer.setShadowQuality(render::ShadowQuality::Low);
        std::string font;auto fontPath=platform::readContentFile("engine/fonts/ui.ttf",font)?"engine/fonts/ui.ttf":"engine/assets/fonts/ui.ttf";
        gui.init(device,fontPath,20);
        render::MeshBuilder builder;builder.color(render::rgba(255,255,255)).box({0,0.5f,0},{1,1,1});cube=builder.upload(device);
        lang.load([](const auto& path,auto& data){return platform::readContentFile(path,data);},language,{"engine/assets/lang","engine/lang"});
    }
    std::string tr(const char* key){return lang.tr(key);}
    void event(const platform::Event& e) {
        using E=platform::EventType;using K=platform::Key;
        bool captured=gui.handleEvent(e);
        if(e.type==E::FocusLost){keys.clear();paused=true;}
        if(e.type==E::KeyUp)keys.erase(e.key);
        if(e.type==E::KeyDown&&!e.repeat&&e.key==K::Escape){paused=!paused;keys.clear();}
        else if(e.type==E::KeyDown&&!captured&&!paused)keys.insert(e.key);
    }
    void submit(uint16_t type,int64_t param=0,int other=0) {
        if(paused||sim->result())return;
        Command c;c.player=1;c.type=type;c.param=param;c.point.x=Fixed::fromInt(other);sim->submit(c);
    }
    void box(const world::Bounds& b,render::float4 color) {
        auto base=Vec3{(b.min.x+b.max.x)/2,b.min.y,(b.min.z+b.max.z)/2};
        renderer.drawMesh(cube,render::mat4::translate(visual(base))*render::mat4::scale(visual(b.max-b.min)),color);
    }
    void boardUi() {
        auto& s=board->state(*sim);
        ImGui::BeginDisabled(paused||sim->result()!=0||automatic);
        if(board->kind()==genres::BoardKind::Cards) {
            ImGui::TextUnformatted(lang.tr("genres.card.status",i18n::Args().num("turn",s.turn).num("player",s.active+1)).c_str());
            for(size_t i=0;i<s.players.size();++i) {
                const auto& p=s.players[i];
                ImGui::TextUnformatted(lang.tr("genres.card.player",i18n::Args().num("player",int64_t(i+1)).num("health",p.health).num("guard",p.guard).num("energy",p.energy)).c_str());
            }
            ImGui::Separator();ImGui::TextUnformatted(tr("genres.card.hand").c_str());
            auto& p=s.players[0];ImGui::BeginDisabled(s.active!=0);
            for(size_t i=0;i<p.hand.size();++i) {
                if(i%5)ImGui::SameLine();ImGui::PushID(int(i));
                const auto& d=board->cards()[size_t(p.hand[i])];
                auto label=lang.tr("genres.card.button",i18n::Args().str("card",lang.trOr("genres.card."+d.id,d.id)).num("cost",d.cost).num("damage",d.damage).num("guard",d.guard));
                ImGui::BeginDisabled(p.energy<d.cost);
                if(ImGui::Button(label.c_str(),{155,100}))submit(genres::PlayCard,int64_t(i));
                ImGui::EndDisabled();ImGui::PopID();
            }
            if(ImGui::Button(tr("genres.card.end").c_str()))submit(genres::EndTurn);
            ImGui::EndDisabled();
        }else {
            ImGui::TextUnformatted(lang.tr("genres.puzzle.status",i18n::Args().num("moves",s.moves).num("score",s.score)).c_str());
            ImGui::TextUnformatted(tr("genres.puzzle.controls").c_str());
            ImGui::BeginDisabled(s.resolving);
            for(size_t i=0;i<s.cells.size();++i) {
                if(i%size_t(board->width()))ImGui::SameLine();ImGui::PushID(int(i));
                float hue=float(s.cells[i]-1)/7;ImVec4 color=ImColor::HSV(hue,0.6f,selected==int(i)?1.f:0.65f);
                ImGui::PushStyleColor(ImGuiCol_Button,color);
                if(ImGui::Button(std::to_string(s.cells[i]).c_str(),{56,56})) {
                    if(selected<0)selected=int(i);else {submit(genres::SwapCells,selected,int(i));selected=-1;}
                }
                ImGui::PopStyleColor();ImGui::PopID();
            }
            ImGui::EndDisabled();
        }
        ImGui::EndDisabled();
    }
    void frame(gfx::Device& device,double dt) {
        if(gui.wantsKeyboard())keys.clear();
        if(!paused&&!sim->result()) {
            accumulated+=std::min(dt,0.25);int budget=8;
            while(accumulated>=1.0/sim->tickRate()&&budget--) {
                if(motion&&!automatic) {
                    using K=platform::Key;Command c;c.player=1;c.type=genres::kMotionInput;
                    auto axis=motion->kind()==genres::MotionKind::Platformer?
                        (keys.contains(K::D)?1000:0)-(keys.contains(K::A)?1000:0):
                        (keys.contains(K::W)?1000:0)-(keys.contains(K::S)?1000:0);
                    auto steer=(keys.contains(K::D)?1000:0)-(keys.contains(K::A)?1000:0);
                    c.args={axis,steer,keys.contains(K::Space)?1:0};sim->submit(c);
                }
                sim->step();accumulated-=1.0/sim->tickRate();
            }
        }
        device.beginFrame(platform::framebufferWidth(),platform::framebufferHeight());gui.newFrame(dt);
        render::RTSCamera cam;
        if(motion) {
            auto& s=motion->state(*sim);cam.target=visual(s.actors[0].body.pos);cam.distance=25;cam.ortho=true;
            cam.pitch=motion->kind()==genres::MotionKind::Platformer?0.f:1.3f;cam.yaw=0;
            renderer.beginScene(cam,device.framebufferWidth(),device.framebufferHeight());
            for(const auto& [id,c]:motion->collision().colliders()){(void)id;box(c.bounds,{0.24f,0.3f,0.4f,1});}
            for(const auto& c:motion->checkpoints())box(c,{0.2f,0.7f,0.35f,1});
            for(const auto& a:s.actors)box({a.body.pos-Vec3{Fixed::fromRatio(3,10),{},Fixed::fromRatio(3,10)},a.body.pos+Vec3{Fixed::fromRatio(3,10),Fixed::fromRatio(18,10),Fixed::fromRatio(3,10)}},{0.9f,0.5f,0.15f,1});
        }else renderer.beginScene(cam,device.framebufferWidth(),device.framebufferHeight());
        gfx::PassDesc pass;pass.clearColor[0]=0.06f;pass.clearColor[1]=0.08f;pass.clearColor[2]=0.12f;renderer.render(pass);
        ImGui::SetNextWindowPos({20,20},ImGuiCond_Once);ImGui::SetNextWindowSize(board?ImVec2(900,580):ImVec2(420,230),ImGuiCond_Once);
        ImGui::Begin(tr("genres.title").c_str());
        if(ImGui::Button(tr(paused?"genres.resume":"genres.pause").c_str())){paused=!paused;keys.clear();}
        ImGui::SameLine();if(ImGui::Button(tr("genres.save").c_str()))saved=sim->serialize();
        ImGui::SameLine();ImGui::BeginDisabled(saved.empty());
        if(ImGui::Button(tr("genres.restore").c_str())) {std::string error;if(!sim->deserialize(saved,&error))std::fprintf(stderr,"%s\n",error.c_str());selected=-1;keys.clear();accumulated=0;}
        ImGui::EndDisabled();
        if(sim->result())ImGui::TextUnformatted(lang.tr("genres.finished",i18n::Args().num("winner",sim->result())).c_str());
        if(board)boardUi();
        else {
            ImGui::TextWrapped("%s",tr(motion->kind()==genres::MotionKind::Platformer?"genres.platform.controls":"genres.racing.controls").c_str());
            auto& p=motion->state(*sim).actors[0];
            ImGui::TextUnformatted(lang.tr("genres.motion.status",i18n::Args().num("lap",p.lap).num("checkpoint",p.checkpoint).num("deaths",p.deaths)).c_str());
        }
        ImGui::End();gui.render();device.endPass();device.endFrame();
    }
    void shutdown(gfx::Device& device){gui.shutdown();renderer.shutdown();device.destroyBuffer(cube.vb);device.destroyBuffer(cube.ib);}
};
}
int aiverseMain(int argc,char** argv) {
    try {
        App app;std::string root,mapFile;
        for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--project"&&i+1<argc)root=argv[++i];else if(arg=="--map"&&i+1<argc)mapFile=argv[++i];else if(arg=="--auto")app.automatic=true;else if(arg=="--lang"&&i+1<argc)app.language=argv[++i];else throw std::runtime_error("usage: aiverse_genre_client --project DIR [--auto] [--lang en|ru]");}
        platform::AppConfig cfg;cfg.title="AIVERSE";cfg.width=1200;cfg.height=800;
        gfx::Device* device=nullptr;platform::AppCallbacks cb;
        cb.init=[&](auto& d){device=&d;
        auto read=[&](const std::string& file){std::string text;if(!platform::readContentFile(root.empty()?file:root+"/"+file,text))throw std::runtime_error(std::string("cannot read ")+file);return Json::parse(text);};
        Json manifest;std::string manifestText;
        if(platform::readContentFile(root.empty()?"game.json":root+"/game.json",manifestText))manifest=Json::parse(manifestText);
        auto rules=read(manifest.getStr("rules","rules.json"));
        Json map;
        if(mapFile.empty())map=read(manifest["maps"][0].getStr("file","map.json"));
        else {std::string text;if(!platform::readContentFile(mapFile,text))throw JsonError("cannot read selected map");map=Json::parse(text);}
        if(!app.automatic&&!map["players"].items().empty())map.member("players").itemsMut()[0].set("bot",Json::fromBool(false));
        std::unique_ptr<GameModule> module;
        if(rules.has("board")){auto m=std::make_unique<genres::BoardModule>();app.board=m.get();module=std::move(m);}
        else {auto m=std::make_unique<genres::MotionModule>();app.motion=m.get();module=std::move(m);}
        app.sim=std::make_unique<Simulation>(SimConfig{17,60,0},std::move(module));app.sim->setup(rules,map);
        app.init(d);};cb.frame=[&](auto& d,double dt){app.frame(d,dt);};cb.event=[&](const auto& e){app.event(e);};cb.shutdown=[&]{app.shutdown(*device);};
        return platform::run(cfg,cb);
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
