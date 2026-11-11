#pragma once
#include "Math3D.h"
#include "StaticPointShadows.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace aiverse::render {
// Immutable point lights, binned conservatively in world space. No view-dependent selection.
struct StaticLightGrid {
    std::vector<float4> texels;
    float4 origin{}, size{};
    int maxCellLights=0;
    template<class Lights> void build(const Lights& lights,float cellSize,const std::vector<StaticPointShadow>* shadows=nullptr) {
        texels.clear();maxCellLights=0;origin={};size={};
        if(lights.empty())return;
        if(lights.size()>100000)throw std::runtime_error("too many static point lights");
        if(!std::isfinite(cellSize)||cellSize<.25f)throw std::runtime_error("invalid static light cell size");
        float3 lo{1e9f,1e9f,1e9f},hi{-1e9f,-1e9f,-1e9f};
        for(const auto& l:lights){
            if(l.spot()||!std::isfinite(l.radius)||l.radius<=0||!std::isfinite(l.intensity)||l.intensity<0)throw std::runtime_error("invalid static point light");
            for(float x:{l.pos.x,l.pos.y,l.pos.z,l.color.x,l.color.y,l.color.z})if(!std::isfinite(x))throw std::runtime_error("nonfinite static point light");
            if(l.color.x<0||l.color.y<0||l.color.z<0||l.color.x>1||l.color.y>1||l.color.z>1||l.intensity>1e6f)throw std::runtime_error("static light radiance out of range");
            lo={std::min(lo.x,l.pos.x-l.radius),std::min(lo.y,l.pos.y-l.radius),std::min(lo.z,l.pos.z-l.radius)};
            hi={std::max(hi.x,l.pos.x+l.radius),std::max(hi.y,l.pos.y+l.radius),std::max(hi.z,l.pos.z+l.radius)};
        }
        const auto extent=(hi-lo)*(1.0f/cellSize);
        if(extent.x>256||extent.y>256||extent.z>256)throw std::runtime_error("static light grid extent too large");
        int nx=int(std::ceil(extent.x))+1,ny=int(std::ceil(extent.y))+1,nz=int(std::ceil(extent.z))+1;
        const int cells=nx*ny*nz;if(cells>262144)throw std::runtime_error("static light grid has too many cells");
        std::vector<std::vector<size_t>> bins(size_t(cells),std::vector<size_t>{});
        for(size_t i=0;i<lights.size();++i){const auto& l=lights[i];auto a=(l.pos-float3{l.radius,l.radius,l.radius}-lo)*(1.0f/cellSize);auto b=(l.pos+float3{l.radius,l.radius,l.radius}-lo)*(1.0f/cellSize);
            for(int z=std::max(0,int(a.z));z<=std::min(nz-1,int(b.z));++z)
            for(int y=std::max(0,int(a.y));y<=std::min(ny-1,int(b.y));++y)
            for(int x=std::max(0,int(a.x));x<=std::min(nx-1,int(b.x));++x){
                float3 mn=lo+float3{float(x),float(y),float(z)}*cellSize,mx=mn+float3{cellSize,cellSize,cellSize};
                float3 q{std::clamp(l.pos.x,mn.x,mx.x),std::clamp(l.pos.y,mn.y,mx.y),std::clamp(l.pos.z,mn.z,mx.z)};
                if(dot(q-l.pos,q-l.pos)<=l.radius*l.radius)bins[size_t(x+nx*(y+ny*z))].push_back(i);
            }
        }
        std::vector<size_t> shadowOffsets(lights.size());
        size_t end=size_t(cells);for(const auto& bin:bins)end+=2*bin.size();
        if(shadows){
            if(shadows->size()!=lights.size())throw std::runtime_error("point shadow count mismatch");
            for(size_t i=0;i<shadows->size();++i){const auto& shadow=(*shadows)[i];
                if(!shadow.resolution)continue;
                if(shadow.resolution<16||shadow.resolution>256||shadow.distances.size()!=size_t(6*shadow.resolution*shadow.resolution)||!std::isfinite(shadow.bias)||shadow.bias<0)
                    throw std::runtime_error("invalid point shadow map");
                if(!shadow.normals.empty()&&shadow.normals.size()!=shadow.distances.size())throw std::runtime_error("point shadow normal count mismatch");
                for(auto normal:shadow.normals)for(float component:{normal.x,normal.y,normal.z})if(!std::isfinite(component))throw std::runtime_error("invalid shadow normal");
                for(float d:shadow.distances)if(!std::isfinite(d)||d<0)throw std::runtime_error("invalid point shadow distance");
                shadowOffsets[i]=end;end+=1+shadow.distances.size();
            }
        }
        if(end>4194304)throw std::runtime_error("static shadow atlas budget exceeded");
        texels.reserve(end);texels.resize(size_t(cells));
        auto decode=[](float c){return c<=.04045f?c/12.92f:std::pow((c+.055f)/1.055f,2.4f);};
        for(int c=0;c<cells;++c){auto& bin=bins[size_t(c)];maxCellLights=std::max(maxCellLights,int(bin.size()));
            if(bin.size()>512||texels.size()+2*bin.size()>4194304)throw std::runtime_error("static light grid budget exceeded: cell="+std::to_string(bin.size())+", texels="+std::to_string(texels.size()));
            texels[size_t(c)]={float(texels.size()),float(bin.size()),0,0};
            for(auto i:bin){const auto& l=lights[i];texels.push_back({l.pos.x,l.pos.y,l.pos.z,l.radius});texels.push_back({decode(l.color.x)*l.intensity,decode(l.color.y)*l.intensity,decode(l.color.z)*l.intensity,float(shadowOffsets[i])});}
        }
        if(shadows)for(size_t i=0;i<shadows->size();++i)if(shadowOffsets[i]){
            const auto& shadow=(*shadows)[i];texels.push_back({float(shadow.resolution),shadow.bias,0,0});
            for(size_t j=0;j<shadow.distances.size();++j){auto normal=shadow.normals.empty()?float3{}:shadow.normals[j];texels.push_back({shadow.distances[j],normal.x,normal.y,normal.z});}
        }
        constexpr int width=2048;texels.resize((texels.size()+width-1)/width*width);
        origin={lo.x,lo.y,lo.z,1*(1.0f/cellSize)};size={float(nx),float(ny),float(nz),float(width)};
    }
};
}
