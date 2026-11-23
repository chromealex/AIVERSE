// Native CIRCUIT actuators: consume authoritative device transitions and
// apply them to existing Box3D doors, light/FX callbacks and gameplay hooks.
// Gameplay device state is stored only in ECS; this cache is presentation
// reconciliation and is invalidated after rollback/replay.
#pragma once
#include "aiverselib/world/CircuitDevices.h"
#include "engine/physics/Scene.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <vector>

namespace aiverse::examples::world_processes_native {
struct CircuitDoorBinding {
    physics::BodyHandle body=physics::kNoBody;
    b3Vec3 closedPosition{},openPosition{};
    b3Quat rotation=b3Quat_identity;
};
struct CircuitDeviceAction {
    Entity device=kNullEntity;
    circuit::DeviceKind kind=circuit::DeviceKind::Generic;
    bool active=false,valid=false;
    std::uint32_t tick=0,transition=0;
};

class CircuitDeviceHost {
public:
    // Called by the existing graphics/gameplay owner. Light intensity, VFX,
    // sounds, pump water, alarms and animation remain their normal systems.
    using Callback=std::function<bool(const CircuitDeviceAction&)>;
    void bindDoor(Entity e,CircuitDoorBinding binding) {
        doors_.insert_or_assign(e.packed(),binding);
    }
    void unbindDoor(Entity e){doors_.erase(e.packed());acknowledged_.erase(e.packed());}
    void setCallback(Callback callback){callback_=std::move(callback);}
    // Called after CIRCUIT.device_outputs tick, before the relevant
    // Box3D/renderer owner frame. Returns false on any rejected actuation.
    // A later retry reapplies the desired state (idempotent kinematic target).
    bool reconcile(const World& ecs,std::uint32_t completedTick,
                   physics::Scene* physicsScene=nullptr,bool force=false){
        std::vector<std::pair<Entity,circuit::Device>> updates;
        ecs.eachExistingConst<circuit::Device>([&](Entity e,const circuit::Device& d){
            if(ecs.alive(e))updates.push_back({e,d});
        });
        std::sort(updates.begin(),updates.end(),
            [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
        std::set<std::uint64_t> live;
        for(const auto& [e,d]:updates){
            if(!circuit::valid(d)||d.sampledTick!=completedTick)return false;
            const auto key=e.packed();
            live.insert(key);
            const auto it=acknowledged_.find(key);
            if(!force&&it!=acknowledged_.end()&&
               it->second==std::make_pair(d.transition,d.active))continue;
            const CircuitDeviceAction action{
                e,d.kind,d.active,d.valid,completedTick,d.transition};
            if(d.kind==circuit::DeviceKind::Door){
                if(const auto door=doors_.find(key);door!=doors_.end()){
                    if(!physicsScene||!physicsScene->valid(door->second.body)||
                       physicsScene->motion(door->second.body)!=
                           physics::Motion::Kinematic)return false;
                    const auto& destination=d.active?
                        door->second.openPosition:door->second.closedPosition;
                    if(!physicsScene->moveKinematic(door->second.body,
                        destination,door->second.rotation))return false;
                }
            }
            if(callback_&&!callback_(action))return false;
            acknowledged_[key]={d.transition,d.active};
        }
        for(auto it=acknowledged_.begin();it!=acknowledged_.end();)
            it=live.contains(it->first)?std::next(it):acknowledged_.erase(it);
        return true;
    }
    void resetAfterRollback(){acknowledged_.clear();}
    std::size_t acknowledgedDevices()const{return acknowledged_.size();}
private:
    std::map<std::uint64_t,CircuitDoorBinding> doors_;
    std::map<std::uint64_t,std::pair<std::uint32_t,bool>> acknowledged_;
    Callback callback_;
};
} // namespace aiverse::examples::world_processes_native
