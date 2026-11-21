#include "AutoLodRuntime.h"
#include "Model.h"
#include "meshoptimizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <unordered_set>
#include <vector>

namespace aiverse::render {
namespace {
constexpr size_t kMinPrimitiveIndices = 384; // 128 triangles
constexpr size_t kMaxVertices = 500000; // bound import latency + temporary memory
constexpr size_t kMaxIndices = 1500000;
constexpr std::array<float, 3> kRatios{0.50f, 0.25f, 0.125f};
constexpr std::array<float, 3> kError{0.005f, 0.01f, 0.02f};
constexpr std::array<float, 27> kAttributeWeights{
    .5f, .5f, .5f,  // normals
    1.f, 1.f,       // UV0
    .25f, .25f, .25f, .25f, // tangent incl. handedness
    1.f, 1.f, 1.f, 1.f, // RGBA
    1.f, 1.f,       // encoded lightmap UV1
    2.f, 2.f, 2.f, 2.f, 2.f, 2.f, // signed POSITION morph envelope
    .5f, .5f, .5f, .5f, .5f, .5f // signed NORMAL morph envelope
};

bool eligible(const Model& model, const MeshDef& mesh) {
    if (!mesh.lods.empty() || mesh.primitives.empty()) return false;
    const bool hasMorph=std::any_of(mesh.primitives.begin(),mesh.primitives.end(),
        [](const Primitive& p){return p.morphTargets>0;});
    // Deformation can also mean skinning; this first generator only
    // supports unskinned meshes with complete semantic morph identities.
    if(mesh.deformable && !hasMorph)return false;
    if(hasMorph) {
        if(mesh.morphTargetNames.empty() ||
           mesh.morphTargetNames.size()>size_t(kMaxMorphTargets))return false;
        std::unordered_set<std::string> names;
        for(const auto& name:mesh.morphTargetNames)
            if(name.empty() || !names.insert(name).second)return false;
    }
    for (const Primitive& p : mesh.primitives) {
        if (p.indexCount == 0 || p.material < 0 ||
            size_t(p.material) >= model.materials.size() ||
            p.indexCount % 3 || p.firstIndex > model.indices.size() ||
            p.indexCount > model.indices.size() - p.firstIndex ||
            !p.jointPalette.empty() || !p.variantMaterial.empty() ||
            p.morphAliasMesh>=0 || p.morphAliasPrimitive>=0)
            return false;
        if(hasMorph && (p.morphTargets!=int(mesh.morphTargetNames.size()) ||
           p.firstVertex>model.vertices.size() ||
           p.vertexCount>model.vertices.size()-p.firstVertex ||
           p.vertexCount==0 ||
           p.vertexCount>std::numeric_limits<size_t>::max()/6u/
               size_t(p.morphTargets) ||
           p.morphDeltas.size()!=size_t(p.morphTargets)*p.vertexCount*6u))
            return false;
        if(!hasMorph && p.morphTargets!=0)return false;
        const Material& material = model.materials[size_t(p.material)];
        // Opaque ordinary materials only. Masked foliage, custom vertex
        // displacement and transparent glass require author-controlled LOD.
        if (material.alpha != AlphaMode::Opaque || material.transparent ||
            material.shaderGraph)
            return false;
    }
    return true;
}

// The source GPU morph layout is unchanged. Attribute-aware simplification
// additionally protects the worst signed displacement over ALL channels,
// rather than silently considering only the first handful of expressions.
// Position + normal min/max occupy 12 of meshoptimizer's <=32 attributes.
std::vector<float> vertexAttributes(const Model& model) {
    std::vector<float> attrs;
    attrs.reserve(model.vertices.size() * kAttributeWeights.size());
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
        attrs.insert(attrs.end(),12u,0.f);
    }
    // Add envelopes only for source primitives eligible for this generator.
    // A malformed morph source cannot create an apparently valid LOD.
    for(const auto& mesh:model.meshes) {
        if(!eligible(model,mesh))continue;
        for(const auto& p:mesh.primitives) {
            if(p.morphTargets==0)continue;
            const size_t count=p.vertexCount;
            for(size_t t=0;t<size_t(p.morphTargets);++t)
                for(size_t v=0;v<count;++v) {
                    const size_t dst=(size_t(p.firstVertex)+v)*27u;
                    const size_t src=(t*count+v)*6u;
                    for(size_t axis=0;axis<6u;++axis) {
                        const float delta=p.morphDeltas[src+axis];
                        const size_t low=axis<3?15u+axis:21u+axis-3u;
                        const size_t high=axis<3?18u+axis:24u+axis-3u;
                        attrs[dst+low]+=std::min(0.f,delta);
                        attrs[dst+high]+=std::max(0.f,delta);
                    }
                }
        }
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

    // Preserve animated nodes and the exact original vertex/morph buffer.
    // Mesh simplification is index-only: reductions save triangle work, not
    // morph texture VRAM. Skinned meshes still require authored LODs.
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
        info.reason = "no eligible rigid/morph mesh missing an imported LOD chain";
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
        // Capacity is reserved for source plus 3 levels each: references
        // remain stable while coarse meshes are appended. Avoid copying the
        // potentially huge 64-channel CPU morph arrays at every LOD level.
        const MeshDef& original = model.meshes[mi];
        if (!used[mi] || !eligible(model, original)) continue;
        size_t baseIndices = 0;
        for (const Primitive& p : original.primitives) baseIndices += p.indexCount;
        if (baseIndices < kMinPrimitiveIndices) continue;

        const size_t levelsBefore = info.generatedLevels;
        size_t previousIndices = baseIndices;
        float previousPixels = std::numeric_limits<float>::max();
        for (size_t level = 0; level < kRatios.size(); ++level) {
            MeshDef coarse;
            coarse.name = original.name + "-auto-lod" + std::to_string(level + 1);
            coarse.weights = original.weights;
            coarse.morphTargetNames = original.morphTargetNames;
            coarse.primitives.reserve(original.primitives.size());
            // Copy metadata only. Never duplicate large authored 52/64
            // channel CPU morph arrays just to generate an index-only LOD.
            for(size_t pi=0;pi<original.primitives.size();++pi) {
                const Primitive& src=original.primitives[pi];
                Primitive dst;
                dst.firstIndex=src.firstIndex;
                dst.indexCount=src.indexCount;
                dst.material=src.material;
                dst.jointPalette=src.jointPalette;
                dst.variantMaterial=src.variantMaterial;
                dst.boundsMin=src.boundsMin;
                dst.boundsMax=src.boundsMax;
                dst.firstVertex=src.firstVertex;
                dst.vertexCount=src.vertexCount;
                dst.morphTargets=src.morphTargets;
                dst.morphTexel=src.morphTexel;
                dst.lightmapUv=src.lightmapUv;
                dst.shadowBatched=false;
                if(src.morphTargets>0) {
                    dst.morphAliasMesh=int(mi);
                    dst.morphAliasPrimitive=int(pi);
                }
                coarse.primitives.push_back(std::move(dst));
            }
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
                        attrs.data(), 27 * sizeof(float),
                        kAttributeWeights.data(), 27, nullptr,
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
            ModelLod lod{lodMesh,pixels};
            if(!original.morphTargetNames.empty()) {
                lod.morphSourceForTarget.resize(original.morphTargetNames.size());
                std::iota(lod.morphSourceForTarget.begin(),
                          lod.morphSourceForTarget.end(),0);
            }
            model.meshes[mi].lods.push_back(std::move(lod));
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
        info.reason = "generated index-only rigid/morph LODs before GPU upload";
    } else info.reason = "simplifier could not reach a useful 20% triangle saving";
    return info.generatedLevels;
}
} // namespace aiverse::render
