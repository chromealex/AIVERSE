// TerraWeave Horizon HLOD: one coarse terrain mesh and a shared forest-cluster
// silhouette atlas covering several kilometres. CPU-only deterministic bake.
// Forest cover is evaluated at coarse resolution, NOT one tree at a time.
#pragma once
#include "engine/world/TerraWeave.h"
#include "engine/world/TerraWeavePhenology.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <limits>
#include <tuple>
#include <span>
#include <map>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct HorizonSettings {
    int32_t middleTileCm = 12800;       // one 128m forest cluster
    uint32_t middleTilesPerSide = 16;   // 2.0km across (ring 0.25..1km)
    int32_t distantTileCm = 51200;      // one 512m forest cluster
    uint32_t distantTilesPerSide = 24;  // 12.3km across (ring 1..6km)
    int32_t closeForestEndCm = 25000;  // individual tree renderer owns this
    int32_t middleForestEndCm = 100000;
    int32_t terrainBeginCm = 110000;   // no proxy terrain over near authored ground
    uint32_t terrainResolution = 48;   // 48x48 quads for all 12km
    uint32_t atlasTilePixels = 48;     // ~8MB with default region sizes
    int32_t canopyHeightCm = 1800;    // forest crown silhouette thickness
    uint32_t crownDetail = 2;         // 1..3 CPU-only crown silhouette quality
    uint32_t orientationViews = 1;    // 1 cheap shared, 2 orthogonal canopy projections
    uint32_t maxCachedRegions = 2;    // 0=disable, 1..4 recent CPU atlases
    uint32_t maxCachedGpuRegions = 1; // 0=disable, 1..3 additional GPU-resident regions
    uint32_t gpuCacheBudgetMiB = 32;  // hard LRU allowance for cached models, not active/staging
    uint32_t regionTransitionMs = 240; // crossfade between neighboring HLOD regions; 0=off
    int32_t regionHysteresisCm = 3200; // camera must pass cell edge by this margin
    int32_t nearBlendCm = 6000;       // masked silhouette fade-in before near trees end
    int32_t tierBlendCm = 16000;      // stochastic mid-to-far overlap around 1 km
    bool distantTerrain = true;       // opt-in manifest, disabled unless requested
    // Trailing to preserve older aggregate HorizonSettings initializers.
    int32_t terrainSkirtDepthCm = 1200; // 0=off, lower outer edge by 12m
    uint32_t terrainGridSnapSteps = 4; // 1..16 grid steps; stabilize distant mountain silhouettes
    int32_t innerTerrainLipCm = 40; // 0..200cm, modest seam lip around owned near-ground quads
    uint32_t maxNearClipTriangles = 2048; // 0=off, bounded extra triangles for partial near overlap
    uint32_t maxNearWeldSlopePermille = 2500; // 0=off; reject new wedge >2.5m/m rather than distort seam
    uint32_t nearSeamNormalBlendPercent = 65; // 0=original normals, 100=area-weighted welded normals
};

struct HorizonCluster {
    int64_t centerXcm = 0, centerZcm = 0;
    int32_t tileSizeCm = 0;
    int32_t bottomCm = 0, topCm = 0;
    uint32_t atlasX = 0, atlasY = 0; // first directional silhouette
    uint32_t atlasSideX = 0, atlasSideY = 0; // 90-degree silhouette if enabled
    uint32_t atlasSize = 0;
    uint8_t coverage = 0;             // 0..255, rasterized forest mask
};

struct HorizonTerrainVertex {
    int64_t xCm = 0, zCm = 0;
    int32_t heightCm = 0;
    uint32_t colorRgba = 0xffffffffu;
    // Slope sampled on the globally anchored vertex lattice. Identical
    // world coordinates have identical normals even at region borders.
    int32_t slopeXPermille = 0, slopeZPermille = 0;
};

// Optional immutable host-supplied landscape data. Passed as a borrowed pointer
// only for the duration of bakeHorizon; the async host owns a shared snapshot.
struct HorizonSurfaceSources {
    std::function<int32_t(int64_t, int64_t)> heightAtCm;
    std::function<Biome(int64_t, int64_t)> biomeAt;
    // Road, water, and building masks suppress individual crown samples.
    std::function<bool(int64_t, int64_t)> allowsCanopyAt;
    // Exact half-open world-centimetre rectangle, covered in its entirety
    // by already resident authored near-terrain geometry. If unset, the
    // backwards-compatible radial cutout is used instead.
    std::function<bool(int64_t, int64_t, int64_t, int64_t)> ownsNearTerrainRect;
    // Optional complete list of non-overlapping near-ground rectangles
    // intersecting the requested distant terrain footprint. Return false
    // for overflow to disable fine clipping for this bake safely.
    std::function<bool(RectCm,std::vector<RectCm>&)> collectNearTerrainRects;
    // Exact authored near-surface height in world centimetres; nullopt
    // means the point is not resident. Called on CPU during baking only.
    std::function<std::optional<int32_t>(int64_t,int64_t)>
        nearGroundHeightAtCm;
    // Optional high-precision immutable near surface query. Coordinates
    // carry 16 fractional bits per centimetre, preserving the true source
    // height and light gradient at the exact triangle clipping position.
    // Legacy native providers can omit this and retain whole-cm sampling.
    std::function<std::optional<int32_t>(int64_t,int64_t)>
        nearGroundHeightAtQ16Cm;
    // Optional neighbor-aware signed X/Z slopes in permille. Returns nullopt
    // when a tile is absent or a boundary is discontinuous. CPU bake only.
    std::function<std::optional<std::pair<int32_t,int32_t>>(int64_t,int64_t)>
        nearGroundContinuousSlopeAtQ16Cm;
    // Optional immutable seasonal appearance. CPU canopy sampling only;
    // ground height, authored biomes, near ownership and physics unchanged.
    CanopyPhenology phenology{};
};

