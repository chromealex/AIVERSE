// TerraWeave Stage 62: bounded Box3D rigid debris spawned by deterministic
// slope-failure events. Ground mass already moved in Earthworks: these are
// optional visual/collision fragments, NOT a second soil-mass simulation.
// Physics floats are server-owned; replicate transforms, never rely on
// cross-platform Box3D lockstep. Spawn/step/prune on Scene owner thread.
#pragma once
#include "engine/world/TerraWeaveSlopeFailure.h"
#include "engine/physics/Scene.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <compare>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct SlopeDebrisId {
    uint64_t tick=0;
    EarthVertex from{},to{};
    constexpr auto operator<=>(const SlopeDebrisId&)const=default;
};

struct SlopeDebrisVisual {
    SlopeDebrisId id{};
    physics::BodyHandle body=physics::kNoBody;
    lib::nereid::ShoreSoil soil=lib::nereid::ShoreSoil::Loam;
    uint32_t movedCm=0;
    uint32_t radiusCm=0;
    uint64_t expiresTick=0;
};

struct SlopeDebrisPolicy {
    uint32_t maxResident=256;    // 1..1024 for bounded Box3D instances
    uint32_t maxNewPerTick=64;   // 1..128 deterministic event selection
    uint32_t lifetimeTicks=900; // 1..360000; old transient debris is retired
    uint32_t minimumMoveCm=2;   // tiny soil displacements remain terrain-only
    float density=1200.f;
};

inline void validateSlopeDebrisPolicy(const SlopeDebrisPolicy& p){
    if(p.maxResident<1 || p.maxResident>1024 ||
       p.maxNewPerTick<1 || p.maxNewPerTick>128 ||
       p.lifetimeTicks<1 || p.lifetimeTicks>360000 ||
       p.minimumMoveCm<1 || p.minimumMoveCm>500 ||
       !std::isfinite(p.density) || p.density<0.01f ||
       p.density>100000.f)
        throw std::invalid_argument("TerraWeave debris physics policy");
}

class SlopeDebrisPhysics {
public:
    explicit SlopeDebrisPhysics(physics::Scene& scene,
                               SlopeDebrisPolicy policy={})
        :scene_(scene),policy_(policy){
        validateSlopeDebrisPolicy(policy_);
    }

    size_t count()const{return bodies_.size();}
    std::vector<SlopeDebrisVisual> snapshot()const{
        std::vector<SlopeDebrisVisual> result;
        result.reserve(bodies_.size());
        for(const auto& [id,item]:bodies_){
            (void)id;result.push_back(item);
        }
        return result;
    }

