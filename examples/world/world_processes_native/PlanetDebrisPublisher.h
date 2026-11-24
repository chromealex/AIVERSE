// Optional native Box3D projection of a committed FRACTURE consequence.
// No second authoritative debris store: owner supplies its durable shape,
// pose, velocity, and identity. Only transient Box3D handles live here.
#pragma once
#include "aiverselib/world/PlanetFractureDelivery.h"
#include "engine/physics/PhysisBox3D.h"
#include <map>
#include <set>

namespace aiverse::examples::world_processes_native {
struct PlanetDebrisProjection {
    fracture::Event event{};
    std::string bodyId;
    Vec3 simulationPosition{}; // durable owner pose, canonical Fixed metres
    physics::BodyDesc description{}; // owner rotation/shape/velocities/material
    bool awake=true; // restored owner sleep state, not a separate physics policy
};
// Handle-free owner checkpoint. Native authority only; never resample on
// deterministic lockstep peers. Shape/material remain in the owner record.
struct PlanetDebrisState {
    fracture::Event event{};
    std::string bodyId;
    Vec3 simulationPosition{};
    b3Quat rotation=b3Quat_identity;
    b3Vec3 linearVelocity{},angularVelocity{};
    bool awake=true;
};
inline bool validPlanetDebrisMotion(const PlanetDebrisState& state){
    const auto finiteVelocity=[](b3Vec3 v){
        return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z)&&
            std::abs(v.x)<=1000&&std::abs(v.y)<=1000&&std::abs(v.z)<=1000;
    };
    const auto q=state.rotation;
    const float norm=q.v.x*q.v.x+q.v.y*q.v.y+q.v.z*q.v.z+q.s*q.s;
    return state.event.entity.valid()&&state.event.key&&!state.bodyId.empty()&&
        std::isfinite(norm)&&std::abs(norm-1.f)<=.01f&&
        finiteVelocity(state.linearVelocity)&&finiteVelocity(state.angularVelocity);
}
// Merge a saved motion checkpoint into the EXISTING owner shape record.
// Identity mismatch cannot restore a different collapse or another planet.
inline bool restorePlanetDebrisMotion(PlanetDebrisProjection& owner,const PlanetDebrisState& state){
    if(!validPlanetDebrisMotion(state)||owner.event.entity!=state.event.entity||
       owner.event.key!=state.event.key||owner.event.tick!=state.event.tick||
       owner.bodyId!=state.bodyId)return false;
    owner.simulationPosition=state.simulationPosition;
    owner.description.rotation=state.rotation;
    owner.description.linearVelocity=state.linearVelocity;
    owner.description.angularVelocity=state.angularVelocity;
    owner.awake=state.awake;return true;
}
class PlanetDebrisPublisher {
public:
    // One selected body and one real physics scene. Source collider removal
    // and persisted debris pose belong to the caller, not this projection.
    // Repeated exact receipt is idempotent; a new event on the same entity
    // requires explicit retirement/reconciliation first.
    bool publish(physics::Scene& scene,const World& world,const game::Surface& surface,
                 const PlanetDebrisProjection& request){
        if(scene_&&scene_!=&scene)return false;
        if(!bodyId_.empty()&&bodyId_!=request.bodyId)return false;
        if(!fracture::planetCollapseReceipt(world,request.event)||
           request.description.motion!=physics::Motion::Dynamic||
           request.description.shape==physics::ShapeKind::Mesh||
           request.description.sensor)return false;
        const auto frame=lib::worldprocess::surfaceFrame(&surface,request.simulationPosition);
        if(!frame||surface.bodies[size_t(frame->body)].id!=request.bodyId)return false;
        const auto offset=game::Surface::anchor(frame->body);
        if(scene_&&offset_!=offset)return false;
        const auto id=request.event.entity.packed();
        const auto old=entries_.find(id);
        if(old!=entries_.end()){
            // A lost externally destroyed handle must be recovered from
            // owner durable pose, not silently recreated from an old request.
            return old->second.event.key==request.event.key&&
                old->second.event.tick==request.event.tick&&scene.valid(old->second.handle);
        }
        if(entries_.size()>=fracture::kMaxMembers)return false;
        auto desc=request.description;
        desc.position=physics::physisB3(request.simulationPosition-offset);
        // Let the existing PHYSIS bridge compensate native gravity once.
        const auto handle=scene.create(desc);
        if(handle==physics::kNoBody)return false;
        if(!request.awake&&!scene.setAwake(handle,false)){
            scene.destroy(handle);return false;
        }
        try{
            entries_.emplace(id,Entry{request.event,handle});
            bodyId_=request.bodyId;
        }catch(...){
            entries_.erase(id);scene.destroy(handle);throw;
        }
        scene_=&scene;offset_=offset;
        return true;
    }
    // Before each Box3D step, validate all receipts/handles and coordinates.
    // If force application fails after an earlier body was updated, host
    // must abort/reconcile that physics step rather than retrying forces.
    bool applyGravity(physics::Scene& scene,const World& world,const game::Surface& surface,
                      const physics::PhysisBodyMaterial& material={}){
        if(!scene_)return true;
        if(scene_!=&scene)return false;
        for(const auto& [id,entry]:entries_){
            (void)id;
            if(!scene.valid(entry.handle)||!fracture::planetCollapseReceipt(world,entry.event))return false;
            Vec3 local{};
            if(!physics::physisVec3(scene.position(entry.handle),local))return false;
            const auto frame=lib::worldprocess::surfaceFrame(&surface,local+offset_);
            if(!frame||surface.bodies[size_t(frame->body)].id!=bodyId_)return false;
        }
        for(const auto& [id,entry]:entries_){
            (void)id;
            if(!physics::applyPhysisToBody(scene,world,entry.handle,material,offset_))return false;
        }
        return true;
    }
    // Copy all current motion in stable entity order after the sole scene step.
    // Fail closed on a missing receipt, lost handle, out-of-body position or
    // non-restorable native motion. Output is unchanged on failure.
    bool capture(const physics::Scene& scene,const World& world,const game::Surface& surface,
                 std::vector<PlanetDebrisState>& output)const{
        if(scene_&&scene_!=&scene)return false;
        std::vector<PlanetDebrisState> staged;staged.reserve(entries_.size());
        for(const auto& [id,entry]:entries_){
            (void)id;
            if(!scene.valid(entry.handle)||scene.motion(entry.handle)!=physics::Motion::Dynamic||
               !fracture::planetCollapseReceipt(world,entry.event))return false;
            const auto p=scene.position(entry.handle);
            Vec3 local{};
            if(!physics::physisVec3(p,local)||std::abs(p.x)>100000||
               std::abs(p.y)>100000||std::abs(p.z)>100000)return false;
            PlanetDebrisState state{entry.event,bodyId_,local+offset_,
                scene.rotation(entry.handle),scene.linearVelocity(entry.handle),
                scene.angularVelocity(entry.handle),scene.awake(entry.handle)};
            const auto frame=lib::worldprocess::surfaceFrame(&surface,state.simulationPosition);
            if(!frame||surface.bodies[size_t(frame->body)].id!=bodyId_||
               game::Surface::anchor(frame->body)!=offset_||!validPlanetDebrisMotion(state))return false;
            staged.push_back(std::move(state));
        }
        output=std::move(staged);return true;
    }
    physics::BodyHandle handle(Entity e)const{
        const auto it=entries_.find(e.packed());
        return it==entries_.end()?physics::kNoBody:it->second.handle;
    }
    bool retire(physics::Scene& scene,Entity e){
        if(scene_&&scene_!=&scene)return false;
        const auto it=entries_.find(e.packed());
        if(it==entries_.end())return true;
        if(scene.valid(it->second.handle)&&!scene.destroy(it->second.handle))return false;
        entries_.erase(it);return true;
    }
    // Rollback/body switch: owner restores durable consequences first,
    // resets projection, then republishes from restored authoritative poses.
    bool reset(physics::Scene& scene){
        if(scene_&&scene_!=&scene)return false;
        for(auto it=entries_.begin();it!=entries_.end();){
            if(scene.valid(it->second.handle)&&!scene.destroy(it->second.handle))return false;
            it=entries_.erase(it);
        }
        scene_=nullptr;bodyId_.clear();offset_={};return true;
    }
private:
    struct Entry {fracture::Event event;physics::BodyHandle handle;};
    std::map<uint64_t,Entry> entries_;
    physics::Scene* scene_=nullptr;
    std::string bodyId_;
    Vec3 offset_{};
};
} // namespace aiverse::examples::world_processes_native
