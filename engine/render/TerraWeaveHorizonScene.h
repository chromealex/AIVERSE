// TerraWeave Horizon: async region baking with ONE resident model and atlas.
// Reuses AIVERSE AsyncChunkStream for background CPU work; only the owning
// render thread uploads and releases GPU buffers. The previous GPU proxy
// stays displayed until a new region has been successfully uploaded.
#pragma once
#include "engine/render/TerraWeaveHorizonModel.h"
#include "engine/render/TerraWeaveHorizonGpuPatch.h"
#include "engine/render/TerraWeaveHorizonVertexPatch.h"
#include "engine/render/TerraWeaveHorizonGpuCache.h"
#include "engine/render/TerraWeaveVertexWind.h"
#include "engine/world/TerraWeaveWindField.h"
#include "engine/world/TerraWeaveHorizonPresentationQuality.h"
#include "engine/world/TerraWeaveHorizonAdaptiveQuality.h"
#include "engine/world/TerraWeaveHorizonArchive.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/AsyncChunkStream.h"
#include "engine/render/TerraWeaveForestAdapter.h"
#include <atomic>
#include <cstdint>
#include <chrono>
#include <limits>
#include <memory>
#include <optional>
#include <functional>
#include <vector>
#include <map>
#include <mutex>
#include <stdexcept>
#include <span>

namespace aiverse::terraweave {

// Host-owned asset plumbing for static worlds; all callbacks execute on
// the bounded Horizon CPU worker. Paths/storage belong to the game, not RHI.
struct HorizonArchiveStore {
    std::function<bool(int32_t cellX,int32_t cellZ,
                       std::vector<uint8_t>& archive)> read;
    std::function<void(int32_t cellX,int32_t cellZ,
                       const std::vector<uint8_t>& archive)> write;
    // Stable content hash of authored terrain/biome/masks. Set to zero for
    // procedural worlds; with live authored sources, zero disables disk reuse.
    uint64_t authoredContentHash=0;
};

struct HorizonRuntimeStats {
    size_t clusters=0, atlasBytes=0, vertices=0, triangles=0;
    double cpuPrepareMs=0, gpuUploadMs=0;
    size_t preparing=0, discarded=0; // stream status on owning thread
    size_t cachedRegions=0, cacheHits=0; // bounded CPU bake reuse
    size_t archiveHits=0; // full re-use across game launches, when configured
    size_t atlasPatchCount=0, atlasPatchBytes=0; // successful incremental RHI uploads
    size_t atlasPatchRegions=0, fullUploadCount=0; // last patch region count, full model uploads
    size_t vertexPatchCount=0, vertexPatchBytes=0; // successful partial VB commits
    size_t vertexPatchRanges=0; // last successful vertex update run count
    size_t gpuCachedRegions=0, gpuCachedBytes=0, gpuCacheHits=0; // extra GPU residency
    size_t gpuCacheEvictions=0; // LRU and stale-revision RHI retirements
    size_t gpuCachedWindShadowBytes=0; // inactive cached CPU wind shadow capacity
    size_t gpuWindShadowBudgetEvictions=0; // whole GPU entries retired for CPU budget
    size_t gpuCachedCompressedWindShadows=0; // live inactive compressed entries
    size_t gpuWindShadowCompressions=0; // successful CPU-only pack operations
    size_t gpuWindShadowRestores=0; // exact CPU restoration on GPU cache hit
    size_t gpuWindShadowDecodeFailures=0; // corrupt GPU LRU entries retired
    size_t windOverlayPatchWrites=0, windOverlayPatchBytes=0;
    size_t windOverlayPatchRanges=0, windOverlayChangedClusters=0;
    size_t windOverlayFullRangeFallbacks=0, windOverlayNoopRevisions=0;
    // Stage39 effective presentation quality, including manual overrides.
    size_t windUploadBudgetBytes=0, cachedWindShadowBudgetBytes=0;
    uint32_t windUploadSparseRangeCap=0, windUploadFullFallbackPercent=0;
    bool windShadowCompressionEnabled=false, spatialWindVariationEnabled=false;
    // Host-supplied frame timing only; never fabricate GPU measurements.
    bool adaptiveQualityEnabled=false;
    HorizonPresentationQualityPreset adaptiveQualityLevel=
        HorizonPresentationQualityPreset::Balanced;
    uint64_t adaptiveQualitySamples=0, adaptiveQualitySwitches=0;
    uint64_t adaptiveQualityDowngrades=0, adaptiveQualityUpgrades=0;
    uint32_t adaptiveQualityLastFrameUs=0;
    bool windOverlayPending=false, exactWindFieldActive=false;
    size_t transitionStarts=0;
    size_t nearClipQuads=0, nearClipTriangles=0;
    bool nearClipFallback=false;
    size_t weldedNearVertices=0;
    int32_t maxNearWeldDeltaCm=0;
    size_t smoothedNearNormals=0;
    size_t matchedAuthoredNearNormals=0;
    size_t continuousNearNormalSamples=0;
    size_t localFallbackNearNormalSamples=0;
    bool nearNormalProvenanceAvailable=false;
    uint64_t sourceRevision=0, activeRevision=0;
    bool activeRegionPresent=false, activeRegionResolved=false;
    bool activeRegionEmpty=false, staleProxy=false;
    bool requestedRegionPresent=false, requestedRegionPending=false;
    int32_t requestedCellX=0, requestedCellZ=0;
    int32_t activeCellX=0, activeCellZ=0;
    bool nearWeldSlopeFallback=false;
    bool transitioning=false;
    uint32_t transitionProgressPermille=1000;
};

class TerraWeaveHorizonScene {
public:
    TerraWeaveHorizonScene(TerrainSettings terrain, BiomeSettings biomes,
                           HorizonSettings settings = {})
        : terrain_(terrain), biomes_(biomes), settings_(settings) {
        validateHorizonSettings(settings_);
        world::StreamLimits limits;
        limits.residentChunks=1;
        limits.residentBytes=kRegionBudget;
        limits.retryUpdates=2;
        stream_=std::make_unique<world::AsyncChunkStream>(
            limits, 2*kRegionBudget, 1,
            [this](world::Cell cell)->world::AsyncChunkStream::Payload {
                auto result=std::make_shared<Prepared>();
                const auto start=std::chrono::steady_clock::now();
                // Snap to the same stable middle-ring grid when preparing,
                // preventing a full atlas re-bake at every camera movement.
                const int64_t x=int64_t(cell.x)*settings_.middleTileCm+
                                settings_.middleTileCm/2;
                const int64_t z=int64_t(cell.z)*settings_.middleTileCm+
                                settings_.middleTileCm/2;
                // Retain a coherent generation+surface snapshot per job.
                // An old generation is never served from the current cache.
                const auto generation=std::atomic_load_explicit(&sources_,std::memory_order_acquire);
                const uint64_t revision=generation?generation->revision:0;
                result->sourceRevision=revision; // reject obsolete prepared jobs
                std::shared_ptr<const HorizonBake> baked;
                if(settings_.maxCachedRegions){
                    std::lock_guard lock(cacheMutex_);
                    const auto it=cache_.find(cell);
                    if(it!=cache_.end()&&it->second.revision==revision){
                        baked=it->second.bake;
                        it->second.stamp=++cacheTick_;
                        ++cacheHits_;
                    }
                }
                if(!baked){
                    const auto surface=generation?generation->surface:nullptr;
                    const auto archive=archiveStore_;
                    // A changing authored world may still advertise the
                    // previous static archiveContentHash. Partial updates
                    // always bypass disk archives for the modified region.
                    const bool dirtySource=generation &&
                        !generation->edited.empty();
                    // Static TWH8 archives have no phenology payload or
                    // seasonal fingerprint; never replay a summer atlas
                    // while the runtime requests a different canopy state.
                    const bool seasonalSource=surface &&
                        surface->phenology!=CanopyPhenology{};
                    const bool diskAllowed=archive && !dirtySource &&
                        !seasonalSource &&
                        (!surface || archive->authoredContentHash!=0);
                    const uint64_t fingerprint=diskAllowed ?
                        horizonArchiveFingerprint(terrain_,biomes_,settings_,
                                                  archive->authoredContentHash)
                        : 0;
                    if(diskAllowed && archive->read){
                        try {
                            std::vector<uint8_t> bytes;
                            if(archive->read(cell.x,cell.z,bytes)){
                                auto previous=decodeHorizonArchive(bytes,fingerprint);
                                // Reject a wrong/colliding storage key even
                                // if its settings and payload are valid.
                                if(previous.cameraXcm==x && previous.cameraZcm==z){
                                    baked=std::make_shared<const HorizonBake>(
                                        std::move(previous));
                                    ++archiveHits_;
                                }
                            }
                        } catch(const std::exception&) {
                            // Missing/corrupt/stale cache assets are not a
                            // reason to fail streaming. Generate afresh.
                        }
                    }
                    if(!baked){
                        auto generated=bakeHorizon(
                            terrain_,biomes_,settings_,x,z,surface.get(),
                            generation?generation->previous.get():nullptr,
                            generation?std::span<const RectCm>(
                                generation->edited.data(),generation->edited.size())
                                :std::span<const RectCm>{});
                        // Persist exact near-terrain boundary heights with
                        // the immutable bake. GPU and cache never consult
                        // live streamed WorldScene collision/height data.
                        populateHorizonNearSeamSamples(generated,surface.get());
                        baked=std::make_shared<const HorizonBake>(
                            std::move(generated));
                        if(diskAllowed && archive->write){
                            try {
                                auto bytes=encodeHorizonArchive(*baked,fingerprint);
                                archive->write(cell.x,cell.z,bytes);
                            } catch(const std::exception&) {
                                // Caching is best effort, never gameplay state.
                            }
                        }
                    }
                    if(settings_.maxCachedRegions){
                        std::lock_guard lock(cacheMutex_);
                        if(std::atomic_load_explicit(&sources_,std::memory_order_acquire)==generation){
                            cache_[cell]={baked,revision,++cacheTick_};
                            while(cache_.size()>settings_.maxCachedRegions){
                                auto old=cache_.begin();
                                for(auto it=cache_.begin();it!=cache_.end();++it)
                                    if(it->second.stamp<old->second.stamp)
                                        old=it;
                                cache_.erase(old);
                            }
                        }
                    }
                }
                result->bake=std::move(baked);
                result->model=makeHorizonModel(*result->bake,
                                                &result->clipStats);
                if(result->model)render::prepareModelUpload(*result->model);
                const auto end=std::chrono::steady_clock::now();
                result->cpuPrepareMs=
                    std::chrono::duration<double,std::milli>(end-start).count();
                return result;
            });
    }
    TerraWeaveHorizonScene(const TerraWeaveHorizonScene&)=delete;
    TerraWeaveHorizonScene& operator=(const TerraWeaveHorizonScene&)=delete;
    ~TerraWeaveHorizonScene() {
        if(stream_)stream_->shutdown([](world::Cell){});
        // Call shutdown(device) before destroying the RHI for GPU cleanup.
    }

