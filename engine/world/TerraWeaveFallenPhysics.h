// TerraWeave: optional Box3D falling trees/shrubs with stable plant identity.
// Only authoritative native hosts opt into Box3D; deterministic ground/root
// events stay integer and can be saved/replicated independently of float sim.
#pragma once
#include "engine/world/TerraWeaveRootEvents.h"
#include "engine/world/TerraWeaveForestScene.h"
#include "engine/world/TerraWeaveDeadwoodPresentation.h"
#include "engine/physics/Scene.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct FallenRenderOriginCm {
    int64_t x=0,y=0,z=0;
};

struct FallenPlantBodySpec {
    uint32_t heightCm=800;       // configure per species/model
    uint32_t radiusCm=24;
    uint32_t modelScalePermille=1000;
    uint32_t initialTiltMilliDegrees=12000;
    float density=650.0f;        // Box3D density is host presentation/physics
    float angularSpeed=0.9f;     // radians/sec along fall axis
    float friction=0.8f;
};

inline void validateFallenPlantBodySpec(const FallenPlantBodySpec& s){
    if(s.heightCm<30 || s.heightCm>100000 ||
       s.radiusCm<1 || s.radiusCm>10000 ||
       s.radiusCm*2u>=s.heightCm ||
       s.modelScalePermille<1 || s.modelScalePermille>10000 ||
       s.initialTiltMilliDegrees<1000 ||
       s.initialTiltMilliDegrees>40000 ||
       !std::isfinite(s.density) || s.density<0.01f ||
       s.density>100000.f || !std::isfinite(s.angularSpeed) ||
       s.angularSpeed<0 || s.angularSpeed>20.f ||
       !std::isfinite(s.friction) ||
       s.friction<0 || s.friction>2.f)
        throw std::invalid_argument("TerraWeave invalid fallen-plant physics");
}

struct FallenPlantBodyInfo {
    AnchoredPlantId id{};
    physics::BodyHandle body=physics::kNoBody;
    uint64_t fallTick=0;
    uint64_t soilRevision=0;
    uint32_t heightCm=0;
    uint32_t modelScalePermille=1000;
};

class FallenPlantPhysics {
public:
    explicit FallenPlantPhysics(physics::Scene& scene)
        :scene_(scene) {}

    static constexpr size_t kMaxFallenBodies=8192;
    size_t count()const{return bodies_.size();}
    physics::BodyHandle body(AnchoredPlantId id)const {
        const auto it=bodies_.find(id);
        return it==bodies_.end() ? physics::kNoBody : it->second.body;
    }
    std::vector<FallenPlantBodyInfo> snapshot()const{
        std::vector<FallenPlantBodyInfo> out;
        out.reserve(bodies_.size());
        for(const auto& [id,entry]:bodies_){
            (void)id;out.push_back(entry);
        }
        return out;
    }

    // One event creates one persistent dynamic body. A restored/replayed
    // identical event never spawns a second tree. The native server owns
    // Box3D::step, replication and optional persistable motion snapshots.
    physics::BodyHandle spawn(const PlantFallEvent& fall,
                              FallenPlantBodySpec spec,
                              TerraWeaveForestScene* forest=nullptr){
        validateFallenPlantBodySpec(spec);
        if(!fall.id.speciesId || !fall.id.key ||
           fall.habit==PlantHabit::Grass ||
           fall.xCm<-kWorldLimitCm || fall.xCm>kWorldLimitCm ||
           fall.zCm<-kWorldLimitCm || fall.zCm>kWorldLimitCm ||
           fall.supportPermille>1000u)
            throw std::invalid_argument("TerraWeave invalid plant fall event");
        if(auto existing=bodies_.find(fall.id);existing!=bodies_.end()){
            if(scene_.valid(existing->second.body))
                return existing->second.body;
            bodies_.erase(existing); // stale handle after external reset
        }
        if(bodies_.size()>=kMaxFallenBodies)
            throw std::length_error("TerraWeave fallen-body budget");

        int32_t fx=fall.fallDirectionXPermille;
        int32_t fz=fall.fallDirectionZPermille;
        if(!fx && !fz){
            const uint64_t h=detail::mix64(
                fall.id.key^(uint64_t(fall.id.speciesId)<<32));
            static constexpr int32_t dirs[8][2]={
                {1000,0},{707,707},{0,1000},{-707,707},
                {-1000,0},{-707,-707},{0,-1000},{707,-707}
            };
            fx=dirs[h%8u][0];fz=dirs[h%8u][1];
        }
        const float dx=float(fx)*0.001f,dz=float(fz)*0.001f;
        const float length=std::sqrt(dx*dx+dz*dz);
        if(length<0.001f || !std::isfinite(length))
            throw std::invalid_argument("TerraWeave invalid fall direction");
        const float ux=dx/length,uz=dz/length;
        const float height=float(spec.heightCm)*
            float(spec.modelScalePermille)*0.00001f;
        const float radius=float(spec.radiusCm)*
            float(spec.modelScalePermille)*0.00001f;
        if(radius>100.f || height>10000.f ||
           height<=2.f*radius)
            throw std::out_of_range(
                "TerraWeave falling plant exceeds Box3D body bounds");
        constexpr float kRadPerMilliDegree=0.000017453292519943295f;
        const float tilt=float(spec.initialTiltMilliDegrees)*
            kRadPerMilliDegree;
        // Rotate world +Y toward the failing-bank direction by a small
        // initial tilt. Gravity and angular velocity carry the remaining
        // topple; the physics scene determines actual contacts.
        const b3Vec3 axis{uz,0,-ux};
        physics::BodyDesc desc;
        desc.motion=physics::Motion::Dynamic;
        desc.shape=physics::ShapeKind::Capsule;
        desc.radius=radius;
        desc.halfHeight=std::max(0.01f,height*0.5f-radius);
        desc.position={
            float(fall.xCm)*0.01f+ux*(height*0.5f)*std::sin(tilt),
            float(fall.plantedHeightCm)*0.01f+
                (height*0.5f)*std::cos(tilt),
            float(fall.zCm)*0.01f+uz*(height*0.5f)*std::sin(tilt)
        };
        desc.rotation=b3MakeQuatFromAxisAngle(axis,tilt);
        desc.angularVelocity={
            axis.x*spec.angularSpeed,
            axis.y*spec.angularSpeed,
            axis.z*spec.angularSpeed
        };
        desc.density=spec.density;
        desc.friction=spec.friction;
        desc.linearDamping=0.08f;
        desc.angularDamping=0.12f;
        desc.category=physics::kCategoryDynamic;
        desc.sleep=true;
        const auto created=scene_.create(desc);
        if(created==physics::kNoBody)return created;

        // Reserve the identity record before changing forest visibility.
        // Rejected map allocation/tree hide can clean up the newly created
        // body without leaving a body that has no authoritative identity.
        try {
            const bool inserted=bodies_.emplace(fall.id,
                FallenPlantBodyInfo{fall.id,created,fall.tick,
                                    fall.sourceSoilRevision,
                                    spec.heightCm,spec.modelScalePermille}).second;
            if(!inserted)throw std::logic_error("TerraWeave duplicated falling tree");
            if(forest)forest->removeTree({fall.id.key,fall.id.speciesId});
        }catch(...){
            bodies_.erase(fall.id);
            scene_.destroy(created);
            throw;
        }
        return created;
    }

