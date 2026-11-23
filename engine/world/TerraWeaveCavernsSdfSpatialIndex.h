#pragma once
// Stage39: immutable, bounded spatial acceleration for authored CaveVolumeSnapshot.
// The index changes WHERE we look, never HOW the integer SDF is evaluated.
// Integer max/min reduction and carve-then-fill precedence are preserved exactly.
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
struct CaveSdfPointCandidates {size_t stamps=0,entrances=0;};
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
    struct Influence {uint32_t index=0;PointCm min{},max{};};
    struct Node {
        PointCm min{},max{};
        int32_t left=-1,right=-1;
        uint32_t begin=0,count=0;
    };
public:
    explicit CaveSnapshotSpatialIndex(
        std::shared_ptr<const CaveVolumeSnapshot> source,
        CaveSdfIndexConfig config={})
        :source_(std::move(source)),config_(config) {
        if(!source_||!source_->sourceRevision||!source_->generatorVersion||
           source_->stamps.size()>16384||source_->entrances.size()>4096||
           config_.cellCm<100||config_.cellCm>1000000||
           !config_.maxCellsPerInfluence||config_.maxCellsPerInfluence>65536||
           !config_.maxCellPostings||config_.maxCellPostings>4000000)
            throw std::invalid_argument("Caverns spatial SDF index settings");
        validate(source_->settings);
        // Snapshot supplied by the authoritative owner is canonical/immutable.
        // The index records its revision and never dereferences mutable data.
        stampCount_=source_->stamps.size();
        entranceCount_=source_->entrances.size();
        for(uint32_t i=0;i<entranceCount_;++i) {
            const auto& e=source_->entrances[i];
            validatePoint({e.xCm,e.caveYcm,e.zCm});
            validatePoint({e.xCm,e.surfaceYcm,e.zCm});
            if(e.radiusCm<=0||e.radiusCm>1000000||
               e.surfaceYcm<e.caveYcm)
                throw std::invalid_argument("Caverns indexed entrance bounds");
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
            validatePoint(stamp.center);
            if(stamp.radiusCm<1||stamp.radiusCm>1000000||
               (stamp.mode!=CaveStampMode::Carve &&
                stamp.mode!=CaveStampMode::Fill))
                throw std::invalid_argument("Caverns indexed stamp bounds");
            const int64_t r=stamp.radiusCm;
            // Beyond this closed AABB, caveSphereField returns -100000.
            // Carve max(-100000) and Fill min(+100000) are exact no-ops.
            insert(stampGrid_,overflowStamps_,stats_.stampPostings,
                i,{stamp.center.x-r,stamp.center.y-r,stamp.center.z-r},
                  {stamp.center.x+r,stamp.center.y+r,stamp.center.z+r});
        }
        buildTree(overflowStamps_,stampTree_,0,overflowStamps_.size());
        buildTree(overflowEntrances_,entranceTree_,0,overflowEntrances_.size());
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

    // Read-only workload instrumentation, useful for editor brush telemetry.
    // Counts influence candidates which can affect this exact query point;
    // it does not change SDF, allocate memory, or store mutable counters.
    CaveSdfPointCandidates candidates(PointCm p)const {
        const Key key{floorDiv(p.x,config_.cellCm),
                      floorDiv(p.y,config_.cellCm),
                      floorDiv(p.z,config_.cellCm)};
        CaveSdfPointCandidates count;
        visit(stampGrid_,overflowStamps_,stampTree_,key,p,
              [&](uint32_t){++count.stamps;});
        visit(entranceGrid_,overflowEntrances_,entranceTree_,key,p,
              [&](uint32_t){++count.entrances;});
        return count;
    }

    // Produces exactly caveSnapshotFieldCm(snapshot(),p) for every legal
    // point, including negative coordinates, touching sphere boundaries,
    // interleaved Carve/Fill IDs and entrance radial tails.
    int64_t field(PointCm p)const {
        const int64_t x=floorDiv(p.x,config_.cellCm);
        const int64_t y=floorDiv(p.y,config_.cellCm);
        const int64_t z=floorDiv(p.z,config_.cellCm);
        const Key key{x,y,z};
        int64_t cave=caveFieldCmUnchecked(source_->settings,p);
        visit(entranceGrid_,overflowEntrances_,entranceTree_,key,p,[&](uint32_t i) {
            cave=std::max(cave,entranceFieldCm(source_->entrances[i],p));
        });
        // Compute all Carve contributions first, then all Fill contributions.
        // Never use ID order to interleave these two passes.
        visit(stampGrid_,overflowStamps_,stampTree_,key,p,[&](uint32_t i) {
            const auto& stamp=source_->stamps[i];
            if(stamp.mode==CaveStampMode::Carve)
                cave=std::max(cave,caveSphereField(stamp,p));
        });
        visit(stampGrid_,overflowStamps_,stampTree_,key,p,[&](uint32_t i) {
            const auto& stamp=source_->stamps[i];
            if(stamp.mode==CaveStampMode::Fill)
                cave=std::min(cave,-caveSphereField(stamp,p));
        });
        return cave;
    }
private:
    static bool contains(PointCm p,PointCm a,PointCm b)noexcept{
        return p.x>=a.x&&p.y>=a.y&&p.z>=a.z&&
               p.x<=b.x&&p.y<=b.y&&p.z<=b.z;
    }
    static int64_t axisValue(PointCm p,unsigned axis)noexcept{
        return axis==0?p.x:(axis==1?p.y:p.z);
    }
    static PointCm pointMin(PointCm a,PointCm b)noexcept{
        return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};
    }
    static PointCm pointMax(PointCm a,PointCm b)noexcept{
        return {std::max(a.x,b.x),std::max(a.y,b.y),std::max(a.z,b.z)};
    }
    static int32_t buildTree(std::vector<Influence>& records,
                             std::vector<Node>& nodes,size_t begin,size_t end){
        if(begin==end)return -1;
        Node bounds;
        bounds.min=records[begin].min;bounds.max=records[begin].max;
        for(size_t i=begin+1;i<end;++i){
            bounds.min=pointMin(bounds.min,records[i].min);
            bounds.max=pointMax(bounds.max,records[i].max);
        }
        const int32_t id=int32_t(nodes.size());
        nodes.push_back(bounds);
        if(end-begin<=8){
            nodes[size_t(id)].begin=uint32_t(begin);
            nodes[size_t(id)].count=uint32_t(end-begin);
            return id;
        }
        const int64_t sx=bounds.max.x-bounds.min.x;
        const int64_t sy=bounds.max.y-bounds.min.y;
        const int64_t sz=bounds.max.z-bounds.min.z;
        const unsigned axis=sx>=sy&&sx>=sz?0:(sy>=sz?1:2);
        std::sort(records.begin()+std::ptrdiff_t(begin),
                  records.begin()+std::ptrdiff_t(end),
                  [axis](const Influence& a,const Influence& b){
            const int64_t ca=axisValue(a.min,axis)+
                (axisValue(a.max,axis)-axisValue(a.min,axis))/2;
            const int64_t cb=axisValue(b.min,axis)+
                (axisValue(b.max,axis)-axisValue(b.min,axis))/2;
            return ca==cb?a.index<b.index:ca<cb;
        });
        const size_t middle=begin+(end-begin)/2;
        const int32_t left=buildTree(records,nodes,begin,middle);
        const int32_t right=buildTree(records,nodes,middle,end);
        nodes[size_t(id)].left=left;
        nodes[size_t(id)].right=right;
        return id;
    }
    template<class Func>
    static void visitTree(const std::vector<Influence>& records,
                          const std::vector<Node>& nodes,int32_t id,
                          PointCm p,Func& fn){
        if(id<0)return;
        const Node& node=nodes[size_t(id)];
        if(!contains(p,node.min,node.max))return;
        if(node.left<0&&node.right<0){
            const size_t last=size_t(node.begin)+node.count;
            for(size_t i=node.begin;i<last;++i)
                if(contains(p,records[i].min,records[i].max))
                    fn(records[i].index);
        }else{
            visitTree(records,nodes,node.left,p,fn);
            visitTree(records,nodes,node.right,p,fn);
        }
    }
    template<class Func>
    static void visit(const Grid& grid,
                      const std::vector<Influence>& overflow,
                      const std::vector<Node>& tree,const Key& key,
                      PointCm p,Func&& fn){
        const auto it=grid.find(key);
        if(it!=grid.end())for(uint32_t id:it->second)fn(id);
        // max/min of integer fields are order-independent within each pass.
        // Carve and Fill still run as two separate passes.
        visitTree(overflow,tree,tree.empty()?-1:0,p,fn);
    }
    void insert(Grid& grid,std::vector<Influence>& overflow,
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
            overflow.push_back({id,min,max});return;
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
    std::vector<Influence> overflowStamps_,overflowEntrances_;
    std::vector<Node> stampTree_,entranceTree_;
};
inline CaveMesh generateCaveIndexedSnapshotMesh(
    const CaveSnapshotSpatialIndex& index,ChunkKey3 key,
    uint32_t resolution,int32_t spacingCm){
    return generateCaveMeshSampled(index.snapshot().settings,key,
        resolution,spacingCm,[&index](PointCm p){return index.field(p);});
}
} // namespace aiverse::terraweave::caverns
