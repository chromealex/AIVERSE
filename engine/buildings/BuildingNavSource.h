#pragma once
// Native nav::Source for procedural building walkable floor spans.
// A geometry-to-raster adapter, not another navmesh implementation.
// Stairs and lifts continue to use the explicit off-mesh circulation API.
#include "engine/buildings/BuildingNavMeshBridge.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::building {

struct BuildingNavSourceOptions {
    Fixed cell=Fixed::fromRatio(1,2);
    Fixed actorHeight=Fixed::fromRatio(9,5);
    Fixed stepHeight=Fixed::fromRatio(1,5);
    int32_t maxColumns=1024*1024;
    uint16_t floorArea=1;
};

class BuildingNavSource final:public nav::Source {
public:
    bool initialize(const BuildingStructureGraph& graph,
                    float width,float depth,Vec3 origin,uint32_t yaw,
                    BuildingNavSourceOptions options={},
                    std::string* error=nullptr) {
        auto fail=[&](const char* why) {
            if(error)*error=why;
            return false;
        };
        if(!graph||yaw>3||!std::isfinite(width)||!std::isfinite(depth)||
           width<=0||depth<=0||options.cell.raw<=0||
           options.actorHeight.raw<=0||options.stepHeight.raw<0||
           options.maxColumns<=0||options.floorArea==0)
            return fail("Invalid building source graph, transform or options");
        const auto fixed=[](float value) {
            return Fixed::fromRatio(int64_t(std::llround(double(value)*1000)),1000);
        };
        const Fixed w=fixed(width),d=fixed(depth);
        const Fixed extentX=(yaw%2)?d:w,extentZ=(yaw%2)?w:d;
        const int64_t nx=(extentX.raw+options.cell.raw-1)/options.cell.raw;
        const int64_t nz=(extentZ.raw+options.cell.raw-1)/options.cell.raw;
        if(nx<=0||nz<=0||nx>INT32_MAX||nz>INT32_MAX||
           nx*nz>options.maxColumns)return fail("Building nav raster budget exceeded");
        // Stage all spans first: a failed initialization must not publish
        // a partially initialized Source to the existing nav::Mesh.
        std::vector<Solid> staged;
        staged.reserve(graph.solids.size());
        for(const auto& part:graph.solids) {
            if(!(part.x0<part.x1&&part.y0<part.y1&&part.z0<part.z1))
                return fail("Invalid building solid bounds");
            staged.push_back({part.part,
                fixed(part.x0),fixed(part.y0),fixed(part.z0),
                fixed(part.x1),fixed(part.y1),fixed(part.z1)});
        }
        nav::Config cfg;
        cfg.width=static_cast<int32_t>(nx);
        cfg.depth=static_cast<int32_t>(nz);
        cfg.originX=origin.x;
        cfg.originZ=origin.z;
        cfg.cell=options.cell;
        cfg.flat=true; // floor layer heights remain explicit
        BuildingNavSource candidate;
        candidate.config_=cfg;candidate.options_=options;
        candidate.origin_=origin;candidate.width_=w;
        candidate.depth_=d;candidate.yaw_=yaw;
        candidate.solids_=std::move(staged);
        candidate.columns_.resize(size_t(nx*nz));
        // Raster in fixed-size deterministic column order. A cell whose
        // footprint extends outside the building is conservatively empty.
        for(int32_t z=0;z<cfg.depth;++z)
            for(int32_t x=0;x<cfg.width;++x)
                candidate.sampleColumn(x,z,
                    candidate.columns_[size_t(z)*size_t(cfg.width)+size_t(x)]);
        *this=std::move(candidate);
        if(error)error->clear();
        return true;
    }

