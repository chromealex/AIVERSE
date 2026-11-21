// TerraWeave Horizon presentation: a SINGLE standard AIVERSE Model with
// one coarse terrain primitive and one forest-cluster primitive. The latter
// shares ONE packed RGBA8 atlas for thousands of crowns, not per-tree draws.
// Rendering, shadows and buffer upload remain owned by Renderer3D/gfx::Device.
#pragma once
#include "engine/world/TerraWeaveHorizon.h"
#include "engine/render/TerraWeaveHorizonClip.h"
#include "engine/render/TerraWeaveVertexWind.h"
#include "engine/render/Model.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <map>
#include <tuple>
#include <utility>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::terraweave {

struct HorizonModelBuildStats {
    size_t clippedQuads=0;
    size_t clippedTriangles=0;
    bool clipBudgetFallback=false;
    size_t weldedVertices=0;
    int32_t maxWeldHeightDeltaCm=0;
    size_t smoothedNearNormals=0;
    size_t matchedAuthoredNearNormals=0; // exact Q16 source normals at seam
    bool nearWeldSlopeFallback=false;
};

inline std::unique_ptr<render::Model> makeHorizonModel(
        const HorizonBake& b,HorizonModelBuildStats* stats=nullptr) {
    if (b.atlasWidth == 0 || b.atlasWidth != b.atlasHeight ||
        b.atlasRgba.size() != size_t(b.atlasWidth)*b.atlasHeight*4)
        throw std::invalid_argument("TerraWeave Horizon invalid RGBA atlas");
    auto model=std::make_unique<render::Model>();
    model->path="terraweave/horizon";
    model->batching.autoLod=false;
    model->batching.allowGeometryPool=false; // required for safe mutable terrain vertex ranges

    render::Material ground;
    ground.name="TerraWeave Horizon Terrain";
    ground.shading=render::ShadingModel::Pbr;
    ground.baseColor={1,1,1,1};
    ground.roughness=1.0f;
    ground.castShadow=false;
    model->materials.push_back(ground);

    render::Material forest;
    forest.name="TerraWeave Horizon Forest Cluster";
    forest.shading=render::ShadingModel::Unlit;
    forest.baseColor={1,1,1,1};
    forest.baseTex=0;
    forest.alpha=render::AlphaMode::Mask;
    forest.alphaCutoff=0.5f;
    // Forest-card vertex alpha is a root-to-canopy shader wind weight,
    // not an atlas transparency factor. The cutout atlas owns opacity.
    forest.vertexColors=false;
    forest.doubleSided=true;
    forest.castShadow=false;
    forest.receiveShadow=false;
    forest.roughness=1.0f;
    model->materials.push_back(forest);

    render::Image atlas;
    atlas.name="terraweave/horizon_clusters_rgba8";
    atlas.width=static_cast<int>(b.atlasWidth);
    atlas.height=static_cast<int>(b.atlasHeight);
    atlas.repeat=false;
    atlas.nearest=false;
    // Mutable atlas patches rely on backend automatic mip regeneration.
    atlas.preserveAlphaCoverage=false;
    atlas.rgba=b.atlasRgba;
    model->images.push_back(std::move(atlas));

    render::MeshDef mesh;
    mesh.name="TerraWeave Horizon Combined";
    if (!b.terrainVertices.empty()) {
        const uint32_t n=b.terrainResolution;
        if (b.terrainVertices.size()!=size_t(n+1)*(n+1))
            throw std::invalid_argument("TerraWeave Horizon terrain samples");
        const uint32_t start=static_cast<uint32_t>(model->vertices.size());
        const bool authoredNear=!b.nearOwnedQuads.empty();
        if(authoredNear && b.nearOwnedQuads.size()!=size_t(n)*n)
            throw std::invalid_argument("TerraWeave Horizon near terrain ownership grid");
        // Near-geometry ownership replaces the legacy circular cutout when
        // the host supplies exact resident authored terrain coverage. This
        // prevents a sky hole where nearby terrain has not loaded yet.
        std::vector<uint8_t> farQuad(size_t(n)*n,1u);
        const auto clipped=planHorizonNearGroundClipping(b);
        if(stats) {
            stats->nearWeldSlopeFallback=b.nearWeldSlopeFallback;
            stats->clipBudgetFallback=!clipped &&
                b.maxNearClipTriangles && !b.nearClipRects.empty();
            stats->clippedTriangles=clipped ? clipped->triangles.size():0;
            stats->clippedQuads=clipped ? static_cast<size_t>(std::count(
                clipped->touchedQuads.begin(),clipped->touchedQuads.end(),
                uint8_t(1))) : 0;
        }
        // A rejected plan (too fragmented) preserves the preceding
        // conservative coarse-quad implementation. No partial application.
        const auto fineClipping=clipped && !clipped->touchedQuads.empty();
        for (uint32_t z=0;z<=n;++z) {
            for (uint32_t x=0;x<=n;++x) {
                const auto& v=b.terrainVertices[size_t(z)*(n+1)+x];
                // Normals are authored from the same globally anchored
                // height stencil in every region, including outer borders.
                // Never use a one-sided normal at the local patch boundary.
                const auto normal=render::normalize(render::float3{
                    float(v.slopeXPermille)*0.001f,1.0f,
                    float(v.slopeZPermille)*0.001f
                });
                render::ModelVertex out;
                out.pos={
                    float(v.xCm-b.originXcm)*0.01f,
                    float(v.heightCm)*0.01f,
                    float(v.zCm-b.originZcm)*0.01f
                };
                out.nrm=normal;
                out.uv={
                    float(v.xCm-b.originXcm)*0.00002f,
                    float(v.zCm-b.originZcm)*0.00002f
                };
                out.color=v.colorRgba;
                model->vertices.push_back(out);
            }
        }
        const uint32_t begin=static_cast<uint32_t>(model->indices.size());
        for (uint32_t z=0;z<n;++z)
            for (uint32_t x=0;x<n;++x) {
                const size_t idx=size_t(z)*(n+1)+x;
                const auto& a=b.terrainVertices[idx];
                const auto& d=b.terrainVertices[idx+(n+1)+1];
                const int64_t cx=(a.xCm+d.xCm)/2-b.cameraXcm;
                const int64_t cz=(a.zCm+d.zCm)/2-b.cameraZcm;
                const size_t quad=size_t(z)*n+x;
                if(authoredNear ? b.nearOwnedQuads[quad]!=0u :
                    cx*cx+cz*cz<int64_t(b.terrainBeginCm)*
                                      b.terrainBeginCm) {
                    farQuad[quad]=0u; // near ground owns the full quad
                    continue;
                }
                if(fineClipping && clipped->touchedQuads[quad])
                    continue; // replaced by bounded clipped triangles below
                const uint32_t i=start+static_cast<uint32_t>(idx);
                const uint32_t stride=n+1;
                model->indices.insert(model->indices.end(),{
                    i,i+stride,i+1,
                    i+1,i+stride,i+stride+1
                });
            }
        if(fineClipping){
            const size_t firstClippedVertex=model->vertices.size();
            std::vector<std::array<HorizonClipPoint,3>> emittedCoords;
            emittedCoords.reserve(clipped->triangles.size());
            // Lookup baked, versioned near-boundary heights without querying
            // live WorldScene or physics in model build/render stages.
            using SeamKey=std::pair<int64_t,int64_t>;
            std::map<SeamKey,int32_t> nearEdgeHeights;
            std::map<SeamKey,render::float3> authoredNearNormals;
            for(const auto& entry:b.nearSeamSamples) {
                const SeamKey key{entry.xQ16cm,entry.zQ16cm};
                nearEdgeHeights.emplace(key,entry.nearHeightCm);
                if(entry.nearNormalValid)
                    authoredNearNormals.emplace(key,render::normalize(
                        render::float3{
                            float(entry.nearSlopeXPermille)*0.001f,1.0f,
                            float(entry.nearSlopeZPermille)*0.001f}));
            }
            // Only polygons clipped from the two ORIGINAL source triangles
            // are emitted. Barycentric interpolation stays on each triangle's
            // surface plane, so clipping does not invent a new height seam.
            for(const auto& triangle:clipped->triangles){
                const uint32_t x=triangle.quadIndex%n;
                const uint32_t z=triangle.quadIndex/n;
                const uint32_t i=start+z*(n+1)+x,step=n+1;
                const std::array<uint32_t,3> vertexIds=triangle.sourceHalf==0 ?
                    std::array<uint32_t,3>{i,i+step,i+1} :
                    std::array<uint32_t,3>{i+1,i+step,i+step+1};
                // Copy source vertices before push_back can reallocate.
                const std::array<render::ModelVertex,3> baseVertices{
                    model->vertices[vertexIds[0]],
                    model->vertices[vertexIds[1]],
                    model->vertices[vertexIds[2]]
                };
                const std::array<HorizonClipPoint,3> srcCoords{{
                    {double(b.terrainVertices[vertexIds[0]-start].xCm),
                     double(b.terrainVertices[vertexIds[0]-start].zCm)},
                    {double(b.terrainVertices[vertexIds[1]-start].xCm),
                     double(b.terrainVertices[vertexIds[1]-start].zCm)},
                    {double(b.terrainVertices[vertexIds[2]-start].xCm),
                     double(b.terrainVertices[vertexIds[2]-start].zCm)}
                }};
                const double ax=srcCoords[1].x-srcCoords[0].x;
                const double az=srcCoords[1].z-srcCoords[0].z;
                const double bx=srcCoords[2].x-srcCoords[0].x;
                const double bz=srcCoords[2].z-srcCoords[0].z;
                const double area=ax*bz-az*bx;
                if(std::abs(area)<1e-8)continue;
                const uint32_t first=
                    static_cast<uint32_t>(model->vertices.size());
                for(const HorizonClipPoint& point:triangle.corners){
                    const double px=point.x-srcCoords[0].x;
                    const double pz=point.z-srcCoords[0].z;
                    const double w1=(px*bz-pz*bx)/area;
                    const double w2=(ax*pz-az*px)/area;
                    const std::array<double,3> w{1.0-w1-w2,w1,w2};
                    const auto interpolate=[&](auto field){
                        return float(w[0]*field(baseVertices[0])+
                                     w[1]*field(baseVertices[1])+
                                     w[2]*field(baseVertices[2]));
                    };
                    render::ModelVertex v;
                    v.pos={
                        float((point.x-double(b.originXcm))*0.01),
                        interpolate([](const auto& s){return s.pos.y;}),
                        float((point.z-double(b.originZcm))*0.01)
                    };
                    v.nrm=render::normalize(render::float3{
                        interpolate([](const auto& s){return s.nrm.x;}),
                        interpolate([](const auto& s){return s.nrm.y;}),
                        interpolate([](const auto& s){return s.nrm.z;})
                    });
                    v.uv={
                        interpolate([](const auto& s){return s.uv.x;}),
                        interpolate([](const auto& s){return s.uv.y;})
                    };
                    uint32_t color=0;
                    for(uint32_t shift=0;shift<=24;shift+=8){
                        const auto ch=[shift](const render::ModelVertex& s){
                            return double((s.color>>shift)&255u);
                        };
                        const double value=w[0]*ch(baseVertices[0])+
                                           w[1]*ch(baseVertices[1])+
                                           w[2]*ch(baseVertices[2]);
                        const auto rounded=static_cast<uint32_t>(
                            std::clamp(std::round(value),0.0,255.0));
                        color|=rounded<<shift;
                    }
                    v.color=color;
                    const SeamKey key{
                        static_cast<int64_t>(std::llround(point.x*65536.0)),
                        static_cast<int64_t>(std::llround(point.z*65536.0))};
                    if(const auto hit=nearEdgeHeights.find(key);
                       hit!=nearEdgeHeights.end()) {
                        // The source far-triangle plane remains unchanged
                        // away from its clipped near edge. Only exact edge
                        // vertices are welded to resident authored height.
                        const int32_t farCm=static_cast<int32_t>(
                            std::llround(double(v.pos.y)*100.0));
                        const int64_t delta=int64_t(hit->second)-farCm;
                        v.pos.y=float(hit->second)*0.01f;
                        if(stats){
                            ++stats->weldedVertices;
                            stats->maxWeldHeightDeltaCm=
                                static_cast<int32_t>(std::max<int64_t>(
                                    stats->maxWeldHeightDeltaCm,
                                    std::abs(delta)));
                        }
                    }
                    model->vertices.push_back(v);
                }
                model->indices.insert(model->indices.end(),{
                    first,first+1,first+2
                });
                emittedCoords.push_back(triangle.corners);
            }
            if(!nearEdgeHeights.empty() &&
               b.nearSeamNormalBlendPercent>0 &&
               model->vertices.size()>=firstClippedVertex+3){
                // Area-weighted face normals are gathered by the *world*
                // corner key. This keeps adjacent clipped triangles sharing
                // the same normalized lighting response at the same XYZ,
                // instead of picking a different face normal per triangle.
                using Key=std::tuple<int64_t,int64_t,int64_t>;
                struct NormalAccum {
                    render::float3 sum{0,0,0};
                };
                std::map<Key,NormalAccum> accumulated;
                const auto keyFor=[&](size_t t,size_t j){
                    const auto& point=emittedCoords[t][j];
                    const auto& v=model->vertices[
                        firstClippedVertex+t*3+j];
                    return Key{
                        static_cast<int64_t>(std::llround(point.x*65536.0)),
                        static_cast<int64_t>(std::llround(point.z*65536.0)),
                        static_cast<int64_t>(
                            std::llround(double(v.pos.y)*1000.0))
                    };
                };
                for(size_t t=0;t<emittedCoords.size();++t){
                    const size_t i=firstClippedVertex+t*3;
                    const auto& a=model->vertices[i].pos;
                    const auto& b=model->vertices[i+1].pos;
                    const auto& d=model->vertices[i+2].pos;
                    const float ux=b.x-a.x,uy=b.y-a.y,uz=b.z-a.z;
                    const float vx=d.x-a.x,vy=d.y-a.y,vz=d.z-a.z;
                    render::float3 face{
                        uy*vz-uz*vy,uz*vx-ux*vz,ux*vy-uy*vx
                    };
                    // The horizontal coarse terrain is upward facing.
                    // Do not let a degenerate or inverted face corrupt
                    // normals on the boundary of the immutable near mesh.
                    if(face.y<=0.000001f)continue;
                    for(size_t j=0;j<3;++j){
                        auto& sum=accumulated[keyFor(t,j)].sum;
                        sum.x+=face.x;sum.y+=face.y;sum.z+=face.z;
                    }
                }
                const float strength=
                    float(b.nearSeamNormalBlendPercent)*0.01f;
                for(size_t t=0;t<emittedCoords.size();++t)
                    for(size_t j=0;j<3;++j){
                        const auto key=keyFor(t,j);
                        const auto it=accumulated.find(key);
                        if(it==accumulated.end())continue;
                        auto& v=model->vertices[
                            firstClippedVertex+t*3+j];
                        const auto face=render::normalize(it->second.sum);
                        const auto [x,z,y]=key;
                        (void)y;
                        // On the X/Z seam, prefer an authored source normal
                        // computed from the SAME immutable near triangle
                        // field as the welded Y. Interior points still use
                        // area-weighted geometric far-triangle normals.
                        // Both preserve the previous no-extra-geometry path.
                        const SeamKey seamKey{x,z};
                        const bool seam=nearEdgeHeights.contains(seamKey);
                        const auto source=authoredNearNormals.find(seamKey);
                        const bool nearNormal=seam &&
                            source!=authoredNearNormals.end();
                        const auto target=nearNormal ? source->second : face;
                        const float mix=seam ? strength : strength*0.25f;
                        v.nrm=render::normalize(render::float3{
                            v.nrm.x*(1.0f-mix)+target.x*mix,
                            v.nrm.y*(1.0f-mix)+target.y*mix,
                            v.nrm.z*(1.0f-mix)+target.z*mix
                        });
                        if(stats) {
                            ++stats->smoothedNearNormals;
                            if(nearNormal)++stats->matchedAuthoredNearNormals;
                        }
                    }
            }
        }
        // An optional lightweight ring extends the outer silhouette
        // DOWNWARD without altering the true sampled top-edge heights.
        // It hides sub-mesh cracks between a coarse far field and neighboring
        // landscape while preserving deterministic world-position edges.
        if(b.terrainSkirtDepthCm>0 && model->indices.size()>begin){
            const uint32_t lowerFirst=
                static_cast<uint32_t>(model->vertices.size());
            const auto rimIndex=[n](uint32_t side,uint32_t i)->uint32_t{
                switch(side){
                    case 0:return i;                       // north, eastward
                    case 1:return i*(n+1)+n;              // east, southward
                    case 2:return n*(n+1)+(n-i);          // south, westward
                    default:return (n-i)*(n+1);           // west, northward
                }
            };
            const auto dimColor=[](uint32_t color)->uint32_t{
                const uint32_t red=(color&255u)*3u/4u;
                const uint32_t green=((color>>8)&255u)*3u/4u;
                const uint32_t blue=((color>>16)&255u)*3u/4u;
                return (color&0xff000000u)|(blue<<16)|(green<<8)|red;
            };
            for(uint32_t side=0;side<4;++side){
                for(uint32_t i=0;i<=n;++i){
                    auto bottom=model->vertices[start+rimIndex(side,i)];
                    bottom.pos.y-=float(b.terrainSkirtDepthCm)*0.01f;
                    bottom.nrm=render::normalize(
                        render::float3{side==1?1.0f:side==3?-1.0f:0.0f,
                                       0.25f,
                                       side==2?1.0f:side==0?-1.0f:0.0f});
                    bottom.color=dimColor(bottom.color);
                    model->vertices.push_back(bottom);
                }
                for(uint32_t i=0;i<n;++i) {
                    const uint32_t topA=start+rimIndex(side,i);
                    const uint32_t topB=start+rimIndex(side,i+1);
                    const uint32_t lowA=lowerFirst+side*(n+1)+i;
                    const uint32_t lowB=lowA+1;
                    model->indices.insert(model->indices.end(),{
                        topA,topB,lowA,lowA,topB,lowB
                    });
                }
            }
        }
        // Bounded INNER seam lip along the actual near/far ownership
        // interface. A missing near tile leaves the far quad rendered,
        // while a fully-owned near tile omits distant geometry. Two small
        // downward vertices seal each interface edge without altering
        // authoritative terrain or adding a renderer/material.
        if(authoredNear && b.innerTerrainLipCm>0){
            const auto visible=[&](int32_t x,int32_t z)->bool {
                return x>=0 && z>=0 && x<int32_t(n) && z<int32_t(n) &&
                    farQuad[size_t(z)*n+size_t(x)]!=0;
            };
            const auto owned=[&](int32_t x,int32_t z)->bool {
                return x>=0 && z>=0 && x<int32_t(n) && z<int32_t(n) &&
                    farQuad[size_t(z)*n+size_t(x)]==0;
            };
            size_t segmentCount=0;
            for(int32_t z=0;z<int32_t(n);++z)
                for(int32_t x=0;x<int32_t(n);++x)
                    if(owned(x,z)){
                        segmentCount+=visible(x,z-1)?1u:0u;
                        segmentCount+=visible(x+1,z)?1u:0u;
                        segmentCount+=visible(x,z+1)?1u:0u;
                        segmentCount+=visible(x-1,z)?1u:0u;
                    }
            // Sparse checkerboard maps must not generate unbounded lip
            // geometry. Fall back to the exact near ownership mask
            // without an inner lip rather than exceeding the cap.
            constexpr size_t kMaxInnerLipSegments=512;
            if(segmentCount<=kMaxInnerLipSegments){
                const auto edge=[&](uint32_t ax,uint32_t az,
                                    uint32_t bx,uint32_t bz,
                                    render::float3 normal) {
                    const uint32_t topA=start+az*(n+1)+ax;
                    const uint32_t topB=start+bz*(n+1)+bx;
                    const uint32_t lowA=
                        static_cast<uint32_t>(model->vertices.size());
                    auto vA=model->vertices[topA];
                    auto vB=model->vertices[topB];
                    vA.pos.y-=float(b.innerTerrainLipCm)*0.01f;
                    vB.pos.y-=float(b.innerTerrainLipCm)*0.01f;
                    vA.nrm=normal;vB.nrm=normal;
                    model->vertices.push_back(vA);
                    model->vertices.push_back(vB);
                    model->indices.insert(model->indices.end(),{
                        topA,topB,lowA,lowA,topB,lowA+1
                    });
                };
                for(uint32_t z=0;z<n;++z)
                    for(uint32_t x=0;x<n;++x) {
                        if(!owned(int32_t(x),int32_t(z)))continue;
                        if(visible(int32_t(x),int32_t(z)-1))
                            edge(x,z,x+1,z,{0,0,-1});
                        if(visible(int32_t(x)+1,int32_t(z)))
                            edge(x+1,z,x+1,z+1,{1,0,0});
                        if(visible(int32_t(x),int32_t(z)+1))
                            edge(x+1,z+1,x,z+1,{0,0,1});
                        if(visible(int32_t(x)-1,int32_t(z)))
                            edge(x,z+1,x,z,{-1,0,0});
                    }
            }
        }
        if (model->indices.size()>begin) {
            render::Primitive primitive;
            primitive.material=0;
            primitive.firstVertex=start;
            primitive.vertexCount=static_cast<uint32_t>(
                model->vertices.size()-start);
            primitive.firstIndex=begin;
            primitive.indexCount=static_cast<uint32_t>(model->indices.size())-begin;
            mesh.primitives.push_back(primitive);
        }
    }

    const uint32_t forestVertexStart=static_cast<uint32_t>(model->vertices.size());
    const uint32_t forestIndexStart=static_cast<uint32_t>(model->indices.size());
    for (const HorizonCluster& cluster:b.clusters) {
        const float x=float(cluster.centerXcm-b.originXcm)*0.01f;
        const float z=float(cluster.centerZcm-b.originZcm)*0.01f;
        const float base=float(cluster.bottomCm)*0.01f;
        const float top=float(cluster.topCm)*0.01f;
        const float half=float(cluster.tileSizeCm)*0.005f;
        for(int orientation=0;orientation<2;++orientation) {
            // One atlas tile per cluster in economy mode. Higher quality
            // may bake an independent 90-degree silhouette, but the same
            // two crossed cards and single material/texture are retained.
            const uint32_t atlasX=orientation==0 ?
                cluster.atlasX : cluster.atlasSideX;
            const uint32_t atlasY=orientation==0 ?
                cluster.atlasY : cluster.atlasSideY;
            const float u0=(float(atlasX)+0.5f)/float(b.atlasWidth);
            const float u1=(float(atlasX+cluster.atlasSize)-0.5f)/float(b.atlasWidth);
            const float v0=(float(atlasY)+0.5f)/float(b.atlasHeight);
            const float v1=(float(atlasY+cluster.atlasSize)-0.5f)/float(b.atlasHeight);
            const float dx=orientation==0?half:0.0f;
            const float dz=orientation==0?0.0f:half;
            const uint32_t first=static_cast<uint32_t>(model->vertices.size());
            const float nx=orientation==0?0:1, nz=orientation==0?1:0;
            const render::float3 pos[4] = {
                {x-dx,base,z-dz}, {x+dx,base,z+dz},
                {x+dx,top,z+dz}, {x-dx,top,z-dz}
            };
            const render::float2 uv[4] = {
                {u0,v1},{u1,v1},{u1,v0},{u0,v0}
            };
            for (int i=0;i<4;++i) {
                render::ModelVertex v;
                v.pos=pos[i];v.nrm={nx,0,nz};v.uv=uv[i];
                // RGB: smooth 2.4km world-anchored direction, strength
                // and phase, shared by both crossing cards of this cluster.
                // A: 0 at the base, 255 at the canopy top (Stage 31).
                // Materials ignore vertexColors in fragment shading.
                v.color=horizonSpatialWindVertexColor(
                    cluster.centerXcm,cluster.centerZcm,
                    0x544552524157494eULL,i>=2);
                model->vertices.push_back(v);
            }
            model->indices.insert(model->indices.end(),{
                first,first+1,first+2,first,first+2,first+3
            });
        }
    }
    if (model->indices.size()>forestIndexStart) {
        render::Primitive p;
        p.material=1;
        p.firstVertex=forestVertexStart;
        p.vertexCount=static_cast<uint32_t>(model->vertices.size())-forestVertexStart;
        p.firstIndex=forestIndexStart;
        p.indexCount=static_cast<uint32_t>(model->indices.size())-forestIndexStart;
        mesh.primitives.push_back(p);
    }
    if(mesh.primitives.empty())return {};
    model->meshes.push_back(std::move(mesh));
    render::Node root;
    root.name="TerraWeave Horizon Combined";
    root.mesh=0;
    model->nodes.push_back(std::move(root));
    model->finalize();
    return model;
}

} // namespace aiverse::terraweave