struct HorizonBake {
    int64_t originXcm = 0, originZcm = 0; // subtract before casting to float
    int32_t sideCm = 0;
    uint32_t terrainResolution = 0;
    int32_t terrainBeginCm = 0;
    int64_t cameraXcm = 0, cameraZcm = 0;
    uint32_t atlasWidth = 0, atlasHeight = 0;
    std::vector<uint8_t> atlasRgba; // one atlas, shared by all forest cluster cards
    std::vector<HorizonTerrainVertex> terrainVertices;
    std::vector<HorizonCluster> clusters;
    // Trailing to preserve existing aggregate HorizonBake initializers.
    int32_t terrainSkirtDepthCm = 0;
    // One byte per coarse terrain cell; 1 means near terrain owns the
    // FULL quad and its two triangles must be omitted from distant ground.
    // Empty = legacy radial cutout mode, for procedural worlds.
    std::vector<uint8_t> nearOwnedQuads;
    int32_t innerTerrainLipCm = 0;
    // World-centimetre half-open near-ground rectangles; a shared immutable
    // snapshot of the exact resident tiles used by this bake (at most 1024).
    std::vector<RectCm> nearClipRects;
    uint32_t maxNearClipTriangles = 0; // 0=coarse legacy; cap on additional partial triangles
    // CPU-baked render-only seam samples. Q16 centimetres retain fractional
    // polygon intersection coordinates across archive roundtrips.
    struct NearSeamSample {
        int64_t xQ16cm=0,zQ16cm=0;
        int32_t nearHeightCm=0;
        // Render-only authored surface normal, sampled inside the same
        // immutable tile. Defaults keep legacy aggregate initializers valid.
        int32_t nearSlopeXPermille=0, nearSlopeZPermille=0;
        bool nearNormalValid=false;
    };
    std::vector<NearSeamSample> nearSeamSamples; // capped at 4096 unique points
    // CPU-only provenance; intentionally excluded from TWH8 serialization.
    size_t continuousNearNormalSamples=0;
    size_t localFallbackNearNormalSamples=0;
    bool nearNormalProvenanceAvailable=false; // CPU-only; absent from TWH8 archive
    uint32_t maxNearWeldSlopePermille = 0; // 0=disable steepness guard
    uint32_t nearSeamNormalBlendPercent = 0; // 0..100; render-only shading
    bool nearWeldSlopeFallback = false; // source heights not welded for excessive wedge
};

namespace horizon_detail {
inline uint32_t checkedSquareRootCeil(uint32_t count) {
    uint32_t x=1;
    while (uint64_t(x)*x<count) ++x;
    return x;
}
inline uint32_t rgba(uint32_t r,uint32_t g,uint32_t b) {
    return 0xff000000u | ((b&255u)<<16) | ((g&255u)<<8) | (r&255u);
}
inline uint32_t biomeColor(Biome biome, int32_t heightCm, uint64_t noise) {
    uint32_t r=83, g=127, b=63;
    switch(biome) {
        case Biome::Ocean: r=42;g=74;b=117;break;
        case Biome::Tundra: r=131;g=137;b=126;break;
        case Biome::Desert: r=193;g=169;b=114;break;
        case Biome::Grassland: r=105;g=145;b=72;break;
        case Biome::Forest: r=52;g=99;b=50;break;
        case Biome::Wetland: r=68;g=110;b=73;break;
        case Biome::Alpine: r=116;g=121;b=116;break;
    }
    if (heightCm>240000) {r=196;g=199;b=194;}
    const int k=int((noise>>24)%21)-10;
    const auto add=[k](uint32_t c)->uint32_t {
        return static_cast<uint32_t>(std::clamp(int(c)+k,0,255));
    };
    return rgba(add(r),add(g),add(b));
}
inline uint32_t smoothCoverage(int64_t value, int64_t start, int64_t end) {
    if (end<=start) return value>=end ? 255u : 0u;
    if (value<=start) return 0u;
    if (value>=end) return 255u;
    // Q16 smoothstep; deterministic integer CPU baker.
    const int64_t t=(value-start)*65536/(end-start);
    const int64_t t2=t*t/65536;
    const int64_t eased=t2*(3*65536-2*t)/65536;
    return static_cast<uint32_t>(eased*255/65536);
}
inline uint32_t canopyStrength(Biome b) {
    switch(b) {
        case Biome::Forest: return 230;
        case Biome::Wetland: return 180;
        case Biome::Grassland: return 85;
        case Biome::Tundra: return 30;
        default: return 0;
    }
}
} // namespace horizon_detail

