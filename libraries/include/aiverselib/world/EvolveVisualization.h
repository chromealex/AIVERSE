#pragma once
#include "EvolveArea.h"
#include <array>
namespace aiverse::evolve {
enum class VisualLayer { Fertility, Pollution, Succession, Durability, Combined };
struct VisualRegion {
    Extent bounds;
    std::array<float,4> rgba{};
    I fertility=0,pollution=0,succession=0,durability=0;
};
inline float visualUnit(I value){
    return float(std::clamp(value,I(0),kMaxState))/float(kMaxState);
}
inline std::vector<VisualRegion> visualRegions(const World& world,
        VisualLayer layer=VisualLayer::Combined,float opacity=.65f){
    std::vector<VisualRegion> result;
    for(const auto& extent:extents(world)){
        const auto& r=*world.tryGetConst<Region>(extent.region);
        if(!r.enabled||!valid(r))continue;
        const float f=visualUnit(r.fertility),p=visualUnit(r.pollution),
                    s=visualUnit(r.succession),d=visualUnit(r.durability);
        std::array<float,4> color{p,f,.2f,std::clamp(opacity,0.f,1.f)};
        switch(layer){
            case VisualLayer::Fertility:color[0]=.12f; color[1]=f;break;
            case VisualLayer::Pollution:color[0]=p; color[1]=.12f;break;
            case VisualLayer::Succession:color[0]=.15f; color[1]=s; color[2]=.8f;break;
            case VisualLayer::Durability:color[0]=1.f-d; color[1]=d;break;
            case VisualLayer::Combined:break;
        }
        result.push_back({extent,color,r.fertility,r.pollution,r.succession,r.durability});
    }
    return result;
}
}
