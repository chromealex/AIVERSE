// Authoring overlay for flat map region letters. Coordinates are map cell corners.
#pragma once
#include "apps/studio-native/StudioTerrainGeometry.h"
#include <array>

namespace aiverse::studio {
struct StudioRegionEdge { render::float3 a,b; char letter; };
struct StudioRegionBorders {
    std::vector<StudioRegionEdge> edges;
    using Rect=std::array<int,4>;
    Rect bounds{};
    float low=0,high=0;
    int stride=1;

    void measure(const editor::MapData& map,float level,bool terrain) {
        low=high=0;
        if(!terrain || std::max(map.w,map.h)<=256)return;
        for(int z=0;z<=map.h;++z)for(int x=0;x<=map.w;++x){
            const float y=terrainPreviewCorner(map,x,z,level);
            low=std::min(low,y);high=std::max(high,y);
        }
    }
    Rect visible(const editor::MapData& map,const render::Camera& view,int width,int height) const {
        const Rect whole{0,0,map.w,map.h};
        if(std::max(map.w,map.h)<=256)return whole;
        Rect box{map.w,map.h,0,0};
        for(const float y:{low,high})for(int sx:{0,width})for(int sy:{0,height}){
            render::float3 hit;
            if(!view.screenToGround(float(sx),float(sy),float(width),float(height),hit,y) ||
               !std::isfinite(hit.x)||!std::isfinite(hit.z))return whole;
            const int x=int(std::clamp(hit.x,0.f,float(map.w)));
            const int z=int(std::clamp(hit.z,0.f,float(map.h)));
            box[0]=std::min(box[0],std::max(0,x-2));box[2]=std::max(box[2],std::min(map.w,x+2));
            box[1]=std::min(box[1],std::max(0,z-2));box[3]=std::max(box[3],std::min(map.h,z+2));
        }
        return box;
    }
    void build(const editor::MapData& map,Rect area,float level,bool terrain,char selected) {
        bounds=area;edges.clear();stride=1;
        if(map.worldSpace || map.planet() || map.regions.size()!=size_t(map.w)*map.h)return;
        const auto cell=[&](int x,int z){return map.inside(x,z)?map.regions[map.idx(x,z)]:'.';};
        const auto edgeColor=[&](char left,char right){
            if(left==right)return '.';
            return (right!='.' && right==selected) || left=='.'?right:left;
        };
        constexpr size_t maxSegments=32768;
        // Keep long, straight borders as one segment. Only exceptionally dense views
        // skip alternate grid lines. Every drawn edge still compares adjacent cells.
        for(;;stride*=2){
            edges.clear();
            for(bool vertical:{false,true}){
                const int xEnd=vertical?map.h:map.w, zEnd=vertical?map.w:map.h;
                const int x0=area[vertical?1:0]/stride*stride, z0=area[vertical?0:1]/stride*stride;
                const int xmax=std::min(xEnd,((area[vertical?3:2]+stride-1)/stride)*stride);
                const int zmax=std::min(zEnd,((area[vertical?2:3]+stride-1)/stride)*stride);
                for(int z=z0;;z=std::min(z+stride,zmax)){
                    const int back=z-1;
                    bool join=false;
                    float lastSlope=0;
                    for(int x=x0;x<xmax;++x){
                        const int end=x+1;
                        const char c=vertical?edgeColor(cell(back,x),cell(z,x)):edgeColor(cell(x,back),cell(x,z));
                        if(c=='.'){join=false;continue;}
                        auto point=[&](int along){
                            int u=vertical?z:along,v=vertical?along:z;
                            return render::float3{float(u),terrain?terrainPreviewCorner(map,u,v,level)+.05f:.05f,float(v)};
                        };
                        const auto a=point(x),b=point(end);
                        const float slope=(b.y-a.y)/float(end-x);
                        if(join && edges.back().letter==c && slope==lastSlope)edges.back().b=b;
                        else edges.push_back({a,b,c});
                        join=true;lastSlope=slope;
                        if(edges.size()>maxSegments)break;
                    }
                    if(z==zmax || edges.size()>maxSegments)break;
                }
                if(edges.size()>maxSegments)break;
            }
            if(edges.size()<=maxSegments)return;
        }
    }
};
}
