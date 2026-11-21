// Read-only signed horizontal clearance for native authoring/presentation.
// Does not replace authoritative coversWaterBody or change water schemas.
#pragma once
#include <aiverselib/water/NereidGeometry.h>
#include <optional>

namespace aiverse::lib::nereid {
inline std::optional<Fixed> waterFootprintDistance(
        const WaterBodyComponent& body,const WaterGeometryComponent* geometry,Vec2 point) {
    if(!body.enabled||body.surfaceY<=body.bottomY)return std::nullopt;
    if(geometry&&geometry->enabled) {
        if(!validWaterGeometry(*geometry))return std::nullopt;
        std::optional<Fixed> best;
        const auto& pts=geometry->points;
        const size_t edges=geometry->kind==WaterGeometryKind::Polygon?pts.size():pts.size()-1;
        for(size_t i=0;i<edges;++i) {
            const auto& a=pts[i];const auto& b=pts[(i+1)%pts.size()];
            const Vec2 origin{a.x,a.z},delta{b.x-a.x,b.z-a.z};
            const auto length=delta.lengthSq();
            if(length<=Fixed{})continue;
            const auto t=fxClamp((point-origin).dot(delta)/length,0_fx,1_fx);
            auto distance=(point-origin-delta*t).length();
            if(geometry->kind==WaterGeometryKind::RiverSpline)
                distance-=a.halfWidth+(b.halfWidth-a.halfWidth)*t;
            if(!best||distance<*best)best=distance;
        }
        if(best&&geometry->kind==WaterGeometryKind::Polygon&&pointInWaterPolygon(*geometry,point))
            return -*best;
        return best;
    }
    const Vec2 delta=point-Vec2{body.center.x,body.center.z};
    switch(body.footprint) {
        case WaterFootprint::Infinite:return -1000000000_fx;
        case WaterFootprint::Disc:
            if(body.radius<=0_fx)return std::nullopt;
            return delta.length()-body.radius;
        case WaterFootprint::Box: {
            if(body.halfExtents.x<=0_fx||body.halfExtents.y<=0_fx)return std::nullopt;
            const Vec2 q{fxAbs(delta.x)-body.halfExtents.x,fxAbs(delta.y)-body.halfExtents.y};
            const Vec2 outside{fxMax(q.x,0_fx),fxMax(q.y,0_fx)};
            return outside.length()+fxMin(fxMax(q.x,q.y),0_fx);
        }
        default:return std::nullopt;
    }
}
} // namespace aiverse::lib::nereid
