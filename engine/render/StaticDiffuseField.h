#pragma once
#include "StaticPointShadows.h"
#include <array>
namespace aiverse::render {
struct StaticDiffuseProbe {
    std::array<float4,3> irradiance{}; // RGB first-order clamped-cosine coefficients: constant, xyz.
    std::array<float4,3> sun{}, sky{}; // Unit sun / sky transport, relit without tracing.
    float4 skyVisibility{}; // Cosine-weighted upper-hemisphere visibility, first-order SH.
    StaticPointShadow visibility;
};
struct StaticDiffuseField {
    float3 origin{};
    float spacing=2;
    bool occludeAmbient=false;
    int nx=0,ny=0,nz=0;
    std::vector<StaticDiffuseProbe> probes;
};
}
