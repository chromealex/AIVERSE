// NEREID — deterministic, genre-independent spatial currents along rivers.
// Optional ECS field on the water body; no PHYSIS/Box3D/render dependency.
// WaterBody.flow remains the base current and is not mutated.
#pragma once
#include "NereidGeometry.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace aiverse::lib::nereid {

// One control per RiverSpline point. Speed follows the local river tangent;
// lateral flows toward the right bank (signed), shear varies across width.
struct WaterCurrentNode {
    Fixed downstream=0_fx;   // m/s relative to WaterBody.flow
    Fixed lateral=0_fx;      // m/s toward right bank
    Fixed shear=0_fx;        // signed bank-to-bank downstream variation
};
struct WaterCurrentFieldComponent {
    std::vector<WaterCurrentNode> nodes;
    Fixed bankFade=Fixed::fromRatio(1,4); // 0..1 fraction of bank strip
    bool enabled=true;
};

inline bool validWaterCurrentField(const WaterCurrentFieldComponent& field,
                                   const WaterGeometryComponent* geometry) {
    if(!field.enabled || !geometry || !geometry->enabled ||
       geometry->kind!=WaterGeometryKind::RiverSpline ||
       !validWaterGeometry(*geometry) ||
       field.nodes.size()!=geometry->points.size() ||
       field.bankFade<0_fx || field.bankFade>1_fx)return false;
    for(const auto& node:field.nodes)
        if(node.downstream < -1000_fx || node.downstream > 1000_fx ||
           node.lateral < -1000_fx || node.lateral > 1000_fx ||
           node.shear < -1000_fx || node.shear > 1000_fx)return false;
    return true;
}

// Pure fixed-point computation shared by reference and indexed sampling.
// Select closest segment with stable first-segment tie breaking.
// Degenerate segments are ignored; invalid field falls back to base flow.
inline Vec3 waterCurrentAt(const WaterBodyComponent& body,
                          const WaterGeometryComponent* geometry,
                          const WaterCurrentFieldComponent* field,
                          Vec3 position) {
    if(!field || !validWaterCurrentField(*field,geometry))return body.flow;
    const auto& points=geometry->points;
    size_t best=points.size();
    Fixed bestDistance{};
    Fixed bestT{};
    Vec2 bestClosest{};
    for(size_t i=1;i<points.size();++i) {
        const Vec2 a{points[i-1].x,points[i-1].z};
        const Vec2 d{points[i].x-a.x,points[i].z-a.y};
        const Fixed lenSq=d.lengthSq();
        if(lenSq<=Fixed{})continue;
        const Vec2 p=position.xz();
        const Fixed t=fxClamp((p-a).dot(d)/lenSq,0_fx,1_fx);
        const Vec2 closest=a+d*t;
        const Fixed distance=(p-closest).lengthSq();
        if(best==points.size() || distance<bestDistance) {
            best=i;bestDistance=distance;bestT=t;bestClosest=closest;
        }
    }
    if(best==points.size())return body.flow;
    const Vec2 a{points[best-1].x,points[best-1].z};
    const Vec2 d{points[best].x-a.x,points[best].z-a.y};
    const Fixed len=d.length();
    if(len<=Fixed{})return body.flow;
    const Vec2 tangent=d/len;
    const Vec2 right{tangent.y,-tangent.x};
    const auto& from=field->nodes[best-1];
    const auto& to=field->nodes[best];
    const Fixed downstream=from.downstream+(to.downstream-from.downstream)*bestT;
    const Fixed lateral=from.lateral+(to.lateral-from.lateral)*bestT;
    const Fixed shear=from.shear+(to.shear-from.shear)*bestT;
    const Fixed width=points[best-1].halfWidth+
        (points[best].halfWidth-points[best-1].halfWidth)*bestT;
    const Fixed signedSide=fxClamp(
        ((position.xz()-bestClosest).dot(right))/width,-1_fx,1_fx);
    // Fade additional currents at the river banks: avoids high-speed
    // velocities persisting outside a spline's hydraulic footprint.
    const Fixed distanceFromBank=1_fx-fxAbs(signedSide);
    const Fixed edge=field->bankFade>Fixed{} ?
        fxClamp(distanceFromBank/field->bankFade,0_fx,1_fx) : 1_fx;
    const Fixed along=(downstream+shear*signedSide)*edge;
    const Fixed across=lateral*edge;
    return body.flow+Vec3{
        tangent.x*along+right.x*across,
        Fixed{},
        tangent.y*along+right.y*across
    };
}

inline void io(Archive& a,WaterCurrentNode& node) {
    a(node.downstream,node.lateral,node.shear);
}
inline void io(Archive& a,WaterCurrentFieldComponent& field) {
    a(field.bankFade,field.enabled);
    const uint32_t count=a.count(field.nodes.size());
    if(count>kMaxWaterGeometryPoints) {a.fail();return;}
    if(!a.writing())field.nodes.assign(count,{});
    for(uint32_t i=0;i<count && a.ok();++i)a(field.nodes[i]);
}
} // namespace aiverse::lib::nereid
