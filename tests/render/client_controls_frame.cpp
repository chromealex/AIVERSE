// Actual hosted client, stock RmlUi HUD and ordinary platform input on Metal.
#include "apps/client/GameClient.h"
#include "engine/render/RenderTargetPool.h"
#include "engine/render/CameraPng.h"
#include <RmlUi/Core.h>
#include <filesystem>
#include <iostream>
using namespace aiverse;
namespace {
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
Rml::Element* element(const char* selector) {
    auto* context=Rml::GetContext("main");if(!context)return nullptr;
    for(int i=0;i<context->GetNumDocuments();++i)
        if(context->GetDocument(i)->IsVisible())
            if(auto* e=context->GetDocument(i)->QuerySelector(selector);e&&e->IsVisible(true))return e;
    return nullptr;
}
Rml::Element* button(const char* text) {
    auto* context=Rml::GetContext("main");if(!context)return nullptr;
    for(int i=0;i<context->GetNumDocuments();++i){
        if(!context->GetDocument(i)->IsVisible())continue;
        Rml::ElementList buttons;context->GetDocument(i)->QuerySelectorAll(buttons,"button");
        for(auto* e:buttons)if(e->IsVisible(true)&&e->GetInnerRML().find(text)!=std::string::npos)return e;
    }return nullptr;
}
}
int aiverseMain(int argc,char** argv) {
    require(argc>=3,"usage: client_controls_frame PROJECT ENGINE_ASSETS [CAPTURES]");
    client::GameClient client;render::CameraRenderTargetPool targets;
    platform::HostedView hosted;hosted.width=hosted.fbWidth=1280;hosted.height=hosted.fbHeight=760;
    hosted.contentRoot=argv[1];hosted.engineAssets=argv[2];
    const std::string capture=argc>3?argv[3]:"";
    gfx::Device* device=nullptr;int frames=0,ready=0,stage=0;bool failed=false,pending=false,done=false;Rml::ElementDocument* typing=nullptr;
    auto key=[&](platform::Key k){platform::Event e;e.type=platform::EventType::KeyDown;e.key=k;client.event(e);e.type=platform::EventType::KeyUp;client.event(e);};
    auto click=[&](float x,float y){platform::Event e;e.x=x;e.y=y;e.type=platform::EventType::MouseMove;client.event(e);e.type=platform::EventType::MouseDown;e.button=0;client.event(e);e.type=platform::EventType::MouseUp;client.event(e);};
    auto tap=[&](float x,float y){platform::Event e;e.x=x;e.y=y;e.touchId=17;e.type=platform::EventType::TouchBegin;client.event(e);e.type=platform::EventType::TouchEnd;client.event(e);};
    platform::AppConfig app;app.title="Client controls acceptance";app.width=1280;app.height=760;app.highDpi=false;
    platform::AppCallbacks cb;
    cb.init=[&](gfx::Device& d){device=&d;platform::setHostedView(&hosted);try {
        require(targets.init(d),"target pool init");client.forceLanguage("en");client.init(d);
        require(client.errorText().empty(),client.errorText().c_str());client.autostart(0);
        if(!capture.empty())std::filesystem::create_directories(capture);
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';failed=true;}platform::setHostedView(nullptr);};
    cb.frame=[&](gfx::Device& d,double){
        if(failed||done){platform::requestQuit();return;}
        platform::setHostedView(&hosted);
        try {
            require(++frames<1200,"client acceptance timeout");
            const auto* target=targets.acquire(1,1280,760);require(target,"target allocation");
            client.setHostTarget(target->color,target->depth,1280,760);client.frame(d,1.0/60);
            require(client.errorText().empty(),client.errorText().c_str());
            if(!client.inMatch()||pending){platform::setHostedView(nullptr);return;}
            if(++ready%12){platform::setHostedView(nullptr);return;}
            auto snapshot=[&](const char* name){if(capture.empty())return;pending=true;
                require(d.captureTexture(target->color,[&,name](int w,int h,std::vector<uint8_t> pixels){
                    if(!render::cameraWritePngRgba8((std::filesystem::path(capture)/(std::string(name)+".png")).string(),w,h,pixels))failed=true;
                    pending=false;
                }),"capture submit");};
            switch(stage++) {
            case 0:require(element(".selection-shortcut"),"shortcut shown");require(element(".selection-shortcut")->GetInnerRML().find("F6")!=std::string::npos,"shortcut key shown");key(platform::Key::F6);break;
            case 1:require(button("Move"),"hotkey selects mobile command card");snapshot("scout");key(platform::Key::F6);break;
            case 2:key(platform::Key::M);break;
            case 3:require(element("#modehint")&&element("#modehint")->GetInnerRML().find("Move")!=std::string::npos,"M activates Move targeting");snapshot("move");click(740,330);break;
            case 4:{bool moving=false;client.simulation()->world().each<game::Mobile>([&](Entity,const auto& m){moving|=m.moving;});require(moving,"mouse target creates movement");key(platform::Key::F7);break;}
            case 5:{require(button("Attack")&&button("Stop")&&!button("Move"),"view.select armed structure card");
                auto* aura=button("Guard aura");require(aura&&aura->IsClassSet("disabled"),"passive aura is disabled card button");
                require(aura->QuerySelector(".tip"),"passive aura tooltip");
                const auto p=aura->GetAbsoluteOffset(Rml::BoxArea::Border);platform::Event hover;hover.type=platform::EventType::MouseMove;hover.x=p.x+8;hover.y=p.y+8;client.event(hover);break;}
            case 6:{require(button("Guard aura")->QuerySelector(".tip")->IsVisible(true),"hover reveals passive description");snapshot("tower");auto* context=Rml::GetContext("main");typing=context->LoadDocumentFromMemory("<rml><body style='position:absolute;left:10px;top:80px;width:200px;height:40px'><input id='typing' type='text'/></body></rml>");
                require(typing,"typing document");typing->Show();context->Update();auto* input=typing->GetElementById("typing");require(input&&input->Focus(),"text input focus");
                key(platform::Key::F6);break;}
            case 7:require(button("Attack")&&!button("Move"),"shortcut suppressed while typing");typing->Close();typing=nullptr;break;
            case 8:key(platform::Key::F6);break;
            case 9:require(button("Move"),"shortcut works after text blur");key(platform::Key::S);hosted.touch=true;break;
            case 10:{bool moving=false;client.simulation()->world().each<game::Mobile>([&](Entity,const auto& m){moving|=m.moving;});require(!moving,"Stop before touch order");
                auto* move=button("Move");require(move,"touch Move card");const auto p=move->GetAbsoluteOffset(Rml::BoxArea::Border);const auto size=move->GetBox().GetSize(Rml::BoxArea::Border);tap(p.x+size.x/2,p.y+size.y/2);break;}
            case 11:require(element("#modehint")&&element("#modehint")->GetInnerRML().find("Move")!=std::string::npos,"touch button activates Move");tap(540,300);break;
            case 12:{bool moving=false;client.simulation()->world().each<game::Mobile>([&](Entity,const auto& m){moving|=m.moving;});require(moving,"touch target creates movement");snapshot("touch");hosted.touch=false;key(platform::Key::Escape);break;}
            case 13:require(button("Quit game"),"pause quit button");snapshot("pause");break;
            case 14:{auto* quit=button("Quit game");require(quit,"quit still visible");const auto p=quit->GetAbsoluteOffset(Rml::BoxArea::Border);const auto size=quit->GetBox().GetSize(Rml::BoxArea::Border);click(p.x+size.x/2,p.y+size.y/2);break;}
            case 15:require(hosted.quitRequested,"quit requests native host termination");done=true;std::cout<<"PASS: shortcut, text suppression, mouse/touch Move, structure/passive card, pause quit\n";break;
            }
        }catch(const std::exception& e){std::cerr<<"stage "<<stage<<": "<<e.what()<<'\n';failed=true;}
        platform::setHostedView(nullptr);
    };
    cb.shutdown=[&]{platform::setHostedView(&hosted);if(device&&device->waitForResourceIdle()){client.shutdown();targets.shutdownAfterGpuIdle();}else failed=true;platform::setHostedView(nullptr);if(failed||!done){std::fflush(nullptr);std::exit(1);}};
    platform::run(app,cb);return failed||!done?1:0;
}
