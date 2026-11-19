// Immutable render-only adapter for AIVERSE WorldScene's authored terrain
// height tiles. All input/output positions are signed world centimetres.
// Interpolation follows readChunkCollision()'s (a,c,b), (a,d,c) diagonal.
// Does not modify collision physics, navmesh, ECS or game state.
#pragma once

#include "engine/world/WorldAssets.h"
#include "engine/world/TerraWeave.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct AuthoredHeightTileCm {
    uint32_t resolution = 0;
    std::vector<int32_t> heightsCm; // x + z*resolution; copied from WorldScene terrain
};

class AuthoredWorldHeightSnapshot {
public:
    explicit AuthoredWorldHeightSnapshot(int32_t worldCellSizeMetres)
        : cellSizeCm_(int64_t(worldCellSizeMetres) * 100) {
        if (worldCellSizeMetres < 1 || worldCellSizeMetres > 4096)
            throw std::invalid_argument("TerraWeave authored tile cell size");
    }

    void setTile(world::Cell key, AuthoredHeightTileCm tile) {
        if (tile.resolution < 2 || tile.resolution > 129 ||
            tile.heightsCm.size() !=
                size_t(tile.resolution) * size_t(tile.resolution))
            throw std::invalid_argument("TerraWeave authored tile resolution");
        tiles_[key] = std::move(tile);
    }

    size_t tileCount() const { return tiles_.size(); }

    // True ONLY if every WorldScene terrain tile touched by the half-open
    // rectangle is resident and owns rendered ground geometry. A coarse
    // Horizon quad may be omitted only when near geometry fully covers it.
    // Rectangular coverage avoids a circular near-hole that can expose sky
    // when authored terrain tiles have not been streamed yet.
    bool coversRect(int64_t x0,int64_t z0,int64_t x1,int64_t z1) const {
        if(x0>=x1||z0>=z1||x0<-kWorldLimitCm||
           z0<-kWorldLimitCm||x1>kWorldLimitCm||
           z1>kWorldLimitCm)
            return false;
        const int64_t left=detail::floorDiv(x0,cellSizeCm_);
        const int64_t right=detail::floorDiv(x1-1,cellSizeCm_);
        const int64_t top=detail::floorDiv(z0,cellSizeCm_);
        const int64_t bottom=detail::floorDiv(z1-1,cellSizeCm_);
        const int64_t width=right-left+1,height=bottom-top+1;
        if(width<=0||height<=0||width*height>4096)return false;
        for(int64_t z=top;z<=bottom;++z)
            for(int64_t x=left;x<=right;++x)
                if(!tiles_.contains(world::Cell{
                    static_cast<int32_t>(x),0,static_cast<int32_t>(z)}))
                    return false;
        return true;
    }

    // Collect clipped, nonoverlapping rectangles of resident authored
    // ground within a half-open world rectangle. Ownership is exact on X/Z,
    // unlike coversRect(), which only answers whether ALL tiles are loaded.
    // False means there were too many pieces; the caller must fall back to
    // the conservative full-quad ownership path instead of an unbounded mesh.
    bool coveredRectsIn(RectCm area,std::vector<RectCm>& out,
                        size_t maxRects=1024) const {
        out.clear();
        if(area.minX>=area.maxX||area.minZ>=area.maxZ||
           area.minX<-kWorldLimitCm||area.maxX>kWorldLimitCm||
           area.minZ<-kWorldLimitCm||area.maxZ>kWorldLimitCm||
           maxRects>4096)return false;
        for(const auto& [cell,tile]:tiles_){
            (void)tile;
            const int64_t x0=int64_t(cell.x)*cellSizeCm_;
            const int64_t z0=int64_t(cell.z)*cellSizeCm_;
            const RectCm piece{
                std::max(area.minX,x0),std::max(area.minZ,z0),
                std::min(area.maxX,x0+cellSizeCm_),
                std::min(area.maxZ,z0+cellSizeCm_)
            };
            if(piece.minX>=piece.maxX||piece.minZ>=piece.maxZ)continue;
            if(out.size()>=maxRects){out.clear();return false;}
            out.push_back(piece);
        }
        // The resident tile map orders world Cells, not always geometry
        // rectangles. Stable geometric order is needed for deterministic
        // clipping and cross-machine archive hashes.
        std::sort(out.begin(),out.end(),[](const RectCm& a,const RectCm& b){
            if(a.minZ!=b.minZ)return a.minZ<b.minZ;
            if(a.minX!=b.minX)return a.minX<b.minX;
            if(a.maxZ!=b.maxZ)return a.maxZ<b.maxZ;
            return a.maxX<b.maxX;
        });
        return true;
    }

