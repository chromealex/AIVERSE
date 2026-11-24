// AIVERSE NEREID -- deterministic optional geometry for ECS water bodies.
// Pure geometry policy: no RTS map, rendering, floating point or global cache.
#pragma once
#include "NereidWater.h"
#include <cstddef>
#include <vector>

namespace aiverse::lib::nereid {

enum class WaterGeometryKind : uint8_t { Polygon=0, RiverSpline=1 };

// Polygon: x/z vertices, halfWidth ignored; first/last are implicitly joined.
// RiverSpline: centerline nodes, with interpolated halfWidth (metres).
struct WaterGeometryPoint {
    Fixed x{}, z{}, halfWidth{};
};
struct WaterGeometryComponent {
    WaterGeometryKind kind=WaterGeometryKind::Polygon;
    std::vector<WaterGeometryPoint> points;
    bool enabled=true; // false => use WaterBodyComponent's original footprint
};
inline constexpr size_t kMaxWaterGeometryPoints=256;

inline bool validWaterGeometry(const WaterGeometryComponent& geometry) {
    if(geometry.points.size()>kMaxWaterGeometryPoints)return false;
    switch(geometry.kind) {
        case WaterGeometryKind::Polygon:
            return geometry.points.size()>=3;
        case WaterGeometryKind::RiverSpline:
            if(geometry.points.size()<2)return false;
            for(const auto& point:geometry.points)
                if(point.halfWidth<=Fixed{})return false;
            return true;
        default:return false;
    }
}

inline bool pointInWaterPolygon(const WaterGeometryComponent& geometry,Vec2 point) {
    if(geometry.kind!=WaterGeometryKind::Polygon || !validWaterGeometry(geometry))
        return false;
    bool inside=false;
    const auto& pts=geometry.points;
    for(size_t i=0,j=pts.size()-1;i<pts.size();j=i++) {
        const auto& a=pts[j];
        const auto& b=pts[i];
        const Fixed dx=b.x-a.x, dz=b.z-a.z;
        const Fixed px=point.x-a.x, pz=point.y-a.z;
        // Include the boundary: adjacent body queries agree at a shared bank.
        const Fixed cross=px*dz-pz*dx;
        if(cross==Fixed{} &&
           point.x>=fxMin(a.x,b.x) && point.x<=fxMax(a.x,b.x) &&
           point.y>=fxMin(a.z,b.z) && point.y<=fxMax(a.z,b.z))
            return true;
        // Half-open crossings avoid double counts at vertices.
        if((a.z>point.y)!=(b.z>point.y)) {
            const Fixed hitX=a.x+dx*((point.y-a.z)/dz);
            if(hitX>point.x)inside=!inside;
        }
    }
    return inside;
}

inline bool pointInWaterRiver(const WaterGeometryComponent& geometry,Vec2 point) {
    if(geometry.kind!=WaterGeometryKind::RiverSpline || !validWaterGeometry(geometry))
        return false;
    const auto& pts=geometry.points;
    for(size_t i=1;i<pts.size();++i) {
        const auto& a=pts[i-1];const auto& b=pts[i];
        const Vec2 origin{a.x,a.z},direction{b.x-a.x,b.z-a.z};
        const Fixed lengthSq=direction.lengthSq();
        if(lengthSq<=Fixed{})continue; // Zero-length segment has no area.
        const Fixed t=fxClamp((point-origin).dot(direction)/lengthSq,
                              Fixed{},1_fx);
        const Vec2 closest=origin+direction*t;
        const Fixed width=a.halfWidth+(b.halfWidth-a.halfWidth)*t;
        if((point-closest).lengthSq()<=width*width)return true;
    }
    return false;
}

inline bool coversWaterGeometry(const WaterGeometryComponent& geometry,Vec3 point) {
    if(!geometry.enabled)return false;
    if(geometry.kind==WaterGeometryKind::Polygon)
        return pointInWaterPolygon(geometry,point.xz());
    if(geometry.kind==WaterGeometryKind::RiverSpline)
        return pointInWaterRiver(geometry,point.xz());
    return false;
}

// An attached and enabled geometry overrides only the footprint, not height,
// priority, body identity or currents. Invalid geometry covers nothing.
// Without an enabled geometry, v1 Infinite/Box/Disc behavior is unchanged.
inline bool coversWaterBody(const WaterBodyComponent& body,
                            const WaterGeometryComponent* geometry,
                            Vec3 point) {
    if(!body.enabled || body.surfaceY<=body.bottomY)return false;
    if(geometry && geometry->enabled)return coversWaterGeometry(*geometry,point);
    return coversXZ(body,point);
}

// Archive intentionally bounds hostile/corrupted lengths before allocating.
// A missing geometry component on old v1 saves is supported by the ECS schema.
inline void io(Archive& a,WaterGeometryPoint& point) {
    a(point.x,point.z,point.halfWidth);
}
inline void io(Archive& a,WaterGeometryComponent& geometry) {
    a(geometry.kind,geometry.enabled);
    const uint32_t n=a.count(geometry.points.size());
    if(n>kMaxWaterGeometryPoints) {a.fail();return;}
    if(!a.writing())geometry.points.assign(n,WaterGeometryPoint{});
    for(uint32_t i=0;i<n && a.ok();++i)a(geometry.points[i]);
}

} // namespace aiverse::lib::nereid
