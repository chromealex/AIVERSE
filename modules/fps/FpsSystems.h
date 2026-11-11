#pragma once
#include "engine/world/SpatialWorld.h"
#include "engine/core/Archive.h"
namespace aiverse::fps {
enum class Action:uint8_t { None, Frag, Smoke, Flash, Heal, Interact, Drop, Buy, Plant, Attach, Vault };
enum class ItemKind:int32_t { Medkit=1,Armor,Frag,Smoke,Flash,Scope,Suppressor,Magazine,Ammo };
struct Item { int32_t type=0,count=0; bool operator==(const Item&)const=default; };
inline void io(Archive& ar,Item& v){ar(v.type,v.count);}
struct SystemsConfig {
    bool enabled=false,knockdown=false;
    int32_t startArmor=0,startMoney=800,killReward=300,winReward=3000,lossReward=1500;
    uint32_t healTicks=180,reviveTicks=240,bleedTicks=1800,freezeTicks=180,buyTicks=1200,plantTicks=180,defuseTicks=300,bombTicks=2400,roundsToWin=3;
    uint32_t grenadeFuse=120,smokeTicks=900,flashTicks=180;
    Fixed interactRange=3_fx,blastRadius=8_fx,smokeRadius=6_fx,flashRadius=20_fx;
};
struct Projectile {
    uint32_t id=0,expires=0;uint8_t owner=0,kind=0; // 0 bullet, 1 frag, 2 smoke, 3 flash, 4 smoke cloud
    Vec3 pos{},velocity{};int32_t damage=0;Fixed remaining{};uint8_t weapon=0;Fixed penetration{};uint32_t pierced[8]{};uint8_t piercedCount=0;
};
inline void io(Archive& ar,Projectile& v){ar(v.id,v.expires,v.owner,v.kind,v.pos,v.velocity,v.damage,v.remaining,v.weapon,v.penetration,v.pierced,v.piercedCount);}
struct Loot {uint32_t id=0;Vec3 pos{};int32_t type=0,count=1,ammo=0,reserve=0;uint8_t attachments=0;};
inline void io(Archive& ar,Loot& v){ar(v.id,v.pos,v.type,v.count,v.ammo,v.reserve,v.attachments);}
struct Vehicle {uint32_t id=0;Vec3 pos{},velocity{};Fixed yaw{},speed=15_fx;uint8_t driver=0;int32_t health=500;};
inline void io(Archive& ar,Vehicle& v){ar(v.id,v.pos,v.velocity,v.yaw,v.speed,v.driver,v.health);}
struct RoundState {
    uint32_t start=0,end=0,bombExplodes=0;int32_t scoreA=0,scoreB=0,winner=0;
    bool planted=false;Vec3 bombPos{};uint8_t carrier=1;
};
inline void io(Archive& ar,RoundState& v){ar(v.start,v.end,v.bombExplodes,v.scoreA,v.scoreB,v.winner,v.planted,v.bombPos,v.carrier);}
}