    // The owner thread publishes an immutable snapshot. Canceled workers
    // retain their captured shared_ptr until they finish; old GPU proxy
    // remains in place until a replacement is uploaded.
    void setSurfaceSources(std::shared_ptr<const HorizonSurfaceSources> source,
                           std::span<const RectCm> edited = {}) {
        auto generation=std::make_shared<SourceGeneration>();
        generation->revision=++sourceRevision_; // owner-thread only
        generation->surface=std::move(source);
        // Keep the previous immutable CPU bake only for small localized
        // landscape edits. The actual GPU model remains available until
        // the updated region is uploaded on the main render thread.
        if(activeBake_ && !edited.empty() && edited.size()<=128) {
            generation->previous=activeBake_;
            generation->edited.assign(edited.begin(),edited.end());
        }
        std::atomic_store_explicit(&sources_,std::shared_ptr<const SourceGeneration>(generation),std::memory_order_release);
        {
            std::lock_guard lock(cacheMutex_);
            cache_.clear(); // old source generation must never escape
        }
        // Decide if the current visible region itself changed. Even when
        // it did not, pending workers preparing ANOTHER region still capture
        // an old source generation and must be invalidated.
        bool affectsActive=true;
        if(activeCell_ && !edited.empty() && activeBake_) {
            affectsActive=false;
            const int64_t x0=activeBake_->originXcm;
            const int64_t z0=activeBake_->originZcm;
            const int64_t x1=x0+activeBake_->sideCm;
            const int64_t z1=z0+activeBake_->sideCm;
            const int64_t pad=settings_.distantTileCm;
            for(const RectCm& r:edited)
                if(r.maxX>x0-pad && r.minX<x1+pad &&
                   r.maxZ>z0-pad && r.minZ<z1+pad) {
                    affectsActive=true;
                    break;
                }
        }
        if(stream_ && cataloged_) {
            stream_->remove(cell_,[](world::Cell){});
            cataloged_=false;
        }
        if(!affectsActive && activeCell_)
            activeRevision_=generation->revision; // unchanged visible region
        // The GPU cache still carries old generation tags; update(device)
        // will release all stale entries on the RHI owner thread. No GPU
        // calls are made here because no device is supplied to this API.
    }
    // Configure optional packed/static game-asset lookup before streaming
    // starts. No implicit filesystem access is ever performed by the engine.
    void setArchiveStore(std::shared_ptr<const HorizonArchiveStore> store) {
        if(streamStarted_)throw std::logic_error(
            "TerraWeave Horizon archive store must be set before update");
        archiveStore_=std::move(store);
    }
    const TerrainSettings& terrainSettings() const {return terrain_;}
    const BiomeSettings& biomeSettings() const {return biomes_;}
    const HorizonSettings& settings() const {return settings_;}
    // Wind is an opt-in presentation input, independent of atlas baking.
    // PHYSIS can supply this same POD to near Arbor and far Horizon.
    void setWind(ForestWindState wind) {
        validateForestWind(wind);
        if(wind==wind_)return;
        wind_=wind;
        // The fallback wind also determines the exact values outside
        // the published PHYSIS grid. Repack only when a field is active.
        if(windField_)++windFieldRevision_;
    }
    const ForestWindState& wind() const {return wind_;}
    // Optional low-frequency cluster-local direction/strength/phase.
    // Toggle only on the render owner thread before submit(); this changes
    // the shared vertex shader graph, never the mesh or CPU atlas.
    void setSpatialWindVariation(bool enabled) {
        adaptiveQualityConfig_.enabled=false; // explicit manual override
        spatialWindVariation_=enabled;
    }
    bool spatialWindVariation() const {return spatialWindVariation_;}
    // Snapshot is owned by the native host; a single macro sample is
    // shared across all cards of an HLOD region and both crossfade models.
    // No per-tree data in the statistical Horizon cluster atlas.
    void setWindField(std::shared_ptr<const ForestWindField> field) {
        if(field==windField_)return;
        if(field)field->validate();
        windFieldHasWind_=field && field->anyWind();
        windField_=std::move(field);
        ++windFieldRevision_;
    }
    // Exact PHYSIS field updates uploaded distant card RGB8 attributes
    // when the immutable snapshot changes. Disabling restores Stage33
    // procedural metadata at the next safe RHI update.
    void setExactSpatialWindField(bool enabled) {
        adaptiveQualityConfig_.enabled=false; // explicit manual override
        if(exactSpatialWindField_==enabled)return;
        exactSpatialWindField_=enabled;
        ++windFieldRevision_;
    }
    bool exactSpatialWindField() const {return exactSpatialWindField_;}
    // May be adjusted on the render owner thread without rebaking a region.
    // Changes apply to pending wind revisions at the next update(device).
    void setWindUploadPolicy(HorizonWindUploadPolicy policy) {
        validateHorizonWindUploadPolicy(policy);
        adaptiveQualityConfig_.enabled=false; // explicit manual override
        windUploadPolicy_=policy;
    }
    HorizonWindUploadPolicy windUploadPolicy() const {
        return windUploadPolicy_;
    }
    // CPU policy is distinct from Stage36's GPU *transfer* policy.
    // Setter is owner-thread-only; an actual cache trim needs gfx::Device,
    // so it happens at the start of the next update(), not in this setter.
    void setWindShadowCachePolicy(HorizonWindShadowCachePolicy policy) {
        validateHorizonWindShadowCachePolicy(policy);
        adaptiveQualityConfig_.enabled=false; // explicit manual override
        windShadowCachePolicy_=policy;
    }
    HorizonWindShadowCachePolicy windShadowCachePolicy() const {
        return windShadowCachePolicy_;
    }
    // A single validated owner-thread operation configures the existing
    // Stage33/34 shader switches, Stage36 GPU upload budget and Stage37/38
    // inactive CPU wind shadow memory. Terrain/atlas settings are immutable.
    void setPresentationPolicy(HorizonPresentationPolicy policy) {
        validateHorizonPresentationPolicy(policy);
        setWindUploadPolicy(policy.windUpload);
        setWindShadowCachePolicy(policy.windShadowCache);
        setSpatialWindVariation(policy.spatialWindVariation);
        setExactSpatialWindField(policy.exactSpatialWindField);
    }
    HorizonPresentationPolicy presentationPolicy() const {
        HorizonPresentationPolicy policy;
        policy.exactSpatialWindField=exactSpatialWindField_;
        policy.spatialWindVariation=spatialWindVariation_;
        policy.windUpload=windUploadPolicy_;
        policy.windShadowCache=windShadowCachePolicy_;
        return policy;
    }
    void setPresentationQualityPreset(HorizonPresentationQualityPreset preset) {
        setPresentationPolicy(horizonPresentationPolicyForQuality(preset));
    }
    // Explicit host-fed adaptive quality (OFF by default). Enabling selects
    // the configured initial Stage39 presentation profile immediately;
    // disabling preserves the last selected profile. Manual policy changes
    // intentionally opt out rather than fighting the host's selection.
    void setAdaptiveQualityConfig(HorizonAdaptiveQualityConfig config) {
        validateHorizonAdaptiveQualityConfig(config);
        if(config.enabled) {
            const auto initial=horizonPresentationPolicyForQuality(config.initial);
            setPresentationPolicy(initial);
            adaptiveQualityState_=initialHorizonAdaptiveQualityState(config);
        }
        adaptiveQualityConfig_=config;
    }
    HorizonAdaptiveQualityConfig adaptiveQualityConfig() const {
        return adaptiveQualityConfig_;
    }
    HorizonAdaptiveQualityState adaptiveQualityState() const {
        return adaptiveQualityState_;
    }
    // The engine has no universal GPU timer; caller must submit actual
    // CPU/GPU frame duration in microseconds once per monotonically indexed
    // rendered frame. No update/device calls or geometry re-bakes here.
    std::optional<HorizonPresentationQualityPreset> submitAdaptiveFrameTiming(
            HorizonFrameTimingSample sample) {
        auto next=advanceHorizonAdaptiveQuality(
            adaptiveQualityConfig_,adaptiveQualityState_,sample);
        if(next) {
            // Stage39 manual setter clears the controller's enabled flag;
            // re-enable it solely for this internally decided transition.
            const auto selected=horizonPresentationPolicyForQuality(*next);
            setPresentationPolicy(selected);
            adaptiveQualityConfig_.enabled=true;
        }
        return next;
    }
    std::shared_ptr<const ForestWindField> windField() const {
        return windField_;
    }
    void setPresentationTick(uint64_t tick) {presentationTick_=tick;}
    uint64_t presentationTick() const {return presentationTick_;}

