// Immutable, CPU-authored terrain LOD. Physics keeps the original TerrainPatch.
// Every tier retains every full-resolution outer edge vertex (no skirts).
#pragma once
#include "engine/world/TerraWeaveTerrainPatch.h"
#include "engine/render/Model.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {
struct TerrainPatchLodLevel {
    uint32_t stride=0,triangles=0;
    float errorMetres=0,maxPixels=0;
};
// Call after smoothing source normals/UVs and before upload. Only the exact
// full-resolution patch triangulation is accepted; existing LODs are rejected.
// Invalid source input leaves all model values unchanged.
inline std::vector<TerrainPatchLodLevel> appendTerrainPatchLods(
        render::Model& model,const TerrainPatch& patch,float targetErrorPixels=1.5f) {
    const uint32_t cells=patch.resolution?patch.resolution-1:0,n=cells+1;
    if(cells<8||cells>128||(cells&(cells-1))||patch.cellSizeMetres<16||
       patch.cellSizeMetres>4096||patch.heightsCm.size()!=size_t(n)*n||
       !std::isfinite(targetErrorPixels)||targetErrorPixels<.25f||targetErrorPixels>4||
       model.vb.valid()||model.ib.valid()||model.meshes.size()!=1||
       model.meshes[0].primitives.size()!=1||!model.meshes[0].lods.empty()||model.skinned()||!model.clips.empty()||model.meshes[0].deformable)
        throw std::invalid_argument("Terra immutable terrain LOD source");
    const auto source=model.meshes[0].primitives[0];
    if(source.indexCount!=cells*cells*6||source.firstIndex>model.indices.size()||
       source.indexCount>model.indices.size()-source.firstIndex||model.vertices.empty()||model.vertices.size()>size_t(cells)*cells*6||
       source.morphTargets||source.lightmapUv||!source.jointPalette.empty())
        throw std::invalid_argument("Terra terrain LOD topology");
    const uint32_t invalid=std::numeric_limits<uint32_t>::max();
    std::vector<uint32_t> grid(size_t(n)*n,invalid),vertexGrid(model.vertices.size(),invalid);
    const double step=double(patch.cellSizeMetres)/cells;
    for(uint32_t index=0;index<model.vertices.size();++index) {
        const auto& v=model.vertices[index];
        if(!std::isfinite(v.pos.x)||!std::isfinite(v.pos.y)||!std::isfinite(v.pos.z)||
           !std::isfinite(v.nrm.x)||!std::isfinite(v.nrm.y)||!std::isfinite(v.nrm.z)||
           !std::isfinite(v.uv.x)||!std::isfinite(v.uv.y))
            throw std::invalid_argument("Terra terrain LOD finite position");
        if(v.pos.x<-.0001f||v.pos.z<-.0001f||v.pos.x>patch.cellSizeMetres+.0001f||v.pos.z>patch.cellSizeMetres+.0001f)
            throw std::invalid_argument("Terra terrain LOD position extent");
        const auto x=std::llround(v.pos.x/step),z=std::llround(v.pos.z/step);
        if(x<0||z<0||x>cells||z>cells||std::abs(v.pos.x-x*step)>.0001||std::abs(v.pos.z-z*step)>.0001)
            throw std::invalid_argument("Terra terrain LOD grid position");
        const auto key=uint32_t(z)*n+uint32_t(x);vertexGrid[index]=key;
        const double tolerance=1./65536+std::abs(double(v.pos.y))*std::ldexp(1.,-24)+.000001;
        if(std::abs(double(v.pos.y)-double(patch.heightsCm[key])/100)>tolerance)
            throw std::invalid_argument("Terra terrain LOD source height mismatch");
        if(grid[key]==invalid)grid[key]=index;
        else {
            const auto& old=model.vertices[grid[key]];
            if(std::abs(old.pos.y-v.pos.y)>.0001f||std::abs(old.nrm.x-v.nrm.x)>.00001f||
               std::abs(old.nrm.y-v.nrm.y)>.00001f||std::abs(old.nrm.z-v.nrm.z)>.00001f||
               std::abs(old.uv.x-v.uv.x)>.00001f||std::abs(old.uv.y-v.uv.y)>.00001f||old.color!=v.color)
                throw std::invalid_argument("Terra terrain LOD needs shared smooth normals and UVs");
        }
    }
    if(std::find(grid.begin(),grid.end(),invalid)!=grid.end())
        throw std::invalid_argument("Terra terrain LOD incomplete grid");
    std::vector<uint8_t> covered(size_t(cells)*cells,0);
    for(uint32_t i=source.firstIndex;i<source.firstIndex+source.indexCount;i+=3) {
        std::array<uint32_t,3> t;
        for(int j=0;j<3;++j){const auto k=model.indices[i+j];if(k>=vertexGrid.size())throw std::invalid_argument("Terra terrain LOD index");t[j]=vertexGrid[k];}
        const int ax=t[0]%n,az=t[0]/n,bx=t[1]%n,bz=t[1]/n,cx=t[2]%n,cz=t[2]/n;
        if((bz-az)*(cx-ax)-(bx-ax)*(cz-az)!=1)
            throw std::invalid_argument("Terra terrain LOD winding/cell");
        const uint32_t x=uint32_t(std::min({ax,bx,cx})),z=uint32_t(std::min({az,bz,cz}));
        if(x>=cells||z>=cells)throw std::invalid_argument("Terra terrain LOD cell");
        std::sort(t.begin(),t.end());const uint32_t a=z*n+x,b=a+1,d=a+n,c=d+1;
        const uint8_t bit=t==std::array<uint32_t,3>{a,b,c}?1:t==std::array<uint32_t,3>{a,d,c}?2:0;
        auto& mask=covered[z*cells+x];if(!bit||(mask&bit))throw std::invalid_argument("Terra terrain LOD diagonal/duplicate");mask|=bit;
    }
    if(std::any_of(covered.begin(),covered.end(),[](uint8_t mask){return mask!=3;}))
        throw std::invalid_argument("Terra terrain LOD holes");
    const auto height=[&](uint32_t x,uint32_t z){return double(model.vertices[grid[z*n+x]].pos.y);};
    double low=height(0,0),high=low;for(auto index:grid){low=std::min(low,double(model.vertices[index].pos.y));high=std::max(high,double(model.vertices[index].pos.y));}
    const double diameter=std::sqrt(2.*patch.cellSizeMetres*patch.cellSizeMetres+(high-low)*(high-low));
    const auto fineHeight=[&](double x,double z) {
        const uint32_t ix=std::min(cells-1,uint32_t(std::max(0.,std::floor(x)))),iz=std::min(cells-1,uint32_t(std::max(0.,std::floor(z))));
        const double u=std::clamp(x-ix,0.,1.),v=std::clamp(z-iz,0.,1.),a=height(ix,iz),b=height(ix+1,iz),c=height(ix+1,iz+1),d=height(ix,iz+1);
        return v<=u?a+(b-a)*u+(c-b)*v:a+(c-d)*u+(d-a)*v;
    };
    std::vector<render::ModelVertex> extraVertices;
    std::vector<uint32_t> extraIndices;
    std::vector<render::MeshDef> extraMeshes;
    std::vector<TerrainPatchLodLevel> report;
    const auto originalVertices=uint32_t(model.vertices.size()),originalIndices=uint32_t(model.indices.size());
    float previous=std::numeric_limits<float>::infinity();
    for(uint32_t stride:{2u,4u,8u}) {
        std::vector<std::array<uint32_t,3>> triangles;
        for(uint32_t z=0;z<cells;z+=stride)for(uint32_t x=0;x<cells;x+=stride) {
            const uint32_t a=z*n+x,b=a+stride,d=a+stride*n,c=d+stride;
            if(x&&z&&x+stride<cells&&z+stride<cells){triangles.push_back({a,c,b});triangles.push_back({a,d,c});continue;}
            const std::array<uint32_t,4> corners{a,d,c,b};
            std::vector<uint32_t> rim;
            for(int edge=0;edge<4;++edge) {
                const uint32_t from=corners[edge],to=corners[(edge+1)%4];
                const bool border=(from%n==0&&to%n==0)||(from%n==cells&&to%n==cells)||
                    (from/n==0&&to/n==0)||(from/n==cells&&to/n==cells);
                const uint32_t count=border?stride:1;
                for(uint32_t j=0;j<count;++j)rim.push_back(uint32_t(int64_t(from)+(int64_t(to)-from)*j/count));
            }
            const uint32_t centre=(z+stride/2)*n+x+stride/2;
            for(size_t j=0;j<rim.size();++j)triangles.push_back({centre,rim[j],rim[(j+1)%rim.size()]});
        }
        double error=0;
        for(const auto& tri:triangles) {
            const double ax=tri[0]%n,az=tri[0]/n,bx=tri[1]%n,bz=tri[1]/n,cx=tri[2]%n,cz=tri[2]/n;
            const double ah=height(uint32_t(ax),uint32_t(az)),bh=height(uint32_t(bx),uint32_t(bz)),ch=height(uint32_t(cx),uint32_t(cz));
            const double determinant=(bz-cz)*(ax-cx)+(cx-bx)*(az-cz);
            for(int z=int(std::min({az,bz,cz}));z<=int(std::max({az,bz,cz}));++z)
                for(int x=int(std::min({ax,bx,cx}));x<=int(std::max({ax,bx,cx}));++x) {
                    const double u=((bz-cz)*(x-cx)+(cx-bx)*(z-cz))/determinant,v=((cz-az)*(x-cx)+(ax-cx)*(z-cz))/determinant;
                    if(u>=-1e-9&&v>=-1e-9&&u+v<=1+1e-9)error=std::max(error,std::abs(height(x,z)-(ah*u+bh*v+ch*(1-u-v))));
                }
            // Overlay vertices also include coarse edges crossing the fine
            // grid's X, Z and X-Z diagonals, not only original grid vertices.
            for(int edge=0;edge<3;++edge) {
                const auto from=tri[edge],to=tri[(edge+1)%3];const double x0=from%n,z0=from/n,x1=to%n,z1=to/n,h0=height(uint32_t(x0),uint32_t(z0)),h1=height(uint32_t(x1),uint32_t(z1));
                for(int axis=0;axis<3;++axis) {
                    const double p=axis==0?x0:axis==1?z0:x0-z0,q=axis==0?x1:axis==1?z1:x1-z1;
                    if(p==q)continue;
                    for(int k=int(std::floor(std::min(p,q)))+1;k<std::max(p,q);++k) {
                        const double t=(k-p)/(q-p),x=x0+(x1-x0)*t,z=z0+(z1-z0)*t;
                        error=std::max(error,std::abs(fineHeight(x,z)-(h0+(h1-h0)*t)));
                    }
                }
            }
        }
        const float pixels=std::min({512.f/float(stride/2),previous*.75f,float(targetErrorPixels*diameter/std::max(error,1e-6))});
        previous=pixels;
        render::MeshDef mesh;mesh.name="Terra Terrain LOD "+std::to_string(stride);
        auto primitive=source;primitive.firstIndex=originalIndices+uint32_t(extraIndices.size());primitive.firstVertex=originalVertices+uint32_t(extraVertices.size());
        std::vector<uint32_t> remap(grid.size(),invalid);
        for(const auto& tri:triangles)for(auto key:tri) {
            if(remap[key]==invalid){remap[key]=originalVertices+uint32_t(extraVertices.size());extraVertices.push_back(model.vertices[grid[key]]);}
            extraIndices.push_back(remap[key]);
        }
        primitive.indexCount=uint32_t(triangles.size()*3);primitive.vertexCount=originalVertices+uint32_t(extraVertices.size())-primitive.firstVertex;
        mesh.primitives.push_back(std::move(primitive));extraMeshes.push_back(std::move(mesh));
        report.push_back({stride,uint32_t(triangles.size()),float(error),pixels});
    }
    model.vertices.reserve(model.vertices.size()+extraVertices.size());model.indices.reserve(model.indices.size()+extraIndices.size());model.meshes.reserve(4);
    model.vertices.insert(model.vertices.end(),extraVertices.begin(),extraVertices.end());model.indices.insert(model.indices.end(),extraIndices.begin(),extraIndices.end());
    for(size_t i=0;i<extraMeshes.size();++i){model.meshes[0].lods.push_back({int(model.meshes.size()),report[i].maxPixels});model.meshes.push_back(std::move(extraMeshes[i]));}
    model.batching.independentShadowLod=true;model.batching.autoLod=false;model.markGeometryChanged();model.finalize();return report;
}
} // namespace aiverse::terraweave
