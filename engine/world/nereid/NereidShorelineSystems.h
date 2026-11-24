// NEREID optional ECS bank-erosion stage. Registered after water sampling.
// NEREID alone owns current/flow; the ground owner applies resulting R16
// height changes after the simulation read barrier (TerraWeave bridge).
#pragma once
#include "NereidErosion.h"
#include "NereidWaterSystems.h"
#include <string>
#include <utility>

namespace aiverse::lib::nereid {

// Attach to stable bank-cell entities (not to individual blades/trees).
// A host shoreline rasterizer chooses the cell density and wet-bank mask.
// This is authoritative and must be persisted/replayed with terrain edits.
struct WaterBankSoilComponent {
    ShoreErosionCell soil{};
    uint32_t waveEnergyPermille=0;
    uint32_t wetContactPermille=0;
    uint32_t bankSlopePermille=0;
    ShoreErosionSettings settings{};
    bool enabled=true;
};

// Optional surface protection from grass, litter and recovering soil crust.
// Unlike ShoreErosionCell::rootBindingPermille, this does NOT claim grass
// has tree/shrub roots, and it doesn't alter sediment mass directly.
struct WaterBankSurfaceCoverComponent {
    uint32_t shieldPermille=0;
};
inline void io(Archive& a,WaterBankSurfaceCoverComponent& cover){
    a(cover.shieldPermille);
    if(!a.writing() && cover.shieldPermille>1000u)a.fail();
}

struct WaterBankErosionOutputComponent {
    int32_t beforeGroundCm=0,afterGroundCm=0;
    uint32_t removedCm=0,depositedCm=0;
    uint32_t sampledTick=0;
    bool changed=false;
};

// Pure NEREID update; no structural mutation or platform/time input.
// The engine owner reads output AFTER the ECS tick and publishes sparse
// dirt/collider/navmesh/vegetation changed regions in its normal barrier.
inline void addNereidBankErosionSystem(Simulation& sim,std::string name,
                                      uint32_t fixedDeltaMs=16u) {
    if(fixedDeltaMs==0 || fixedDeltaMs>1000)
        throw std::invalid_argument("NEREID bank erosion fixed timestep");
    sim.addSystem(std::move(name),[fixedDeltaMs](Simulation& current) {
        World& world=current.world();
        // pool<T> detaches shared ECS COW pool ownership. The older
        // prepareExistingWrite<T>() only prepares pages and is insufficient
        // for resource/rollback isolation when the pool itself is shared.
        auto* soils=&world.pool<WaterBankSoilComponent>();
        auto* output=&world.pool<WaterBankErosionOutputComponent>();
        const World& read=world;
        for(size_t i=0;i<output->size();++i) {
            const Entity entity=output->entityAt(i);
            if(!read.alive(entity))continue;
            auto& out=output->dataAt(i);
            out={};
            out.sampledTick=current.tick();
            auto* soil=soils->tryGet(entity.index);
            const auto* contact=read.tryGetConst<WaterContactComponent>(entity);
            if(!soil || !soil->enabled || !contact ||
               contact->sampledTick!=current.tick() ||
               !contact->sample.found)
                continue;
            const int32_t original=soil->soil.groundCm;
            ShoreErosionForcing forcing;
            forcing.flow=contact->sample.flow;
            forcing.waveEnergyPermille=soil->waveEnergyPermille;
            forcing.wetContactPermille=soil->wetContactPermille;
            forcing.bankSlopePermille=soil->bankSlopePermille;
            if(const auto* cover=read.tryGetConst<
                    WaterBankSurfaceCoverComponent>(entity))
                forcing.surfaceShieldPermille=cover->shieldPermille;
            const auto result=erodeShore(
                soil->soil,forcing,fixedDeltaMs,soil->settings);
            soil->soil=result.cell;
            out.beforeGroundCm=original;
            out.afterGroundCm=result.cell.groundCm;
            out.removedCm=result.removedCm;
            out.depositedCm=result.depositedCm;
            out.changed=result.changed;
        }
    });
}

inline void io(Archive& a,ShoreErosionCell& c) {
    a(c.soil,c.groundCm,c.bedrockCm,c.suspendedCm,
      c.erosionCarryMilliCm,c.depositionCarryMilliCm,
      c.rootBindingPermille);
}
inline void io(Archive& a,ShoreErosionSettings& s) {
    a(s.maxCutCmPerStep,s.maxDepositCmPerStep,s.ratePermille);
}
inline void io(Archive& a,WaterBankSoilComponent& c) {
    a(c.soil,c.waveEnergyPermille,c.wetContactPermille,
      c.bankSlopePermille,c.settings,c.enabled);
}
inline void io(Archive& a,WaterBankErosionOutputComponent& o) {
    a(o.beforeGroundCm,o.afterGroundCm,o.removedCm,
      o.depositedCm,o.sampledTick,o.changed);
}
inline void registerNereidBankErosionPersistence(Simulation& sim) {
    sim.registerPersistentComponent<WaterBankSoilComponent>(
        "nereid.bank_soil",1);
    sim.registerPersistentComponent<WaterBankErosionOutputComponent>(
        "nereid.bank_erosion_output",1);
    sim.registerPersistentComponent<WaterBankSurfaceCoverComponent>(
        "nereid.bank_surface_cover",1);
}

} // namespace aiverse::lib::nereid
