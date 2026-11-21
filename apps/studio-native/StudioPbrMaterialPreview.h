// AIVERSE Studio PBR preview — convert *resolved* authoring factors into the
// renderer's existing PBR material data. Pure CPU adapter: no ImGui, file IO,
// preview screenshots, custom shaders or game genre assumptions.
#pragma once
#include "engine/render/Model.h"
#include "engine/data/Json.h"
#include <algorithm>
#include <cmath>
#include <string>

namespace aiverse::studio {
inline float previewScalar(const Json& value, float fallback, float lo, float hi) {
    if (!value.isNumber()) return fallback;
    const double number = value.toDouble();
    return std::isfinite(number) ? float(std::clamp(number,double(lo),double(hi))) : fallback;
}
inline render::float3 previewRgb(const Json& value, render::float3 fallback) {
    if (!value.isArray() || value.size()<3) return fallback;
    return {previewScalar(value[0],fallback.x,0,1),
            previewScalar(value[1],fallback.y,0,1),
            previewScalar(value[2],fallback.z,0,1)};
}
inline render::Material studioPbrPreviewMaterial(const Json& resolved,
                                                 const std::string& name) {
    render::Material out;
    out.name=name;
    out.shading=render::ShadingModel::Pbr;
    out.vertexColors=false;
    const auto rgb=previewRgb(resolved["color"],{.68f,.73f,.82f});
    out.baseColor={rgb.x,rgb.y,rgb.z,1};
    if(resolved["color"].isArray() && resolved["color"].size()>=4)
        out.baseColor.w=previewScalar(resolved["color"][3],1,0,1);
    out.metallic=previewScalar(resolved["metallic"],0,0,1);
    out.roughness=previewScalar(resolved["roughness"],.8f,.04f,1);
    out.thinTransmission=previewScalar(resolved["thinTransmission"],0,0,1);
    out.shadowDiffusion=previewScalar(resolved["shadowDiffusion"],0,0,1);
    out.sssStrength=previewScalar(resolved["sssStrength"],0,0,1);
    out.sssScale=previewScalar(resolved["sssScale"],.5f,0,1);
    out.sssThickness=previewScalar(resolved["sssThickness"],0,0,1);
    out.sssColor=previewRgb(resolved["sssColor"],{1,1,1});
    out.normalScale=previewScalar(resolved["normalScale"],1,0,16);
    out.occlusionStrength=previewScalar(resolved["occlusionStrength"],1,0,1);
    out.emissive=previewRgb(resolved["emissive"],{0,0,0});
    out.emissiveStrength=previewScalar(resolved["emissiveStrength"],1,0,100);
    const std::string alpha=resolved.getStr("alpha","opaque");
    if(alpha=="mask")out.alpha=render::AlphaMode::Mask;
    else if(alpha=="blend") {
        out.alpha=render::AlphaMode::Blend;
        out.transparent=true;
    } else out.alpha=render::AlphaMode::Opaque;
    out.alphaCutoff=previewScalar(resolved["cutoff"],.5f,0,1);
    out.doubleSided=resolved.getBool("doubleSided",false);
    out.castShadow=resolved.getBool("castShadow",true);
    out.receiveShadow=resolved.getBool("receiveShadow",true);
    if(resolved.getBool("unlit",false))out.shading=render::ShadingModel::Unlit;
    // Image maps, shader graphs, overrides and UV transforms are NOT
    // evaluated in this self-contained geometric preview.
    return out;
}
}