    void update(gfx::Device& device,ForestCameraCm observer) {
        streamStarted_=true;
        lastObserver_=observer;
        if(observer.x<-kWorldLimitCm||observer.x>kWorldLimitCm||
           observer.z<-kWorldLimitCm||observer.z>kWorldLimitCm)
            throw std::out_of_range("TerraWeave Horizon camera extent");
        // Keep the current region until the camera moves past the cell
        // boundary plus a hysteresis margin. This prevents rapid rebuilds
        // when the observer oscillates near a 128m streaming edge.
        const int64_t side=settings_.middleTileCm;
        int64_t wantedX=detail::floorDiv(observer.x,side);
        int64_t wantedZ=detail::floorDiv(observer.z,side);
        const int64_t margin=settings_.regionHysteresisCm;
        const std::optional<world::Cell> anchorCell = cataloged_ ?
            std::optional<world::Cell>{cell_} : activeCell_;
        if(anchorCell && margin>0) {
            const auto inExpandedCell=[side,margin](int64_t position,
                                                     int32_t cell) {
                const int64_t lo=int64_t(cell)*side-margin;
                const int64_t hi=(int64_t(cell)+1)*side+margin;
                return position>=lo && position<hi;
            };
            if(inExpandedCell(observer.x,anchorCell->x))
                wantedX=anchorCell->x;
            if(inExpandedCell(observer.z,anchorCell->z))
                wantedZ=anchorCell->z;
        }
        const world::Cell desired{
            static_cast<int32_t>(wantedX),0,static_cast<int32_t>(wantedZ)
        };
        requestedCell_=desired; // updated before cache, fast-return or worker paths
        // Source revisions invalidate cached geometry. Cached entries
        // can only be retired on the owning gfx::Device thread.
        gpuCache_.prune(device,sourceRevision_);
        finishTransition(device);
        // Runtime policy changes affect already-cached dormant models on
        // the next safe gfx owner-thread update, before budget eviction.
        if(windShadowCachePolicy_.compressCachedShadows)
            gpuCache_.compactWindShadows();
        gpuCache_.trimWindShadowBytes(device,
            windShadowCachePolicy_.maxCachedShadowBytes);
        // Patch only models that survived transition retirement. This must
        // run BEFORE fast-return/cached-region paths, and never during submit.
        refreshForestWindGpu(device);
        // A recently departed region may still be in the short-lived
        // transition slot instead of the normal GPU LRU.
        if(transition_.model && transitionCell_ &&
           *transitionCell_==desired &&
           transitionRevision_==sourceRevision_) {
            // Camera reversal during the fade: the departing GPU model
            // already exists in the transition slot. Re-adopt it directly
            // instead of first placing it into the LRU (which might evict
            // this desired region when the cache has only one slot).
            if(cataloged_) {
                stream_->remove(cell_,[](world::Cell){});
                cataloged_=false;
            }
            HorizonGpuRegion returning=std::move(transition_);
            transition_={};
            transitionCell_.reset();
            transitionRevision_=0;
            retireActive(device,desired,false);
            adoptCached(desired,std::move(returning));
            cell_=desired;
            drainCanceledJobs();
            return;
        }

        // Fast return for the already visible region, including a teleport
        // back from an in-flight request to the previous camera region.
        if(activeCell_ && *activeCell_==desired &&
           activeRevision_==sourceRevision_) {
            if(cataloged_){
                stream_->remove(cell_,[](world::Cell){});
                cataloged_=false;
            }
            cell_=desired;
            drainCanceledJobs();
            return;
        }

        // Reuse GPU buffers and atlas without a worker bake or RHI upload.
        // Evict the current model into the bounded LRU only AFTER removing
        // the desired cached entry; this prevents eviction of the target.
        if(auto cached=gpuCache_.take(device,desired,sourceRevision_)){
            if(cataloged_){
                stream_->remove(cell_,[](world::Cell){});
                cataloged_=false;
            }
            retireActive(device,desired);
            adoptCached(desired,std::move(*cached));
            cell_=desired;
            drainCanceledJobs();
            return;
        }

        if(!cataloged_||!(desired==cell_)) {
            if(cataloged_)stream_->remove(cell_,[](world::Cell){});
            if(!stream_->add(desired,kRegionBudget))
                throw std::runtime_error("TerraWeave Horizon region reservation");
            cell_=desired;
            cataloged_=true;
        }
        stream_->update({},std::span<const world::Cell>(&desired,1),
            1,1,[&](world::Cell cell,const world::AsyncChunkStream::Payload& payload) {
                if(!(cell==cell_))return false;
                auto prepared=std::static_pointer_cast<Prepared>(payload);
                if(prepared->sourceRevision!=sourceRevision_)
                    return false; // stale worker from previous terrain revision
                // If the next region is empty (e.g. ocean and terrain
                // proxy disabled), retire the old forest rather than leaving
                // stale distant silhouettes on-screen indefinitely.
                if(!prepared->model){
                    retireActive(device,cell,false); // empty region has no incoming fade target
                    activeCell_=cell;
                    activeRevision_=sourceRevision_;
                    clusters_=0;
                    terrainQuads_=0;
                    runtime_={};
                    runtime_.cpuPrepareMs=prepared->cpuPrepareMs;
                    runtime_.nearClipQuads=prepared->clipStats.clippedQuads;
                    runtime_.nearClipTriangles=prepared->clipStats.clippedTriangles;
                    runtime_.nearClipFallback=prepared->clipStats.clipBudgetFallback;
                    runtime_.weldedNearVertices=prepared->clipStats.weldedVertices;
                    runtime_.maxNearWeldDeltaCm=prepared->clipStats.maxWeldHeightDeltaCm;
                    runtime_.smoothedNearNormals=prepared->clipStats.smoothedNearNormals;
                    runtime_.matchedAuthoredNearNormals=prepared->clipStats.matchedAuthoredNearNormals;
                    runtime_.continuousNearNormalSamples=prepared->bake->continuousNearNormalSamples;
                    runtime_.localFallbackNearNormalSamples=prepared->bake->localFallbackNearNormalSamples;
                    runtime_.nearNormalProvenanceAvailable=prepared->bake->nearNormalProvenanceAvailable;
                    runtime_.nearWeldSlopeFallback=prepared->clipStats.nearWeldSlopeFallback;
                    return true;
                }
                // Fast path: patch both forest atlas and terrain vertices
                // when index topology, crosscards, UVs and old culling bounds
                // are still valid. Preflight both resources BEFORE the first
                // GPU write; never write into the shared geometry pool.
                if(model_ && activeBake_ && !activeVertices_.empty() &&
                   model_->images.size()==1) {
                    const auto atlas=planHorizonAtlasUpload(
                        *activeBake_,*prepared->bake,
                        settings_.atlasTilePixels,6,25,true);
                    const auto vertices=planHorizonVertexUpload(
                        *activeBake_,*prepared->bake,*model_,
                        activeVertices_,activeIndices_,*prepared->model);
                    if(atlas && vertices &&
                       horizonCanApplyVertexUpload(device,*model_,
                                                    *prepared->model,*vertices)){
                        // Allocate the replacement CPU snapshot before changing
                        // the GPU; allocation failures leave the old model intact.
                        std::vector<render::ModelVertex> replacement=
                            prepared->model->vertices;
                        const auto patchStart=std::chrono::steady_clock::now();
                        if(applyHorizonAtlasUpload(device,*model_,
                                                   *prepared->bake,*atlas) &&
                           applyHorizonVertexUpload(device,*model_,
                                                     *prepared->model,*vertices)){
                            activeVertices_.swap(replacement);
                            activeBake_=prepared->bake;
                            activeCell_=cell;
                            activeRevision_=sourceRevision_;
                            originXcm_=prepared->bake->originXcm;
                            originZcm_=prepared->bake->originZcm;
                            clusters_=prepared->bake->clusters.size();
                            terrainQuads_=prepared->bake->terrainResolution*
                                          prepared->bake->terrainResolution;
                            runtime_.clusters=clusters_;
                            runtime_.atlasBytes=prepared->bake->atlasRgba.size();
                            runtime_.cpuPrepareMs=prepared->cpuPrepareMs;
                            runtime_.nearClipQuads=prepared->clipStats.clippedQuads;
                            runtime_.nearClipTriangles=prepared->clipStats.clippedTriangles;
                            runtime_.nearClipFallback=prepared->clipStats.clipBudgetFallback;
                            runtime_.weldedNearVertices=prepared->clipStats.weldedVertices;
                            runtime_.maxNearWeldDeltaCm=prepared->clipStats.maxWeldHeightDeltaCm;
                            runtime_.smoothedNearNormals=prepared->clipStats.smoothedNearNormals;
                            runtime_.matchedAuthoredNearNormals=prepared->clipStats.matchedAuthoredNearNormals;
                    runtime_.continuousNearNormalSamples=prepared->bake->continuousNearNormalSamples;
                    runtime_.localFallbackNearNormalSamples=prepared->bake->localFallbackNearNormalSamples;
                    runtime_.nearNormalProvenanceAvailable=prepared->bake->nearNormalProvenanceAvailable;
                            runtime_.nearWeldSlopeFallback=prepared->clipStats.nearWeldSlopeFallback;
                            runtime_.gpuUploadMs=std::chrono::duration<
                                double,std::milli>(
                                    std::chrono::steady_clock::now()-patchStart
                                ).count();
                            if(!atlas->regions.empty()){
                                ++runtime_.atlasPatchCount;
                                runtime_.atlasPatchBytes+=atlas->updatedBytes;
                            }
                            runtime_.atlasPatchRegions=atlas->regions.size();
                            if(!vertices->ranges.empty()){
                                ++runtime_.vertexPatchCount;
                                runtime_.vertexPatchBytes+=vertices->updatedBytes;
                            }
                            runtime_.vertexPatchRanges=vertices->ranges.size();
                            return true;
                        }
                    }
                }
                // CPU model is already prepared on worker. uploadModel only
                // touches the renderer-owned Device on this update thread.
                auto next=std::move(prepared->model);
                const size_t nextAtlasBytes=prepared->bake->atlasRgba.size();
                const size_t nextVertices=next->vertices.size();
                const size_t nextTriangles=next->indices.size()/3;
                // Preserve the packed post-prepare model arrays only when
                // CPU region caching is enabled. uploadModel clears them.
                auto windOverlay=captureHorizonForestWindOverlay(
                    *prepared->bake,*next);
                if(!exactSpatialWindField_ || !windField_)
                    windOverlay.appliedRevision=windFieldRevision_;
                std::vector<render::ModelVertex> keptVertices;
                std::vector<uint32_t> keptIndices;
                if(settings_.maxCachedRegions){
                    keptVertices=next->vertices;
                    keptIndices=next->indices;
                }
                const auto uploadStart=std::chrono::steady_clock::now();
                if(!render::uploadModel(device,*next)){
                    render::releaseModel(device,*next);
                    return false;
                }
                retireActive(device,cell);
                model_=std::move(next);
                activeWindOverlay_=std::move(windOverlay);
                activeCell_=cell;
                activeRevision_=sourceRevision_;
                activeBake_=settings_.maxCachedRegions ?
                    prepared->bake : std::shared_ptr<const HorizonBake>{};
                activeVertices_.swap(keptVertices);
                activeIndices_.swap(keptIndices);
                originXcm_=prepared->bake->originXcm;
                originZcm_=prepared->bake->originZcm;
                clusters_=prepared->bake->clusters.size();
                terrainQuads_=prepared->bake->terrainResolution*
                              prepared->bake->terrainResolution;
                runtime_.clusters=clusters_;
                runtime_.atlasBytes=nextAtlasBytes;
                runtime_.vertices=nextVertices;
                runtime_.triangles=nextTriangles;
                runtime_.nearClipQuads=prepared->clipStats.clippedQuads;
                runtime_.nearClipTriangles=prepared->clipStats.clippedTriangles;
                runtime_.nearClipFallback=prepared->clipStats.clipBudgetFallback;
                runtime_.weldedNearVertices=prepared->clipStats.weldedVertices;
                runtime_.maxNearWeldDeltaCm=prepared->clipStats.maxWeldHeightDeltaCm;
                runtime_.smoothedNearNormals=prepared->clipStats.smoothedNearNormals;
                runtime_.matchedAuthoredNearNormals=prepared->clipStats.matchedAuthoredNearNormals;
                    runtime_.continuousNearNormalSamples=prepared->bake->continuousNearNormalSamples;
                    runtime_.localFallbackNearNormalSamples=prepared->bake->localFallbackNearNormalSamples;
                    runtime_.nearNormalProvenanceAvailable=prepared->bake->nearNormalProvenanceAvailable;
                runtime_.nearWeldSlopeFallback=prepared->clipStats.nearWeldSlopeFallback;
                runtime_.cpuPrepareMs=prepared->cpuPrepareMs;
                runtime_.gpuUploadMs=std::chrono::duration<double,std::milli>(
                    std::chrono::steady_clock::now()-uploadStart).count();
                runtime_.atlasPatchRegions=0;
                runtime_.vertexPatchRanges=0;
                ++runtime_.fullUploadCount;
                return true;
            },[](world::Cell){});
    }

