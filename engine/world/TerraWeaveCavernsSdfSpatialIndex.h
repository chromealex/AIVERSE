#pragma once
// Stage39: immutable, bounded spatial acceleration for authored CaveVolumeSnapshot.
// The index changes WHERE we look, never HOW the integer SDF is evaluated.
// Canonical ID order and carve-then-fill precedence are preserved exactly.
// Large influence volumes go into a bounded overflow list, not millions of cells.
// No global caches, GPU, ECS, mutable reader state, or genre dependencies.
#include "engine/world/TerraWeaveCavernsVolumeSnapshot.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave::caverns {
struct CaveSdfIndexConfig {
    int32_t cellCm=4096;
    uint32_t maxCellsPerInfluence=256;
    size_t maxCellPostings=262144;
};
struct CaveSdfIndexStats {
    size_t stampCells=0,entranceCells=0;
    size_t stampPostings=0,entrancePostings=0;
    size_t overflowStamps=0,overflowEntrances=0;
};
class CaveSnapshotSpatialIndex {
    struct Key {
        int64_t x=0,y=0,z=0;
        constexpr bool operator<(const Key& b)const noexcept {
            if(x!=b.x)return x<b.x;
            if(y!=b.y)return y<b.y;
            return z<b.z;
        }
    };
    using Grid=std::map<Key,std::vector<uint32_t>>;
public:
    explicit CaveSnapshotSpatialIndex(
        std::shared_ptr<const CaveVolumeSnapshot> source,
        CaveSdfIndexConfig config={})
        :source_(std::move(source)),config_(config) {
        if(!source_||!source_->sourceRevision||!source_->generatorVersion||
           config_.cellCm<100||config_.cellCm>1000000||
           !config_.maxCellsPerInfluence||config_.maxCellsPerInfluence>65536||
           !config_.maxCellPostings||config_.maxCellPostings>4000000)
            throw std::invalid_argument("Caverns spatial SDF index settings");
        // Snapshot supplied by the authoritative owner is canonical/immutable.
        // The index records its revision and never dereferences mutable data.
        stampCount_=source_->stamps.size();
        entranceCount_=source_->entrances.size();
        for(uint32_t i=0;i<entranceCount_;++i) {
            const auto& e=source_->entrances[i];
            const int64_t r=int64_t(e.radiusCm);
            // entranceFieldCm does NOT clamp its negative radial influence
            // at +/-radius. It can raise the base field as far as radial
            // reaches -100000. r+100000 is a safe conservative XZ radius:
            // (r+100000)^2 >= r^2 + 2*r*100000.
            const int64_t influence=r+100000;
            insert(entranceGrid_,overflowEntrances_,stats_.entrancePostings,
                i,{e.xCm-influence,e.caveYcm-r,e.zCm-influence},
                  {e.xCm+influence,e.surfaceYcm+r,e.zCm+influence});
        }
        for(uint32_t i=0;i<stampCount_;++i) {
            const auto& stamp=source_->stamps[i];
            const int64_t r=stamp.radiusCm;
            // Beyond this closed AABB, caveSphereField returns -100000.
            // Carve max(-100000) and Fill min(+100000) are exact no-ops.
            insert(stampGrid_,overflowStamps_,stats_.stampPostings,
                i,{stamp.center.x-r,stamp.center.y-r,stamp.center.z-r},
                  {stamp.center.x+r,stamp.center.y+r,stamp.center.z+r});
        }
        stats_.stampCells=stampGrid_.size();
        stats_.entranceCells=entranceGrid_.size();
        stats_.overflowStamps=overflowStamps_.size();
        stats_.overflowEntrances=overflowEntrances_.size();
    }
    const CaveVolumeSnapshot& snapshot()const noexcept{return *source_;}
    std::shared_ptr<const CaveVolumeSnapshot> frozenSnapshot()const noexcept {
        return source_;
    }
    uint64_t revision()const noexcept{return source_->sourceRevision;}
    const CaveSdfIndexStats& stats()const noexcept{return stats_;}

