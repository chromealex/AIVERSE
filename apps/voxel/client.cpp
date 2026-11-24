// Voxel host of the game client (AiverseGame): the voxel sandbox game (templates/voxel_sandbox). Single player runs a local authoritative
// server over a loopback; --listen also accepts friends (UDP 27200 / WebSocket 27201); --connect joins a server.
//   AiverseGame [--project DIR] [--listen | --connect udp://host:27200]
// Controls: mouse look (click to capture), WASD, Space jump / swim, Shift sprint, Ctrl sneak, LMB dig / hit,
// RMB place, 1-9 or wheel pick a hotbar slot, F3 debug, Esc release the mouse.
#include "engine/platform/TemplatesDir.h"
#include "engine/voxel/VoxelSession.h"
#include "engine/voxel/Project.h"
#include "engine/voxel/view/VoxelView.h"
#include "engine/net/Udp.h"
#include "engine/net/WebSocket.h"
#include "engine/platform/Platform.h"
#include "apps/client/HostBoot.h"
#include "engine/platform/EngineDir.h"
#include "engine/render/Renderer3D.h"
#include "engine/render/ImGuiLayer.h"
#include "engine/i18n/Localization.h"
#include "imgui.h"
#include <cstdio>
#include <filesystem>
#include <set>

using namespace aiverse;
using namespace aiverse::lib::systems;
namespace {
render::float3 local(Vec3 v,BlockPos o){return {float((v.x-Fixed::fromInt(o.x)).toDouble()),float((v.y-Fixed::fromInt(o.y)).toDouble()),float((v.z-Fixed::fromInt(o.z)).toDouble())};}
BlockPos cellOf(Vec3 p){return {int32_t(p.x.floorInt()),int32_t(p.y.floorInt()),int32_t(p.z.floorInt())};}
struct App{
    Json rules,map;voxelgame::Content content;
    std::unique_ptr<voxelgame::Server> server;std::unique_ptr<voxelgame::Client> client;bool listen=false;
#ifndef __EMSCRIPTEN__
    net::UdpListener udp;net::WsListener ws;
#endif
    render::Renderer3D renderer;render::ImGuiLayer gui;render::Mesh cube;voxelgame::view::ChunkRenderer chunks;
    i18n::Localization lang;std::set<platform::Key> keys;voxelgame::view::Look look;
    bool breakHeld=false,placeHeld=false,jumpQueued=false,debug=false;int selected=0;double accumulated=0,fpsTime=0;int frames=0,fps=0;
    void init(gfx::Device& device){
        renderer.init(device);renderer.setShadowQuality(render::ShadowQuality::Low);renderer.lighting.tonemap=false;
        gui.init(device,"engine/assets/fonts/ui.ttf",17);
        render::MeshBuilder b;b.color(render::rgba(255,255,255)).box({0,0.5f,0},{1,1,1});cube=b.upload(device);
        lang.load([](const auto& path,auto& out){return platform::readContentFile(path,out);},"en",{"engine/assets/lang"});
    }
    void releaseInput(){platform::setPointerLock(false);keys.clear();breakHeld=placeHeld=jumpQueued=false;}
    void event(const platform::Event& e){
        using E=platform::EventType;using K=platform::Key;
        if(e.type==E::FocusLost)releaseInput();
        if(e.type==E::KeyDown){
            if(e.key==K::Escape){releaseInput();return;}
            if(e.key==K::F3&&!e.repeat)debug=!debug;
            if(e.key>=K::Num1&&e.key<=K::Num9)selected=int(e.key)-int(K::Num1);
            if(platform::pointerLocked()){keys.insert(e.key);if(e.key==K::Space&&!e.repeat)jumpQueued=true;}
        }
        if(e.type==E::KeyUp)keys.erase(e.key);
        if(e.type==E::MouseDown){if(!platform::pointerLocked()){platform::setPointerLock(true);return;}if(e.button==0)breakHeld=true;if(e.button==1)placeHeld=true;}
        if(e.type==E::MouseUp){if(e.button==0)breakHeld=false;if(e.button==1)placeHeld=false;}
        if(e.type==E::MouseWheel&&e.wheelY!=0)selected=(selected+(e.wheelY<0?1:voxelgame::kHotbar-1))%voxelgame::kHotbar;
        if(e.type==E::MouseMove&&platform::pointerLocked())look.motion(e.deltaX,e.deltaY);
    }
    void tick(uint64_t now){
        using K=platform::Key;voxelgame::Input in;
        in.forward=(keys.contains(K::W)?1000:0)-(keys.contains(K::S)?1000:0);in.right=(keys.contains(K::D)?1000:0)-(keys.contains(K::A)?1000:0);
        in.yaw=Fixed::fromRaw(int64_t(look.yaw*65536));in.pitch=Fixed::fromRaw(int64_t(look.pitch*65536));
        if(keys.contains(K::Space)||jumpQueued)in.buttons|=voxelgame::Jump;if(keys.contains(K::LeftShift))in.buttons|=voxelgame::Sprint;
        if(keys.contains(K::LeftCtrl))in.buttons|=voxelgame::Sneak;if(breakHeld)in.buttons|=voxelgame::Break;if(placeHeld)in.buttons|=voxelgame::Place;
        in.slot=uint8_t(selected+1);
        if(client->submit(in))jumpQueued=false;
        if(server){server->poll(now);server->step();}
        client->poll(now);
    }
    void frame(gfx::Device& device,double dt){
        const auto now=net::nowMs();
#ifndef __EMSCRIPTEN__
        if(listen){auto admit=[&](auto conns){for(auto& c:conns){uint8_t slot=0;for(uint8_t i=2;i<=content.config.maxPlayers;++i)if(!server->occupied(i)){slot=i;break;}if(!slot||!server->attach(slot,std::move(c)))c->close();}};admit(udp.poll(now));admit(ws.poll(now));}
#endif
        if(server)server->poll(now);client->poll(now);
        accumulated+=std::min(dt,0.25);int budget=4;const double step=1.0/content.config.tickRate;
        while(accumulated>=step&&budget--){tick(now);accumulated-=step;}
        if(!platform::pointerLocked()){keys.clear();breakHeld=placeHeld=false;}
        fpsTime+=dt;++frames;if(fpsTime>=1){fps=frames;frames=0;fpsTime=0;}
        const auto& me=client->predicted();const auto& snap=client->snapshot();
        float day=voxelgame::daylight(snap.time,content.config.dayTicks);
        auto eye=me.pos+Vec3{{},content.config.motor.eye,{}};auto eyeCell=cellOf(eye);auto origin=chunkOrigin(chunkOf(eyeCell));
        chunks.sync(device,client->world(),content.blocks,eyeCell,day,6);
        device.beginFrame(platform::framebufferWidth(),platform::framebufferHeight());gui.newFrame(dt);
        render::float3 sky{0.47f*day+0.02f,0.66f*day+0.03f,0.98f*day+0.06f};
        renderer.lighting.fogColor=sky;renderer.lighting.fogDensity=1.6f/float(content.config.viewRadius*16);
        auto cam=look.camera(local(eye,origin));
        renderer.beginScene(cam,device.framebufferWidth(),device.framebufferHeight());
        chunks.draw(renderer,origin,content.config.viewRadius+1);
        auto box=[&](Vec3 at,render::float3 size,render::float4 color){renderer.drawMesh(cube,render::mat4::translate(local(at,origin))*render::mat4::scale(size),color,render::kDrawNoCastShadow);};
        for(const auto& m:snap.mobs){bool hurt=m.hurtAt+6>snap.time;auto body=hurt?render::float4{0.8f,0.25f,0.2f,1}:render::float4{0.25f,0.55f,0.3f,1};
            box(m.pos,{0.6f,1.4f,0.4f},body);box(m.pos+Vec3{{},Fixed::fromRatio(14,10),{}},{0.5f,0.5f,0.5f},hurt?body:render::float4{0.35f,0.7f,0.4f,1});}
        for(const auto& p:snap.players)if(p.health>0){box(p.pos,{0.6f,1.5f,0.35f},{0.2f,0.35f,0.8f,1});box(p.pos+Vec3{{},Fixed::fromRatio(15,10),{}},{0.45f,0.45f,0.45f},{0.85f,0.7f,0.55f,1});}
        // Outline of the block under the crosshair (client-side ray on the mirror world).
        auto ray=voxelgame::lookDirection(Fixed::fromRaw(int64_t(look.yaw*65536)),Fixed::fromRaw(int64_t(look.pitch*65536)))*content.config.reach;
        auto hit=voxelRaycast(client->world(),content.blocks,eye,ray,[](BlockId id,const BlockDef& d){return id!=kAir&&!d.fluid;});
        if(hit.hit&&me.health>0){render::float3 a{float(hit.block.x-origin.x)-0.004f,float(hit.block.y-origin.y)-0.004f,float(hit.block.z-origin.z)-0.004f};float s=1.008f;
            render::float3 c[8];for(int i=0;i<8;++i)c[i]={a.x+((i&1)?s:0),a.y+((i&2)?s:0),a.z+((i&4)?s:0)};
            const int edges[12][2]={{0,1},{2,3},{4,5},{6,7},{0,2},{1,3},{4,6},{5,7},{0,4},{1,5},{2,6},{3,7}};
            for(auto& e:edges)renderer.line(c[e[0]],c[e[1]],render::rgba(20,20,20,230));}
        gfx::PassDesc pass;pass.clearColor[0]=sky.x;pass.clearColor[1]=sky.y;pass.clearColor[2]=sky.z;renderer.render(pass);
        hud(snap,me,day);
        gui.render();device.endPass();device.endFrame();
    }
    void hud(const voxelgame::Snapshot& snap,const voxelgame::Player& me,float day){
        auto* draw=ImGui::GetForegroundDrawList();float w=float(platform::windowWidth()),h=float(platform::windowHeight()),cx=w/2,cy=h/2;
        draw->AddLine({cx-8,cy},{cx+8,cy},IM_COL32_WHITE,2);draw->AddLine({cx,cy-8},{cx,cy+8},IM_COL32_WHITE,2);
        const float cell=46,gap=4,total=voxelgame::kHotbar*cell+(voxelgame::kHotbar-1)*gap,x0=cx-total/2,y0=h-cell-16;
        for(int i=0;i<voxelgame::kHotbar;++i){float x=x0+float(i)*(cell+gap);
            draw->AddRectFilled({x,y0},{x+cell,y0+cell},IM_COL32(20,20,24,170),4);
            const auto& st=me.hotbar[static_cast<size_t>(i)];
            if(st.count&&st.block){auto c=content.blocks[st.block].colors[3];draw->AddRectFilled({x+8,y0+8},{x+cell-8,y0+cell-8},IM_COL32((c>>16)&255,(c>>8)&255,c&255,255),3);
                if(!content.config.creative){char n[8];std::snprintf(n,sizeof n,"%u",st.count);draw->AddText({x+cell-6-ImGui::CalcTextSize(n).x,y0+cell-20},IM_COL32_WHITE,n);}}
            if(i==selected)draw->AddRect(ImVec2(x-2,y0-2),ImVec2(x+cell+2,y0+cell+2),IM_COL32_WHITE,5.0f,0,3.0f);}
        if(!content.config.creative)for(int i=0;i<int(content.config.playerHealth)/2;++i){float x=x0+float(i)*18,y=y0-22;bool full=me.health>=2*(i+1),half=me.health==2*i+1;
            draw->AddRectFilled({x,y},{x+14,y+12},full?IM_COL32(220,40,40,255):half?IM_COL32(150,40,40,255):IM_COL32(60,20,20,200),3);}
        if(me.hasBreaking&&me.breakProgress){auto need=std::max<uint32_t>(1,content.blocks[client->world().blockOr(me.breaking,kAir)].breakTicks);float f=std::min(1.0f,float(me.breakProgress)/float(need));
            draw->AddRectFilled({cx-30,cy+18},{cx+30,cy+24},IM_COL32(0,0,0,150),2);draw->AddRectFilled({cx-30,cy+18},{cx-30+60*f,cy+24},IM_COL32(255,255,255,220),2);}
        ImGui::SetNextWindowPos({12,12});ImGui::SetNextWindowBgAlpha(0.5f);
        ImGui::Begin("##voxel",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoInputs);
        if(!client->ready())ImGui::TextUnformatted(lang.tr("voxel.connecting").c_str());
        else if(!platform::pointerLocked()){ImGui::PushTextWrapPos(560);ImGui::TextUnformatted(lang.tr("voxel.controls").c_str());ImGui::PopTextWrapPos();}
        if(!client->connected())ImGui::TextUnformatted(lang.tr("voxel.disconnected").c_str());
        if(debug){auto st=chunks.stats();
            ImGui::Text("%d fps  pos %.1f %.1f %.1f  chunk %d %d %d",fps,me.pos.x.toDouble(),me.pos.y.toDouble(),me.pos.z.toDouble(),int(me.pos.x.floorInt()>>4),int(me.pos.y.floorInt()>>4),int(me.pos.z.floorInt()>>4));
            ImGui::Text("chunks %zu  meshes %zu  queued %zu  triangles %zu",client->world().chunks().size(),st.meshes,st.pending,st.triangles);
            ImGui::Text("tick %u  day %.2f  mobs %zu  players %zu",snap.tick,double(day),snap.mobs.size(),snap.players.size()+1);}
        ImGui::End();
        if(me.health<=0&&client->ready()){draw->AddRectFilled({0,0},{w,h},IM_COL32(120,0,0,110));auto t=lang.tr("voxel.dead");auto sz=ImGui::CalcTextSize(t.c_str());draw->AddText({cx-sz.x/2,cy-40},IM_COL32_WHITE,t.c_str());}
    }
    void shutdown(gfx::Device& device){chunks.shutdown(device);server.reset();client.reset();platform::setPointerLock(false);gui.shutdown();device.destroyBuffer(cube.vb);device.destroyBuffer(cube.ib);renderer.shutdown();}
};
}
// Voxel projects (game.json "module": "voxel") in the one game client (apps/client/main.cpp dispatches here).
int voxelClientMain(int argc,char** argv){
    try{
        App app;auto engineRoot=platform::resolveEngineDir(AIVERSE_SOURCE_DIR,argv[0]);
        std::string project=aiverse::platform::templatesDir(engineRoot)+"/voxel_sandbox",connect;
        for(int i=1;i<argc;++i){std::string a=argv[i];if(a=="--project"&&i+1<argc)project=argv[++i];else if(a=="--connect"&&i+1<argc)connect=argv[++i];else if(a=="--listen")app.listen=true;
            else throw std::runtime_error("usage: AiverseGame [--project DIR] [--listen | --connect udp://host:27200]");}
        project=std::filesystem::absolute(project).string();std::filesystem::current_path(engineRoot);
        if(app.listen&&!connect.empty())throw std::runtime_error("choose --listen or --connect");
        auto data=voxelgame::readProject([&](const auto& file,auto& text){return platform::readContentFile(project+"/"+file,text);});
        app.rules=data.rules;app.map=data.map;app.content=voxelgame::readContent(app.rules);
        if(connect.empty()){
            app.server=std::make_unique<voxelgame::Server>(app.rules,app.map,1);auto pair=net::makeLoopbackPair();app.server->attach(1,std::move(pair.first));
            app.client=std::make_unique<voxelgame::Client>(app.rules,app.map,std::move(pair.second));
#ifndef __EMSCRIPTEN__
            if(app.listen&&(!app.udp.open(27200)||!app.ws.open(27201)))throw std::runtime_error("cannot open listen server");
#endif
        }else{auto conn=net::connectTo(connect);if(!conn)throw std::runtime_error("invalid server address");app.client=std::make_unique<voxelgame::Client>(app.rules,app.map,std::move(conn));}
        platform::AppConfig cfg;cfg.title=data.manifest.getStr("name","AIVERSE Blocks");cfg.width=1280;cfg.height=720;
        // The shared loading screen (game.json "splash"; apps/client/HostBoot.h) shows while the app loads.
        client::useEngineAssets(engineRoot);
        platform::AppCallbacks cb;client::HostBoot boot;boot.install(cb,app,data.manifest["splash"],project);
        return platform::run(cfg,cb);
    }catch(const std::exception& e){std::fprintf(stderr,"%s\n",e.what());return 1;}
}
