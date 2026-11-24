#pragma once
#include "engine/mechanics/Namespace.h"
// JSON definitions of a battle-royale match (a rules block, a map block or a standalone file). Distances
// are metres (decimal literals parsed as Fixed without floating point), times are simulation ticks.
// {"bounds":{"min":[-4000,-4000],"max":[4000,4000]},
//  "zone":{"center":[0,0],"radius":5700,"startTick":3600,"centerPull":1,
//          "phases":[{"wait":5400,"shrink":5400,"radius":2800,"damage":1,"interval":30,"center":[0,0]}]},
//  "flight":{"speed":3,"altitude":800,"margin":200,"crossRadius":1500,"doorOpen":150,"exitCarry":0.25},
//  "drop":{"gravity":0.034,"fallSpeed":2,"diveFallSpeed":3,"glideSpeed":1,"diveGlideSpeed":1.5,
//          "chuteFallSpeed":0.2,"chuteSpeed":0.66,"steer":0.05,"autoDeployHeight":150,"minDeployHeight":40,"chuteDelay":0},
//  "bombardment":{"firstTick":9000,"interval":5400,"duration":600,"maxStrikes":0,"radius":150,"warning":150,
//                 "shellInterval":6,"shellsPerVolley":2,"shellRadius":8,"damage":80},
//  "supplyDrops":{"firstTick":7200,"interval":7200,"maxDrops":4,"fall":600,"altitude":300,"lootTable":1,"edgeMargin":20},
//  "standings":{"knockdowns":true,"finishKnocked":true,"friendlyCredit":false},
//  "lobby":{"minPlayers":2,"fillPlayers":100,"maxPlayers":100,"maxWait":1800,"countdown":300,"fillWithBots":true}}
// Only bounds and zone are required. Unknown keys are errors: JsonError("royale.<path>: ...").
#include "engine/mechanics/royale/Persistence.h"
#include "engine/data/Json.h"
#include <charconv>
#include <initializer_list>
#include <string>
#include <string_view>
namespace aiverse::lib::mechanics {
struct RoyaleRules{
    RoyaleBounds bounds;SafeZoneDef zone;
    std::optional<InsertionFlightDef> flight;SkydiveDef drop;
    std::optional<BombardmentDef> bombardment;std::optional<SupplyDropDef> supplyDrops;
    RoyaleStandingsConfig standings;std::optional<RoyaleLobbyDef> lobby;
};
namespace royaleData {
inline void keys(const Json& j,const std::string& path,std::initializer_list<std::string_view> names){
    if(!j.isObject())throw JsonError(path+": expected object");
    for(const auto& [key,value]:j.members()){(void)value;bool found=false;for(auto n:names)if(n==key)found=true;if(!found)throw JsonError(path+": unknown key '"+key+"'");}
}
inline int64_t integer(const Json& j,const std::string& path,int64_t low,int64_t high){
    if(!j.isNumber())throw JsonError(path+": expected integer");
    const auto& t=j.numberLiteral();int64_t v=0;auto r=std::from_chars(t.data(),t.data()+t.size(),v);
    if(r.ec!=std::errc{}||r.ptr!=t.data()+t.size())throw JsonError(path+": expected integer");
    if(v<low||v>high)throw JsonError(path+": must be in "+std::to_string(low)+".."+std::to_string(high));
    return v;
}
inline uint32_t ticks(const Json& j,const std::string& key,const std::string& path,uint32_t def,uint32_t low=0){
    return j.has(key)?uint32_t(integer(j[key],path+"."+key,low,kRoyaleMaxTicks)):def;
}
inline Fixed number(const Json& j,const std::string& key,const std::string& path,Fixed def,Fixed low,Fixed high){
    if(!j.has(key))return def;
    const auto& v=j[key];if(!v.isNumber())throw JsonError(path+"."+key+": expected number");
    bool ok=false;auto f=Fixed::parse(v.numberLiteral(),&ok);if(!ok)throw JsonError(path+"."+key+": expected number");
    if(f<low||f>high)throw JsonError(path+"."+key+": must be in "+low.toString()+".."+high.toString());
    return f;
}
inline bool flag(const Json& j,const std::string& key,const std::string& path,bool def){
    if(!j.has(key))return def;if(j[key].type()!=Json::Type::Bool)throw JsonError(path+"."+key+": expected true or false");return j[key].boolean();
}
inline Vec2 point(const Json& j,const std::string& path){
    if(!j.isArray()||j.size()!=2)throw JsonError(path+": expected [x,z] in metres");
    Fixed c[2];auto lim=Fixed::fromInt(kRoyaleMaxCoord);
    for(std::size_t i=0;i<2;++i){bool ok=false;if(!j[i].isNumber())throw JsonError(path+": expected [x,z] in metres");c[i]=Fixed::parse(j[i].numberLiteral(),&ok);
        if(!ok||c[i]< -lim||c[i]>lim)throw JsonError(path+": coordinates must stay within +/-"+std::to_string(kRoyaleMaxCoord));}
    return {c[0],c[1]};
}
inline Fixed metres(const Json& j,const std::string& key,const std::string& path,Fixed def,Fixed low={}){return number(j,key,path,def,low,Fixed::fromInt(kRoyaleMaxCoord));}
}
inline RoyaleRules readRoyale(const Json& j,const std::string& root="royale"){
    using namespace royaleData;RoyaleRules r;
    keys(j,root,{"bounds","zone","flight","drop","bombardment","supplyDrops","standings","lobby"});
    {auto p=root+".bounds";const auto& b=j["bounds"];if(b.isNull())throw JsonError(p+": required");keys(b,p,{"min","max"});
     r.bounds={point(b["min"],p+".min"),point(b["max"],p+".max")};if(!valid(r.bounds))throw JsonError(p+": min must be below max on both axes");}
    {auto p=root+".zone";const auto& z=j["zone"];if(z.isNull())throw JsonError(p+": required");keys(z,p,{"center","radius","startTick","centerPull","phases","preDamage","preDamageInterval"});
     auto& d=r.zone;d.bounds=r.bounds;
     d.initial.center=z.has("center")?point(z["center"],p+".center"):Vec2{(r.bounds.min.x+r.bounds.max.x)/2,(r.bounds.min.y+r.bounds.max.y)/2};
     d.initial.radius=metres(z,"radius",p,(r.bounds.max-r.bounds.min).length()/2,Fixed::fromRatio(1,100));
     d.startTick=ticks(z,"startTick",p,0);d.centerPull=number(z,"centerPull",p,1_fx,Fixed{},1_fx);
     d.preDamage=int32_t(z.has("preDamage")?integer(z["preDamage"],p+".preDamage",0,1000000):0);d.preDamageInterval=ticks(z,"preDamageInterval",p,0);
     if(d.preDamage&&!d.preDamageInterval)throw JsonError(p+".preDamageInterval: needs at least 1 tick when preDamage is set");
     const auto& phases=z["phases"];if(!phases.isArray()||!phases.size()||phases.size()>kRoyaleMaxPhases)throw JsonError(p+".phases: expected 1.."+std::to_string(kRoyaleMaxPhases)+" phases");
     Fixed last=d.initial.radius;
     for(std::size_t i=0;i<phases.size();++i){
         auto q=p+".phases["+std::to_string(i)+"]";const auto& ph=phases[i];keys(ph,q,{"wait","shrink","radius","damage","interval","center"});
         SafeZonePhase zp;zp.waitTicks=ticks(ph,"wait",q,0);zp.shrinkTicks=ticks(ph,"shrink",q,1);
         if(!ph.has("radius"))throw JsonError(q+".radius: required");zp.radius=metres(ph,"radius",q,{});
         if(zp.radius>last)throw JsonError(q+".radius: must not exceed the previous radius ("+last.toString()+")");last=zp.radius;
         zp.damage=int32_t(ph.has("damage")?integer(ph["damage"],q+".damage",0,1000000):1);zp.damageInterval=ticks(ph,"interval",q,30);
         if(zp.damage&&!zp.damageInterval)throw JsonError(q+".interval: needs at least 1 tick when damage is set");
         if(ph.has("center"))zp.center=point(ph["center"],q+".center");
         d.phases.push_back(zp);
     }}
    if(j.has("flight")){auto p=root+".flight";const auto& f=j["flight"];keys(f,p,{"speed","altitude","margin","crossRadius","doorOpen","exitCarry"});InsertionFlightDef d;
        d.speed=number(f,"speed",p,d.speed,Fixed::fromRatio(1,1000),1000_fx);d.altitude=metres(f,"altitude",p,d.altitude);d.margin=metres(f,"margin",p,d.margin);
        d.crossRadius=metres(f,"crossRadius",p,d.crossRadius);d.doorOpenTicks=ticks(f,"doorOpen",p,d.doorOpenTicks);d.exitCarry=number(f,"exitCarry",p,d.exitCarry,Fixed{},1_fx);r.flight=d;}
    if(j.has("drop")){auto p=root+".drop";const auto& f=j["drop"];
        keys(f,p,{"gravity","fallSpeed","diveFallSpeed","glideSpeed","diveGlideSpeed","chuteFallSpeed","chuteSpeed","steer","autoDeployHeight","minDeployHeight","chuteDelay"});
        auto& d=r.drop;auto speed=[&](const char* k,Fixed v){return number(f,k,p,v,Fixed::fromRatio(1,1000),1000_fx);};
        d.gravity=speed("gravity",d.gravity);d.fallSpeed=speed("fallSpeed",d.fallSpeed);d.diveFallSpeed=speed("diveFallSpeed",d.diveFallSpeed);
        d.glideSpeed=speed("glideSpeed",d.glideSpeed);d.diveGlideSpeed=speed("diveGlideSpeed",d.diveGlideSpeed);d.chuteFallSpeed=speed("chuteFallSpeed",d.chuteFallSpeed);
        d.chuteSpeed=speed("chuteSpeed",d.chuteSpeed);d.steer=speed("steer",d.steer);d.autoDeployHeight=metres(f,"autoDeployHeight",p,d.autoDeployHeight);
        d.minDeployHeight=metres(f,"minDeployHeight",p,d.minDeployHeight);d.chuteDelayTicks=ticks(f,"chuteDelay",p,d.chuteDelayTicks);
        if(d.diveFallSpeed<d.fallSpeed)throw JsonError(p+".diveFallSpeed: must be at least fallSpeed");
        if(d.autoDeployHeight<d.minDeployHeight)throw JsonError(p+".autoDeployHeight: must be at least minDeployHeight");}
    if(j.has("bombardment")){auto p=root+".bombardment";const auto& f=j["bombardment"];
        keys(f,p,{"firstTick","interval","duration","maxStrikes","radius","warning","shellInterval","shellsPerVolley","shellRadius","damage"});BombardmentDef d;
        d.firstTick=ticks(f,"firstTick",p,d.firstTick);d.intervalTicks=ticks(f,"interval",p,d.intervalTicks,1);d.durationTicks=ticks(f,"duration",p,d.durationTicks,1);
        if(d.durationTicks>d.intervalTicks)throw JsonError(p+".duration: must not exceed interval");
        d.maxStrikes=ticks(f,"maxStrikes",p,0);d.areaRadius=metres(f,"radius",p,d.areaRadius,Fixed::fromRatio(1,100));d.warningTicks=ticks(f,"warning",p,d.warningTicks);
        d.shellInterval=ticks(f,"shellInterval",p,d.shellInterval,1);d.shellsPerVolley=f.has("shellsPerVolley")?uint32_t(integer(f["shellsPerVolley"],p+".shellsPerVolley",1,64)):1;
        d.shellRadius=metres(f,"shellRadius",p,d.shellRadius,Fixed::fromRatio(1,100));d.damage=int32_t(f.has("damage")?integer(f["damage"],p+".damage",0,1000000):d.damage);r.bombardment=d;}
    if(j.has("supplyDrops")){auto p=root+".supplyDrops";const auto& f=j["supplyDrops"];keys(f,p,{"firstTick","interval","maxDrops","fall","altitude","lootTable","edgeMargin"});SupplyDropDef d;
        d.firstTick=ticks(f,"firstTick",p,d.firstTick);d.intervalTicks=ticks(f,"interval",p,d.intervalTicks,1);
        d.maxDrops=f.has("maxDrops")?uint32_t(integer(f["maxDrops"],p+".maxDrops",0,256)):d.maxDrops;d.fallTicks=ticks(f,"fall",p,d.fallTicks);
        d.altitude=metres(f,"altitude",p,d.altitude);d.lootTable=f.has("lootTable")?integer(f["lootTable"],p+".lootTable",1,INT64_MAX):1;d.edgeMargin=metres(f,"edgeMargin",p,d.edgeMargin);r.supplyDrops=d;}
    if(j.has("standings")){auto p=root+".standings";const auto& f=j["standings"];keys(f,p,{"knockdowns","finishKnocked","friendlyCredit"});
        r.standings.knockdowns=flag(f,"knockdowns",p,true);r.standings.finishKnocked=flag(f,"finishKnocked",p,true);r.standings.friendlyCredit=flag(f,"friendlyCredit",p,false);}
    if(j.has("lobby")){auto p=root+".lobby";const auto& f=j["lobby"];keys(f,p,{"minPlayers","fillPlayers","maxPlayers","maxWait","countdown","fillWithBots"});RoyaleLobbyDef d;
        auto count=[&](const char* k,uint32_t v){return f.has(k)?uint32_t(integer(f[k],p+"."+k,1,int64_t(kRoyaleMaxCompetitors))):v;};
        d.maxPlayers=count("maxPlayers",d.maxPlayers);d.fillPlayers=count("fillPlayers",std::min(d.fillPlayers,d.maxPlayers));d.minPlayers=count("minPlayers",std::min(d.minPlayers,d.fillPlayers));
        if(d.minPlayers>d.fillPlayers||d.fillPlayers>d.maxPlayers)throw JsonError(p+": needs minPlayers <= fillPlayers <= maxPlayers");
        d.maxWaitTicks=ticks(f,"maxWait",p,d.maxWaitTicks);d.countdownTicks=ticks(f,"countdown",p,d.countdownTicks);d.fillWithBots=flag(f,"fillWithBots",p,true);r.lobby=d;}
    if(!valid(r.zone)||(r.flight&&!valid(*r.flight))||!valid(r.drop)||(r.bombardment&&!valid(*r.bombardment))||(r.supplyDrops&&!valid(*r.supplyDrops))||(r.lobby&&!valid(*r.lobby)))
        throw JsonError(root+": definition is out of range");
    return r;
}
// Content hash of the definition JSON for the save envelope (compact dump: key order as in the file).
inline uint64_t royaleDataHash(const Json& j){Hasher h;h.str(j.dump(-1));return h.value();}
}
