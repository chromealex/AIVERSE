// CPU-only exact 2D subtraction of resident near-ground rectangles from the
// two original triangles of a Horizon terrain quad. Polygons are clipped in
// double-precision world centimetres and triangulated with their original
// winding, preserving the source distant terrain plane via barycentric sampling.
// The total extra triangle count is strictly bounded; on overflow the caller
// uses the conservative whole-quad fallback. No backend/ECS dependencies.
#pragma once

#include "engine/world/TerraWeaveHorizon.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <optional>
#include <map>
#include <vector>
#include <utility>

namespace aiverse::terraweave {

struct HorizonClipPoint { double x=0,z=0; };
struct HorizonClipTriangle {
    uint32_t quadIndex=0;
    uint8_t sourceHalf=0; // original terrain quad triangle 0 or 1
    std::array<HorizonClipPoint,3> corners{};
};
struct HorizonTerrainClipPlan {
    std::vector<uint8_t> touchedQuads; // N*N: original triangles replaced
    std::vector<HorizonClipTriangle> triangles; // bounded by maxNearClipTriangles
};

namespace horizon_clip_detail {
using Polygon=std::vector<HorizonClipPoint>;

inline double area2(const Polygon& p) {
    if(p.size()<3)return 0.0;
    // Subtract the first point before cross-products. At large absolute
    // world coordinates, direct x*z shoelaces lose small polygon precision.
    double area=0;
    const auto base=p.front();
    for(size_t i=1;i+1<p.size();++i){
        const auto& a=p[i],&b=p[i+1];
        area+=(a.x-base.x)*(b.z-base.z)-
              (a.z-base.z)*(b.x-base.x);
    }
    return area;
}

inline Polygon halfPlane(const Polygon& poly,int axis,double edge,bool greater) {
    Polygon out;
    if(poly.empty())return out;
    out.reserve(poly.size()+2);
    const auto coordinate=[axis](HorizonClipPoint p) {
        return axis==0 ? p.x : p.z;
    };
    const auto inside=[greater,edge](double p) {
        return greater ? p>=edge : p<=edge;
    };
    auto prev=poly.back();
    bool wasInside=inside(coordinate(prev));
    for(const auto& current:poly) {
        const bool isInside=inside(coordinate(current));
        if(isInside!=wasInside){
            const double prevCoord=coordinate(prev);
            const double currCoord=coordinate(current);
            const double delta=currCoord-prevCoord;
            const double t=delta==0 ? 0.0 :
                std::clamp((edge-prevCoord)/delta,0.0,1.0);
            HorizonClipPoint p{prev.x+(current.x-prev.x)*t,
                               prev.z+(current.z-prev.z)*t};
            if(axis==0)p.x=edge;else p.z=edge;
            out.push_back(p);
        }
        if(isInside)out.push_back(current);
        prev=current;
        wasInside=isInside;
    }
    // Shared corner intersections may appear twice if a triangle edge is
    // collinear with a tile boundary; suppress zero-size segments.
    Polygon clean;
    clean.reserve(out.size());
    for(const auto& p:out)
        if(clean.empty() || std::abs(clean.back().x-p.x)>1e-7 ||
                            std::abs(clean.back().z-p.z)>1e-7)
            clean.push_back(p);
    if(clean.size()>1 && std::abs(clean.front().x-clean.back().x)<1e-7 &&
                         std::abs(clean.front().z-clean.back().z)<1e-7)
        clean.pop_back();
    return clean;
}
inline bool overlaps(HorizonClipPoint a,HorizonClipPoint b,const RectCm& rect) {
    return a.x<double(rect.maxX) && b.x>double(rect.minX) &&
           a.z<double(rect.maxZ) && b.z>double(rect.minZ);
}
inline std::pair<HorizonClipPoint,HorizonClipPoint> bounds(const Polygon& p) {
    HorizonClipPoint lo{1e30,1e30},hi{-1e30,-1e30};
    for(const auto& q:p) {
        lo.x=std::min(lo.x,q.x);lo.z=std::min(lo.z,q.z);
        hi.x=std::max(hi.x,q.x);hi.z=std::max(hi.z,q.z);
    }
    return {lo,hi};
}
inline void appendIfArea(std::vector<Polygon>& into,Polygon p) {
    if(p.size()>=3 && std::abs(area2(p))>1e-5)
        into.push_back(std::move(p));
}
inline std::vector<Polygon> subtract(const Polygon& polygon,const RectCm& rect) {
    const auto [min,max]=bounds(polygon);
    if(!overlaps(min,max,rect))return {polygon};
    std::vector<Polygon> out;
    // Partition the outside into mutually disjoint regions: left, right,
    // middle-top and middle-bottom. No triangle may be double counted.
    appendIfArea(out,halfPlane(polygon,0,double(rect.minX),false));
    appendIfArea(out,halfPlane(polygon,0,double(rect.maxX),true));
    auto middle=halfPlane(polygon,0,double(rect.minX),true);
    middle=halfPlane(middle,0,double(rect.maxX),false);
    appendIfArea(out,halfPlane(middle,1,double(rect.minZ),false));
    appendIfArea(out,halfPlane(middle,1,double(rect.maxZ),true));
    return out;
}
} // namespace horizon_clip_detail

// A plan is optional: nullopt explicitly signals too much work / invalid
// near rectangles and requests the preceding conservative quad algorithm.
// This API is reusable by native games and unrelated terrain generators.
inline std::optional<HorizonTerrainClipPlan> planHorizonNearGroundClipping(
        const HorizonBake& bake) {
    HorizonTerrainClipPlan plan;
    if(!bake.maxNearClipTriangles || bake.nearClipRects.empty() ||
       bake.terrainVertices.empty())return plan;
    const uint32_t n=bake.terrainResolution;
    if(!n || n>96 || bake.terrainVertices.size()!=size_t(n+1)*(n+1) ||
       bake.nearClipRects.size()>1024 ||
       (!bake.nearOwnedQuads.empty() &&
         bake.nearOwnedQuads.size()!=size_t(n)*n))
        return std::nullopt;
    plan.touchedQuads.assign(size_t(n)*n,0);
    // Immutable near coverage is a set of non-overlapping half-open XZ
    // rectangles. Each partial coarse quad is split along its two original
    // source triangles to retain exactly their slope and winding.
    for(uint32_t z=0;z<n;++z)
        for(uint32_t x=0;x<n;++x){
            const uint32_t quad=z*n+x;
            if(!bake.nearOwnedQuads.empty()&&bake.nearOwnedQuads[quad])
                continue; // already entirely removed by existing mask
            const size_t a=size_t(z)*(n+1)+x;
            const auto& tl=bake.terrainVertices[a];
            const auto& tr=bake.terrainVertices[a+1];
            const auto& bl=bake.terrainVertices[a+n+1];
            const auto& br=bake.terrainVertices[a+n+2];
            const RectCm cell{tl.xCm,tl.zCm,br.xCm,br.zCm};
            std::vector<const RectCm*> clips;
            for(const auto& rect:bake.nearClipRects)
                if(cell.minX<rect.maxX && cell.maxX>rect.minX &&
                   cell.minZ<rect.maxZ && cell.maxZ>rect.minZ)
                    clips.push_back(&rect);
            if(clips.empty())continue;
            plan.touchedQuads[quad]=1;
            const std::array<std::array<HorizonClipPoint,3>,2> halves{{
                {{{double(tl.xCm),double(tl.zCm)},
                  {double(bl.xCm),double(bl.zCm)},
                  {double(tr.xCm),double(tr.zCm)}}},
                {{{double(tr.xCm),double(tr.zCm)},
                  {double(bl.xCm),double(bl.zCm)},
                  {double(br.xCm),double(br.zCm)}}}
            }};
            for(uint8_t half=0;half<2;++half) {
                std::vector<horizon_clip_detail::Polygon> polygons;
                polygons.emplace_back(halves[half].begin(),
                                      halves[half].end());
                for(const RectCm* rect:clips){
                    std::vector<horizon_clip_detail::Polygon> remaining;
                    for(const auto& poly:polygons){
                        auto outside=horizon_clip_detail::subtract(poly,*rect);
                        for(auto& part:outside)
                            remaining.push_back(std::move(part));
                        if(remaining.size()>bake.maxNearClipTriangles)
                            return std::nullopt;
                    }
                    polygons=std::move(remaining);
                    if(polygons.empty())break;
                }
                for(const auto& polygon:polygons){
                    for(size_t i=1;i+1<polygon.size();++i){
                        // A half-plane can leave collinear vertices exactly
                        // on a near tile edge. Do not upload degenerate
                        // triangles or count them against geometry budgets.
                        const double ax=polygon[i].x-polygon[0].x;
                        const double az=polygon[i].z-polygon[0].z;
                        const double bx=polygon[i+1].x-polygon[0].x;
                        const double bz=polygon[i+1].z-polygon[0].z;
                        if(std::abs(ax*bz-az*bx)<1e-5)continue;
                        if(plan.triangles.size()>=bake.maxNearClipTriangles)
                            return std::nullopt;
                        plan.triangles.push_back({
                            quad,half,{{polygon[0],polygon[i],polygon[i+1]}}
                        });
                    }
                }
            }
        }
    return plan;
}

// Populate a complete immutable snapshot of near-surface heights at the
// precise perimeter vertices of the clipped far terrain. No renderer or
// WorldScene object is consulted after this bake. If even one required
// height is unavailable, do not attempt a half-welded seam.
inline void populateHorizonNearSeamSamples(
        HorizonBake& bake,const HorizonSurfaceSources* source) {
    bake.nearSeamSamples.clear();
    bake.continuousNearNormalSamples=0;
    bake.localFallbackNearNormalSamples=0;
    bake.nearNormalProvenanceAvailable=false;
    bake.nearWeldSlopeFallback=false;
    // No partial diagnostic result may survive a failed all-or-none
    // height sampling pass or a truncated sample budget.
    const auto discardIncompleteSamples=[&bake]() {
        bake.nearSeamSamples.clear();
        bake.continuousNearNormalSamples=0;
        bake.localFallbackNearNormalSamples=0;
        bake.nearNormalProvenanceAvailable=false;
    };
    if(!source || (!source->nearGroundHeightAtCm &&
                   !source->nearGroundHeightAtQ16Cm) ||
       bake.nearClipRects.empty() || !bake.maxNearClipTriangles)
        return;
    const auto planned=planHorizonNearGroundClipping(bake);
    if(!planned)return; // geometric limit, old full-quad path is used
    bake.nearNormalProvenanceAvailable=true;
    constexpr size_t kMaxSamples=4096;
    std::map<std::pair<int64_t,int64_t>,int32_t> samples;
    std::map<std::pair<int64_t,int64_t>,std::pair<int32_t,int32_t>>
        nearSlopes;
    for(const HorizonClipTriangle& tri:planned->triangles) {
        for(const HorizonClipPoint& p:tri.corners) {
            const RectCm* rectAtEdge=nullptr;
            for(const RectCm& r:bake.nearClipRects) {
                constexpr double epsilon=0.00001;
                const bool vertical=(
                    std::abs(p.x-double(r.minX))<epsilon ||
                    std::abs(p.x-double(r.maxX))<epsilon) &&
                    p.z>=double(r.minZ)-epsilon &&
                    p.z<=double(r.maxZ)+epsilon;
                const bool horizontal=(
                    std::abs(p.z-double(r.minZ))<epsilon ||
                    std::abs(p.z-double(r.maxZ))<epsilon) &&
                    p.x>=double(r.minX)-epsilon &&
                    p.x<=double(r.maxX)+epsilon;
                if(vertical||horizontal) {rectAtEdge=&r;break;}
            }
            if(!rectAtEdge)continue;
            const auto key=std::pair<int64_t,int64_t>{
                static_cast<int64_t>(std::llround(p.x*65536.0)),
                static_cast<int64_t>(std::llround(p.z*65536.0))};
            if(samples.contains(key))continue;
            if(samples.size()>=kMaxSamples){
                discardIncompleteSamples();
                return;
            }
            // Half-open authored tiles: clamp the exact Q16 position
            // within the SAME known resident rectangle. High edges require
            // only one Q16 unit of inset (1/65536 cm), not the historical
            // full-centimetre offset which produced height errors on slopes.
            const auto& r=*rectAtEdge;
            constexpr int64_t scale=65536;
            const int64_t loX=r.minX*scale,hiX=r.maxX*scale-1;
            const int64_t loZ=r.minZ*scale,hiZ=r.maxZ*scale-1;
            const int64_t xq=std::clamp(key.first,loX,hiX);
            const int64_t zq=std::clamp(key.second,loZ,hiZ);
            const auto height=source->nearGroundHeightAtQ16Cm ?
                source->nearGroundHeightAtQ16Cm(xq,zq) :
                source->nearGroundHeightAtCm(
                    std::clamp(static_cast<int64_t>(std::llround(p.x)),
                               r.minX,r.maxX-1),
                    std::clamp(static_cast<int64_t>(std::llround(p.z)),
                               r.minZ,r.maxZ-1));
            if(!height || *height < -kWorldLimitCm ||
                          *height > kWorldLimitCm){
                discardIncompleteSamples();
                return; // no half-weld or misleading partial provenance
            }
            samples.emplace(key,*height);
            if(source->nearGroundContinuousSlopeAtQ16Cm){
                // Prefer continuous neighboring tile normals only when the
                // authored source verifies the entire stencil is seamless.
                if(auto slope=source->nearGroundContinuousSlopeAtQ16Cm(
                        xq,zq)){
                    nearSlopes.emplace(key,*slope);
                    ++bake.continuousNearNormalSamples;
                }
            }
            if(source->nearGroundHeightAtQ16Cm &&
               !nearSlopes.contains(key)){
                // Fallback to tile-local derivatives at missing/cliff edges.
                // One-sided derivatives stay inside the same authored
                // triangle mesh at the tile edge. The Q16 stencil is
                // wide enough to avoid 1cm height quantization noise.
                constexpr int64_t stencil=200*scale; // +/- 2 metres
                const int64_t xl=std::max(loX,xq-stencil);
                const int64_t xr=std::min(hiX,xq+stencil);
                const int64_t zl=std::max(loZ,zq-stencil);
                const int64_t zr=std::min(hiZ,zq+stencil);
                const auto hx0=source->nearGroundHeightAtQ16Cm(xl,zq);
                const auto hx1=source->nearGroundHeightAtQ16Cm(xr,zq);
                const auto hz0=source->nearGroundHeightAtQ16Cm(xq,zl);
                const auto hz1=source->nearGroundHeightAtQ16Cm(xq,zr);
                if(xr>xl && zr>zl && hx0 && hx1 && hz0 && hz1){
                    const auto slope=[](int32_t left,int32_t right,
                                             int64_t distanceQ16) {
                        const int64_t diff=(int64_t(left)-right)*1000*scale;
                        return static_cast<int32_t>(std::clamp<int64_t>(
                            diff/distanceQ16,-1000000,1000000));
                    };
                    nearSlopes.emplace(key,std::pair<int32_t,int32_t>{
                        slope(*hx0,*hx1,xr-xl),
                        slope(*hz0,*hz1,zr-zl)
                    });
                    ++bake.localFallbackNearNormalSamples;
                }
            }
        }
    }
    // Preflight all would-be welded triangles before committing a SINGLE
    // seam sample. A true height match is preserved wherever we weld, but
    // a nearly vertical newly introduced wedge is worse than the original
    // horizontal clip. An impossible region reverts entirely to the prior
    // exact X/Z cutout, rather than clamping Y and reintroducing a crack.
    if(bake.maxNearWeldSlopePermille && !samples.empty()){
        const uint32_t n=bake.terrainResolution;
        for(const auto& triangle:planned->triangles){
            const uint32_t x=triangle.quadIndex%n;
            const uint32_t z=triangle.quadIndex/n;
            const size_t i=size_t(z)*(n+1)+x,step=n+1;
            const std::array<size_t,3> srcIds=triangle.sourceHalf==0 ?
                std::array<size_t,3>{i,i+step,i+1} :
                std::array<size_t,3>{i+1,i+step,i+step+1};
            const auto& pa=bake.terrainVertices[srcIds[0]];
            const auto& pb=bake.terrainVertices[srcIds[1]];
            const auto& pc=bake.terrainVertices[srcIds[2]];
            const double ax=double(pb.xCm-pa.xCm);
            const double az=double(pb.zCm-pa.zCm);
            const double bx=double(pc.xCm-pa.xCm);
            const double bz=double(pc.zCm-pa.zCm);
            const double baseArea=ax*bz-az*bx;
            if(std::abs(baseArea)<1e-7){
                bake.nearWeldSlopeFallback=true;
                bake.continuousNearNormalSamples=0;
                bake.localFallbackNearNormalSamples=0;
                return;
            }
            std::array<double,3> original{},welded{};
            for(size_t j=0;j<3;++j){
                const auto p=triangle.corners[j];
                const double px=p.x-double(pa.xCm);
                const double pz=p.z-double(pa.zCm);
                const double w1=(px*bz-pz*bx)/baseArea;
                const double w2=(ax*pz-az*px)/baseArea;
                const double y=(1.0-w1-w2)*pa.heightCm+
                    w1*pb.heightCm+w2*pc.heightCm;
                original[j]=welded[j]=y;
                const auto key=std::pair<int64_t,int64_t>{
                    static_cast<int64_t>(std::llround(p.x*65536.0)),
                    static_cast<int64_t>(std::llround(p.z*65536.0))};
                if(auto it=samples.find(key);it!=samples.end())
                    welded[j]=it->second;
            }
            const auto gradient=[&](const std::array<double,3>& y) {
                const auto& p0=triangle.corners[0];
                const auto& p1=triangle.corners[1];
                const auto& p2=triangle.corners[2];
                const double u=p1.x-p0.x,v=p1.z-p0.z;
                const double w=p2.x-p0.x,q=p2.z-p0.z;
                const double det=u*q-v*w;
                if(std::abs(det)<1e-7)return 0.0;
                const double dy1=y[1]-y[0],dy2=y[2]-y[0];
                const double dx=(dy1*q-dy2*v)/det;
                const double dz=(dy2*u-dy1*w)/det;
                return std::hypot(dx,dz)*1000.0;
            };
            const double baseSlope=gradient(original);
            const double newSlope=gradient(welded);
            if(!std::isfinite(newSlope) ||
               (newSlope>double(bake.maxNearWeldSlopePermille) &&
                newSlope>baseSlope+0.001)){
                bake.nearWeldSlopeFallback=true;
                // The sampled data exists, but no seam normal is applied
                // when the entire height-weld is rejected.
                bake.continuousNearNormalSamples=0;
                bake.localFallbackNearNormalSamples=0;
                return;
            }
        }
    }
    bake.nearSeamSamples.reserve(samples.size());
    for(const auto& [key,height]:samples){
        HorizonBake::NearSeamSample sample{key.first,key.second,height};
        if(const auto s=nearSlopes.find(key);s!=nearSlopes.end()){
            sample.nearSlopeXPermille=s->second.first;
            sample.nearSlopeZPermille=s->second.second;
            sample.nearNormalValid=true;
        }
        bake.nearSeamSamples.push_back(sample);
    }
}

} // namespace aiverse::terraweave
