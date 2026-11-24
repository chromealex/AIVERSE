// ECS adapter for the existing deterministic character-locomotion policy.
// Provides velocity/state only. The collision/character owner integrates
// position and supplies grounded/ceiling feedback in a separate system.
#pragma once
#include "engine/mechanics/Namespace.h"
#include "engine/mechanics/EcsMechanicSystems.h"
#include "engine/mechanics/Locomotion.h"
#include <string>
#include <utility>

namespace aiverse::lib::mechanics {

struct LocomotionParametersComponent { MotionConfig value{}; };
struct LocomotionStateComponent { MotionState value{}; };
struct LocomotionInputComponent { MotionInput value{}; };
struct LocomotionOutputComponent {
    MotionResult value{};
    uint32_t tick=0;
    bool accepted=false;
};

// Register after validated command/AI input and collision environment sampling,
// before collision integration. Physics and transforms are NOT duplicated here.
// The same stepMotion() algorithm serves arbitrary genre-hosted entities.
inline void addLocomotionPolicySystem(Simulation& sim,std::string systemName) {
    sim.addSystem(std::move(systemName),[](Simulation& current) {
        World& world=current.world();
        auto* states=world.prepareExistingWrite<LocomotionStateComponent>();
        auto* inputs=world.prepareExistingWrite<LocomotionInputComponent>();
        auto* outputs=world.prepareExistingWrite<LocomotionOutputComponent>();
        if(!states||!inputs)return;
        const World& read=world;
        for(size_t i=0;i<states->size();++i) {
            const Entity entity=states->entityAt(i);
            if(!world.alive(entity))continue;
            const auto* config=read.tryGetConst<LocomotionParametersComponent>(entity);
            auto* input=inputs->tryGet(entity.index);
            if(!config || !input)continue;
            auto& state=states->dataAt(i).value;
            MotionState next=state;
            const auto result=stepMotion(config->value,next,input->value,
                                         current.tick(),current.dt());
            // A held movement wish persists; jumping is a single-tick edge.
            input->value.jumpPressed=false;
            if(auto* output=outputs?outputs->tryGet(entity.index):nullptr) {
                output->accepted=result.has_value();
                output->tick=current.tick();
                if(result)output->value=*result;
            }
            if(result)state=std::move(next);
        }
    });
}

} // namespace aiverse::lib::mechanics
