#pragma once
// vehicle-controllers: the data of a race, one format for every project and host (docs/races.md): rules "race"
// (readRaceRules: countdown, grace, timeout, laps, bot knobs) and map "race" (readRaceTrack: checkpoints, grid, line).
// Missing keys keep the defaults (countdown 3 s, grace 10 s at the tick rate). Errors name `path`.
#include "RaceSession.h"
#include "VehicleConfigured.h"
#include "../gameplay/Configuration.h"
#include "engine/data/Json.h"
#include <string>
namespace aiverse::lib::genre {
template<class Json> uint32_t raceSessionTicks(const Json& j,const std::string& path,const char* key,uint32_t fallback){
    if(!j.has(key))return fallback;
    const auto& v=j[key];if(!v.isNumber())throw JsonError(path+"."+key+": expected ticks");
    auto x=v.fx();if(x.raw%Fixed::kOne||x<Fixed{}||x>Fixed::fromInt(kRaceSessionMaxTicks))throw JsonError(path+"."+key+": expected 0.."+std::to_string(kRaceSessionMaxTicks)+" ticks");
    return uint32_t(x.floorInt());
}
// Race knobs of a project (rules "race", the same block for every host):
//   {"countdownTicks": 180, "finishGraceTicks": 600, "timeoutTicks": 0, "laps": 3, "crashDamage": false,
//    "vehicles": { generated `vehicles` settings section: wrong way, stuck respawn, race bot knobs }}
// laps: overrides the track's laps (0 = keep); crashDamage: hard crashes damage the racing cars (host rule).
struct RaceRules{RaceSessionConfig session;uint32_t laps=0;bool crashDamage=false;};
template<class Json> RaceRules readRaceRules(const Json& j,const std::string& path,uint32_t tickRate){
    RaceRules r;auto& c=r.session;
    if(!j.isNull()&&!j.isObject())throw JsonError(path+": expected an object");
    if(j.isObject())for(const auto& [key,value]:j.members()){(void)value;
        if(key!="countdownTicks"&&key!="finishGraceTicks"&&key!="timeoutTicks"&&key!="laps"&&key!="crashDamage"&&key!="vehicles")throw JsonError(path+"."+key+": unknown key");}
    c.countdownTicks=raceSessionTicks(j,path,"countdownTicks",3*tickRate);
    c.finishGraceTicks=raceSessionTicks(j,path,"finishGraceTicks",10*tickRate);
    c.timeoutTicks=raceSessionTicks(j,path,"timeoutTicks",0);
    if(j.has("laps")){r.laps=raceSessionTicks(j,path,"laps",0);if(r.laps<1||r.laps>1000)throw JsonError(path+".laps: expected 1..1000");}
    if(j.has("crashDamage")){if(j["crashDamage"].type()!=Json::Type::Bool)throw JsonError(path+".crashDamage: expected boolean");r.crashDamage=j["crashDamage"].boolean();}
    if(j.has("vehicles")){
        Json root=Json::object();root.set("schemaVersion",Json::number(int64_t(1)));root.set("vehicles",j["vehicles"]);
        auto settings=gameplay::readGameplaySettings(root);
        if(!settings.value)throw JsonError(path+"."+(settings.errors.empty()?std::string("vehicles: invalid"):settings.errors.front()));
        auto errors=vehicleSettingsErrors(*settings.value);if(!errors.empty())throw JsonError(path+"."+errors.front());
        c.race=vehicleRaceConfig(*settings.value);c.bot=bots::raceBotConfig(*settings.value);
        if(!r.laps&&j["vehicles"].has("raceLaps"))r.laps=vehicleRaceLaps(*settings.value);
    }
    if(!valid(c))throw JsonError(path+": invalid race settings");
    return r;
}
}
namespace aiverse::lib::genre {
// The track of a race in the project's own map units, the same block for every host (map "race"):
//   {"laps": 3,
//    "checkpoints": [{"x": 30, "z": 20, "yaw": 0, "width": 16, "respawn": [x, z]}],   // in driving order; last = finish
//    "grid": [{"x": 28, "z": -12, "yaw": 0}],                                           // optional: else the cars' poses
//    "line": [[x, z], ...]}                                                             // closed racing line (race bots)
// yaw: direction of travel in turns (0 = +z, 0.25 = +x); width: gate width. metresPerUnit converts map units (cells in
// a grid map, metres in a metre map) to the metres of the vehicle controllers.
struct RaceTrack{RaceCourse course;std::vector<Vec3> line;bool grid=false;};
template<class Json> RaceTrack readRaceTrack(const Json& j,const std::string& path,Fixed metresPerUnit){
    if(!j.isObject())throw JsonError(path+": expected an object");
    for(const auto& [key,value]:j.members()){(void)value;if(key!="laps"&&key!="checkpoints"&&key!="grid"&&key!="line")throw JsonError(path+"."+key+": unknown key");}
    auto num=[&](const Json& v,const std::string& where){if(!v.isNumber())throw JsonError(where+": expected a number");return v.fx();};
    auto pos=[&](const Json& o,const std::string& where){
        Vec3 p{num(o["x"],where+".x")*metresPerUnit,Fixed{},num(o["z"],where+".z")*metresPerUnit};
        if(!vehicleDetail::vehicleBounded(p,kVehicleCoordLimit))throw JsonError(where+": outside the world");return p;};
    auto yaw=[&](const Json& o,const std::string& where){Fixed y=o.has("yaw")?num(o["yaw"],where+".yaw"):Fixed{};if(fxAbs(y)>1_fx)throw JsonError(where+".yaw: turns in -1..1");return y;};
    RaceTrack t;
    if(j.has("laps")){auto l=raceSessionTicks(j,path,"laps",3);if(l<1||l>1000)throw JsonError(path+".laps: expected 1..1000");t.course.laps=l;}
    const auto& cs=j["checkpoints"];
    if(!cs.isArray()||cs.size()<2||cs.size()>kRaceMaxCheckpoints)throw JsonError(path+".checkpoints: expected 2.."+std::to_string(kRaceMaxCheckpoints)+" gates");
    for(std::size_t i=0;i<cs.size();++i){
        const auto& c=cs[i];auto where=path+".checkpoints["+std::to_string(i)+"]";
        RaceCheckpoint cp;cp.gate.id=int64_t(i+1);cp.gate.center=pos(c,where);auto y=yaw(c,where);
        cp.gate.normal={fxSinTurns(y),Fixed{},fxCosTurns(y)};
        if(!c.has("width"))throw JsonError(where+".width: required");
        cp.gate.radius=num(c["width"],where+".width")*metresPerUnit/2;
        if(cp.gate.radius<=Fixed{})throw JsonError(where+".width: must be positive");
        cp.respawn=cp.gate.center;cp.respawnYaw=y;
        if(c.has("respawn")){const auto& r=c["respawn"];if(!r.isArray()||r.size()!=2)throw JsonError(where+".respawn: expected [x, z]");
            cp.respawn={num(r[0],where+".respawn")*metresPerUnit,Fixed{},num(r[1],where+".respawn")*metresPerUnit};}
        t.course.checkpoints.push_back(cp);
    }
    if(j.has("grid")){
        const auto& g=j["grid"];if(!g.isArray()||g.size()>kRaceMaxRacers)throw JsonError(path+".grid: expected at most "+std::to_string(kRaceMaxRacers)+" slots");
        for(std::size_t i=0;i<g.size();++i){auto where=path+".grid["+std::to_string(i)+"]";t.course.grid.push_back({pos(g[i],where),yaw(g[i],where)});}
        t.grid=true;
    }
    if(j.has("line")){
        const auto& l=j["line"];if(!l.isArray())throw JsonError(path+".line: expected [[x, z], ...]");
        for(std::size_t i=0;i<l.size();++i){const auto& p=l[i];auto where=path+".line["+std::to_string(i)+"]";
            if(!p.isArray()||p.size()!=2)throw JsonError(where+": expected [x, z]");t.line.push_back({num(p[0],where)*metresPerUnit,Fixed{},num(p[1],where)*metresPerUnit});}
        if(!bots::makeRaceLine(t.line))throw JsonError(path+".line: a closed loop of at least 3 distinct points inside the world");
    }
    if(!valid(t.course))throw JsonError(path+": invalid track");
    return t;
}
}