    void submit(render::Renderer3D& renderer,ForestRenderOriginCm origin = {}) const {
        if(!model_)return;
        // Prevent a stale proxy drawing after a long camera teleport while
        // the worker prepares the next region. Keep small relocations visible
        // because the same 12km proxy still safely covers that observer.
        const auto proxyCoversCamera=[&]() {
            const int64_t side=activeBake_ ? activeBake_->sideCm :
                int64_t(settings_.distantTileCm)*settings_.distantTilesPerSide;
            const int64_t centerX=activeBake_ ? activeBake_->cameraXcm :
                (activeCell_ ? int64_t(activeCell_->x)*settings_.middleTileCm +
                    settings_.middleTileCm/2 : lastObserver_.x);
            const int64_t centerZ=activeBake_ ? activeBake_->cameraZcm :
                (activeCell_ ? int64_t(activeCell_->z)*settings_.middleTileCm +
                    settings_.middleTileCm/2 : lastObserver_.z);
            const int64_t half=side/2;
            const int64_t safety=settings_.distantTileCm;
            const int64_t dx=lastObserver_.x-centerX;
            const int64_t dz=lastObserver_.z-centerZ;
            return half>safety &&
                dx>=-(half-safety) && dx<half-safety &&
                dz>=-(half-safety) && dz<half-safety;
        };
        if(!proxyCoversCamera())return;

        // During a neighboring region transition, the incoming far-terrain
        // depth surface is drawn exactly once OPAQUE, and ONLY the forest
        // cluster primitives of each region participate in tint-alpha fade.
        // Both primitives share the same GPU vertex/index/atlas buffers;
        // ModelDraw::primitiveMaterialFilter avoids any duplicate geometry.
        // One continuously varying observer-space macro wind is used for
        // BOTH outgoing and incoming regions. This avoids a velocity snap
        // at HLOD region boundaries without rebaking the atlas per tick.
        const ForestWindState macroWind=sampleForestWindField(
            windField_.get(),lastObserver_.x,lastObserver_.z,wind_);
        const bool exactRequested=exactSpatialWindField_ &&
            bool(windField_);
        const auto drawRegion=[&](render::Model* model,
                                  const HorizonForestWindOverlay& overlay,
                                  int64_t xCm,int64_t zCm,
                                  float alpha,bool blending,int materialFilter) {
            if(!model || alpha<=0.0f)return;
            const bool gpuWind=macroWind.branchFlexPermille>0 &&
                macroWind.maxLeanMilliDegrees>0 &&
                (macroWind.xCmPerSec || macroWind.zCmPerSec ||
                 (exactRequested && windFieldHasWind_));
            const bool upToDate=overlay.appliedRevision==windFieldRevision_;
            const bool exactReady=exactRequested && upToDate &&
                                  !overlay.vertices.empty();
            const bool proceduralReady=!exactRequested && upToDate;
            // The far forest is one primitive/material in a shared model.
            // Only that material receives vertexOffset; ground is inert.
            // Existing active and crossfade GPU regions are updated without
            // rebaking their CPU atlas or reallocating vertex buffers.
            if(model->materials.size()>1){
                const auto desired=gpuWind ?
                    (exactReady ? horizonExactFieldWindGraph() :
                     (proceduralReady && spatialWindVariation_) ?
                         horizonSpatialWindGraph() : canopyWindGraph()) :
                    std::shared_ptr<const render::graph::Program>{};
                if(model->materials[1].shaderGraph!=desired)
                    model->materials[1].shaderGraph=desired;
            }
            render::ModelDraw draw;
            draw.model=model;
            draw.world=render::mat4::translate({
                float(xCm-origin.x)*0.01f,
                float(-origin.y)*0.01f,
                float(zCm-origin.z)*0.01f
            });
            draw.tint={1.0f,1.0f,1.0f,alpha};
            draw.primitiveMaterialFilter=materialFilter;
            if(gpuWind && materialFilter!=0){
                // HLOD crossing cards receive a small synchronized bend.
                // The bottom two vertices have zero weight; only the top
                // canopy edge moves, avoiding animated ground geometry.
                draw.vertexParameters=exactReady ?
                    horizonExactFieldVertexParameters(
                        wind_,presentationTick_) :
                    canopyWindVertexParameters(
                        macroWind,presentationTick_,0.0f,0.30f);
                draw.useVertexParameters=true;
                draw.maxVertexOffsetMetres=kCanopyGpuWindMaxOffsetMetres;
            }
            draw.flags=blending ?
                (render::kDrawTransparent | render::kDrawNoCastShadow |
                 render::kDrawNoReceiveShadow) :
                (materialFilter<0 ? render::kDrawStatic :
                    (render::kDrawNoCastShadow|render::kDrawNoReceiveShadow));
            // Split draws are ephemeral: do not register an incomplete
            // partial Model into the persistent scene/lightbake capture.
            if(materialFilter<0)
                draw.persistentId=detail::hash2(xCm,zCm,
                    0x5445525241484c4fULL)|1u;
            renderer.drawModel(draw);
        };
        if(transition_.model && transitionRevision_==sourceRevision_ &&
           settings_.regionTransitionMs && activeCell_) {
            const float t=transitionFraction();
            if(t<1.0f) {
                // Source material slot 0 is distant terrain, slot 1 forest.
                // A single opaque terrain draw writes correct scene depth;
                // neither outgoing nor incoming terrain uses transparency.
                drawRegion(model_.get(),activeWindOverlay_,
                           originXcm_,originZcm_,1.0f,false,0);
                drawRegion(transition_.model.get(),transition_.forestWind,
                    transition_.originXcm,transition_.originZcm,
                    1.0f-t,true,1);
                drawRegion(model_.get(),activeWindOverlay_,
                           originXcm_,originZcm_,t,true,1);
                return;
            }
        }
        drawRegion(model_.get(),activeWindOverlay_,
                   originXcm_,originZcm_,1.0f,false,-1);
    }
    void shutdown(gfx::Device& device) {
        if(stream_){
            stream_->shutdown([](world::Cell){});
            stream_.reset();
        }
        if(model_){render::releaseModel(device,*model_);model_.reset();}
        finishTransition(device,true);
        gpuCache_.clear(device);
        activeCell_.reset();
        requestedCell_.reset();
        activeRevision_=0;
        activeBake_.reset();
        activeVertices_.clear();
        activeIndices_.clear();
        activeWindOverlay_={};
        { std::lock_guard lock(cacheMutex_); cache_.clear(); }
        runtime_={}; // No active GPU region: diagnostics must not describe the retired one.
        clusters_=0;
        terrainQuads_=0;
        originXcm_=0;
        originZcm_=0;
    }
    bool ready() const {return model_!=nullptr;}
    size_t clusterCount() const {return clusters_;}
    size_t approximateTerrainQuads() const {return terrainQuads_;}
    HorizonRuntimeStats runtimeStats() const {
        auto out=runtime_;
        out.sourceRevision=sourceRevision_;
        out.activeRevision=activeRevision_;
        out.activeRegionPresent=bool(model_) && activeCell_.has_value();
        // An accepted empty bake resolves the cell without a GPU model.
        out.activeRegionResolved=activeCell_.has_value();
        out.activeRegionEmpty=out.activeRegionResolved && !bool(model_);
        out.staleProxy=out.activeRegionResolved &&
                       out.activeRevision!=out.sourceRevision;
        out.requestedRegionPresent=requestedCell_.has_value();
        if(requestedCell_){
            out.requestedCellX=requestedCell_->x;
            out.requestedCellZ=requestedCell_->z;
        }
        if(out.activeRegionResolved){
            out.activeCellX=activeCell_->x;
            out.activeCellZ=activeCell_->z;
        }
        out.requestedRegionPending=out.requestedRegionPresent &&
            (!out.activeRegionResolved ||
             !(*requestedCell_==*activeCell_) || out.staleProxy);
        if(stream_) {
            const auto& s=stream_->status();
            out.preparing=s.preparing;
            out.discarded=s.discarded;
        }
        {
            std::lock_guard lock(cacheMutex_);
            out.cachedRegions=cache_.size();
        }
        out.cacheHits=cacheHits_.load(std::memory_order_relaxed);
        out.archiveHits=archiveHits_.load(std::memory_order_relaxed);
        out.gpuCachedRegions=gpuCache_.size();
        out.gpuCachedBytes=gpuCache_.bytes();
        out.gpuCacheHits=gpuCacheHits_;
        out.gpuCacheEvictions=gpuCache_.evictions();
        out.gpuCachedWindShadowBytes=gpuCache_.cachedWindShadowBytes();
        out.gpuWindShadowBudgetEvictions=gpuCache_.windShadowEvictions();
        out.gpuCachedCompressedWindShadows=
            gpuCache_.compressedWindShadowRegions();
        out.gpuWindShadowCompressions=gpuCache_.windShadowCompressions();
        out.gpuWindShadowRestores=gpuCache_.windShadowRestores();
        out.gpuWindShadowDecodeFailures=
            gpuCache_.windShadowDecodeFailures();
        out.exactWindFieldActive=exactSpatialWindField_ && bool(windField_);
        out.windUploadBudgetBytes=windUploadPolicy_.maxBytesPerUpdate;
        out.cachedWindShadowBudgetBytes=
            windShadowCachePolicy_.maxCachedShadowBytes;
        out.windUploadSparseRangeCap=windUploadPolicy_.maxSparseRanges;
        out.windUploadFullFallbackPercent=windUploadPolicy_.fullFallbackPercent;
        out.windShadowCompressionEnabled=
            windShadowCachePolicy_.compressCachedShadows;
        out.spatialWindVariationEnabled=spatialWindVariation_;
        out.adaptiveQualityEnabled=adaptiveQualityConfig_.enabled;
        out.adaptiveQualityLevel=adaptiveQualityState_.current;
        out.adaptiveQualitySamples=adaptiveQualityState_.samples;
        out.adaptiveQualitySwitches=adaptiveQualityState_.switches;
        out.adaptiveQualityDowngrades=adaptiveQualityState_.downgrades;
        out.adaptiveQualityUpgrades=adaptiveQualityState_.upgrades;
        out.adaptiveQualityLastFrameUs=adaptiveQualityState_.lastMeasuredFrameUs;
        out.windOverlayPending=bool(model_) &&
            !activeWindOverlay_.vertices.empty() &&
            activeWindOverlay_.appliedRevision!=windFieldRevision_;
        out.transitionStarts=transitionStarts_;
        out.transitioning=transition_.model!=nullptr &&
                          transitionRevision_==sourceRevision_ &&
                          transitionFraction()<1.0f && model_!=nullptr;
        out.transitionProgressPermille=out.transitioning ?
            static_cast<uint32_t>(transitionFraction()*1000.0f):1000;
        return out;
    }
    const world::AsyncChunkStream::Status& status() const {
        if(stream_)return stream_->status();
        static const world::AsyncChunkStream::Status empty;
        return empty;
    }

private:
    void refreshForestWindGpu(gfx::Device& device) {
        // Up to eight sparse dirty-cluster ranges per region, with a
        // configurable full-forest-suffix fallback for fragmented/dense
        // changes. Active and outgoing regions share one bounded host
        // render-owner update budget and retain their old safe shader
        // mode until the entire field revision is committed.
        size_t remaining=windUploadPolicy_.maxBytesPerUpdate;
        const ForestWindField* selectedField=
            exactSpatialWindField_ ? windField_.get() : nullptr;
        const auto refresh=[&](render::Model* model,
                               HorizonForestWindOverlay& overlay) {
            if(!model || overlay.vertices.empty() ||
               overlay.appliedRevision==windFieldRevision_)return;
            const auto result=applyHorizonForestWindOverlay(
                device,*model,overlay,selectedField,wind_,
                windFieldRevision_,remaining,windUploadPolicy_);
            if(result.ready && !result.uploaded)
                ++runtime_.windOverlayNoopRevisions;
            if(result.uploaded) {
                remaining-=result.bytes;
                ++runtime_.windOverlayPatchWrites;
                runtime_.windOverlayPatchBytes+=result.bytes;
                runtime_.windOverlayPatchRanges+=result.ranges;
                runtime_.windOverlayChangedClusters+=
                    result.changedClusters;
                if(result.fullRangeFallback)
                    ++runtime_.windOverlayFullRangeFallbacks;
            }
        };
        refresh(model_.get(),activeWindOverlay_);
        if(transition_.model)
            refresh(transition_.model.get(),transition_.forestWind);
    }

