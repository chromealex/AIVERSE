// AIVERSE Studio — CPU terrain preview adapter for the current MapDoc document schema.
// No ImGui, GPU APIs, or genre-specific rendering calls. Not a replacement for
// the engine's full terrain/detail/water/planet renderer.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include "engine/view/game/GameTerrainSurface.h"
#include "apps/editor/core/TerraWeavePlanetCavernsDoc.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/TerraWeaveCavernsArchive.h"
#include "engine/world/TerraWeaveCavernsSurfaceCut.h"
#include "apps/studio-native/StudioPlanetCavernsRim.h"
#include <string>
#include <limits>
#include <vector>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace aiverse::studio {
struct TerrainPreviewStats {
    int step = 0;
    size_t quads = 0;
    size_t triangles=0,cutEntrances=0,rejectedEntrances=0;
    bool handled=false; // valid heightfield, including an intentionally empty cut
    std::string cutDiagnostic;
};

inline float terrainPreviewCorner(const editor::MapData& d, int x, int z, float levelHeight = 1.5f) {
    if (d.w <= 0 || d.h <= 0) return 0.0f;
    x = std::clamp(x, 0, d.w);
    z = std::clamp(z, 0, d.h);
    const size_t cornerCount = size_t(d.w + 1) * size_t(d.h + 1);
    if (d.corners.size() == cornerCount)
        return float(d.corners[size_t(z) * size_t(d.w + 1) + size_t(x)]) / 16.0f;
    const int cx = std::clamp(x, 0, d.w - 1);
    const int cz = std::clamp(z, 0, d.h - 1);
    const size_t offset = size_t(cz) * size_t(d.w) + size_t(cx);
    const float fine = offset < d.heights.size() ? float(d.heights[offset]) / 16.0f : 0.0f;
    const float cliff = offset < d.levels.size() ? float(d.levels[offset]) * levelHeight : 0.0f;
    return fine + cliff;
}

inline int terrainDetailScale(const editor::MapData& d) {
    const int k=d.detailScale;
    return k>0 && k<=8 && d.w>0 && d.h>0 && d.w<INT32_MAX/k && d.h<INT32_MAX/k &&
        (uint64_t(d.w)*k+1)*(uint64_t(d.h)*k+1)==d.detail.size() ? k : 0;
}

inline float terrainPreviewHeight(const editor::MapData& d, float x, float z, float levelHeight = 1.5f, bool baseGrid=false) {
    if (d.w <= 0 || d.h <= 0) return 0;
    x = std::clamp(x, 0.0f, float(d.w));
    z = std::clamp(z, 0.0f, float(d.h));
    if (const int k=baseGrid ? 0 : terrainDetailScale(d)) {
        const int width=d.w*k, height=d.h*k;
        const float sx=x*k, sz=z*k;
        const int ix=int(sx), iz=int(sz), jx=std::min(ix+1,width), jz=std::min(iz+1,height);
        auto h=[&](int a,int b){return d.detail[size_t(b)*(width+1)+a]/16.f;};
        return game::terrain::quadSurface(h(ix,iz),h(jx,iz),h(ix,jz),h(jx,jz),sx-ix,sz-iz);
    }
    const int x0 = int(std::floor(x)), z0 = int(std::floor(z));
    const int x1 = std::min(x0 + 1, d.w), z1 = std::min(z0 + 1, d.h);
    const float tx = x - float(x0), tz = z - float(z0);
    const float a = terrainPreviewCorner(d, x0, z0, levelHeight);
    const float b = terrainPreviewCorner(d, x1, z0, levelHeight);
    const float c = terrainPreviewCorner(d, x1, z1, levelHeight);
    const float e = terrainPreviewCorner(d, x0, z1, levelHeight);
    return (a * (1-tx) + b * tx) * (1-tz) + (e * (1-tx) + c * tx) * tz;
}

inline uint32_t terrainPreviewColor(char kind) {
    using render::rgba;
    switch (kind) {
        case '~': return rgba(36,87,137);  // water
        case '=': return rgba(65,116,150); // shallow
        case '#': return rgba(105,113,127); // blocked/cliff
        case 'R': return rgba(112,102,94); // stone
        case ',': return rgba(154,128,79); // no build
        case '/': return rgba(118,130,107); // ramp
        case '+': return rgba(113,103,89); // road
        default:  return rgba(86,122,98); // ground or custom terrain
    }
}

