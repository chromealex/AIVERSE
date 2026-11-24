#include "ActorModule.h"
#include "engine/mechanics/EcsWeaponLaunchSystems.h"
#include <algorithm>
#include <set>
#include "engine/core/FixedMath.h"
#include "engine/mechanics/EcsTimedMechanics.h"
#include "engine/world/OrientedBox.h"
#include "engine/nav/CaveActorMotorSteering.h"
#include "engine/world/TerraWeaveCavernsArchive.h"
#include "engine/world/TerraWeaveCavernsVolumeCollision.h"
#include <stdexcept>

namespace aiverse::actor {
namespace {
int64_t integer(const Json& j,const char* key,int64_t fallback,int64_t lo,int64_t hi) {
    if(j.has(key)&&(!j[key].isNumber()||j[key].fx().raw%Fixed::kOne))throw JsonError(std::string("actor.")+key+": expected integer");
    auto v=j.getInt(key,fallback);if(v<lo||v>hi)throw JsonError(std::string("actor.")+key+": outside supported range");return v;
}
Fixed number(const Json& j,const char* key,Fixed fallback,Fixed lo,Fixed hi) {
    if(j.has(key)&&!j[key].isNumber())throw JsonError(std::string("actor.")+key+": expected number");
    auto v=j.getFx(key,fallback);if(v<lo||v>hi)throw JsonError(std::string("actor.")+key+": outside supported range");return v;
}
Vec3 vector(const Json& j) {
    if(!j.isArray()||j.size()!=3)throw JsonError("actor: expected three coordinates");
    Vec3 v;Fixed* p[]={&v.x,&v.y,&v.z};
    for(int i=0;i<3;++i){if(!j[size_t(i)].isNumber())throw JsonError("actor: non-numeric coordinate");*p[i]=j[size_t(i)].fx();if(fxAbs(*p[i])>100000_fx)throw JsonError("actor: coordinate exceeds 100 km");}return v;
}
void royaleIo(Archive& a,RoyaleRun& r) {
    using lib::mechanics::io;
    a(r.enabled);if(!r.enabled)return;
    io(a,r.zone);io(a,r.bombardment);io(a,r.drops);io(a,r.flight);
    uint32_t n=a.count(r.divers.size());if(n>256){a.fail();return;}if(!a.writing())r.divers.resize(n);for(auto& d:r.divers)io(a,d);
    io(a,r.standings);a(r.lastAttacker,r.lastWeapon);
    uint32_t feed=a.count(r.feed.size());if(feed>6){a.fail();return;}if(!a.writing())r.feed.resize(feed);for(auto& e:r.feed)lib::mechanics::io(a,e);
}
}
Config readConfig(const Json& root) {
    if(!root["actor"].isObject())throw JsonError("rules.actor: expected object");
    // Capability blocks that are not specific to this host may sit at the top level of the rules (shared names,
    // docs/no-genre-split-audit.md G6); the legacy place rules.actor.<key> still loads. One place per block.
    Json j=root["actor"];
    for(const char* key:{"royale","gear","crate","vehicleTypes","bots","weapons","weapon","locomotion"}){
        if(!root.has(key))continue;
        if(j.has(key))throw JsonError(std::string("rules.")+key+" and rules.actor."+key+": keep one of them");
        j.set(key,root[key]);
    }
    Config c;
    c.tickRate=uint32_t(integer(j,"tickRate",60,20,120));
    c.snapshotRate=uint32_t(integer(j,"snapshotRate",20,1,c.tickRate));
    c.maxPlayers=uint32_t(integer(j,"maxPlayers",100,1,200));
    c.rewindTicks=uint32_t(integer(j,"rewindTicks",12,0,120));
    c.inputTimeout=uint32_t(integer(j,"inputTimeout",15,1,120));
    c.interestRadius=number(j,"interestRadius",250_fx,10_fx,10000_fx);
    c.worldLimit=number(j,"worldLimit",100000_fx,10_fx,100000_fx);
    c.mode=j.getStr("mode","deathmatch");
    if(c.mode!="deathmatch"&&c.mode!="elimination"&&c.mode!="battleRoyale"&&c.mode!="bomb")throw JsonError("actor.mode: unknown mode");
    c.friendlyFire=j.getBool("friendlyFire",false);
    c.roundTicks=uint32_t(integer(j,"roundTicks",7200,1,10000000));
    c.respawnTicks=uint32_t(integer(j,"respawnTicks",180,1,100000));
    if(j.has("locomotion")){
        // Same keys, ranges and relations as the generated gameplay settings section `locomotion`.
        if(!j["locomotion"].isObject())throw JsonError("rules.locomotion: expected object");
        Json settings=Json::object();settings.set("schemaVersion",Json::number(int64_t(1)));settings.set("locomotion",j["locomotion"]);
        auto parsed=lib::gameplay::readGameplaySettings(settings);
        if(!parsed.value)throw JsonError("rules."+parsed.errors.at(0));
        auto modes=lib::mechanics::locomotionModeConfig(*parsed.value);
        if(auto error=lib::mechanics::locomotionModeError(modes);!error.empty())throw JsonError("rules."+error);
        c.locomotion=modes;
    }
    const auto& m=j["motor"];
    c.motor.radius=number(m,"radius",c.motor.radius,Fixed::fromRatio(1,10),2_fx);
    c.motor.height=number(m,"height",c.motor.height,c.motor.radius*2,4_fx);
    c.motor.crouchHeight=number(m,"crouchHeight",c.motor.crouchHeight,c.motor.radius*2,c.motor.height);
    c.motor.speed=number(m,"speed",5_fx,1_fx,30_fx);
    c.motor.sprintSpeed=number(m,"sprintSpeed",8_fx,c.motor.speed,50_fx);
    c.motor.gravity=number(m,"gravity",20_fx,1_fx,100_fx);
    c.motor.jumpSpeed=number(m,"jumpSpeed",6_fx,Fixed{},30_fx);
    c.motor.stepHeight=number(m,"stepHeight",c.motor.stepHeight,Fixed{},1_fx);
    auto weapon=[&](const Json& w) {
        if(!w.isNull()&&!w.isObject())throw JsonError("actor.weapon: expected object");
        WeaponConfig v;
        v.damage=int32_t(integer(w,"damage",25,1,1000));
        v.magazine=int32_t(integer(w,"magazine",30,1,1000));
        v.reserve=int32_t(integer(w,"reserve",120,0,100000));
        v.interval=uint32_t(integer(w,"intervalTicks",6,1,1000));
        v.reloadTicks=uint32_t(integer(w,"reloadTicks",120,1,100000));
        v.switchTicks=uint32_t(integer(w,"switchTicks",15,1,1000));
        v.range=number(w,"range",300_fx,1_fx,10000_fx);
        v.spread=number(w,"spread",Fixed{},Fixed{},Fixed::fromRatio(1,20));
        v.hipSpread=number(w,"hipSpread",Fixed{},Fixed{},Fixed::fromRatio(1,20));
        v.moveSpread=number(w,"moveSpread",Fixed{},Fixed{},Fixed::fromRatio(1,20));
        v.recoil=number(w,"recoil",Fixed{},Fixed{},Fixed::fromRatio(1,50));
        v.bulletSpeed=number(w,"bulletSpeed",Fixed{},Fixed{},2000_fx);
        v.bulletGravity=number(w,"bulletGravity",10_fx,Fixed{},100_fx);
        if(w.has("flight")){
            v.flight=projectile::parseProjectileFlight(w["flight"],"actor.weapon.flight");
            if(v.bulletSpeed<=Fixed{})throw JsonError("actor.weapon.flight requires bulletSpeed > 0");
            if(!w["flight"].has("gravity"))v.flight.gravity=v.bulletGravity;
        }
        v.penetration=number(w,"penetration",Fixed{},Fixed{},10_fx);
        v.headMultiplier=number(w,"headMultiplier",2_fx,1_fx,5_fx);
        v.limbMultiplier=number(w,"limbMultiplier",Fixed::fromRatio(3,4),Fixed::fromRatio(1,10),1_fx);
        v.price=int32_t(integer(w,"price",600,0,16000));
        v.pellets=uint32_t(integer(w,"pellets",1,1,16));
        v.automatic=w.getBool("automatic",true);
        v.nameKey=w.getStr("nameKey","actor.weapon.rifle");
        return v;
    };
    if(j.has("weapons")) {
        if(!j["weapons"].isArray()||j["weapons"].size()<1||j["weapons"].size()>4)throw JsonError("actor.weapons: expected 1..4 weapons");
        for(const auto& w:j["weapons"].items())c.weapons.push_back(weapon(w));
    }else c.weapons.push_back(weapon(j["weapon"]));
    c.weapon=c.weapons.front(); // Legacy single-weapon rules remain supported.
    const auto& z=j["zone"];
    if(z.has("center"))c.zoneCenter=vector(z["center"]);
    c.zoneRadius=number(z,"radius",2000_fx,1_fx,100000_fx);
    c.zoneEndRadius=number(z,"endRadius",10_fx,Fixed{},c.zoneRadius);
    c.zoneStart=uint32_t(integer(z,"startTick",1800,0,10000000));
    c.zoneDuration=uint32_t(integer(z,"durationTicks",7200,1,10000000));
    c.zoneDamage=int32_t(integer(z,"damagePerSecond",5,1,100));
    if(j.has("royale")){
        if(c.mode!="battleRoyale")throw JsonError("actor.royale: requires fps.mode \"battleRoyale\"");
        c.royale=lib::mechanics::readRoyale(j["royale"],"actor.royale");
    }
    if(j.has("crate")){
        if(!j["crate"].isArray()||j["crate"].size()>16)throw JsonError("actor.crate: expected at most 16 items");
        for(const auto& item:j["crate"].items()){
            auto type=integer(item,"type",0,1,255),count=integer(item,"count",1,1,20);
            if(!((type>=1&&type<=9)||(type>=100&&type<100+int64_t(c.weapons.size()))||type>=200))throw JsonError("actor.crate: invalid item type "+std::to_string(type));
            c.crate.push_back({int32_t(type),int32_t(count)});
        }
    }else if(c.royale)c.crate={{2,1},{1,2},{8,1},{9,3}};
    const auto& systems=j["mechanics"];auto& mcfg=c.systems;
    mcfg.enabled=systems.getBool("enabled",false);mcfg.knockdown=systems.getBool("knockdown",false);
    mcfg.startArmor=int32_t(integer(systems,"startArmor",0,0,100));
    mcfg.startMoney=int32_t(integer(systems,"startMoney",800,0,16000));
    mcfg.killReward=int32_t(integer(systems,"killReward",300,0,16000));
    mcfg.winReward=int32_t(integer(systems,"winReward",3000,0,16000));
    mcfg.lossReward=int32_t(integer(systems,"lossReward",1500,0,16000));
    auto time=[&](const char* key,uint32_t seconds){return uint32_t(integer(systems,key,seconds*c.tickRate,1,100000));};
    mcfg.healTicks=time("healTicks",3);mcfg.reviveTicks=time("reviveTicks",4);mcfg.bleedTicks=time("bleedTicks",30);
    mcfg.freezeTicks=time("freezeTicks",3);mcfg.buyTicks=time("buyTicks",20);
    mcfg.plantTicks=time("plantTicks",3);mcfg.defuseTicks=time("defuseTicks",5);mcfg.bombTicks=time("bombTicks",40);
    mcfg.grenadeFuse=time("grenadeFuse",2);mcfg.smokeTicks=time("smokeTicks",15);mcfg.flashTicks=time("flashTicks",3);
    mcfg.roundsToWin=uint32_t(integer(systems,"roundsToWin",3,1,100));
    mcfg.interactRange=number(systems,"interactRange",3_fx,1_fx,5_fx);
    mcfg.blastRadius=number(systems,"blastRadius",8_fx,1_fx,30_fx);
    mcfg.smokeRadius=number(systems,"smokeRadius",6_fx,1_fx,20_fx);
    mcfg.flashRadius=number(systems,"flashRadius",20_fx,1_fx,50_fx);
    if(c.mode=="bomb"&&!mcfg.enabled)throw JsonError("actor bomb mode requires mechanics.enabled");
    if(j.has("gear")){
        if(!c.systems.enabled)throw JsonError("actor.gear: requires mechanics.enabled");
        c.gear=lib::systems::readGearCatalog(j["gear"],"actor.gear");
        for(const auto& [id,g]:c.gear)if(id>55)throw JsonError("actor.gear: ids must be 1..55 (loot type 200 + id)");
    }
    for(auto item:c.crate)if(item.type>=200&&!c.gear.contains(item.type-200))throw JsonError("actor.crate: unknown gear "+std::to_string(item.type-200));
    if(j.has("bots")){if(!j["bots"].isObject())throw JsonError("actor.bots: expected object");c.bots=j["bots"];}
    if(j.has("vehicleTypes")){
        if(!c.systems.enabled)throw JsonError("actor.vehicleTypes: requires mechanics.enabled");
        c.vehicleTypes=lib::mechanics::readVehicleDefinitions(j["vehicleTypes"]);
        for(const auto& [id,d]:c.vehicleTypes->vehicles)if(d.seats.seats.empty())throw JsonError("actor.vehicleTypes: vehicle "+std::to_string(id)+" needs seats");
    }
    const auto& melee=j["melee"];auto& mel=c.melee;
    if(!melee.isNull()&&!melee.isObject())throw JsonError("actor.melee: expected object");
    mel.enabled=melee.getBool("enabled",false);
    mel.damage=int32_t(integer(melee,"damage",50,1,1000));
    mel.windup=uint32_t(integer(melee,"windupTicks",c.tickRate/10,0,3600));
    mel.active=uint32_t(integer(melee,"activeTicks",std::max<uint32_t>(1,c.tickRate/15),1,3600));
    mel.recovery=uint32_t(integer(melee,"recoveryTicks",c.tickRate/5,0,3600));
    mel.bufferTicks=uint32_t(integer(melee,"bufferTicks",c.tickRate*2/15,0,3600));
    mel.parryTicks=uint32_t(integer(melee,"parryTicks",c.tickRate*2/15,0,3600));
    mel.staggerTicks=uint32_t(integer(melee,"staggerTicks",c.tickRate/2,0,3600));
    mel.range=number(melee,"range",2_fx,Fixed::fromRatio(1,2),10_fx);
    mel.radius=number(melee,"radius",Fixed::fromRatio(3,10),Fixed{},2_fx);
    mel.guardTurns=number(melee,"guardTurns",Fixed::fromRatio(1,8),Fixed{},Fixed::fromRatio(1,4));
    mel.lockRange=number(melee,"lockRange",8_fx,mel.range,100_fx);
    return c;
}
void ActorHost::equipInitial(CharacterSnapshot& a) const {
    a.weaponIndex=0;a.owned=config_.mode=="bomb"?1:uint8_t((1u<<config_.weapons.size())-1);
    for(size_t i=0;i<config_.weapons.size();++i){a.storedAmmo[i]=config_.weapons[i].magazine;a.storedReserve[i]=config_.weapons[i].reserve;}
    a.ammo=a.storedAmmo[0];a.reserve=a.storedReserve[0];
    if(config_.systems.enabled)a.bag={{1,2},{3,2},{4,2},{5,2}};
}
// ---------------------------------------------------------------- ECS accessors
size_t ActorHost::characterCount(const Simulation& sim) const {
    const auto* index=sim.world().resourceConst<CharacterIndex>();
    return index?index->bySlot.size():0;
}
Entity ActorHost::characterEntity(const Simulation& sim,uint8_t slot) const {
    const auto* index=sim.world().resourceConst<CharacterIndex>();
    if(!index||slot==0||size_t(slot)>index->bySlot.size())return kNullEntity;
    const Entity e=index->bySlot[size_t(slot-1)];
    return sim.world().alive(e)?e:kNullEntity;
}
CharacterRef ActorHost::character(Simulation& sim,uint8_t slot) const {
    const Entity e=characterEntity(sim,slot);
    if(!e.valid())throw std::out_of_range("actor: no character in slot "+std::to_string(slot));
    return makeCharacterRef<false>(sim.world(),e);
}
ConstCharacterRef ActorHost::character(const Simulation& sim,uint8_t slot) const {
    const Entity e=characterEntity(sim,slot);
    if(!e.valid())throw std::out_of_range("actor: no character in slot "+std::to_string(slot));
    return makeCharacterRef<true>(sim.world(),e);
}
CharacterSnapshot ActorHost::snapshot(const Simulation& sim,uint8_t slot) const { return actor::snapshot(character(sim,slot)); }
std::vector<CharacterSnapshot> ActorHost::snapshots(const Simulation& sim) const {
    std::vector<CharacterSnapshot> out;const size_t n=characterCount(sim);out.reserve(n);
    for(size_t slot=1;slot<=n;++slot)out.push_back(actor::snapshot(character(sim,uint8_t(slot))));
    return out;
}
std::vector<HitPose> ActorHost::livePoses(Simulation& sim) const {
    const World& w=sim.world();const auto* index=w.resourceConst<CharacterIndex>();
    std::vector<HitPose> out;if(!index)return out;out.reserve(index->bySlot.size());
    for(const Entity e:index->bySlot){
        const auto* id=w.tryGetConst<CharacterId>(e);const auto* body=w.tryGetConst<CharacterBody>(e);const auto* health=w.tryGetConst<CharacterHealth>(e);
        if(!id||!body||!health)throw std::logic_error("actor: character components missing");
        out.push_back({id->slot,id->team,id->life,health->health,body->pos,body->crouched,body->prone,health->downed});
    }
    return out;
}
lib::systems::GearLoadout* ActorHost::gear(Simulation& sim,uint8_t slot) const {
    return sim.world().tryGet<lib::systems::GearLoadout>(characterEntity(sim,slot));
}
const LocomotionBody* ActorHost::locomotion(const Simulation& sim,uint8_t slot) const {
    const Entity e=characterEntity(sim,slot);return e.valid()?sim.world().tryGetConst<LocomotionBody>(e):nullptr;
}
const lib::systems::GearLoadout* ActorHost::gear(const Simulation& sim,uint8_t slot) const {
    return sim.world().tryGetConst<lib::systems::GearLoadout>(characterEntity(sim,slot));
}
template<class T> std::vector<Entity> ActorHost::orderedById(const Simulation& sim) const {
    const World& w=sim.world();std::vector<std::pair<uint32_t,Entity>> items;
    w.eachExistingConst<T>([&](Entity e,const T& v){if(w.alive(e))items.push_back({v.id,e});});
    std::sort(items.begin(),items.end(),[](const auto& a,const auto& b){return a.first<b.first;});
    std::vector<Entity> out;out.reserve(items.size());
    for(const auto& item:items)out.push_back(item.second);
    return out;
}
std::vector<Entity> ActorHost::projectileEntities(const Simulation& sim) const { return orderedById<Projectile>(sim); }
std::vector<Entity> ActorHost::lootEntities(const Simulation& sim) const { return orderedById<Loot>(sim); }
std::vector<Entity> ActorHost::vehicleEntities(const Simulation& sim) const { return orderedById<Vehicle>(sim); }
namespace {
template<class T> std::vector<T> values(const World& w,const std::vector<Entity>& entities) {
    std::vector<T> out;out.reserve(entities.size());
    for(const Entity e:entities)out.push_back(*w.tryGetConst<T>(e));
    return out;
}
}
std::vector<Projectile> ActorHost::projectiles(const Simulation& sim) const { return values<Projectile>(sim.world(),projectileEntities(sim)); }
std::vector<Loot> ActorHost::loot(const Simulation& sim) const { return values<Loot>(sim.world(),lootEntities(sim)); }
std::vector<Vehicle> ActorHost::vehicles(const Simulation& sim) const { return values<Vehicle>(sim.world(),vehicleEntities(sim)); }
Entity ActorHost::vehicleEntity(const Simulation& sim,uint32_t id) const {
    Entity found=kNullEntity;if(!id)return found;
    const World& w=sim.world();
    w.eachExistingConst<Vehicle>([&](Entity e,const Vehicle& v){if(v.id==id&&w.alive(e))found=e;});
    return found;
}
void ActorHost::spawnProjectile(Simulation& sim,Projectile projectile) {
    World& w=sim.world();
    if(w.countExisting<Projectile>()>=kMaxProjectiles)throw std::logic_error("FPS projectile limit exceeded at spawn");
    const Entity e=w.create();w.add<Projectile>(e,std::move(projectile));
}
void ActorHost::spawnLoot(Simulation& sim,Loot loot) {
    World& w=sim.world();const Entity e=w.create();w.add<Loot>(e,std::move(loot));
}
Entity ActorHost::spawnVehicle(Simulation& sim,Vehicle vehicle) {
    World& w=sim.world();const Entity e=w.create();w.add<Vehicle>(e,std::move(vehicle));return e;
}
void ActorHost::clearProjectiles(Simulation& sim) {
    World& w=sim.world();
    for(const Entity e:projectileEntities(sim))w.destroy(e);
}
void ActorHost::resetLootAndVehicles(Simulation& sim) {
    World& w=sim.world();
    for(const Entity e:lootEntities(sim))w.destroy(e);
    for(const auto& l:initialLoot_)spawnLoot(sim,l);
    // Vehicles are never created or destroyed after setup: restore their initial values in place
    // (the entity and a typed vehicle's VehicleDrive stay).
    for(const Entity e:vehicleEntities(sim)){
        auto& v=*w.tryGet<Vehicle>(e);
        for(const auto& initial:initialVehicles_)if(initial.id==v.id){v=initial;break;}
    }
}
// ---------------------------------------------------------------- setup
void ActorHost::setup(Simulation& sim,const Json& rules,const Json& map) {
    config_=readConfig(rules);
    if(sim.tickRate()!=config_.tickRate)throw JsonError("actor.tickRate differs from simulation tickRate");
    if(sim.players().empty()||sim.players().size()>config_.maxPlayers)throw JsonError("actor: invalid player count");
    streamedCollision_.reset();streamManifest_=map["world"];
    if(streamManifest_.getBool("async"))world::WorldAssets::parse(streamManifest_);
    collision_.clear();spawns_.clear();bots_.clear();
    const auto& boxes=map["colliders"];
    if(!boxes.isArray()||boxes.size()>100000)throw JsonError("actor.colliders: expected at most 100000 boxes");
    uint32_t rotated=kRotatedColliderBase;
    for(const auto& b:boxes.items()) {
        if(b.has("yaw")){   // rotated box: {"id","center","half","yaw" (turns)} -> 12 exact triangles
            auto id=integer(b,"id",0,1,kBuildingColliderBase-1);(void)id;
            if(!b["yaw"].isNumber())throw JsonError("actor.collider.yaw: expected turns");
            auto half=vector(b["half"]);if(half.x<=Fixed{}||half.y<=Fixed{}||half.z<=Fixed{}||half.x>2048_fx||half.z>2048_fx)throw JsonError("actor.collider.half: expected positive half sizes up to 2048");
            for(const auto& t:world::orientedBoxTriangles(vector(b["center"]),half,b["yaw"].fx()))
                if(!collision_.addTriangle(rotated++,t))throw JsonError("actor.collider: invalid rotated box");
            continue;
        }
        auto id=integer(b,"id",0,1,UINT32_MAX);
        if(collision_.colliders().contains(uint32_t(id))||!collision_.add({uint32_t(id),{vector(b["min"]),vector(b["max"])}}))throw JsonError("actor.collider: invalid or duplicate box");
    }
    // Opt-in world-space cave navigation. Generate authored collision from
    // the SAME immutable SDF, install into this EXISTING SpatialWorld before
    // character spawn/motor setup, never create a second actor controller.
    caveNavigation_.reset();
    const auto& caveNavConfig=map["cavernsNavigation"];
    if(caveNavConfig.isObject()&&caveNavConfig.getBool("enabled",false)){
        using namespace terraweave::caverns;
        const Json& authored=map["terraweaveCaverns"].isObject()
            ?map["terraweaveCaverns"]:map["extra"]["terraweaveCaverns"];
        if(!authored.isObject())
            throw JsonError("actor.cavernsNavigation: missing authored SDF");
        auto snapshot=std::make_shared<CaveVolumeSnapshot>(
            caveSnapshotFromJson(authored));
        auto indexed=std::make_shared<const CaveSnapshotSpatialIndex>(snapshot);
        const auto& keys=caveNavConfig["collisionChunks"];
        if(!keys.isArray()||keys.size()==0||keys.size()>64)
            throw JsonError("actor.cavernsNavigation: 1..64 collisionChunks required");
        const uint32_t resolution=uint32_t(
            caveNavConfig.getInt("resolution",16));
        const int32_t spacingCm=int32_t(
            caveNavConfig.getInt("spacingCm",100));
        uint32_t nativeId=0x60000000u;
        for(const auto& tuple:keys.items()){
            if(!tuple.isArray()||tuple.size()!=3)
                throw JsonError("actor.cavernsNavigation: chunk [x,y,z] expected");
            const ChunkKey3 key{
                tuple[size_t(0)].integer(),
                tuple[size_t(1)].integer(),
                tuple[size_t(2)].integer()};
            auto surface=generateCaveIndexedSnapshotMesh(
                *indexed,key,resolution,spacingCm);
            auto collision=prepareCaveVolumeCollision(
                surface,resolution,spacingCm,snapshot->sourceRevision);
            if(collision.triangles.size()>65534u ||
               collision.triangles.size()>UINT32_MAX-nativeId)
                throw JsonError("actor.cavernsNavigation: collider id budget");
            for(const auto& triangle:collision.triangles)
                if(!collision_.addTriangle(nativeId++,triangle))
                    throw JsonError("actor.cavernsNavigation: invalid native collider");
        }
        caveNavigation_=std::make_shared<const CaveNavigationDataProvider>(
            std::move(indexed));
    }
    auto buildingLoot=buildMapBuildings(map);
    const auto& spawns=map["spawns"];
    if(!spawns.isArray()||spawns.size()<sim.players().size())throw JsonError("actor.spawns: one spawn per player required");
    std::vector<CharacterSnapshot> characters;
    for(size_t i=0;i<sim.players().size();++i) {
        CharacterSnapshot a;a.slot=uint8_t(i+1);a.team=sim.players()[i].team;
        if(a.team==0)throw JsonError("actor.player.team: must be positive");
        a.pos=vector(spawns[i]);
        if(fxAbs(a.pos.x)>config_.worldLimit||fxAbs(a.pos.y)>config_.worldLimit||fxAbs(a.pos.z)>config_.worldLimit)throw JsonError("actor.spawn: outside world limit");
        equipInitial(a);
        auto b=actorBounds(a,config_.motor);b.min.y+=Fixed::fromRatio(1,1000);
        if(collision_.blocked(b))throw JsonError("actor.spawn: intersects collision");
        characters.push_back(a);spawns_.push_back(a.pos);bots_.push_back(map["players"][i].getBool("bot",false));
    }
    World& world=sim.world();
    world.setResource<State>();
    // One character entity per player slot, never destroyed during the match.
    CharacterIndex index;
    for(const auto& a:characters){
        const Entity e=world.create();addCharacterComponents(world,e,a);index.bySlot.push_back(e);
        if(config_.locomotion){LocomotionBody l;l.life=a.life;world.add<LocomotionBody>(e,l);}
    }
    world.setResource<CharacterIndex>(std::move(index));
    auto& s=state(sim);
    setupSystems(sim,map);
    if(config_.systems.enabled)for(auto l:buildingLoot)if(world.countExisting<Loot>()<kMaxLoot){l.id=s.nextObject++;spawnLoot(sim,l);}
    setupRoyale(sim,map);
    if(!config_.gear.empty())for(const Entity e:world.resourceConst<CharacterIndex>()->bySlot)world.add<lib::systems::GearLoadout>(e);
    setupVehicles(sim,map);
    setupRace(sim,rules,map);
    setupBots(sim,map);
    if(caveNavigation_){
        sim.registerPersistentComponent<nav::CaveRouteIntent>(
            "caverns.route.intent",1);
        sim.registerPersistentComponent<nav::CaveRouteFollower>(
            "caverns.route",2);
        // Actor's own "actor" system remains the ONLY character mover;
        // route goals are assigned explicitly to entities by game commands.
    }
    meleeAttacks_.clear();
    if(config_.melee.enabled){
        const auto& m=config_.melee;auto total=m.windup+m.active+m.recovery;
        meleeAttacks_[1]={1,m.windup,m.active,m.recovery,total,m.bufferTicks,{1}};
        if(!lib::mechanics::valid(meleeAttacks_))throw JsonError("actor.melee: invalid attack timing");
    }
    Hasher hash;hash.str(rules.dump(-1));hash.str(map.dump(-1));if(streamManifest_.getBool("async"))hash.str("world-stream-v1");contentHash_=hash.value();
    // Stage order: programmed projectiles, ordinary projectiles (bullets, grenades, smoke), then the
    // match / royale / character tick. Immediate hits of the projectile stages precede everything else.
    sim.addSystem("actor.projectilePrograms",[this](Simulation& s) {
        if(state(s).winner)return;
        tickProjectilePrograms(s);
    });
    sim.addSystem("actor.ordinaryProjectiles",[this](Simulation& s) {
        if(state(s).winner || !config_.systems.enabled)return;
        tickOrdinaryProjectiles(s);
    });
    sim.addSystem("actor",[this](Simulation& s){tick(s);});
}
void ActorHost::applyCommand(Simulation& sim,const Command& c) {
    Input input;
    if(c.player==0||c.player>characterCount(sim)||!decodeInput(c,input))return;
    auto p=character(sim,c.player);
    if(input.sequence==0||input.sequence<=p.lastSequence||input.viewTick>sim.tick()||
       (p.lastSequence&&p.lastInputTick==sim.tick()))return;
    p.input=input;p.lastSequence=input.sequence;p.lastInputTick=sim.tick();
}
void ActorHost::shoot(Simulation& sim,CharacterRef p) {
    auto& s=state(sim);
    uint32_t earliest=sim.tick()>config_.rewindTicks?sim.tick()-config_.rewindTicks:0;
    uint32_t requested=std::clamp(p.input.viewTick,earliest,sim.tick());
    // Lag compensation: the newest history frame not after the shooter's view; none = the live characters.
    std::optional<std::vector<HitPose>> rewind;
    for(auto it=s.history.rbegin();it!=s.history.rend();++it)if(it->tick<=requested){
        rewind.emplace();rewind->reserve(it->characters.size());
        for(const auto& a:it->characters)rewind->push_back(HitPose::of(a));
        break;
    }
    auto eye=p.pos+Vec3{{},stanceHeight(p,config_.motor)-Fixed::fromRatio(1,10),{}};
    const auto& weapon=config_.weapons[p.weaponIndex];
    lib::mechanics::commitAcceptedShot(p.ammo,p.nextFire,sim.tick(),weapon.interval,&p.shots);
    for(uint32_t pellet=0;pellet<weapon.pellets;++pellet) {
        auto yaw=p.yaw,pitch=p.pitch;
        if(config_.systems.enabled) {
            Fixed spread=weapon.hipSpread*((p.aiming)?Fixed::fromRatio(1,4):1_fx);
            if(p.velocity.xz().lengthSq()>1_fx)spread+=weapon.moveSpread;
            if(p.attachments[p.weaponIndex]&1)spread=spread/2;
            if(weapon.pellets>1)spread+=weapon.spread;
            yaw+=(sim.rng().unit()*2-1_fx)*spread;pitch+=(sim.rng().unit()*2-1_fx)*spread+p.recoil;
        }else if(weapon.pellets>1) {yaw+=weapon.spread*int32_t(int(pellet%3)-1);pitch+=weapon.spread*int32_t(int(pellet/3)-1);}
        pitch=fxClamp(pitch,-Fixed::fromRatio(24,100),Fixed::fromRatio(24,100));
        if((config_.systems.enabled||weapon.flight.enabled)&&weapon.bulletSpeed>Fixed{}) {
            if(sim.world().countExisting<Projectile>()<kMaxProjectiles){
                Projectile shot{ s.nextObject++,sim.tick()+config_.tickRate*20,p.slot,0,eye,direction(yaw,pitch)*weapon.bulletSpeed,weapon.damage,weapon.range,p.weaponIndex,weapon.penetration };
                if(weapon.flight.enabled){
                    shot.programSpeed=weapon.bulletSpeed;
                    projectile::initialize(shot.program,shot.programSpeed,weapon.flight,direction(yaw,pitch));
                    shot.program.distanceLeft=fxMin(shot.program.distanceLeft,weapon.range);
                    shot.velocity=shot.program.direction*shot.programSpeed+Vec3{{},shot.program.verticalSpeed,{}};
                }
                spawnProjectile(sim,std::move(shot));
            }
        }else traceShot(sim,p.slot,eye,direction(yaw,pitch)*weapon.range,weapon.damage,weapon,rewind?&*rewind:nullptr);
    }
    if(config_.systems.enabled)p.recoil=fxMin(Fixed::fromRatio(1,20),p.recoil+weapon.recoil*((p.attachments[p.weaponIndex]&2)?Fixed::fromRatio(3,4):1_fx));
}
// ---------------------------------------------------------------- tick
void ActorHost::tick(Simulation& sim) {
    auto& s=state(sim);if(s.winner)return;
    if(caveNavigation_)
        (void)nav::repathCaveActorRoutes(sim,*caveNavigation_,8);
    // Programmed and ordinary projectiles have already advanced in the preceding stages.
    if(config_.systems.enabled)tickSystems(sim);
    tickRoyale(sim);
    if(s.winner)return;
    s.history.push_back({sim.tick(),snapshots(sim)});
    while(s.history.size()>size_t(config_.rewindTicks)+1)s.history.pop_front();
    const World& view=sim.world();
    const size_t count=characterCount(sim);
    for(size_t index=1;index<=count;++index) {
        auto a=character(sim,uint8_t(index));
        if(a.health<=0) {
            if(config_.mode=="deathmatch"&&sim.tick()>=a.respawnAt) {
                CharacterSnapshot fresh;fresh.slot=a.slot;fresh.team=a.team;fresh.kills=a.kills;fresh.lastSequence=a.lastSequence;
                fresh.armor=config_.systems.startArmor;fresh.money=config_.systems.startMoney;
                fresh.life=a.life+1;fresh.pos=spawns_[a.slot-1];equipInitial(fresh);
                assign(a,fresh);
            } else {skydive(sim,a);continue;}   // a body in the air keeps falling (battle royale)
        }
        if(skydive(sim,a))continue;
        // Race bots drive through the race session (tickRace); they never run the on-foot bot.
        const bool footBot=bots_[a.slot-1]&&a.lastSequence==0&&!race_;
        if(footBot&&thinkBot(sim,a)){}
        else if(footBot) {
            std::vector<Vec3> clouds;
            if(config_.systems.enabled)for(const Entity e:projectileEntities(sim)){const auto& cloud=*view.tryGetConst<Projectile>(e);if(cloud.kind==4)clouds.push_back(cloud.pos);}
            uint8_t target=0;Vec3 targetPos{};Fixed nearest=config_.weapons[a.weaponIndex].range*config_.weapons[a.weaponIndex].range;
            for(size_t k=1;k<=count;++k){
                const auto candidate=character(sim,uint8_t(k));
                if(candidate.health>0&&!candidate.downed&&candidate.team!=a.team) {
                    auto distance=(candidate.pos-a.pos).lengthSq();if(distance<nearest){
                        auto eye=a.pos+Vec3{{},1_fx,{}};auto delta=candidate.pos+Vec3{{},1_fx,{}}-eye;bool visible=!collision_.cast(eye,delta).hit;
                        for(const auto& cloud:clouds){auto radius=config_.systems.smokeRadius;Fixed fraction;Vec3 normal;if(world::intersect(eye,delta,{cloud-Vec3{radius,radius,radius},cloud+Vec3{radius,radius,radius}},fraction,normal))visible=false;}
                        if(visible){nearest=distance;target=candidate.slot;targetPos=candidate.pos;}
                    }
                }
            }
            a.input={};a.lastInputTick=sim.tick();a.input.viewTick=sim.tick();
            if(target) {
                auto delta=targetPos-a.pos;
                auto yaw=fxAtan2Turns(delta.x,delta.z);if(yaw<Fixed{})yaw+=1_fx;
                a.input.yaw=yaw;a.input.pitch=fxAtan2Turns(delta.y,delta.xz().length());
                a.input.forward=nearest>100_fx?700:0;a.input.right=((sim.tick()/90+a.slot)%2)?350:-350;
                a.input.buttons=a.ammo?((config_.weapons[a.weaponIndex].automatic||(!a.fireHeld&&sim.tick()>=a.nextFire))?Fire:0u):Reload;
            }
            steerRoyaleBot(sim,a);
        }
        Input input=a.input;
        if(sim.tick()-a.lastInputTick>config_.inputTimeout){input.forward=input.right=0;input.buttons=0;input.action=Action::None;}
        if(caveNavigation_&&!a.vehicle){
            // Apply route steering AFTER stale network input is cleared;
            // the existing moveActor motor alone owns character motion.
            (void)nav::steerCaveActorMotor(
                sim.world(),a.entity,input,*caveNavigation_,collision_);
        }
        if(config_.systems.enabled)actorSystems(sim,a,input);
        if(!a.vehicle){
            if(config_.locomotion)moveActorModes(a,*sim.world().tryGet<LocomotionBody>(a.entity),input,config_.motor,*config_.locomotion,collision_,sim.dt(),config_.worldLimit);
            else moveActor(a,input,config_.motor,collision_,sim.dt(),config_.worldLimit);
        }
        if(config_.melee.enabled)tickMelee(sim,a,input);
        // Jump is an edge, movement/fire are held; loss of input eventually releases them.
        a.input.buttons&=~uint32_t(Jump);
        auto selection=(input.buttons&WeaponMask)>>5;
        if(selection&&selection<=config_.weapons.size()&&selection-1!=a.weaponIndex&&(a.owned&(1u<<(selection-1)))) {
            a.storedAmmo[a.weaponIndex]=a.ammo;a.storedReserve[a.weaponIndex]=a.reserve;
            a.weaponIndex=uint8_t(selection-1);a.ammo=a.storedAmmo[a.weaponIndex];a.reserve=a.storedReserve[a.weaponIndex];
            a.reloadUntil=0;a.switchUntil=sim.tick()+config_.weapons[a.weaponIndex].switchTicks;
        }
        a.input.buttons&=~uint32_t(WeaponMask);
        const auto& weapon=config_.weapons[a.weaponIndex];
        const int capacity=weapon.magazine+((a.attachments[a.weaponIndex]&4)?weapon.magazine/2:0);
        // Generic ECS hosts and the FPS host share the same reload algorithm.
        lib::mechanics::WeaponStateComponent reloadWeapon{a.ammo,a.nextFire};
        lib::mechanics::WeaponReloadComponent reloadState{a.reserve,capacity,weapon.reloadTicks,a.reloadUntil};
        lib::mechanics::stepWeaponReload(reloadWeapon,reloadState,sim.tick(),(input.buttons&Reload)!=0);
        a.ammo=reloadWeapon.ammo;
        a.reserve=reloadState.reserve;
        a.reloadUntil=reloadState.finishesAt;
        bool trigger=(input.buttons&Fire)!=0;
        if(trigger&&!a.downed&&(!a.vehicle||canFireFromVehicle(sim,a))&&a.activeAction==Action::None&&!a.blockHeld&&sim.tick()>=a.staggerUntil&&(a.owned&(1u<<a.weaponIndex))&&(weapon.automatic||!a.fireHeld)&&!a.reloadUntil&&a.ammo>0&&sim.tick()>=a.nextFire&&sim.tick()>=a.switchUntil)shoot(sim,a);
        a.fireHeld=trigger;
        if(a.pos.y< -100_fx){a.health=0;a.respawnAt=sim.tick()+config_.respawnTicks;}
        if(config_.mode=="battleRoyale"&&!config_.royale&&sim.tick()>=config_.zoneStart&&sim.tick()%config_.tickRate==0) {
            auto t=Fixed::fromRatio(std::min(sim.tick()-config_.zoneStart,config_.zoneDuration),config_.zoneDuration);
            auto radius=config_.zoneRadius+(config_.zoneEndRadius-config_.zoneRadius)*t;
            if((a.pos-config_.zoneCenter).xz().lengthSq()>radius*radius)a.health=std::max(0,a.health-config_.zoneDamage);
        }
    }
    if(race_){tickRace(sim);return;}
    tickVehicles(sim);
    if(config_.mode=="bomb")return;
    if(config_.royale){settleRoyale(sim);if(s.winner)return;}
    else if(config_.mode!="deathmatch") {
        std::set<uint8_t> teams;forEachCharacter(sim,[&](CharacterRef a){if(a.health>0&&!a.downed)teams.insert(a.team);});
        if(teams.size()<=1)s.winner=teams.empty()?-1:*teams.begin();
    }
    if(sim.tick()+1>=config_.roundTicks&&!s.winner) {
        if(config_.mode=="deathmatch") {
            std::map<uint8_t,int> scores;forEachCharacter(sim,[&](CharacterRef a){scores[a.team]+=a.kills;});
            int best=-1,winner=-1;for(auto [team,score]:scores)if(score>best){best=score;winner=team;}else if(score==best)winner=-1;
            s.winner=winner;
        } else s.winner=-1;
    }
}
// ---------------------------------------------------------------- checksum and save
void ActorHost::hashState(Simulation& sim,Hasher& hash) {
    // Read-only: const World access never detaches copy-on-write snapshot pools. The byte layout is the one of
    // the former FPS state stream (characters / projectiles / loot / vehicles in slot / id order), so match
    // checksums do not depend on the storage.
    const Simulation& view=sim;const World& w=view.world();
    const State* stored=w.resourceConst<State>();
    if(!stored)throw std::logic_error("FPS State missing during checksum");
    State& s=const_cast<State&>(*stored);   // Archive writes only read their arguments
    auto characters=snapshots(view);auto shots=projectiles(view);auto items=loot(view);auto cars=vehicles(view);
    hash.u64(contentHash_);
    {
        std::string bytes;Archive a(&bytes);
        a(s.round,s.winner,characters,s.nextObject,shots,items,cars,s.match);
        a.count(s.history.size());
        for(auto& frame:s.history)a(frame.tick,frame.characters);
        hash.str(bytes);
    }
    if(config_.melee.enabled){std::string extra;Archive m(&extra);for(auto& c:characters)ioMelee(m,c);hash.str(extra);}
    for(auto& p:shots)if(p.program.enabled){std::string extra;Archive e(&extra);e(p.id,p.program,p.programSpeed,p.programTarget);hash.str(extra);}
    if(s.royale.enabled){std::string extra;Archive r(&extra);royaleIo(r,s.royale);hash.str(extra);}
    for(size_t slot=1;slot<=characters.size();++slot)if(const auto* g=gear(view,uint8_t(slot)))hash.u64(lib::systems::gearHash(*g));
    if(w.countExisting<vehicles::VehicleDrive>())vehicles::hashVehicles(w,hash,false);   // typed vehicles (the shared vehicle system)
    if(race_)race_->hash(w,hash);
    if(botHost_)for(const Entity e:w.resourceConst<CharacterIndex>()->bySlot)if(const auto* m=w.tryGetConst<BotMemory>(e))hash.str(m->bytes);
    if(config_.locomotion)for(const Entity e:w.resourceConst<CharacterIndex>()->bySlot)if(const auto* l=w.tryGetConst<LocomotionBody>(e))hashLocomotionBody(hash,*l);
}
// FPS save version 10: written from the components (characters in slot order, dynamic objects in id order,
// each with its entity handle); older versions are not readable. Typed vehicles write the shared VehicleDrive record
// (engine/vehicles); maps with a race (map "race") append the shared RaceState; other maps write no race bytes.
bool ActorHost::serializeState(Simulation& sim,Archive& archive) {
    uint32_t version=10;uint64_t content=contentHash_;archive(version,content);
    if(version!=10||content!=contentHash_)return false;
    struct Character { Entity entity;CharacterSnapshot value; };
    struct Object { Entity entity; };
    struct SavedProjectile : Object { Projectile value; };
    struct SavedLoot : Object { Loot value; };
    struct SavedVehicle : Object { Vehicle value;bool typed=false;vehicles::VehicleDrive drive; };
    State s;std::vector<Character> characters;std::vector<SavedProjectile> shots;std::vector<SavedLoot> items;std::vector<SavedVehicle> cars;
    std::vector<lib::systems::GearLoadout> gears;std::vector<std::string> bots;
    std::vector<LocomotionBody> locos;   // rules `locomotion` only
    vehicles::RaceState race;   // race maps only
    World& world=sim.world();
    if(archive.writing()){
        const Simulation& view=sim;const World& w=view.world();
        s=state(view);
        if(race_)if(const auto* r=vehicles::VehicleRace::state(w))race=*r;
        for(size_t slot=1;slot<=characterCount(view);++slot){
            characters.push_back({characterEntity(view,uint8_t(slot)),snapshot(view,uint8_t(slot))});
            if(const auto* g=gear(view,uint8_t(slot)))gears.push_back(*g);
            if(const auto* m=w.tryGetConst<BotMemory>(characters.back().entity))bots.push_back(m->bytes);
            if(const auto* l=w.tryGetConst<LocomotionBody>(characters.back().entity))locos.push_back(*l);
        }
        for(const Entity e:projectileEntities(view))shots.push_back({{e},*w.tryGetConst<Projectile>(e)});
        for(const Entity e:lootEntities(view))items.push_back({{e},*w.tryGetConst<Loot>(e)});
        for(const Entity e:vehicleEntities(view)){
            SavedVehicle v{{e},*w.tryGetConst<Vehicle>(e),false,{}};
            if(const auto* d=w.tryGetConst<vehicles::VehicleDrive>(e)){v.typed=true;v.drive=*d;}
            cars.push_back(std::move(v));
        }
    }
    archive(s.round,s.winner,s.nextObject,s.match);
    auto sized=[&](auto& list,size_t limit){
        uint32_t n=archive.count(list.size());
        if(n>limit){archive.fail();return false;}
        if(!archive.writing())list.resize(n);
        return true;
    };
    if(!sized(characters,256))return false;
    for(auto& c:characters){archive(c.entity);io(archive,c.value);ioMelee(archive,c.value);}
    if(!sized(shots,kMaxProjectiles))return false;
    for(auto& p:shots){archive(p.entity);io(archive,p.value);archive(p.value.program,p.value.programSpeed,p.value.programTarget);}
    if(!sized(items,kMaxLoot))return false;
    for(auto& l:items){archive(l.entity);io(archive,l.value);}
    if(!sized(cars,kMaxVehicles))return false;
    for(auto& v:cars){
        archive(v.entity);io(archive,v.value);archive(v.typed);
        if(v.typed)vehicles::io(archive,v.drive);
    }
    if(!sized(s.history,121))return false;
    for(auto& frame:s.history){
        archive(frame.tick);
        if(!sized(frame.characters,256))return false;
        for(auto& c:frame.characters){io(archive,c);ioMelee(archive,c);}
    }
    royaleIo(archive,s.royale);
    if(!sized(gears,256))return false;
    for(auto& g:gears)lib::systems::io(archive,g);
    archive(bots);
    if(race_)vehicles::VehicleRace::io(archive,race);   // only maps with a race carry the race block
    if(config_.locomotion){if(!sized(locos,256))return false;for(auto& l:locos)io(archive,l);}   // only rules `locomotion` append it
    if(!archive.ok())return false;
    // Validation (also when writing: a corrupt in-memory state must not produce a save).
    const size_t n=spawns_.size();
    if(characters.size()!=n||bots.size()!=(botHost_?n:0)||gears.size()!=(config_.gear.empty()?0:n)||locos.size()!=(config_.locomotion?n:0))return false;
    for(const auto& l:locos)if(!valid(l))return false;
    for(const auto& g:gears)if(!lib::systems::valid(config_.gear,g))return false;
    if(s.royale.enabled!=bool(config_.royale))return false;
    if(race_){
        std::vector<Entity> typed;for(const auto& c:cars)if(c.typed)typed.push_back(c.entity);
        if(!validRace(race,typed))return false;
    }
    if(s.royale.enabled){
        using namespace lib::mechanics;const auto& r=s.royale;
        if(!valid(r.zone)||!valid(r.bombardment)||!valid(r.drops)||!valid(r.flight)||!valid(r.standings)||r.lastAttacker.size()!=n||r.lastWeapon.size()!=n||
           (r.divers.size()!=n&&!(r.divers.empty()&&!config_.royale->flight))||r.standings.players.size()!=n)return false;
        for(const auto& d:r.divers)if(!valid(d))return false;
        for(auto x:r.lastAttacker)if(x>n)return false;
    }
    auto validCharacter=[&](const CharacterSnapshot& actor){
        if(actor.weaponIndex>=config_.weapons.size()||actor.slot==0||actor.slot>n||actor.bag.size()>24||actor.actionTarget>n||uint32_t(actor.activeAction)>uint32_t(Action::Vault)||actor.owned>15||actor.armor<0||actor.armor>100||actor.health<0||actor.health>100||actor.money<0||actor.money>16000)return false;
        for(auto item:actor.bag)if(item.type<1||item.type>9||item.count<1||item.count>20)return false;
        const auto& m=actor.melee;
        if(actor.lockTarget>n||m.attack<0||m.attack>1||m.queued<0||m.queued>1||m.started>m.lastTick||m.queuedAt>m.lastTick||m.hitTargets.size()>n||
           !std::is_sorted(m.hitTargets.begin(),m.hitTargets.end())||std::adjacent_find(m.hitTargets.begin(),m.hitTargets.end())!=m.hitTargets.end()||
           (!m.hitTargets.empty()&&(m.hitTargets.front()<1||m.hitTargets.back()>int64_t(n))))return false;
        return true;
    };
    for(size_t i=0;i<characters.size();++i)if(characters[i].value.slot!=i+1||!validCharacter(characters[i].value))return false;
    for(const auto& frame:s.history)for(const auto& actor:frame.characters)if(!validCharacter(actor))return false;
    uint32_t previous=0;
    for(const auto& item:shots){const auto& p=item.value;
        if(p.id<=previous||p.id>=s.nextObject||p.kind>4||!p.owner||p.owner>n||p.weapon>=config_.weapons.size()||p.piercedCount>8)return false;
        if(!projectile::valid(p.program,p.programSpeed,config_.weapons[p.weapon].flight))return false;
        previous=p.id;}
    previous=0;
    for(const auto& item:items){if(item.value.id<=previous||item.value.id>=s.nextObject)return false;previous=item.value.id;}
    previous=0;
    for(const auto& item:cars){const auto& v=item.value;
        if(v.id<=previous||v.id>=s.nextObject||v.driver>n||v.health<0||v.health>500)return false;
        previous=v.id;
        if(!item.typed)continue;
        const auto& r=item.drive;
        if(!config_.vehicleTypes||!config_.vehicleTypes->vehicles.contains(r.type)||!vehicles::valid(r))return false;
        if(r.player>n||(r.driver.valid()&&(!r.player||r.driver!=characters[r.player-1].entity)))return false;
        for(auto [seat,actor]:r.seats.occupants){(void)seat;if(actor<1||actor>int64_t(n))return false;}
    }
    // Entity handles: alive in the (restored) allocator and distinct.
    std::vector<Entity> handles;
    for(const auto& c:characters)handles.push_back(c.entity);
    for(const auto& o:shots)handles.push_back(o.entity);
    for(const auto& o:items)handles.push_back(o.entity);
    for(const auto& o:cars)handles.push_back(o.entity);
    for(const Entity e:handles)if(!world.alive(e))return false;
    std::sort(handles.begin(),handles.end(),[](Entity a,Entity b){return a.index<b.index;});
    if(std::adjacent_find(handles.begin(),handles.end(),[](Entity a,Entity b){return a.index==b.index;})!=handles.end())return false;
    if(archive.writing())return true;
    // Load: replace every FPS component pool with the saved contents.
    world.pool<CharacterId>().clear();world.pool<CharacterBody>().clear();world.pool<CharacterHealth>().clear();
    world.pool<CharacterWeapons>().clear();world.pool<CharacterInventory>().clear();world.pool<CharacterControl>().clear();
    world.pool<CharacterAction>().clear();world.pool<CharacterMelee>().clear();world.pool<lib::systems::GearLoadout>().clear();
    world.pool<BotMemory>().clear();world.pool<Projectile>().clear();world.pool<Loot>().clear();world.pool<Vehicle>().clear();
    world.pool<vehicles::VehicleDrive>().clear();world.pool<LocomotionBody>().clear();
    CharacterIndex index;
    for(size_t i=0;i<characters.size();++i){
        const Entity e=characters[i].entity;addCharacterComponents(world,e,characters[i].value);index.bySlot.push_back(e);
        if(!gears.empty())world.add<lib::systems::GearLoadout>(e,gears[i]);
        if(!bots.empty())world.add<BotMemory>(e,BotMemory{bots[i]});
        if(!locos.empty())world.add<LocomotionBody>(e,locos[i]);
    }
    for(auto& o:shots)world.add<Projectile>(o.entity,std::move(o.value));
    for(auto& o:items)world.add<Loot>(o.entity,std::move(o.value));
    for(auto& o:cars){world.add<Vehicle>(o.entity,o.value);if(o.typed)world.add<vehicles::VehicleDrive>(o.entity,std::move(o.drive));}
    if(race_)world.setResource<vehicles::RaceState>(std::move(race));
    world.setResource<CharacterIndex>(std::move(index));
    world.setResource<State>(std::move(s));
    return true;
}
}