    // Smoothstep in real elapsed monotonic time; no tick/ECS state is touched.
    float transitionFraction() const {
        if(!transition_.model || settings_.regionTransitionMs==0)
            return 1.0f;
        const auto elapsed=std::chrono::duration<double,std::milli>(
            Clock::now()-transitionStart_).count();
        const float t=std::clamp(float(elapsed/
            double(settings_.regionTransitionMs)),0.0f,1.0f);
        return t*t*(3.0f-2.0f*t);
    }

    // Only the RHI owner thread may relinquish the outgoing GPU model.
    // Completed transitions become ordinary LRU entries only when the
    // world surface generation still matches their recorded revision.
    void finishTransition(gfx::Device& device,bool force=false) {
        if(!transition_.model)return;
        if(!force && transitionRevision_==sourceRevision_ &&
           transitionFraction()<1.0f)return;
        if(transitionRevision_==sourceRevision_ && transitionCell_ &&
           settings_.maxCachedGpuRegions)
            gpuCache_.retain(device,*transitionCell_,transitionRevision_,
                std::move(transition_),settings_.maxCachedGpuRegions,
                size_t(settings_.gpuCacheBudgetMiB)*1024u*1024u,
                windShadowCachePolicy_.maxCachedShadowBytes,
                windShadowCachePolicy_.compressCachedShadows);
        else
            render::releaseModel(device,*transition_.model);
        transition_={};
        transitionCell_.reset();
        transitionRevision_=0;
    }

