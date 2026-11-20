// TerraWeave sparse terrain authoring and camera-adaptive visual geometry.
// Gameplay owns ONE EarthworksLayer; the editor supplies authored occupancy.
// All world coordinates are signed centimetres and no RTS/FPS map dimensions
// are consulted. Collision remains at a stable separate resolution.
#pragma once
#include "engine/world/TerraWeaveTerrainPatch.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <deque>
#include <limits>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct SparseTerrainLod {
    // Nested intervals: 32,16,8,4 segments, plus a SINGLE flat quad.
    std::array<uint32_t,4> resolutions{33,17,9,5};
    std::array<int64_t,3> distancesCm{32000,96000,256000};
    uint32_t flatProbeResolution=33; // camera-independent flatness probe
};
inline void validateSparseTerrainLod(SparseTerrainLod s) {
    if(s.flatProbeResolution<3 || s.flatProbeResolution>129)
        throw std::invalid_argument("TerraWeave invalid flat terrain probe");
    for(size_t i=0;i<s.resolutions.size();++i) {
        const uint32_t n=s.resolutions[i];
        if(n<2 || n>129 ||
           (i && (s.resolutions[i-1]-1)%(n-1)!=0))
            throw std::invalid_argument("TerraWeave terrain LODs must nest");
        if(i<s.distancesCm.size() &&
           (s.distancesCm[i]<100 || s.distancesCm[i]>kWorldLimitCm ||
            (i && s.distancesCm[i]<=s.distancesCm[i-1])))
            throw std::invalid_argument("TerraWeave invalid terrain LOD distance");
    }
}

struct SparseTerrainView {
    world::Cell cell{};
    uint32_t resolution=33;
    uint8_t tier=0; // 0..3; flat tiles use resolution=2 at EVERY distance
    bool flat=false;
    // West, east, north (-Z), south (+Z). Zero = no neighbor.
    std::array<uint32_t,4> neighborResolution{};
};

class SparseTerrainAuthoring {
public:
    static constexpr size_t kMaxChunks=65536;
    explicit SparseTerrainAuthoring(EarthworksLayer& ground,
                                    int32_t chunkMetres=256)
        : ground_(ground),chunkMetres_(chunkMetres) {
        if(chunkMetres_<16 || chunkMetres_>4096)
            throw std::invalid_argument("TerraWeave chunk size 16..4096m");
    }
    int32_t chunkSizeMetres() const{return chunkMetres_;}
    size_t chunkCount() const{return chunks_.size();}
    bool contains(world::Cell cell) const{return chunks_.contains(cell);}
    std::vector<world::Cell> chunks() const {
        return {chunks_.begin(),chunks_.end()};
    }
    void invalidateFlatness()const{flatCache_.clear();}
    world::Cell cellAt(int64_t xCm,int64_t zCm)const {
        const int64_t span=int64_t(chunkMetres_)*100;
        const int64_t x=detail::floorDiv(xCm,span);
        const int64_t z=detail::floorDiv(zCm,span);
        // SpatialWorld collision has a tighter world extent than
        // the procedural generator. Reject tiles whose corners exceed it.
        constexpr int64_t limit=100000000;
        if(x*span< -limit || x*span>limit-span ||
           z*span< -limit || z*span>limit-span)
            throw std::out_of_range("TerraWeave authored tile outside collision world");
        return {int32_t(x),0,int32_t(z)};
    }
    bool ensure(int64_t xCm,int64_t zCm) {
        const auto cell=cellAt(xCm,zCm);
        if(chunks_.contains(cell))return false;
        if(chunks_.size()>=kMaxChunks)
            throw std::length_error("TerraWeave authored terrain chunk budget");
        chunks_.insert(cell);
        return true;
    }
    // Atomic creation of ALL tiles touched by the stroke, including across
    // negative world coordinates and four corners. A failed Earthworks
    // validation/sculpt MUST leave occupancy and soil intact.
    EarthworkChange paint(EarthworkBrush brush) {
        if(brush.radiusCm<1 || brush.radiusCm>50000)
            throw std::invalid_argument("TerraWeave sparse brush radius");
        (void)cellAt(brush.centerXcm,brush.centerZcm);
        const int64_t span=int64_t(chunkMetres_)*100;
        const int64_t x0=detail::floorDiv(brush.centerXcm-brush.radiusCm,span);
        const int64_t x1=detail::floorDiv(brush.centerXcm+brush.radiusCm,span);
        const int64_t z0=detail::floorDiv(brush.centerZcm-brush.radiusCm,span);
        const int64_t z1=detail::floorDiv(brush.centerZcm+brush.radiusCm,span);
        if((x1-x0+1)*(z1-z0+1)>256)
            throw std::length_error("TerraWeave authored stroke tile budget");
        auto next=chunks_;
        const int64_t r2=int64_t(brush.radiusCm)*brush.radiusCm;
        for(int64_t z=z0;z<=z1;++z)
            for(int64_t x=x0;x<=x1;++x) {
                const int64_t px=x*span,pz=z*span;
                const int64_t nearestX=std::clamp(brush.centerXcm,px,px+span);
                const int64_t nearestZ=std::clamp(brush.centerZcm,pz,pz+span);
                const int64_t dx=nearestX-brush.centerXcm;
                const int64_t dz=nearestZ-brush.centerZcm;
                if(dx*dx+dz*dz>r2)continue;
                next.insert(cellAt(px+span/2,pz+span/2));
            }
        if(next.size()>kMaxChunks)
            throw std::length_error("TerraWeave authored chunk budget");
        auto change=ground_.sculpt(brush);
        chunks_.swap(next);
        invalidateFlatness();
        return change;
    }
    // Store only canonical occupancy. Existing Earthworks snapshot contains
    // authoritative heights and revision; this type never duplicates them.
    void restore(std::span<const world::Cell> cells) {
        if(cells.size()>kMaxChunks)
            throw std::length_error("TerraWeave restore chunk budget");
        std::set<world::Cell> next;
        const int64_t span=int64_t(chunkMetres_)*100;
        for(const auto cell:cells) {
            if(cell.y!=0 || cell!=cellAt(int64_t(cell.x)*span+span/2,
                                         int64_t(cell.z)*span+span/2) ||
               !next.insert(cell).second)
                throw std::invalid_argument("TerraWeave invalid/duplicate chunk");
        }
        chunks_.swap(next);
        invalidateFlatness();
    }

