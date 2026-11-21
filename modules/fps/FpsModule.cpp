#include "FpsModule.h"
#include "libraries/include/aiverselib/genre/EcsWeaponLaunchSystems.h"
#include "engine/experiments/EcsExperiments.h"
#include <algorithm>
#include <set>
#include "engine/core/FixedMath.h"
#include "libraries/include/aiverselib/genre/EcsTimedMechanics.h"
#include "engine/world/OrientedBox.h"
#include <stdexcept>

namespace aiverse::fps {
namespace {
int64_t integer(const Json& j,const char* key,int64_t fallback,int64_t lo,int64_t hi) {
    if(j.has(key)&&(!j[key].isNumber()||j[key].fx().raw%Fixed::kOne))throw JsonError(std::string("fps.")+key+": expected integer");
    auto v=j.getInt(key,fallback);if(v<lo||v>hi)throw JsonError(std::string("fps.")+key+": outside supported range");return v;
}
Fixed number(const Json& j,const char* key,Fixed fallback,Fixed lo,Fixed hi) {
    if(j.has(key)&&!j[key].isNumber())throw JsonError(std::string("fps.")+key+": expected number");
    auto v=j.getFx(key,fallback);if(v<lo||v>hi)throw JsonError(std::string("fps.")+key+": outside supported range");return v;
}
Vec3 vector(const Json& j) {
    if(!j.isArray()||j.size()!=3)throw JsonError("fps: expected three coordinates");
    Vec3 v;Fixed* p[]={&v.x,&v.y,&v.z};
    for(int i=0;i<3;++i){if(!j[size_t(i)].isNumber())throw JsonError("fps: non-numeric coordinate");*p[i]=j[size_t(i)].fx();if(fxAbs(*p[i])>100000_fx)throw JsonError("fps: coordinate exceeds 100 km");}return v;
}
void meleeIo(Archive& a,Actor& p) {
    a(p.melee.attack,p.melee.queued,p.melee.started,p.melee.queuedAt,p.melee.lastTick,p.melee.initialized,p.melee.hitTargets,
      p.parryUntil,p.staggerUntil,p.lockTarget,p.blockHeld,p.meleeHasTip,p.meleeTip);
}
void royaleIo(Archive& a,RoyaleRun& r) {
    using lib::genre::io;
    a(r.enabled);if(!r.enabled)return;
    io(a,r.zone);io(a,r.bombardment);io(a,r.drops);io(a,r.flight);
    uint32_t n=a.count(r.divers.size());if(n>256){a.fail();return;}if(!a.writing())r.divers.resize(n);for(auto& d:r.divers)io(a,d);
    io(a,r.standings);a(r.lastAttacker,r.lastWeapon);
    uint32_t feed=a.count(r.feed.size());if(feed>6){a.fail();return;}if(!a.writing())r.feed.resize(feed);for(auto& e:r.feed)lib::genre::io(a,e);
}
}
void ioVehicleRuns(Archive&,std::vector<VehicleRun>&);
void raceIo(Archive&,RaceRun&);   // FpsRace.cpp
namespace {
void stateIo(Archive& a,State& s,bool programs=false,bool melee=false) {
    a(s.round,s.winner,s.actors,s.nextObject,s.projectiles,s.loot,s.vehicles,s.match);
    uint32_t n=a.count(s.history.size());
    if(n>121){a.fail();return;}
    if(!a.writing())s.history.resize(n);
    for(auto& frame:s.history)a(frame.tick,frame.actors);
    if(programs)for(auto& p:s.projectiles)a(p.program,p.programSpeed,p.programTarget);
    if(melee){for(auto& actor:s.actors)meleeIo(a,actor);for(auto& frame:s.history)for(auto& actor:frame.actors)meleeIo(a,actor);}
}
}
Config readConfig(const Json& root) {
    const auto& j=root["fps"];if(!j.isObject())throw JsonError("rules.fps: expected object");
    Config c;
    c.tickRate=uint32_t(integer(j,"tickRate",60,20,120));
    c.ecsAuthority=j.getBool("ecsAuthority",false);
    c.snapshotRate=uint32_t(integer(j,"snapshotRate",20,1,c.tickRate));
    c.maxPlayers=uint32_t(integer(j,"maxPlayers",100,1,200));
    c.rewindTicks=uint32_t(integer(j,"rewindTicks",12,0,120));
    c.inputTimeout=uint32_t(integer(j,"inputTimeout",15,1,120));
    c.interestRadius=number(j,"interestRadius",250_fx,10_fx,10000_fx);
    c.worldLimit=number(j,"worldLimit",100000_fx,10_fx,100000_fx);
    c.mode=j.getStr("mode","deathmatch");
    if(c.mode!="deathmatch"&&c.mode!="elimination"&&c.mode!="battleRoyale"&&c.mode!="bomb")throw JsonError("fps.mode: unknown mode");
    c.friendlyFire=j.getBool("friendlyFire",false);
    c.roundTicks=uint32_t(integer(j,"roundTicks",7200,1,10000000));
    c.respawnTicks=uint32_t(integer(j,"respawnTicks",180,1,100000));
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
        if(!w.isNull()&&!w.isObject())throw JsonError("fps.weapon: expected object");
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
            v.flight=projectile::parseProjectileFlight(w["flight"],"fps.weapon.flight");
            if(v.bulletSpeed<=Fixed{})throw JsonError("fps.weapon.flight requires bulletSpeed > 0");
            if(!w["flight"].has("gravity"))v.flight.gravity=v.bulletGravity;
        }
        v.penetration=number(w,"penetration",Fixed{},Fixed{},10_fx);
        v.headMultiplier=number(w,"headMultiplier",2_fx,1_fx,5_fx);
        v.limbMultiplier=number(w,"limbMultiplier",Fixed::fromRatio(3,4),Fixed::fromRatio(1,10),1_fx);
        v.price=int32_t(integer(w,"price",600,0,16000));
        v.pellets=uint32_t(integer(w,"pellets",1,1,16));
        v.automatic=w.getBool("automatic",true);
        v.nameKey=w.getStr("nameKey","fps.weapon.rifle");
        return v;
    };
    if(j.has("weapons")) {
        if(!j["weapons"].isArray()||j["weapons"].size()<1||j["weapons"].size()>4)throw JsonError("fps.weapons: expected 1..4 weapons");
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
        if(c.mode!="battleRoyale")throw JsonError("fps.royale: requires fps.mode \"battleRoyale\"");
        c.royale=lib::genre::readRoyale(j["royale"],"fps.royale");
    }
    if(j.has("crate")){
        if(!j["crate"].isArray()||j["crate"].size()>16)throw JsonError("fps.crate: expected at most 16 items");
        for(const auto& item:j["crate"].items()){
            auto type=integer(item,"type",0,1,255),count=integer(item,"count",1,1,20);
            if(!((type>=1&&type<=9)||(type>=100&&type<100+int64_t(c.weapons.size()))||type>=200))throw JsonError("fps.crate: invalid item type "+std::to_string(type));
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
    if(c.mode=="bomb"&&!mcfg.enabled)throw JsonError("fps bomb mode requires mechanics.enabled");
    if(j.has("gear")){
        if(!c.systems.enabled)throw JsonError("fps.gear: requires mechanics.enabled");
        c.gear=lib::systems::readGearCatalog(j["gear"],"fps.gear");
        for(const auto& [id,g]:c.gear)if(id>55)throw JsonError("fps.gear: ids must be 1..55 (loot type 200 + id)");
    }
    for(auto item:c.crate)if(item.type>=200&&!c.gear.contains(item.type-200))throw JsonError("fps.crate: unknown gear "+std::to_string(item.type-200));
    if(j.has("bots")){if(!j["bots"].isObject())throw JsonError("fps.bots: expected object");c.bots=j["bots"];}
    if(j.has("vehicleTypes")){
        if(!c.systems.enabled)throw JsonError("fps.vehicleTypes: requires mechanics.enabled");
        c.vehicleTypes=lib::genre::readVehicleDefinitions(j["vehicleTypes"]);
        for(const auto& [id,d]:c.vehicleTypes->vehicles)if(d.seats.seats.empty())throw JsonError("fps.vehicleTypes: vehicle "+std::to_string(id)+" needs seats");
    }
    const auto& melee=j["melee"];auto& mel=c.melee;
    if(!melee.isNull()&&!melee.isObject())throw JsonError("fps.melee: expected object");
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
void Module::equipInitial(Actor& a) const {
    a.weaponIndex=0;a.owned=config_.mode=="bomb"?1:uint8_t((1u<<config_.weapons.size())-1);
    for(size_t i=0;i<config_.weapons.size();++i){a.storedAmmo[i]=config_.weapons[i].magazine;a.storedReserve[i]=config_.weapons[i].reserve;}
    a.ammo=a.storedAmmo[0];a.reserve=a.storedReserve[0];
    if(config_.systems.enabled)a.bag={{1,2},{3,2},{4,2},{5,2}};
}
Entity Module::actorEntity(Simulation& sim, uint8_t slot) const {
    const auto* index = sim.world().resource<ActorEntityIndex>();
    if (!index || slot == 0 || size_t(slot) > index->bySlot.size())
        return kNullEntity;
    const Entity entity = index->bySlot[size_t(slot - 1)];
    return sim.world().alive(entity) ? entity : kNullEntity;
}

void Module::syncActorEntities(Simulation& sim) const {
    World& world = sim.world();
    auto* index = world.resource<ActorEntityIndex>();
    auto* current = world.resource<State>();
    if (!index || !current) return;
    // Save v9 predates these ECS pools. Its entity allocator can legitimately
    // contain no actor identities. Recreate derived identities deterministically
    // after loading such a save, without modifying the saved Actor/command data.
    if (index->bySlot.size() < current->actors.size())
        index->bySlot.resize(current->actors.size(), kNullEntity);
    for (const Actor& actor : current->actors) {
        if (actor.slot == 0 || size_t(actor.slot) > index->bySlot.size()) continue;
        Entity& entity = index->bySlot[size_t(actor.slot - 1)];
        if (!world.alive(entity)) entity = world.create();
        // A legacy save can restore an allocator that predates these pools.
        // Do not retain stale dynamic ECS components at a recycled index.
        if (world.has<ProjectileEntity>(entity)) world.remove<ProjectileEntity>(entity);
        if (world.has<LootEntity>(entity)) world.remove<LootEntity>(entity);
        if (world.has<VehicleEntity>(entity)) world.remove<VehicleEntity>(entity);
        if (world.has<ProjectileRuntime>(entity)) world.remove<ProjectileRuntime>(entity);
        if (world.has<LootRuntime>(entity)) world.remove<LootRuntime>(entity);
        if (world.has<VehicleRuntime>(entity)) world.remove<VehicleRuntime>(entity);
        if (!world.has<ActorEntityIdentity>(entity))
            world.add<ActorEntityIdentity>(entity);
        if (!world.has<ActorEntityTransform>(entity))
            world.add<ActorEntityTransform>(entity);
        if (!world.has<ActorEntityVitals>(entity))
            world.add<ActorEntityVitals>(entity);
        if (config_.ecsAuthority) {
            if (!world.has<ActorRuntime>(entity))
                world.add<ActorRuntime>(entity);
            // The old FPS algorithms still use State::actors as a canonical
            // ordered tick workspace; commit the complete result to ECS.
            *world.tryGet<ActorRuntime>(entity) = ActorRuntime{actor};
        }
        // With ECS authority enabled, Editor projections are derived from
        // the committed ActorRuntime, never from a second gameplay owner.
        const Actor& projected = config_.ecsAuthority
            ? world.tryGetConst<ActorRuntime>(entity)->value : actor;
        if (auto* identity = world.tryGet<ActorEntityIdentity>(entity))
            *identity = {projected.slot, projected.team, projected.life};
        if (auto* transform = world.tryGet<ActorEntityTransform>(entity))
            *transform = {projected.pos, projected.velocity, projected.yaw, projected.pitch};
        if (auto* vitals = world.tryGet<ActorEntityVitals>(entity))
            *vitals = {projected.health, projected.armor, projected.ammo, projected.reserve,
                       projected.downed, projected.crouched, projected.prone};
    }
}

void Module::hydrateActorsFromEcs(Simulation& sim) const {
    if (!config_.ecsAuthority) return;
    World& world=sim.world();
    auto* current=world.resource<State>();
    const auto* index=static_cast<const World&>(world).resourceConst<ActorEntityIndex>();
    if (!current || !index || index->bySlot.size()!=current->actors.size())
        throw std::logic_error("FPS ECS authority index is missing or incomplete");
    for(size_t i=0;i<index->bySlot.size();++i) {
        const Entity e=index->bySlot[i];
        const ActorRuntime* actor=world.tryGetConst<ActorRuntime>(e);
        if(!world.alive(e)||!actor||actor->value.slot!=i+1)
            throw std::logic_error("FPS ECS authoritative ActorRuntime is missing");
        current->actors[i]=actor->value;
    }
}

// Preserve the old public native symbol for import from legacy v9 saves and
// native tooling. The end-of-tick ECS mirror explicitly chooses the separate
// canonical ProjectileRuntime -> State projection overload.
void Module::syncDynamicEntities(Simulation& sim) const {
    syncDynamicEntities(sim,false);
}

void Module::syncDynamicEntities(Simulation& sim,bool projectProjectilesFromEcs) const {
    World& world = sim.world();
    State* current = world.resource<State>();
    auto* index = world.resource<DynamicEntityIndex>();
    if (!current || !index) return;
    std::set<uint64_t> observed;
    auto ensure = [&](uint8_t kind, uint32_t id) {
        const uint64_t key = (uint64_t(kind) << 32) | uint64_t(id);
        observed.insert(key);
        auto [it, inserted] = index->byKey.emplace(key, kNullEntity);
        (void)inserted;
        if (!world.alive(it->second)) it->second = world.create();
        const Entity entity = it->second;
        // World::restoreEntityState restores generations, not component pools.
        // Strip stale components before reusing a source ID on legacy saves.
        if (world.has<ActorEntityIdentity>(entity)) world.remove<ActorEntityIdentity>(entity);
        if (world.has<ActorEntityTransform>(entity)) world.remove<ActorEntityTransform>(entity);
        if (world.has<ActorEntityVitals>(entity)) world.remove<ActorEntityVitals>(entity);
        if (world.has<ActorRuntime>(entity)) world.remove<ActorRuntime>(entity);
        if (kind != 1 && world.has<ProjectileEntity>(entity))
            world.remove<ProjectileEntity>(entity);
        if (kind != 2 && world.has<LootEntity>(entity))
            world.remove<LootEntity>(entity);
        if (kind != 3 && world.has<VehicleEntity>(entity))
            world.remove<VehicleEntity>(entity);
        if (kind != 1 && world.has<ProjectileRuntime>(entity)) world.remove<ProjectileRuntime>(entity);
        if (kind != 2 && world.has<LootRuntime>(entity)) world.remove<LootRuntime>(entity);
        if (kind != 3 && world.has<VehicleRuntime>(entity)) world.remove<VehicleRuntime>(entity);
        return entity;
    };
    if(config_.ecsAuthority && projectProjectilesFromEcs) {
        // End-of-tick ECS mirror is a ONE-WAY projection for projectiles.
        // The component is canonical: never overwrite ProjectileRuntime
        // from State::projectiles just because a compatibility consumer
        // retained a stale vector. Setup and v9 deserialize still use the
        // legacy->ECS reconstruction branch below.
        struct RuntimeRef {Entity entity;uint32_t order,id;};
        std::vector<RuntimeRef> ordered;
        std::set<uint32_t> uniqueIds;
        const World& read=world;
        read.eachExistingConst<ProjectileRuntime>(
            [&](Entity entity,const ProjectileRuntime& runtime) {
                if(!read.alive(entity))return;
                if(!runtime.value.id || !uniqueIds.insert(runtime.value.id).second)
                    throw std::logic_error("FPS ECS duplicate/zero projectile ID at mirror");
                ordered.push_back({entity,runtime.order,runtime.value.id});
            });
        std::sort(ordered.begin(),ordered.end(),
            [](const RuntimeRef& a,const RuntimeRef& b) {
                return a.order!=b.order ? a.order<b.order : a.id<b.id;
            });
        std::vector<Projectile> projected;
        projected.reserve(ordered.size());
        for(const RuntimeRef& item:ordered) {
            const auto* runtime=read.tryGetConst<ProjectileRuntime>(item.entity);
            if(!runtime)throw std::logic_error("FPS ECS projectile missing at mirror");
            const Projectile& projectile=runtime->value;
            const uint64_t key=(uint64_t(1)<<32)|projectile.id;
            observed.insert(key);
            auto [it,inserted]=index->byKey.emplace(key,item.entity);
            if(!inserted && world.alive(it->second) && !(it->second==item.entity))
                throw std::logic_error("FPS ECS projectile index conflict at mirror");
            it->second=item.entity;
            if(!world.has<ProjectileEntity>(item.entity))
                world.add<ProjectileEntity>(item.entity);
            *world.tryGet<ProjectileEntity>(item.entity)={
                projectile.id,projectile.owner,projectile.kind,
                projectile.pos,projectile.velocity,projectile.damage
            };
            projected.push_back(projectile);
        }
        current->projectiles=std::move(projected);
    } else {
        // Legacy/default source-of-truth and v9 load migration.
        uint32_t projectileOrder=0;
        for (const Projectile& projectile : current->projectiles) {
            const Entity entity=ensure(1,projectile.id);
            if(config_.ecsAuthority) {
                if(!world.has<ProjectileRuntime>(entity))
                    world.add<ProjectileRuntime>(entity);
                *world.tryGet<ProjectileRuntime>(entity)={projectile,projectileOrder};
            }
            ++projectileOrder;
            if(!world.has<ProjectileEntity>(entity))
                world.add<ProjectileEntity>(entity);
            *world.tryGet<ProjectileEntity>(entity)={
                projectile.id,projectile.owner,projectile.kind,
                projectile.pos,projectile.velocity,projectile.damage
            };
        }
    }
    uint32_t lootOrder=0;
    for (const Loot& loot : current->loot) {
        const Entity entity = ensure(2, loot.id);
        if(config_.ecsAuthority) {
            if(!world.has<LootRuntime>(entity))world.add<LootRuntime>(entity);
            *world.tryGet<LootRuntime>(entity)={loot,lootOrder};
        }
        ++lootOrder;
        if (!world.has<LootEntity>(entity)) world.add<LootEntity>(entity);
        *world.tryGet<LootEntity>(entity) = {
            loot.id, loot.pos, loot.type, loot.count
        };
    }
    uint32_t vehicleOrder=0;
    for (const Vehicle& vehicle : current->vehicles) {
        const Entity entity = ensure(3, vehicle.id);
        if(config_.ecsAuthority) {
            if(!world.has<VehicleRuntime>(entity))world.add<VehicleRuntime>(entity);
            *world.tryGet<VehicleRuntime>(entity)={vehicle,vehicleOrder};
        }
        ++vehicleOrder;
        if (!world.has<VehicleEntity>(entity)) world.add<VehicleEntity>(entity);
        *world.tryGet<VehicleEntity>(entity) = {
            vehicle.id, vehicle.pos, vehicle.velocity,
            vehicle.yaw, vehicle.health, vehicle.driver
        };
    }
    // Retire vanished objects in deterministic source-ID order. Never keep
    // a dead entity in the editor hierarchy across a save/rollback.
    for (auto it = index->byKey.begin(); it != index->byKey.end();) {
        if (!observed.contains(it->first)) {
            if (world.alive(it->second)) world.destroy(it->second);
            it = index->byKey.erase(it);
        } else {
            ++it;
        }
    }
}

void Module::hydrateDynamicsFromEcs(Simulation& sim) const {
    if(!config_.ecsAuthority)return;
    World& world=sim.world();
    auto* current=world.resource<State>();
    if(!current)throw std::logic_error("FPS authoritative dynamic State unavailable");
    // Native code may spawn a full ECS runtime component directly without
    // touching State vectors. Reconcile stable kind/id keys before the tick,
    // so the existing Inspector/Hierarchy projections reuse that Entity.
    auto* index=world.resource<DynamicEntityIndex>();
    if(!index)throw std::logic_error("FPS DynamicEntityIndex missing");
    std::set<uint64_t> seen;
    auto indexRuntime=[&](uint8_t kind,uint32_t id,Entity entity) {
        if(!id)throw std::logic_error("FPS ECS dynamic runtime has zero source ID");
        const uint64_t key=(uint64_t(kind)<<32)|id;
        if(!seen.insert(key).second)
            throw std::logic_error("Duplicate FPS ECS dynamic kind/id");
        auto [it,inserted]=index->byKey.emplace(key,entity);
        if(!inserted) {
            if(!world.alive(it->second))it->second=entity;
            else if(!(it->second==entity))
                throw std::logic_error("Conflicting FPS ECS dynamic Entity for source ID");
        }
    };
    world.eachExistingConst<ProjectileRuntime>([&](Entity e,const ProjectileRuntime& r) {
        if(world.alive(e))indexRuntime(1,r.value.id,e);
    });
    world.eachExistingConst<LootRuntime>([&](Entity e,const LootRuntime& r) {
        if(world.alive(e))indexRuntime(2,r.value.id,e);
    });
    world.eachExistingConst<VehicleRuntime>([&](Entity e,const VehicleRuntime& r) {
        if(world.alive(e))indexRuntime(3,r.value.id,e);
    });
    auto projectiles=std::vector<std::pair<uint32_t,Projectile>>{};
    auto loot=std::vector<std::pair<uint32_t,Loot>>{};
    auto vehicles=std::vector<std::pair<uint32_t,Vehicle>>{};
    world.eachExistingConst<ProjectileRuntime>([&](Entity e,const ProjectileRuntime& r) {
        if(world.alive(e))projectiles.emplace_back(r.order,r.value);
    });
    world.eachExistingConst<LootRuntime>([&](Entity e,const LootRuntime& r) {
        if(world.alive(e))loot.emplace_back(r.order,r.value);
    });
    world.eachExistingConst<VehicleRuntime>([&](Entity e,const VehicleRuntime& r) {
        if(world.alive(e))vehicles.emplace_back(r.order,r.value);
    });
    auto ordered=[](auto& values,auto& target) {
        std::sort(values.begin(),values.end(),[](const auto& a,const auto& b) {
            if(a.first!=b.first)return a.first<b.first;
            return a.second.id<b.second.id;
        });
        target.clear();
        target.reserve(values.size());
        for(auto& entry:values)target.push_back(std::move(entry.second));
    };
    ordered(projectiles,current->projectiles);
    ordered(loot,current->loot);
    ordered(vehicles,current->vehicles);
}

void Module::setup(Simulation& sim,const Json& rules,const Json& map) {
    config_=readConfig(rules);
    experimentalMotorBatches_=experimentalMotorCommits_=0;
    if(sim.tickRate()!=config_.tickRate)throw JsonError("fps.tickRate differs from simulation tickRate");
    if(sim.players().empty()||sim.players().size()>config_.maxPlayers)throw JsonError("fps: invalid player count");
    streamedCollision_.reset();streamManifest_=map["world"];
    if(streamManifest_.getBool("async"))world::WorldAssets::parse(streamManifest_);
    collision_.clear();spawns_.clear();bots_.clear();
    const auto& boxes=map["colliders"];
    if(!boxes.isArray()||boxes.size()>100000)throw JsonError("fps.colliders: expected at most 100000 boxes");
    uint32_t rotated=kRotatedColliderBase;
    for(const auto& b:boxes.items()) {
        if(b.has("yaw")){   // rotated box: {"id","center","half","yaw" (turns)} -> 12 exact triangles
            auto id=integer(b,"id",0,1,kBuildingColliderBase-1);(void)id;
            if(!b["yaw"].isNumber())throw JsonError("fps.collider.yaw: expected turns");
            auto half=vector(b["half"]);if(half.x<=Fixed{}||half.y<=Fixed{}||half.z<=Fixed{}||half.x>2048_fx||half.z>2048_fx)throw JsonError("fps.collider.half: expected positive half sizes up to 2048");
            for(const auto& t:world::orientedBoxTriangles(vector(b["center"]),half,b["yaw"].fx()))
                if(!collision_.addTriangle(rotated++,t))throw JsonError("fps.collider: invalid rotated box");
            continue;
        }
        auto id=integer(b,"id",0,1,UINT32_MAX);
        if(collision_.colliders().contains(uint32_t(id))||!collision_.add({uint32_t(id),{vector(b["min"]),vector(b["max"])}}))throw JsonError("fps.collider: invalid or duplicate box");
    }
    auto buildingLoot=buildMapBuildings(map);
    const auto& spawns=map["spawns"];
    if(!spawns.isArray()||spawns.size()<sim.players().size())throw JsonError("fps.spawns: one spawn per player required");
    State s;
    for(size_t i=0;i<sim.players().size();++i) {
        Actor a;a.slot=uint8_t(i+1);a.team=sim.players()[i].team;
        if(a.team==0)throw JsonError("fps.player.team: must be positive");
        a.pos=vector(spawns[i]);
        if(fxAbs(a.pos.x)>config_.worldLimit||fxAbs(a.pos.y)>config_.worldLimit||fxAbs(a.pos.z)>config_.worldLimit)throw JsonError("fps.spawn: outside world limit");
        equipInitial(a);
        auto b=actorBounds(a,config_.motor);b.min.y+=Fixed::fromRatio(1,1000);
        if(collision_.blocked(b))throw JsonError("fps.spawn: intersects collision");
        s.actors.push_back(a);spawns_.push_back(a.pos);bots_.push_back(map["players"][i].getBool("bot",false));
    }
    setupSystems(sim,map,s);
    if(config_.systems.enabled)for(auto l:buildingLoot)if(s.loot.size()<256){l.id=s.nextObject++;s.loot.push_back(l);}
    setupRoyale(sim,map,s);
    if(!config_.gear.empty())s.gear.assign(s.actors.size(),{});
    setupVehicles(sim,map,s);
    setupRace(sim,rules,map,s);
    setupBots(sim,map,s);
    meleeAttacks_.clear();
    if(config_.melee.enabled){
        const auto& m=config_.melee;auto total=m.windup+m.active+m.recovery;
        meleeAttacks_[1]={1,m.windup,m.active,m.recovery,total,m.bufferTicks,{1}};
        if(!lib::genre::valid(meleeAttacks_))throw JsonError("fps.melee: invalid attack timing");
    }
    // ecsAuthority changes storage ownership, not gameplay rules. Keep the
    // legacy content hash/save v9 checksum stable across both implementations.
    Json hashRules=rules;
    hashRules.member("fps").erase("ecsAuthority");
    Hasher hash;hash.str(hashRules.dump(-1));hash.str(map.dump(-1));if(streamManifest_.getBool("async"))hash.str("world-stream-v1");contentHash_=hash.value();
    sim.world().setResource<State>(std::move(s));
    // Allocate one deterministic ECS identity per actor slot. No FPS save,
    // command, rewind or network format changes: the existing State resource
    // remains authoritative during this staged migration.
    ActorEntityIndex actorIndex;
    actorIndex.bySlot.reserve(sim.players().size());
    for (size_t i = 0; i < sim.players().size(); ++i) {
        const Entity entity = sim.world().create();
        actorIndex.bySlot.push_back(entity);
        sim.world().add<ActorEntityIdentity>(entity);
        sim.world().add<ActorEntityTransform>(entity);
        sim.world().add<ActorEntityVitals>(entity);
    }
    sim.world().setResource<ActorEntityIndex>(std::move(actorIndex));
    sim.world().setResource<DynamicEntityIndex>();
    syncActorEntities(sim);
    syncDynamicEntities(sim);
    if (config_.ecsAuthority)
        sim.addSystem("fps.ecsHydrate",[this](Simulation& s){
            hydrateActorsFromEcs(s);
            hydrateDynamicsFromEcs(s);
        });
    // Transitional system boundary: preserve the exact original FPS tick order.
    // With ecsAuthority enabled, ECS is hydrated before this stage and the
    // dynamic projectile components are committed after the main actor tick.
    // Do not batch weapon impact events later: immediate hits here must still
    // precede tickSystems(), royale, rewind capture and player movement.
    sim.addSystem("fps.compat.projectilePrograms",[this](Simulation& s) {
        if(state(s).winner)return;
        tickProjectilePrograms(s);
    });
    // Ordinary bullets, grenade fuses, bounce and smoke remain synchronous
    // with the original tick, but are now a separate composable stage before
    // round/royale and player systems. Both projectile stages preserve the
    // previous per-tick ordering and damage callback barriers.
    sim.addSystem("fps.compat.ordinaryProjectiles",[this](Simulation& s) {
        if(state(s).winner || !config_.systems.enabled)return;
        tickOrdinaryProjectiles(s);
    });
    sim.addSystem("fps",[this](Simulation& s){tick(s);});
    sim.addSystem("fps.ecsMirror",[this](Simulation& s){
        syncActorEntities(s);
        syncDynamicEntities(s,config_.ecsAuthority);
        if (!config_.ecsAuthority) return;
        // Migration invariant: after the commit barrier, the complete legacy
        // workspace actor must match its authoritative ECS payload.
        // Keep this check opt-in with ecsAuthority until all legacy systems
        // have been replaced by direct component systems.
        const auto& actors=state(s).actors;
        for (const Actor& actor : actors) {
            const Entity entity=actorEntity(s,actor.slot);
            const auto* runtime=s.world().tryGetConst<ActorRuntime>(entity);
            if (!runtime || !(runtime->value == actor))
                throw std::logic_error("FPS ECS actor mirror diverged after tick");
            const auto* identity=s.world().tryGetConst<ActorEntityIdentity>(entity);
            const auto* transform=s.world().tryGetConst<ActorEntityTransform>(entity);
            const auto* vitals=s.world().tryGetConst<ActorEntityVitals>(entity);
            if (!identity || identity->slot!=actor.slot ||
                identity->team!=actor.team || identity->life!=actor.life ||
                !transform || transform->position!=actor.pos ||
                transform->velocity!=actor.velocity ||
                transform->yaw!=actor.yaw || transform->pitch!=actor.pitch ||
                !vitals || vitals->health!=actor.health ||
                vitals->armor!=actor.armor || vitals->ammo!=actor.ammo ||
                vitals->reserve!=actor.reserve ||
                vitals->downed!=actor.downed ||
                vitals->crouched!=actor.crouched ||
                vitals->prone!=actor.prone)
                throw std::logic_error("FPS ECS editor projection diverged after tick");
        }
    });
}
void Module::applyCommand(Simulation& sim,const Command& c) {
    auto& s=state(sim);Input input;
    if(c.player==0||c.player>s.actors.size()||!decodeInput(c,input))return;
    Actor* canonical=&s.actors[c.player-1];
    if(config_.ecsAuthority) {
        const Entity e=actorEntity(sim,c.player);
        auto* component=sim.world().tryGet<ActorRuntime>(e);
        if(!component)return;
        canonical=&component->value;
    }
    Actor& p=*canonical;
    if(input.sequence==0||input.sequence<=p.lastSequence||input.viewTick>sim.tick()||
       (p.lastSequence&&p.lastInputTick==sim.tick()))return;
    p.input=input;p.lastSequence=input.sequence;p.lastInputTick=sim.tick();
    if(config_.ecsAuthority)
        s.actors[c.player-1]=p; // maintain existing external view until the tick
}
// Canonical spawn entry for weapons, throwable actions and other native FPS
// producers. The ECS runtime owns the new projectile at creation time when
// ecsAuthority is enabled; the old vector is retained only as a same-tick
// compatibility view until all other projectile consumers have migrated.
void Module::spawnProjectile(Simulation& sim,Projectile projectile) {
    State& s=state(sim);
    if(s.projectiles.size()>=256)
        throw std::logic_error("FPS projectile limit exceeded at spawn");
    if(!config_.ecsAuthority) {
        s.projectiles.push_back(std::move(projectile));
        return;
    }
    World& world=sim.world();
    auto* index=world.resource<DynamicEntityIndex>();
    if(!index || projectile.id==0)
        throw std::logic_error("FPS ECS projectile spawn missing index/id");
    const uint64_t key=(uint64_t(1)<<32)|projectile.id;
    const auto existing=index->byKey.find(key);
    if(existing!=index->byKey.end() && world.alive(existing->second))
        throw std::logic_error("FPS ECS duplicate projectile source ID on spawn");
    const Entity entity=world.create();
    world.add<ProjectileRuntime>(
        entity,ProjectileRuntime{projectile,uint32_t(s.projectiles.size())});
    index->byKey[key]=entity;
    world.add<ProjectileEntity>(entity,ProjectileEntity{
        projectile.id,projectile.owner,projectile.kind,
        projectile.pos,projectile.velocity,projectile.damage
    });
    s.projectiles.push_back(std::move(projectile));
}

// End-of-round objects are retired from the authoritative ECS World at the
// same logical barrier as the round reset, not recreated from a stale State
// projection at the end of the frame. Deterministic source-ID order matches
// the former DynamicEntityIndex map walk in fps.ecsMirror.
void Module::clearProjectiles(Simulation& sim) {
    State& current=state(sim);
    if(!config_.ecsAuthority) {
        current.projectiles.clear();
        return;
    }
    World& world=sim.world();
    auto* index=world.resource<DynamicEntityIndex>();
    if(!index)throw std::logic_error("FPS projectile reset missing dynamic ECS index");
    std::vector<std::pair<uint32_t,Entity>> expired;
    const World& read=world;
    read.eachExistingConst<ProjectileRuntime>(
        [&](Entity entity,const ProjectileRuntime& runtime) {
            if(read.alive(entity))
                expired.emplace_back(runtime.value.id,entity);
        });
    std::sort(expired.begin(),expired.end(),
        [](const auto& a,const auto& b){return a.first<b.first;});
    for(const auto& [id,entity]:expired) {
        world.destroy(entity);
        index->byKey.erase((uint64_t(1)<<32)|id);
    }
    // Drop stale projectile keys as well (e.g. an externally destroyed
    // Runtime). Non-projectile loot/vehicle keys must remain untouched.
    for(auto it=index->byKey.begin();it!=index->byKey.end();) {
        if((it->first>>32)==1)it=index->byKey.erase(it);
        else ++it;
    }
    current.projectiles.clear();
}

void Module::shoot(Simulation& sim,Actor& p) {
    auto& s=state(sim);
    uint32_t earliest=sim.tick()>config_.rewindTicks?sim.tick()-config_.rewindTicks:0;
    uint32_t requested=std::clamp(p.input.viewTick,earliest,sim.tick());
    const std::vector<Actor>* targets=&s.actors;
    for(auto it=s.history.rbegin();it!=s.history.rend();++it)if(it->tick<=requested){targets=&it->actors;break;}
    auto eye=p.pos+Vec3{{},stanceHeight(p,config_.motor)-Fixed::fromRatio(1,10),{}};
    const auto& weapon=config_.weapons[p.weaponIndex];
    if(config_.ecsAuthority) {
        // Fire accounting is committed to canonical ECS first. The legacy
        // Actor is only a compatibility workspace for hitscan/rewind and
        // existing save v9/network presentation until those stages migrate.
        const Entity shooter=actorEntity(sim,p.slot);
        auto* runtime=sim.world().tryGet<ActorRuntime>(shooter);
        if(!runtime)throw std::logic_error("FPS ECS shooter missing during fire");
        runtime->value.ammo=p.ammo;
        runtime->value.shots=p.shots;
        runtime->value.nextFire=p.nextFire;
        lib::genre::commitAcceptedShot(runtime->value.ammo,
            runtime->value.nextFire,sim.tick(),weapon.interval,
            &runtime->value.shots);
        p.ammo=runtime->value.ammo;
        p.shots=runtime->value.shots;
        p.nextFire=runtime->value.nextFire;
    } else {
        lib::genre::commitAcceptedShot(p.ammo,p.nextFire,sim.tick(),
                                       weapon.interval,&p.shots);
    }
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
            if(s.projectiles.size()<256){
                Projectile shot{ s.nextObject++,sim.tick()+config_.tickRate*20,p.slot,0,eye,direction(yaw,pitch)*weapon.bulletSpeed,weapon.damage,weapon.range,p.weaponIndex,weapon.penetration };
                if(weapon.flight.enabled){
                    shot.programSpeed=weapon.bulletSpeed;
                    projectile::initialize(shot.program,shot.programSpeed,weapon.flight,direction(yaw,pitch));
                    shot.program.distanceLeft=fxMin(shot.program.distanceLeft,weapon.range);
                    shot.velocity=shot.program.direction*shot.programSpeed+Vec3{{},shot.program.verticalSpeed,{}};
                }
                spawnProjectile(sim,std::move(shot));
            }
        }else traceShot(sim,p.slot,eye,direction(yaw,pitch)*weapon.range,weapon.damage,weapon,*targets);
    }
    if(config_.systems.enabled)p.recoil=fxMin(Fixed::fromRatio(1,20),p.recoil+weapon.recoil*((p.attachments[p.weaponIndex]&2)?Fixed::fromRatio(3,4):1_fx));
}

