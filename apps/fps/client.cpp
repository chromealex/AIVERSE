#include <aiverselib/net/Admission.h>
#include "engine/net/authority/Session.h"
#include "modules/fps/Project.h"
#include "modules/fps/view/FirstPersonCamera.h"
#include "RoyaleHud.h"
#include "engine/net/Udp.h"
#include "engine/net/WebSocket.h"
#include "engine/platform/Platform.h"
#include "engine/platform/InputFlow.h"
#include "engine/world/ChunkScene.h"
#include "engine/world/WorldScene.h"
#include "engine/world/TerraWeaveHorizonFrameTimingBridge.h"
#include "engine/platform/ContentReader.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/GeometryScene.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/i18n/Localization.h"
#include "imgui.h"
#include "engine/audio/Audio.h"
#include "engine/platform/EngineDir.h"
#include <fstream>
#include <filesystem>
#include <iterator>
#include <chrono>
#include <cstdlib>
#include <cmath>
#include <cstdio>

using namespace aiverse;
namespace ein = aiverse::lib::gameplay::inputflow;
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
    terraweave::HorizonFrameTimingBridge horizonFrameTiming; // optional, native stream QoS
    platform::ContentReader contentReader;
    render::GeometryScene staticGeometry;
    render::Renderer3D renderer;render::ImGuiLayer gui;render::Mesh cube;
    i18n::Localization lang;
    ein::InputFlow actions;
    platform::InputFlowAdapter inputAdapter{actions};
    platform::inputflow_config::UserProfiles userProfiles;
    ein::InputFlow projectActions;
    bool settingsOpen=false;
    std::string rebindContext, settingsError;
    std::uint64_t rebindMapping=0;
    ein::RebindPolicy rebindPolicy=ein::RebindPolicy::Swap;
    fps::Input input;
    bool fireHeld=false,fireQueued=false,jumpQueued=false,aimHeld=false,prone=false;uint32_t buySelection=0;
    audio::AudioEngine audio;double stepSound=0,viewTime=0;uint32_t lastReload=0;
    std::map<uint8_t,std::pair<uint32_t,uint32_t>> heardShots;
    uint32_t shownShots=0,shownHits=0,shownLife=0;
    double flash=0,hitFlash=0;
    double accumulated=0;bool listen=false;
    fps::view::Look look;
    net::authority::Snapshot previousSnapshot,received;uint32_t lastSnapshotTick=0;double snapshotAge=0;

    // Data-driven semantic mapping: games may replace this context with their own JSON assets.
    // These defaults reproduce the reference FPS controls; the network command layout is unchanged.
    void configureInput() {
        std::string error;
        if(!platform::inputflow_config::installAsset(actions,platform::inputflow_presets::fpsReference(),&error))
            throw std::runtime_error("FPS InputFlow preset: "+error);
    }

    void loadInputConfig() {
        namespace cfg=platform::inputflow_config;
        std::string text,error;
        // A project asset is a complete mapping scheme. Never store authored
        // keys inside the OS user profile.
        if(platform::readContentFile(projectRoot+"/input/actions.json",text)){
            try{
                cfg::Asset asset;
                if(!cfg::readAsset(Json::parse(text),asset,&error) ||
                   !cfg::installAsset(actions,asset,&error))
                    std::fprintf(stderr,"InputFlow asset ignored: %s\n",error.c_str());
            }catch(const std::exception& ex){
                std::fprintf(stderr,"InputFlow asset ignored: %s\n",ex.what());
            }
        }
        projectActions=actions; // reloading player settings must preserve project defaults
        userProfiles.setScope(platform::userDataDir(),projectRoot);
        if(!userProfiles.load(0,actions,&error))
            std::fprintf(stderr,"InputFlow profile ignored: %s\n",error.c_str());
    }
    // One transactional path for both single-player FPS and future couch co-op.
    bool rebindInput(const std::string& context,std::uint64_t id,ein::Control control,
                     ein::RebindPolicy policy) {
        return userProfiles.rebind(0,actions,context,id,control,policy,&settingsError);
    }
    // Presentation-only in-game rebinding. Authored actions.json is never changed by players.
    void drawInputSettings() {
        if(!settingsOpen)return;
        ImGui::SetNextWindowSize({600,570},ImGuiCond_FirstUseEver);
        if(!ImGui::Begin("InputFlow Settings (F10)",&settingsOpen)){
            ImGui::End();return;
        }
        ImGui::TextWrapped("Click Rebind, then press a keyboard key or mouse/gamepad button. "
                           "Overrides are saved per player and project; Escape cancels capture.");
        if(ImGui::Button("Resume (click viewport to capture mouse)")){
            settingsOpen=false;rebindMapping=0;
        }
        ImGui::SameLine();
        if(ImGui::Button("Restore project defaults")){
            if(userProfiles.reset(0,actions,projectActions,&settingsError)){
                settingsError.clear();
                rebindMapping=0;
                platform::syncUserData();
            }
        }
        int policy=int(rebindPolicy);
        if(ImGui::Combo("Binding conflicts",&policy,
                        "Allow duplicate\0Reject duplicate\0Swap keys\0\0"))
            rebindPolicy=ein::RebindPolicy(policy);
        if(!settingsError.empty())ImGui::TextWrapped("Input: %s",settingsError.c_str());
        const auto conflicts=actions.conflicts();
        if(!conflicts.empty() && ImGui::CollapsingHeader("Detected conflicts")) {
            for(const auto& c:conflicts) {
                ImGui::TextWrapped("%s:%s / %s:%s %s",c.higherContext.c_str(),
                    c.higherAction.c_str(),c.lowerContext.c_str(),
                    c.lowerAction.c_str(),c.shadowed?"(shadowed)":"(shared)");
            }
        }
        ImGui::Separator();
        if(ImGui::BeginChild("##bindings")) {
            for(const auto& name:actions.registeredContexts()) {
                const auto* context=actions.context(name);
                if(!context)continue;
                const std::string header=name+(actions.isContextActive(name)?" (active)":" (inactive)");
                if(!ImGui::CollapsingHeader(header.c_str(),ImGuiTreeNodeFlags_DefaultOpen))continue;
                for(const auto& mapping:context->mappings) {
                    ImGui::PushID(name.c_str());
                    ImGui::PushID(int(mapping.id)); // stable mapping id for runtime profile
                    const auto binding=platform::inputflow_config::detail::writeControl(mapping.control).dump(0);
                    ImGui::Text("%s  %s",mapping.action.c_str(),binding.c_str());
                    ImGui::SameLine();
                    if(ImGui::SmallButton("Rebind")){
                        rebindContext=name;rebindMapping=mapping.id;settingsError.clear();
                    }
                    if(rebindMapping==mapping.id&&rebindContext==name) {
                        ImGui::TextDisabled("Waiting for key / mouse / gamepad...");
                    }
                    for(const auto& sibling:context->mappings)
                        if(sibling.id!=mapping.id&&sibling.control==mapping.control)
                            ImGui::TextDisabled("Conflict: another action uses this control.");
                    ImGui::PopID();
                    ImGui::PopID();
                }
            }
        }
        ImGui::EndChild();
        ImGui::End();
        if(!settingsOpen)rebindMapping=0;
    }
    bool finishRebind(ein::Control control){
        if(!rebindMapping)return false;
        const bool ok=rebindInput(rebindContext,rebindMapping,control,rebindPolicy);
        rebindMapping=0;rebindContext.clear();
        return ok;
    }
    void init(gfx::Device& device) {
        configureInput();
        loadInputConfig();
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
        using E=platform::EventType; using K=platform::Key;
        gui.handleEvent(e);
        if(e.type==E::KeyDown&&e.key==K::F10&&!e.repeat){
            settingsOpen=!settingsOpen;
            rebindMapping=0;
            platform::setPointerLock(false);
            inputAdapter.focusLost();
            fireHeld=fireQueued=jumpQueued=aimHeld=false;input.buttons=0;
            return;
        }
        if(settingsOpen) {
            if(rebindMapping && e.type==E::KeyDown && !e.repeat) {
                if(e.key==K::Escape){rebindMapping=0;rebindContext.clear();}
                else if(e.key!=K::Unknown)
                    finishRebind({ein::Device::Keyboard,int(e.key)});
            }else if(rebindMapping && e.type==E::MouseDown && e.button>=0 && e.button<=4){
                finishRebind({ein::Device::MouseButton,e.button});
            }else if(e.type==E::KeyDown && e.key==K::Escape) {
                settingsOpen=false;rebindMapping=0;rebindContext.clear();
            }
            if(e.type!=E::FocusLost)return;
        }
        if(e.type==E::FocusLost){
            platform::setPointerLock(false);inputAdapter.focusLost();
            input.buttons=0;input.action=fps::Action::None;
            fireHeld=fireQueued=jumpQueued=aimHeld=false;
            return;
        }
        if(e.type==E::KeyDown&&e.key==K::Escape){
            platform::setPointerLock(false);inputAdapter.focusLost();
            input.buttons=0;input.action=fps::Action::None;
            fireHeld=fireQueued=jumpQueued=aimHeld=false;
            return;
        }
        if(e.type==E::MouseDown&&e.button==0&&!platform::pointerLocked())
            platform::setPointerLock(true);
        // Gameplay context is only fed while the viewport owns the pointer lock.
        if(platform::pointerLocked()||e.type==E::KeyUp||e.type==E::MouseUp)
            inputAdapter.onEvent(e);
    }
    void frame(gfx::Device& device,double dt) {
        const auto frameStarted=terraweave::HorizonFrameTimingBridge::Clock::now();
        const auto now=net::nowMs();
#ifndef __EMSCRIPTEN__
        if(listen) {
            auto admit=[&](auto connections){for(auto& conn:connections){uint8_t slot=0;for(size_t i=2;i<=map["players"].size();++i)if(!server->occupied(uint8_t(i))){slot=uint8_t(i);break;}if(slot)server->attach(slot,std::move(conn));else conn->close();}};
            admit(udp.poll(now));admit(ws.poll(now));
        }
#endif
        client->updateStreaming();client->poll(now);if(server)server->poll(now);
        const bool playing=platform::pointerLocked() && !settingsOpen;
        if(settingsOpen) {
            platform::pollGamepads();
            if(rebindMapping){
                for(int pad=0;pad<platform::kMaxGamepads && rebindMapping;++pad){
                    const auto& state=platform::gamepad(pad);
                    if(!state.connected)continue;
                    for(int button=0;button<int(platform::PadButton::Count);++button){
                        if(state.pressed(platform::PadButton(button))){
                            finishRebind({ein::Device::GamepadButton,button,pad});break;
                        }
                    }
                    for(int axis=0;axis<int(platform::PadAxis::Count)&&rebindMapping;++axis)
                        if(std::abs(state.axis(platform::PadAxis(axis)))>0.8f)
                            finishRebind({ein::Device::GamepadAxis,axis,pad});
                }
            }
        }
        if(playing){
            inputAdapter.pollGamepads();
            const auto events=actions.update(float(std::min(dt,0.25)));
            for(const auto& e:events){
                if(e.phase!=ein::Phase::Triggered)continue;
                const auto& name=e.action;
                if(name=="Jump")jumpQueued=true;
                else if(name=="Fire")fireQueued=true;
                else if(name=="Prone")prone=!prone;
                else if(name=="Frag")input.action=fps::Action::Frag;
                else if(name=="Smoke")input.action=fps::Action::Smoke;
                else if(name=="Flash")input.action=fps::Action::Flash;
                else if(name=="Heal")input.action=fps::Action::Heal;
                else if(name=="Interact")input.action=fps::Action::Interact;
                else if(name=="Drop")input.action=fps::Action::Drop;
                else if(name=="Plant")input.action=fps::Action::Plant;
                else if(name=="Vault")input.action=fps::Action::Vault;
                else if(name=="Buy"){
                    input.action=fps::Action::Buy;input.argument=buySelection;
                    input.buttons=(input.buttons&~uint32_t(fps::WeaponMask))|fps::selectWeapon(buySelection);
                }else if(name=="Weapon1"||name=="Weapon2"||name=="Weapon3"||name=="Weapon4"){
                    buySelection=uint32_t(name.back()-'1');
                    input.buttons=(input.buttons&~uint32_t(fps::WeaponMask))|fps::selectWeapon(buySelection);
                }else if(name=="Attach1"||name=="Attach2"||name=="Attach3"){
                    input.action=fps::Action::Attach;input.argument=6+uint32_t(name.back()-'1');
                }
            }
            auto held=[&](const char* name){const auto* state=actions.state(name);return state&&state->active;};
            fireHeld=held("Fire");aimHeld=held("Aim");
            if(const auto* mouse=actions.state("Look");mouse&&mouse->active)
                look.motion(mouse->value.x,mouse->value.y,aimHeld?0.00015f:0.0004f);
            if(const auto* stick=actions.state("LookStick");stick&&stick->active){
                const float pxPerSecond=1400.0f;
                const float scaled=float(std::min(dt,0.25))*pxPerSecond;
                look.motion(stick->value.x*scaled,stick->value.y*scaled,aimHeld?0.00015f:0.0004f);
            }
        }else{
            inputAdapter.focusLost();
            input.buttons=0;input.action=fps::Action::None;
            fireHeld=fireQueued=jumpQueued=aimHeld=false;
        }
        auto held=[&](const char* name){const auto* state=actions.state(name);return state&&state->active;};
        accumulated+=std::min(dt,0.25);int budget=8;
        while(accumulated>=1.0/config.tickRate&&budget--){
            const auto* move=actions.state("Move");
            input.forward=move?int32_t(std::clamp(move->value.y,-1.0f,1.0f)*1000):0;
            input.right=move?int32_t(std::clamp(move->value.x,-1.0f,1.0f)*1000):0;
            input.buttons&=uint32_t(fps::WeaponMask);
            if(jumpQueued)input.buttons|=fps::Jump;
            if(aimHeld)input.buttons|=fps::Aim;
            if(prone)input.buttons|=fps::Prone;
            if(fireHeld||fireQueued)input.buttons|=fps::Fire;
            if(held("Crouch"))input.buttons|=fps::Crouch;
            if(held("Sprint"))input.buttons|=fps::Sprint;
            if(held("Reload"))input.buttons|=fps::Reload;
            if(config.melee.enabled){
                if(held("Melee"))input.buttons|=fps::Melee;
                if(held("Block"))input.buttons|=fps::Block;
                if(held("LockOn"))input.buttons|=fps::LockOn;
            }
            input.yaw=Fixed::fromRaw(int64_t(look.yaw*65536));input.pitch=Fixed::fromRaw(int64_t(look.pitch*65536));
            if(client->submit(input)){
                fireQueued=jumpQueued=false;
                input.action=fps::Action::None;
                input.buttons&=~uint32_t(fps::Jump|fps::WeaponMask);
            }
            if(server){server->poll(now);if(!server->step()){accumulated=0;break;}client->poll(now);}
            accumulated-=1.0/config.tickRate;
        }
        device.beginFrame(platform::framebufferWidth(),platform::framebufferHeight());
        // GPU query belongs to this FPS frame's command buffer. Unsupported
        // backends transparently use measured CPU callback time only.
        horizonFrameTiming.begin(device,streamedScene.get());
        gui.newFrame(dt);
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
            if(chunks.enabled()&&id!=1&&id<fps::kBuildingColliderBase)continue;   // generated buildings are not in the chunk files
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
        fps::view::drawZoneWall(renderer,cube,client->snapshot().royale,actor.pos,origin,visual);
        for(const auto& c:client->snapshot().royale.crates)box(c,{1.2f,1.2f,1.2f},{0.95f,0.6f,0.1f,1});
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
            if(held("ShowInventory"))for(auto item:actor.bag)ImGui::TextUnformatted(lang.tr("fps.item",i18n::Args().str("name",lang.tr("fps.item."+std::to_string(item.type))).num("count",item.count)).c_str());
            if(config.mode=="bomb")ImGui::TextUnformatted(lang.tr("fps.round",i18n::Args().num("round",client->snapshot().round).num("a",client->snapshot().match.scoreA).num("b",client->snapshot().match.scoreB).num("bomb",client->snapshot().match.planted?std::max(0,int(client->snapshot().match.bombExplodes)-int(client->snapshot().tick))/int(config.tickRate):0)).c_str());
        }
        ImGui::End();
        drawInputSettings();
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
        fps::view::drawRoyaleHud(lang,client->snapshot(),actor,config);
        gui.render();device.endPass();device.endFrame();
        horizonFrameTiming.finish(streamedScene.get(),frameStarted);
    }
    void shutdown(gfx::Device& device){horizonFrameTiming.detach(device);if(streamedScene)streamedScene->shutdown(device);server.reset();client.reset();staticGeometry.shutdown(device);platform::setPointerLock(false);audio.shutdown();gui.shutdown();renderer.shutdown();device.destroyBuffer(cube.vb);device.destroyBuffer(cube.ib);}
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
