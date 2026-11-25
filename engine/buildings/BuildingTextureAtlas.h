#pragma once
// Deterministic unique triangle charts for a building's generated PBR maps.
// CPU-only, no changes to source geometry/collision and no external assets.
#include <engine/systems/BuildingStyle.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace aiverse::lib::systems {
struct BuildingAtlas {
    StyledMesh mesh;
    std::array<BuildingSurfacePixels,4> maps;
    int size=0;
    size_t charts=0;
};
namespace buildingAtlasDetail {
inline constexpr int gutter=4;
struct Chart{
    size_t part=0,material=0;
    std::array<StyledVertex,3> vertices{};
    float u=0,v=0,du=0,dv=0;
    int w=0,h=0,x=0,y=0;
};
inline bool pack(std::vector<Chart>& charts,int size){
    std::vector<size_t> order(charts.size());
    std::iota(order.begin(),order.end(),size_t(0));
    std::stable_sort(order.begin(),order.end(),[&](size_t a,size_t b){
        return charts[a].h!=charts[b].h?charts[a].h>charts[b].h:charts[a].w>charts[b].w;
    });
    int x=0,y=0,row=0;
    for(size_t i:order){
        auto& c=charts[i];
        const int w=c.w+2*gutter,h=c.h+2*gutter;
        if(w>size||h>size)return false;
        if(x+w>size){x=0;y+=row;row=0;}
        if(y+h>size)return false;
        c.x=x+gutter;c.y=y+gutter;x+=w;row=std::max(row,h);
    }
    return true;
}
inline std::array<uint8_t,4> sample(const BuildingSurfacePixels& src,float u,float v){
    // Match repeat addressing and texel-centre convention.
    const int n=src.width;
    const double x=(double(u)-std::floor(double(u)))*n-.5;
    const double y=(double(v)-std::floor(double(v)))*n-.5;
    const int ix=int(std::floor(x)),iy=int(std::floor(y));
    const double fx=x-ix,fy=y-iy;
    auto at=[&](int a,int b,int k){
        return double(src.rgba[(size_t((b%n+n)%n)*n+size_t((a%n+n)%n))*4+size_t(k)]);
    };
    std::array<uint8_t,4> out{};
    for(int k=0;k<4;++k){
        const double a=at(ix,iy,k)*(1-fx)+at(ix+1,iy,k)*fx;
        const double b=at(ix,iy+1,k)*(1-fx)+at(ix+1,iy+1,k)*fx;
        out[size_t(k)]=uint8_t(std::clamp(int(std::lround(a*(1-fy)+b*fy)),0,255));
    }
    return out;
}
} // namespace buildingAtlasDetail

