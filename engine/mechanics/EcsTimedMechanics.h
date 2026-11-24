// Universal ECS timed gameplay building blocks.
// Works with FPS players, RTS units, vehicles, turrets, spells and project-native
// entities. The host supplies inputs/config; no genre module dependencies.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/mechanics/EcsWeaponLaunchSystems.h"
#include "engine/mechanics/FixedRegeneration.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::lib::mechanics {

// Mutable state is owned by ECS, not copied into a per-genre state machine.
struct WeaponReloadComponent {
    int32_t reserve=0;
    int32_t magazineCapacity=30;
    uint32_t durationTicks=60;
    uint32_t finishesAt=0; // 0 means idle; finish tick is strictly positive
};
struct ReloadIntentComponent {bool requested=false;};

// Deterministic shared one-tick reload policy, also usable from transitional
// legacy host adapters. The native FPS adapter feeds the CharacterWeapons
// component values without introducing a second mutable weapon state.
inline void stepWeaponReload(WeaponStateComponent& weapon,
                             WeaponReloadComponent& reload,
                             uint32_t tick,bool requested) {
    if(reload.finishesAt && tick>=reload.finishesAt) {
        // Keep the exact pre-existing FPS transfer order and result for
        // valid inventory states (including completion+new request same tick).
        const int32_t transferred=std::min(reload.magazineCapacity-weapon.ammo,
                                           reload.reserve);
        weapon.ammo+=transferred;
        reload.reserve-=transferred;
        reload.finishesAt=0;
    }
    if(!requested || reload.finishesAt || reload.reserve<=0 ||
       reload.magazineCapacity<=0 || reload.durationTicks==0 ||
       weapon.ammo>=reload.magazineCapacity ||
       tick>=std::numeric_limits<uint32_t>::max()-reload.durationTicks)
        return;
    reload.finishesAt=tick+reload.durationTicks;
}

// A transport adapter may compose an Actor's legacy save fields into these
// data-only parameters; systems and adapters MUST both use this one function.

// Register before weapon launch. Completion occurs before a new reload request
// is considered; requests are consumed even when no ammo/reserve is available.
// External systems should only submit one-tick ReloadIntent values.
inline void addWeaponReloadSystem(Simulation& sim,std::string systemName) {
    sim.addSystem(std::move(systemName),[](Simulation& current) {
        World& world=current.world();
        auto* weapons=world.prepareExistingWrite<WeaponStateComponent>();
        auto* magazines=world.prepareExistingWrite<WeaponReloadComponent>();
        auto* intents=world.prepareExistingWrite<ReloadIntentComponent>();
        if(!weapons || !magazines)return;
        const uint32_t tick=current.tick();
        for(size_t i=0;i<weapons->size();++i) {
            const Entity entity=weapons->entityAt(i);
            if(!world.alive(entity))continue;
            WeaponReloadComponent* reload=magazines->tryGet(entity.index);
            if(!reload)continue;
            WeaponStateComponent& weapon=weapons->dataAt(i);
            ReloadIntentComponent* intent=intents?intents->tryGet(entity.index):nullptr;
            const bool requested=intent && intent->requested;
            if(intent)intent->requested=false;
            stepWeaponReload(weapon,*reload,tick,requested);
        }
    });
}

// Optional gate, immediately after reload and before weapon launch: a weapon
// reloading cannot emit a shot. FireIntent is consumed once, not buffered.
inline void addReloadFireGateSystem(Simulation& sim,std::string systemName) {
    sim.addSystem(std::move(systemName),[](Simulation& current) {
        World& world=current.world();
        auto* intents=world.prepareExistingWrite<FireIntentComponent>();
        if(!intents)return;
        const World& read=world;
        for(size_t i=0;i<intents->size();++i) {
            const Entity entity=intents->entityAt(i);
            if(!world.alive(entity))continue;
            const auto* reload=read.tryGetConst<WeaponReloadComponent>(entity);
            if(reload && reload->finishesAt && current.tick()<reload->finishesAt)
                intents->dataAt(i).requested=false;
        }
    });
}

// Health uses the SAME HitPointsComponent as the generic impact/damage system.
// Fractional fixed-point remainder is persistent and included in snapshots.
struct HealthRegenerationComponent {
    Fixed pointsPerSecond{};
    Fixed remainder{};
    bool enabled=true;
};
inline void addHealthRegenerationSystem(Simulation& sim,std::string systemName) {
    sim.addSystem(std::move(systemName),[](Simulation& current) {
        World& world=current.world();
        auto* healths=world.prepareExistingWrite<HitPointsComponent>();
        auto* regens=world.prepareExistingWrite<HealthRegenerationComponent>();
        if(!healths || !regens)return;
        for(size_t i=0;i<healths->size();++i) {
            const Entity entity=healths->entityAt(i);
            if(!world.alive(entity))continue;
            auto* regen=regens->tryGet(entity.index);
            if(!regen || !regen->enabled || regen->pointsPerSecond<=Fixed{} ||
               regen->pointsPerSecond>1000000_fx)continue;
            auto& health=healths->dataAt(i);
            if(health.maximum<=0 || health.current<=0)continue; // no implicit resurrection
            if(health.current>=health.maximum){regen->remainder=Fixed{};continue;}
            const Fixed generated=regen->remainder+regen->pointsPerSecond*current.dt();
            const int64_t points=std::max<int64_t>(0,generated.toInt());
            const int64_t applied=std::min<int64_t>(points,int64_t(health.maximum)-health.current);
            health.current+=int32_t(applied);
            regen->remainder=health.current==health.maximum?Fixed{}:
                generated-Fixed::fromInt(points);
        }
    });
}

struct EnergyRegenerationComponent {
    Fixed current{};
    Fixed maximum=100_fx;
    Fixed perSecond{};
    bool enabled=true;
};
inline void addEnergyRegenerationSystem(Simulation& sim,std::string systemName) {
    sim.addSystem(std::move(systemName),[](Simulation& current) {
        eachEcsMechanic<EnergyRegenerationComponent>(current.world(),
            [&](Entity,EnergyRegenerationComponent& energy) {
                if(!energy.enabled || energy.maximum<=Fixed{} ||
                   energy.perSecond<=Fixed{} || energy.perSecond>1000000_fx)return;
                energy.current=stepFixedResourceRegeneration(
                    fxMax(Fixed{},energy.current),energy.maximum,
                    energy.perSecond,current.dt());
            });
    });
}

struct TimedExpiryComponent {
    uint32_t expiresAtTick=0; // zero means infinite lifetime
};
inline void addTimedExpirySystem(Simulation& sim,std::string systemName) {
    sim.addSystem(std::move(systemName),[](Simulation& current) {
        const World& world=current.world();
        std::vector<Entity> expired;
        world.eachExistingConst<TimedExpiryComponent>(
            [&](Entity entity,const TimedExpiryComponent& timer) {
                if(timer.expiresAtTick && current.tick()>=timer.expiresAtTick &&
                   world.alive(entity))expired.push_back(entity);
            });
        for(const Entity entity:expired)current.world().destroyLater(entity);
    });
}

} // namespace aiverse::lib::mechanics