    const nav::Config& config()const noexcept{return config_;}
    void spans(int32_t x,int32_t z,std::vector<nav::Span>& out)const override {
        if(!inside(x,z))return;
        const auto& col=columns_[size_t(z)*size_t(config_.width)+size_t(x)];
        out.insert(out.end(),col.begin(),col.end());
    }
    bool link(int32_t x0,int32_t z0,uint32_t i,
              int32_t x1,int32_t z1,uint32_t j)const override {
        if(!inside(x0,z0)||!inside(x1,z1)||
           (std::abs(x1-x0)+std::abs(z1-z0))!=1)return false;
        const auto& a=columns_[size_t(z0)*size_t(config_.width)+size_t(x0)];
        const auto& b=columns_[size_t(z1)*size_t(config_.width)+size_t(x1)];
        if(i>=a.size()||j>=b.size()||
           fxAbs(a[i].y-b[j].y)>options_.stepHeight)return false;
        const Fixed y=fxMax(a[i].y,b[j].y);
        const Fixed wx=config_.originX+
            config_.cell*int64_t(x0+x1+1)/2;
        const Fixed wz=config_.originZ+
            config_.cell*int64_t(z0+z1+1)/2;
        const auto [lx,lz]=local(wx,wz);
        if(lx<Fixed{}||lz<Fixed{}||lx>=width_||lz>=depth_)return false;
        // Sweep the boundary midpoint against structural walls/partitions
        // including door leaf collision. We don't create a fake portal
        // across a closed partition or a wall.
        return !blocked(lx,lz,y-origin_.y);
    }
private:
    struct Solid {
        BuildingGraphPart part;
        Fixed x0,y0,z0,x1,y1,z1;
    };
    bool inside(int32_t x,int32_t z)const noexcept{
        return x>=0&&z>=0&&x<config_.width&&z<config_.depth;
    }
    std::pair<Fixed,Fixed> local(Fixed wx,Fixed wz)const {
        const Fixed x=wx-origin_.x,z=wz-origin_.z;
        switch(yaw_) {
            case 1:return {z,depth_-x};
            case 2:return {width_-x,depth_-z};
            case 3:return {width_-z,x};
            default:return {x,z};
        }
    }
    static bool supports(BuildingGraphPart part){
        // ElevatorPlatform is dynamic and intentionally omitted from the
        // static mesh. StairTreads are handled by explicit stair paths.
        return part==BuildingGraphPart::Floor;
    }
    bool blocked(Fixed x,Fixed z,Fixed y)const {
        const Fixed skin=Fixed::fromRatio(1,100);
        for(const auto& s:solids_) {
            if(x<s.x0||x>=s.x1||z<s.z0||z>=s.z1)continue;
            // Only geometry actually intersecting the actor's standing
            // headroom obstructs this surface. A higher floor slab must
            // not erase a lower floor if sufficient headroom remains.
            if(s.y1<=y+skin||s.y0>=y+options_.actorHeight)continue;
            return true;
        }
        return false;
    }
    void sampleColumn(int32_t x,int32_t z,
                      std::vector<nav::Span>& out)const {
        const Fixed wx=config_.originX+config_.cell*int64_t(2*x+1)/2;
        const Fixed wz=config_.originZ+config_.cell*int64_t(2*z+1)/2;
        const auto [lx,lz]=local(wx,wz);
        if(lx<Fixed{}||lz<Fixed{}||lx>=width_||lz>=depth_)return;
        for(const auto& s:solids_) {
            if(!supports(s.part)||lx<s.x0||lx>=s.x1||
               lz<s.z0||lz>=s.z1)continue;
            const Fixed y=origin_.y+s.y1;
            if(!blocked(lx,lz,s.y1))
                out.push_back({y,options_.floorArea});
        }
        std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){
            if(a.y!=b.y)return a.y<b.y;
            return a.area<b.area;
        });
        out.erase(std::unique(out.begin(),out.end(),[](const auto& a,const auto& b){
            return a.y==b.y&&a.area==b.area;
        }),out.end());
    }
    nav::Config config_{};
    BuildingNavSourceOptions options_{};
    Vec3 origin_{};
    Fixed width_{},depth_{};
    uint32_t yaw_=0;
    std::vector<Solid> solids_;
    std::vector<std::vector<nav::Span>> columns_;
};

} // namespace aiverse::building
