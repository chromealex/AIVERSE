// Shared ECS weapon fire -> deferred projectile spawn.
// Compose with EcsProjectileSystems.h and EcsDamageSystems.h. Genre modules
// provide input and configuration; no RTS/FPS dependency in this pipeline.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/mechanics/EcsDamageSystems.h"
#include "engine/core/FixedMath.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::mechanics {

struct WeaponParametersComponent {
    Fixed muzzleSpeed=20_fx;
    Fixed gravity{};
    Fixed radius=Fixed::fromRatio(1,20);
    uint32_t lifetime=60;
    uint32_t fireInterval=6;
    int32_t directDamage=25;
};

struct WeaponStateComponent {
    int32_t ammo=30;
    uint32_t nextFireTick=0;
};

struct FireIntentComponent {
    Vec3 origin{};
    Vec3 direction{Fixed{},Fixed{},1_fx};
    bool requested=false;
};

struct ProjectileSpawnCommand {
    ProjectileFlightComponent flight;
    ProjectileDamageComponent damage;
};

struct ProjectileSpawnQueue {
    uint32_t nextShotId=1;
    std::vector<ProjectileSpawnCommand> pending;
};

inline bool validWeaponParameters(const WeaponParametersComponent& p) {
    return p.muzzleSpeed>Fixed{} && p.muzzleSpeed<=1000_fx &&
           p.gravity>=Fixed{} && p.gravity<=1000_fx &&
           p.radius>=Fixed{} && p.radius<=2_fx &&
           p.lifetime>0 && p.lifetime<=100000 &&
           p.fireInterval>0 && p.fireInterval<=100000 &&
           p.directDamage>0 && p.directDamage<=1000000;
}

// Shared post-acceptance weapon accounting. Call only after the host has
// validated its own firing rules (FPS hitscan/rewind, RTS target policy, etc.).
// This pure policy has no genre dependency, RNG use or structural ECS writes.
inline void commitAcceptedShot(int32_t& ammo,uint32_t& nextFireTick,
                               uint32_t now,uint32_t interval,
                               uint32_t* shots=nullptr) {
    --ammo;
    nextFireTick=now+interval;
    if(shots)++*shots;
}

// A fire request is consumed exactly once. Successful shots are collected
// in source Entity dense order, but no Entity is created while iterating the
// weapon pool. Config is read-only; ammo and fire intent are writable.
inline void addWeaponLaunchSystem(Simulation& sim,std::string name) {
    sim.addSystem(std::move(name),[](Simulation& current) {
        World& world=current.world();
        auto* weapons=world.prepareExistingWrite<WeaponStateComponent>();
        auto* intents=world.prepareExistingWrite<FireIntentComponent>();
        auto* spawns=world.resource<ProjectileSpawnQueue>();
        if(!weapons || !intents || !spawns)return;
        const World& read=world;
        const auto now=current.tick();
        const size_t count=weapons->size();
        for(size_t i=0;i<count;++i) {
            const Entity owner=weapons->entityAt(i);
            if(!world.alive(owner))continue;
            WeaponStateComponent& state=weapons->dataAt(i);
            FireIntentComponent* intent=intents->tryGet(owner.index);
            if(!intent || !intent->requested)continue;
            intent->requested=false;
            const auto* p=read.tryGetConst<WeaponParametersComponent>(owner);
            if(!p || !validWeaponParameters(*p) ||
               state.ammo<=0 || now<state.nextFireTick ||
               now>std::numeric_limits<uint32_t>::max()-p->fireInterval ||
               spawns->nextShotId==0 ||
               spawns->nextShotId==std::numeric_limits<uint32_t>::max() ||
               intent->direction.lengthSq()<=Fixed{})continue;

            ProjectileSpawnCommand cmd;
            cmd.flight.owner=owner;
            cmd.flight.shotId=spawns->nextShotId++;
            cmd.flight.config.gravity=p->gravity;
            cmd.flight.config.radius=p->radius;
            cmd.flight.config.lifetime=p->lifetime;
            cmd.flight.state.position=intent->origin;
            cmd.flight.state.velocity=fxNormalize(intent->direction)*p->muzzleSpeed;
            cmd.damage.directDamage=p->directDamage;
            spawns->pending.push_back(std::move(cmd));
            commitAcceptedShot(state.ammo,state.nextFireTick,now,p->fireInterval);
        }
    });
}

// Register after launch and before flight. Structural ECS changes occur only
// here, at their own system barrier, in the original source submission order.
inline void addProjectileSpawnSystem(Simulation& sim,std::string name) {
    sim.addSystem(std::move(name),[](Simulation& current) {
        World& world=current.world();
        auto* pending=world.resource<ProjectileSpawnQueue>();
        if(!pending || pending->pending.empty())return;
        std::vector<ProjectileSpawnCommand> batch;
        batch.swap(pending->pending);
        for(auto& command:batch) {
            const Entity shot=world.create();
            world.add<ProjectileFlightComponent>(shot,std::move(command.flight));
            world.add<ProjectileDamageComponent>(shot,std::move(command.damage));
        }
    });
}

} // namespace aiverse::lib::mechanics