    // Fractional world-centimetre Q16 sample at the exact near/far clipped
    // vertex, using the SAME authored two-triangle diagonal as sample().
    // The old integer API stays unchanged for gameplay/render callers.
    // Weights are normalized to Q16 before multiplying 32-bit heights,
    // keeping all interpolation arithmetic safely inside signed int64.
    std::optional<int32_t> sampleQ16(int64_t xQ16cm,int64_t zQ16cm) const {
        constexpr int64_t scale=65536;
        const int64_t limit=kWorldLimitCm*scale;
        if(xQ16cm < -limit || xQ16cm > limit ||
           zQ16cm < -limit || zQ16cm > limit)
            throw std::out_of_range("TerraWeave authored Q16 terrain extent");
        // Preserve the exact integral-world-centimetre interpolation
        // contract, including rounding of negative heights.
        if(xQ16cm%scale==0 && zQ16cm%scale==0)
            return sample(xQ16cm/scale,zQ16cm/scale);
        const int64_t step=cellSizeCm_*scale;
        const int64_t cx=detail::floorDiv(xQ16cm,step);
        const int64_t cz=detail::floorDiv(zQ16cm,step);
        const auto it=tiles_.find(world::Cell{
            static_cast<int32_t>(cx),0,static_cast<int32_t>(cz)});
        if(it==tiles_.end())return std::nullopt;
        const auto& tile=it->second;
        const int64_t localX=xQ16cm-cx*step;
        const int64_t localZ=zQ16cm-cz*step;
        const int64_t gx=localX*(tile.resolution-1);
        const int64_t gz=localZ*(tile.resolution-1);
        const size_t ix=static_cast<size_t>(gx/step);
        const size_t iz=static_cast<size_t>(gz/step);
        const int64_t fx=(gx%step)*scale/step;
        const int64_t fz=(gz%step)*scale/step;
        const auto at=[&](size_t x,size_t z){
            return int64_t(tile.heightsCm[z*tile.resolution+x]);
        };
        const int64_t a=at(ix,iz),b=at(ix+1,iz);
        const int64_t d=at(ix,iz+1),v=at(ix+1,iz+1);
        const int64_t sum=fx>=fz ?
            a*(scale-fx)+b*(fx-fz)+v*fz :
            a*(scale-fz)+d*(fz-fx)+v*fx;
        const int64_t positive=sum<0 ? -sum : sum;
        const int64_t rounded=(positive+scale/2)/scale;
        return static_cast<int32_t>(sum<0 ? -rounded : rounded);
    }