inline BuildingAtlas buildUniqueBuildingAtlas(const StyledMesh& source){
    using namespace buildingAtlasDetail;
    if(source.triangles()==0||source.triangles()>50000)
        throw std::runtime_error("Unique atlas requires 1..50000 triangles");
    std::vector<Chart> charts;
    charts.reserve(source.triangles());
    for(size_t p=0;p<source.parts.size();++p){
        const auto& part=source.parts[p];
        if(part.material<0||size_t(part.material)>=source.materials.size()||part.indices.size()%3)
            throw std::runtime_error("Invalid atlas material or triangle");
        const auto& m=source.materials[size_t(part.material)];
        if(m.surfacePattern.empty()||!validBuildingSurfacePattern(m.surfacePattern)||
           !validBuildingSurfaceResolution(m.surfaceResolution)||m.uvRotation!=0.f||
           m.uvScale[0]<=0.f||m.uvScale[1]<=0.f||
           !m.textures.baseColor.empty()||!m.textures.metallicRoughness.empty()||
           !m.textures.normal.empty()||!m.textures.occlusion.empty()||!m.textures.emissive.empty())
            throw std::runtime_error("Unique atlas supports procedural surfaces with positive UV scale, no UV rotation or imported textures");
        for(size_t i=0;i<part.indices.size();i+=3){
            Chart c;c.part=p;c.material=size_t(part.material);
            float maxU=0,maxV=0;
            for(size_t k=0;k<3;++k){
                const auto idx=part.indices[i+k];
                if(idx>=source.vertices.size())throw std::runtime_error("Invalid atlas vertex");
                auto& v=c.vertices[k];v=source.vertices[idx];
                for(int a=0;a<2;++a){
                    v.uv[a]=v.uv[a]*m.uvScale[a]+m.uvOffset[a];
                    if(!std::isfinite(v.uv[a])||std::abs(v.uv[a])>100000.f)
                        throw std::runtime_error("Atlas UV outside supported range");
                }
                if(k==0){c.u=maxU=v.uv[0];c.v=maxV=v.uv[1];}
                else{c.u=std::min(c.u,v.uv[0]);c.v=std::min(c.v,v.uv[1]);
                    maxU=std::max(maxU,v.uv[0]);maxV=std::max(maxV,v.uv[1]);}
            }
            c.du=maxU-c.u;c.dv=maxV-c.v;
            charts.push_back(c);
        }
    }
    // Density degrades explicitly to fit a single 2048 square page.
    // A chart's UV aspect ratio is retained as closely as integer pixels allow.
    int selected=0;
    for(int density:{32,16,8,4}){
        for(auto& c:charts){
            const float scale=std::min(float(density),63.f/std::max({c.du,c.dv,1e-6f}));
            c.w=std::max(2,int(std::ceil(c.du*scale))+1);
            c.h=std::max(2,int(std::ceil(c.dv*scale))+1);
        }
        for(int size:{256,512,1024,2048})if(pack(charts,size)){selected=size;break;}
        if(selected)break;
    }
    if(!selected)throw std::runtime_error("Unique building atlas exceeds one 2048 page; use tiled textures");
    BuildingAtlas result;result.size=selected;result.charts=charts.size();
    auto& mesh=result.mesh;mesh.materials=source.materials;mesh.parts=source.parts;
    std::copy_n(source.boundsMin,3,mesh.boundsMin);std::copy_n(source.boundsMax,3,mesh.boundsMax);
    for(auto& p:mesh.parts)p.indices.clear();
    for(auto& m:mesh.materials){m.uvScale={1,1};m.uvOffset={0,0};m.uvRotation=0;}
    for(const auto& c:charts){
        auto& indices=mesh.parts[c.part].indices;
        for(auto v:c.vertices){
            const float u=c.du>1e-7f?(v.uv[0]-c.u)/c.du:.5f;
            const float t=c.dv>1e-7f?(v.uv[1]-c.v)/c.dv:.5f;
            v.uv[0]=(c.x+.5f+u*(c.w-1))/selected;
            v.uv[1]=(c.y+.5f+t*(c.h-1))/selected;
            indices.push_back(uint32_t(mesh.vertices.size()));mesh.vertices.push_back(v);
        }
    }
    // One material source at a time, four common atlas images.
    for(size_t channel=0;channel<kBuildingSurfaceChannels.size();++channel){
        auto& target=result.maps[channel];target.width=target.height=selected;
        target.rgba.resize(size_t(selected)*selected*4);
        const std::array<uint8_t,4> fill=channel==2?std::array<uint8_t,4>{128,128,255,255}:
            std::array<uint8_t,4>{255,255,255,255};
        for(size_t i=0;i<target.rgba.size();i+=4)std::copy(fill.begin(),fill.end(),target.rgba.begin()+i);
        std::vector<std::vector<size_t>> byMaterial(source.materials.size());
        for(size_t i=0;i<charts.size();++i)byMaterial[charts[i].material].push_back(i);
        for(size_t m=0;m<byMaterial.size();++m){
            if(byMaterial[m].empty())continue;
            const auto& material=source.materials[m];
            const auto src=generateBuildingSurface(material.surfacePattern,material.id,material.surfaceSeed,
                kBuildingSurfaceChannels[channel],material.surfaceResolution);
            if(src.rgba.empty())throw std::runtime_error("Cannot bake atlas source");
            for(size_t i:byMaterial[m]){
                const auto& c=charts[i];
                for(int y=-gutter;y<c.h+gutter;++y)for(int x=-gutter;x<c.w+gutter;++x){
                    const float u=c.u+c.du*float(std::clamp(x,0,c.w-1))/float(c.w-1);
                    const float v=c.v+c.dv*float(std::clamp(y,0,c.h-1))/float(c.h-1);
                    const auto pixel=sample(src,u,v);
                    const size_t off=(size_t(c.y+y)*selected+size_t(c.x+x))*4;
                    std::copy(pixel.begin(),pixel.end(),target.rgba.begin()+off);
                }
            }
        }
    }
    return result;
}
} // namespace aiverse::lib::systems