    // Produces exactly caveSnapshotFieldCm(snapshot(),p) for every legal
    // point, including negative coordinates, touching sphere boundaries,
    // interleaved Carve/Fill IDs and entrance radial tails.
    int64_t field(PointCm p)const {
        const int64_t x=floorDiv(p.x,config_.cellCm);
        const int64_t y=floorDiv(p.y,config_.cellCm);
        const int64_t z=floorDiv(p.z,config_.cellCm);
        const Key key{x,y,z};
        int64_t cave=caveFieldCmUnchecked(source_->settings,p);
        visit(entranceGrid_,overflowEntrances_,key,[&](uint32_t i) {
            cave=std::max(cave,entranceFieldCm(source_->entrances[i],p));
        });
        // Compute all Carve contributions first, then all Fill contributions.
        // Never use ID order to interleave these two passes.
        visit(stampGrid_,overflowStamps_,key,[&](uint32_t i) {
            const auto& stamp=source_->stamps[i];
            if(stamp.mode==CaveStampMode::Carve)
                cave=std::max(cave,caveSphereField(stamp,p));
        });
        visit(stampGrid_,overflowStamps_,key,[&](uint32_t i) {
            const auto& stamp=source_->stamps[i];
            if(stamp.mode==CaveStampMode::Fill)
                cave=std::min(cave,-caveSphereField(stamp,p));
        });
        return cave;
    }
private:
    template<class Func>
    static void visit(const Grid& grid,const std::vector<uint32_t>& overflow,
                      const Key& key,Func&& fn){
        static const std::vector<uint32_t> empty;
        const auto it=grid.find(key);
        const auto& local=it==grid.end()?empty:it->second;
        // Both lists have strictly increasing indices, and an influence
        // belongs to exactly one of them. Merge in canonical source order.
        size_t a=0,b=0;
        while(a<local.size()||b<overflow.size()){
            if(b==overflow.size() ||
               (a<local.size()&&local[a]<overflow[b]))
                fn(local[a++]);
            else fn(overflow[b++]);
        }
    }
    void insert(Grid& grid,std::vector<uint32_t>& overflow,
                size_t& postings,uint32_t id,PointCm min,PointCm max){
        // Clamp to the legal field world, which is all that callers sample.
        min.x=std::max(min.x,-kLimitCm);min.y=std::max(min.y,-kLimitCm);
        min.z=std::max(min.z,-kLimitCm);
        max.x=std::min(max.x,kLimitCm);max.y=std::min(max.y,kLimitCm);
        max.z=std::min(max.z,kLimitCm);
        if(min.x>max.x||min.y>max.y||min.z>max.z)return;
        const Key first{floorDiv(min.x,config_.cellCm),
                        floorDiv(min.y,config_.cellCm),
                        floorDiv(min.z,config_.cellCm)};
        const Key last{floorDiv(max.x,config_.cellCm),
                       floorDiv(max.y,config_.cellCm),
                       floorDiv(max.z,config_.cellCm)};
        const uint64_t nx=uint64_t(last.x-first.x+1);
        const uint64_t ny=uint64_t(last.y-first.y+1);
        const uint64_t nz=uint64_t(last.z-first.z+1);
        const uint64_t cap=config_.maxCellsPerInfluence;
        if(nx>cap||ny>cap||nz>cap||nx*ny>cap||
           nx*ny*nz>cap||nx*ny*nz>config_.maxCellPostings||
           postings>config_.maxCellPostings-nx*ny*nz){
            overflow.push_back(id);return;
        }
        for(int64_t z=first.z;z<=last.z;++z)
        for(int64_t y=first.y;y<=last.y;++y)
        for(int64_t x=first.x;x<=last.x;++x)
            grid[{x,y,z}].push_back(id);
        postings+=size_t(nx*ny*nz);
    }
    std::shared_ptr<const CaveVolumeSnapshot> source_;
    CaveSdfIndexConfig config_;
    CaveSdfIndexStats stats_{};
    size_t stampCount_=0,entranceCount_=0;
    Grid stampGrid_,entranceGrid_;
    std::vector<uint32_t> overflowStamps_,overflowEntrances_;
};
inline CaveMesh generateCaveIndexedSnapshotMesh(
    const CaveSnapshotSpatialIndex& index,ChunkKey3 key,
    uint32_t resolution,int32_t spacingCm){
    return generateCaveMeshSampled(index.snapshot().settings,key,
        resolution,spacingCm,[&index](PointCm p){return index.field(p);});
}
} // namespace aiverse::terraweave::caverns
