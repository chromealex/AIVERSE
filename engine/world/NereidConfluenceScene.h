// NEREID Stage 42 — read-only ECS-to-scene confluence flow projection.
// Works on immutable snapshots, sharing Stage 41 fixed-point flow kernels.
// Scene publication must follow an ECS read barrier; no gameplay writes.
#pragma once
#include "engine/world/NereidWaterScene.h"
#include "engine/world/nereid/NereidConfluenceFlow.h"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::world {

// ECS snapshots use packed generation-safe Entity + 1; static JSON scenes
// instead use explicit stable water source IDs (never fabricated ECS handles).
// The junction component carries copied Fixed flow settings, not live ECS.
// For JSON-only sources, its Entity link fields remain null: the source's
// inletWaterIds/outletWaterId carry the actual scene links.
struct NereidConfluenceSource {
    uint64_t id=0; // generation-safe confluence Entity + 1
    lib::nereid::WaterConfluenceComponent junction{};
    std::array<uint64_t,lib::nereid::kMaxWaterConfluenceInlets> inletWaterIds{};
    uint64_t outletWaterId=0;
};

class NereidConfluenceScene final {
public:
    static constexpr size_t kMaxSources=128;
    static constexpr size_t kMaxContributions=32;

    void setConfluences(std::vector<NereidConfluenceSource> incoming) {
        if(incoming.size()>kMaxSources)
            throw std::invalid_argument("nereid.confluence: too many scene junctions");
        std::map<uint64_t,NereidConfluenceSource> next;
        for(auto& source:incoming) {
            if(!source.id || !source.outletWaterId ||
               !lib::nereid::validWaterConfluenceFlowPolicy(source.junction))
                throw std::invalid_argument("nereid.confluence: invalid scene junction");
            for(size_t i=0;i<source.junction.inletCount;++i) {
                if(!source.inletWaterIds[i] ||
                   source.inletWaterIds[i]==source.outletWaterId)
                    throw std::invalid_argument(
                        "nereid.confluence: invalid linked scene water ID");
                for(size_t j=0;j<i;++j)
                    if(source.inletWaterIds[j]==source.inletWaterIds[i])
                        throw std::invalid_argument(
                            "nereid.confluence: duplicate inlet water ID");
            }
            const uint64_t id=source.id;
            if(!next.emplace(id,std::move(source)).second)
                throw std::invalid_argument("nereid.confluence: duplicate junction ID");
        }
        sources_.swap(next);
    }
    size_t count()const{return sources_.size();}

    // Same eligibility/mean/kernel/reducer as the reference/indexed ECS
    // sample, except that water IDs resolve via immutable scene snapshots.
    Vec3 flowForWater(uint64_t waterId,Vec3 position,Vec3 base,
                      const NereidWaterScene& water)const {
        if(!waterId || sources_.empty())return base;
        std::array<lib::nereid::WaterConfluenceFlowContribution,
                   kMaxContributions> entries{};
        size_t used=0;
        for(const auto& [id,source]:sources_) {
            if(source.outletWaterId!=waterId)continue;
            const auto& junction=source.junction;
            const auto* outlet=water.surfaceForId(source.outletWaterId);
            if(!outlet || !lib::nereid::coversWaterBody(
                   outlet->body,outlet->useGeometry?&outlet->geometry:nullptr,
                   junction.mouth))continue;
            auto sorted=source.inletWaterIds;
            std::sort(sorted.begin(),sorted.begin()+junction.inletCount);
            Vec2 sum{};
            bool complete=true;
            for(size_t i=0;i<junction.inletCount;++i) {
                const auto* upper=water.surfaceForId(sorted[i]);
                if(!upper || !lib::nereid::coversWaterBody(
                       upper->body,upper->useGeometry?&upper->geometry:nullptr,
                       junction.mouth)) {
                    complete=false;
                    break;
                }
                const Vec3 current=lib::nereid::waterCurrentWithVortices(
                    upper->body,upper->useGeometry?&upper->geometry:nullptr,
                    upper->useCurrentField?&upper->currentField:nullptr,
                    upper->useVortices?&upper->vortices:nullptr,
                    junction.mouth);
                sum.x+=fxClamp(current.x,-1000_fx,1000_fx);
                sum.y+=fxClamp(current.z,-1000_fx,1000_fx);
            }
            if(!complete)continue;
            const Fixed count=Fixed::fromInt(junction.inletCount);
            const auto candidate=lib::nereid::waterConfluenceMouthContribution(
                id,junction,sum/count,position);
            if(candidate.weight<=Fixed{})continue;
            // Sources are already ID-sorted by map. The first 32 eligible
            // are exactly the lowest IDs, matching ECS bounded selection.
            if(used<entries.size())entries[used++]=candidate;
        }
        return lib::nereid::blendWaterConfluenceContributions(
            base,entries,used);
    }

private:
    std::map<uint64_t,NereidConfluenceSource> sources_;
};
} // namespace aiverse::world