inline void validateHorizonSettings(const HorizonSettings& s) {
    if (s.middleTileCm<1000 || s.middleTileCm>200000 ||
        s.distantTileCm<s.middleTileCm || s.distantTileCm>400000 ||
        s.middleTilesPerSide<4 || s.middleTilesPerSide>32 ||
        s.distantTilesPerSide<4 || s.distantTilesPerSide>32 ||
        (s.middleTilesPerSide&1u) || (s.distantTilesPerSide&1u) ||
        s.closeForestEndCm<0 || s.middleForestEndCm<=s.closeForestEndCm ||
        s.terrainBeginCm<0 || s.terrainSkirtDepthCm<0 ||
        s.terrainSkirtDepthCm>5000 || s.atlasTilePixels<16 ||
        s.atlasTilePixels>64 || s.terrainResolution<8 ||
        s.terrainResolution>96 || s.canopyHeightCm<100 ||
        s.canopyHeightCm>10000 || s.crownDetail<1 || s.crownDetail>3 ||
        (s.orientationViews!=1 && s.orientationViews!=2) ||
        s.maxCachedRegions>4 || s.maxCachedGpuRegions>3 ||
        s.gpuCacheBudgetMiB<8 || s.gpuCacheBudgetMiB>96 ||
        s.regionTransitionMs>1000 ||
        s.terrainGridSnapSteps==0 || s.terrainGridSnapSteps>16 ||
        s.innerTerrainLipCm<0 || s.innerTerrainLipCm>200 ||
        s.maxNearClipTriangles>8192 ||
        s.maxNearWeldSlopePermille>8000 ||
        s.nearSeamNormalBlendPercent>100 ||
        s.regionHysteresisCm<0 ||
        s.regionHysteresisCm>=s.middleTileCm/2 ||
        s.nearBlendCm<0 || s.nearBlendCm>s.closeForestEndCm ||
        int64_t(s.nearBlendCm)>s.middleForestEndCm-s.closeForestEndCm ||
        s.tierBlendCm<0 ||
        int64_t(s.tierBlendCm)>s.middleForestEndCm-s.closeForestEndCm)
        throw std::invalid_argument("TerraWeave Horizon settings");
    const uint64_t tiles=(uint64_t(s.middleTilesPerSide)*s.middleTilesPerSide+
                          uint64_t(s.distantTilesPerSide)*s.distantTilesPerSide)*
                          s.orientationViews;
    const uint64_t side=uint64_t(horizon_detail::checkedSquareRootCeil(
        static_cast<uint32_t>(tiles)))*s.atlasTilePixels;
    if (side>2048 || side*side*4>16u*1024u*1024u)
        throw std::invalid_argument("TerraWeave Horizon atlas memory budget");
    if (int64_t(s.middleTileCm)*s.middleTilesPerSide/2 <
            s.middleForestEndCm-s.middleTileCm ||
        int64_t(s.distantTileCm)*s.distantTilesPerSide/2 <
            s.middleForestEndCm + s.distantTileCm)
        throw std::invalid_argument("TerraWeave Horizon rings do not cover ranges");
}

// Opt-in quality presets retain the same two-primitive render path,
// shared canopy atlas and genre-independent generator. Callers may override
// individual settings after selecting a preset.
enum class HorizonQualityPreset : uint8_t {
    Minimal, Mobile, Balanced, High
};

inline HorizonSettings horizonSettingsForQuality(HorizonQualityPreset preset) {
    HorizonSettings settings;
    switch (preset) {
        case HorizonQualityPreset::Minimal:
            settings.atlasTilePixels=16;
            settings.terrainResolution=16;
            settings.terrainGridSnapSteps=1;
            settings.maxNearClipTriangles=0;
            settings.terrainSkirtDepthCm=0; // mobile-minimum eliminates outer skirt geometry
            settings.crownDetail=1;
            settings.maxCachedRegions=0;
            settings.maxCachedGpuRegions=0;
            settings.regionTransitionMs=0;
            settings.regionHysteresisCm=4800;
            break;
        case HorizonQualityPreset::Mobile:
            settings.atlasTilePixels=24;
            settings.terrainResolution=24;
            settings.terrainGridSnapSteps=2;
            settings.maxNearClipTriangles=512;
            settings.terrainSkirtDepthCm=600;
            settings.crownDetail=1;
            settings.maxCachedRegions=0;
            settings.maxCachedGpuRegions=0;
            settings.regionTransitionMs=0;
            settings.regionHysteresisCm=4000;
            break;
        case HorizonQualityPreset::Balanced:
            break;
        case HorizonQualityPreset::High:
            settings.atlasTilePixels=48;
            settings.terrainResolution=64;
            settings.crownDetail=3;
            settings.orientationViews=2;
            settings.maxNearClipTriangles=4096;
            settings.maxCachedRegions=2;
            settings.maxCachedGpuRegions=2;
            break;
        default:
            throw std::invalid_argument("TerraWeave Horizon quality preset");
    }
    validateHorizonSettings(settings);
    return settings;
}

// Worst-case resident atlas/mesh budget, independent of sampled biome and
// without claiming any particular GPU driver's measured draw-call count.
struct HorizonBudget {
    size_t atlasBytes = 0;
    size_t maxVertices = 0;
    size_t maxTriangles = 0;
    size_t maxForestClusters = 0;
};