// Both the ordinary mesh and the PBR lighting receiver use this same
// CPU triangle source. No separate fake hole mask or terrain replacement.
// The chart inspector draws the actual coarse spherical triangles in
// local tangent coordinates. Leave the surface intact until the planetary
// volume/collar preview has a matching native seam certificate.
inline TerrainPreviewStats buildPlanetChartPreview(const editor::MapData& map,
        render::MeshBuilder& out,uint64_t revision){
    TerrainPreviewStats stats;stats.handled=true;
    try{
        const auto region=editor::planet_caves::read(map);
        const auto patch=editor::planet_caves::captureLocalSurface(map,region.anchor,revision);
        const auto point=[](Vec3 v){return render::float3{float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};};
        for(const auto& t:patch.chartTriangles){
            const auto a=point(t.a),b=point(t.b),c=point(t.c);
            const auto normal=render::normalize(render::cross(b-a,c-a));
            const auto color=render::rgba(86,122,98);
            const auto i=out.addVertex(a,normal,color),j=out.addVertex(b,normal,color),k=out.addVertex(c,normal,color);
            out.addTriangle(i,j,k);
        }
        stats.triangles=patch.chartTriangles.size();
        stats.quads=stats.triangles/2;
        stats.rejectedEntrances=region.volume.entrances.size();
        if(stats.rejectedEntrances)stats.cutDiagnostic="Planet chart solid until matching collar is ready";
    }catch(const std::exception& e){
        out.clear();stats.cutDiagnostic=e.what();
    }
    return stats;
}
inline size_t buildPlanetChartCutPreview(const terraweave::caverns::CaveSurfaceGroupPlan& plan,
        render::MeshBuilder& out){
    if(plan.patches.size()!=1||plan.rims.empty())
        throw std::invalid_argument("Caverns chart cut preview group");
    const auto& triangles=plan.patches.begin()->second.collision;
    const auto point=[](Vec3 v){return render::float3{float(v.x.toDouble()),float(v.y.toDouble()),float(v.z.toDouble())};};
    for(const auto& t:triangles){
        const auto a=point(t.a),b=point(t.b),c=point(t.c);
        const auto n=render::normalize(render::cross(b-a,c-a));
        const auto color=render::rgba(86,122,98);
        const auto i=out.addVertex(a,n,color),j=out.addVertex(b,n,color),k=out.addVertex(c,n,color);
        out.addTriangle(i,j,k);
    }
    return triangles.size();
}
inline TerrainPreviewStats buildTerrainPreview(const editor::MapData& d,
                                               render::MeshBuilder& out,
                                               float levelHeight = 1.5f, bool baseGrid=false) {
    namespace cave=terraweave::caverns;
    using render::float3;
    TerrainPreviewStats stats;
    if(d.extra.getStr("terrainPresentation","legacy")=="entities")return stats;
    if (d.planet() || d.w <= 0 || d.h <= 0 || d.w > 200000 || d.h > 200000) return stats;
    const int detail=baseGrid ? 0 : terrainDetailScale(d);
    const int scale=std::max(1,detail);
    const int width=d.w*scale, height=d.h*scale;
    const int step=std::max(1,(std::max(width,height)+255)/256);
    stats.step=step;stats.handled=true;
    const int nx=(width+step-1)/step,nz=(height+step-1)/step;
    const auto vertex=[&](int sx,int sz){
        const float x=float(sx)/scale,z=float(sz)/scale;
        const float y=detail ? d.detail[size_t(sz)*(width+1)+sx]/16.f : terrainPreviewCorner(d,sx,sz,levelHeight);
        return float3{x,y,z};
    };
    const auto cell=[&](int x,int z){
        const int ex=std::min(width,x+step),ez=std::min(height,z+step);
        return std::array<float3,4>{vertex(x,z),vertex(ex,z),vertex(ex,ez),vertex(x,ez)};
    };
    const auto cm=[](float3 p){return cave::CutVertexCm{double(p.x)*100.,double(p.y)*100.,double(p.z)*100.};};
    constexpr size_t maxTriangles=600000,maxAssociations=262144;
    constexpr unsigned sides=16;
    cave::CaveVolumeSnapshot snapshot;
    std::vector<std::vector<const cave::Entrance*>> bins(size_t(nx)*size_t(nz));
    if(!d.extra["terraweaveCaverns"].isNull()){
        try{
            snapshot=cave::caveSnapshotFromJson(d.extra["terraweaveCaverns"]);
            size_t associations=0,fillChecks=0;
            std::vector<const cave::CaveSphereStamp*> fills;
            for(const auto& stamp:snapshot.stamps)
                if(stamp.mode==cave::CaveStampMode::Fill)fills.push_back(&stamp);
            const int64_t span=int64_t(step)*100;
            for(const auto& entrance:snapshot.entrances){
                const auto bounds=cave::entranceBounds(entrance);
                // Refuse partial map-edge holes rather than claiming a
                // closed seam where the source terrain does not exist.
                if(bounds.minX<0||bounds.minZ<0||bounds.maxX>int64_t(d.w)*100+1||
                   bounds.maxZ>int64_t(d.h)*100+1){++stats.rejectedEntrances;continue;}
                const int ax=int(bounds.minX*scale/span),az=int(bounds.minZ*scale/span);
                const int bx=std::min(nx-1,int((bounds.maxX*scale-1)/span));
                const int bz=std::min(nz-1,int((bounds.maxZ*scale-1)/span));
                if(ax>bx||az>bz){++stats.rejectedEntrances;continue;}
                const size_t count=size_t(bx-ax+1)*size_t(bz-az+1);
                if(count>maxAssociations-associations)throw std::length_error("Caverns terrain cut work budget");
                associations+=count;
                bool valid=true;
                double loY=std::numeric_limits<double>::infinity(),hiY=-loY;
                // All intersected planes must lie inside this authored
                // shaft's vertical SDF extent. Reject the whole entrance,
                // not a partial ring, on a mismatched surface elevation.
                for(int iz=az;valid&&iz<=bz;++iz)for(int ix=ax;valid&&ix<=bx;++ix){
                    const auto v=cell(ix*step,iz*step);
                    for(const auto& p:v){loY=std::min(loY,double(p.y)*100.);hiY=std::max(hiY,double(p.y)*100.);}
                    valid=cave::entranceIntersectsTriangleAtHeight({cm(v[0]),cm(v[2]),cm(v[1])},entrance,sides)&&
                          cave::entranceIntersectsTriangleAtHeight({cm(v[0]),cm(v[3]),cm(v[2])},entrance,sides);
                }
                // Fill wins over carve. Conservatively keep the terrain
                // intact if a fill brush could seal any part of this mouth.
                if(valid)for(const auto* stamp:fills){
                    if(++fillChecks>maxAssociations)throw std::length_error("Caverns entrance fill work budget");
                    const double r=stamp->radiusCm;
                    const auto& p=stamp->center;
                    if(double(p.x)+r>=bounds.minX&&double(p.x)-r<=bounds.maxX&&
                       double(p.z)+r>=bounds.minZ&&double(p.z)-r<=bounds.maxZ&&
                       double(p.y)+r>=loY&&double(p.y)-r<=hiY){valid=false;break;}
                }
                if(!valid){++stats.rejectedEntrances;continue;}
                for(int iz=az;iz<=bz;++iz)for(int ix=ax;ix<=bx;++ix)
                    bins[size_t(iz)*nx+ix].push_back(&entrance);
                ++stats.cutEntrances;
            }
            if(stats.rejectedEntrances)stats.cutDiagnostic="Some shafts are outside the terrain, do not reach its heightfield, or may be sealed by Fill";
        }catch(const std::exception& e){
            // No partially cut output is published if planning fails.
            for(auto& bin:bins)bin.clear();
            stats.cutEntrances=0;stats.rejectedEntrances=snapshot.entrances.size();
            stats.cutDiagnostic=e.what();
        }
    }
    render::MeshBuilder staged;
    const auto build=[&](bool cut){
        for(int iz=0;iz<nz;++iz)for(int ix=0;ix<nx;++ix){
            const int x=ix*step,z=iz*step;
            const auto v=cell(x,z);
            const auto normal=render::normalize(render::cross(v[2]-v[0],v[1]-v[0]));
            const size_t offset=size_t(z/scale)*size_t(d.w)+size_t(x/scale);
            const uint32_t color=terrainPreviewColor(offset<d.cells.size()?d.cells[offset]:'.');
            const auto& candidates=bins[size_t(iz)*nx+ix];
            if(!cut||candidates.empty()){
                const auto a=staged.addVertex(v[0],normal,color),b=staged.addVertex(v[1],normal,color);
                const auto c=staged.addVertex(v[2],normal,color),e=staged.addVertex(v[3],normal,color);
                staged.addTriangle(a,c,b);staged.addTriangle(a,e,c);
            }else{
                std::vector<cave::CutTriangleCm> fragments;
                cave::appendCutTerrainTriangle(fragments,{cm(v[0]),cm(v[2]),cm(v[1])},candidates,sides,maxTriangles);
                cave::appendCutTerrainTriangle(fragments,{cm(v[0]),cm(v[3]),cm(v[2])},candidates,sides,maxTriangles);
                if(fragments.size()>maxTriangles-staged.indices().size()/3)
                    throw std::length_error("Caverns terrain cut triangle budget");
                const auto metres=[](cave::CutVertexCm p){return float3{float(p.x*.01),float(p.y*.01),float(p.z*.01)};};
                for(const auto& t:fragments){
                    const auto a=metres(t.a),b=metres(t.b),c=metres(t.c);
                    const auto cross=render::cross(b-a,c-a);
                    if(render::dot(cross,cross)<=1e-16f)continue;
                    const auto n=render::normalize(cross);
                    const auto i=staged.addVertex(a,n,color),j=staged.addVertex(b,n,color),k=staged.addVertex(c,n,color);
                    staged.addTriangle(i,j,k);
                }
            }
            if(staged.indices().size()/3>maxTriangles)throw std::length_error("Caverns terrain preview triangle budget");
        }
    };
    try{build(stats.cutEntrances!=0);}
    catch(const std::length_error& e){
        staged.clear();stats.cutDiagnostic=e.what();stats.rejectedEntrances+=stats.cutEntrances;stats.cutEntrances=0;
        build(false); // whole original terrain, with explicit diagnostic
    }
    const auto base=uint32_t(out.vertexCount());
    for(const auto& v:staged.vertices())out.addVertex(v.pos,v.nrm,v.rgba);
    const auto& indices=staged.indices();
    for(size_t i=0;i<indices.size();i+=3)out.addTriangle(base+indices[i],base+indices[i+1],base+indices[i+2]);
    stats.quads=size_t(nx)*nz;stats.triangles=indices.size()/3;
    return stats;
}
} // namespace aiverse::studio
