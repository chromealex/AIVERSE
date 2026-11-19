#include "engine/render/TerraWeaveHorizonModel.h"
#include "engine/render/TerraWeaveHorizonScene.h"
#include "engine/world/TerraWeaveWorldHeightSource.h"
#include "engine/render/TerraWeaveHorizonClip.h"
#include <optional>
#include <utility>
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <stdexcept>
#include <set>
#include <vector>

using namespace aiverse::terraweave;

int main() {
    TerrainSettings terrain;
    terrain.seed=1537;
    terrain.baseHeightCm=450;
    terrain.reliefCm=0;
    BiomeSettings climate;
    climate.baseHumidity=900;
    climate.variation=0;
    HorizonSettings settings;
    settings.middleTileCm=8000;
    settings.middleTilesPerSide=12;
    settings.distantTileCm=32000;
    settings.distantTilesPerSide=12;
    settings.closeForestEndCm=8000;
    settings.middleForestEndCm=30000;
    settings.terrainBeginCm=25000;
    settings.terrainResolution=12;
    settings.atlasTilePixels=16;
    settings.canopyHeightCm=1000;

    // The conservative budget includes authored near-ground clipping,
    // its inner lip and the terrain skirt in addition to the two crossed
    // forest cards per cluster. The old terrain+forest-only bound omitted
    // these optional triangles and rejected otherwise valid configurations.
    const auto expectedMaximumTriangles=[](const HorizonSettings& s,
                                           size_t clusters)->size_t {
        const size_t terrain=s.distantTerrain ?
            size_t(s.terrainResolution)*s.terrainResolution*2u +
            (s.terrainSkirtDepthCm>0 ? size_t(8)*s.terrainResolution : 0u) +
            (s.innerTerrainLipCm>0 ? 1024u : 0u) +
            s.maxNearClipTriangles : 0u;
        return terrain+clusters*4u;
    };
    const auto budget=estimateHorizonBudget(settings);
    assert(budget.atlasBytes<=16u*1024u*1024u);
    assert(budget.maxTriangles==
           expectedMaximumTriangles(settings,budget.maxForestClusters));
    const HorizonSettings defaultSettings{};
    const auto defaults=estimateHorizonBudget(defaultSettings);
    assert(defaults.atlasBytes<=8u*1024u*1024u);
    assert(defaults.maxTriangles==
           expectedMaximumTriangles(defaultSettings,defaults.maxForestClusters));
    assert(defaults.maxTriangles==11392u); // terrain + skirt + lip + clip + forest
    assert(defaults.maxForestClusters==832);
    HorizonSettings dualDefault;
    dualDefault.orientationViews=2;
    const auto dualBudget=estimateHorizonBudget(dualDefault);
    assert(dualBudget.atlasBytes<=16u*1024u*1024u);
    assert(dualBudget.atlasBytes>defaults.atlasBytes);
    assert(dualBudget.maxTriangles==defaults.maxTriangles);

    const auto bake=bakeHorizon(terrain,climate,settings,0,0);
    assert(bake.atlasWidth==bake.atlasHeight);
    assert(bake.atlasRgba.size()==size_t(bake.atlasWidth)*bake.atlasHeight*4);
    assert(!bake.clusters.empty());
    assert(bake.terrainVertices.size()==13*13);
    for(const auto& cluster:bake.clusters) {
        assert(cluster.atlasX+cluster.atlasSize<=bake.atlasWidth);
        assert(cluster.atlasY+cluster.atlasSize<=bake.atlasHeight);
        assert(cluster.topCm>cluster.bottomCm);
        const int64_t dx=cluster.centerXcm, dz=cluster.centerZcm;
        const int64_t visibleStart=std::max<int32_t>(
            0,settings.closeForestEndCm-settings.nearBlendCm);
        assert(dx*dx+dz*dz>=visibleStart*visibleStart);
    }
    uint32_t coverage=0;
    for(size_t i=3;i<bake.atlasRgba.size();i+=4)
        coverage+=bake.atlasRgba[i]>0;
    assert(coverage>0);
    // Crowns must be shaped, not uniform solid columns/hedge stripes.
    std::set<uint32_t> leafColours;
    for(size_t i=0;i<bake.atlasRgba.size();i+=4) {
        if (bake.atlasRgba[i+3]>=128) {
            const uint32_t color=uint32_t(bake.atlasRgba[i]) |
                (uint32_t(bake.atlasRgba[i+1])<<8) |
                (uint32_t(bake.atlasRgba[i+2])<<16);
            leafColours.insert(color);
        }
    }
    assert(leafColours.size()>25);
    assert(horizon_detail::smoothCoverage(50,100,200)==0);
    assert(horizon_detail::smoothCoverage(200,100,200)==255);
    assert(horizon_detail::smoothCoverage(150,100,200)>110);
    assert(horizon_detail::smoothCoverage(150,100,200)<145);
    const auto repeat=bakeHorizon(terrain,climate,settings,0,0);
    assert(bake.atlasRgba==repeat.atlasRgba);
    assert(bake.clusters.size()==repeat.clusters.size());
    for(size_t i=0;i<bake.clusters.size();++i) {
        const auto&a=bake.clusters[i],&b=repeat.clusters[i];
        assert(a.centerXcm==b.centerXcm);
        assert(a.centerZcm==b.centerZcm);
        assert(a.atlasX==b.atlasX&&a.atlasY==b.atlasY);
        assert(a.bottomCm==b.bottomCm&&a.topCm==b.topCm);
    }

    auto model=makeHorizonModel(bake);
    assert(model);
    assert(model->nodes.size()==1);
    assert(model->meshes.size()==1);
    assert(model->materials.size()==2);
    assert(model->images.size()==1);
    assert(model->images[0].rgba==bake.atlasRgba);
    assert(model->materials[1].alpha==aiverse::render::AlphaMode::Mask);
    assert(model->meshes[0].primitives.size()==2);
    // The terrain primitive may include an outer skirt, bounded inner
    // seam lip, or clipped near-ground triangles; the historical
    // (n+1)^2 + 8*clusters + 8 bound ignored those valid vertices.
    // Verify actual forest layout and the configured conservative
    // whole-model budget instead of assuming no terrain additions.
    const auto& groundPart=model->meshes[0].primitives.front();
    const auto& canopyPart=model->meshes[0].primitives.back();
    assert(groundPart.material==0);
    assert(canopyPart.material==1);
    assert(groundPart.firstVertex==0);
    assert(canopyPart.firstVertex==groundPart.vertexCount);
    assert(canopyPart.vertexCount==bake.clusters.size()*8u);
    assert(model->vertices.size()==
           size_t(canopyPart.firstVertex)+canopyPart.vertexCount);
    assert(model->vertices.size()>=
           bake.terrainVertices.size()+bake.clusters.size()*8u);
    assert(model->vertices.size()<=budget.maxVertices);
    for(const auto& p:model->meshes[0].primitives) {
        assert(p.indexCount>0);
        assert(p.indexCount%3==0);
        assert(p.firstIndex+p.indexCount<=model->indices.size());
        for(uint32_t j=p.firstIndex;j<p.firstIndex+p.indexCount;++j)
            assert(model->indices[j]<model->vertices.size());
    }
    assert(model->meshes[0].primitives[1].indexCount ==
           bake.clusters.size()*12u); // two crossed cards per cluster
    assert(!model->batching.autoLod);

    // Quality tier: two distinct 90-degree atlas captures, same mesh
    // and triangle counts, still one model / one atlas / two materials.
    HorizonSettings dualSettings=settings;
    dualSettings.orientationViews=2;
    const auto sideBake=bakeHorizon(terrain,climate,dualSettings,0,0);
    assert(sideBake.clusters.size()==bake.clusters.size());
    assert(sideBake.atlasWidth>bake.atlasWidth);
    assert(sideBake.atlasRgba!=bake.atlasRgba);
    for (const auto& cluster:sideBake.clusters) {
        assert(cluster.atlasX!=cluster.atlasSideX ||
               cluster.atlasY!=cluster.atlasSideY);
        assert(cluster.atlasSideX+cluster.atlasSize<=sideBake.atlasWidth);
        assert(cluster.atlasSideY+cluster.atlasSize<=sideBake.atlasHeight);
    }
    const auto sideModel=makeHorizonModel(sideBake);
    assert(sideModel->meshes[0].primitives.size()==
           model->meshes[0].primitives.size());
    assert(sideModel->vertices.size()==model->vertices.size());
    assert(sideModel->indices==model->indices);
    assert(sideModel->images.size()==1);
    assert(sideModel->materials.size()==2);
    const auto& forestPart=sideModel->meshes[0].primitives.back();
    assert(forestPart.vertexCount>=8);
    const auto& nearUv=sideModel->vertices[forestPart.firstVertex].uv;
    const auto& sideUv=sideModel->vertices[forestPart.firstVertex+4].uv;
    assert(nearUv.x!=sideUv.x || nearUv.y!=sideUv.y);


    bool rejected=false;
    auto bad=settings;
    bad.atlasTilePixels=128;
    try {validateHorizonSettings(bad);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    rejected=false;
    bad=settings;
    bad.crownDetail=0;
    try {validateHorizonSettings(bad);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    rejected=false;
    bad=settings;
    bad.tierBlendCm=settings.middleForestEndCm;
    try {validateHorizonSettings(bad);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    rejected=false;
    bad=settings;
    bad.orientationViews=3;
    try {validateHorizonSettings(bad);}
    catch(const std::invalid_argument&){rejected=true;}
    assert(rejected);
    rejected=false;
    try{(void)bakeHorizon(terrain,climate,settings,INT64_MAX,0);}
    catch(const std::out_of_range&){rejected=true;}
    assert(rejected);

    // Stage 22 regression: signed authored Q16 sampling and normals spanning
    // two resident tiles must agree with the same two-triangle collision
    // surface. Heights increase by 100 cm per 1000 cm along world X.
    constexpr int64_t q16=65536;
    AuthoredWorldHeightSnapshot neighborHeights(10); // 1000 cm per tile
    neighborHeights.setTile({0,0,0},AuthoredHeightTileCm{
        2,{0,100,0,100}});
    neighborHeights.setTile({1,0,0},AuthoredHeightTileCm{
        2,{100,200,100,200}});
    const auto continuous=neighborHeights.continuousSlopeQ16(
        1000*q16,500*q16);
    assert(continuous.has_value());
    assert(continuous->first==-100 && continuous->second==0);
    assert(neighborHeights.sampleQ16(1000*q16,500*q16)==100);
    assert(!neighborHeights.continuousSlopeQ16(1000*q16,500*q16,0));
    assert(!neighborHeights.continuousSlopeQ16(1000*q16,500*q16,4097));
    assert(!neighborHeights.continuousSlopeQ16(1000*q16,500*q16,200,101));

    AuthoredWorldHeightSnapshot missingNeighbor(10);
    missingNeighbor.setTile({0,0,0},AuthoredHeightTileCm{
        2,{0,100,0,100}});
    assert(!missingNeighbor.continuousSlopeQ16(999*q16,500*q16));

    AuthoredWorldHeightSnapshot cliff(10);
    cliff.setTile({0,0,0},AuthoredHeightTileCm{
        2,{0,100,0,100}});
    cliff.setTile({1,0,0},AuthoredHeightTileCm{
        2,{500,600,500,600}});
    assert(!cliff.continuousSlopeQ16(1000*q16,500*q16));

    // A negative tile boundary is not special: floor-div and Q16
    // interpolation must preserve the signed world coordinate convention.
    AuthoredWorldHeightSnapshot negativeNeighbor(10);
    negativeNeighbor.setTile({-1,0,0},AuthoredHeightTileCm{
        2,{-100,0,-100,0}});
    negativeNeighbor.setTile({0,0,0},AuthoredHeightTileCm{
        2,{0,100,0,100}});
    const auto negativeSlope=negativeNeighbor.continuousSlopeQ16(
        0,500*q16);
    assert(negativeSlope.has_value());
    assert(negativeSlope->first==-100 && negativeSlope->second==0);
    assert(negativeNeighbor.sampleQ16(-500*q16,500*q16)==-50);
    assert(negativeNeighbor.sampleQ16(-500*q16,500*q16)==
           negativeNeighbor.sample(-500,500));

    // TWH8 intentionally stores the normal values but not whether they
    // came from the neighbor stencil or tile-local fallback. Archive hits
    // must report unknown provenance rather than an observed zero.
    auto provenanceBake=bake;
    provenanceBake.nearNormalProvenanceAvailable=true;
    provenanceBake.continuousNearNormalSamples=3;
    provenanceBake.localFallbackNearNormalSamples=2;
    const uint64_t fingerprint=horizonArchiveFingerprint(
        terrain,climate,settings);
    const auto archiveBytes=encodeHorizonArchive(
        provenanceBake,fingerprint);
    const auto archiveRestored=decodeHorizonArchive(
        archiveBytes,fingerprint);
    assert(!archiveRestored.nearNormalProvenanceAvailable);
    assert(archiveRestored.continuousNearNormalSamples==0);
    assert(archiveRestored.localFallbackNearNormalSamples==0);

    // Stage 23: clipped seam provenance is all-or-none. CPU-only fixture
    // carved inside a distant terrain quad; no live GPU device is required.
    auto clipped=bake;
    clipped.nearOwnedQuads.clear();
    clipped.maxNearClipTriangles=4096;
    clipped.maxNearWeldSlopePermille=2500;
    const auto& tl=bake.terrainVertices[0];
    const auto& tr=bake.terrainVertices[1];
    const auto& bl=bake.terrainVertices[bake.terrainResolution+1];
    const int64_t width=int64_t(tr.xCm)-tl.xCm;
    const int64_t depth=int64_t(bl.zCm)-tl.zCm;
    assert(width>0 && depth>0);
    clipped.nearClipRects={RectCm{
        tl.xCm+width/3,tl.zCm+depth/3,
        tl.xCm+width*2/3,tl.zCm+depth*2/3}};
    const auto clipPlan=planHorizonNearGroundClipping(clipped);
    assert(clipPlan.has_value() && !clipPlan->triangles.empty());

    HorizonSurfaceSources clippedSource;
    clippedSource.nearGroundHeightAtQ16Cm=[](
            int64_t,int64_t)->std::optional<int32_t> {
        return 450; // identical to the source's flat distant terrain
    };
    clippedSource.nearGroundContinuousSlopeAtQ16Cm=[](
            int64_t,int64_t)
            ->std::optional<std::pair<int32_t,int32_t>> {
        return std::pair<int32_t,int32_t>{-100,0};
    };
    auto continuousSeam=clipped;
    populateHorizonNearSeamSamples(continuousSeam,&clippedSource);
    assert(!continuousSeam.nearSeamSamples.empty());
    assert(continuousSeam.nearNormalProvenanceAvailable);
    assert(continuousSeam.continuousNearNormalSamples==
           continuousSeam.nearSeamSamples.size());
    assert(continuousSeam.localFallbackNearNormalSamples==0);

    // TWH8 stores the actual near-seam normal slopes, but intentionally
    // omits CPU-only provenance counters. Encode the *populated* seam rather
    // than a bake with no authored near boundary samples.
    const auto seamBytes=encodeHorizonArchive(continuousSeam,fingerprint);
    const auto seamRestored=decodeHorizonArchive(seamBytes,fingerprint);
    assert(seamRestored.nearSeamSamples.size()==
           continuousSeam.nearSeamSamples.size());
    for(size_t i=0;i<continuousSeam.nearSeamSamples.size();++i) {
        const auto& src=continuousSeam.nearSeamSamples[i];
        const auto& dst=seamRestored.nearSeamSamples[i];
        assert(dst.xQ16cm==src.xQ16cm);
        assert(dst.zQ16cm==src.zQ16cm);
        assert(dst.nearHeightCm==src.nearHeightCm);
        assert(dst.nearNormalValid==src.nearNormalValid);
        assert(dst.nearSlopeXPermille==src.nearSlopeXPermille);
        assert(dst.nearSlopeZPermille==src.nearSlopeZPermille);
    }
    assert(!seamRestored.nearNormalProvenanceAvailable);
    assert(seamRestored.continuousNearNormalSamples==0);
    assert(seamRestored.localFallbackNearNormalSamples==0);
    auto corruptSeamBytes=seamBytes;
    corruptSeamBytes.back()^=0x80;
    bool badSeamRejected=false;
    try { (void)decodeHorizonArchive(corruptSeamBytes,fingerprint); }
    catch(const std::invalid_argument&) { badSeamRejected=true; }
    assert(badSeamRejected);
    badSeamRejected=false;
    try { (void)decodeHorizonArchive(seamBytes,fingerprint^1ULL); }
    catch(const std::invalid_argument&) { badSeamRejected=true; }
    assert(badSeamRejected);

    auto localOnly=clippedSource;
    localOnly.nearGroundContinuousSlopeAtQ16Cm={};
    auto localSeam=clipped;
    populateHorizonNearSeamSamples(localSeam,&localOnly);
    assert(localSeam.nearNormalProvenanceAvailable);
    assert(localSeam.continuousNearNormalSamples==0);
    assert(localSeam.localFallbackNearNormalSamples==
           localSeam.nearSeamSamples.size());

    // A missing second height sample must discard the previous valid
    // sample's diagnostic counts rather than claiming a partial weld.
    auto incompleteSource=clippedSource;
    int heightCalls=0;
    incompleteSource.nearGroundHeightAtQ16Cm=[&heightCalls](
            int64_t,int64_t)->std::optional<int32_t> {
        ++heightCalls;
        if(heightCalls==2)return std::nullopt;
        return 450;
    };
    auto incompleteSeam=clipped;
    populateHorizonNearSeamSamples(incompleteSeam,&incompleteSource);
    assert(heightCalls>=2);
    assert(incompleteSeam.nearSeamSamples.empty());
    assert(!incompleteSeam.nearNormalProvenanceAvailable);
    assert(incompleteSeam.continuousNearNormalSamples==0);
    assert(incompleteSeam.localFallbackNearNormalSamples==0);

    // Reject a steep newly-welded wedge atomically. Its calculated
    // normals must not be counted as actually applied.
    auto steepSource=clippedSource;
    steepSource.nearGroundHeightAtQ16Cm=[](
            int64_t,int64_t)->std::optional<int32_t> {return 500000;};
    auto steepSeam=clipped;
    populateHorizonNearSeamSamples(steepSeam,&steepSource);
    assert(steepSeam.nearWeldSlopeFallback);
    assert(steepSeam.nearSeamSamples.empty());
    assert(steepSeam.continuousNearNormalSamples==0);
    assert(steepSeam.localFallbackNearNormalSamples==0);

    // Slope-guarded fallback remains serializable with no half-welded seam.
    const auto guardedBytes=encodeHorizonArchive(steepSeam,fingerprint);
    const auto guardedRestored=decodeHorizonArchive(guardedBytes,fingerprint);
    assert(guardedRestored.nearWeldSlopeFallback);
    assert(guardedRestored.nearSeamSamples.empty());

    // Constructor should prepare a *bounded* async scheduler without requiring
    // an initialized graphics device. Actual GPU upload has a separate test.
    TerraWeaveHorizonScene scene(terrain,climate,settings);
    assert(!scene.ready());
    assert(scene.clusterCount()==0);
    const auto uninitialized=scene.runtimeStats();
    assert(uninitialized.clusters==0);
    assert(uninitialized.atlasBytes==0);
    assert(uninitialized.triangles==0);
    assert(uninitialized.cpuPrepareMs==0.0);
    assert(uninitialized.gpuUploadMs==0.0);
    assert(!uninitialized.requestedRegionPresent);
    assert(!uninitialized.requestedRegionPending);
    assert(!uninitialized.activeRegionPresent);
    assert(!uninitialized.activeRegionResolved);
    assert(!uninitialized.activeRegionEmpty);
    assert(!uninitialized.staleProxy);
    assert(!uninitialized.nearNormalProvenanceAvailable);
}
