// NEREID stage 7C: opt-in Renderer3D bridge for the universal water mesh.
// Game/scene owns the uploaded GPU handles and must release/rebuild them on
// content/LOD/quality change or device loss. Never called from Simulation.
#pragma once
#include "Renderer3D.h"
#include <aiverselib/water/NereidWaterSurfaceMesh.h>
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::render {

// All color composition is presentation-only. Per-vertex shore strength
// tints the wave surface toward foam; shader animates normals/reflections.
inline uint32_t nereidWaterVertexColor(
        const lib::nereid::WaterSurfaceVertex& vertex,
        const lib::nereid::WaterSurfaceStyle& authored={}) {
    const auto style=lib::nereid::resolvedWaterOptics(authored);
    const float foam=std::clamp(vertex.shore*style.foamStrength,0.0f,1.0f);
    auto channel=[foam](float deep,float rim) {
        const float x=std::clamp(deep+(rim-deep)*foam,0.0f,1.0f);
        return uint8_t(x*255.0f+0.5f);
    };
    return rgba(channel(style.deepR,style.foamR),
                channel(style.deepG,style.foamG),
                channel(style.deepB,style.foamB),255);
}
inline bool validNereidWaterStyle(const lib::nereid::WaterSurfaceStyle& s) {
    return std::isfinite(s.deepR)&&std::isfinite(s.deepG)&&std::isfinite(s.deepB)&&
           std::isfinite(s.foamR)&&std::isfinite(s.foamG)&&std::isfinite(s.foamB)&&
           std::isfinite(s.foamStrength)&&std::isfinite(s.opacity)&&
           std::isfinite(s.specular)&&std::isfinite(s.refractionStrength)&&
           std::isfinite(s.absorption)&&std::isfinite(s.shoreFoamDepth)&&
           std::isfinite(s.depthTint)&&
           std::isfinite(s.underwaterDistortion)&&
           std::isfinite(s.underwaterScattering)&&
           std::isfinite(s.rapidFoamStrength)&&
           std::isfinite(s.rapidFlowThreshold)&&
           std::isfinite(s.rapidNoiseScale)&&
           std::isfinite(s.rapidFoamR)&&std::isfinite(s.rapidFoamG)&&
           std::isfinite(s.rapidFoamB)&&
           std::isfinite(s.turbidity)&&s.turbidity>=0.f&&s.turbidity<=1.f&&
           std::isfinite(s.siltR)&&std::isfinite(s.siltG)&&std::isfinite(s.siltB)&&
           s.opacity>0 && s.opacity<0.99f &&
           s.specular>=0.85f && s.specular<=1.f &&
           s.foamStrength>=0 && s.foamStrength<=1 &&
           s.refractionStrength>=0 && s.refractionStrength<=1 &&
           s.absorption>=0 && s.absorption<=4 &&
           s.shoreFoamDepth>=0 && s.shoreFoamDepth<=20 &&
           s.depthTint>=0 && s.depthTint<=1 &&
           s.underwaterDistortion>=0 && s.underwaterDistortion<=1 &&
           s.underwaterScattering>=0 && s.underwaterScattering<=1 &&
           s.rapidFoamStrength>=0 && s.rapidFoamStrength<=1 &&
           s.rapidFlowThreshold>=0 && s.rapidFlowThreshold<=100 &&
           s.rapidNoiseScale>=0.1f && s.rapidNoiseScale<=8.f &&
           s.rapidFoamR>=0.f && s.rapidFoamR<=1.f &&
           s.rapidFoamG>=0.f && s.rapidFoamG<=1.f &&
           s.rapidFoamB>=0.f && s.rapidFoamB<=1.f &&
           uint8_t(s.rapidDetail)<=uint8_t(lib::nereid::WaterRapidDetail::High);
}

// Upload at content/render setup, not every simulation tick.
// A successfully built mesh has its own static VB/IB; release it explicitly.
inline Mesh uploadNereidWaterSurface(gfx::Device& device,
        const lib::nereid::WaterSurfaceMesh& surface,
        lib::nereid::WaterSurfaceStyle style={}) {
    if(surface.empty() || !validNereidWaterStyle(style))return {};
    MeshBuilder builder;
    const auto normal=normalize(float3{surface.normalX,1.f,surface.normalZ});
    for(const auto& v:surface.vertices)
        builder.addVertex({v.x,v.y,v.z},normal,
                          nereidWaterVertexColor(v,style));
    for(size_t i=0;i<surface.indices.size();i+=3)
        builder.addTriangle(surface.indices[i],surface.indices[i+1],
                            surface.indices[i+2]);
    return builder.upload(device);
}
inline void drawNereidWaterSurface(Renderer3D& renderer,const Mesh& mesh,
        lib::nereid::WaterSurfaceStyle style={},
        const mat4& transform=mat4::identity(),
        const std::array<float4,16>& ripples={},
        uint32_t firstIndex=0,uint32_t indexCount=0,
        const std::array<float4,16>& wakeShapes={},
        float4 wind={},float4 hydraulics={},NereidFlowBinding flow={}) {
    if(mesh.indexCount==0 || !validNereidWaterStyle(style))return;
    style=lib::nereid::resolvedWaterOptics(style);
    renderer.drawNereidRippleMesh(mesh,transform,
                      {style.absorption,style.shoreFoamDepth,
                       style.depthTint,style.opacity},
                       style.specular,style.refractionStrength,ripples,
                       firstIndex,indexCount,wakeShapes,wind,hydraulics,
                        {style.rapidFoamR,style.rapidFoamG,
                         style.rapidFoamB,float(style.rapidDetail)},std::move(flow));
}
inline void releaseNereidWaterSurface(gfx::Device& device,Mesh& mesh) {
    if(mesh.vb.valid())device.destroyBuffer(mesh.vb);
    if(mesh.ib.valid())device.destroyBuffer(mesh.ib);
    mesh={};
}

} // namespace aiverse::render
