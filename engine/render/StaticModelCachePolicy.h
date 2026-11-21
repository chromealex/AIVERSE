#pragma once
// CPU-only eligibility contract for an optional per-entity rigid transform cache.
// The renderer still performs per-view culling, LOD, material selection and submission.
#include "Renderer3D.h"
#include "ModelStaticEligibility.h"
#include "GpuMaterialState.h"
#include <algorithm>
#include <cstring>
#include <vector>

namespace aiverse::render {

// An explicit stable identity is mandatory. Models may *contain authored*
// animation clips while a particular static instance remains at its bind
// pose. Never cache an active pose, baked animation, skin or mutable hierarchy.
inline bool staticModelCacheDrawCandidate(const ModelDraw& d,
                                          bool activeDisplacement,
                                          ModelStaticEligibility model) {
    return model.transforms && d.model && d.persistentId &&
        (d.flags & kDrawStatic) && !activeDisplacement &&
        !d.nodeWorld && !d.pose && !d.baked && !d.vertexOverride.valid() &&
        !d.useVertexParameters && d.primitiveMaterialFilter < 0;
}

// Public pure function for standalone callers. Renderer3D uses the identical
// draw policy with a per-model geometryRevision memo for the invariant part.
inline bool staticModelCacheCandidate(const ModelDraw& d, bool activeDisplacement) {
    return d.model &&
        staticModelCacheDrawCandidate(
            d, activeDisplacement, classifyModelStaticEligibility(*d.model));
}

// The cache contains only geometry-derived transforms, bounds and offsets.
// Material copies / per-node material overrides do not invalidate transforms;
// *active vertex displacement* does, and must bypass cached bounds as well.
inline bool staticModelCacheGeometryCompatible(const StaticModelCache& cache,
                                                const ModelDraw& d,
                                                bool activeDisplacement) {
    const Model* m = d.model;
    return m && cache.model == m && !activeDisplacement && !d.vertexOverride.valid() &&
        !d.useVertexParameters && d.primitiveMaterialFilter < 0 &&
        !d.nodeWorld && !d.pose && !d.baked && m->skins.empty() &&
        m->bindWorld.size() == m->nodes.size() &&
        cache.transforms.size() == m->nodes.size() &&
        cache.primitiveOffsets.size() == m->meshes.size() &&
        std::memcmp(cache.world.m, d.world.m, sizeof(d.world.m)) == 0;
}

// Packet replay is opt-in and conservatively validated against a snapshot of
// resolved GPU material states, even for pointer-stable per-draw material copies.
// Any non-material caller-side mutations still require staticPacketRevision.
// Runtime LOD is selected again for each camera and checked before reuse.
// GPU-authored selection packets carry a per-frame frameSerial and command
// buffers that change every frame; a replayed CPU packet would resubmit the
// stale packet captured at record time. Such draws never reuse packets.
inline bool drawHasGpuSelectionPackets(const ModelDraw& d) {
    return d.gpuIndexed.valid() || !d.gpuIndexedPrimitives.empty() ||
        !d.gpuShadowPrimitives.empty();
}

inline bool staticRenderPacketDrawCandidate(
    const ModelDraw& d, bool activeDisplacement, bool bakedLighting,
    bool lodDiagnostics, ModelStaticEligibility model) {
    // Per-node material overrides and KHR material variants are draw inputs:
    // their selected state is validated separately against each saved packet.
    return model.packets && d.staticPacketRevision &&
        !bakedLighting && !lodDiagnostics && !drawHasGpuSelectionPackets(d) &&
        staticModelCacheDrawCandidate(d, activeDisplacement, model);
}

inline bool staticRenderPacketCandidate(const ModelDraw& d,
                                        bool activeDisplacement, bool bakedLighting,
                                        bool lodDiagnostics) {
    return d.model && staticRenderPacketDrawCandidate(
        d, activeDisplacement, bakedLighting, lodDiagnostics,
        classifyModelStaticEligibility(*d.model));
}

// Capture resolved state rather than Material* identity. Hashes cheaply reject
// mutations; an exact comparison also guards against hash collisions.
// GPU texture *content* changed behind a stable handle still needs an explicit
// asset/material revision from its owner (normal texture upload contract).
inline std::vector<uint64_t> staticPacketMaterialHashes(
    const Model& model, const std::vector<Material>& materials) {
    std::vector<uint64_t> hashes;
    hashes.reserve(materials.size());
    for (const Material& material : materials)
        hashes.push_back(gpuMaterialStateHash(model, material));
    return hashes;
}

inline bool staticPacketMaterialStateMatches(
    const Model& model, const std::vector<Material>& live,
    const std::vector<Material>& saved, const std::vector<uint64_t>& hashes) {
    if (live.size() != saved.size() || live.size() != hashes.size()) return false;
    for (size_t i = 0; i < live.size(); ++i)
        if (gpuMaterialStateHash(model, live[i]) != hashes[i] ||
            !equivalentGpuMaterial(model, live[i], model, saved[i]))
            return false;
    return true;
}

inline bool equivalentStaticRenderPacketDraw(const ModelDraw& a,
                                              const ModelDraw& b) {
    const auto eq4 = [](const float4& x, const float4& y) {
        return x.x == y.x && x.y == y.y && x.z == y.z && x.w == y.w;
    };
    const auto eq3 = [](const float3& x, const float3& y) {
        return x.x == y.x && x.y == y.y && x.z == y.z;
    };
    const auto& g = a.geometry;
    const auto& h = b.geometry;
    return a.model == b.model && a.persistentId == b.persistentId &&
        a.staticPacketRevision == b.staticPacketRevision &&
        a.materialOwnerKey == b.materialOwnerKey &&
        a.materialOwnerRevision == b.materialOwnerRevision &&
        a.flags == b.flags && a.variant == b.variant &&
        a.flash == b.flash && a.teamGlow == b.teamGlow &&
        a.dissolve == b.dissolve &&
        eq4(a.tint, b.tint) && eq4(a.outline, b.outline) &&
        eq3(a.team, b.team) &&
        g.clusters == h.clusters && g.occlusion == h.occlusion &&
        g.occluder == h.occluder && g.shadowClusters == h.shadowClusters &&
        g.errorPixels == h.errorPixels &&
        g.shadowErrorPixels == h.shadowErrorPixels &&
        std::memcmp(a.world.m, b.world.m, sizeof(a.world.m)) == 0;
}

} // namespace aiverse::render
