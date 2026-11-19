// TerraWeave Stage63: opt-in automatic shoreline/bank refresh when NEREID
// floodwater reaches/drains grid cells or canonical Earthworks terrain moves.
// All water movement is computed by NEREID FloodRouting; this module ONLY
// materializes/updates persistent bank ECS components at the system barrier.
#pragma once
#include "engine/world/TerraWeaveShorelineRaster.h"
#include "engine/world/TerraWeaveNereidFloodBridge.h"
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>

namespace aiverse::terraweave {

struct AutomaticShorelineRevision {
    uint32_t version=1;
    uint64_t lastSoilRevision=0;
    uint64_t lastFloodWetnessRevision=0;
    bool initialized=false;
};
inline void io(Archive& a,AutomaticShorelineRevision& r){
    a(r.version,r.lastSoilRevision,r.lastFloodWetnessRevision,r.initialized);
    if(!a.writing() && r.version!=1)a.fail();
}
inline void registerAutomaticShorelineRevisionPersistence(Simulation& sim){
    sim.registerPersistentResource<AutomaticShorelineRevision>(
        "terraweave.auto_shoreline_revision",1);
}
inline void requestAutomaticShorelineRefresh(World& ecs) {
    auto previous=ecs.resourceConst<AutomaticShorelineRevision>();
    AutomaticShorelineRevision next=
        previous?*previous:AutomaticShorelineRevision{};
    next.initialized=false;
    ecs.setResource<AutomaticShorelineRevision>(std::move(next));
}

// Install as the LAST water/soil owner-system: NEREID flood routes first,
// native NEREID bank erosion moves water-carried sediment, TerraWeave applies
// grouped ground slides, TerraWeaveFloodGroundSync updates column bottoms,
// THEN generated bank cells may be added/disabled for the NEXT ECS tick.
// Rerasterization occurs on meaningful topology changes, not each frame.
inline void addAutomaticNereidShorelineRefreshSystem(
        Simulation& sim,std::string name,
        TerraWeaveEcsAuthorityConfig cfg,
        ShorelineRasterSettings scan,
        ShorelineSoilAt material={},
        ShorelineRootsAt roots={}) {
    validateEcsAuthorityConfig(cfg);
    sim.addSystem(std::move(name),
        [cfg=std::move(cfg),scan,
         material=std::move(material),
         roots=std::move(roots)](Simulation& current) {
            World& ecs=current.world();
            const auto* authority=ecs.resourceConst<TerraWeaveAuthorityResource>();
            if(!authority)return;
            const auto* flood=ecs.resourceConst<lib::nereid::WaterFloodResource>();
            const auto* previous=ecs.resourceConst<AutomaticShorelineRevision>();
            const uint64_t wet=flood?flood->wetnessRevision:0;
            if(previous && previous->initialized &&
               previous->lastSoilRevision==authority->earth.revision &&
               previous->lastFloodWetnessRevision==wet)return;
            const auto earth=materializeEcsTerrain(*authority,cfg);
            const auto candidates=discoverNereidShoreline(
                ecs,earth,scan,material,roots);
            // This routine is intentionally called outside any water query
            // index lifetime; all registration is structural ECS ownership.
            (void)refreshAutoShorelineBanks(ecs,candidates);
            AutomaticShorelineRevision next;
            next.initialized=true;
            next.lastSoilRevision=authority->earth.revision;
            next.lastFloodWetnessRevision=wet;
            ecs.setResource<AutomaticShorelineRevision>(std::move(next));
        });
}

} // namespace aiverse::terraweave