    // Snapshot an already-uploaded GPU model before replacing the active
    // region. Retain only if its source revision is still current; otherwise
    // release using the same RHI Device that originally uploaded it.
    void retireActive(gfx::Device& device,world::Cell incoming,
                      bool allowFade=true) {
        // At most one outgoing model is retained; a new swap completes
        // the prior transition before taking ownership of another one.
        finishTransition(device,true);
        if(!model_){
            activeBake_.reset();
            activeVertices_.clear();
            activeIndices_.clear();
            activeWindOverlay_={};
            return;
        }
        HorizonGpuRegion previous;
        previous.model=std::move(model_);
        previous.bake=std::move(activeBake_);
        previous.vertices=std::move(activeVertices_);
        previous.indices=std::move(activeIndices_);
        previous.forestWind=std::move(activeWindOverlay_);
        previous.originXcm=originXcm_;
        previous.originZcm=originZcm_;
        previous.clusters=clusters_;
        previous.terrainQuads=terrainQuads_;
        previous.atlasBytes=runtime_.atlasBytes;
        previous.vertexCount=runtime_.vertices;
        previous.triangleCount=runtime_.triangles;
        previous.nearClipQuads=runtime_.nearClipQuads;
        previous.nearClipTriangles=runtime_.nearClipTriangles;
        previous.nearClipFallback=runtime_.nearClipFallback;
        previous.weldedNearVertices=runtime_.weldedNearVertices;
        previous.maxNearWeldDeltaCm=runtime_.maxNearWeldDeltaCm;
        previous.smoothedNearNormals=runtime_.smoothedNearNormals;
        previous.matchedAuthoredNearNormals=runtime_.matchedAuthoredNearNormals;
        previous.continuousNearNormalSamples=runtime_.continuousNearNormalSamples;
        previous.localFallbackNearNormalSamples=runtime_.localFallbackNearNormalSamples;
        previous.nearNormalProvenanceAvailable=runtime_.nearNormalProvenanceAvailable;
        previous.nearWeldSlopeFallback=runtime_.nearWeldSlopeFallback;
        previous.estimatedGpuBytes=estimateHorizonGpuResidencyBytes(
            previous.atlasBytes,previous.vertexCount,previous.triangleCount);
        if(activeCell_ && !(*activeCell_==incoming) &&
           activeRevision_==sourceRevision_ &&
           settings_.maxCachedGpuRegions) {
            const int64_t dx=int64_t(activeCell_->x)-incoming.x;
            const int64_t dz=int64_t(activeCell_->z)-incoming.z;
            const bool adjacent=dx>=-1&&dx<=1&&dz>=-1&&dz<=1;
            const size_t budget=size_t(settings_.gpuCacheBudgetMiB)*
                                 1024u*1024u;
            if(allowFade && adjacent && settings_.regionTransitionMs>0 &&
               gpuCache_.reserveTransient(device,previous.estimatedGpuBytes,
                   settings_.maxCachedGpuRegions,budget)) {
                transition_=std::move(previous);
                transitionCell_=*activeCell_;
                transitionRevision_=activeRevision_;
                transitionStart_=Clock::now();
                ++transitionStarts_;
                return;
            }
            gpuCache_.retain(device,*activeCell_,activeRevision_,
                std::move(previous),settings_.maxCachedGpuRegions,budget,
                windShadowCachePolicy_.maxCachedShadowBytes,
                windShadowCachePolicy_.compressCachedShadows);
        } else if(previous.model)
            render::releaseModel(device,*previous.model);
    }

