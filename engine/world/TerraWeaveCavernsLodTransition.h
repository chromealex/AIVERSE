#pragma once
// TerraWeave Caverns Stage 14: certified mixed-LOD boundary-edge subdivision.
// Supports both face orientations where collinear fine edges partition each coarse edge.
// If contours diverge, decline rather than fabricate a non-manifold bridge.
// The resulting mesh is PRESENTATION ONLY. Authoritative physics remains unchanged.
#include "engine/world/TerraWeaveCavernsSeamAudit.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <stdexcept>
#include <vector>
namespace aiverse::terraweave::caverns {
struct CaveTransitionResult {
    CaveMesh coarseStitched;
    size_t splitTriangles=0;
    bool supported=false;
};
inline int64_t seamCoord(SeamPoint p,unsigned axis){
    return axis==0?p.x:(axis==1?p.y:p.z);
}
inline bool seamOnSegment(SeamPoint p,SeamPoint a,SeamPoint b) {
    const int64_t ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
    const int64_t vx=p.x-a.x,vy=p.y-a.y,vz=p.z-a.z;
    // Cave chunks are max 640,000 cm wide; cross/dot products fit int64.
    if(uy*vz!=uz*vy || uz*vx!=ux*vz || ux*vy!=uy*vx)return false;
    const int64_t projection=vx*ux+vy*uy+vz*uz;
    const int64_t length=ux*ux+uy*uy+uz*uz;
    return projection>=0 && projection<=length;
}
// Coarse and fine refer to chunk-local metres; world coordinate conversion
// must use each chunk's OWN sampling resolution/spacing.
inline CaveTransitionResult stitchCaveCollinearBoundary(
    const CaveMesh& coarse,uint32_t coarseResolution,int32_t coarseSpacingCm,
    const CaveMesh& fine,uint32_t fineResolution,int32_t fineSpacingCm,
    unsigned axis) {
    if(axis>2||coarseResolution==0||fineResolution==0||
       coarseResolution>64||fineResolution>64||
       coarseSpacingCm<=0||fineSpacingCm<=0||
       int64_t(coarseResolution)*coarseSpacingCm!=int64_t(fineResolution)*fineSpacingCm||
       coarseSpacingCm<fineSpacingCm)
        throw std::invalid_argument("Caverns mixed LOD input");
    const std::array<int64_t,3> a{coarse.key.x,coarse.key.y,coarse.key.z};
    const std::array<int64_t,3> b{fine.key.x,fine.key.y,fine.key.z};
    int direction=0;
    for(unsigned d=0;d<3;++d) {
        const int64_t delta=b[d]-a[d];
        if(d==axis) {
            if(delta!=1 && delta!=-1)
                throw std::invalid_argument("Caverns mixed LOD adjacency");
            direction=delta>0?1:-1;
        } else if(delta!=0)
            throw std::invalid_argument("Caverns mixed LOD adjacency");
    }
    // Both orientations are supported: coarse on negative or positive side
    // of X/Y/Z. The shared world plane is the HIGH boundary of the negative
    // chunk, regardless of which one is coarser.
    const int64_t span=int64_t(coarseResolution)*coarseSpacingCm;
    const int64_t plane=(a[axis]+(direction>0?1:0))*span;
    const auto coarseEdges=caveBoundaryEdges(coarse,coarseResolution,coarseSpacingCm,axis,plane);
    const auto fineEdges=caveBoundaryEdges(fine,fineResolution,fineSpacingCm,axis,plane);
    CaveTransitionResult out;out.coarseStitched=coarse;
    if(coarseEdges==fineEdges){out.supported=true;return out;}
    if(coarseEdges.empty()||fineEdges.empty())return out;
    // Each fine boundary edge must lie in exactly one coarse boundary edge.
    std::map<SeamEdge,std::vector<SeamPoint>> splitPoints;
    for(const auto& [ce,count]:coarseEdges){
        (void)count;splitPoints[ce]={ce.a,ce.b};
    }
    for(const auto& [fe,count]:fineEdges){
        (void)count;
        size_t owners=0;
        for(auto& [ce,pts]:splitPoints){
            if(seamOnSegment(fe.a,ce.a,ce.b)&&seamOnSegment(fe.b,ce.a,ce.b)){
                pts.push_back(fe.a);pts.push_back(fe.b);++owners;
            }
        }
        if(owners!=1)return out; // divergent or ambiguous contour
    }
    for(auto& [ce,pts]:splitPoints){
        const double vx=double(ce.b.x-ce.a),vy=double(ce.b.y-ce.a),vz=double(ce.b.z-ce.a);
        auto param=[&](SeamPoint p){
            return (double(p.x-ce.a.x)*vx+double(p.y-ce.a.y)*vy+
                    double(p.z-ce.a.z)*vz)/(vx*vx+vy*vy+vz*vz);
        };
        std::sort(pts.begin(),pts.end(),[&](auto x,auto y){return param(x)<param(y);});
        pts.erase(std::unique(pts.begin(),pts.end()),pts.end());
        // Require an uninterrupted partition of each coarse edge by fine edges.
        for(size_t i=1;i<pts.size();++i){
            SeamEdge segment{pts[i-1],pts[i]};
            if(segment.b<segment.a)std::swap(segment.a,segment.b);
            if(!fineEdges.contains(segment))return out;
        }
    }
    CaveMesh candidate;candidate.key=coarse.key;
    candidate.vertices=coarse.vertices;
    candidate.indices.reserve(coarse.indices.size()+fineEdges.size()*3);
    const auto pushTriangle=[&](uint32_t x,uint32_t y,uint32_t z){
        candidate.indices.push_back(x);candidate.indices.push_back(y);candidate.indices.push_back(z);
    };
    for(size_t i=0;i<coarse.indices.size();i+=3){
        if(coarse.indices.size()%3)throw std::invalid_argument("Caverns mixed LOD indices");
        const uint32_t tri[3]={coarse.indices[i],coarse.indices[i+1],coarse.indices[i+2]};
        for(uint32_t v:tri)if(v>=coarse.vertices.size())
            throw std::invalid_argument("Caverns mixed LOD vertex index");
        int hit=-1;SeamEdge boundary{};
        for(int e=0;e<3;++e){
            auto p=caveWorldVertexCm(coarse.key,coarseResolution,coarseSpacingCm,coarse.vertices[tri[e]]);
            auto q=caveWorldVertexCm(coarse.key,coarseResolution,coarseSpacingCm,coarse.vertices[tri[(e+1)%3]]);
            if(p==q||seamCoord(p,axis)!=plane||seamCoord(q,axis)!=plane)continue;
            if(q<p)std::swap(p,q);
            SeamEdge edge{p,q};
            if(splitPoints.contains(edge)){
                if(hit!=-1)return out; // multi-boundary triangle: decline
                hit=e;boundary=edge;
            }
        }
        if(hit<0){pushTriangle(tri[0],tri[1],tri[2]);continue;}
        const auto& pts=splitPoints.at(boundary);
        if(pts.size()==2){pushTriangle(tri[0],tri[1],tri[2]);continue;}
        const uint32_t start=tri[hit],end=tri[(hit+1)%3],opposite=tri[(hit+2)%3];
        const auto startWorld=caveWorldVertexCm(coarse.key,coarseResolution,coarseSpacingCm,coarse.vertices[start]);
        const bool forward=startWorld==boundary.a;
        std::vector<uint32_t> path{start};
        const auto& va=coarse.vertices[start];const auto& vb=coarse.vertices[end];
        for(size_t step=1;step+1<pts.size();++step){
            const SeamPoint p=forward?pts[step]:pts[pts.size()-1-step];
            const auto origin=startWorld;
            const auto finish=caveWorldVertexCm(coarse.key,coarseResolution,coarseSpacingCm,vb);
            const double dx=double(finish.x-origin.x),dy=double(finish.y-origin.y),dz=double(finish.z-origin.z);
            const double t=(double(p.x-origin.x)*dx+double(p.y-origin.y)*dy+
                            double(p.z-origin.z)*dz)/(dx*dx+dy*dy+dz*dz);
            MeshVertex v=va;
            v.x=float((double(p.x-coarse.key.x*int64_t(coarseResolution)*coarseSpacingCm))/100.);
            v.y=float((double(p.y-coarse.key.y*int64_t(coarseResolution)*coarseSpacingCm))/100.);
            v.z=float((double(p.z-coarse.key.z*int64_t(coarseResolution)*coarseSpacingCm))/100.);
            v.nx=float(va.nx+(vb.nx-va.nx)*t);
            v.ny=float(va.ny+(vb.ny-va.ny)*t);
            v.nz=float(va.nz+(vb.nz-va.nz)*t);
            const float len=std::sqrt(v.nx*v.nx+v.ny*v.ny+v.nz*v.nz);
            if(len>0){v.nx/=len;v.ny/=len;v.nz/=len;}
            path.push_back(uint32_t(candidate.vertices.size()));
            candidate.vertices.push_back(v);
        }
        path.push_back(end);
        for(size_t j=1;j<path.size();++j)pushTriangle(path[j-1],path[j],opposite);
        ++out.splitTriangles;
    }
    const auto resultEdges=caveBoundaryEdges(candidate,coarseResolution,coarseSpacingCm,axis,plane);
    if(resultEdges!=fineEdges)return out;
    out.coarseStitched=std::move(candidate);
    out.supported=true;
    return out;
}
} // namespace aiverse::terraweave::caverns
