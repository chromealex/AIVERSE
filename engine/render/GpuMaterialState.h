// GPU material equivalence for batching. Material object identity is irrelevant:
// only shader programs, pipeline state, uniforms and resolved texture resources matter.
#pragma once
#include <algorithm>
#include <cmath>
namespace aiverse::render {
// PBR material uniform light.w carries two independent features:
// floor() = thin diffuse transmission quantised to 1/255 (0 = legacy PBR),
// fract() = backlit SSS max transmitted world depth in [0, 0.999] (0 = artistic backlight).
inline float gpuPackThinSss(float thinTransmission, float sssThickness) {
    const float thin = std::isfinite(thinTransmission) ? std::clamp(thinTransmission, 0.0f, 1.0f) : 0.0f;
    const float depth = std::isfinite(sssThickness) ? std::clamp(sssThickness, 0.0f, 0.999f) : 0.0f;
    return std::round(thin * 255.0f) + depth;
}
}
#include "Model.h"
#include <tuple>
#include <bit>
#include <string_view>

namespace aiverse::render {

inline bool equivalentGpuMaterial(const Model& ma, const Material& a,
                                  const Model& mb, const Material& b) {
    if (a.shaderGraph != b.shaderGraph) {
        if (!a.shaderGraph || !b.shaderGraph) return false;
        const auto& g = *a.shaderGraph;
        const auto& h = *b.shaderGraph;
        if (g.domain != h.domain || g.vertexGlsl != h.vertexGlsl ||
            g.vertexMsl != h.vertexMsl || g.glsl != h.glsl ||
            g.msl != h.msl || g.outputs != h.outputs ||
            g.textures != h.textures || g.minQuality != h.minQuality ||
            g.usesVirtualTexture != h.usesVirtualTexture) return false;
    }
    auto state = [](const Material& m) {
        return std::tie(m.shading, m.alpha, m.alphaCutoff, m.doubleSided,
            m.vertexOcclusion, m.backOcclusion, m.vertexColors,
            m.castShadow, m.receiveShadow, m.transparent,
            m.team, m.teamStrength, m.normalScale, m.occlusionStrength,
            m.baseColor.x, m.baseColor.y, m.baseColor.z, m.baseColor.w,
            m.metallic, m.roughness, m.emissive.x, m.emissive.y,
            m.emissive.z, m.emissiveStrength, m.uvOffset.x,
            m.uvOffset.y, m.uvScale.x, m.uvScale.y, m.uvRotation,
            m.fixedLight.x, m.fixedLight.y, m.fixedLight.z, m.fixedLight.w,
            m.thinTransmission, m.shadowDiffusion, m.fixedShade.x, m.fixedShade.y,
            m.sssStrength, m.sssColor.x, m.sssColor.y, m.sssColor.z, m.sssScale, m.sssThickness, m.humanProfile.x, m.humanProfile.y, m.eyeOptics.x, m.eyeOptics.y, m.eyeOptics.z, m.eyeOptics.w);
    };
    if (state(a) != state(b)) return false;
    auto texture = [](const Model& model, int slot) -> uint32_t {
        return slot >= 0 && size_t(slot) < model.images.size()
            ? model.images[size_t(slot)].tex.id : 0;
    };
    const int ta[] = {a.baseTex, a.mrTex, a.normalTex, a.occlusionTex, a.emissiveTex, a.teamTex};
    const int tb[] = {b.baseTex, b.mrTex, b.normalTex, b.occlusionTex, b.emissiveTex, b.teamTex};
    for (size_t i = 0; i < 6; ++i)
        if (texture(ma, ta[i]) != texture(mb, tb[i])) return false;
    // A reduced VT base image can have a different full-resolution R8
    // alpha mask despite otherwise equivalent material parameters.
    const auto alphaTexture = [](const Model& model,const Material& material) {
        return material.baseTex >= 0 &&
               size_t(material.baseTex) < model.images.size() ?
               model.images[size_t(material.baseTex)].virtualAlphaMask.id : 0u;
    };
    if (alphaTexture(ma,a) != alphaTexture(mb,b)) return false;
    if (a.shaderGraph) {
        for (size_t i = 0; i < a.shaderGraph->textures.size() && i < 4; ++i)
            if (texture(ma, a.graphTextures[i]) != texture(mb, b.graphTextures[i])) return false;
    }
    return true;
}

// Fast, collision-safe grouping key. Hash collisions are resolved with
// equivalentGpuMaterial(), never treated as automatic material equivalence.
inline uint64_t gpuMaterialStateHash(const Model& model, const Material& m) {
    uint64_t hash = 14695981039346656037ull;
    auto u = [&](uint64_t v) { hash ^= v; hash *= 1099511628211ull; };
    auto f = [&](float v) { u(v == 0 ? 0u : std::bit_cast<uint32_t>(v)); };
    auto str = [&](std::string_view v) {
        u(v.size());
        for (unsigned char c : v) u(c);
    };
    u(m.shaderGraph ? 1 : 0);
    if (m.shaderGraph) {
        const auto& g = *m.shaderGraph;
        str(g.domain); str(g.vertexGlsl); str(g.vertexMsl);
        str(g.glsl); str(g.msl); u(g.minQuality);
        u(g.usesVirtualTexture ? 1u : 0u);
        u(g.textures.size());
        for (const auto& t : g.textures) str(t);
        u(g.outputs.size());
        for (const auto& [key, value] : g.outputs) { str(key); str(value); }
    }
    u(uint32_t(m.shading)); u(uint32_t(m.alpha)); f(m.alphaCutoff);
    for (bool b : {m.doubleSided, m.vertexOcclusion, m.backOcclusion,
                   m.vertexColors, m.castShadow, m.receiveShadow, m.transparent}) u(b);
    u(uint32_t(m.team)); f(m.teamStrength);
    f(m.normalScale); f(m.occlusionStrength);
    f(m.baseColor.x); f(m.baseColor.y); f(m.baseColor.z); f(m.baseColor.w);
    f(m.metallic); f(m.roughness);
    f(m.emissive.x); f(m.emissive.y); f(m.emissive.z); f(m.emissiveStrength);
    f(m.uvOffset.x); f(m.uvOffset.y);
    f(m.uvScale.x); f(m.uvScale.y); f(m.uvRotation);
    f(m.fixedLight.x); f(m.fixedLight.y); f(m.fixedLight.z); f(m.fixedLight.w);
    f(m.thinTransmission); f(m.shadowDiffusion); f(m.fixedShade.x); f(m.fixedShade.y);
    f(m.sssStrength); f(m.sssColor.x); f(m.sssColor.y); f(m.sssColor.z); f(m.sssScale);
    f(m.sssThickness);
    f(m.humanProfile.x); f(m.humanProfile.y);
    f(m.eyeOptics.x); f(m.eyeOptics.y); f(m.eyeOptics.z); f(m.eyeOptics.w);
    auto texture = [&](int index) {
        u(index >= 0 && size_t(index) < model.images.size()
            ? model.images[size_t(index)].tex.id : 0);
    };
    for (int t : {m.baseTex, m.mrTex, m.normalTex, m.occlusionTex, m.emissiveTex, m.teamTex}) texture(t);
    u(m.baseTex >= 0 && size_t(m.baseTex) < model.images.size() ?
        model.images[size_t(m.baseTex)].virtualAlphaMask.id : 0u);
    if (m.shaderGraph)
        for (size_t i = 0; i < m.shaderGraph->textures.size() && i < 4; ++i)
            texture(m.graphTextures[i]);
    return hash;
}


// Experimental Metal GPU-ICB *built-in opaque* material compatibility.
// U2 factors and textures 1..6 are per-command only with the opt-in
// descriptor ABI. Pipeline-sensitive features remain equal. Graph,
// masked/transparent, and alpha coverage variants remain excluded.
// The normal-texture presence affects the NORMAL_MAP shader permutation.
inline bool equivalentGpuIcbPbrPipelineMaterial(const Material& a,
                                                const Material& b) {
    return !a.shaderGraph && !b.shaderGraph &&
           a.alpha == AlphaMode::Opaque && b.alpha == AlphaMode::Opaque &&
           !a.transparent && !b.transparent &&
           a.doubleSided == b.doubleSided &&
           a.castShadow == b.castShadow &&
           a.receiveShadow == b.receiveShadow &&
           (a.normalTex >= 0) == (b.normalTex >= 0);
}

inline uint64_t gpuIcbPbrPipelineHash(const Material& m) {
    uint64_t h=14695981039346656037ull;
    const auto mix=[&](uint64_t value) {
        h^=value; h*=1099511628211ull;
    };
    mix(m.shaderGraph ? 1u : 0u);
    mix(uint32_t(m.alpha));
    mix(m.transparent ? 1u : 0u);
    mix(m.doubleSided ? 1u : 0u);
    mix(m.castShadow ? 1u : 0u);
    mix(m.receiveShadow ? 1u : 0u);
    mix(m.normalTex >= 0 ? 1u : 0u);
    return h;
}

// Experimental fragment-only Shader Graph compatibility for Metal GPU ICB.
// Nodes may differ in texture *path* only if their compiled shader source,
// graph wiring, output map and sampler count are identical. Actual source
// textures and U2 are distinct per-command resource-table signatures.
inline bool gpuIcbFragmentGraphEligible(const Material& m) {
    if (!m.shaderGraph || m.alpha != AlphaMode::Opaque || m.transparent)
        return false;
    const auto& g=*m.shaderGraph;
    return g.domain == "material" && g.textures.size() <= 4 &&
           g.vertexGlsl.empty() && g.vertexMsl.empty() &&
           !g.outputs.count("vertexOffset") &&
           !g.outputs.count("vertexNormal") &&
           g.outputs.count("color") != 0;
}
inline bool equivalentGpuIcbGraphPipelineMaterial(const Material& a,
                                                   const Material& b) {
    if (!gpuIcbFragmentGraphEligible(a) ||
        !gpuIcbFragmentGraphEligible(b)) return false;
    if (a.doubleSided != b.doubleSided ||
        a.castShadow != b.castShadow ||
        a.receiveShadow != b.receiveShadow ||
        (a.normalTex>=0) != (b.normalTex>=0)) return false;
    const auto& x=*a.shaderGraph;
    const auto& y=*b.shaderGraph;
    return x.domain == y.domain && x.minQuality == y.minQuality &&
           x.glsl == y.glsl && x.msl == y.msl &&
           x.vertexGlsl == y.vertexGlsl &&
           x.vertexMsl == y.vertexMsl &&
           x.outputs == y.outputs &&
           x.usesVirtualTexture == y.usesVirtualTexture &&
           x.textures.size() == y.textures.size();
}
inline uint64_t gpuIcbGraphPipelineHash(const Material& material) {
    uint64_t h=gpuIcbPbrPipelineHash(material);
    if (!gpuIcbFragmentGraphEligible(material)) return h;
    const auto& g=*material.shaderGraph;
    const auto mix=[&](uint64_t n) {
        h^=n;h*=1099511628211ull;
    };
    const auto str=[&](std::string_view v) {
        mix(v.size());for(unsigned char c:v)mix(c);
    };
    str(g.domain);str(g.glsl);str(g.msl);
    str(g.vertexGlsl);str(g.vertexMsl);
    mix(uint64_t(g.minQuality));mix(g.usesVirtualTexture ? 1u : 0u);
    mix(g.textures.size());
    for(const auto& [name,value]:g.outputs) {
        str(name);str(value);
    }
    return h;
}

// Indirect per-instance material palette: those three scalar factors travel
// with each indexed command instead of being inherited from U2. Every other
// shader factor, texture handle, sampler/alpha mode and graph remains part of
// the batching key. Copies are used only by the optional CPU grouping policy;
// the original Material objects are never mutated.
inline Material withoutGpuPaletteFactors(const Material& source) {
    Material normalized = source;
    normalized.baseColor = {1, 1, 1, 1};
    normalized.metallic = 0;
    normalized.roughness = 0;
    return normalized;
}
inline bool equivalentGpuPaletteState(const Model& aModel, const Material& a,
                                      const Model& bModel, const Material& b) {
    return equivalentGpuMaterial(aModel, withoutGpuPaletteFactors(a),
                                 bModel, withoutGpuPaletteFactors(b));
}
inline uint64_t gpuPaletteStateHash(const Model& model, const Material& material) {
    return gpuMaterialStateHash(model, withoutGpuPaletteFactors(material));
}

} // namespace aiverse::render
