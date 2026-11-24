// NEREID opt-in bridge: authoritative ECS interaction outputs -> presentation.
// Read-only and genre-neutral; downstream SpectraFX/Audio chooses effects.
// Never consumes or resets ECS output and never assumes the water's RHI.
#pragma once
#include "NereidInteractions.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::lib::nereid {

struct WaterPresentationEvent {
    Entity source=kNullEntity;
    WaterInteractionOutputComponent event{};
    Vec2 movementDirection{};
    Fixed travelled{};
};

// Call exactly once per presented simulation tick after water.interaction.
// Stable ordering ensures all visual/audio consumers agree on an event
// sequence even if ECS pool insertion order differs across snapshots.
inline std::vector<WaterPresentationEvent> snapshotWaterInteractionEvents(
        const World& world,uint32_t completedTick,size_t limit=4096) {
    std::vector<WaterPresentationEvent> result;
    world.eachExistingConst<WaterInteractionOutputComponent>(
        [&](Entity entity,const WaterInteractionOutputComponent& output) {
            if(!world.alive(entity) ||
               output.tick!=completedTick ||
               output.kind==WaterInteractionKind::None)return;
            if(result.size()>=limit)
                throw std::invalid_argument("nereid: interaction event snapshot limit exceeded");
            WaterPresentationEvent presentation;
            presentation.source=entity;
            presentation.event=output;
            if(const auto* motion=world.tryGetConst<WaterWakeDirectionComponent>(entity)) {
                if(motion->sampledTick==completedTick) {
                    presentation.movementDirection=motion->direction;
                    presentation.travelled=motion->travelled;
                }
            }
            result.push_back(presentation);
        });
    std::sort(result.begin(),result.end(),
        [](const WaterPresentationEvent& a,const WaterPresentationEvent& b) {
            return a.source.packed()<b.source.packed();
        });
    return result;
}

} // namespace aiverse::lib::nereid
