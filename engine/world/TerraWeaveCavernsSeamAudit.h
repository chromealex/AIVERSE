#pragma once
// Stage 12: deterministic geometric seam diagnostics for cave surface chunks.
// Checks SAME-resolution shared-boundary edge segments, not LOD transitions.
#include "engine/world/TerraWeaveCavernsMesh.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <stdexcept>
#include <utility>
namespace aiverse::terraweave::caverns {
struct SeamPoint {
    int64_t x=0,y=0,z=0;
    constexpr bool operator==(const SeamPoint&)const=default;
    constexpr auto operator<=>(const SeamPoint&)const=default;
};
struct SeamEdge {
    SeamPoint a{},b{};
    constexpr bool operator==(const SeamEdge&)const=default;
    constexpr auto operator<=>(const SeamEdge&)const=default;
};
struct CaveSeamAudit {
    size_t leftBoundaryEdges=0,rightBoundaryEdges=0;
    size_t unmatchedLeft=0,unmatchedRight=0;
    bool matched()const{return unmatchedLeft==0&&unmatchedRight==0;}
};
inline SeamPoint caveWorldVertexCm(ChunkKey3 k,uint32_t resolution,int32_t spacingCm,
                                    MeshVertex v) {
    const int64_t span=int64_t(resolution)*spacingCm;
    return {k.x*span+int64_t(std::llround(double(v.x)*100.)),
            k.y*span+int64_t(std::llround(double(v.y)*100.)),
            k.z*span+int64_t(std::llround(double(v.z)*100.))};
}
inline std::map<SeamEdge,uint32_t> caveBoundaryEdges(
        const CaveMesh& mesh,uint32_t resolution,int32_t spacingCm,
        unsigned axis,int64_t planeCm) {
    if(!resolution||resolution>64||spacingCm<=0||axis>2||
       mesh.indices.size()%3)
        throw std::invalid_argument("Caverns seam audit settings");
    std::map<SeamEdge,uint32_t> edges;
    const auto coordinate=[axis](SeamPoint p)->int64_t {
        return axis==0?p.x:(axis==1?p.y:p.z);
    };
    for(size_t i=0;i<mesh.indices.size();i+=3) {
        std::array<SeamPoint,3> p;
        for(unsigned j=0;j<3;++j) {
            const uint32_t index=mesh.indices[i+j];
            if(index>=mesh.vertices.size())
                throw std::invalid_argument("Caverns seam mesh index");
            p[j]=caveWorldVertexCm(mesh.key,resolution,spacingCm,mesh.vertices[index]);
        }
        for(unsigned e=0;e<3;++e){
            SeamPoint a=p[e],b=p[(e+1)%3];
            if(a==b||coordinate(a)!=planeCm||coordinate(b)!=planeCm)continue;
            if(b<a)std::swap(a,b);
            ++edges[{a,b}];
        }
    }
    // Duplicate coincident triangle edges inside one chunk cancel, leaving
    // only odd-use boundary edges. This is a diagnostic, not mesh welding.
    for(auto it=edges.begin();it!=edges.end();) {
        if((it->second&1u)==0)it=edges.erase(it);
        else {it->second=1;++it;}
    }
    return edges;
}
inline CaveSeamAudit auditCaveSameLodSeam(
        const CaveMesh& a,const CaveMesh& b,uint32_t resolution,
        int32_t spacingCm,unsigned axis) {
    if(axis>2||!resolution||resolution>64||spacingCm<=0)
        throw std::invalid_argument("Caverns seam axis/settings");
    const auto ka=std::array<int64_t,3>{a.key.x,a.key.y,a.key.z};
    const auto kb=std::array<int64_t,3>{b.key.x,b.key.y,b.key.z};
    for(unsigned i=0;i<3;++i)
        if(kb[i]-ka[i]!=int64_t(i==axis))
            throw std::invalid_argument("Caverns seam chunks are not adjacent");
    const int64_t plane=(ka[axis]+1)*int64_t(resolution)*spacingCm;
    auto left=caveBoundaryEdges(a,resolution,spacingCm,axis,plane);
    auto right=caveBoundaryEdges(b,resolution,spacingCm,axis,plane);
    CaveSeamAudit result;
    result.leftBoundaryEdges=left.size();
    result.rightBoundaryEdges=right.size();
    for(const auto& [edge,count]:left){
        (void)count;
        if(!right.contains(edge))++result.unmatchedLeft;
    }
    for(const auto& [edge,count]:right){
        (void)count;
        if(!left.contains(edge))++result.unmatchedRight;
    }
    return result;
}
} // namespace aiverse::terraweave::caverns
