// CPU-only Strata import regression. No GPU/renderer ownership.
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "engine/render/StrataModelAdapter.h"
#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

using namespace aiverse::render;
using namespace aiverse::render::strata;

int main() {
    Model model;
    model.materials.resize(2);
    model.materials[1].alpha = AlphaMode::Mask;
    constexpr uint32_t side = 17;
    for (uint32_t z = 0; z < side; ++z)
        for (uint32_t x = 0; x < side; ++x) {
            ModelVertex v;
            v.pos = {float(x), float(z % 2) * .01f, float(z)};
            v.nrm = {0, 1, 0};
            v.uv = {float(x) / (side - 1), float(z) / (side - 1)};
            model.vertices.push_back(v);
        }
    for (uint32_t z = 0; z + 1 < side; ++z)
        for (uint32_t x = 0; x + 1 < side; ++x) {
            const uint32_t a = z * side + x;
            model.indices.insert(model.indices.end(), {
                a, a + 1, a + side,
                a + 1, a + side + 1, a + side
            });
        }
    const size_t indexCount = model.indices.size();
    MeshDef mesh;
    Primitive opaque;
    opaque.firstIndex = 0;
    opaque.indexCount = uint32_t(indexCount);
    opaque.material = 0;
    mesh.primitives.push_back(opaque);
    Primitive masked = opaque;
    masked.material = 1;
    mesh.primitives.push_back(masked);
    model.meshes.push_back(mesh);
    const std::vector<uint32_t> original = model.indices;
    const size_t originalVertices = model.vertices.size();
    ModelAsset asset;
    std::string error;
    assert(buildModelAsset(model, {}, asset, &error));
    assert(error.empty());
    assert(asset.primitives.size() == 1);
    assert(asset.skipped.size() == 1);
    assert(asset.skipped.front().reason == "non-opaque or custom shader material");
    const auto& partition = asset.primitives.front();
    assert(partition.mesh == 0 && partition.primitive == 0 && partition.material == 0);
    assert(validate(partition.geometry.nodes, partition.geometry.roots));
    assert(!partition.geometry.meshlets.empty());
    assert(model.indices == original && model.vertices.size() == originalVertices);

    // Imported author LODs take precedence over Strata until the renderer
    // supports single-writer LOD routing.
    auto originalAsset = asset;
    model.meshes[0].lods.push_back({0, 120.f});
    assert(buildModelAsset(model, {}, asset, &error));
    assert(asset.primitives.empty() && asset.skipped.size() == 2);
    model.meshes[0].lods.clear();

    // Invalid eligible primitives must fail transactionally without changing
    // the previously generated CPU asset.
    model.meshes[0].primitives[0].indexCount = UINT32_MAX;
    assert(buildModelAsset(model, {}, asset, &error));
    assert(asset.primitives.empty() && asset.skipped.size() == 2);
    assert(asset.skipped.front().reason == "invalid primitive index range");
    model.meshes[0].primitives[0] = opaque;
    const auto prior = originalAsset;
    asset = originalAsset;
    model.indices[5] = UINT32_MAX;
    assert(!buildModelAsset(model, {}, asset, &error));
    assert(!error.empty());
    assert(asset.primitives.size() == prior.primitives.size());
    assert(asset.primitives.front().geometry.indices ==
           prior.primitives.front().geometry.indices);
    model.indices = original;

    // A skinned or animated model may not create a competing render owner.
    model.clips.emplace_back();
    assert(buildModelAsset(model, {}, asset, &error));
    assert(asset.primitives.empty());
    model.clips.clear();
    assert(buildModelAsset(model, {}, asset, &error));
    assert(asset.primitives.size() == 1);

    // Scene staging is keyed by immutable source model ID: it must not build
    // separate representations for each repeated GeometryScene instance.
    SceneStaging staging;
    assert(staging.find("shared/model.glb") == nullptr);
    assert(staging.stage("shared/model.glb", model));
    assert(staging.find("shared/model.glb") != nullptr);
    assert(staging.stats().stagedSources == 1);
    assert(staging.stats().stagedPrimitives == 1);
    assert(staging.stats().skippedPrimitives == 1);
    assert(staging.stats().clusterNodes > 0);
    assert(staging.stats().meshlets > 0);
    const auto beforeStats = staging.stats();
    const std::vector<uint32_t> stagedIndices =
        staging.find("shared/model.glb")->primitives.front().geometry.indices;
    assert(staging.stage("shared/model.glb", model));
    assert(staging.stats().stagedSources == beforeStats.stagedSources);
    assert(staging.stats().clusterNodes == beforeStats.clusterNodes);
    assert(staging.find("shared/model.glb")->primitives.front().geometry.indices ==
           stagedIndices);

    // Staging owns its CPU representation independent of subsequent source
    // changes, and a failed additional source cannot destroy earlier assets.
    model.indices[0] = UINT32_MAX;
    assert(!staging.stage("broken/model.glb", model));
    assert(staging.stats().failedSources == 1);
    assert(!staging.warnings().empty());
    assert(staging.find("shared/model.glb")->primitives.front().geometry.indices ==
           stagedIndices);
    model.indices = original;
    SceneStaging bounded;
    bounded.setLimits({4, 6});
    assert(!bounded.stage("oversized.glb", model));
    assert(bounded.stats().skippedSources == 1);
    assert(bounded.find("oversized.glb") == nullptr);
    assert(!bounded.warnings().empty());

    staging.clear();
    assert(staging.find("shared/model.glb") == nullptr);
    assert(staging.stats().stagedSources == 0);
    assert(staging.stats().failedSources == 0);
    assert(staging.warnings().empty());
    assert(staging.stage("shared/model.glb", model));
    // The owning GeometryScene invokes this only after the disk archive
    // fingerprint was checked and root GPU pages were published.
    const auto* staged=staging.find("shared/model.glb");
    assert(staged && staged->primitives.size()==1);
    const auto bytes=staged->primitives[0].geometry.indices.size()*sizeof(uint32_t);
    const auto count=staged->primitives[0].geometry.logicalIndexCount();
    assert(staging.discardArchivedCpuIndices("shared/model.glb")==bytes);
    staged=staging.find("shared/model.glb");
    assert(staged->primitives[0].geometry.indices.empty());
    assert(staged->primitives[0].geometry.logicalIndexCount()==count);
    assert(staging.discardArchivedCpuIndices("shared/model.glb")==0);
    assert(staging.discardArchivedCpuIndices("unrelated")==0);
}
