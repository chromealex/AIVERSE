#pragma once
#include "EvolveArea.h"
#include "aiverselib/algorithms/Bvh.h"
namespace aiverse::evolve {
// Immutable derived index; rebuild at authoritative tick barrier.
// Convert half-open integer ECS extents to inclusive library bounds.
class SpatialIndex {
    using Index=lib::algorithms::Bvh2D<size_t>;
    std::vector<Extent> zones_;
    Index index_;
public:
    explicit SpatialIndex(const World& world):SpatialIndex(extents(world)){}
    // Accept a precomputed authoritative extents snapshot (already sorted).
    explicit SpatialIndex(std::vector<Extent> zones):zones_(std::move(zones)){
        std::vector<Index::Entry> entries;
        entries.reserve(zones_.size());
        for(size_t i=0;i<zones_.size();++i){
            const auto& z=zones_[i];
            entries.push_back({i,{{z.minXcm,z.minZcm},{z.maxXcm-1,z.maxZcm-1}}});
        }
        index_.build(entries);
    }
    template<class Fn> void query(I x,I z,Fn&& fn)const{
        std::vector<size_t> scratch;
        query(x,z,scratch,std::forward<Fn>(fn));
    }
    template<class Fn> void query(I x,I z,std::vector<size_t>& scratch,Fn&& fn)const{
        index_.queryPoint({x,z},scratch);
        for(auto i:scratch)fn(zones_[i]);
    }
};
}