    // At most maxNewPerTick fragments from one mass-conserving slide plan.
    // Input order is normalized by canonical source/destination coordinates.
    // Existing event IDs are idempotent for reliable server event delivery.
    uint32_t spawn(const SlopeFailurePlan& failure,
                   const EarthworksLayer& ground,uint64_t completedTick){
        if(failure.slides.size()>4096u)
            throw std::length_error("TerraWeave debris slide input budget");
        std::vector<SlopeFailureSlide> ordered=failure.slides;
        std::sort(ordered.begin(),ordered.end(),
            [](const auto& a,const auto& b){
                if(a.from<b.from)return true;
                if(b.from<a.from)return false;
                return a.to<b.to;
            });
        uint32_t spawned=0;
        for(const auto& slide:ordered){
            if(spawned>=policy_.maxNewPerTick ||
               bodies_.size()>=policy_.maxResident)break;
            if(slide.movedCm<policy_.minimumMoveCm)continue;
            if(slide.movedCm>500 || slide.from==slide.to ||
               slide.soil==lib::nereid::ShoreSoil::Rock)
                throw std::invalid_argument("TerraWeave invalid debris source slide");
            const int64_t spacing=ground.spacingCm();
            const int64_t limit=(kWorldLimitCm-spacing)/spacing;
            if(slide.from.x<-limit || slide.from.x>limit ||
               slide.from.z<-limit || slide.from.z>limit ||
               slide.to.x<-limit || slide.to.x>limit ||
               slide.to.z<-limit || slide.to.z>limit)
                throw std::out_of_range(
                    "TerraWeave debris source outside terrain grid");
            const SlopeDebrisId id{completedTick,slide.from,slide.to};
            if(auto it=bodies_.find(id);it!=bodies_.end()){
                if(scene_.valid(it->second.body))continue;
                bodies_.erase(it);
            }
            if(completedTick>UINT64_MAX-policy_.lifetimeTicks)
                throw std::overflow_error("TerraWeave debris lifetime tick overflow");
            const int64_t sx=slide.to.x*spacing;
            const int64_t sz=slide.to.z*spacing;
            if(sx<=-10000000 || sx>=10000000 ||
               sz<=-10000000 || sz>=10000000)continue;
            const int32_t y=ground.heightAt(slide.to);
            if(y<=-10000000 || y>=10000000)continue;
            const uint32_t radiusCm=uint32_t(std::clamp<int64_t>(
                int64_t(slide.movedCm)/3+5,5,60));
            const float radius=float(radiusCm)*0.01f;
            const float vx=float(slide.to.x-slide.from.x);
            const float vz=float(slide.to.z-slide.from.z);
            physics::BodyDesc desc;
            desc.motion=physics::Motion::Dynamic;
            desc.shape=physics::ShapeKind::Box;
            desc.size={radius*2,radius*2,radius*2};
            desc.position={
                float(sx)*0.01f,
                float(y+int32_t(radiusCm)+4)*0.01f,
                float(sz)*0.01f};
            desc.linearVelocity={vx*0.45f,0.15f,vz*0.45f};
            desc.density=policy_.density;
            desc.friction=0.85f;
            desc.restitution=0.04f;
            desc.sleep=true;
            const auto handle=scene_.create(desc);
            if(handle==physics::kNoBody)break;
            try {
                if(!bodies_.emplace(id,SlopeDebrisVisual{
                        id,handle,slide.soil,slide.movedCm,radiusCm,
                        completedTick+policy_.lifetimeTicks}).second)
                    throw std::logic_error("TerraWeave duplicate debris identity");
            }catch(...){scene_.destroy(handle);throw;}
            ++spawned;
        }
        return spawned;
    }

    // Retire old fragments at an authoritative server tick, not a camera
    // distance/graphics frame. Deleting these shapes changes Box3D contacts
    // and is NOT a gameplay terrain mass edit.
    uint32_t prune(uint64_t completedTick){
        uint32_t removed=0;
        for(auto it=bodies_.begin();it!=bodies_.end();){
            if(it->second.expiresTick>completedTick &&
               scene_.valid(it->second.body)){++it;continue;}
            if(scene_.valid(it->second.body))
                scene_.destroy(it->second.body);
            it=bodies_.erase(it);
            ++removed;
        }
        return removed;
    }

    // Borrow one stable uploaded prototype per soil material from the
    // native game. One dynamic draw per debris body, within maxResident.
    template<class ModelForSoil>
    void submit(render::Renderer3D& renderer,Vec3 visualOrigin,
                ModelForSoil&& modelForSoil)const {
        for(const auto& [key,item]:bodies_){
            (void)key;
            if(!scene_.valid(item.body))continue;
            const auto* model=modelForSoil(item.soil);
            if(!model)continue;
            const b3Vec3 p=scene_.position(item.body);
            const b3Quat q=scene_.rotation(item.body);
            render::ModelDraw draw;
            draw.model=model;
            draw.world=render::mat4::translate({
                p.x-float(visualOrigin.x.toDouble()),
                p.y-float(visualOrigin.y.toDouble()),
                p.z-float(visualOrigin.z.toDouble())
            }) * render::mat4::rotate(
                render::quat{q.v.x,q.v.y,q.v.z,q.s}) *
                render::mat4::scale({
                    float(item.radiusCm)*0.02f,
                    float(item.radiusCm)*0.02f,
                    float(item.radiusCm)*0.02f});
            renderer.drawModel(draw);
        }
    }

    void shutdown(){
        for(const auto& [id,entry]:bodies_){
            (void)id;
            if(scene_.valid(entry.body))scene_.destroy(entry.body);
        }
        bodies_.clear();
    }

private:
    physics::Scene& scene_;
    SlopeDebrisPolicy policy_{};
    std::map<SlopeDebrisId,SlopeDebrisVisual> bodies_;
};

} // namespace aiverse::terraweave
