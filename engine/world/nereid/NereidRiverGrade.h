// Optional deterministic water plane grade and friction-limited current.
#pragma once
#include "NereidGeometry.h"
namespace aiverse::lib::nereid {
struct WaterRiverGradeComponent {
    Vec2 origin{};
    Vec2 risePerMetre{}; // signed dY/dX,dY/dZ; downhill is negative gradient
    Fixed responseSeconds=5_fx; // linear drag relaxation: v_terminal = g*S*tau
    Fixed maxSpeed=10_fx;
    bool automaticCurrent=true;
    bool enabled=true;
};
inline bool sameWaterRiverGrade(const WaterRiverGradeComponent& a,const WaterRiverGradeComponent& b){return a.origin==b.origin&&a.risePerMetre==b.risePerMetre&&a.responseSeconds==b.responseSeconds&&a.maxSpeed==b.maxSpeed&&a.automaticCurrent==b.automaticCurrent&&a.enabled==b.enabled;}
inline bool validWaterRiverGrade(const WaterRiverGradeComponent& g){
    return g.enabled && fxAbs(g.origin.x)<=100000_fx && fxAbs(g.origin.y)<=100000_fx &&
        fxAbs(g.risePerMetre.x)<=1_fx && fxAbs(g.risePerMetre.y)<=1_fx &&
        g.responseSeconds>0_fx && g.responseSeconds<=60_fx && g.maxSpeed>0_fx && g.maxSpeed<=100_fx;
}
inline Fixed waterGradeOffset(const WaterRiverGradeComponent& g,Vec3 position){
    return (position.x-g.origin.x)*g.risePerMetre.x+(position.z-g.origin.y)*g.risePerMetre.y;
}
inline WaterBodyComponent gradedWaterBody(const WaterBodyComponent& body,
        const WaterRiverGradeComponent* grade,Vec3 position,const WaterGeometryComponent* geometry=nullptr){
    if(!grade||!validWaterRiverGrade(*grade))return body;
    auto result=body;const auto offset=waterGradeOffset(*grade,position);
    result.surfaceY+=offset;result.bottomY+=offset;
    if(grade->automaticCurrent){
        Vec2 velocity=grade->risePerMetre*(-Fixed::fromRatio(981,100)*grade->responseSeconds);
        if(geometry&&geometry->kind==WaterGeometryKind::RiverSpline&&validWaterGeometry(*geometry)){
            Fixed best=Fixed::fromInt(1000000000);Vec2 tangent{};
            for(size_t i=1;i<geometry->points.size();++i){const auto& a=geometry->points[i-1];const auto& b=geometry->points[i];const Vec2 delta{b.x-a.x,b.z-a.z},from{position.x-a.x,position.z-a.z};const auto length=delta.lengthSq();if(length<=Fixed{})continue;const auto t=fxClamp(from.dot(delta)/length,0_fx,1_fx);const auto distance=(from-delta*t).lengthSq();if(distance<best){best=distance;tangent=delta/delta.length();}}
            velocity=tangent*velocity.dot(tangent);
        }
        const auto speed=velocity.length();
        if(speed>grade->maxSpeed)velocity=velocity*(grade->maxSpeed/speed);
        result.flow={velocity.x,{},velocity.y};
    }
    return result;
}
inline void io(Archive& a,WaterRiverGradeComponent& g){
    a(g.origin.x,g.origin.y,g.risePerMetre.x,g.risePerMetre.y,g.responseSeconds,g.maxSpeed,g.automaticCurrent,g.enabled);
    if(!a.writing()&&g.enabled&&!validWaterRiverGrade(g))a.fail();
}
} // namespace aiverse::lib::nereid
