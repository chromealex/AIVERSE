// Optional TerraWeave forest visualization in the existing AIVERSE editor Scene.
// No gameplay ECS mutations; generated vegetation is an editor-only overlay.
#pragma once

#include "apps/editor/core/MapDoc.h"
#include "apps/editor/core/TerraWeaveOps.h"
#include "apps/editor/TerraWeavePlacementMask.h"
#include "engine/render/TerraWeaveForestAdapter.h"
#include "engine/world/TerraWeaveBotanicalAppearance.h"
#include "engine/render/TerraWeaveBotanicalForest.h"
#include "engine/render/TerraWeaveHorizonScene.h"
#include <fstream>
#include <iterator>
#include <unordered_map>
#include <utility>
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
    // GPU hierarchical wind masks (trunk -> branch -> twig -> leaf) on near
    // trees. Takes effect on the next prototype build (release() first).
    bool hierarchicalWind() const { return hierarchicalWind_; }
    void setHierarchicalWind(bool on) { hierarchicalWind_ = on; }
    // Botanical catalog (oak, birch, maple, spruce, pine) with the PBR bark and
    // leaf maps of engine/assets/materials/vegetation/textures, else the two
    // classic untextured prototypes. Takes effect after release().
    bool botanical() const { return botanical_; }
    void setBotanical(bool on) { botanical_ = on; }
    // Distance multiplier for near-model LOD ranges (beauty captures); 1 = default.
    float lodScale() const { return lodScale_; }
    void setLodScale(float scale) { lodScale_ = std::clamp(scale, 0.25f, 4.f); }
    // Near-tree geometry (merged whole-tree tiers vs one node per branch).
    terraweave::ArborTreeGeometry treeGeometry() const { return treeGeometry_; }
    void setTreeGeometry(terraweave::ArborTreeGeometry geometry) { treeGeometry_ = geometry; }
    void setEngineDir(std::string dir) { engineDir_ = std::move(dir); }
    size_t visibleInstances() const { return visibleCount_; }
    size_t generatedInstances() const { return placements_.size(); }

    void release(gfx::Device& device) {
        if (horizon_) horizon_->shutdown(device);
        horizon_.reset();
        horizonRecipe_.clear();
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
            if (hierarchicalWind_) forest_->enableHierarchicalWind();
            forest_->setTreeGeometry(treeGeometry_);
            if (botanical_) addBotanicalPrototypes();
            else addClassicPrototypes();
        }
        if (!forest_->uploadModels(device))
            throw std::runtime_error("TerraWeave forest GPU upload failed");

        constexpr int64_t kPreviewTileCm = 3200;
        // Horizon mode: individual trees (near merged models, then per-tree
        // impostors) out to 250 m, TerraWeave Horizon cluster cards beyond.
        const int kRadiusInTiles = horizonEnabled_ ? 8 : 2;
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
            cachedTileZ_ != tz || cachedRecipe_ != recipeKey || cachedRadius_ != kRadiusInTiles) {
            cachedRadius_ = kRadiusInTiles;
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
            std::vector<terraweave::FoliageSpecies> species;
            for (const auto& [id, weight] : speciesWeights_) {
                terraweave::FoliageSpecies f;
                f.id = id;
                f.biomeMask = forestBiomes;
                f.weight = weight;
                f.maxSlopePermille = 1250;
                species.push_back(f);
            }
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
        view.highRangeCm = int32_t(2100 * lodScale_);
        view.mediumRangeCm = int32_t(3800 * lodScale_);
        view.lowRangeCm = int32_t(5600 * lodScale_);
        view.cullRangeCm = horizonEnabled_ ? std::max(int32_t(7700 * lodScale_), kHorizonCloseCm)
                                           : int32_t(7700 * lodScale_);
        view.farDensityPermille = 1000;
        const auto batches = terraweave::buildFloraBatches(placements_, view);
        for (const auto& batch : batches) visibleCount_ += batch.instances.size();
        // Existing Renderer3D takes care of RHI, material sorting,
        // per-branch LOD and standard instance grouping.
        forest_->submit(renderer, batches, {});

        if (horizonEnabled_) {
            // Kilometre-scale forest HLOD: cluster cards in ONE model, from the
            // same TerraWeave seeds, so the forest continues to the horizon.
            if (!horizon_ || horizonRecipe_ != recipeKey) {
                if (horizon_) horizon_->shutdown(device);
                terraweave::HorizonSettings hs;
                hs.closeForestEndCm = kHorizonCloseCm;
                // Map ground covers the camera neighbourhood; far proxy ground
                // starts where the authored map usually ends.
                hs.terrainBeginCm = std::clamp<int32_t>(int32_t(std::min(doc.data.w, doc.data.h)) * origin.cellSizeCm / 2,
                                                       kHorizonCloseCm, 110000);
                hs.crownDetail = 3;
                hs.orientationViews = 2;
                hs.atlasTilePixels = 64;
                horizon_ = std::make_unique<terraweave::TerraWeaveHorizonScene>(
                    terraweave_ops_detail::terrain(recipe), terraweave::BiomeSettings{}, hs);
                horizonRecipe_ = recipeKey;
            }
            horizon_->update(device, {globalCameraXcm, globalCameraZcm});
            if (horizon_->ready())
                horizon_->submit(renderer, {origin.worldOriginXcm, 0, origin.worldOriginZcm});
        }
    }
    bool horizon() const { return horizonEnabled_; }
    void setHorizon(bool on) { horizonEnabled_ = on; }

