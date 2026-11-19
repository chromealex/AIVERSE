// Optional TerraWeave forest visualization in the existing AIVERSE editor Scene.
// No gameplay ECS mutations; generated vegetation is an editor-only overlay.
#pragma once

#include "apps/editor/core/MapDoc.h"
#include "apps/editor/core/TerraWeaveOps.h"
#include "apps/editor/TerraWeavePlacementMask.h"
#include "engine/render/TerraWeaveForestAdapter.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::editor {

class TerraWeaveForestPreview {
public:
    bool enabled() const { return enabled_; }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    size_t visibleInstances() const { return visibleCount_; }
    size_t generatedInstances() const { return placements_.size(); }

    void release(gfx::Device& device) {
        if (forest_) forest_->releaseModels(device);
        forest_.reset();
        placements_.clear();
        cachedRecipe_.clear();
        cachedRevision_ = std::numeric_limits<uint64_t>::max();
        cachedTileX_ = cachedTileZ_ = std::numeric_limits<int64_t>::max();
        visibleCount_ = 0;
    }

    // Call between Renderer3D::beginScene and Renderer3D::render.
    // sampleHeightCm: editor's CURRENT displayed terrain elevation, not the
    // original procedural noise: preserves alignment after sculpt and Undo.
    void draw(gfx::Device& device, render::Renderer3D& renderer,
              const MapDoc& doc, int64_t cameraXcm, int64_t cameraZcm,
              const std::function<int32_t(int64_t, int64_t)>& sampleHeightCm) {
        visibleCount_ = 0;
        if (!enabled_ || doc.data.planet() || doc.data.fps.isObject()) return;
        const Json& recipe = doc.data.extra["terraweave"];
        if (!recipe.isObject() || recipe.getInt("version", 0) != 1) return;
        if (!sampleHeightCm) throw std::invalid_argument("TerraWeave preview missing height sampler");
        if (cameraXcm < -terraweave::kWorldLimitCm || cameraXcm > terraweave::kWorldLimitCm ||
            cameraZcm < -terraweave::kWorldLimitCm || cameraZcm > terraweave::kWorldLimitCm)
            throw std::out_of_range("TerraWeave preview camera extent");
        if (doc.data.w <= 0 || doc.data.h <= 0) return;

        if (!forest_) {
            forest_ = std::make_unique<terraweave::ArborForestAdapter>();
            terraweave::TreeProfile broadleaf;
            broadleaf.speciesId = 1001;
            broadleaf.minHeightCm = 550;
            broadleaf.maxHeightCm = 1150;
            broadleaf.primaryBranches = 12;
            broadleaf.shape = terraweave::CrownShape::Broadleaf;
            terraweave::TreeProfile conifer;
            conifer.speciesId = 1002;
            conifer.minHeightCm = 900;
            conifer.maxHeightCm = 1750;
            conifer.trunkRadiusCm = 19;
            conifer.primaryBranches = 14;
            conifer.leafRadiusCm = 55;
            conifer.shape = terraweave::CrownShape::Conifer;
            forest_->addPrototype(broadleaf, 0x544552524131ULL);
            forest_->addPrototype(conifer, 0x544552524132ULL);
        }
        if (!forest_->uploadModels(device))
            throw std::runtime_error("TerraWeave forest GPU upload failed");

        constexpr int64_t kPreviewTileCm = 3200;
        constexpr int kRadiusInTiles = 2;
        // Camera/render coordinates are map-local; generator coordinates are
        // world-absolute and may have a nonzero origin in the saved recipe.
        const auto origin = terraweave_ops_detail::origin(recipe);
        // cameraXcm/Zcm are map-local render units (100 cm per cell).
        // A recipe may sample each authored corner at a different world spacing.
        const int64_t globalCameraXcm = terraWeaveWorldFromEditorCm(
            cameraXcm, origin.worldOriginXcm, origin.cellSizeCm);
        const int64_t globalCameraZcm = terraWeaveWorldFromEditorCm(
            cameraZcm, origin.worldOriginZcm, origin.cellSizeCm);
        if (globalCameraXcm < -terraweave::kWorldLimitCm ||
            globalCameraXcm > terraweave::kWorldLimitCm ||
            globalCameraZcm < -terraweave::kWorldLimitCm ||
            globalCameraZcm > terraweave::kWorldLimitCm)
            throw std::out_of_range("TerraWeave preview absolute camera extent");
        const int64_t tx = terraweave::detail::floorDiv(globalCameraXcm, kPreviewTileCm);
        const int64_t tz = terraweave::detail::floorDiv(globalCameraZcm, kPreviewTileCm);
        const std::string recipeKey = recipe.dump();
        if (cachedRevision_ != doc.revision || cachedTileX_ != tx ||
            cachedTileZ_ != tz || cachedRecipe_ != recipeKey) {
            placements_.clear();
            const auto t = terraweave_ops_detail::terrain(recipe);
            const auto b = terraweave::BiomeSettings{};
            // The existing map's cell size controls its generated extent.
            // Clamp each preview tile to the current 2D authored map bounds.
            const int64_t mapMinX = origin.worldOriginXcm;
            const int64_t mapMinZ = origin.worldOriginZcm;
            const int64_t mapMaxX = mapMinX + int64_t(doc.data.w) * origin.cellSizeCm;
            const int64_t mapMaxZ = mapMinZ + int64_t(doc.data.h) * origin.cellSizeCm;
            const uint32_t forestBiomes = terraweave::biomeBit(terraweave::Biome::Forest) |
                terraweave::biomeBit(terraweave::Biome::Grassland) |
                terraweave::biomeBit(terraweave::Biome::Wetland) |
                terraweave::biomeBit(terraweave::Biome::Tundra) |
                terraweave::biomeBit(terraweave::Biome::Alpine);
            terraweave::FoliageSpecies broadleaf;
            broadleaf.id = 1001;
            broadleaf.biomeMask = forestBiomes;
            broadleaf.weight = 3;
            broadleaf.maxSlopePermille = 1250;
            terraweave::FoliageSpecies conifer = broadleaf;
            conifer.id = 1002;
            conifer.weight = 2;
            const std::vector<terraweave::FoliageSpecies> species{broadleaf, conifer};
            terraweave::ScatterSettings scatter;
            scatter.seed = t.seed ^ 0x5457464f52455354ULL;
            scatter.spacingCm = 1100;
            scatter.densityPermille = 780;
            // Deterministic world-lattice generation, independent of camera/tile order.
            for (int iz = -kRadiusInTiles; iz <= kRadiusInTiles; ++iz)
                for (int ix = -kRadiusInTiles; ix <= kRadiusInTiles; ++ix) {
                    const int64_t minX = std::max(mapMinX, (tx + ix) * kPreviewTileCm);
                    const int64_t maxX = std::min(mapMaxX, (tx + ix + 1) * kPreviewTileCm);
                    const int64_t minZ = std::max(mapMinZ, (tz + iz) * kPreviewTileCm);
                    const int64_t maxZ = std::min(mapMaxZ, (tz + iz + 1) * kPreviewTileCm);
                    if (minX >= maxX || minZ >= maxZ) continue;
                    auto items = terraweave::scatter(t, b, scatter,
                        {minX, minZ, maxX, maxZ}, species);
                    for (auto& item : items) {
                        // Authoritative/editor terrain may have been sculpted since
                        // the original seed. Sample the actual map for the overlay.
                        const int64_t mapXcm = terraWeaveEditorFromWorldCm(
                            item.xCm, origin.worldOriginXcm, origin.cellSizeCm);
                        const int64_t mapZcm = terraWeaveEditorFromWorldCm(
                            item.zCm, origin.worldOriginZcm, origin.cellSizeCm);
                        if (!terraWeavePreviewAllowsPlacement(doc.data, mapXcm,
                                                               mapZcm, 100))
                            continue;
                        item.heightCm = sampleHeightCm(mapXcm, mapZcm);
                        item.xCm = mapXcm;
                        item.zCm = mapZcm;
                        placements_.push_back(item);
                    }
                }
            cachedRevision_ = doc.revision;
            cachedTileX_ = tx;
            cachedTileZ_ = tz;
            cachedRecipe_ = recipeKey;
        }
        terraweave::FloraView view;
        view.cameraXcm = cameraXcm;
        view.cameraZcm = cameraZcm;
        view.highRangeCm = 2100;
        view.mediumRangeCm = 3800;
        view.lowRangeCm = 5600;
        view.cullRangeCm = 7700;
        view.farDensityPermille = 1000;
        const auto batches = terraweave::buildFloraBatches(placements_, view);
        for (const auto& batch : batches) visibleCount_ += batch.instances.size();
        // Existing Renderer3D takes care of RHI, material sorting,
        // per-branch LOD and standard instance grouping.
        forest_->submit(renderer, batches, {});
    }

private:
    bool enabled_ = false;
    uint64_t cachedRevision_ = std::numeric_limits<uint64_t>::max();
    int64_t cachedTileX_ = std::numeric_limits<int64_t>::max();
    int64_t cachedTileZ_ = std::numeric_limits<int64_t>::max();
    std::string cachedRecipe_;
    size_t visibleCount_ = 0;
    std::vector<terraweave::FoliageInstance> placements_;
    std::unique_ptr<terraweave::ArborForestAdapter> forest_;
};

} // namespace aiverse::editor
