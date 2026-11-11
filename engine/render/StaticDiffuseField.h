#pragma once
#include "StaticPointShadows.h"
#include <array>
namespace aiverse::render {
struct StaticDiffuseProbe {
    std::array<float4,3> irradiance{}; // RGB first-order clamped-cosine coefficients: constant, xyz.
    StaticPointShadow visibility;
};
struct StaticDiffuseField {
    float3 origin{};
    float spacing=2;
    int nx=0,ny=0,nz=0;
    std::vector<StaticDiffuseProbe> probes;
};
}
