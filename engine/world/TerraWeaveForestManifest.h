// Optional "terraweaveForest" WorldScene manifest adapter.
// Keeps game-specific JSON outside reusable TerraWeave generation/render code.
#pragma once

#include "engine/world/WorldAssets.h"
#include "engine/world/TerraWeaveForestScene.h"
#include <cstdint>
#include <memory>
#include <string>

namespace aiverse::world {

inline std::unique_ptr<terraweave::TerraWeaveForestScene>
forestSceneFromManifest(const Json& manifest) {
    if (!manifest.has("terraweaveForest")) return {};
    const Json& j = manifest["terraweaveForest"];
    if (!j.isObject())
        throw JsonError("world.terraweaveForest: expected object");
    terraweave::ForestSceneSettings settings;
    settings.terrain.seed = static_cast<uint64_t>(worldInteger(
        j, "terrainSeed", 1, 0, INT32_MAX));
    settings.terrain.baseHeightCm = static_cast<int32_t>(worldInteger(
        j, "baseHeightCm", 0, -10000000, 10000000));
    settings.terrain.reliefCm = static_cast<int32_t>(worldInteger(
        j, "reliefCm", 6000, 0, 10000000));
    settings.terrain.basePeriodCm = static_cast<int32_t>(worldInteger(
        j, "basePeriodCm", 6400, 1, 10000000));
    settings.terrain.octaves = static_cast<uint32_t>(worldInteger(
        j, "octaves", 5, 1, 12));
    settings.biomes.seed = static_cast<uint64_t>(worldInteger(
        j, "biomeSeed", 2, 0, INT32_MAX));
    settings.biomes.seaLevelCm = static_cast<int32_t>(worldInteger(
        j, "seaLevelCm", -2500, -10000000, 10000000));
    settings.scatter.seed = static_cast<uint64_t>(worldInteger(
        j, "scatterSeed", 3, 0, INT32_MAX));
    settings.scatter.spacingCm = static_cast<int32_t>(worldInteger(
        j, "spacingCm", 450, 1, 100000));
    settings.scatter.densityPermille = static_cast<uint32_t>(worldInteger(
        j, "densityPermille", 850, 0, 1000));
    settings.chunkSizeCm = static_cast<int32_t>(worldInteger(
        j, "chunkSizeCm", 6400, 100, 100000));
    settings.radiusChunks = static_cast<uint32_t>(worldInteger(
        j, "radiusChunks", 4, 0, 8));
    settings.maxResidentChunks = static_cast<uint32_t>(worldInteger(
        j, "maxResidentChunks", 64, 1, 289));
    // WorldScene uses the existing asynchronous chunk scheduler by default;
    // native consumers constructing ForestSceneSettings directly retain the
    // previous synchronous behaviour unless they explicitly opt in.
    settings.asynchronous = j.getBool("asynchronous", true);
    settings.maxPendingJobs = static_cast<uint32_t>(worldInteger(
        j, "maxPendingJobs", 3, 1, 8));
    settings.requestsPerUpdate = static_cast<uint32_t>(worldInteger(
        j, "requestsPerUpdate", 2, 1, 8));
    settings.publicationsPerUpdate = static_cast<uint32_t>(worldInteger(
        j, "publicationsPerUpdate", 2, 1, 8));
    settings.view.highRangeCm = static_cast<int32_t>(worldInteger(
        j, "highRangeCm", 3000, 0, 100000000));
    settings.view.mediumRangeCm = static_cast<int32_t>(worldInteger(
        j, "mediumRangeCm", 9000, 0, 100000000));
    settings.view.lowRangeCm = static_cast<int32_t>(worldInteger(
        j, "lowRangeCm", 12000, 0, 100000000));
    settings.view.cullRangeCm = static_cast<int32_t>(worldInteger(
        j, "cullRangeCm", 24000, 0, 100000000));
    settings.view.farDensityPermille = static_cast<uint32_t>(worldInteger(
        j, "farDensityPermille", 650, 0, 1000));
    settings.view.impostorBlendWidthCm = static_cast<int32_t>(worldInteger(
        j, "impostorBlendWidthCm", 3000, 0, 100000000));

    auto scene = std::make_unique<terraweave::TerraWeaveForestScene>(settings);
    if (!j.has("species") || !j["species"].isArray() ||
        j["species"].size() == 0 || j["species"].size() > 64)
        throw JsonError("world.terraweaveForest.species: expected 1..64 entries");
    for (const auto& def : j["species"].items()) {
        if (!def.isObject())
            throw JsonError("world.terraweaveForest.species: expected object");
        terraweave::TreeProfile profile;
        profile.speciesId = static_cast<uint32_t>(worldInteger(
            def, "id", 0, 1, INT32_MAX));
        profile.minHeightCm = static_cast<int32_t>(worldInteger(
            def, "minHeightCm", 800, 100, 100000));
        profile.maxHeightCm = static_cast<int32_t>(worldInteger(
            def, "maxHeightCm", 1500, 100, 100000));
        profile.trunkRadiusCm = static_cast<int32_t>(worldInteger(
            def, "trunkRadiusCm", 24, 1, 10000));
        profile.primaryBranches = static_cast<uint32_t>(worldInteger(
            def, "branches", 22, 0, 256));
        profile.leafRadiusCm = static_cast<uint32_t>(worldInteger(
            def, "leafRadiusCm", 95, 0, 100000));
        const std::string shape = def.getStr("shape", "broadleaf");
        if (shape == "broadleaf") profile.shape = terraweave::CrownShape::Broadleaf;
        else if (shape == "conifer") profile.shape = terraweave::CrownShape::Conifer;
        else if (shape == "columnar") profile.shape = terraweave::CrownShape::Columnar;
        else throw JsonError("world.terraweaveForest.species: invalid shape");
        terraweave::FoliageSpecies placement;
        placement.id = profile.speciesId;
        placement.weight = static_cast<uint32_t>(worldInteger(
            def, "weight", 1, 1, 100000));
        placement.biomeMask = static_cast<uint32_t>(worldInteger(
            def, "biomeMask", terraweave::kLandBiomes, 0, 127));
        placement.maxSlopePermille = static_cast<int32_t>(worldInteger(
            def, "maxSlopePermille", 800, 0, 100000));
        const uint64_t prototypeSeed = static_cast<uint64_t>(worldInteger(
            def, "prototypeSeed", profile.speciesId, 0, INT32_MAX));
        scene->addSpecies(profile, placement, prototypeSeed);
    }
    if (j.has("exclusions")) {
        const Json& exclusions = j["exclusions"];
        if (!exclusions.isArray() || exclusions.size() > 4096)
            throw JsonError("world.terraweaveForest.exclusions: invalid");
        std::vector<terraweave::RectCm> rectangles;
        for (const auto& e : exclusions.items()) {
            if (!e.isArray() || e.size() != 4)
                throw JsonError("world.terraweaveForest.exclusions: expected [minX,minZ,maxX,maxZ]");
            terraweave::RectCm r;
            int64_t* parts[] = {&r.minX, &r.minZ, &r.maxX, &r.maxZ};
            for (size_t i = 0; i < 4; ++i) {
                if (!e[i].isNumber() ||
                    e[i].fx().raw % Fixed::kOne != 0 ||
                    e[i].integer() < -terraweave::kWorldLimitCm ||
                    e[i].integer() > terraweave::kWorldLimitCm)
                    throw JsonError("world.terraweaveForest.exclusions: invalid centimetre coordinate");
                *parts[i] = e[i].integer();
            }
            rectangles.push_back(r);
        }
        scene->setExclusions(std::move(rectangles));
    }
    return scene;
}

} // namespace aiverse::world
