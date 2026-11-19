// WorldScene opt-in configuration for TerraWeave Horizon HLOD.
// Keeps JSON parsing out of the reusable terrain/silhouette generator.
#pragma once
#include "engine/world/WorldAssets.h"
#include "engine/render/TerraWeaveHorizonScene.h"
#include "engine/world/TerraWeaveForestScene.h"
#include <memory>
#include <cstdint>
#include <climits>

namespace aiverse::world {

inline std::unique_ptr<terraweave::TerraWeaveHorizonScene>
horizonSceneFromManifest(
    const Json& manifest,
    const terraweave::TerraWeaveForestScene* forest = nullptr,
    AssetReader reader = {}) {
    if (!manifest.has("terraweaveHorizon")) return {};
    const Json& j=manifest["terraweaveHorizon"];
    if (!j.isObject())
        throw JsonError("world.terraweaveHorizon: expected object");

    terraweave::TerrainSettings terrain =
        forest ? forest->settings().terrain : terraweave::TerrainSettings{};
    terraweave::BiomeSettings biomes =
        forest ? forest->settings().biomes : terraweave::BiomeSettings{};
    terrain.seed=static_cast<uint64_t>(worldInteger(
        j,"terrainSeed",static_cast<int64_t>(terrain.seed),0,INT32_MAX));
    terrain.baseHeightCm=static_cast<int32_t>(worldInteger(
        j,"baseHeightCm",terrain.baseHeightCm,-1000000,1000000));
    terrain.reliefCm=static_cast<int32_t>(worldInteger(
        j,"reliefCm",terrain.reliefCm,0,1000000));
    terrain.basePeriodCm=static_cast<int32_t>(worldInteger(
        j,"basePeriodCm",terrain.basePeriodCm,1,10000000));
    terrain.octaves=static_cast<uint32_t>(worldInteger(
        j,"octaves",terrain.octaves,1,12));
    biomes.seed=static_cast<uint64_t>(worldInteger(
        j,"biomeSeed",static_cast<int64_t>(biomes.seed),0,INT32_MAX));
    biomes.seaLevelCm=static_cast<int32_t>(worldInteger(
        j,"seaLevelCm",biomes.seaLevelCm,-1000000,1000000));

    const std::string quality=j.getStr("quality","balanced");
    terraweave::HorizonQualityPreset preset;
    if(quality=="minimal")preset=terraweave::HorizonQualityPreset::Minimal;
    else if(quality=="mobile")preset=terraweave::HorizonQualityPreset::Mobile;
    else if(quality=="balanced")preset=terraweave::HorizonQualityPreset::Balanced;
    else if(quality=="high")preset=terraweave::HorizonQualityPreset::High;
    else throw JsonError("world.terraweaveHorizon.quality: expected minimal|mobile|balanced|high");
    terraweave::HorizonSettings settings=terraweave::horizonSettingsForQuality(preset);
    settings.middleTileCm=static_cast<int32_t>(worldInteger(
        j,"middleTileCm",settings.middleTileCm,1000,200000));
    settings.middleTilesPerSide=static_cast<uint32_t>(worldInteger(
        j,"middleTilesPerSide",settings.middleTilesPerSide,4,32));
    settings.distantTileCm=static_cast<int32_t>(worldInteger(
        j,"distantTileCm",settings.distantTileCm,1000,400000));
    settings.distantTilesPerSide=static_cast<uint32_t>(worldInteger(
        j,"distantTilesPerSide",settings.distantTilesPerSide,4,32));
    // Align the handover with the actual detail-forest radius, avoiding
    // an uncovered annulus when both optional modules are enabled.
    const int32_t nearForestEnd = forest ?
        forest->settings().view.cullRangeCm : settings.closeForestEndCm;
    settings.closeForestEndCm=static_cast<int32_t>(worldInteger(
        j,"closeForestEndCm",nearForestEnd,0,10000000));
    settings.middleForestEndCm=static_cast<int32_t>(worldInteger(
        j,"middleForestEndCm",settings.middleForestEndCm,0,10000000));
    settings.terrainBeginCm=static_cast<int32_t>(worldInteger(
        j,"terrainBeginCm",settings.terrainBeginCm,0,10000000));
    settings.terrainSkirtDepthCm=static_cast<int32_t>(worldInteger(
        j,"terrainSkirtDepthCm",settings.terrainSkirtDepthCm,0,5000));
    settings.terrainGridSnapSteps=static_cast<uint32_t>(worldInteger(
        j,"terrainGridSnapSteps",settings.terrainGridSnapSteps,1,16));
    settings.innerTerrainLipCm=static_cast<int32_t>(worldInteger(
        j,"innerTerrainLipCm",settings.innerTerrainLipCm,0,200));
    settings.maxNearClipTriangles=static_cast<uint32_t>(worldInteger(
        j,"maxNearClipTriangles",settings.maxNearClipTriangles,0,8192));
    settings.maxNearWeldSlopePermille=static_cast<uint32_t>(worldInteger(
        j,"maxNearWeldSlopePermille",settings.maxNearWeldSlopePermille,0,8000));
    settings.nearSeamNormalBlendPercent=static_cast<uint32_t>(worldInteger(
        j,"nearSeamNormalBlendPercent",settings.nearSeamNormalBlendPercent,0,100));
    settings.terrainResolution=static_cast<uint32_t>(worldInteger(
        j,"terrainResolution",settings.terrainResolution,8,96));
    settings.atlasTilePixels=static_cast<uint32_t>(worldInteger(
        j,"atlasTilePixels",settings.atlasTilePixels,16,64));
    settings.canopyHeightCm=static_cast<int32_t>(worldInteger(
        j,"canopyHeightCm",settings.canopyHeightCm,100,10000));
    settings.crownDetail=static_cast<uint32_t>(worldInteger(
        j,"crownDetail",settings.crownDetail,1,3));
    settings.orientationViews=static_cast<uint32_t>(worldInteger(
        j,"orientationViews",settings.orientationViews,1,2));
    settings.maxCachedRegions=static_cast<uint32_t>(worldInteger(
        j,"maxCachedRegions",settings.maxCachedRegions,0,4));
    settings.maxCachedGpuRegions=static_cast<uint32_t>(worldInteger(
        j,"maxCachedGpuRegions",settings.maxCachedGpuRegions,0,3));
    settings.gpuCacheBudgetMiB=static_cast<uint32_t>(worldInteger(
        j,"gpuCacheBudgetMiB",settings.gpuCacheBudgetMiB,8,96));
    settings.regionTransitionMs=static_cast<uint32_t>(worldInteger(
        j,"regionTransitionMs",settings.regionTransitionMs,0,1000));
    settings.regionHysteresisCm=static_cast<int32_t>(worldInteger(
        j,"regionHysteresisCm",settings.regionHysteresisCm,0,100000));
    settings.nearBlendCm=static_cast<int32_t>(worldInteger(
        j,"nearBlendCm",settings.nearBlendCm,0,10000000));
    settings.tierBlendCm=static_cast<int32_t>(worldInteger(
        j,"tierBlendCm",settings.tierBlendCm,0,10000000));
    settings.distantTerrain=j.getBool("distantTerrain",true);
    terraweave::validateHorizonSettings(settings);
    auto scene=std::make_unique<terraweave::TerraWeaveHorizonScene>(
        terrain,biomes,settings);
    // Stage39 quality applies to presentation only, independently of the
    // existing "quality" geometry/bake preset. No field = legacy defaults.
    const std::string presentationQuality=j.getStr("presentationQuality","");
    if(!presentationQuality.empty()) {
        terraweave::HorizonPresentationQualityPreset selected;
        using Preset=terraweave::HorizonPresentationQualityPreset;
        if(presentationQuality=="battery-saver") selected=Preset::BatterySaver;
        else if(presentationQuality=="mobile") selected=Preset::Mobile;
        else if(presentationQuality=="balanced") selected=Preset::Balanced;
        else if(presentationQuality=="desktop") selected=Preset::Desktop;
        else throw JsonError(
            "world.terraweaveHorizon.presentationQuality: expected battery-saver|mobile|balanced|desktop");
        scene->setPresentationQualityPreset(selected);
    }
    // Stage40 controller is separate from the geometry and runtime preset.
    // It remains OFF unless explicitly enabled and requires the native host
    // to feed real frame timing samples; the manifest cannot invent timings.
    if(j.has("adaptiveQuality")) {
        const Json& adaptive=j["adaptiveQuality"];
        if(!adaptive.isObject())
            throw JsonError("world.terraweaveHorizon.adaptiveQuality: expected object");
        const auto parseLevel=[](const std::string& value)
                ->terraweave::HorizonPresentationQualityPreset {
            using Preset=terraweave::HorizonPresentationQualityPreset;
            if(value=="battery-saver")return Preset::BatterySaver;
            if(value=="mobile")return Preset::Mobile;
            if(value=="balanced")return Preset::Balanced;
            if(value=="desktop")return Preset::Desktop;
            throw JsonError("world.terraweaveHorizon.adaptiveQuality: invalid quality level");
        };
        terraweave::HorizonAdaptiveQualityConfig config;
        config.enabled=adaptive.getBool("enabled",false);
        // If a static BatterySaver presentation preset was selected,
        // admit it as a valid starting adaptive floor by default.
        config.minimum=parseLevel(adaptive.getStr("minimum",
            presentationQuality=="battery-saver" ?
                "battery-saver" : "mobile"));
        config.maximum=parseLevel(adaptive.getStr("maximum","desktop"));
        // A selected static runtime profile also serves as the default
        // starting level unless the adaptive block overrides "initial".
        config.initial=parseLevel(adaptive.getStr("initial",
            presentationQuality.empty() ? "balanced" : presentationQuality));
        config.targetFrameUs=static_cast<uint32_t>(worldInteger(
            adaptive,"targetFrameUs",config.targetFrameUs,4000,100000));
        config.downgradePercent=static_cast<uint32_t>(worldInteger(
            adaptive,"downgradePercent",config.downgradePercent,101,200));
        config.upgradePercent=static_cast<uint32_t>(worldInteger(
            adaptive,"upgradePercent",config.upgradePercent,25,99));
        config.downgradeFrames=static_cast<uint32_t>(worldInteger(
            adaptive,"downgradeFrames",config.downgradeFrames,2,600));
        config.upgradeFrames=static_cast<uint32_t>(worldInteger(
            adaptive,"upgradeFrames",config.upgradeFrames,2,3600));
        config.cooldownFrames=static_cast<uint32_t>(worldInteger(
            adaptive,"cooldownFrames",config.cooldownFrames,0,36000));
        try {
            scene->setAdaptiveQualityConfig(config);
        } catch(const std::invalid_argument&) {
            throw JsonError("world.terraweaveHorizon.adaptiveQuality: invalid quality bounds or initial level");
        }
    }
    // Read-only prebaked assets are strictly opt-in. The game pack owns
    // access to the archive files; runtime never writes into content.
    const std::string archivePrefix=j.getStr("archivePrefix");
    if(!archivePrefix.empty()){
        if(!reader || !safeAssetPath(archivePrefix))
            throw JsonError("world.terraweaveHorizon.archivePrefix: invalid asset folder");
        auto store=std::make_shared<terraweave::HorizonArchiveStore>();
        store->authoredContentHash=static_cast<uint64_t>(worldInteger(
            j,"archiveContentHash",0,0,INT32_MAX));
        store->read=[reader,prefix=archivePrefix](
                int32_t cx,int32_t cz,std::vector<uint8_t>& out)->bool {
            // The only variable filename components are bounded integer
            // region coordinates; caller cannot supply a traversal path.
            const std::string asset=prefix+"/"+std::to_string(cx)+
                                    "_"+std::to_string(cz)+".twh";
            std::string bytes;
            if(!reader(asset,bytes))return false;
            if(bytes.size()>20u*1024u*1024u)return false;
            out.assign(bytes.begin(),bytes.end());
            return true;
        };
        scene->setArchiveStore(std::move(store));
    }
    return scene;
}

} // namespace aiverse::world