    // Tree can be cut down, harvested, removed from the world or replanted.
    // Replanting is a GAME decision: call RootSupportRegistry::resetFall too.
    bool erase(AnchoredPlantId id,TerraWeaveForestScene* forest=nullptr,
               bool restoreStanding=false) {
        const auto it=bodies_.find(id);
        if(it==bodies_.end())return false;
        if(forest && restoreStanding)
            forest->restoreTree({id.key,id.speciesId});
        if(scene_.valid(it->second.body))
            scene_.destroy(it->second.body);
        bodies_.erase(it);
        return true;
    }

    // No per-frame mesh rebuild. Reuse existing Arbor species prototype and
    // the current Box3D transform. Caller provides a stable, uploaded
    // render::Model per species. One draw per INTERACTABLE fallen tree;
    // grass/shrub cosmetic patches remain grouped independently.
    template<class Prototype>
    void submit(render::Renderer3D& renderer,
                FallenRenderOriginCm origin,
                Prototype&& prototype,
                const ForestDeadwoodResource* deadwood=nullptr)const {
        for(const auto& [id,item]:bodies_){
            if(!scene_.valid(item.body))continue;
            const auto appearance=deadwood?
                deadwoodVisualState(*deadwood,id):DeadwoodVisualState{};
            if(appearance.tier==DeadwoodVisualTier::Gone)continue;
            const auto* model=prototype(id.speciesId);
            if(!model)continue;
            const b3Vec3 p=scene_.position(item.body);
            const b3Quat q=scene_.rotation(item.body);
            render::ModelDraw draw;
            draw.model=model;
            // O(log N) lookup, one model prototype per species and one
            // existing ModelDraw tint per instance. Alpha stays opaque:
            // no extra transparent pass or per-tree geometry allocation.
            draw.tint={
                float(appearance.tintRPermille)*0.001f,
                float(appearance.tintGPermille)*0.001f,
                float(appearance.tintBPermille)*0.001f,1.0f};
            draw.persistentId=detail::mix64(
                id.key^(uint64_t(id.speciesId)<<32)^0x5ab0fa11bade0011ULL);
            if(!draw.persistentId)draw.persistentId=1;
            const render::quat orientation{q.v.x,q.v.y,q.v.z,q.s};
            const float scale=float(item.modelScalePermille)*0.001f;
            const float height=float(item.heightCm)*0.01f;
            draw.world=render::mat4::translate({
                p.x-float(origin.x)*0.01f,
                p.y-float(origin.y)*0.01f,
                p.z-float(origin.z)*0.01f
            })*render::mat4::rotate(orientation)*
              render::mat4::translate({0,-height*scale*0.5f,0})*
              render::mat4::scale({scale,scale,scale});
            // A falling body is dynamic; never mark it kDrawStatic.
            renderer.drawModel(draw);
        }
    }

    void shutdown(){
        for(const auto& [id,entry]:bodies_) {
            (void)id;
            if(scene_.valid(entry.body))scene_.destroy(entry.body);
        }
        bodies_.clear();
    }

private:
    physics::Scene& scene_;
    std::map<AnchoredPlantId,FallenPlantBodyInfo> bodies_;
};

} // namespace aiverse::terraweave
