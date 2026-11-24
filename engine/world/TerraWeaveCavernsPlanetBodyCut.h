#pragma once
// Aggregate cut publication for one body. Region plans remain chart-local;
// only exact body-space triangle sources are shared by the renderer.
#include "engine/world/TerraWeaveCavernsPlanetCut.h"
#include "engine/world/TerraWeaveCavernsPlanetRegionSet.h"

namespace aiverse::terraweave::caverns {
struct CavePlanetBodyCutPlan {
    std::string bodyId;
    uint64_t surfaceRevision=0,surfaceEpoch=0;
    std::map<uint64_t,std::shared_ptr<const CavePlanetCutPlan>> regions;
};
struct CavePlanetBodyCutSlotRef {
    uint64_t region=0;
    const CavePlanetCutSlot* slot=nullptr;
};
using CavePlanetBodyCutSlots=std::map<CavePlanetSurfaceSlotKey,CavePlanetBodyCutSlotRef>;
inline CavePlanetBodyCutPlan singlePlanetBodyCut(std::shared_ptr<const CavePlanetCutPlan> plan){
    if(!plan)throw std::invalid_argument("Caverns missing chart plan");
    CavePlanetBodyCutPlan out;
    out.bodyId=plan->anchor.bodyId;out.surfaceRevision=plan->surfaceRevision;
    out.surfaceEpoch=plan->surfaceEpoch;out.regions.emplace(1,std::move(plan));return out;
}
// Verify uniqueness at the physical leaf level, not merely at region IDs.
// The returned references borrow the immutable plan for the duration of staging.
inline CavePlanetBodyCutSlots collectPlanetBodyCutSlots(const CavePlanetBodyCutPlan& plan){
    if(plan.bodyId.empty()||!plan.surfaceRevision||!plan.surfaceEpoch||
       plan.regions.empty()||plan.regions.size()>kMaxPlanetCaveRegions)
        throw std::invalid_argument("Caverns body cut identity");
    CavePlanetBodyCutSlots result;
    size_t sourceCount=0,resultCount=0,rimVertices=0;
    for(const auto& [id,p]:plan.regions){
        if(!id||!p||p->anchor.bodyId!=plan.bodyId||p->surfaceRevision!=plan.surfaceRevision||
           p->surfaceEpoch!=plan.surfaceEpoch||!p->authorityRevision||p->slots.empty()||
           p->slots.size()>64-result.size()||p->rims.size()!=p->accepted.size())
            throw std::invalid_argument("Caverns body cut region identity or budget");
        std::set<uint64_t> mouths(p->accepted.begin(),p->accepted.end());
        if(mouths.size()!=p->accepted.size()||mouths.contains(0))
            throw std::invalid_argument("Caverns body cut mouth IDs");
        for(const auto& rim:p->rims){
            if(!mouths.erase(rim.entranceId)||rim.ring.size()<3||rim.ring.size()>8192||
               rim.ring.size()>32768-rimVertices)
                throw std::invalid_argument("Caverns body cut rim identity or budget");
            rimVertices+=rim.ring.size();
        }
        for(const auto& [key,slot]:p->slots){
            if(key!=slot.key||key.face<0||key.face>=6||key.x<0||key.z<0||
               key.size<=0||key.level<0||slot.originalBody.empty()||
               slot.originalBody.size()>65534||slot.bodyTriangles.size()>65534||
               slot.bodyTriangles.size()!=slot.chartTriangles.size()||
               slot.originalBody.size()>131072-sourceCount||
               slot.bodyTriangles.size()>262144-resultCount)
                throw std::invalid_argument("Caverns body cut slot budget");
            for(const auto& [other,ref]:result){
                (void)ref;
                if(key.face==other.face&&int64_t(key.x)<int64_t(other.x)+other.size&&
                   int64_t(other.x)<int64_t(key.x)+key.size&&
                   int64_t(key.z)<int64_t(other.z)+other.size&&
                   int64_t(other.z)<int64_t(key.z)+key.size)
                    throw std::invalid_argument("Caverns body cut overlapping leaf ownership");
            }
            sourceCount+=slot.originalBody.size();resultCount+=slot.bodyTriangles.size();
            if(!result.emplace(key,CavePlanetBodyCutSlotRef{id,&slot}).second)
                throw std::invalid_argument("Caverns duplicate body leaf");
        }
    }
    return result;
}
using CavePlanetBodyFrames=std::map<uint64_t,CavePlanetFrame>;
// Verify the actual body-space boundary of the complete candidate, including
// seams shared by neighboring chart leaf groups. No approximate mesh is accepted.
inline CavePlanetBodyFrames certifyPlanetBodyCut(const CavePlanetBodyCutPlan& plan,
        const std::shared_ptr<const game::Surface>& surface){
    const auto slots=collectPlanetBodyCutSlots(plan);
    CavePlanetBodyFrames frames;
    std::vector<world::Triangle> original,cut;
    for(const auto& [id,p]:plan.regions){
        auto frame=CavePlanetFrame::make(surface,p->anchor);
        for(const auto& [prior,old]:frames){
            (void)prior;
            if(cavePlanetFramesMayOverlap(old,frame))
                throw std::invalid_argument("Caverns body cut overlapping charts");
        }
        frames.emplace(id,std::move(frame));
    }
    for(const auto& [key,ref]:slots){
        (void)key;
        original.insert(original.end(),ref.slot->originalBody.begin(),ref.slot->originalBody.end());
        cut.insert(cut.end(),ref.slot->bodyTriangles.begin(),ref.slot->bodyTriangles.end());
    }
    auto expected=planetaryOpenEdges(original);
    for(const auto& [id,p]:plan.regions){
        const auto& frame=frames.at(id);
        const auto point=[&](const SurfaceVertexKey& k){
            return frame.toBody({Fixed::fromRaw(k[0]),Fixed::fromRaw(k[1]),Fixed::fromRaw(k[2])});
        };
        for(const auto& rim:p->rims)for(size_t i=0;i<rim.ring.size();++i){
            const auto a=point(rim.ring[i]),b=point(rim.ring[(i+1)%rim.ring.size()]);
            if(a==b||!expected.insert(surfaceEdgeKey(a,b)).second)
                throw std::invalid_argument("Caverns body cut overlapping rim");
        }
    }
    if(planetaryOpenEdges(cut)!=expected)
        throw std::invalid_argument("Caverns body cut exterior or rim mismatch");
    return frames;
}
} // namespace aiverse::terraweave::caverns