    bool flat(world::Cell cell,uint32_t probeResolution=33)const{
        if(probeResolution<3 || probeResolution>129)
            throw std::invalid_argument("TerraWeave flat probe resolution");
        const auto key=std::make_pair(cell,probeResolution);
        if(const auto it=flatCache_.find(key);
           it!=flatCache_.end() && it->second.first==ground_.revision())
            return it->second.second;
        const int64_t span=int64_t(chunkMetres_)*100;
        const int64_t ox=int64_t(cell.x)*span,oz=int64_t(cell.z)*span;
        // Explicit sparse overrides between the probe samples are
        // significant even when the coarse sample grid misses them.
        const int32_t base=ground_.sampleHeightCm(ox,oz);
        bool isFlat=true;
        for(const auto& [v,height]:ground_.overrides()) {
            const int64_t x=v.x*ground_.spacingCm();
            const int64_t z=v.z*ground_.spacingCm();
            if(x>=ox && x<=ox+span && z>=oz && z<=oz+span &&
               height!=base) {isFlat=false;break;}
        }
        if(isFlat)for(uint32_t z=0;z<probeResolution && isFlat;++z)
            for(uint32_t x=0;x<probeResolution;++x) {
                const int64_t sx=ox+
                    int64_t(x)*span/(probeResolution-1);
                const int64_t sz=oz+
                    int64_t(z)*span/(probeResolution-1);
                if(ground_.sampleHeightCm(sx,sz)!=base) {
                    isFlat=false;break;
                }
            }
        flatCache_[key]={ground_.revision(),isFlat};
        return isFlat;
    }

    // Deterministic camera-dependent LOD for occupied tiles. Flatness is
    // independent of the camera: a truly flat tile is ALWAYS resolution 2.
    // Iterative queue refinement keeps adjacent non-flat tiers <=1 apart.
    std::vector<SparseTerrainView> view(int64_t camXcm,int64_t camZcm,
                                       SparseTerrainLod s={},
                                       int64_t maxDistanceCm=0)const {
        validateSparseTerrainLod(s);
        if(maxDistanceCm<0 || maxDistanceCm>kWorldLimitCm)
            throw std::invalid_argument("TerraWeave invalid view streaming radius");
        if(camXcm<=-kWorldLimitCm || camXcm>=kWorldLimitCm ||
           camZcm<=-kWorldLimitCm || camZcm>=kWorldLimitCm)
            throw std::out_of_range("TerraWeave camera outside world");
        const int64_t span=int64_t(chunkMetres_)*100;
        std::map<world::Cell,uint8_t> tiers;
        for(const auto& cell:chunks_) {
            const int64_t dx=int64_t(cell.x)*span+span/2-camXcm;
            const int64_t dz=int64_t(cell.z)*span+span/2-camZcm;
            const uint64_t d2=uint64_t(dx*dx)+uint64_t(dz*dz);
            if(maxDistanceCm &&
               d2>uint64_t(maxDistanceCm)*uint64_t(maxDistanceCm))
                continue; // skip distant chunks before any flatness work
            uint8_t tier=0;
            for(int64_t threshold:s.distancesCm)
                if(d2>=uint64_t(threshold)*threshold)++tier;
            tiers.emplace(cell,tier);
        }
        const std::array<std::pair<int,int>,4> cardinal{{
            {-1,0},{1,0},{0,-1},{0,1}}};
        std::deque<world::Cell> queue;
        for(const auto& [cell,tier]:tiers){(void)tier;queue.push_back(cell);}
        while(!queue.empty()){
            const auto cell=queue.front();queue.pop_front();
            const uint8_t tier=tiers.at(cell);
            for(const auto& [dx,dz]:cardinal){
                const auto neighbor=world::Cell{cell.x+dx,0,cell.z+dz};
                auto found=tiers.find(neighbor);
                if(found!=tiers.end() && found->second>tier+1){
                    found->second=uint8_t(tier+1);
                    queue.push_back(neighbor);
                }
            }
        }
        std::map<world::Cell,uint32_t> effective;
        for(const auto& [cell,tier]:tiers)
            effective[cell]=flat(cell,s.flatProbeResolution)?
                2u:s.resolutions[tier];
        std::vector<SparseTerrainView> output;
        output.reserve(tiers.size());
        for(const auto& [cell,tier]:tiers){
            SparseTerrainView tile;
            tile.cell=cell;tile.tier=tier;
            tile.resolution=effective.at(cell);
            tile.flat=tile.resolution==2u;
            for(size_t i=0;i<cardinal.size();++i) {
                const auto [dx,dz]=cardinal[i];
                const auto found=effective.find({cell.x+dx,0,cell.z+dz});
                if(found!=effective.end())
                    tile.neighborResolution[i]=found->second;
            }
            output.push_back(tile);
        }
        return output;
    }
private:
    EarthworksLayer& ground_;
    int32_t chunkMetres_=256;
    std::set<world::Cell> chunks_;
    mutable std::map<std::pair<world::Cell,uint32_t>,
                     std::pair<uint64_t,bool>> flatCache_;
};