inline HorizonBudget estimateHorizonBudget(const HorizonSettings& s) {
    validateHorizonSettings(s);
    const size_t clusters=size_t(s.middleTilesPerSide)*s.middleTilesPerSide+
                          size_t(s.distantTilesPerSide)*s.distantTilesPerSide;
    const size_t atlasSide=
        size_t(horizon_detail::checkedSquareRootCeil(
            static_cast<uint32_t>(clusters*s.orientationViews)))*
            s.atlasTilePixels;
    // Up to 512 near ownership seam segments, two new bottom vertices
    // and two triangles each. This is a conservative upper bound.
    const size_t innerEdges=s.innerTerrainLipCm>0 ? 512u:0u;
    const size_t clipTriangles=s.maxNearClipTriangles;
    const size_t terrainVertices=s.distantTerrain ?
        size_t(s.terrainResolution+1)*(s.terrainResolution+1) +
        (s.terrainSkirtDepthCm>0 ? size_t(4)*(s.terrainResolution+1):0) +
        innerEdges*2+clipTriangles*3 : 0;
    const size_t terrainTriangles=s.distantTerrain ?
        size_t(s.terrainResolution)*s.terrainResolution*2 +
        (s.terrainSkirtDepthCm>0 ? size_t(8)*s.terrainResolution:0) +
        innerEdges*2+clipTriangles : 0;
    // Two crossed cards per cluster, each with 4 vertices and 2 triangles.
    return {atlasSide*atlasSide*4,terrainVertices+clusters*8,
            terrainTriangles+clusters*4,clusters};
}

// Public, genre-independent world-lattice coordinate contract. Other
// terrain producers and offline asset bakers can sample precisely the same
// far-grid coordinates without building a full HLOD forest atlas.
struct HorizonTerrainLattice {
    int64_t sideCm=0;
    int64_t startIndexX=0, startIndexZ=0;
    uint32_t resolution=0;
    void validate() const {
        if(sideCm<=0 || resolution==0 || resolution>96)
            throw std::invalid_argument("TerraWeave Horizon terrain lattice");
    }
    int64_t worldX(int64_t vertexIndex) const {
        validate();
        return detail::floorDiv(
            (startIndexX+vertexIndex)*sideCm,int64_t(resolution));
    }
    int64_t worldZ(int64_t vertexIndex) const {
        validate();
        return detail::floorDiv(
            (startIndexZ+vertexIndex)*sideCm,int64_t(resolution));
    }
    int64_t nominalStepCm() const {
        validate();
        return detail::floorDiv(sideCm,int64_t(resolution));
    }
};

inline HorizonTerrainLattice makeHorizonTerrainLattice(
        const HorizonSettings& s,int64_t cameraXcm,int64_t cameraZcm) {
    validateHorizonSettings(s);
    if(cameraXcm<-kWorldLimitCm || cameraXcm>kWorldLimitCm ||
       cameraZcm<-kWorldLimitCm || cameraZcm>kWorldLimitCm)
        throw std::out_of_range("TerraWeave Horizon lattice observer");
    const int64_t side=int64_t(s.distantTileCm)*s.distantTilesPerSide;
    const int64_t n=s.terrainResolution;
    const int64_t stride=s.terrainGridSnapSteps;
    const int64_t xStep=detail::floorDiv(cameraXcm*n,side);
    const int64_t zStep=detail::floorDiv(cameraZcm*n,side);
    // Round to the *nearest* world-grid anchor instead of always flooring.
    // This limits maximum camera-to-terrain-center offset to roughly half
    // the anchor interval, keeping adequate far-field coverage around view.
    return {side,
        detail::floorDiv(xStep+stride/2,stride)*stride-n/2,
        detail::floorDiv(zStep+stride/2,stride)*stride-n/2,
        s.terrainResolution};
}