void Module::moveActorEcs(Simulation& sim,Actor& actor,const Input& input) {
    // Runtime motor operates on a real ECS component. Only state fields
    // consumed/produced by moveActor are copied across the compatibility
    // boundary; bags, weapon state, melee history etc. are NOT duplicated.
    const Entity entity=actorEntity(sim,actor.slot);
    auto* component=sim.world().tryGet<ActorRuntime>(entity);
    if(!component)
        throw std::logic_error("FPS ActorRuntime missing during ECS motor");
    Actor& motor=component->value;
    motor.pos=actor.pos;
    motor.velocity=actor.velocity;
    motor.yaw=actor.yaw;
    motor.pitch=actor.pitch;
    motor.grounded=actor.grounded;
    motor.crouched=actor.crouched;
    motor.prone=actor.prone;
    motor.downed=actor.downed;
    motor.climbing=actor.climbing;
    motor.frozen=actor.frozen;
    motor.vehicle=actor.vehicle;
    motor.health=actor.health;
    moveActor(motor,input,config_.motor,collision_,sim.dt(),config_.worldLimit);
    actor.pos=motor.pos;
    actor.velocity=motor.velocity;
    actor.yaw=motor.yaw;
    actor.pitch=motor.pitch;
    actor.grounded=motor.grounded;
    actor.crouched=motor.crouched;
    actor.prone=motor.prone;
}

