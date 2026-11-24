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
};
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
