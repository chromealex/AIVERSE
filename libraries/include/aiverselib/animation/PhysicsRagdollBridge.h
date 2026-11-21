// Optional Box3D-backed ragdoll runtime. Build with AIVERSE_ENABLE_PHYSICS=ON.
// The fixed-step owner controls advance; presentation reads immutable snapshots.
#pragma once
#include "RagdollAsset.h"
#include "engine/physics/World.h"
#include <stdexcept>
#include <utility>
namespace aiverse::lib::animation {
class RagdollPhysicsBridge {
public:
    explicit RagdollPhysicsBridge(RagdollAsset asset,int workers=1)
        :asset_(std::move(asset)),physics_(asset_.scene(),workers){}
    void step(){physics_.step();}
    uint64_t tick() const {return physics_.tick();}
    const physics::World& world()const{return physics_;}
    void sample(render::Pose& out) const {
        std::vector<RagdollBodyPose> bodies;
        bodies.reserve(asset_.bindings().size());
        for(const auto& binding:asset_.bindings()) {
            const auto* object=physics_.find(binding.id);
            if(!object) throw std::runtime_error("ragdoll missing body "+binding.id);
            const auto p=b3Body_GetPosition(object->body);
            const auto q=b3Body_GetRotation(object->body);
            bodies.push_back({binding.id,{p.x,p.y,p.z},
                render::quat{q.v.x,q.v.y,q.v.z,q.s}});
        }
        asset_.applyBodyPoses(bodies,out);
    }
private:
    RagdollAsset asset_;
    physics::World physics_;
};
} // namespace aiverse::lib::animation