void Module::tick(Simulation& sim) {
    auto& s=state(sim);if(s.winner)return;
    // Programmed projectiles have already advanced in the immediately
    // preceding Simulation stage (after optional ECS hydration).
    if(config_.systems.enabled)tickSystems(sim);
    tickRoyale(sim);
    if(s.winner)return;
    s.history.push_back({sim.tick(),s.actors});
    while(s.history.size()>size_t(config_.rewindTicks)+1)s.history.pop_front();
#if defined(AIVERSE_FPS_EXPERIMENTAL_PARALLEL_MOTOR)
    // Two-phase fixed-point motor update for independent, movement-only actors.
    // Never parallelize combat, shared State/RNG, bot target selection or events.
    // Copying Actor is expensive, hence the threshold; benchmark real-world gains.
    struct MotorResult {
        Vec3 pos{},velocity{};
        Fixed yaw{},pitch{};
        bool grounded=false,crouched=false,prone=false;
    };
    // The parallel phase computes independent fixed-point movement only.
    // The original Actor snapshot is compared at canonical commit time.
    // Prior damage, bot decisions or other changes invalidate a precompute.
    const bool motorWorldEligible=!config_.systems.enabled &&
        !s.royale.enabled && !streamedCollision_ && s.vehicleRuns.empty();
    // Retain only the fields read by moveActor(). Actor contains bags and
    // melee hit lists; deep-copying whole Actors per tick would dominate the
    // speculative motor cost for large multiplayer matches.
    struct MotorSource {
        Vec3 pos{},velocity{};
        Fixed yaw{},pitch{};
        Input input{};
        uint32_t lastInputTick=0,vehicle=0;
        int32_t health=0;
        bool frozen=false,climbing=false,prone=false,crouched=false,
             grounded=false,downed=false;
        static MotorSource capture(const Actor& a) {
            return {a.pos,a.velocity,a.yaw,a.pitch,a.input,a.lastInputTick,
                    a.vehicle,a.health,a.frozen,a.climbing,a.prone,
                    a.crouched,a.grounded,a.downed};
        }
        bool sameMotorInputs(const Actor& a) const {
            return pos==a.pos && velocity==a.velocity &&
                   yaw==a.yaw && pitch==a.pitch && input==a.input &&
                   lastInputTick==a.lastInputTick && vehicle==a.vehicle &&
                   health==a.health && frozen==a.frozen &&
                   climbing==a.climbing && prone==a.prone &&
                   crouched==a.crouched && grounded==a.grounded &&
                   downed==a.downed;
        }
        Actor toActor() const {
            Actor a;
            a.pos=pos;a.velocity=velocity;a.yaw=yaw;a.pitch=pitch;
            a.input=input;a.lastInputTick=lastInputTick;
            a.vehicle=vehicle;a.health=health;
            a.frozen=frozen;a.climbing=climbing;a.prone=prone;
            a.crouched=crouched;a.grounded=grounded;a.downed=downed;
            return a;
        }
    };
    std::vector<MotorResult> motorResults;
    std::vector<MotorSource> motorOriginals;
    std::vector<uint8_t> motorEligible;
    bool parallelMotor=false;
    if(motorWorldEligible && s.actors.size()>=32) {
        motorEligible.assign(s.actors.size(),uint8_t(0));
        size_t count=0;
        for(size_t i=0;i<s.actors.size();++i) {
            const Actor& actor=s.actors[i];
            if(!bots_[i] && actor.health>0 && !actor.vehicle &&
               !actor.frozen && !actor.climbing) {
                motorEligible[i]=1;
                ++count;
            }
        }
        parallelMotor=count>=32;
    }
    if(parallelMotor) {
        ++experimentalMotorBatches_;
        motorResults.resize(s.actors.size());
        motorOriginals.resize(s.actors.size());
        for(size_t i=0;i<s.actors.size();++i)
            if(motorEligible[i])motorOriginals[i]=MotorSource::capture(s.actors[i]);
        static thread_local ecs_experiment::PersistentWorkers motorWorkers{3};
        motorWorkers.parallelFor(s.actors.size(),[&](size_t begin,size_t end) {
            for(size_t i=begin;i<end;++i) {
                if(!motorEligible[i])continue;
                Actor copy=motorOriginals[i].toActor();
                Input input=copy.input;
                if(sim.tick()-copy.lastInputTick>config_.inputTimeout) {
                    input.forward=input.right=0;
                    input.buttons=0;input.action=Action::None;
                }
                moveActor(copy,input,config_.motor,collision_,sim.dt(),config_.worldLimit);
                motorResults[i]={copy.pos,copy.velocity,copy.yaw,copy.pitch,
                                 copy.grounded,copy.crouched,copy.prone};
            }
        });
    }
#endif
    for(auto& a:s.actors) {
        if(a.health<=0) {
            if(config_.mode=="deathmatch"&&sim.tick()>=a.respawnAt) {
                auto slot=a.slot,team=a.team;auto kills=a.kills;auto seq=a.lastSequence;auto life=a.life;
                a=Actor{};a.slot=slot;a.team=team;a.kills=kills;a.lastSequence=seq;
                a.armor=config_.systems.startArmor;a.money=config_.systems.startMoney;
                a.life=life+1;a.pos=spawns_[slot-1];equipInitial(a);
            } else {skydive(sim,a);continue;}   // a body in the air keeps falling (battle royale)
        }
        if(skydive(sim,a))continue;
        // Race bots drive through the race session (tickRace); they never run the on-foot bot.
        const bool footBot=bots_[a.slot-1]&&a.lastSequence==0&&!s.race.enabled;
        if(footBot&&thinkBot(sim,a)){}
        else if(footBot) {
            const Actor* target=nullptr;Fixed nearest=config_.weapons[a.weaponIndex].range*config_.weapons[a.weaponIndex].range;
            for(const auto& candidate:s.actors)if(candidate.health>0&&!candidate.downed&&candidate.team!=a.team) {
                auto distance=(candidate.pos-a.pos).lengthSq();if(distance<nearest){
                    auto eye=a.pos+Vec3{{},1_fx,{}};auto delta=candidate.pos+Vec3{{},1_fx,{}}-eye;bool visible=!collision_.cast(eye,delta).hit;
                    if(config_.systems.enabled)for(const auto& cloud:s.projectiles)if(cloud.kind==4){auto radius=config_.systems.smokeRadius;Fixed fraction;Vec3 normal;if(world::intersect(eye,delta,{cloud.pos-Vec3{radius,radius,radius},cloud.pos+Vec3{radius,radius,radius}},fraction,normal))visible=false;}
                    if(visible){nearest=distance;target=&candidate;}
                }
            }
            a.input={};a.lastInputTick=sim.tick();a.input.viewTick=sim.tick();
            if(target) {
                auto delta=target->pos-a.pos;
                auto yaw=fxAtan2Turns(delta.x,delta.z);if(yaw<Fixed{})yaw+=1_fx;
                a.input.yaw=yaw;a.input.pitch=fxAtan2Turns(delta.y,delta.xz().length());
                a.input.forward=nearest>100_fx?700:0;a.input.right=((sim.tick()/90+a.slot)%2)?350:-350;
                a.input.buttons=a.ammo?((config_.weapons[a.weaponIndex].automatic||(!a.fireHeld&&sim.tick()>=a.nextFire))?Fire:0u):Reload;
            }
            steerRoyaleBot(sim,a);
        }
        auto input=a.input;
        if(sim.tick()-a.lastInputTick>config_.inputTimeout){input.forward=input.right=0;input.buttons=0;input.action=Action::None;}
        if(config_.systems.enabled)actorSystems(sim,a,input);
#if defined(AIVERSE_FPS_EXPERIMENTAL_PARALLEL_MOTOR)
        const size_t motorIndex=size_t(&a-s.actors.data());
        if(parallelMotor && motorEligible[motorIndex] &&
           motorOriginals[motorIndex].sameMotorInputs(a)) {
            ++experimentalMotorCommits_;
            const auto& m=motorResults[motorIndex];
            a.pos=m.pos;a.velocity=m.velocity;a.yaw=m.yaw;a.pitch=m.pitch;
            a.grounded=m.grounded;a.crouched=m.crouched;a.prone=m.prone;
        } else
#endif
        if(!a.vehicle) {
            if(config_.ecsAuthority)moveActorEcs(sim,a,input);
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
        // Transitional native adapter: the ECS ActorRuntime is canonical between
        // ticks when ecsAuthority is ON, and State::actors remains the legacy
        // tick workspace. Do not create parallel authoritative ammo components.
        // Both generic ECS hosts and FPS now share the SAME reload algorithm.
        lib::genre::WeaponStateComponent reloadWeapon{a.ammo,a.nextFire};
        lib::genre::WeaponReloadComponent reloadState{
            a.reserve,capacity,weapon.reloadTicks,a.reloadUntil};
        lib::genre::stepWeaponReload(reloadWeapon,reloadState,sim.tick(),
                                      (input.buttons&Reload)!=0);
        a.ammo=reloadWeapon.ammo;
        a.reserve=reloadState.reserve;
        a.reloadUntil=reloadState.finishesAt;
        bool trigger=(input.buttons&Fire)!=0;
        if(trigger&&!a.downed&&(!a.vehicle||canFireFromVehicle(s,a))&&a.activeAction==Action::None&&!a.blockHeld&&sim.tick()>=a.staggerUntil&&(a.owned&(1u<<a.weaponIndex))&&(weapon.automatic||!a.fireHeld)&&!a.reloadUntil&&a.ammo>0&&sim.tick()>=a.nextFire&&sim.tick()>=a.switchUntil)shoot(sim,a);
        a.fireHeld=trigger;
        if(a.pos.y< -100_fx){a.health=0;a.respawnAt=sim.tick()+config_.respawnTicks;}
        if(config_.mode=="battleRoyale"&&!config_.royale&&sim.tick()>=config_.zoneStart&&sim.tick()%config_.tickRate==0) {
            auto t=Fixed::fromRatio(std::min(sim.tick()-config_.zoneStart,config_.zoneDuration),config_.zoneDuration);
            auto radius=config_.zoneRadius+(config_.zoneEndRadius-config_.zoneRadius)*t;
            if((a.pos-config_.zoneCenter).xz().lengthSq()>radius*radius)a.health=std::max(0,a.health-config_.zoneDamage);
        }
    }
    if(s.race.enabled){tickRace(sim);return;}
    tickVehicles(sim);
    if(config_.mode=="bomb")return;
    if(config_.royale){settleRoyale(sim);if(s.winner)return;}
    else if(config_.mode!="deathmatch") {
        std::set<uint8_t> teams;for(const auto& a:s.actors)if(a.health>0&&!a.downed)teams.insert(a.team);
        if(teams.size()<=1)s.winner=teams.empty()?-1:*teams.begin();
    }
    if(sim.tick()+1>=config_.roundTicks&&!s.winner) {
        if(config_.mode=="deathmatch") {
            std::map<uint8_t,int> scores;for(const auto& a:s.actors)scores[a.team]+=a.kills;
            int best=-1,winner=-1;for(auto [team,score]:scores)if(score>best){best=score;winner=team;}else if(score==best)winner=-1;
            s.winner=winner;
        } else s.winner=-1;
    }
}
State Module::authoritativeStateCopy(Simulation& sim) const {
    // Serialize/hash from canonical ECS payloads, never from a possibly stale
    // State::actors/projectiles/loot/vehicles compatibility workspace.
    // Saving/checksumming must not detach a shared COW State merely to
    // read it. The mutable state(sim) accessor is reserved for gameplay writes.
    const State* stored=sim.world().resourceConst<State>();
    if(!stored)throw std::logic_error("FPS State missing during save/checksum");
    State result=*stored;
    if(!config_.ecsAuthority)return result;
    const World& world=sim.world();
    const auto* index=world.resourceConst<ActorEntityIndex>();
    if(!index||index->bySlot.size()!=result.actors.size())
        throw std::logic_error("FPS ECS authority index missing during save/checksum");
    for(size_t i=0;i<result.actors.size();++i) {
        const Entity e=index->bySlot[i];
        const auto* runtime=world.tryGetConst<ActorRuntime>(e);
        if(!runtime||!world.alive(e)||runtime->value.slot!=i+1)
            throw std::logic_error("FPS ActorRuntime missing during save/checksum");
        result.actors[i]=runtime->value;
    }
    std::vector<std::pair<uint32_t,Projectile>> projectiles;
    std::vector<std::pair<uint32_t,Loot>> loot;
    std::vector<std::pair<uint32_t,Vehicle>> vehicles;
    world.eachExistingConst<ProjectileRuntime>([&](Entity e,const ProjectileRuntime& r) {
        if(world.alive(e))projectiles.emplace_back(r.order,r.value);
    });
    world.eachExistingConst<LootRuntime>([&](Entity e,const LootRuntime& r) {
        if(world.alive(e))loot.emplace_back(r.order,r.value);
    });
    world.eachExistingConst<VehicleRuntime>([&](Entity e,const VehicleRuntime& r) {
        if(world.alive(e))vehicles.emplace_back(r.order,r.value);
    });
    auto reconstruct=[](auto& entries,auto& out) {
        std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b) {
            if(a.first!=b.first)return a.first<b.first;
            return a.second.id<b.second.id;
        });
        out.clear();
        out.reserve(entries.size());
        for(auto& entry:entries)out.push_back(std::move(entry.second));
    };
    reconstruct(projectiles,result.projectiles);
    reconstruct(loot,result.loot);
    reconstruct(vehicles,result.vehicles);
    return result;
}
void Module::hashState(Simulation& sim,Hasher& hash) {
    State materialized;
    // Read through the const ECS resource API: resource<State>() performs
    // copy-on-write detachment even for a read-only checksum. A hash must
    // preserve the snapshot's resource sharing and have no World side effects.
    const State* stored=sim.world().resourceConst<State>();
    if(!stored)throw std::logic_error("FPS State missing during checksum");
    if(config_.ecsAuthority) {
        materialized=authoritativeStateCopy(sim);
        stored=&materialized;
    }
    // Archive's output-only path takes non-const references for its
    // bidirectional IO API, but never writes any State field while writing().
    State& s=const_cast<State&>(*stored);
    hash.u64(contentHash_);std::string bytes;Archive a(&bytes);stateIo(a,s);hash.str(bytes);
    if(config_.melee.enabled){std::string extra;Archive m(&extra);for(auto& actor:s.actors)meleeIo(m,actor);hash.str(extra);}
    for(auto& p:s.projectiles)if(p.program.enabled){std::string extra;Archive e(&extra);e(p.id,p.program,p.programSpeed,p.programTarget);hash.str(extra);}
    if(s.royale.enabled){std::string extra;Archive r(&extra);royaleIo(r,s.royale);hash.str(extra);}
    for(const auto& g:s.gear)hash.u64(lib::systems::gearHash(g));
    if(!s.vehicleRuns.empty()){std::string extra;Archive v(&extra);ioVehicleRuns(v,s.vehicleRuns);hash.str(extra);}
    if(s.race.enabled){std::string extra;Archive r(&extra);raceIo(r,s.race);hash.str(extra);}
    for(const auto& b:s.botStates)hash.str(b);
}
bool Module::serializeState(Simulation& sim,Archive& archive) {
    // Version 10 only adds the race run; maps without a race keep writing version 9 byte for byte.
    uint32_t version=raceRules_?10:9;uint64_t content=contentHash_;archive(version,content);
    if((version!=3&&version<6)||version>10||content!=contentHash_)return false;
    // On load, Simulation has already restored the saved Entity allocator;
    // old v9 saves may not have any ECS actor identities yet. Only writing
    // materializes canonical ECS payloads; reading starts from the old state.
    State copy=(archive.writing()&&config_.ecsAuthority)?authoritativeStateCopy(sim):state(sim);
    stateIo(archive,copy,version>=6,version>=7);
    if(version>=8)royaleIo(archive,copy.royale);else copy.royale=RoyaleRun{};
    if(version>=9){uint32_t n=archive.count(copy.gear.size());if(n>256){archive.fail();return false;}if(!archive.writing())copy.gear.resize(n);for(auto& g:copy.gear)lib::systems::io(archive,g);}else copy.gear.clear();
    if(version>=9)ioVehicleRuns(archive,copy.vehicleRuns);else copy.vehicleRuns.clear();
    if(version>=9){archive(copy.botStates);if(copy.botStates.size()>256)return false;}else copy.botStates.clear();
    if(version>=10)raceIo(archive,copy.race);else copy.race=RaceRun{};
    if(copy.race.enabled!=bool(raceRules_))return false;
    if(copy.race.enabled&&!validRace(copy))return false;
    if(copy.botStates.size()!=(botHost_?spawns_.size():0))return false;
    for(const auto& r:copy.vehicleRuns){
        if(!config_.vehicleTypes||!config_.vehicleTypes->vehicles.contains(r.type)||!lib::genre::valid(r.controller)||!lib::genre::valid(r.seats))return false;
        if(std::none_of(copy.vehicles.begin(),copy.vehicles.end(),[&](const Vehicle& v){return v.id==r.vehicle;}))return false;
        for(auto [seat,actor]:r.seats.occupants){(void)seat;if(actor<1||actor>int64_t(spawns_.size()))return false;}
    }
    if(copy.gear.size()!=(config_.gear.empty()?0:spawns_.size()))return false;
    for(const auto& g:copy.gear)if(!lib::systems::valid(config_.gear,g))return false;
    if(copy.royale.enabled!=bool(config_.royale))return false;
    if(copy.royale.enabled){
        using namespace lib::genre;const auto& r=copy.royale;auto n=spawns_.size();
        if(!valid(r.zone)||!valid(r.bombardment)||!valid(r.drops)||!valid(r.flight)||!valid(r.standings)||r.lastAttacker.size()!=n||r.lastWeapon.size()!=n||
           (r.divers.size()!=n&&!(r.divers.empty()&&!config_.royale->flight))||r.standings.players.size()!=n)return false;
        for(const auto& d:r.divers)if(!valid(d))return false;
        for(auto x:r.lastAttacker)if(x>n)return false;
    }
    if(version<7){auto reset=[](Actor& p){p.melee={};p.parryUntil=p.staggerUntil=0;p.lockTarget=0;p.blockHeld=p.meleeHasTip=false;p.meleeTip={};};
        for(auto& p:copy.actors)reset(p);
        for(auto& f:copy.history)for(auto& p:f.actors)reset(p);}
    if(!archive.ok()||copy.actors.size()!=spawns_.size())return false;
    auto valid=[&](const Actor& actor){
        if(actor.weaponIndex>=config_.weapons.size()||actor.slot==0||actor.slot>spawns_.size()||actor.bag.size()>24||actor.actionTarget>spawns_.size()||uint32_t(actor.activeAction)>uint32_t(Action::Vault)||actor.owned>15||actor.armor<0||actor.armor>100||actor.health<0||actor.health>100||actor.money<0||actor.money>16000)return false;
        for(auto item:actor.bag)if(item.type<1||item.type>9||item.count<1||item.count>20)return false;
        const auto& m=actor.melee;
        if(actor.lockTarget>spawns_.size()||m.attack<0||m.attack>1||m.queued<0||m.queued>1||m.started>m.lastTick||m.queuedAt>m.lastTick||m.hitTargets.size()>spawns_.size()||
           !std::is_sorted(m.hitTargets.begin(),m.hitTargets.end())||std::adjacent_find(m.hitTargets.begin(),m.hitTargets.end())!=m.hitTargets.end()||
           (!m.hitTargets.empty()&&(m.hitTargets.front()<1||m.hitTargets.back()>int64_t(spawns_.size()))))return false;
        return true;
    };
    if(copy.projectiles.size()>256||copy.loot.size()>256||copy.vehicles.size()>32)return false;
    for(const auto& p:copy.projectiles)if(p.kind>4||!p.owner||p.owner>spawns_.size()||p.weapon>=config_.weapons.size()||p.piercedCount>8)return false;
    for(const auto& p:copy.projectiles)if(!projectile::valid(p.program,p.programSpeed,config_.weapons[p.weapon].flight))return false;
    for(const auto& v:copy.vehicles)if(v.driver>spawns_.size()||v.health<0||v.health>500)return false;
    for(const auto& actor:copy.actors)if(!valid(actor))return false;
    for(const auto& frame:copy.history)for(const auto& actor:frame.actors)if(!valid(actor))return false;
    if(!archive.writing()) {
        state(sim)=std::move(copy);
        // Derived ECS components must track legacy v9 save contents.
        syncActorEntities(sim);
        syncDynamicEntities(sim);
    }
    return true;
}
}
