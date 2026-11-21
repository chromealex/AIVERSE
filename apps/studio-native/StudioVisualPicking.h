// Precise CPU picking for the geometry actually submitted by HTML5 Studio:
// a rotated unit cube scaled to the document proxy half-extents. The ray is
// transformed into local cube coordinates and intersected with [-half,+half].
// Unlike world AABBs, this never selects the empty corners of a rotated proxy.
#pragma once
#include "apps/studio-native/StudioDocumentVisuals.h"
#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace aiverse::studio {

struct StudioVisualPick {
    std::string id;
    float distance = std::numeric_limits<float>::infinity();
};

inline float studioRayProxyBox(const StudioDocumentProxy& proxy,
                              render::float3 origin, render::float3 direction) {
    const float cosine=std::cos(proxy.yaw),sine=std::sin(proxy.yaw);
    // Inverse rotation of renderer's mat4::rotateY(yaw).
    const render::float3 relative=origin-proxy.center;
    const render::float3 localOrigin{
        cosine*relative.x-sine*relative.z,
        relative.y,
        sine*relative.x+cosine*relative.z
    };
    const render::float3 localDirection{
        cosine*direction.x-sine*direction.z,
        direction.y,
        sine*direction.x+cosine*direction.z
    };
    const float starts[3]={localOrigin.x,localOrigin.y,localOrigin.z};
    const float dirs[3]={localDirection.x,localDirection.y,localDirection.z};
    const float half[3]={proxy.half.x,proxy.half.y,proxy.half.z};
    float nearT=-std::numeric_limits<float>::infinity();
    float farT=std::numeric_limits<float>::infinity();
    for (int i=0;i<3;++i) {
        if (!std::isfinite(starts[i]) || !std::isfinite(dirs[i]) ||
            !std::isfinite(half[i]) || half[i]<=0)
            return std::numeric_limits<float>::infinity();
        if (std::abs(dirs[i])<1e-8f) {
            if (starts[i]<-half[i] || starts[i]>half[i])
                return std::numeric_limits<float>::infinity();
            continue;
        }
        const float a=(-half[i]-starts[i])/dirs[i];
        const float b=(half[i]-starts[i])/dirs[i];
        nearT=std::max(nearT,std::min(a,b));
        farT=std::min(farT,std::max(a,b));
        if (nearT>farT) return std::numeric_limits<float>::infinity();
    }
    if (farT<0) return std::numeric_limits<float>::infinity();
    return nearT>=0 ? nearT : farT; // inside: first visible exiting surface
}

inline StudioVisualPick studioPickVisibleProxy(
    const std::vector<StudioDocumentProxy>& proxies,
    render::float3 origin,render::float3 direction) {
    StudioVisualPick nearest;
    const float length2=render::dot(direction,direction);
    if (!std::isfinite(length2) || length2<1e-12f ||
        !std::isfinite(origin.x)||!std::isfinite(origin.y)||!std::isfinite(origin.z))
        return nearest;
    for (const auto& proxy:proxies) {
        const float distance=studioRayProxyBox(proxy,origin,direction);
        if (distance<nearest.distance) {
            nearest.id=proxy.id;
            nearest.distance=distance;
        }
    }
    return nearest;
}

} // namespace aiverse::studio
