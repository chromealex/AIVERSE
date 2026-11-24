// TerraWeave Stage 60 — deterministic mass-conserving slope failure.
// NEREID produces shoreline erosion / water saturation; TerraWeave owns
// soil movement and vegetation consequences. This is a heightfield soil
// relaxation policy, not a volumetric landslide or second water solver.
// All values are integer centimetres and no random/float state is used.
#pragma once
#include "engine/world/TerraWeaveEarthworks.h"
#include "engine/world/nereid/NereidErosion.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct SlopeSoil {
    lib::nereid::ShoreSoil soil=lib::nereid::ShoreSoil::Loam;
    uint32_t waterSaturationPermille=0; // 0..1000; NEREID/PHYSIS-owned input
    uint32_t rootBindingPermille=0;     // 0..1000; saved vegetation source
};
using SlopeSoilSampler=std::function<SlopeSoil(EarthVertex)>;

struct SlopeFailureSettings {
    uint32_t maxSlidesPerTick=4096;    // <=4096, one disjoint source/target per tick
    uint32_t maxMoveCmPerSlide=40;      // <=500
    uint32_t slopeFactorPermille=1000; // 100..4000, global stability multiplier
    uint32_t waterWeakeningPermille=450; // 0..900, saturated soil weakening
    uint32_t rootStrengthPermille=600;   // 0..2000, root reinforcement
};

struct SlopeFailureSlide {
    EarthVertex from{},to{};
    uint32_t movedCm=0;
    lib::nereid::ShoreSoil soil=lib::nereid::ShoreSoil::Loam;
    uint32_t slopePermille=0;
    uint32_t criticalPermille=0;
};
struct SlopeFailurePlan {
    std::vector<EarthVertexEdit> edits; // sorted, unique, one Earthworks revision
    std::vector<SlopeFailureSlide> slides;
    uint64_t movedVolumeCm3=0;
};

// Angle-of-repose proxies by material. Rocks are fixed unless the host
// converts fractured bedrock into loose debris by an explicit gameplay edit.
inline uint32_t slopeReposePermille(lib::nereid::ShoreSoil soil) {
    switch(soil) {
        case lib::nereid::ShoreSoil::Sand:return 580;
        case lib::nereid::ShoreSoil::Silt:return 650;
        case lib::nereid::ShoreSoil::Loam:return 850;
        case lib::nereid::ShoreSoil::Clay:return 1000;
        case lib::nereid::ShoreSoil::Gravel:return 1300;
        case lib::nereid::ShoreSoil::Rock:return UINT32_MAX;
    }
    throw std::invalid_argument("TerraWeave unknown slope soil");
}

inline void validateSlopeFailureSettings(const SlopeFailureSettings& c){
    if(c.maxSlidesPerTick==0 || c.maxSlidesPerTick>4096 ||
       c.maxMoveCmPerSlide==0 || c.maxMoveCmPerSlide>500 ||
       c.slopeFactorPermille<100 || c.slopeFactorPermille>4000 ||
       c.waterWeakeningPermille>900 ||
       c.rootStrengthPermille>2000)
        throw std::invalid_argument("TerraWeave slope failure settings");
}

inline uint32_t criticalSlopePermille(
        SlopeSoil soil,SlopeFailureSettings c={}) {
    validateSlopeFailureSettings(c);
    const uint32_t base=slopeReposePermille(soil.soil);
    if(soil.waterSaturationPermille>1000 ||
       soil.rootBindingPermille>1000)
        throw std::invalid_argument("TerraWeave slope moisture/root strength");
    if(base==UINT32_MAX)return base;
    // Immersion weakens cohesive soil, but plant roots reinforce its
    // effective angle of repose. Arithmetic is bounded at 64 bits.
    const uint64_t wet=1000ull-
        uint64_t(soil.waterSaturationPermille)*
        c.waterWeakeningPermille/1000ull;
    const uint64_t root=1000ull+
        uint64_t(soil.rootBindingPermille)*
        c.rootStrengthPermille/1000ull;
    return uint32_t(std::clamp<uint64_t>(
        uint64_t(base)*c.slopeFactorPermille*wet*root/
        1000000000ull,100ull,8000ull));
}

namespace slope_detail {
inline bool validVertex(EarthVertex vertex,int64_t spacing) {
    const int64_t limit=(kWorldLimitCm-spacing)/spacing;
    return vertex.x>=-limit && vertex.x<=limit &&
           vertex.z>=-limit && vertex.z<=limit;
}
inline constexpr std::array<std::array<int,2>,4> neighbours{{
    {{-1,0}},{{0,-1}},{{0,1}},{{1,0}}
}};
}

