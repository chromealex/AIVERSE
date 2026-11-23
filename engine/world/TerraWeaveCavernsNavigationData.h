#pragma once
// TerraWeave Caverns Stage41: shared navigation DATA provider, NOT a pathfinder.
// A*/HPA*/FlowField/Hybrid, nav::Mesh and custom solvers consume the same
// revisioned source and deterministic topology. No RTS/FPS or renderer deps.
// Coordinates in signed centimetres; no floating-point gameplay decisions.
#include "engine/world/TerraWeaveCavernsSdfSpatialIndex.h"
#include "engine/world/TerraWeaveCavernsRaycast.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {

enum class CaveTraversal : uint8_t {
    Ground, Flying, Swimming, Climbing, Custom
};
struct CaveAgentProfile {
    CaveTraversal traversal=CaveTraversal::Ground;
    int32_t radiusCm=30;
    int32_t heightCm=180;
    int32_t maxStepCm=45;
    // Ground/surface queries: sample up to this far below the cell centre
    // to detect a support floor. Flying ignores support.
    int32_t groundProbeCm=100;
    int32_t sampleCm=25;
    uint32_t areaMask=0xffffffffu;
};
struct CaveNavigationCell {
    PointCm centre{};
    bool traversable=false;
    bool hasFloor=false;
    bool flooded=false;
    uint16_t area=0;
    uint32_t cost=10;  // integer solver cost; never negative
};
struct CaveNavigationChange {
    uint64_t fromRevision=0,toRevision=0;
    CaveDirtyBounds bounds{}; // half-open, with caller-provided halo
    bool fullRebuild=false;
};
struct CaveNavigationCallbacks {
    // Water, hazards, climbable surfaces and custom costs belong to their
    // existing engine owners (NEREID, PHYSIS, project ECS).
    // These callbacks are sampled from one immutable published world epoch.
    std::function<bool(PointCm)> submerged;
    std::function<bool(PointCm,PointCm)> climbable;
    std::function<uint32_t(PointCm,CaveTraversal)> traversalCost;
    std::function<bool(PointCm,PointCm,const CaveAgentProfile&)> customLink;
    std::function<uint16_t(PointCm)> areaAt;
};
class CaveNavigationDataProvider {
public:
    explicit CaveNavigationDataProvider(
        std::shared_ptr<const CaveSnapshotSpatialIndex> index,
        CaveNavigationCallbacks callbacks={})
        :index_(std::move(index)),callbacks_(std::move(callbacks)){
        if(!index_||!index_->revision())
            throw std::invalid_argument("Caverns navigation source epoch");
    }
    uint64_t revision()const noexcept{return index_->revision();}
    const CaveSnapshotSpatialIndex& sdf()const noexcept{return *index_;}
    std::shared_ptr<const CaveSnapshotSpatialIndex> indexedSource()const{
        return index_;
    }
    static void validateProfile(CaveAgentProfile p) {
        if(p.radiusCm<0||p.radiusCm>10000||
           p.heightCm<1||p.heightCm>10000||
           p.maxStepCm<0||p.maxStepCm>10000||
           p.groundProbeCm<1||p.groundProbeCm>10000||
           p.sampleCm<1||p.sampleCm>1000)
            throw std::invalid_argument("Caverns agent profile");
    }
    // Uses the exact same sign convention as mesh/raycast: positive=air.
    // This intentionally uses a conservative cross-sectional sample stencil
    // rather than an unchecked centre-only occupancy test.
    bool clearAt(PointCm p,const CaveAgentProfile& agent)const {
        validateProfile(agent);
        const int64_t r=agent.radiusCm;
        constexpr std::array<std::pair<int32_t,int32_t>,9> stencil{{
            {0,0},{1,0},{-1,0},{0,1},{0,-1},
            {1,1},{1,-1},{-1,1},{-1,-1}
        }};
        const auto check=[&](int64_t y)->bool {
            for(const auto [dx,dz]:stencil) {
                // Diagonal samples are conservative (at square corners).
                const PointCm s{p.x+r*dx,y,p.z+r*dz};
                if(!inBounds(s)||index_->field(s)<=0)return false;
            }
            return true;
        };
        const int32_t increment=agent.sampleCm;
        if(!check(p.y))return false;
        const int64_t top=p.y+agent.heightCm;
        for(int64_t y=p.y+increment;y<top;y+=increment)
            if(!check(y))return false;
        return check(top);
    }
    bool hasSupport(PointCm p,const CaveAgentProfile& agent)const {
        validateProfile(agent);
        // Ground requires a solid voxel/surface below the actor footprint.
        // A flying/swimming actor is handled by a separate traversal policy.
        if(!inBounds(p)||index_->field(p)<=0)return false;
        for(int32_t depth=agent.sampleCm;depth<=agent.groundProbeCm;
            depth+=agent.sampleCm) {
            PointCm down{p.x,p.y-depth,p.z};
            if(inBounds(down)&&index_->field(down)<=0)return true;
            if(agent.groundProbeCm-depth<agent.sampleCm)break;
        }
        return false;
    }
    CaveNavigationCell sample(PointCm p,const CaveAgentProfile& agent)const {
        CaveNavigationCell out;out.centre=p;
        out.traversable=clearAt(p,agent);
        out.hasFloor=out.traversable&&hasSupport(p,agent);
        if(!out.traversable)return out;
        out.flooded=callbacks_.submerged&&callbacks_.submerged(p);
        out.area=callbacks_.areaAt?callbacks_.areaAt(p):uint16_t(0);
        const uint32_t cost=callbacks_.traversalCost
            ?callbacks_.traversalCost(p,agent.traversal):10u;
        out.cost=std::max(1u,cost);
        switch(agent.traversal){
        case CaveTraversal::Ground:out.traversable=out.hasFloor;break;
        case CaveTraversal::Swimming:out.traversable=out.flooded;break;
        case CaveTraversal::Flying:break;
        case CaveTraversal::Climbing:
            out.traversable=callbacks_.climbable &&
                callbacks_.climbable(p,{p.x,p.y-agent.sampleCm,p.z});
            break;
        case CaveTraversal::Custom:break;
        }
        out.traversable=out.traversable&&
            (agent.areaMask&(1u<<(out.area%32)))!=0;
        return out;
    }
    // Strict directed edge legality. Asymmetric costs/one-way links are
    // allowed by custom algorithms but require the caller's own policy.
    bool canTraverse(PointCm a,PointCm b,
                     const CaveAgentProfile& agent)const {
        if(!inBounds(a)||!inBounds(b))return false;
        if(agent.traversal==CaveTraversal::Custom&&callbacks_.customLink)
            return callbacks_.customLink(a,b,agent);
        const auto from=sample(a,agent),to=sample(b,agent);
        if(!from.traversable||!to.traversable)return false;
        if(agent.traversal==CaveTraversal::Ground &&
           absDelta(a.y,b.y)>uint64_t(agent.maxStepCm))return false;
        // Avoid tunnelling through narrow walls/ceilings between free cells.
        const int64_t maxDelta=int64_t(std::max({
            absDelta(a.x,b.x),absDelta(a.y,b.y),absDelta(a.z,b.z)}));
        const int64_t steps=std::max<int64_t>(1,
            (maxDelta+agent.sampleCm-1)/agent.sampleCm);
        if(steps>65536)return false; // bounded query, fail closed
        for(int64_t i=1;i<steps;++i) {
            const PointCm midpoint{
                a.x+(b.x-a.x)*i/steps,
                a.y+(b.y-a.y)*i/steps,
                a.z+(b.z-a.z)*i/steps};
            if(!sample(midpoint,agent).traversable)return false;
        }
        return true;
    }
private:
    static bool inBounds(PointCm p)noexcept{
        return p.x>=-kLimitCm&&p.x<=kLimitCm&&
               p.y>=-kLimitCm&&p.y<=kLimitCm&&
               p.z>=-kLimitCm&&p.z<=kLimitCm;
    }
    static uint64_t absDelta(int64_t a,int64_t b)noexcept{
        return uint64_t(a>b?a-b:b-a);
    }
    std::shared_ptr<const CaveSnapshotSpatialIndex> index_;
    CaveNavigationCallbacks callbacks_;
};
} // namespace aiverse::terraweave::caverns