    void adoptCached(world::Cell cell,HorizonGpuRegion region) {
        model_=std::move(region.model);
        activeBake_=std::move(region.bake);
        activeVertices_=std::move(region.vertices);
        activeIndices_=std::move(region.indices);
        activeWindOverlay_=std::move(region.forestWind);
        originXcm_=region.originXcm;
        originZcm_=region.originZcm;
        clusters_=region.clusters;
        terrainQuads_=region.terrainQuads;
        activeCell_=cell;
        activeRevision_=sourceRevision_;
        runtime_.clusters=region.clusters;
        runtime_.atlasBytes=region.atlasBytes;
        runtime_.vertices=region.vertexCount;
        runtime_.triangles=region.triangleCount;
        runtime_.nearClipQuads=region.nearClipQuads;
        runtime_.nearClipTriangles=region.nearClipTriangles;
        runtime_.nearClipFallback=region.nearClipFallback;
        runtime_.weldedNearVertices=region.weldedNearVertices;
        runtime_.maxNearWeldDeltaCm=region.maxNearWeldDeltaCm;
        runtime_.smoothedNearNormals=region.smoothedNearNormals;
        runtime_.matchedAuthoredNearNormals=region.matchedAuthoredNearNormals;
        runtime_.continuousNearNormalSamples=region.continuousNearNormalSamples;
        runtime_.localFallbackNearNormalSamples=region.localFallbackNearNormalSamples;
        runtime_.nearNormalProvenanceAvailable=region.nearNormalProvenanceAvailable;
        runtime_.nearWeldSlopeFallback=region.nearWeldSlopeFallback;
        runtime_.gpuUploadMs=0;
        runtime_.atlasPatchRegions=0;
        runtime_.vertexPatchRanges=0;
        ++gpuCacheHits_;
    }

