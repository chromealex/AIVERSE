// Optional PHYSIS fire -> AETHER oxygen/smoke integration.
// PHYSIS owns climate, wind, flames and fuel. AETHER only owns gas species.
#pragma once
#include "AetherEcs.h"
#include "engine/physis/PhysisDisasters.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace aiverse::aether {
struct FireCoupling {
    Mass oxygenConsumedAtFullFire=0;
    Mass smokeProducedAtFullFire=0;
    std::uint64_t processedClimateTick=std::numeric_limits<std::uint64_t>::max();
    bool enabled=true;
};
inline void io(Archive& a,FireCoupling& c){
    a(c.oxygenConsumedAtFullFire,c.smokeProducedAtFullFire,
      c.processedClimateTick,c.enabled);
    if(!a.writing() && (c.oxygenConsumedAtFullFire<0||
       c.smokeProducedAtFullFire<0||
       c.oxygenConsumedAtFullFire>kMaxDuctRate||
       c.smokeProducedAtFullFire>kMaxDuctRate))a.fail();
}
inline bool valid(const FireCoupling& c){
    return c.oxygenConsumedAtFullFire>=0&&
           c.oxygenConsumedAtFullFire<=kMaxDuctRate&&
           c.smokeProducedAtFullFire>=0&&
           c.smokeProducedAtFullFire<=kMaxDuctRate;
}

// Call after PHYSIS environmental update and AETHER transport. One cell can
// attach BOTH GasCell and PHYSIS HazardCell. Does not modify fire strength:
// turning off fire when oxygen runs out needs an explicit PHYSIS authority API.
// This coupling is a mass conversion heuristic, not combustion chemistry.
// Returns false transactionally on invalid authoring, leaving World unchanged.
inline bool applyPhysisFire(World& world){
    const World& read=world;
    const auto* climate=read.resourceConst<physis::State>();
    if(!climate)return false;
    struct Update {Entity entity;Mass oxygen=0,smoke=0;};
    std::vector<Update> planned;
    bool invalid=false;
    read.eachExistingConst<FireCoupling>([&](Entity e,const FireCoupling& coupling){
        if(!read.alive(e)||!coupling.enabled||
           coupling.processedClimateTick==climate->tick)return;
        if(!valid(coupling)){invalid=true;return;}
        const auto* gas=read.tryGetConst<GasCell>(e);
        const auto* fire=read.tryGetConst<physis::HazardCell>(e);
        if(!gas||!gas->enabled||!valid(*gas)||!fire)return;
        const auto strength=fxClamp(fire->fire,Fixed{},Fixed::fromInt(1)).raw;
        // Rates <= 10m; Q16 intermediate <= 6.55e11, cannot overflow int64.
        const Mass oxygen=std::min(gas->oxygen,
            coupling.oxygenConsumedAtFullFire*strength/Fixed::kOne);
        const Mass smoke=std::min(kMaxCellMass-gas->smoke,
            coupling.smokeProducedAtFullFire*strength/Fixed::kOne);
        planned.push_back({e,oxygen,smoke});
    });
    if(invalid)return false;
    // Single canonical barrier, no scene edits and no PHYSIS state writes.
    for(const auto& update:planned){
        auto& gas=world.get<GasCell>(update.entity);
        gas.oxygen-=update.oxygen;
        gas.smoke+=update.smoke;
        world.get<FireCoupling>(update.entity).processedClimateTick=climate->tick;
    }
    return true;
}
inline void registerPhysisFirePersistence(Simulation& sim){
    sim.registerPersistentComponent<FireCoupling>("aether.fire_coupling",1);
}
inline void addPhysisFireSystem(Simulation& sim,
                                std::string name="aether.physis_fire"){
    // Opt-in: caller registers persistence during setup exactly once.
    registerPhysisFirePersistence(sim);
    sim.addSystem(std::move(name),[](Simulation& s){
        (void)applyPhysisFire(s.world());
    });
}
} // namespace aiverse::aether
