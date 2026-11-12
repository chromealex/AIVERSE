#pragma once
// Experience pickups (match-progression package): xp orbs dropped by kills, landing delay (falling arc),
// lifetime, per-player pickup radius (the PickupRadius stat), one-shot magnets, optional homing attraction
// (bullet-heaven style; speed 0 = instant ground-plane pickup) and an orb budget that merges values instead of
// growing without bound. Collection totals feed grantXp(..., pickupBonus=true). The owner supplies collector
// positions each tick; the policy never moves units. Fixed math, ascending orb/player ids, ticks strictly increase.
#include "engine/core/Fixed.h"
#include "engine/core/FixedMath.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr Fixed kPickupCoordLimit=Fixed::fromInt(100000);
inline constexpr std::size_t kPickupMaxOrbs=65536,kPickupMaxCollectors=64,kPickupMaxSpawns=65536;
inline constexpr int64_t kPickupMaxValue=1000000000000;

struct PickupConfig {
    Fixed collectRadius=Fixed::fromRatio(5,2); // used when a collector reports radius 0
    Fixed magnetRadius=30_fx,grabRadius=Fixed::fromRatio(1,2);
    Fixed attractSpeed{},attractAccel{},maxSpeed=2_fx; // per tick; attractSpeed 0 = collect on contact
    uint32_t lifetime=0,landing=0,maxOrbs=1024;         // lifetime 0 = forever
};
inline std::string pickupConfigError(const PickupConfig& c){
    auto dist=[](Fixed v){return v>=Fixed{}&&v<=kPickupCoordLimit;};
    if(!dist(c.collectRadius)||!dist(c.magnetRadius)||!dist(c.grabRadius))return "pickups: radii must be 0..100000";
    if(c.attractSpeed<Fixed{}||c.attractAccel<Fixed{}||c.maxSpeed<c.attractSpeed||c.maxSpeed>Fixed::fromInt(1000))return "pickups.attractSpeed: speeds must be 0..maxSpeed (maxSpeed <= 1000)";
    if(c.attractSpeed>Fixed{}&&c.grabRadius<=Fixed{})return "pickups.grabRadius: must be positive with attraction";
    if(c.lifetime>10000000||c.landing>10000000)return "pickups.lifetime: ticks must be 0..10000000";
    if(c.maxOrbs<1||c.maxOrbs>kPickupMaxOrbs)return "pickups.maxOrbs: must be 1..65536";
    return {};
}
struct XpOrb {int64_t id=0,owner=0,value=0;Vec3 position{};uint32_t spawnedAt=0,readyAt=0;int64_t target=0;Fixed speed{};}; // owner 0 = anyone
struct PickupState {std::vector<XpOrb> orbs;int64_t nextId=1;uint32_t lastTick=0;bool initialized=false;};
struct XpCollector {int64_t player=0;Vec3 position{};Fixed radius{};bool magnet=false,active=true;};
struct OrbSpawn {int64_t owner=0,value=0;Vec3 position{};};
struct PickupInput {std::vector<OrbSpawn> spawns;std::vector<XpCollector> collectors;};
enum class PickupEventKind:uint8_t {Spawned,Merged,Evicted,Expired,Attracted,Released,Collected};
struct PickupEvent {PickupEventKind kind=PickupEventKind::Spawned;int64_t orb=0,player=0,value=0;};
struct PickupReport {std::vector<PickupEvent> events;std::vector<std::pair<int64_t,int64_t>> collected;}; // (player, xp) ascending