// Sample ONE immutable height snapshot and stage only disjoint source/target
// pairs, so a slide cannot double-spend soil at a vertex in one tick.
// Candidates are the changed vertices and their one-cell neighbourhood,
// not every point of the map. Stable order/tie breaks by X/Z and directions.
inline SlopeFailurePlan planSlopeFailures(
        const EarthworksLayer& earth,
        const std::vector<EarthVertex>& affectedVertices,
        const SlopeSoilSampler& substrate,
        SlopeFailureSettings settings={}) {
    validateSlopeFailureSettings(settings);
    if(!substrate || affectedVertices.size()>16384u)
        throw std::invalid_argument("TerraWeave slope trigger budget/sampler");
    SlopeFailurePlan out;
    if(affectedVertices.empty())return out;
    const int64_t spacing=earth.spacingCm();
    std::set<EarthVertex> candidates;
    for(const EarthVertex key:affectedVertices) {
        if(!slope_detail::validVertex(key,spacing))
            throw std::out_of_range("TerraWeave invalid slope trigger");
        candidates.insert(key);
        for(const auto& d:slope_detail::neighbours){
            EarthVertex neighbour{key.x+d[0],key.z+d[1]};
            if(slope_detail::validVertex(neighbour,spacing))
                candidates.insert(neighbour);
        }
    }
    if(candidates.size()>81920u)
        throw std::length_error("TerraWeave slope candidate budget");
    std::set<EarthVertex> consumed;
    std::map<EarthVertex,int32_t> plannedHeights;
    for(const auto& candidate:candidates){
        if(out.slides.size()>=settings.maxSlidesPerTick)break;
        if(consumed.contains(candidate))continue;
        const int32_t fromHeight=earth.heightAt(candidate);
        const auto material=substrate(candidate);
        const uint32_t critical=criticalSlopePermille(material,settings);
        if(critical==UINT32_MAX)continue;
        EarthVertex best{};
        uint32_t bestSlope=0;
        uint64_t bestExcess=0;
        bool found=false;
        for(const auto& d:slope_detail::neighbours) {
            const EarthVertex target{candidate.x+d[0],candidate.z+d[1]};
            if(!slope_detail::validVertex(target,spacing) ||
               consumed.contains(target))continue;
            const int32_t toHeight=earth.heightAt(target);
            if(fromHeight<=toHeight)continue;
            const int64_t difference=int64_t(fromHeight)-toHeight;
            // Avoid integer division in the decision: height difference
            // must exceed repose rise over this one grid spacing.
            const uint64_t criticalRise=
                (uint64_t(critical)*uint64_t(spacing)+999ull)/1000ull;
            if(uint64_t(difference)<=criticalRise)continue;
            const uint64_t excess=uint64_t(difference)-criticalRise;
            if(!found || excess>bestExcess){
                best=target;
                bestExcess=excess;
                bestSlope=uint32_t(std::min<uint64_t>(
                    8000, uint64_t(difference)*1000ull/spacing));
                found=true;
            }
        }
        if(!found)continue;
        const int64_t remaining=int64_t(fromHeight)-earth.bedrockCm();
        if(remaining<=0)continue;
        // Half the unstable height difference migrates downhill, capped
        // both by bedrock and destination's int32 maximum. Exact equal-
        // area volume is conserved between the two vertices.
        const int64_t targetBefore=earth.heightAt(best);
        const int64_t capacity=int64_t(INT32_MAX)-targetBefore;
        const int64_t limit=std::min<int64_t>({
            int64_t(settings.maxMoveCmPerSlide),remaining,capacity,
            std::max<int64_t>(1,int64_t(bestExcess)/2)});
        if(limit<=0)continue;
        const int32_t amount=static_cast<int32_t>(limit);
        plannedHeights.emplace(candidate,fromHeight-amount);
        plannedHeights.emplace(best,int32_t(targetBefore+amount));
        consumed.insert(candidate);consumed.insert(best);
        const uint64_t cellArea=uint64_t(spacing)*uint64_t(spacing);
        out.movedVolumeCm3+=uint64_t(amount)*cellArea;
        out.slides.push_back({
            candidate,best,static_cast<uint32_t>(amount),
            material.soil,bestSlope,critical});
    }
    out.edits.reserve(plannedHeights.size());
    for(const auto& [vertex,next]:plannedHeights)
        out.edits.push_back({vertex,earth.heightAt(vertex),next});
    return out;
}

struct SlopeFailureOutcome {
    EarthworkChange earth{};
    std::vector<SlopeFailureSlide> slides;
    uint64_t movedVolumeCm3=0;
};

// Commit uses Earthworks' one-transaction sparse edit API: collision,
// NavMesh, grass and roots receive one revision/dirty rectangle afterward.
inline SlopeFailureOutcome applySlopeFailures(
        EarthworksLayer& earth,
        const std::vector<EarthVertex>& affectedVertices,
        const SlopeSoilSampler& substrate,
        SlopeFailureSettings settings={}) {
    const auto plan=planSlopeFailures(
        earth,affectedVertices,substrate,settings);
    SlopeFailureOutcome out;
    out.slides=plan.slides;
    out.movedVolumeCm3=plan.movedVolumeCm3;
    if(!plan.edits.empty())
        out.earth=earth.applyEdits(plan.edits,earth.revision());
    return out;
}

} // namespace aiverse::terraweave