private:
    void addClassicPrototypes() {
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
        speciesWeights_ = {{1001, 3}, {1002, 2}};
    }

    void addBotanicalPrototypes() {
        static constexpr struct { size_t slot; uint32_t weight; } kPick[] = {
            {4, 3}, {5, 2}, {6, 2}, {0, 2}, {1, 2}};   // oak, birch, maple, spruce, pine
        const auto& presets = terraweave::botanicalTreePresets();
        // Shared Terra recipe (also used by the WorldScene manifest).
        terraweave::BotanicalMaterialTable table;
        table.dir = engineDir_.empty() ? std::string() : engineDir_ + "/engine/assets/materials/vegetation/textures";
        table.read = [](const std::string& path, std::string& bytes) {
            std::ifstream f(path, std::ios::binary);
            if (!f) return false;
            bytes.assign((std::istreambuf_iterator<char>(f)), {});
            return true;
        };
        for (const auto& pick : kPick) table.add(presets[pick.slot].profile.speciesId, presets[pick.slot]);
        if (!engineDir_.empty()) forest_->setPrototypeDecorator(table.decorator());
        speciesWeights_.clear();
        for (const auto& pick : kPick) {
            const auto& preset = presets[pick.slot];
            auto mesh = terraweave::botanicalArborMeshSettings(preset);
            forest_->addPrototype(preset.profile, 0x54574254ULL + preset.profile.speciesId, mesh);
            speciesWeights_.push_back({preset.profile.speciesId, pick.weight});
        }
    }
    bool enabled_ = false;
    bool hierarchicalWind_ = true;
    bool botanical_ = true;
    float lodScale_ = 1;
    static constexpr int32_t kHorizonCloseCm = 25000;
    bool horizonEnabled_ = false;
    int cachedRadius_ = 0;
    std::string horizonRecipe_;
    std::unique_ptr<terraweave::TerraWeaveHorizonScene> horizon_;
    terraweave::ArborTreeGeometry treeGeometry_ = terraweave::ArborTreeGeometry::Auto;
    std::string engineDir_;
    std::vector<std::pair<uint32_t, uint32_t>> speciesWeights_;
    uint64_t cachedRevision_ = std::numeric_limits<uint64_t>::max();
    int64_t cachedTileX_ = std::numeric_limits<int64_t>::max();
    int64_t cachedTileZ_ = std::numeric_limits<int64_t>::max();
    std::string cachedRecipe_;
    size_t visibleCount_ = 0;
    std::vector<terraweave::FoliageInstance> placements_;
    std::unique_ptr<terraweave::ArborForestAdapter> forest_;
};

} // namespace aiverse::editor
