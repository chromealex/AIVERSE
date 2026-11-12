#include <aiverselib/net/Admission.h>
#include "engine/net/authority/Session.h"
#include "modules/fps/Project.h"
#include "modules/fps/view/FirstPersonCamera.h"
#include "engine/net/Udp.h"
#include "engine/net/WebSocket.h"
#include "engine/platform/Platform.h"
#include "engine/world/ChunkScene.h"
#include "engine/world/WorldScene.h"
#include "engine/platform/ContentReader.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/GeometryScene.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/i18n/Localization.h"
#include "imgui.h"
#include "engine/audio/Audio.h"
#include "engine/platform/EngineDir.h"
#include <fstream>
#include <cstdlib>
#include <cmath>
#include <cstdio>
#include <set>

using namespace aiverse;
namespace {
render::float3 visual(Vec3 v){return {float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};}
struct App {
    Json rules,map;fps::Config config;
    std::unique_ptr<net::authority::Server> server;
    std::unique_ptr<net::authority::Client> client;
#ifndef __EMSCRIPTEN__
    net::UdpListener udp;net::WsListener ws;
#endif
    world::SpatialWorld geometry;world::ChunkScene chunks;std::unique_ptr<world::WorldScene> streamedScene;std::string projectRoot;
    platform::ContentReader contentReader;
    render::GeometryScene staticGeometry;
    render::Renderer3D renderer;render::ImGuiLayer gui;render::Mesh cube;
    i18n::Localization lang;
    std::set<platform::Key> keys;
    fps::Input input;
    bool fireHeld=false,fireQueued=false,aimHeld=false,prone=false;uint32_t buySelection=0;
    audio::AudioEngine audio;double stepSound=0,viewTime=0;uint32_t lastReload=0;
    std::map<uint8_t,std::pair<uint32_t,uint32_t>> heardShots;
    uint32_t shownShots=0,shownHits=0,shownLife=0;
    double flash=0,hitFlash=0;
    double accumulated=0;bool listen=false;
    fps::view::Look look;
    net::authority::Snapshot previousSnapshot,received;uint32_t lastSnapshotTick=0;double snapshotAge=0;
    void init(gfx::Device& device) {
        renderer.init(device);renderer.setShadowQuality(render::ShadowQuality::Low);
        if (const auto file=map.getStr("geometryScene"); !file.empty()) {
            auto reader=[&](const std::string& path,std::string& text){return platform::readContentFile(projectRoot+"/"+path,text);};
            std::string text;if(!reader(file,text))throw std::runtime_error("geometryScene: file not found: "+file);
            staticGeometry.load(device,Json::parse(text),reader);
        }
        gui.init(device,"engine/assets/fonts/ui.ttf",17);
        render::MeshBuilder builder;builder.color(render::rgba(255,255,255)).box({0,0.5f,0},{1,1,1});cube=builder.upload(device);
        lang.load([](const auto& path,auto& out){return platform::readContentFile(path,out);},"en",{"engine/assets/lang"});
        Simulation sim({1,config.tickRate,0},std::make_unique<fps::Module>());sim.setup(rules,map);
        geometry=static_cast<fps::Module&>(sim.module()).collision();
        if(map["world"].getBool("async"))streamedScene=std::make_unique<world::WorldScene>(map["world"],contentReader);
        else chunks.configure(map["world"]);
        audio::AudioConfig ac;
        if(const char* mode=std::getenv("AIVERSE_AUDIO")){ac.openDevice=std::string(mode)!="off";ac.nullBackend=std::string(mode)=="null";}
        if(const char* wav=std::getenv("AIVERSE_AUDIO_CAPTURE"))ac.capturePath=wav;
        audio.init(ac);std::string bank,error;
        if(platform::readContentFile(projectRoot+"/audio.json",bank)||platform::readContentFile("engine/assets/fps_audio.json",bank))if(!audio.loadBank(Json::parse(bank),[](const auto& file,auto& out){return platform::readContentFile(file,out);},&error))std::fprintf(stderr,"FPS audio: %s\n",error.c_str());
    }
    void event(const platform::Event& e) {
        using E=platform::EventType;using K=platform::Key;
        if(e.type==E::FocusLost){platform::setPointerLock(false);keys.clear();input.buttons=0;fireHeld=fireQueued=aimHeld=false;input.action=fps::Action::None;}
        if(e.type==E::KeyDown) {
            if(e.key==K::Escape){platform::setPointerLock(false);keys.clear();input.buttons=0;fireHeld=fireQueued=aimHeld=false;input.action=fps::Action::None;return;}
            if(platform::pointerLocked()){
                if(e.key>=K::Num1&&e.key<=K::Num4&&!e.repeat){buySelection=uint32_t(e.key)-uint32_t(K::Num1);input.buttons=(input.buttons&~uint32_t(fps::WeaponMask))|fps::selectWeapon(buySelection);}
                if(!e.repeat){
                    using A=fps::Action;
                    if(e.key==K::G)input.action=A::Frag;
                    if(e.key==K::X)input.action=A::Smoke;
                    if(e.key==K::C)input.action=A::Flash;
                    if(e.key==K::H)input.action=A::Heal;
                    if(e.key==K::E)input.action=A::Interact;
                    if(e.key==K::Q)input.action=A::Drop;
                    if(e.key==K::P)input.action=A::Plant;
                    if(e.key==K::F)input.action=A::Vault;
                    if(e.key==K::B){input.action=A::Buy;input.argument=buySelection;input.buttons=(input.buttons&~uint32_t(fps::WeaponMask))|fps::selectWeapon(buySelection);}
                    if(e.key>=K::F1&&e.key<=K::F3){input.action=A::Attach;input.argument=6+uint32_t(e.key)-uint32_t(K::F1);}
                    if(e.key==K::V)prone=!prone;
                }
                keys.insert(e.key);if(e.key==K::Space&&!e.repeat)input.buttons|=fps::Jump;}
        }
        if(e.type==E::KeyUp)keys.erase(e.key);
        if(e.type==E::MouseDown&&e.button==0){if(!platform::pointerLocked())platform::setPointerLock(true);if(platform::pointerLocked()){fireHeld=true;fireQueued=true;}}
        if(e.type==E::MouseUp&&e.button==0)fireHeld=false;
        if(e.type==E::MouseDown&&e.button==1&&platform::pointerLocked())aimHeld=true;
        if(e.type==E::MouseUp&&e.button==1)aimHeld=false;
        if(e.type==E::MouseMove&&platform::pointerLocked()) {
            look.motion(e.deltaX,e.deltaY,aimHeld?0.00015f:0.0004f);
        }
    }
    void frame(gfx::Device& device,double dt) {
        const auto now=net::nowMs();
#ifndef __EMSCRIPTEN__
        if(listen) {
            auto admit=[&](auto connections){for(auto& conn:connections){uint8_t slot=0;for(size_t i=2;i<=map["players"].size();++i)if(!server->occupied(uint8_t(i))){slot=uint8_t(i);break;}if(slot)server->attach(slot,std::move(conn));else conn->close();}};
            admit(udp.poll(now));admit(ws.poll(now));
        }
#endif
        client->updateStreaming();client->poll(now);if(server)server->poll(now);
        accumulated+=std::min(dt,0.25);int budget=8;
        while(accumulated>=1.0/config.tickRate&&budget--) {
            using K=platform::Key;
            input.forward=(keys.contains(K::W)?1000:0)-(keys.contains(K::S)?1000:0);
            input.right=(keys.contains(K::D)?1000:0)-(keys.contains(K::A)?1000:0);
            input.buttons&=uint32_t(fps::Jump|fps::WeaponMask);
            if(aimHeld)input.buttons|=fps::Aim;
            if(prone)input.buttons|=fps::Prone;
            if(fireHeld||fireQueued)input.buttons|=fps::Fire;
            if(keys.contains(K::LeftCtrl))input.buttons|=fps::Crouch;
            if(keys.contains(K::LeftShift))input.buttons|=fps::Sprint;
            if(keys.contains(K::R))input.buttons|=fps::Reload;
            if(config.melee.enabled){
                if(keys.contains(K::Z))input.buttons|=fps::Melee;
                if(keys.contains(K::LeftAlt))input.buttons|=fps::Block;
                if(keys.contains(K::T))input.buttons|=fps::LockOn;
            }
            input.yaw=Fixed::fromRaw(int64_t(look.yaw*65536));input.pitch=Fixed::fromRaw(int64_t(look.pitch*65536));
            if(client->submit(input)){fireQueued=false;input.action=fps::Action::None;input.buttons&=~uint32_t(fps::Jump|fps::WeaponMask);}
            if(server){server->poll(now);if(!server->step()){accumulated=0;break;}client->poll(now);}
            accumulated-=1.0/config.tickRate;
        }
        if(!platform::pointerLocked()){keys.clear();input.buttons=0;fireHeld=fireQueued=aimHeld=false;input.action=fps::Action::None;}
        device.beginFrame(platform::framebufferWidth(),platform::framebufferHeight());gui.newFrame(dt);
        auto actor=client->predicted();viewTime+=dt;
        if(actor.reloadUntil&&!lastReload)audio.play("reload");
        lastReload=actor.reloadUntil;
        stepSound+=dt;
        if(actor.grounded&&actor.health>0&&!actor.prone&&!actor.vehicle&&actor.velocity.xz().lengthSq()>1_fx&&stepSound>0.38){audio.play("step");stepSound=0;}
        for(const auto& remote:client->snapshot().actors){
            auto it=heardShots.find(remote.slot);
            if(it!=heardShots.end()&&it->second.first==remote.life&&remote.shots>it->second.second){
                audio::PlayParams params;params.positional=remote.slot!=actor.slot;params.x=float(remote.pos.x.toDouble());params.y=float(remote.pos.y.toDouble());params.z=float(remote.pos.z.toDouble());
                params.volume=(remote.attachments[remote.weaponIndex]&2)?0.22f:0.7f;
                if(params.positional&&geometry.cast(actor.pos+Vec3{{},1_fx,{}},remote.pos-actor.pos).hit)params.volume*=0.25f;
                audio.play("shot"+std::to_string(remote.weaponIndex),params);
            }
            heardShots[remote.slot]={remote.life,remote.shots};
        }
        flash=std::max(0.0,flash-dt);hitFlash=std::max(0.0,hitFlash-dt);
        if(shownLife==actor.life) {
            if(actor.shots>shownShots)flash=0.09;
            if(actor.hits>shownHits)hitFlash=0.18;
        }
        shownLife=actor.life;shownShots=actor.shots;shownHits=actor.hits;
        const auto& weapon=config.weapons[std::min(size_t(actor.weaponIndex),config.weapons.size()-1)];
        snapshotAge+=dt;
        
        if(client->snapshot().tick!=lastSnapshotTick){previousSnapshot=received;received=client->snapshot();lastSnapshotTick=received.tick;snapshotAge=0;}
        float interpolation=float(std::min(1.0,snapshotAge*config.snapshotRate));
        world::Cell origin=geometry.cell(actor.pos);
        Vec3 eye=actor.pos+Vec3{{},fps::stanceHeight(actor,config.motor)-Fixed::fromRatio(1,10),{}};
        auto localEye=visual(world::relativeTo(eye,origin,32));
        auto cameraLook=look;cameraLook.pitch+=float(actor.recoil.toDouble());
        auto cam=cameraLook.camera(localEye);
        if(aimHeld)cam.fovY=(weapon.range>=700_fx||(actor.attachments[actor.weaponIndex]&1))?0.28f:0.65f;
        audio::ListenerPose listener;auto globalEye=visual(eye);listener.pos[0]=globalEye.x;listener.pos[1]=globalEye.y;listener.pos[2]=globalEye.z;auto forward=render::normalize(cam.target-cam.eye());listener.forward[0]=forward.x;listener.forward[1]=forward.y;listener.forward[2]=forward.z;audio.setListener(listener);
        if(streamedScene)streamedScene->update(device,{&actor.pos,1});
        renderer.beginScene(cam,device.framebufferWidth(),device.framebufferHeight());
        if(streamedScene)streamedScene->draw(renderer,origin,32);
        staticGeometry.draw(renderer);
        chunks.update(actor.pos,2,2,[&](const auto& file,auto& data){return platform::readContentFile(projectRoot+"/"+file,data);});
        for(const auto& [cell,boxes]:chunks.chunks())for(const auto& b:boxes) {
            (void)cell;
            auto base=Vec3{(b.min.x+b.max.x)/2,b.min.y,(b.min.z+b.max.z)/2};
            auto transform=render::mat4::translate(visual(world::relativeTo(base,origin,32)))*render::mat4::scale(visual(b.max-b.min));
            renderer.drawMesh(cube,transform,{0.4f,0.46f,0.5f,1});
        }
        Vec3 extent{600_fx,600_fx,600_fx};
        for(auto id:geometry.query({actor.pos-extent,actor.pos+extent})) {
            if(chunks.enabled()&&id!=1)continue;
            const auto& b=geometry.colliders().at(id).bounds;
            // Draw relative to the observer before converting coordinates to floating point.
            auto base=Vec3{(b.min.x+b.max.x)/2,b.min.y,(b.min.z+b.max.z)/2};
            auto transform=render::mat4::translate(visual(world::relativeTo(base,origin,32)))*render::mat4::scale(visual(b.max-b.min));
            renderer.drawMesh(cube,transform,id==1?render::float4{0.18f,0.23f,0.22f,1}:render::float4{0.4f,0.46f,0.5f,1});
        }
        for(const auto& remote:client->snapshot().actors)if(remote.slot!=actor.slot&&remote.health>0) {
            auto position=visual(world::relativeTo(remote.pos,origin,32));
            for(const auto& old:previousSnapshot.actors)if(old.slot==remote.slot&&old.life==remote.life&&old.health>0) {
                auto before=visual(world::relativeTo(old.pos,origin,32));position=before+(position-before)*interpolation;break;
            }
            auto transform=render::mat4::translate(position)*render::mat4::scale({0.6f,float(fps::stanceHeight(remote,config.motor).toDouble()),0.6f});
            renderer.drawMesh(cube,transform,remote.team==actor.team?render::float4{0.1f,0.5f,0.9f,1}:render::float4{0.9f,0.2f,0.1f,1});
        }
        auto box=[&](Vec3 pos,render::float3 size,render::float4 color){renderer.drawMesh(cube,render::mat4::translate(visual(world::relativeTo(pos,origin,32)))*render::mat4::scale(size),color);};
        for(const auto& loot:client->snapshot().loot)box(loot.pos,{0.35f,0.35f,0.35f},{0.9f,0.75f,0.15f,1});
        for(const auto& v:client->snapshot().vehicles)if(v.health>0)box(v.pos,{2,1.4f,4},{0.2f,0.5f,0.25f,1});
        for(const auto& projectile:client->snapshot().projectiles){
            if(projectile.kind==4){auto r=float(config.systems.smokeRadius.toDouble());box(projectile.pos-Vec3{{},config.systems.smokeRadius,{}},{r*2,r*2,r*2},{0.65f,0.68f,0.7f,1});}
            else box(projectile.pos,{0.15f,0.15f,0.15f},projectile.kind?render::float4{0.2f,0.3f,0.15f,1}:render::float4{1,0.8f,0.2f,1});
        }
        if(client->snapshot().match.planted)box(client->snapshot().match.bombPos,{0.4f,0.3f,0.3f},{1,0.2f,0.1f,1});
        gfx::PassDesc pass;pass.clearColor[0]=0.2f;pass.clearColor[1]=0.28f;pass.clearColor[2]=0.36f;renderer.render(pass);
        ImGui::SetNextWindowPos({16,16});ImGui::SetNextWindowBgAlpha(0.65f);
        ImGui::Begin("##fps",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs);
        auto text=lang.tr("fps.status",i18n::Args().num("health",actor.health).num("ammo",actor.ammo).num("reserve",actor.reserve).num("tick",client->snapshot().tick));
        ImGui::TextUnformatted(text.c_str());
        if((streamedScene&&!streamedScene->status().complete())||(server&&server->module().streamingStatus()&&!server->module().streamingStatus()->complete()))ImGui::TextUnformatted(lang.tr("fps.worldLoading").c_str());
        ImGui::TextUnformatted(lang.tr(weapon.nameKey).c_str());
        if(actor.reloadUntil)ImGui::TextUnformatted(lang.tr("fps.reloading").c_str());
        else if(actor.ammo==0)ImGui::TextUnformatted(lang.tr("fps.empty").c_str());
        for(size_t i=0;i<config.weapons.size();++i) {
            auto label=lang.tr("fps.weaponSlot",i18n::Args().num("slot",int(i+1)).str("weapon",lang.tr(config.weapons[i].nameKey)));
            ImGui::TextColored(i==actor.weaponIndex?ImVec4(1,0.8f,0.3f,1):ImVec4(0.6f,0.65f,0.7f,1),"%s",label.c_str());
        }
        ImGui::PushTextWrapPos(580);ImGui::TextUnformatted(lang.tr(client->ready()?"fps.controls":"fps.connecting").c_str());ImGui::PopTextWrapPos();
        if(!client->connected())ImGui::TextUnformatted(lang.tr("fps.disconnected").c_str());
        if(client->snapshot().winner)ImGui::TextUnformatted(lang.tr("fps.finished").c_str());
        if(config.systems.enabled){
            auto stats=lang.tr("fps.systems.status",i18n::Args().num("armor",actor.armor).num("money",actor.money));ImGui::TextUnformatted(stats.c_str());
            if(actor.downed)ImGui::TextUnformatted(lang.tr("fps.downed").c_str());
            if(actor.health<=0)ImGui::TextUnformatted(lang.tr("fps.dead").c_str());
            if(actor.activeAction!=fps::Action::None){auto remaining=std::max(0,int(actor.actionUntil)-int(client->snapshot().tick));ImGui::TextUnformatted(lang.tr("fps.action",i18n::Args().num("seconds",(remaining+int(config.tickRate)-1)/int(config.tickRate))).c_str());}
            ImGui::PushTextWrapPos(580);ImGui::TextUnformatted(lang.tr("fps.systems.controls").c_str());ImGui::PopTextWrapPos();
            if(keys.contains(platform::Key::Tab))for(auto item:actor.bag)ImGui::TextUnformatted(lang.tr("fps.item",i18n::Args().str("name",lang.tr("fps.item."+std::to_string(item.type))).num("count",item.count)).c_str());
            if(config.mode=="bomb")ImGui::TextUnformatted(lang.tr("fps.round",i18n::Args().num("round",client->snapshot().round).num("a",client->snapshot().match.scoreA).num("b",client->snapshot().match.scoreB).num("bomb",client->snapshot().match.planted?std::max(0,int(client->snapshot().match.bombExplodes)-int(client->snapshot().tick))/int(config.tickRate):0)).c_str());
        }
        ImGui::End();
        auto* draw=ImGui::GetForegroundDrawList();float x=platform::windowWidth()/2,y=platform::windowHeight()/2;
        draw->AddLine({x-5,y},{x+5,y},IM_COL32_WHITE,1.5f);draw->AddLine({x,y-5},{x,y+5},IM_COL32_WHITE,1.5f);
        if(hitFlash>0) {
            for(float sign:{-1.f,1.f}){draw->AddLine({x+sign*8,y+8},{x+sign*14,y+14},IM_COL32(255,100,60,255),2);draw->AddLine({x+sign*8,y-8},{x+sign*14,y-14},IM_COL32(255,100,60,255),2);}
        }
        // Procedural placeholder viewmodel: distinct silhouettes, no external art dependency.
        float w=float(platform::windowWidth()),h=float(platform::windowHeight());
        float gx=w*(aimHeld?0.5f:0.65f),gy=h*(aimHeld?0.65f:0.79f)+float(flash)*110;
        if(actor.velocity.xz().lengthSq()>1_fx)gy+=std::sin(float(viewTime)*10)*3;
        if(actor.reloadUntil)gy+=45+std::sin(float(viewTime)*8)*20;
        float barrel=weapon.pellets>1?35.f:(weapon.range>=700_fx?13.f:22.f);
        draw->AddQuadFilled({gx-38,h},{gx+70,h},{gx+35,gy+45},{gx-25,gy+45},IM_COL32(44,51,59,255));
        draw->AddRectFilled({gx-barrel,gy},{gx+barrel,gy+105},IM_COL32(80,91,103,255),4);
        draw->AddRectFilled({gx-5,gy-5},{gx+5,gy+10},IM_COL32(160,175,185,255),2);
        if(flash>0){draw->AddTriangleFilled({gx-35,gy-10},{gx+30,gy-5},{gx,gy-70},IM_COL32(255,210,70,230));draw->AddLine({gx,gy-30},{x,y},IM_COL32(255,225,140,150),1.5f);}
        if(aimHeld&&(weapon.range>=700_fx||(actor.attachments[actor.weaponIndex]&1))){
            float radius=h*0.38f;draw->AddRectFilled({0,0},{x-radius,h},IM_COL32_BLACK);draw->AddRectFilled({x+radius,0},{w,h},IM_COL32_BLACK);draw->AddRectFilled({x-radius,0},{x+radius,y-radius},IM_COL32_BLACK);draw->AddRectFilled({x-radius,y+radius},{x+radius,h},IM_COL32_BLACK);draw->AddCircle({x,y},radius,IM_COL32_BLACK,64,12);draw->AddLine({x-radius,y},{x+radius,y},IM_COL32_BLACK);draw->AddLine({x,y-radius},{x,y+radius},IM_COL32_BLACK);
        }
        for(const auto& cloud:client->snapshot().projectiles)if(cloud.kind==4&&(actor.pos-cloud.pos).lengthSq()<config.systems.smokeRadius*config.systems.smokeRadius)draw->AddRectFilled({0,0},{w,h},IM_COL32(165,170,175,250));
        if(actor.blindUntil>client->snapshot().tick){auto remain=actor.blindUntil-client->snapshot().tick;auto alpha=int(std::min(1.0,double(remain)/std::max(1u,config.systems.flashTicks))*255);draw->AddRectFilled({0,0},{w,h},IM_COL32(255,255,255,alpha));}
        gui.render();device.endPass();device.endFrame();
    }
    void shutdown(gfx::Device& device){if(streamedScene)streamedScene->shutdown(device);server.reset();client.reset();staticGeometry.shutdown(device);platform::setPointerLock(false);audio.shutdown();gui.shutdown();renderer.shutdown();device.destroyBuffer(cube.vb);device.destroyBuffer(cube.ib);}
};
}
int aiverseMain(int argc,char** argv) {
    try {
        App app;auto engineRoot=platform::resolveEngineDir(AIVERSE_SOURCE_DIR,argv[0]);
        std::string sessionTokenFile;std::string project=engineRoot+"/templates/fps_training",connect;
        for(int i=1;i<argc;++i){std::string arg=argv[i];if(arg=="--project"&&i+1<argc)project=argv[++i];else if(arg=="--connect"&&i+1<argc)connect=argv[++i];else if(arg=="--session-token-file"&&i+1<argc)sessionTokenFile=argv[++i];else if(arg=="--listen")app.listen=true;else throw std::runtime_error("usage: aiverse_fps_client --project DIR [--connect udp://host:27100 | --listen] [--session-token-file FILE]");}
        project=std::filesystem::absolute(project).string();
        std::filesystem::current_path(engineRoot);
        if(!sessionTokenFile.empty()&&(app.listen||connect.empty()))throw std::runtime_error("session token requires --connect");
        if(app.listen&&!connect.empty())throw std::runtime_error("choose --listen or --connect");
        auto data=fps::readProject([&](const auto& file,auto& text){return platform::readContentFile(project+"/"+file,text);});
        app.projectRoot=project;app.contentReader=platform::makeContentReader(project);app.rules=data.rules;app.map=data.map;app.config=fps::readConfig(app.rules);
        if(connect.empty()) {
            app.server=std::make_unique<net::authority::Server>(app.rules,app.map,1,app.contentReader);
            auto pair=net::makeLoopbackPair();app.server->attach(1,std::move(pair.first));
            app.client=std::make_unique<net::authority::Client>(app.rules,app.map,std::move(pair.second),app.contentReader);
#ifndef __EMSCRIPTEN__
            if(app.listen&&(!app.udp.open(27100)||!app.ws.open(27101)))throw std::runtime_error("cannot open listen server");
#endif
        } else {auto conn=net::connectTo(connect);if(!conn)throw std::runtime_error("invalid server address");if(!sessionTokenFile.empty()){std::ifstream tokenFile(sessionTokenFile);std::string token;std::getline(tokenFile,token);if(!tokenFile||!aiverse::lib::session::detail::tokenValid(token))throw std::runtime_error("invalid session token file");conn=std::make_unique<aiverse::lib::session::ClientAdmission>(std::move(conn),std::move(token));}app.client=std::make_unique<net::authority::Client>(app.rules,app.map,std::move(conn),app.contentReader);}
        platform::AppConfig cfg;cfg.title="AIVERSE FPS";cfg.width=1280;cfg.height=720;
        gfx::Device* device=nullptr;platform::AppCallbacks cb;
        cb.init=[&](auto& d){device=&d;app.init(d);};cb.frame=[&](auto& d,double dt){app.frame(d,dt);};cb.event=[&](const auto& e){app.event(e);};cb.shutdown=[&]{app.shutdown(*device);};
        return platform::run(cfg,cb);
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