inline bool pickupBounded(Vec3 p){return p.x>= -kPickupCoordLimit&&p.x<=kPickupCoordLimit&&p.y>= -kPickupCoordLimit&&p.y<=kPickupCoordLimit&&p.z>= -kPickupCoordLimit&&p.z<=kPickupCoordLimit;}
inline Fixed groundDistSq(Vec3 a,Vec3 b){auto dx=a.x-b.x,dz=a.z-b.z;return dx*dx+dz*dz;}
inline bool valid(const PickupState& s){
    if(s.orbs.size()>kPickupMaxOrbs||s.nextId<1)return false;
    int64_t last=0;
    for(const auto& o:s.orbs)if(o.id<=last||o.id>=s.nextId||o.owner<0||o.value<1||o.value>kPickupMaxValue||!pickupBounded(o.position)||o.readyAt<o.spawnedAt||o.target<0||o.speed<Fixed{})return false;else last=o.id;
    return true;
}
inline std::optional<PickupReport> stepPickups(const PickupConfig& c,PickupState& state,uint32_t tick,const PickupInput& in){
    if(!pickupConfigError(c).empty()||(state.initialized&&tick<=state.lastTick)||in.spawns.size()>kPickupMaxSpawns||in.collectors.size()>kPickupMaxCollectors)return std::nullopt;
    for(const auto& sp:in.spawns)if(sp.owner<0||sp.value<1||sp.value>kPickupMaxValue||!pickupBounded(sp.position))return std::nullopt;
    std::vector<XpCollector> cols=in.collectors;std::sort(cols.begin(),cols.end(),[](const XpCollector& a,const XpCollector& b){return a.player<b.player;});
    for(std::size_t i=0;i<cols.size();++i)if(cols[i].player<=0||(i&&cols[i].player==cols[i-1].player)||!pickupBounded(cols[i].position)||cols[i].radius<Fixed{}||cols[i].radius>kPickupCoordLimit)return std::nullopt;
    auto s=state;PickupReport r;auto ev=[&](PickupEventKind k,int64_t orb,int64_t player,int64_t value){r.events.push_back({k,orb,player,value});};
    auto add=[&](int64_t a,int64_t b){return std::min(kPickupMaxValue,a+b);};
    for(const auto& sp:in.spawns){
        if(s.orbs.size()>=c.maxOrbs){
            XpOrb* best=nullptr;
            for(auto& o:s.orbs)if(o.owner==sp.owner&&!o.target&&(!best||groundDistSq(o.position,sp.position)<groundDistSq(best->position,sp.position)))best=&o;
            if(best){best->value=add(best->value,sp.value);ev(PickupEventKind::Merged,best->id,0,sp.value);continue;}
            ev(PickupEventKind::Evicted,s.orbs.front().id,0,s.orbs.front().value);s.orbs.erase(s.orbs.begin());
        }
        if(s.nextId==INT64_MAX)return std::nullopt;
        s.orbs.push_back({s.nextId++,sp.owner,sp.value,sp.position,tick,tick+c.landing,0,{}});ev(PickupEventKind::Spawned,s.orbs.back().id,sp.owner,sp.value);
    }
    std::map<int64_t,int64_t> totals;std::vector<XpOrb> kept;
    auto find=[&](int64_t player)->const XpCollector*{for(const auto& x:cols)if(x.player==player&&x.active)return &x;return nullptr;};
    for(auto& o:s.orbs){
        if(c.lifetime&&!o.target&&tick-o.spawnedAt>=c.lifetime){ev(PickupEventKind::Expired,o.id,0,o.value);continue;}
        if(tick<o.readyAt){kept.push_back(o);continue;}
        if(o.target&&!find(o.target)){ev(PickupEventKind::Released,o.id,o.target,o.value);o.target=0;o.speed={};}
        if(!o.target){
            const XpCollector* best=nullptr;Fixed bestD{};
            for(const auto& x:cols){
                if(!x.active||(o.owner&&o.owner!=x.player))continue;
                auto rad=x.radius>Fixed{}?x.radius:c.collectRadius;auto d=groundDistSq(o.position,x.position);
                bool in=d<=rad*rad||(x.magnet&&d<=c.magnetRadius*c.magnetRadius);
                if(in&&(!best||d<bestD)){best=&x;bestD=d;}
            }
            if(!best){kept.push_back(o);continue;}
            if(c.attractSpeed<=Fixed{}){ev(PickupEventKind::Collected,o.id,best->player,o.value);totals[best->player]=add(totals[best->player],o.value);continue;}
            o.target=best->player;o.speed=c.attractSpeed;ev(PickupEventKind::Attracted,o.id,o.target,o.value);
        }else o.speed=std::min(c.maxSpeed,o.speed+c.attractAccel);
        const auto* x=find(o.target);moveTowards(o.position,x->position,o.speed);
        if(groundDistSq(o.position,x->position)<=c.grabRadius*c.grabRadius){ev(PickupEventKind::Collected,o.id,o.target,o.value);totals[o.target]=add(totals[o.target],o.value);continue;}
        kept.push_back(o);
    }
    s.orbs=std::move(kept);s.lastTick=tick;s.initialized=true;
    for(auto [p,v]:totals)r.collected.push_back({p,v});
    if(!valid(s))return std::nullopt;
    state=std::move(s);return r;
}
}
