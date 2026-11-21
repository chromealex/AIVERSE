// CPU reference matching the four-probe tetrahedral interpolation in GiDynamicShaders.h.
// Used by tests and optional software validation, never for authoritative ECS state.
#pragma once
#include "GiDynamicProbeReference.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>

namespace aiverse::render {

inline float3 sampleDynamicGi(const GiDynamicProbeReference& gi, float3 origin,
                              float spacing, int nx, int ny, int nz,
                              float3 world, float3 normal) {
    if (!std::isfinite(spacing) || spacing <= 0 || nx < 1 || ny < 1 || nz < 1 ||
        gi.coefficients().size() != size_t(nx) * size_t(ny) * size_t(nz))
        throw std::invalid_argument("GI sampling lattice mismatch");
    const float3 coord = (world - origin) * (1.0f / spacing) - float3{.5f,.5f,.5f};
    const float3 size{float(nx),float(ny),float(nz)};
    if (coord.x < -.5f || coord.y < -.5f || coord.z < -.5f ||
        coord.x > size.x-.5f || coord.y > size.y-.5f || coord.z > size.z-.5f)
        return {};
    const float edge = std::min({
        coord.x + .5f, size.x - .5f - coord.x,
        coord.y + .5f, size.y - .5f - coord.y,
        coord.z + .5f, size.z - .5f - coord.z
    });
    const float fade = std::clamp(edge*2.0f,0.0f,1.0f);
    const float3 p{
        std::clamp(coord.x,0.f,size.x-1.f),
        std::clamp(coord.y,0.f,size.y-1.f),
        std::clamp(coord.z,0.f,size.z-1.f)
    };
    const int base[3] = {int(std::floor(p.x)),int(std::floor(p.y)),int(std::floor(p.z))};
    const float f[3] = {p.x-base[0],p.y-base[1],p.z-base[2]};
    const int first = f[0]>=f[1] ? (f[0]>=f[2]?0:2) : (f[1]>=f[2]?1:2);
    int last = f[0]<=f[1] ? (f[0]<=f[2]?0:2) : (f[1]<=f[2]?1:2);
    if (first == last) last = (first+2)%3;
    const int middle = 3-first-last;
    float3 result{};
    for (int i=0;i<4;++i) {
        int corner[3] = {};
        if (i>=1) corner[first]=1;
        if (i>=2) corner[middle]=1;
        if (i>=3) corner[last]=1;
        const float weight = i==0 ? 1-f[first] : i==1 ? f[first]-f[middle] :
                             i==2 ? f[middle]-f[last] : f[last];
        if (weight<=0) continue;
        const int x = std::clamp(base[0]+corner[0],0,nx-1);
        const int y = std::clamp(base[1]+corner[1],0,ny-1);
        const int z = std::clamp(base[2]+corner[2],0,nz-1);
        result += gi.coefficient(uint32_t(x + nx * (y + ny*z))).evaluate(normal)*weight;
    }
    return result*fade;
}

} // namespace aiverse::render
