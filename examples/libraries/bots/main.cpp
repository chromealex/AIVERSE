// Headless library consumer. This is also a recipe for a project-owned authoritative bot host.
#include <aiverselib/bots/Rts.h>
#include <aiverselib/bots/Persistence.h>
#include <aiverselib/bots/Waypoints.h>
#include <aiverselib/bots/NavMeshData.h>
#include "modules/rts/RtsScript.h"
#include "modules/rts/RtsTiles.h"
#include <fstream>
#include <iostream>
#include <filesystem>
using namespace aiverse;using namespace aiverse::lib::bots;
int main(int argc,char** argv){try{
    if(argc<3||argc>4){std::cerr<<"Usage: aiverse_bot_match <project> <bots.json> [maxTicks]\n";return 2;}
    const std::filesystem::path root=argv[1];auto spec=Json::parseFile(argv[2]);keys(spec,{"genre","settings","waypoints","navmesh"});
    auto genre=spec.getStr("genre");if(genre!="rts"&&genre!="fps")throw JsonError("bots.genre: expected rts or fps");
    auto manifest=Json::parseFile((root/"game.json").string());if(manifest.getStr("module","rts")!=genre)throw JsonError("bots.genre differs from project module");
    auto rules=Json::parseFile((root/manifest.getStr("rules","rules.json")).string());
    auto map=Json::parseFile((root/manifest["maps"][size_t(0)].getStr("file")).string());
    auto read=[&](const std::string& p,std::string& bytes){std::ifstream f(root/p,std::ios::binary);if(!f)return false;bytes.assign(std::istreambuf_iterator<char>(f),{});return true;};
    if(genre=="rts"){std::string error;if(!game::applyTilesToMap(map,read,&error)||!game::embedScripts(rules,map,read,&error))throw std::runtime_error(error);}
    // This host controls every slot. Disable the reference FPS fallback controller.
    for(auto& player:map.member("players").itemsMut())player.set("bot",Json::fromBool(false));
    SimConfig cfg;cfg.seed=3;cfg.inputDelayTicks=0;if(genre=="fps")cfg.tickRate=fps::readConfig(rules).tickRate;
    std::unique_ptr<GameModule> module;if(genre=="fps")module=std::make_unique<fps::Module>();else module=std::make_unique<game::RtsModule>();
    Simulation sim(cfg,std::move(module));sim.setup(rules,map);
    std::vector<RtsBot> rtsBots;std::vector<FpsState> fpsBots(sim.players().size());FpsConfig fc;Waypoints waypoints;std::optional<NavMesh> navigation;
    if(genre=="rts")for(const auto& p:sim.players())rtsBots.emplace_back(p.slot,cfg.seed,spec["settings"]);
    else {
        fc=fpsConfig(spec["settings"]);if(spec.has("waypoints"))waypoints=readWaypoints(spec["waypoints"]);
        const auto& m=static_cast<fps::Module&>(sim.module());NavBuildConfig nc;
        nc.radius=m.config().motor.radius;nc.height=m.config().motor.height;nc.stepHeight=m.config().motor.stepHeight;
        if(spec.has("navmesh"))nc=readNavConfig(spec["navmesh"],nc.radius,nc.height,nc.stepHeight);
        else {
            auto first=m.state(sim).actors.front().pos;nc.bounds={first,first};
            for(const auto& a:m.state(sim).actors){nc.bounds.min.x=fxMin(nc.bounds.min.x,a.pos.x);nc.bounds.min.y=fxMin(nc.bounds.min.y,a.pos.y);nc.bounds.min.z=fxMin(nc.bounds.min.z,a.pos.z);nc.bounds.max.x=fxMax(nc.bounds.max.x,a.pos.x);nc.bounds.max.y=fxMax(nc.bounds.max.y,a.pos.y);nc.bounds.max.z=fxMax(nc.bounds.max.z,a.pos.z);}
            nc.bounds.min=nc.bounds.min-Vec3{16_fx,4_fx,16_fx};nc.bounds.max+=Vec3{16_fx,8_fx,16_fx};
        }
        auto built=buildNavMesh(m.collision(),nc);if(!built.mesh)throw std::runtime_error(built.error);navigation=std::move(built.mesh);
        if(navigation->polygons.empty())throw std::runtime_error("navmesh: no walkable polygons in region");
    }
    const auto maxTicks=argc==4?std::stoul(argv[3]):uint64_t(cfg.tickRate)*1800;
    if(maxTicks==0||maxTicks>10000000)throw std::runtime_error("maxTicks outside 1..10000000");
    uint64_t shots=0,deaths=0;std::map<int,uint64_t> decisions;
    for(uint64_t tick=0;tick<maxTicks&&!sim.result();++tick){
        if(genre=="rts")for(auto& bot:rtsBots)bot.update(sim);
        else for(size_t i=0;i<fpsBots.size();++i){auto cmd=fpsCommand(sim,static_cast<fps::Module&>(sim.module()),uint8_t(i+1),fc,waypoints,fpsBots[i],{},navigation?&*navigation:nullptr);if(cmd)sim.submit(*cmd);++decisions[int(fpsBots[i].decision)];}
        sim.step();if(genre=="fps")for(const auto& event:sim.events()){shots+=event.type==1;deaths+=event.type==2;}
    }
    Json result=Json::object();result.set("genre",Json::string(genre));result.set("ticks",Json::number(int64_t(sim.tick())));result.set("winner",Json::number(int64_t(sim.result())));
    result.set("commands",Json::number(int64_t(sim.executedCommands().size())));result.set("shots",Json::number(int64_t(shots)));result.set("deaths",Json::number(int64_t(deaths)));
    result.set("checksum",Json::string(std::to_string(sim.checksum())));Json counts=Json::object();for(auto [id,count]:decisions)counts.set(std::to_string(id),Json::number(int64_t(count)));result.set("decisions",counts);
    std::cout<<result.dump(2)<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
