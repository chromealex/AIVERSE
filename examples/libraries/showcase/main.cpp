// End-to-end host: real RTS sim/bots, RHI view, C++ RmlUi screens and persistent meta rewards.
#include "engine/platform/Platform.h"
#include "engine/platform/EngineDir.h"
#include "engine/render/Renderer3D.h"
#include "engine/ui/UiLayer.h"
#include "engine/i18n/Localization.h"
#include "engine/meta/Meta.h"
#include "modules/rts/RtsModule.h"
#include "modules/rts/RtsAI.h"
#include "modules/rts/RtsScript.h"
#include "modules/rts/view/RtsView.h"
#include "libraries/adapters/rmlui/ScreenBinding.h"
#include <aiverselib/app/Lifecycle.h>
#include <aiverselib/app/MatchBridge.h>
#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/ElementDocument.h>
#include <filesystem>
#include <fstream>
#include <iostream>
using namespace aiverse;
namespace app=aiverse::lib::app;
namespace fs=std::filesystem;
namespace {
struct Showcase {
    std::unique_ptr<Simulation> sim;
    std::vector<game::RtsAI> bots;
    render::Renderer3D renderer;
    game::RtsView view;
    render::RTSCamera camera;
    ui::UiLayer ui;
    i18n::Localization language;
    Rml::ElementDocument* document=nullptr;
    std::unique_ptr<app::RmlScreenBinding> binding;
    app::ScreenStack screens;
    app::Actions actions;
    app::Lifecycle lifecycle;
    std::unique_ptr<meta::Economy> economy;
    fs::path output;
    std::string matchId="showcase-seed3",saveBytes;
    bool rendererReady=false,viewReady=false,uiReady=false;
    bool automatic=false,ready=false,saved=false,restored=false,finished=false,captured=false;
    int frames=0,failures=0;
    std::uint64_t savedHash=0;
    std::size_t built=0,attacks=0;
    std::vector<std::string> savedBots;
    Json load(const std::string& path){std::string text;if(!platform::readContentFile(path,text))throw std::runtime_error("missing content: "+path);return Json::parse(text);}
    void setupGame(){
        fs::create_directories(output);
        language.load([](const std::string& path,std::string& text){return platform::readContentFile(path,text);},"en",{"engine/lang","lang"});
        meta::Config config;config.assets["gold"]={1000000,Json::object()};meta::Profile profile;profile.player="local";
        if(fs::exists(output/"profile.json"))profile=meta::Profile::load(Json::parseFile((output/"profile.json").string()),config);
        economy=std::make_unique<meta::Economy>(config,profile,[this](std::int64_t expected,const Json& next){
            // Single-process demo store. Real multi-process/backend storage must provide atomic CAS.
            auto path=output/"profile.json";
            if(fs::exists(path)&&Json::parseFile(path.string()).getInt("revision")!=expected)return false;
            auto temporary=output/"profile.tmp";std::ofstream file(temporary);file<<next.dump();file.close();if(!file)return false;
            std::error_code error;fs::rename(temporary,path,error);return !error;
        });
        auto manifest=load("game.json");auto map=load(manifest["maps"][std::size_t(0)].getStr("file"));auto rules=load(manifest.getStr("rules","rules.json"));
        std::string error;auto read=[](const std::string& p,std::string& out){return platform::readContentFile(p,out);};
        if(!game::embedScripts(rules,map,read,&error))throw std::runtime_error(error);
        SimConfig cfg;cfg.seed=3;sim=std::make_unique<Simulation>(cfg,std::make_unique<game::RtsModule>());sim->setup(rules,map);
        for(const auto& player:sim->players())bots.emplace_back(player.slot,3);
    }
    void setup(gfx::Device& device){
        setupGame();
        auto& module=static_cast<game::RtsModule&>(sim->module());auto& grid=module.state(*sim).grid;
        renderer.init(device);rendererReady=true;renderer.setShadowQuality(render::ShadowQuality::Low);
        viewReady=view.init(device,module.rules(),platform::readContentFile);if(!viewReady)throw std::runtime_error("view init failed");view.buildTerrain(device,grid);
        camera.target={grid.w*0.5f,0,grid.h*0.5f};camera.distance=64;camera.pitch=0.95f;camera.yaw=0.4f;
        uiReady=ui.init(device);if(!uiReady)throw std::runtime_error("UI init failed");
        document=ui.context()->LoadDocumentFromMemory(R"(<rml><head><title>Library showcase</title><style>
body { font-family: ui; font-size: 18dp; color: #ecf3ff; }
#panel { position: absolute; left: 18dp; top: 18dp; width: 350dp; background-color: #101b2ee8; padding: 18dp; }
h1 { font-size: 23dp; color: #7ed9ff; margin-bottom: 18dp; }
.row { margin-bottom: 12dp; } .row span { display: inline-block; margin-right: 14dp; }
button { display: block; width: 100%; padding: 9dp; background-color: #285d82; color: #ffffff; margin-top: 5dp; }
button:hover { background-color: #347dac; }
</style></head><body><div id="panel"/></body></rml>)");
        if(!document)throw std::runtime_error("document load failed");
        binding=std::make_unique<app::RmlScreenBinding>(*document->GetElementById("panel"),screens,actions,"lab");document->Show();
        lifecycle.send(app::AppEvent::Ready);ready=true;refresh();
    }
    void refresh(){
        auto tr=[&](const std::string& key){return language.tr(key);};
        app::Screen screen{"lab","library.showcase.title",app::ScreenKind::Hud,false,{}};
        screen.rows={{"time","library.showcase.time",std::to_string(sim->tick()),"","",true},{"build","library.showcase.build",std::to_string(built),"","",true},{"combat","library.showcase.combat",std::to_string(attacks),"","",true},
            {"reward","library.showcase.reward",std::to_string(economy->profile().balances.count("gold")?economy->profile().balances.at("gold"):0),"","",true},
            {"result","library.showcase.result",tr(finished?"library.showcase.finished":lifecycle.phase==app::AppPhase::Menu?"library.showcase.waiting":"library.showcase.running"),"","",true},
            {"restore","library.showcase.restore",tr(restored?"library.showcase.yes":"library.showcase.no"),"","",true},
            {"start","library.showcase.start","","start","",lifecycle.phase==app::AppPhase::Menu},
            {"pause","library.showcase.pause","","pause","",lifecycle.phase==app::AppPhase::Playing||lifecycle.phase==app::AppPhase::Paused},
            {"save","library.showcase.save","","save","",lifecycle.phase==app::AppPhase::Playing||lifecycle.phase==app::AppPhase::Paused}};
        binding->show(std::move(screen),tr);
    }
    void saveRestore(){
        saveBytes=sim->serialize();savedHash=sim->checksum();savedBots.clear();for(const auto& bot:bots)savedBots.push_back(bot.saveState());
        std::ofstream file(output/"match.save",std::ios::binary);file.write(saveBytes.data(),static_cast<std::streamsize>(saveBytes.size()));file.close();if(!file)throw std::runtime_error("save write failed");
        // Exercise the exact saved bytes after perturbing the running state.
        sim->step();std::ifstream input(output/"match.save",std::ios::binary);std::string bytes((std::istreambuf_iterator<char>(input)),{}),error;
        restored=sim->deserialize(bytes,&error)&&sim->checksum()==savedHash;
        for(std::size_t i=0;i<bots.size();++i)restored=bots[i].loadState(savedBots[i])&&restored;
        saved=true;if(!restored)throw std::runtime_error("save round trip failed: "+error);
    }
    void advanceGame(){
        for(const auto& action:actions.drain()){
            if(action.id=="start"&&lifecycle.send(app::AppEvent::Start))lifecycle.loaded(lifecycle.generation);
            if(action.id=="pause")lifecycle.send(lifecycle.phase==app::AppPhase::Paused?app::AppEvent::Resume:app::AppEvent::Pause);
            if(action.id=="save")saveRestore();
        }
        if(lifecycle.phase==app::AppPhase::Playing){
            for(int i=0;i<(automatic?100:1)&&sim->result()==0;++i){
                for(auto& bot:bots)bot.update(*sim);sim->step();
                for(const auto& event:sim->events()){if(event.type==game::kEvBuildDone)++built;if(event.type==game::kEvAttack)++attacks;}
                if(automatic&&!saved&&sim->tick()>=100)saveRestore();
            }
            if(sim->result()!=0){
                finished=true;lifecycle.send(app::AppEvent::Finish);
                const app::RewardReceipt receipt{matchId,"local","gold",100};auto first=app::grantMatch(*economy,receipt);auto again=app::grantMatch(*economy,receipt);
                if(!first||!static_cast<bool>(*first)||!again||again->code!=meta::Code::Duplicate)throw std::runtime_error("reward idempotency failed");
                if(!built||!attacks||!restored)throw std::runtime_error("acceptance chain incomplete");
                std::cout<<"PASS build="<<built<<" attacks="<<attacks<<" restore="<<restored<<" result="<<sim->result()<<" reward="<<economy->profile().balances.at("gold")<<'\n';
            }
        }
    }
    void frame(gfx::Device& device){
        if(!ready)return;
        ++frames;
        if(automatic&&frames==2)document->GetElementById("start")->DispatchEvent("click",{});
        advanceGame();
        refresh();ui.update();
        device.beginFrame(platform::framebufferWidth(),platform::framebufferHeight());
        game::RtsScene scene;game::sceneFromSim(*sim,scene,1.0f,0);
        renderer.beginScene(camera,device.framebufferWidth(),device.framebufferHeight());auto& module=static_cast<game::RtsModule&>(sim->module());
        view.draw(renderer,scene,sim->tick()/20.0,nullptr,module.state(*sim).grid);gfx::PassDesc pass;renderer.render(pass);ui.render();device.endPass();
        if(automatic&&finished&&!captured){captured=true;device.captureNextFrame([this](int w,int h,std::vector<std::uint8_t> rgba){
            if(w<=0||h<=0){++failures;}else{std::ofstream image(output/"showcase.ppm",std::ios::binary);image<<"P6\n"<<w<<' '<<h<<"\n255\n";for(std::size_t i=0;i<rgba.size();i+=4)image.write(reinterpret_cast<const char*>(rgba.data()+i),3);}
            platform::requestQuit();});}
        device.endFrame();
        if(automatic&&frames>1000){++failures;platform::requestQuit();}
    }
    void shutdown(gfx::Device* device){binding.reset();if(document)document->Close();if(uiReady)ui.shutdown();if(device&&viewReady)view.shutdown(*device);if(rendererReady)renderer.shutdown();}
};
}
int aiverseMain(int argc,char** argv){
    bool verify=false;std::string project;
    Showcase app;app.output=fs::temp_directory_path()/"rts-library-showcase";
    for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--verify"){verify=true;app.automatic=true;}else if(arg=="--project"&&i+1<argc)project=argv[++i];else if(arg=="--auto")app.automatic=true;else if(arg=="--output"&&i+1<argc)app.output=argv[++i];}
    if(verify){
        platform::HostedView host;
        host.contentRoot=project.empty()?(std::getenv("AIVERSE_CONTENT")?std::getenv("AIVERSE_CONTENT"):"templates/skirmish"):project;
        host.engineAssets=platform::resolveEngineDir(AIVERSE_LIBRARY_ENGINE_ROOT,argv[0])+"/engine/assets";
        platform::setHostedView(&host);
        try{
            app.setupGame();app.lifecycle.send(app::AppEvent::Ready);app.actions.push({"start",""});
            while(!app.finished&&app.sim->tick()<36000)app.advanceGame();
            platform::setHostedView(nullptr);return app.finished?0:1;
        }catch(const std::exception& e){std::cerr<<e.what()<<'\n';platform::setHostedView(nullptr);return 1;}
    }
    gfx::Device* device=nullptr;platform::AppConfig config;config.title="AIVERSE library showcase";config.width=1280;config.height=800;
    platform::AppCallbacks callbacks;
    callbacks.init=[&](gfx::Device& d){device=&d;try{app.setup(d);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';++app.failures;platform::requestQuit();}};
    callbacks.frame=[&](gfx::Device& d,double){try{app.frame(d);}catch(const std::exception& e){std::cerr<<e.what()<<'\n';++app.failures;platform::requestQuit();}};
    callbacks.event=[&](const platform::Event& e){if(app.ready)app.ui.handleEvent(e);};callbacks.shutdown=[&]{app.shutdown(device);};
    auto result=platform::run(config,std::move(callbacks));return app.failures?1:result;
}
