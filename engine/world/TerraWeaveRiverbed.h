// Native authoring bridge: carve a NEREID RiverSpline into Terra Earthworks.
// Does not simulate water or change the authoritative water-body definition.
#pragma once
#include "engine/world/TerraWeaveEarthworks.h"
#include <aiverselib/water/NereidGeometry.h>
#include <aiverselib/water/NereidRiverGrade.h>
namespace aiverse::terraweave {
inline EarthworkChange carveNereidRiverbed(EarthworksLayer& earth,
        const lib::nereid::WaterBodyComponent& body,
        const lib::nereid::WaterGeometryComponent& geometry,
        RectCm bounds,int32_t bankWidthCm=500,
        const lib::nereid::WaterRiverGradeComponent* grade=nullptr) {
    using namespace lib::nereid;
    if(!validWaterGeometry(geometry)||geometry.kind!=WaterGeometryKind::RiverSpline||
       body.surfaceY<=body.bottomY||body.bottomY< -10000_fx||body.surfaceY>10000_fx||bankWidthCm<100||bankWidthCm>10000||
       bounds.minX>=bounds.maxX||bounds.minZ>=bounds.maxZ||
       bounds.minX< -10000000||bounds.maxX>10000000||
       bounds.minZ< -10000000||bounds.maxZ>10000000)
        throw std::invalid_argument("TerraWeave riverbed recipe");
    for(const auto& p:geometry.points)
        if(p.x< -100000_fx||p.x>100000_fx||p.z< -100000_fx||p.z>100000_fx||p.halfWidth>10000_fx)
            throw std::invalid_argument("TerraWeave riverbed geometry extent");
    const auto step=earth.spacingCm();
    const int64_t x0=-detail::floorDiv(-bounds.minX,step),z0=-detail::floorDiv(-bounds.minZ,step);
    const int64_t x1=-detail::floorDiv(-bounds.maxX,step),z1=-detail::floorDiv(-bounds.maxZ,step);
    if((x1-x0)*(z1-z0)>65536)throw std::length_error("TerraWeave riverbed edit budget");
    std::vector<EarthVertexEdit> edits;
    const Fixed bank=Fixed::fromRatio(bankWidthCm,100);
    for(int64_t z=z0;z<z1;++z)for(int64_t x=x0;x<x1;++x){
        const Vec2 p{Fixed::fromRatio(x*step,100),Fixed::fromRatio(z*step,100)};
        Fixed best=Fixed::fromInt(1000000000),width{};
        for(size_t i=1;i<geometry.points.size();++i){
            const auto& a=geometry.points[i-1];const auto& b=geometry.points[i];
            const Vec2 origin{a.x,a.z},delta{b.x-a.x,b.z-a.z};
            const auto length=delta.lengthSq();if(length<=Fixed{})continue;
            const auto t=fxClamp((p-origin).dot(delta)/length,Fixed{},1_fx);
            const auto distance=(p-(origin+delta*t)).length();
            if(distance<best){best=distance;width=a.halfWidth+(b.halfWidth-a.halfWidth)*t;}
        }
        if(best>=width+bank)continue;
        const EarthVertex key{x,z};const auto before=earth.heightAt(key);
        const auto original=Fixed::fromRatio(before,100);
        const auto localBody=gradedWaterBody(body,grade,{p.x,{},p.y});
        Fixed requested;
        // Bury the mesh perimeter inside the bank instead of ending over
        // ground below water: the visible shoreline is the depth intersection.
        if(best<width){
            const auto t=fxClamp((best-width*Fixed::fromRatio(1,2))/(width*Fixed::fromRatio(1,2)),Fixed{},1_fx);
            requested=localBody.bottomY+(localBody.surfaceY+Fixed::fromRatio(8,100)-localBody.bottomY)*t*t;
        }else{
            auto t=(best-width)/bank;t=t*t*(3_fx-2_fx*t);
            requested=(localBody.surfaceY+Fixed::fromRatio(8,100))*(1_fx-t)+original*t;
        }
        const auto after=int32_t(std::max<int64_t>(earth.bedrockCm(),std::min<int64_t>(before,(requested*100_fx).floorInt())));
        if(after!=before)edits.push_back({key,before,after});
    }
    return earth.applyEdits(edits,earth.revision());
}
} // namespace aiverse::terraweave