// Each call is deterministic for a snapped camera region, with fixed memory
// bounds. Bake on a worker in production; upload prepared models via the RHI.
inline HorizonBake bakeHorizon(const TerrainSettings& terrain,
                              const BiomeSettings& biomes,
                              const HorizonSettings& s,
                              int64_t cameraXcm, int64_t cameraZcm,
                              const HorizonSurfaceSources* source = nullptr,
                              const HorizonBake* previous = nullptr,
                              std::span<const RectCm> edited = {}) {
    validateHorizonSettings(s);
    const CanopyPhenology phenology=source ? source->phenology : CanopyPhenology{};
    validateCanopyPhenology(phenology);
    const auto heightSample = [&](int64_t x, int64_t z) -> int32_t {
        const int32_t h = source && source->heightAtCm ?
            source->heightAtCm(x, z) : heightAtCm(terrain, x, z);
        if (h < -kWorldLimitCm || h > kWorldLimitCm)
            throw std::out_of_range("TerraWeave Horizon sampled height extent");
        return h;
    };
    const auto biomeSample = [&](int64_t x, int64_t z) -> Biome {
        return source && source->biomeAt ?
            source->biomeAt(x, z) : climateAt(terrain, biomes, x, z).biome;
    };
    const auto canopyAllowed = [&](int64_t x, int64_t z) {
        return !source || !source->allowsCanopyAt ||
               source->allowsCanopyAt(x, z);
    };
    if (cameraXcm < -kWorldLimitCm || cameraXcm > kWorldLimitCm ||
        cameraZcm < -kWorldLimitCm || cameraZcm > kWorldLimitCm)
        throw std::out_of_range("TerraWeave Horizon observer");
    HorizonBake out;
    out.cameraXcm=cameraXcm;out.cameraZcm=cameraZcm;
    out.terrainResolution=s.terrainResolution;
    out.terrainBeginCm=s.terrainBeginCm;
    // Every Horizon consumer shares the same signed world-lattice API.
    // Grid coordinates are absolute, independent of the chosen camera
    // region. Ghost neighbors use indices -1 and N+1.
    const auto lattice=makeHorizonTerrainLattice(s,cameraXcm,cameraZcm);
    const int64_t regionSide=lattice.sideCm;
    out.originXcm=lattice.worldX(0);
    out.originZcm=lattice.worldZ(0);
    out.sideCm=static_cast<int32_t>(regionSide);
    out.terrainSkirtDepthCm=s.terrainSkirtDepthCm;
    out.innerTerrainLipCm=s.innerTerrainLipCm;
    out.maxNearClipTriangles=s.maxNearClipTriangles;
    out.maxNearWeldSlopePermille=s.maxNearWeldSlopePermille;
    out.nearSeamNormalBlendPercent=s.nearSeamNormalBlendPercent;
    if (out.originXcm < -kWorldLimitCm+100 ||
        out.originZcm < -kWorldLimitCm+100 ||
        out.originXcm+out.sideCm>kWorldLimitCm-100 ||
        out.originZcm+out.sideCm>kWorldLimitCm-100)
        throw std::out_of_range("TerraWeave Horizon region extent");
    // One bounded query per CPU region. Exact authored coverage is kept
    // independently from the coarse full-quad mask, so partial resident
    // WorldScene tiles can clip rather than overlapping a full far quad.
    if(s.distantTerrain && s.maxNearClipTriangles && source &&
       source->ownsNearTerrainRect && source->collectNearTerrainRects) {
        const RectCm footprint{
            lattice.worldX(0),lattice.worldZ(0),
            lattice.worldX(s.terrainResolution),
            lattice.worldZ(s.terrainResolution)
        };
        std::vector<RectCm> rectangles;
        if(source->collectNearTerrainRects(footprint,rectangles) &&
           rectangles.size()<=1024) {
            bool valid=true;
            for(const RectCm& r:rectangles)
                if(r.minX>=r.maxX||r.minZ>=r.maxZ||
                   r.minX<footprint.minX||r.maxX>footprint.maxX||
                   r.minZ<footprint.minZ||r.maxZ>footprint.maxZ)
                    valid=false;
            if(!valid)throw std::invalid_argument(
                "TerraWeave Horizon near clip rectangle source");
            std::sort(rectangles.begin(),rectangles.end(),
                [](const RectCm& a,const RectCm& b) {
                    return std::tie(a.minZ,a.minX,a.maxZ,a.maxX) <
                           std::tie(b.minZ,b.minX,b.maxZ,b.maxX);
                });
            out.nearClipRects=std::move(rectangles);
        }
    }

    const uint32_t possible=s.middleTilesPerSide*s.middleTilesPerSide+
                            s.distantTilesPerSide*s.distantTilesPerSide;
    const uint32_t atlasSide=horizon_detail::checkedSquareRootCeil(
                                 possible*s.orientationViews)*s.atlasTilePixels;
    out.atlasWidth=atlasSide;out.atlasHeight=atlasSide;
    const bool compatible = previous && !edited.empty() &&
        previous->cameraXcm==cameraXcm && previous->cameraZcm==cameraZcm &&
        previous->originXcm==out.originXcm &&
        previous->originZcm==out.originZcm &&
        previous->sideCm==out.sideCm &&
        previous->atlasWidth==atlasSide &&
        previous->atlasHeight==atlasSide &&
        previous->atlasRgba.size()==size_t(atlasSide)*atlasSide*4 &&
        previous->terrainResolution==s.terrainResolution &&
        previous->terrainBeginCm==s.terrainBeginCm &&
        previous->terrainSkirtDepthCm==s.terrainSkirtDepthCm &&
        previous->innerTerrainLipCm==s.innerTerrainLipCm &&
        previous->maxNearClipTriangles==s.maxNearClipTriangles &&
        previous->maxNearWeldSlopePermille==s.maxNearWeldSlopePermille &&
        previous->nearSeamNormalBlendPercent==s.nearSeamNormalBlendPercent &&
        (source && source->ownsNearTerrainRect
            ? previous->nearOwnedQuads.size()==size_t(s.terrainResolution)*
                                              s.terrainResolution
            : previous->nearOwnedQuads.empty()) &&
        (!s.distantTerrain || previous->terrainVertices.size()==
          size_t(s.terrainResolution+1)*(s.terrainResolution+1));
    for (const RectCm& r : edited) {
        if (r.maxX<=r.minX || r.maxZ<=r.minZ ||
            r.minX<-kWorldLimitCm || r.maxX>kWorldLimitCm ||
            r.minZ<-kWorldLimitCm || r.maxZ>kWorldLimitCm)
            throw std::invalid_argument("TerraWeave Horizon edited region bounds");
    }
    if(compatible)out.atlasRgba=previous->atlasRgba;
    else out.atlasRgba.assign(size_t(atlasSide)*atlasSide*4,0);
    out.clusters.reserve(possible);
    using ClusterId=std::tuple<int64_t,int64_t,int32_t>;
    std::map<ClusterId,HorizonCluster> original;
    if(compatible)for(const auto& cluster:previous->clusters)
        original.emplace(ClusterId{cluster.centerXcm,cluster.centerZcm,
                                    cluster.tileSizeCm},cluster);
    // Samples span the full cluster plus nearby slope/canopy neighbours.
    // Conservative extra half-tile prevents a modified boundary from
    // retaining stale silhouettes in the adjacent tile.
    const auto dirtyAt = [&](int64_t x,int64_t z,int64_t padding)->bool {
        for(const RectCm& r:edited)
            if(x+padding>=r.minX && x-padding<r.maxX &&
               z+padding>=r.minZ && z-padding<r.maxZ)return true;
        return false;
    };

    const auto addLayer = [&](int32_t cell,uint32_t count,int32_t minDistance,
                              int32_t maxDistance,uint64_t salt,bool middle,uint32_t atlasBase) {
        const int64_t centerCellX=detail::floorDiv(cameraXcm,cell);
        const int64_t centerCellZ=detail::floorDiv(cameraZcm,cell);
        const int64_t half=count/2;
        for (uint32_t iz=0;iz<count;++iz)
            for (uint32_t ix=0;ix<count;++ix) {
                const int64_t gx=centerCellX+int64_t(ix)-half;
                const int64_t gz=centerCellZ+int64_t(iz)-half;
                const int64_t x=gx*cell+cell/2;
                const int64_t z=gz*cell+cell/2;
                const int64_t dx=x-cameraXcm,dz=z-cameraZcm;
                const int64_t dist2=dx*dx+dz*dz;
                if (dist2<int64_t(minDistance)*minDistance ||
                    dist2>=int64_t(maxDistance)*maxDistance)continue;
                // Spatial alpha-to-coverage: each cluster's silhouette
                // progressively appears/disappears through world-stable
                // pixel dithering, without one draw per cluster or a
                // new alpha-blended shader. The near geometry retains
                // ownership up to closeForestEndCm.
                const int64_t dist=static_cast<int64_t>(std::sqrt(
                    static_cast<double>(dist2)));
                uint32_t visibility=255;
                if(middle) {
                    const uint32_t begin=horizon_detail::smoothCoverage(
                        dist,s.closeForestEndCm-s.nearBlendCm,
                        s.closeForestEndCm);
                    const uint32_t end=255u-horizon_detail::smoothCoverage(
                        dist,s.middleForestEndCm-s.tierBlendCm/2,
                        s.middleForestEndCm+(s.tierBlendCm-s.tierBlendCm/2));
                    visibility=begin*end/255u;
                } else {
                    visibility=horizon_detail::smoothCoverage(
                        dist,s.middleForestEndCm-s.tierBlendCm/2,
                        s.middleForestEndCm+(s.tierBlendCm-s.tierBlendCm/2));
                }
                if(!visibility)continue;
                if (x-cell/2<-kWorldLimitCm+100 ||
                    z-cell/2<-kWorldLimitCm+100 ||
                    x+cell/2>kWorldLimitCm-100 ||
                    z+cell/2>kWorldLimitCm-100)continue;

                const uint32_t firstSlot=
                    (atlasBase+iz*count+ix)*s.orientationViews;
                const uint32_t slotsPerRow=atlasSide/s.atlasTilePixels;
                const uint32_t px=(firstSlot%slotsPerRow)*s.atlasTilePixels;
                const uint32_t py=(firstSlot/slotsPerRow)*s.atlasTilePixels;
                const uint32_t sideSlot=firstSlot+
                    (s.orientationViews==2?1u:0u);
                const uint32_t pxSide=(sideSlot%slotsPerRow)*s.atlasTilePixels;
                const uint32_t pySide=(sideSlot/slotsPerRow)*s.atlasTilePixels;
                if(compatible && !dirtyAt(x,z,cell)) {
                    const auto old=original.find(ClusterId{x,z,cell});
                    if(old!=original.end())out.clusters.push_back(old->second);
                    continue; // old atlas pixels and metadata remain valid
                }
                if(compatible) {
                    for(uint32_t view=0;view<s.orientationViews;++view) {
                        const uint32_t slot=firstSlot+view;
                        const uint32_t tileX=(slot%slotsPerRow)*s.atlasTilePixels;
                        const uint32_t tileY=(slot/slotsPerRow)*s.atlasTilePixels;
                        for(uint32_t row=0;row<s.atlasTilePixels;++row) {
                            const size_t at=(size_t(tileY+row)*atlasSide+tileX)*4;
                            std::fill_n(out.atlasRgba.begin()+at,
                                        size_t(s.atlasTilePixels)*4,uint8_t(0));
                        }
                    }
                }
                // Coarse vegetation sample: one evaluation per column. Never
                // instantiate individual distant trees or their ECS components.
                const auto biome=biomeSample(x,z);
                const uint32_t density=horizon_detail::canopyStrength(biome);
                if (!density)continue;
                const uint64_t tileHash=detail::hash2(gx,gz,terrain.seed^salt);
                if ((tileHash&255u)>=density)continue;
                int32_t low=std::numeric_limits<int32_t>::max();
                int32_t high=std::numeric_limits<int32_t>::min();
                for (int a=-1;a<=1;++a)for(int b=-1;b<=1;++b) {
                    const int32_t h=heightSample(x+a*cell/3,z+b*cell/3);
                    low=std::min(low,h);high=std::max(high,h);
                }
                const int32_t bottom=low-250;
                const int32_t top=high+s.canopyHeightCm+500;
                if (top-bottom>200000)continue; // prevent absurd giant billboard
                // Atlas position is derived from the fixed region lattice
                // rather than the number of visible clusters. Changing a
                // road mask or biome can no longer move unrelated UV tiles.
                for(uint32_t orientation=0;orientation<s.orientationViews;++orientation) {
                    const uint32_t tileSlot=firstSlot+orientation;
                    const uint32_t tilePx=(tileSlot%slotsPerRow)*s.atlasTilePixels;
                    const uint32_t tilePy=(tileSlot/slotsPerRow)*s.atlasTilePixels;
                // A canopy is a mixture of small overlapping crowns, not
                // one vertical rectangle per image column. All shapes use
                // stable world-cell hashes and integer pixel coverage:
                // camera motion cannot change their profile until a whole
                // region is explicitly rebaked.
                const int32_t heightRange=std::max(1,top-bottom);
                const uint32_t p=s.atlasTilePixels;
                const uint32_t crownCount=3u+(p/6u)*s.crownDetail;
                for(uint32_t crown=0;crown<crownCount;++crown) {
                    const uint64_t h=detail::hash2(
                        gx*257+int64_t(crown),gz*131+int64_t(crown)/3,
                        terrain.seed^salt^0x4c45414643524f57ULL^
                        (uint64_t(orientation)*0x9e3779b97f4a7c15ULL));
                    const uint32_t column=2u+static_cast<uint32_t>(
                        (h&65535ULL)*(p-4u)/65536ULL);
                    const int64_t axisSample=
                        int64_t(column)*cell/(p-1);
                    const int64_t sampleX=orientation==0 ?
                        gx*cell+axisSample : x;
                    const int64_t sampleZ=orientation==1 ?
                        gz*cell+axisSample : z;
                    if (!canopyAllowed(sampleX, sampleZ)) continue;
                    const auto subBiome=biomeSample(sampleX,sampleZ);
                    const bool needleShape=subBiome==Biome::Tundra ||
                        (subBiome==Biome::Forest&&((h>>47)&3ULL)==0);
                    const uint32_t seasonalCover=canopyCoverPermille(
                        phenology,needleShape);
                    const uint32_t coverage=static_cast<uint32_t>(
                        uint64_t(horizon_detail::canopyStrength(subBiome))*
                        seasonalCover/1000u);
                    if (((h>>16)&255ULL)>=coverage)continue;
                    const int32_t surface=heightSample(sampleX,sampleZ);
                    const int32_t crownCm=std::max<int32_t>(220,
                        int64_t(s.canopyHeightCm)*
                        (68+int32_t((h>>27)%60))/100);
                    const int32_t heightCenter=surface+crownCm*66/100-bottom;
                    const int32_t imageY=int32_t(p-1)-
                        int32_t(int64_t(heightCenter)*p/heightRange);
                    const int32_t rx=std::max<int32_t>(2,int32_t(p/11u)+
                        int32_t((h>>40)%4));
                    const int32_t ry=std::clamp<int32_t>(
                        int32_t(int64_t(crownCm)*p*52/
                                (int64_t(heightRange)*100)),2,
                        std::max<int32_t>(2,int32_t(p/3u)));
                    const int32_t loX=std::max<int32_t>(1,int32_t(column)-rx);
                    const int32_t hiX=std::min<int32_t>(int32_t(p)-2,
                                                        int32_t(column)+rx);
                    const int32_t loY=std::max<int32_t>(1,imageY-ry);
                    const int32_t hiY=std::min<int32_t>(int32_t(p)-2,imageY+ry);
                    for(int32_t row=loY;row<=hiY;++row) {
                        const int32_t dy=row-imageY;
                        // Conifer tapers at the top while broadleaf crowns
                        // remain rounded/elliptical.
                        const int32_t width=needleShape ?
                            std::max<int32_t>(1,rx*(dy+ry+1)/(2*ry+1)) : rx;
                        for(int32_t col=loX;col<=hiX;++col) {
                            const int32_t dx=col-int32_t(column);
                            const int64_t ellipse=
                                int64_t(dx)*dx*4096/(int64_t(width)*width)+
                                int64_t(dy)*dy*4096/(int64_t(ry)*ry);
                            if(ellipse>4096)continue;
                            // One-pixel soft coverage for alpha mask filtering.
                            const uint32_t alpha=ellipse<=3277 ? 255u :
                                static_cast<uint32_t>(
                                    (4096-ellipse)*255/819);
                            if(alpha<128u)continue;
                            // Stable world-space pixel threshold. A single
                            // shared atlas remains sufficient; coverage in
                            // the transition band has no extra geometry.
                            if(visibility<255u &&
                                (detail::hash2(gx*4096+col,gz*4096+row,
                                    terrain.seed^salt^0x4449544845524c4fULL)
                                    &255ULL)>=visibility)continue;
                            const size_t at=(size_t(tilePy+uint32_t(row))*
                                atlasSide+tilePx+uint32_t(col))*4;
                            const int32_t shade=std::clamp<int32_t>(
                                12-int32_t(ellipse/260)+
                                (imageY-row)*3+int32_t((h>>52)%17)-8,
                                -30,40);
                            const int32_t baseGreen=subBiome==Biome::Tundra
                                ? 92 : subBiome==Biome::Wetland ? 102 : 89;
                            const uint8_t green=static_cast<uint8_t>(
                                std::clamp(baseGreen+shade,40,158));
                            const uint8_t red=static_cast<uint8_t>(
                                std::clamp(28+shade/3,14,68));
                            const uint8_t blue=static_cast<uint8_t>(
                                std::clamp(25+shade/4,16,65));
                            // Painter's order is stable; silhouettes overlap
                            // naturally and do not add card geometry.
                            if(alpha>=out.atlasRgba[at+3] ||
                                ((h>>55)&1ULL)==0) {
                                out.atlasRgba[at]=tintCanopyChannel(
                                    red,phenology.tintRPermille);
                                out.atlasRgba[at+1]=tintCanopyChannel(
                                    green,phenology.tintGPermille);
                                out.atlasRgba[at+2]=tintCanopyChannel(
                                    blue,phenology.tintBPermille);
                                out.atlasRgba[at+3]=static_cast<uint8_t>(alpha);
                            }
                        }
                    }
                    // Occasional thin exposed trunk helps break the flat
                    // "hedge wall" outline, without generating real trees.
                    if((h&3ULL)==0) {
                        const int32_t groundRow=int32_t(p-1)-
                            int32_t(int64_t(surface-bottom)*p/heightRange);
                        const int32_t trunkStart=std::max(imageY,0);
                        for(int32_t row=trunkStart;row<=std::min<int32_t>(
                                groundRow,int32_t(p)-2);++row) {
                            const size_t at=(size_t(tilePy+uint32_t(row))*
                                atlasSide+tilePx+column)*4;
                            if(out.atlasRgba[at+3])continue;
                            out.atlasRgba[at]=47;
                            out.atlasRgba[at+1]=37;
                            out.atlasRgba[at+2]=24;
                            out.atlasRgba[at+3]=255;
                        }
                    }
                }
                }
                out.clusters.push_back({x,z,cell,bottom,top,
                    px,py,pxSide,pySide,s.atlasTilePixels,
                    static_cast<uint8_t>(density)});
            }
    };
    addLayer(s.middleTileCm,s.middleTilesPerSide,
             std::max(0,s.closeForestEndCm-s.nearBlendCm),
             s.middleForestEndCm+s.tierBlendCm/2,
             0x9c6f54574d49444dULL,true,0u);
    const int64_t halfTerrainSide=regionSide/2;
    // A snapped terrain heightfield may be offset from the camera by
    // several grid steps. The far canopy ring must remain inside the
    // guaranteed covered terrain footprint, including its one-grid-step
    // rounding margin, or a bare sky/forest crack can appear at one edge.
    const int64_t terrainStep=(regionSide+s.terrainResolution-1)/
                              s.terrainResolution;
    const int64_t maxCenterOffset=
        (int64_t(s.terrainGridSnapSteps)+1)*terrainStep/2;
    const int64_t safeTerrainRadius=std::max<int64_t>(
        s.middleForestEndCm+1,halfTerrainSide-maxCenterOffset);
    const int32_t farRadius=static_cast<int32_t>(
        s.distantTerrain
        ? std::min<int64_t>(halfTerrainSide,safeTerrainRadius)
        : halfTerrainSide);
    addLayer(s.distantTileCm,s.distantTilesPerSide,
             s.middleForestEndCm-s.tierBlendCm/2,
             farRadius,0x9c6f54574641524dULL,false,
             s.middleTilesPerSide*s.middleTilesPerSide);

    if(s.distantTerrain) {
        const uint32_t n=s.terrainResolution;
        if(source && source->ownsNearTerrainRect) {
            if(compatible)out.nearOwnedQuads=previous->nearOwnedQuads;
            else out.nearOwnedQuads.resize(size_t(n)*n,0);
            for(uint32_t z=0;z<n;++z)
                for(uint32_t x=0;x<n;++x) {
                    const int64_t minX=lattice.worldX(x);
                    const int64_t minZ=lattice.worldZ(z);
                    const int64_t maxX=lattice.worldX(int64_t(x)+1);
                    const int64_t maxZ=lattice.worldZ(int64_t(z)+1);
                    if(compatible && !dirtyAt((minX+maxX)/2,
                                              (minZ+maxZ)/2,
                                              regionSide/n+1))
                        continue;
                    out.nearOwnedQuads[size_t(z)*n+x]=
                        source->ownsNearTerrainRect(minX,minZ,maxX,maxZ)
                            ? uint8_t(1):uint8_t(0);
                }
        }
        if(compatible)out.terrainVertices=previous->terrainVertices;
        else out.terrainVertices.resize(size_t(n+1)*(n+1));
        for(uint32_t z=0;z<=n;++z)
            for(uint32_t x=0;x<=n;++x) {
                const int64_t wx=lattice.worldX(x),wz=lattice.worldZ(z);
                // Every normal needs ghost-neighbor heights, including
                // outside this region. Clamp only at global world limits.
                const int64_t xL=std::clamp(lattice.worldX(int64_t(x)-1),
                    -kWorldLimitCm,kWorldLimitCm);
                const int64_t xR=std::clamp(lattice.worldX(int64_t(x)+1),
                    -kWorldLimitCm,kWorldLimitCm);
                const int64_t zL=std::clamp(lattice.worldZ(int64_t(z)-1),
                    -kWorldLimitCm,kWorldLimitCm);
                const int64_t zR=std::clamp(lattice.worldZ(int64_t(z)+1),
                    -kWorldLimitCm,kWorldLimitCm);
                const int64_t pad=regionSide/n+1;
                if(compatible && !dirtyAt(wx,wz,pad))continue;
                const int32_t h=heightSample(wx,wz);
                const auto biome=biomeSample(wx,wz);
                const uint32_t color=horizon_detail::biomeColor(
                    biome,h,detail::hash2(
                        detail::floorDiv(wx,51200),
                        detail::floorDiv(wz,51200),terrain.seed));
                const auto slope=[&](int32_t l,int32_t r,int64_t distance) {
                    if(distance<=0)return int32_t(0);
                    return static_cast<int32_t>(std::clamp<int64_t>(
                        (int64_t(l)-r)*1000/distance,-1000000,1000000));
                };
                const int32_t sx=slope(heightSample(xL,wz),
                                        heightSample(xR,wz),xR-xL);
                const int32_t sz=slope(heightSample(wx,zL),
                                        heightSample(wx,zR),zR-zL);
                out.terrainVertices[size_t(z)*(n+1)+x]={
                    wx,wz,h,color,sx,sz};
            }
    }
    return out;
}

} // namespace aiverse::terraweave
