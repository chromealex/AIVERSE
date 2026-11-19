// PHYSIS Stage 22: presentation-only geometry for field marquee and world axes.
// No ImGui, genre module, physics scene or ECS side effects.
#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace aiverse::editor::physis_gizmo {

struct ScreenMarker {
    std::string id;
    double x=0, y=0;
};

inline std::vector<std::string> idsInScreenRect(
    const std::vector<ScreenMarker>& markers,
    double x0,double y0,double x1,double y1) {
    std::vector<std::string> picked;
    if (!std::isfinite(x0)||!std::isfinite(x1)||
        !std::isfinite(y0)||!std::isfinite(y1)) return picked;
    const double left=std::min(x0,x1),right=std::max(x0,x1);
    const double top=std::min(y0,y1),bottom=std::max(y0,y1);
    for (const auto& m:markers)
        if (!m.id.empty() && std::isfinite(m.x) && std::isfinite(m.y) &&
            m.x>=left && m.x<=right && m.y>=top && m.y<=bottom)
            picked.push_back(m.id);
    std::sort(picked.begin(),picked.end());
    picked.erase(std::unique(picked.begin(),picked.end()),picked.end());
    return picked;
}

inline render::float3 worldAxis(int axis) {
    switch (axis) {
        case 0: return {1,0,0};
        case 1: return {0,1,0};
        case 2: return {0,0,1};
        default: return {0,0,0};
    }
}

// Exact closest-point parameter between a ray and an infinite world axis.
// Rejects nearly parallel rays and any closest ray point behind the eye.
inline bool closestRayAxisParameter(const render::float3& rayOrigin,
                                   const render::float3& rayDir,
                                   const render::float3& pivot,
                                   int axis, double& parameter) {
    if (axis < 0 || axis > 2) return false;
    const double o[]={rayOrigin.x,rayOrigin.y,rayOrigin.z};
    const double d[]={rayDir.x,rayDir.y,rayDir.z};
    const double p[]={pivot.x,pivot.y,pivot.z};
    for (int i=0;i<3;++i)
        if (!std::isfinite(o[i])||!std::isfinite(d[i])||!std::isfinite(p[i]))
            return false;
    const double dn=d[0]*d[0]+d[1]*d[1]+d[2]*d[2];
    if (dn<1e-18) return false;
    const double invLength=1.0/std::sqrt(dn);
    const double b=d[axis]*invLength;
    const double denominator=1.0-b*b;
    if (denominator<1e-4) return false;
    const double v[]={p[0]-o[0],p[1]-o[1],p[2]-o[2]};
    const double e=(v[0]*d[0]+v[1]*d[1]+v[2]*d[2])*invLength;
    const double scalar=(b*e-v[axis])/denominator;
    const double alongRay=e+b*scalar;
    if (!std::isfinite(scalar)||!std::isfinite(alongRay) || alongRay<0.0)
        return false;
    parameter=scalar;
    return true;
}

// Screen-space hit testing is used only for choosing the handle, NEVER for
// converting movement into metres (closestRayAxisParameter does that).
inline double segmentDistanceSq(double mx,double my,double ax,double ay,
                                double bx,double by) {
    const double dx=bx-ax,dy=by-ay;
    const double len2=dx*dx+dy*dy;
    if (len2<1.0e-12) return 1.0e30;
    const double fraction=std::clamp(((mx-ax)*dx+(my-ay)*dy)/len2,0.0,1.0);
    const double px=ax+fraction*dx,py=ay+fraction*dy;
    return (mx-px)*(mx-px)+(my-py)*(my-py);
}

} // namespace aiverse::editor::physis_gizmo
