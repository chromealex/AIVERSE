#pragma once
// Native multi-span cave navigation source. Uses existing nav::Mesh;
// no independent pathfinder. The host owns authoritative terrain heights.
#include "engine/world/TerraWeaveCavernsEntrances.h"
#include "engine/nav/NavMesh.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <stdexcept>
namespace aiverse::terraweave::caverns {
struct CaveNavConfig {
    int32_t width=32,depth=32;
    int64_t originXcm=0,originZcm=0;
    int32_t columnCm=100;
    int64_t minYcm=-30000,maxYcm=-100;
    int32_t verticalStepCm=50;
    int32_t agentHeightCm=180;
    int32_t maxStepCm=40;
    uint32_t maxSlopePermille=700;
    uint32_t maxSpansPerColumn=8;
    int32_t tileColumns=16;
};
inline void validateCaveNav(const CaveNavConfig& c) {
    if(c.width<1||c.depth<1||int64_t(c.width)*c.depth>65536||
       c.columnCm<25||c.columnCm>3200||
       c.verticalStepCm<10||c.verticalStepCm>500||
       c.minYcm< -kLimitCm||c.maxYcm>kLimitCm||
       c.minYcm>=c.maxYcm||c.maxYcm-c.minYcm>200000||
       (c.maxYcm-c.minYcm)/c.verticalStepCm>2048||
       c.agentHeightCm<50||c.agentHeightCm>500||
       c.maxStepCm<0||c.maxStepCm>1000||
       c.maxSlopePermille>8000||c.maxSpansPerColumn<1||
       c.maxSpansPerColumn>32||c.tileColumns<1||c.tileColumns>1024||
       c.originXcm< -kLimitCm||c.originZcm< -kLimitCm||
       c.originXcm>kLimitCm-int64_t(c.width)*c.columnCm||
       c.originZcm>kLimitCm-int64_t(c.depth)*c.columnCm)
        throw std::invalid_argument("Caverns nav config");
}
inline nav::Config caveNavMeshConfig(const CaveNavConfig& c) {
    validateCaveNav(c);
    nav::Config n;
    n.width=c.width;n.depth=c.depth;
    n.originX=Fixed::fromRatio(c.originXcm,100);
    n.originZ=Fixed::fromRatio(c.originZcm,100);
    n.cell=Fixed::fromRatio(c.columnCm,100);
    n.tile=c.tileColumns;n.flat=false;
    n.maxSpans=uint32_t(c.width)*uint32_t(c.depth)*c.maxSpansPerColumn;
    return n;
}
class CaveNavSource final:public nav::Source {
public:
    using Height=std::function<int64_t(int64_t,int64_t)>;
    using Density=std::function<int64_t(PointCm)>;
    CaveNavSource(Settings caveSettings,CaveNavConfig config,
                  std::vector<Entrance> entrances,Height terrainHeight,
                  Density customDensity={})
        :caves_(caveSettings),cfg_(config),entrances_(std::move(entrances)),
         height_(std::move(terrainHeight)),density_(std::move(customDensity)) {
        validate(caves_);validateCaveNav(cfg_);
        if(!height_)throw std::invalid_argument("Caverns nav height callback");
    }
    void spans(int32_t x,int32_t z,std::vector<nav::Span>& out)const override {
        if(x<0||z<0||x>=cfg_.width||z>=cfg_.depth)return;
        std::vector<int64_t> floors;
        sampleFloors(x,z,floors);
        for(auto floor:floors)out.push_back({Fixed::fromRatio(floor,100),uint16_t(0)});
    }
    bool link(int32_t x0,int32_t z0,uint32_t i,int32_t x1,int32_t z1,uint32_t j)const override {
        if(x0<0||x0>=cfg_.width||z0<0||z0>=cfg_.depth||
           x1<0||x1>=cfg_.width||z1<0||z1>=cfg_.depth||
           std::abs(int64_t(x0)-x1)+std::abs(int64_t(z0)-z1)!=1)return false;
        std::vector<int64_t> a,b;sampleFloors(x0,z0,a);sampleFloors(x1,z1,b);
        if(i>=a.size()||j>=b.size())return false;
        const int64_t difference=std::abs(a[i]-b[j]);
        return difference<=cfg_.maxStepCm &&
            difference*1000<=int64_t(cfg_.columnCm)*cfg_.maxSlopePermille;
    }
    const CaveNavConfig& config()const{return cfg_;}
private:
    int64_t field(PointCm p)const{
        return density_ ? density_(p) : combinedFieldCm(caves_,entrances_,p);
    }
    void sampleFloors(int32_t x,int32_t z,std::vector<int64_t>& floors)const {
        floors.clear();
        const int64_t px=cfg_.originXcm+int64_t(x)*cfg_.columnCm+cfg_.columnCm/2;
        const int64_t pz=cfg_.originZcm+int64_t(z)*cfg_.columnCm+cfg_.columnCm/2;
        const int64_t ground=height_(px,pz);
        bool wasVoid=false;
        for(int64_t y=cfg_.minYcm;y<=cfg_.maxYcm;y+=cfg_.verticalStepCm) {
            const bool nowVoid=(y+cfg_.agentHeightCm<ground) &&
                field({px,y,pz})>0 &&
                field({px,y+cfg_.agentHeightCm,pz})>0;
            if(nowVoid&&!wasVoid) {
                floors.push_back(y);
                if(floors.size()>cfg_.maxSpansPerColumn)
                    throw std::length_error("Caverns nav spans budget exceeded");
            }
            wasVoid=nowVoid;
            if(cfg_.maxYcm-y<cfg_.verticalStepCm)break;
        }
    }
    Settings caves_;
    CaveNavConfig cfg_;
    std::vector<Entrance> entrances_;
    Height height_;
    Density density_;
};
} // namespace aiverse::terraweave::caverns
