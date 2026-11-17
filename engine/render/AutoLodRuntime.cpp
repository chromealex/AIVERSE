#include "AutoLodRuntime.h"
#include "Model.h"
#include "meshoptimizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

namespace aiverse::render {
namespace {
constexpr size_t kMinPrimitiveIndices = 384; // 128 triangles
constexpr size_t kMaxVertices = 500000; // bound import latency + temporary memory
constexpr size_t kMaxIndices = 1500000;
constexpr std::array<float, 3> kRatios{0.50f, 0.25f, 0.125f};
constexpr std::array<float, 3> kError{0.005f, 0.01f, 0.02f};
constexpr float kAttributeWeights[15] = {
    .5f, .5f, .5f,  // normals
    1.f, 1.f,       // UV0
    .25f, .25f, .25f, .25f, // tangent incl. handedness
    1.f, 1.f, 1.f, 1.f, // vertex RGBA (can carry AO)
    1.f, 1.f        // encoded lightmap UV1
};

bool eligible(const Model& model, const MeshDef& mesh) {
    if (!mesh.lods.empty() || mesh.deformable || mesh.primitives.empty())
        return false;
    for (const Primitive& p : mesh.primitives) {
        if (p.indexCount == 0 || p.material < 0 ||
            size_t(p.material) >= model.materials.size() ||
            p.indexCount % 3 || p.firstIndex > model.indices.size() ||
            p.indexCount > model.indices.size() - p.firstIndex ||
            !p.jointPalette.empty() || p.morphTargets ||
            !p.variantMaterial.empty())
            return false;
        const Material& material = model.materials[size_t(p.material)];
        // Opaque ordinary materials only. Masked foliage, custom vertex
        // displacement and transparent glass require author-controlled LOD.
        if (material.alpha != AlphaMode::Opaque || material.transparent ||
            material.shaderGraph)
            return false;
    }
    return true;
}

std::vector<float> vertexAttributes(const Model& model) {
    std::vector<float> attrs;
    attrs.reserve(model.vertices.size() * 15);
    for (const ModelVertex& v : model.vertices) {
        attrs.insert(attrs.end(), {
            v.nrm.x, v.nrm.y, v.nrm.z, v.uv.x, v.uv.y,
            v.tangent.x, v.tangent.y, v.tangent.z, v.tangent.w
        });
        for (int ch = 0; ch < 4; ++ch)
            attrs.push_back(float((v.color >> (8 * ch)) & 255u) / 255.f);
        const uint32_t lmU = uint32_t(v.joints[0]) | (uint32_t(v.joints[1]) << 8);
        const uint32_t lmV = uint32_t(v.joints[2]) | (uint32_t(v.joints[3]) << 8);
        attrs.push_back(float(lmU) / 65535.f);
        attrs.push_back(float(lmV) / 65535.f);
    }
    return attrs;
}
} // namespace

size_t generateMissingModelLods(Model& model) {
    auto& info = model.autoLod;
    if (info.attempted) return info.generatedLevels;
    info.attempted = true;
    if (!model.batching.autoLod) {
        info.reason = "asset policy disables automatic LOD";
        return 0;
    }
    // Procedural roads, terrain and streamed cluster models have their own
    // rebuild/culling policies. Only importer-backed assets get this fallback.
    if (!isNativeModelFormat(model.path) || model.geometry) {
        info.reason = "not an imported mesh asset";
        return 0;
    }
    if (model.vb.valid() || model.ib.valid() || model.vertices.empty() ||
        model.indices.empty() || model.meshes.empty()) {
        info.reason = "missing CPU geometry or already uploaded";
        return 0;
    }
    if (model.vertices.size() > kMaxVertices || model.indices.size() > kMaxIndices ||
        model.meshes.size() > 10000) {
        info.reason = "asset exceeds bounded runtime simplification budget; precompute LOD offline";
        return 0;
    }

    // Keep animated rigid nodes (cars: wheels, doors, hoods) unchanged. Only
    // simplify their own mesh indices: all node IDs, mesh ownership, vertex
    // attributes and clip channels remain the same. Skinned and morphed
    // assets are better precomputed offline with deformation-aware metrics.
    if (!model.skins.empty()) {
        info.reason = "skinned asset requires authored deformation-aware LOD";
        return 0;
    }
    for (const auto& v : model.vertices)
        if (!std::isfinite(v.pos.x) || !std::isfinite(v.pos.y) ||
            !std::isfinite(v.pos.z)) {
            info.reason = "non-finite vertex position";
            return 0;
        }
    for (uint32_t i : model.indices) if (i >= model.vertices.size()) {
        info.reason = "index outside vertex buffer";
        return 0;
    }

    const size_t sourceMeshes = model.meshes.size();
    std::vector<uint8_t> used(sourceMeshes, 0);
    for (const Node& node : model.nodes)
        if (node.mesh >= 0 && size_t(node.mesh) < used.size() && node.skin < 0)
            used[size_t(node.mesh)] = 1;

    bool hasCandidate = false;
    for (size_t mi = 0; mi < sourceMeshes; ++mi)
        if (used[mi] && eligible(model, model.meshes[mi])) {
            const auto& mesh = model.meshes[mi];
            size_t count = 0;
            for (const auto& p : mesh.primitives) count += p.indexCount;
            hasCandidate |= count >= kMinPrimitiveIndices;
        }
    if (!hasCandidate) {
        info.reason = "no eligible rigid mesh missing an imported LOD chain";
        return 0;
    }

    const std::vector<float> attrs = vertexAttributes(model);
    if (std::any_of(attrs.begin(), attrs.end(), [](float v) { return !std::isfinite(v); })) {
        info.reason = "non-finite vertex attributes";
        return 0;
    }
    // All generated LODs reuse original ModelVertex indices, preserving GPU
    // texture layout and attributes, and have one primitive per source
    // material. Each node retains its original mesh index; only mesh.lods
    // gains references to independently simplified index streams.
    model.meshes.reserve(sourceMeshes * 4);
    for (size_t mi = 0; mi < sourceMeshes; ++mi) {
        const MeshDef original = model.meshes[mi];
        if (!used[mi] || !eligible(model, original)) continue;
        size_t baseIndices = 0;
        for (const Primitive& p : original.primitives) baseIndices += p.indexCount;
        if (baseIndices < kMinPrimitiveIndices) continue;

        const size_t levelsBefore = info.generatedLevels;
        size_t previousIndices = baseIndices;
        float previousPixels = std::numeric_limits<float>::max();
        for (size_t level = 0; level < kRatios.size(); ++level) {
            MeshDef coarse = original;
            coarse.name = original.name + "-auto-lod" + std::to_string(level + 1);
            coarse.lods.clear();
            coarse.materialBatches.clear();
            coarse.shadowBatch = {};
            std::vector<uint32_t> appended;
            float maximumError = 0.f;
            size_t total = 0;
            for (size_t pi = 0; pi < original.primitives.size(); ++pi) {
                const Primitive& source = original.primitives[pi];
                Primitive& output = coarse.primitives[pi];
                if (source.indexCount >= kMinPrimitiveIndices) {
                    std::vector<uint32_t> result(source.indexCount);
                    const size_t target = std::max<size_t>(
                        3, size_t(float(source.indexCount) * kRatios[level]) / 3 * 3);
                    float error = 0.f;
                    const size_t reduced = meshopt_simplifyWithAttributes(
                        result.data(),
                        model.indices.data() + source.firstIndex,
                        source.indexCount,
                        &model.vertices[0].pos.x,
                        model.vertices.size(), sizeof(ModelVertex),
                        attrs.data(), 15 * sizeof(float),
                        kAttributeWeights, 15, nullptr,
                        target, kError[level], meshopt_SimplifyLockBorder, &error);
                    if (reduced >= 3 && reduced % 3 == 0 &&
                        reduced < size_t(double(source.indexCount) * 0.8)) {
                        if (appended.size() > std::numeric_limits<uint32_t>::max() -
                                                  model.indices.size() ||
                            reduced > std::numeric_limits<uint32_t>::max() -
                                      model.indices.size() - appended.size()) {
                            info.reason = "generated index stream exceeds uint32 limit";
                            return info.generatedLevels;
                        }
                        output.firstIndex = uint32_t(model.indices.size() + appended.size());
                        output.indexCount = uint32_t(reduced);
                        appended.insert(appended.end(), result.begin(), result.begin() + reduced);
                        maximumError = std::max(maximumError, error);
                    }
                }
                total += output.indexCount;
            }
            if (total >= size_t(double(previousIndices) * 0.8)) continue;
            // Author LOD0 is untouched. Thresholds decrease strictly and
            // are bounded by meshoptimizer error so they never switch
            // before the requested quality level is reasonably small.
            const float pixels = std::min({
                256.f / float(1u << level),
                .5f / std::max(maximumError, 1e-9f),
                previousPixels * .5f
            });
            if (!std::isfinite(pixels) || pixels <= 0) continue;
            const int lodMesh = int(model.meshes.size());
            model.meshes[mi].lods.push_back({lodMesh, pixels});
            model.meshes.push_back(std::move(coarse));
            model.indices.insert(model.indices.end(), appended.begin(), appended.end());
            ++info.generatedLevels;
            info.addedIndices += appended.size();
            previousIndices = total;
            previousPixels = pixels;
        }
        if (info.generatedLevels > levelsBefore) ++info.generatedMeshes;
    }
    if (info.generatedLevels) {
        // Only new primitive bounds and node hierarchy are recalculated.
        // Original node transforms, clips and vertex data stay untouched.
        model.finalize();
        info.reason = "generated from imported rigid meshes before GPU upload";
    } else info.reason = "simplifier could not reach a useful 20% triangle saving";
    return info.generatedLevels;
}
} // namespace aiverse::render
