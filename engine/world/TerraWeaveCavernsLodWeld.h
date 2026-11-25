#pragma once
// Stage 13: welded presentation vertices and conservative LOD seam avoidance.
// Instead of speculative Transvoxel transitions, promote touching chunks to
// a common sample spacing. STRATA owns visual rendering, never collision.
#include "engine/world/TerraWeaveCavernsSeamAudit.h"
#include "engine/world/TerraWeaveCavernsStreaming.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <queue>
#include <stdexcept>
namespace aiverse::terraweave::caverns {
struct WeldResult { CaveMesh mesh; size_t merged=0,degenerates=0; };
inline WeldResult weldCaveMesh(const CaveMesh& source) {
    if(source.indices.size()%3)throw std::invalid_argument("Caverns weld triangle indices");
    WeldResult result;result.mesh.key=source.key;
    std::map<SeamPoint,uint32_t> lookup;
    std::vector<uint32_t> remap;
    std::vector<std::array<double,3>> normals;
    remap.reserve(source.vertices.size());
    for(const auto& v:source.vertices) {
        if(!std::isfinite(v.x)||!std::isfinite(v.y)||!std::isfinite(v.z)||
           !std::isfinite(v.nx)||!std::isfinite(v.ny)||!std::isfinite(v.nz))
            throw std::invalid_argument("Caverns nonfinite mesh");
        const SeamPoint p{int64_t(std::llround(double(v.x)*100)),
            int64_t(std::llround(double(v.y)*100)),int64_t(std::llround(double(v.z)*100))};
        auto [it,inserted]=lookup.try_emplace(p,uint32_t(result.mesh.vertices.size()));
        if(inserted) { result.mesh.vertices.push_back(v);normals.push_back({0,0,0}); }
        else ++result.merged;
        remap.push_back(it->second);
        auto& n=normals[it->second];n[0]+=v.nx;n[1]+=v.ny;n[2]+=v.nz;
    }
    for(size_t i=0;i<source.indices.size();i+=3){
        const uint32_t a=source.indices[i],b=source.indices[i+1],c=source.indices[i+2];
        if(a>=remap.size()||b>=remap.size()||c>=remap.size())
            throw std::invalid_argument("Caverns invalid weld index");
        const uint32_t x=remap[a],y=remap[b],z=remap[c];
        if(x==y||y==z||x==z){++result.degenerates;continue;}
        result.mesh.indices.insert(result.mesh.indices.end(),{x,y,z});
    }
    for(size_t i=0;i<normals.size();++i){
        const auto& n=normals[i];
        const double length=std::sqrt(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        if(length>0){
            result.mesh.vertices[i].nx=float(n[0]/length);
            result.mesh.vertices[i].ny=float(n[1]/length);
            result.mesh.vertices[i].nz=float(n[2]/length);
        }
    }
    return result;
}
struct CaveLodEntry { ChunkKey3 key{};uint32_t resolution=16;int32_t spacingCm=100; };
struct CaveLodPlan {std::vector<CaveLodEntry> entries;size_t promoted=0;};
// No transitions yet: touching chunks share the finest requested LOD.
// This is a conservative visual policy, NOT a proof of crack-free output;
// still audit real boundary meshes and normals before presentation.
inline CaveLodPlan planCaveSeamSafeLod(std::vector<CaveLodEntry> entries){
    std::map<ChunkKey3,size_t,KeyLess> ids;
    int64_t span=0;
    for(size_t i=0;i<entries.size();++i){
        const auto& e=entries[i];
        if(!e.resolution||e.resolution>64||e.spacingCm<=0)
            throw std::invalid_argument("Caverns LOD settings");
        const int64_t size=int64_t(e.resolution)*e.spacingCm;
        if(!span)span=size;
        if(size!=span||!ids.emplace(e.key,i).second)
            throw std::invalid_argument("Caverns inconsistent LOD chunk");
    }
    CaveLodPlan out;out.entries=std::move(entries);
    std::vector<uint8_t> visited(out.entries.size(),0);
    constexpr std::array<ChunkKey3,6> directions{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
    for(size_t i=0;i<out.entries.size();++i){
        if(visited[i])continue;
        std::queue<size_t> pending;pending.push(i);visited[i]=1;
        std::vector<size_t> component;
        int32_t finest=out.entries[i].spacingCm;
        while(!pending.empty()){
            const size_t j=pending.front();pending.pop();
            component.push_back(j);
            finest=std::min(finest,out.entries[j].spacingCm);
            const auto k=out.entries[j].key;
            for(const auto d:directions){
                const ChunkKey3 neighbor{k.x+d.x,k.y+d.y,k.z+d.z};
                const auto it=ids.find(neighbor);
                if(it!=ids.end()&&!visited[it->second]){
                    visited[it->second]=1;pending.push(it->second);
                }
            }
        }
        if(span%finest!=0||span/finest>64)
            throw std::invalid_argument("Caverns LOD resolution budget");
        for(const size_t j:component){
            auto& e=out.entries[j];
            if(e.spacingCm!=finest)++out.promoted;
            e.spacingCm=finest;e.resolution=uint32_t(span/finest);
        }
    }
    std::sort(out.entries.begin(),out.entries.end(),
        [](const auto& a,const auto& b){return KeyLess{}(a.key,b.key);});
    return out;
}
} // namespace aiverse::terraweave::caverns