    // Stage 22: a bounded neighbor-aware Q16 normal probe. Unlike the
    // tile-local Horizon stencil, this can span a resident tile boundary,
    // but only if both sides agree on the boundary height. Discontinuous
    // authored cliffs deliberately return nullopt rather than smoothing
    // unrelated surfaces. The result is a signed slope in permille, matching
    // HorizonBake::NearSeamSample's convention (-dY/dX, -dY/dZ).
    std::optional<std::pair<int32_t,int32_t>> continuousSlopeQ16(
            int64_t xQ16cm,int64_t zQ16cm,
            int64_t radiusCm=200,int32_t toleranceCm=1) const {
        constexpr int64_t q=65536;
        if(radiusCm<1 || radiusCm>4096 || toleranceCm<0 ||
           toleranceCm>100)return std::nullopt;
        const int64_t extent=kWorldLimitCm*q;
        if(xQ16cm < -extent || xQ16cm > extent ||
           zQ16cm < -extent || zQ16cm > extent)
            return std::nullopt;
        const auto center=sampleQ16(xQ16cm,zQ16cm);
        if(!center)return std::nullopt;
        const int64_t step=radiusCm*q;
        const int64_t xl=std::max(-extent,xQ16cm-step);
        const int64_t xr=std::min(extent,xQ16cm+step);
        const int64_t zl=std::max(-extent,zQ16cm-step);
        const int64_t zr=std::min(extent,zQ16cm+step);
        if(xl==xr || zl==zr)return std::nullopt;
        const auto hx0=sampleQ16(xl,zQ16cm);
        const auto hx1=sampleQ16(xr,zQ16cm);
        const auto hz0=sampleQ16(xQ16cm,zl);
        const auto hz1=sampleQ16(xQ16cm,zr);
        if(!hx0 || !hx1 || !hz0 || !hz1)return std::nullopt;
        // Verify every crossed tile seam independently. A missing neighbor
        // or a discontinuity must not be hidden by a central difference.
        const auto continuous=[&](int64_t a,int64_t b,
                                   int64_t fixed,bool alongX) {
            const int64_t lo=std::min(a,b),hi=std::max(a,b);
            const int64_t tileStep=cellSizeCm_*q;
            int64_t boundary=(detail::floorDiv(lo,tileStep)+1)*tileStep;
            for(;boundary<=hi;boundary+=tileStep){
                if(boundary<=-extent || boundary>=extent)return false;
                const auto left=alongX ?
                    sampleQ16(boundary-1,fixed) :
                    sampleQ16(fixed,boundary-1);
                const auto right=alongX ?
                    sampleQ16(boundary,fixed) :
                    sampleQ16(fixed,boundary);
                if(!left || !right ||
                   std::abs(int64_t(*left)-*right)>toleranceCm)
                    return false;
            }
            return true;
        };
        if(!continuous(xl,xr,zQ16cm,true) ||
           !continuous(zl,zr,xQ16cm,false))return std::nullopt;
        const auto slope=[](int32_t a,int32_t b,int64_t distance) {
            return static_cast<int32_t>(std::clamp<int64_t>(
                (int64_t(a)-b)*1000*q/distance,-1000000,1000000));
        };
        return std::pair<int32_t,int32_t>{
            slope(*hx0,*hx1,xr-xl),slope(*hz0,*hz1,zr-zl)};
    }

    // A null result means that no loaded WorldScene heightfield covers this
    // point. Caller may fall back to deterministic procedural height.
    // The same two triangles as the terrain collider are interpolated in
    // integer centimetres; no bilinear rounding across a diagonal.
    std::optional<int32_t> sample(int64_t xCm, int64_t zCm) const {
        if (xCm < -kWorldLimitCm || xCm > kWorldLimitCm ||
            zCm < -kWorldLimitCm || zCm > kWorldLimitCm)
            throw std::out_of_range("TerraWeave authored terrain world extent");
        const int64_t cx = detail::floorDiv(xCm, cellSizeCm_);
        const int64_t cz = detail::floorDiv(zCm, cellSizeCm_);
        const auto it = tiles_.find(world::Cell{
            static_cast<int32_t>(cx), 0, static_cast<int32_t>(cz)});
        if (it == tiles_.end()) return std::nullopt;
        const AuthoredHeightTileCm& tile = it->second;
        const int64_t localX = xCm - cx * cellSizeCm_;
        const int64_t localZ = zCm - cz * cellSizeCm_;
        const int64_t gx = localX * (tile.resolution - 1);
        const int64_t gz = localZ * (tile.resolution - 1);
        const size_t ix = static_cast<size_t>(gx / cellSizeCm_);
        const size_t iz = static_cast<size_t>(gz / cellSizeCm_);
        const int64_t fx = gx % cellSizeCm_;
        const int64_t fz = gz % cellSizeCm_;
        // localX/localZ are half-open; ix and iz cannot exceed resolution-2.
        const auto at = [&](size_t x, size_t z) {
            return int64_t(tile.heightsCm[z * tile.resolution + x]);
        };
        const int64_t a = at(ix, iz);
        const int64_t b = at(ix + 1, iz);
        const int64_t c = at(ix + 1, iz + 1);
        const int64_t d = at(ix, iz + 1);
        const int64_t weighted = fx >= fz
            ? a * (cellSizeCm_ - fx) + b * (fx - fz) + c * fz
            : a * (cellSizeCm_ - fz) + d * (fz - fx) + c * fx;
        // Round to nearest signed centimetre, never truncate negative Y
        // toward zero differently from positive Y.
        const int64_t absNum = weighted >= 0 ? weighted : -weighted;
        const int64_t rounded = (absNum + cellSizeCm_ / 2) / cellSizeCm_;
        return static_cast<int32_t>(weighted >= 0 ? rounded : -rounded);
    }

private:
    int64_t cellSizeCm_ = 0;
    std::map<world::Cell, AuthoredHeightTileCm> tiles_;
};

} // namespace aiverse::terraweave
