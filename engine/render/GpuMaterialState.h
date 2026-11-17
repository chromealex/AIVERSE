// GPU material equivalence for batching. Material object identity is irrelevant:
// only shader programs, pipeline state, uniforms and resolved texture resources matter.
#pragma once
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
            g.textures != h.textures || g.minQuality != h.minQuality) return false;
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
            m.fixedShade.x, m.fixedShade.y);
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
    f(m.fixedShade.x); f(m.fixedShade.y);
    auto texture = [&](int index) {
        u(index >= 0 && size_t(index) < model.images.size()
            ? model.images[size_t(index)].tex.id : 0);
    };
    for (int t : {m.baseTex, m.mrTex, m.normalTex, m.occlusionTex, m.emissiveTex, m.teamTex}) texture(t);
    if (m.shaderGraph)
        for (size_t i = 0; i < m.shaderGraph->textures.size() && i < 4; ++i)
            texture(m.graphTextures[i]);
    return hash;
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