// Presentation-only visual tessellation. Fine edges use coarse neighbor
// endpoints and interpolate onto the coarse surface. The source of heights
// is shared across all chunks; there are no duplicate authored borders.
// NEVER pass the returned patch to TerrainCollisionPublisher.
inline TerrainPatch bakeSparseTerrainVisual(
        const EarthworksLayer& ground,SparseTerrainView view,
        int32_t chunkMetres) {
    if(view.resolution<2 || view.resolution>129)
        throw std::invalid_argument("TerraWeave visual LOD resolution");
    TerrainPatch patch=bakeEarthworksTerrainPatch(
        ground,view.cell,chunkMetres,view.resolution);
    patch.presentationOnly=true;
    const uint32_t n=patch.resolution;
    for(size_t side=0;side<4;++side){
        const uint32_t coarse=view.neighborResolution[side];
        if(!coarse || coarse>=n)continue;
        if(coarse<2 || (n-1)%(coarse-1)!=0)
            throw std::invalid_argument("TerraWeave mismatched LOD seam");
        const uint32_t step=(n-1)/(coarse-1);
        const auto coord=[n,side](uint32_t i){
            if(side==0)return std::pair<uint32_t,uint32_t>{0,i};
            if(side==1)return std::pair<uint32_t,uint32_t>{n-1,i};
            if(side==2)return std::pair<uint32_t,uint32_t>{i,0};
            return std::pair<uint32_t,uint32_t>{i,n-1};
        };
        for(uint32_t i=1;i+1<n;++i) {
            if(i%step==0)continue;
            const uint32_t lo=i/step*step,hi=lo+step;
            const auto [xa,za]=coord(lo);
            const auto [xb,zb]=coord(hi);
            const auto [x,z]=coord(i);
            const int64_t a=patch.heightsCm[size_t(za)*n+xa];
            const int64_t b=patch.heightsCm[size_t(zb)*n+xb];
            const int64_t sum=a*int64_t(hi-i)+b*int64_t(i-lo);
            const int64_t mag=sum<0?-sum:sum;
            patch.heightsCm[size_t(z)*n+x]=
                int32_t(sum<0?-(mag+step/2)/step:(mag+step/2)/step);
        }
    }
    // The existing TerrainPatch's visual triangles are the GPU mesh source.
    // Rebuild them from stitched heights without altering Earthworks/collision.
    patch.collision.clear();
    patch.collision.reserve(size_t(n-1)*(n-1)*2u);
    const Vec3 origin{Fixed::fromInt(int64_t(view.cell.x)*chunkMetres),
                      {},Fixed::fromInt(int64_t(view.cell.z)*chunkMetres)};
    const auto point=[&](uint32_t x,uint32_t z){
        return origin+Vec3{
            Fixed::fromRatio(int64_t(x)*chunkMetres,n-1),
            Fixed::fromRatio(patch.heightsCm[size_t(z)*n+x],100),
            Fixed::fromRatio(int64_t(z)*chunkMetres,n-1)};
    };
    for(uint32_t z=0;z+1<n;++z)
        for(uint32_t x=0;x+1<n;++x){
            const auto a=point(x,z),b=point(x+1,z);
            const auto c=point(x+1,z+1),d=point(x,z+1);
            patch.collision.push_back({a,c,b});
            patch.collision.push_back({a,d,c});
        }
    return patch;
}
} // namespace aiverse::terraweave
