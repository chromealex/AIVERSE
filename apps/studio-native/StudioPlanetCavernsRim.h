#pragma once
#include "apps/editor/core/TerraWeavePlanetCavernsDoc.h"
#include "engine/world/TerraWeaveCavernsRimTransition.h"

namespace aiverse::studio {
// Local preview only: do not publish this chart-space plan as Earthworks
// terrain or as native planetary leaf/collision ownership.
inline terraweave::caverns::CaveSurfaceGroupPlan refinePlanetChartRim(
        const terraweave::caverns::CavePlanetCutPlan& cut,
        const terraweave::caverns::CavePlanetSurfacePatch& baseline,
        uint64_t runtimeRevision,uint32_t resolution,int32_t spacingCm){
    namespace cave=terraweave::caverns;
    if(!runtimeRevision||!cut.authorityRevision||!cut.surfaceEpoch||
       cut.surfaceRevision!=baseline.surfaceRevision||
       !cave::sameCavePlanetAnchor(cut.anchor,baseline.anchor)||
       cut.rims.empty()||cut.rims.size()!=cut.accepted.size())
        throw std::invalid_argument("Caverns chart collar source identity");
    cave::CaveSurfaceGroupPlan plan;
    plan.soilRevision=cut.surfaceRevision;
    plan.authorityRevision=runtimeRevision;plan.surfaceEpoch=cut.surfaceEpoch;
    plan.resolution=resolution;
    plan.acceptedEntrances=cut.accepted;plan.rejectedEntrances=cut.rejected;
    plan.rims=cut.rims;
    auto& triangles=plan.patches[world::Cell{0,0,0}].collision;
    for(const auto& [key,slot]:cut.slots){
        (void)key;
        if(slot.chartTriangles.size()>65534-triangles.size())
            throw std::length_error("Caverns chart collar triangle budget");
        triangles.insert(triangles.end(),slot.chartTriangles.begin(),slot.chartTriangles.end());
    }
    plan=cave::refineSurfaceGroupForRimGrid(std::move(plan),resolution,spacingCm);
    auto expected=cave::planetaryOpenEdges(baseline.chartTriangles);
    for(const auto& rim:plan.rims){
        for(size_t i=0;i<rim.ring.size();++i){
            auto a=rim.ring[i],b=rim.ring[(i+1)%rim.ring.size()];
            if(b<a)std::swap(a,b);
            if(!expected.emplace(a,b).second)
                throw std::invalid_argument("Caverns chart refined rim overlaps boundary");
        }
    }
    if(cave::planetaryOpenEdges(plan.patches.begin()->second.collision)!=expected)
        throw std::invalid_argument("Caverns chart refined boundary mismatch");
    return plan;
}
} // namespace aiverse::studio