    // Canceled jobs still hold staging reservations until update() drains
    // completion. Do not starve the worker after an instant GPU cache hit.
    void drainCanceledJobs() {
        stream_->update({},std::span<const world::Cell>{},0,0,
            [](world::Cell,const world::AsyncChunkStream::Payload&) {
                return false;
            },
            [](world::Cell){});
    }

    struct SourceGeneration {
        uint64_t revision=0;
        std::shared_ptr<const HorizonSurfaceSources> surface;
        std::shared_ptr<const HorizonBake> previous;
        std::vector<RectCm> edited;
    };
    struct CacheEntry {
        std::shared_ptr<const HorizonBake> bake;
        uint64_t revision=0, stamp=0;
    };
    struct Prepared final : world::AsyncChunkStream::Resource {
        std::shared_ptr<const HorizonBake> bake;
        std::unique_ptr<render::Model> model;
        HorizonModelBuildStats clipStats;
        double cpuPrepareMs=0;
        uint64_t sourceRevision=0;
    };
    static constexpr size_t kRegionBudget=24u*1024u*1024u;
    TerrainSettings terrain_;
    BiomeSettings biomes_;
    HorizonSettings settings_;
    ForestWindState wind_{};
    bool spatialWindVariation_=true;
    bool exactSpatialWindField_=true;
    HorizonWindUploadPolicy windUploadPolicy_{};
    HorizonWindShadowCachePolicy windShadowCachePolicy_{};
    HorizonAdaptiveQualityConfig adaptiveQualityConfig_{};
    HorizonAdaptiveQualityState adaptiveQualityState_{};
    bool windFieldHasWind_=false;
    uint64_t windFieldRevision_=1;
    std::shared_ptr<const ForestWindField> windField_;
    HorizonForestWindOverlay activeWindOverlay_;
    uint64_t presentationTick_=0;
    std::unique_ptr<world::AsyncChunkStream> stream_;
    std::unique_ptr<render::Model> model_;
    std::shared_ptr<const HorizonBake> activeBake_; // optional previous source
    // Compact dedicated CPU reference (~0.5 MB for defaults). This enables
    // exact packed-vertex comparisons while uploadModel itself drops CPU data.
    std::vector<render::ModelVertex> activeVertices_;
    std::vector<uint32_t> activeIndices_;
    world::Cell cell_{};
    bool cataloged_=false;
    std::optional<world::Cell> activeCell_;
    std::optional<world::Cell> requestedCell_;
    uint64_t activeRevision_=0; // authored source revision of GPU-resident region
    HorizonGpuRegionCache gpuCache_;
    size_t gpuCacheHits_=0;
    using Clock=std::chrono::steady_clock;
    HorizonGpuRegion transition_;
    std::optional<world::Cell> transitionCell_;
    uint64_t transitionRevision_=0;
    Clock::time_point transitionStart_{};
    size_t transitionStarts_=0;
    ForestCameraCm lastObserver_{}; // owner-thread presentation only
    int64_t originXcm_=0,originZcm_=0;
    size_t clusters_=0,terrainQuads_=0;
    HorizonRuntimeStats runtime_{};
    // Portable to Apple libc++ where atomic<shared_ptr> is unavailable.
    // Worker snapshots still use acquire/release atomic shared_ptr free functions.
    std::shared_ptr<const SourceGeneration> sources_{};
    uint64_t sourceRevision_=0; // update thread only
    mutable std::mutex cacheMutex_;
    std::map<world::Cell,CacheEntry> cache_;
    uint64_t cacheTick_=0; // protected by cacheMutex_
    std::atomic<size_t> cacheHits_{0};
    std::atomic<size_t> archiveHits_{0};
    std::shared_ptr<const HorizonArchiveStore> archiveStore_;
    bool streamStarted_=false;
};

} // namespace aiverse::terraweave
