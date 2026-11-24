// NEREID Stage 34 — optional deterministic vortex and eddy currents.
// Same fixed-point water-body ECS source; no GPU, PHYSIS or genre coupling.
#pragma once
#include "NereidCurrentField.h"
#include <algorithm>
#include <cstddef>
#include <vector>

namespace aiverse::lib::nereid {

// Horizontal X/Z centre and finite radius in metres. Positive circulation
// rotates counterclockwise in X/Z, positive radial pushes outward.
struct WaterVortex {
    Vec2 center{};
    Fixed radius=5_fx;
    Fixed circulation=2_fx;
    Fixed radial=0_fx;
};
struct WaterVortexFieldComponent {
    std::vector<WaterVortex> vortices;
    bool enabled=true;
};
inline constexpr size_t kMaxWaterVortices=16;
inline bool validWaterVortexField(const WaterVortexFieldComponent& field) {
    if(!field.enabled || field.vortices.size()>kMaxWaterVortices)return false;
    for(const auto& vortex:field.vortices) {
        if(vortex.radius<=Fixed{} || vortex.radius>10000_fx ||
           fxAbs(vortex.circulation)>1000_fx ||
           fxAbs(vortex.radial)>1000_fx)return false;
    }
    return true;
}
// Local velocity due to a spatially bounded vortex field. The resolved
// water body still owns base flow; this is an additive optional component.
inline Vec3 waterVortexVelocity(const WaterVortexFieldComponent* field,
                               Vec3 point) {
    if(!field || !validWaterVortexField(*field))return {};
    Vec2 sum{};
    for(const auto& vortex:field->vortices) {
        const Vec2 displacement=point.xz()-vortex.center;
        const Fixed distance=displacement.length();
        if(distance<=Fixed{} || distance>=vortex.radius)continue;
        const Vec2 direction=displacement/distance;
        const Vec2 tangent{-direction.y,direction.x};
        const Fixed falloff=1_fx-distance/vortex.radius;
        const Fixed weight=falloff*falloff;
        sum+=tangent*(vortex.circulation*weight)+
             direction*(vortex.radial*weight);
    }
    return {fxClamp(sum.x,-1000_fx,1000_fx),Fixed{},
            fxClamp(sum.y,-1000_fx,1000_fx)};
}
// Unified resolved local flow used by both water samplers and presentation.
// River profile is optional; vortices may be attached to any footprint.
inline Vec3 waterCurrentWithVortices(
        const WaterBodyComponent& body,
        const WaterGeometryComponent* geometry,
        const WaterCurrentFieldComponent* current,
        const WaterVortexFieldComponent* vortices,
        Vec3 position) {
    const Vec3 river=waterCurrentAt(body,geometry,current,position);
    const Vec3 eddy=waterVortexVelocity(vortices,position);
    return {fxClamp(river.x+eddy.x,-1000_fx,1000_fx),
            river.y,
            fxClamp(river.z+eddy.z,-1000_fx,1000_fx)};
}
inline void io(Archive& a,WaterVortex& v) {
    a(v.center.x,v.center.y,v.radius,v.circulation,v.radial);
}
inline void io(Archive& a,WaterVortexFieldComponent& f) {
    a(f.enabled);
    const uint32_t count=a.count(f.vortices.size());
    if(count>kMaxWaterVortices){a.fail();return;}
    if(!a.writing())f.vortices.assign(count,{});
    for(uint32_t i=0;i<count && a.ok();++i)a(f.vortices[i]);
}
} // namespace aiverse::lib::nereid
