#pragma once
#include "Geometry.h"
#include "Renderer3D.h"
#include "NativePresentationRetirement.h"
#include "StrataDebugView.h"
#include <cmath>
#include <map>
#include <memory>
#include <functional>
#include <string>

namespace aiverse::render {
namespace strata { struct ModelAsset; struct PortableCompactBuffer; class SceneStaging; class GpuMetadataScene; struct ViewConfig; struct ViewPlan;
    struct GpuSceneInstance; struct GpuPassConfig; struct GpuSelectionSubmission;
    struct GpuSelectionReadback; class GpuClusterCulling;
    class PortableSource;
    struct PortableLodHistory;
    struct RenderCandidate; enum class RenderPass : uint8_t; }
class Renderer3D;
// Static presentation geometry, never collision or simulation objects. HLOD proxies merge
// compatible materials and use bounded-error cluster simplification. Sources stay available.
class GeometryScene {
public:
    struct Stats { uint32_t instances=0, groups=0, proxies=0, submitted=0, surfaceProxiesSubmitted=0;
        uint32_t strataSources=0, strataPrimitives=0, strataSkippedSources=0,
                 strataSkippedPrimitives=0, strataFailures=0;
        uint64_t strataNodes=0, strataMeshlets=0, strataIndexBytes=0;
        uint64_t strataSeamCertifiedPrimitives=0,
                 strataSeamRejectedParents=0;
        uint32_t strataSeamUnsafeDeclined=0;
        uint32_t strataCrossMaterialCertifiedSources=0;
        uint64_t strataCrossMaterialMatchedSeams=0;
        uint32_t strataGpuMetadataSources=0, strataGpuMetadataDeclined=0,
                 strataGpuMetadataFailures=0;
        uint64_t strataGpuMetadataBytes=0;
        uint32_t strataGpuIndexedSources=0, strataGpuIndexDeclined=0,
                 strataGpuIndexFailures=0;
        uint64_t strataGpuIndexBytes=0;
        // Cross-backend CPU-selected Strata indexed rendering (opt-in).
        uint32_t strataPortableSources=0, strataPortableDeclined=0;
        // Copies with the same selected cluster set share one alias Model,
        // so Renderer3D instances them (key: primitive pointer).
        uint32_t strataPortableSharedModels=0;
        // strata.select auto: sources kept on the ordinary LOD path / sent to STRATA.
        uint32_t strataAutoLodSources=0, strataAutoStrataSources=0;
        float strataStageMs=0; // CPU time of building STRATA clusters at load
        // Cooked STRATA: sources loaded from strata.cooked / written by a cook
        // pass / rejected as stale; time spent reading vs building.
        uint32_t strataCookedSources=0, strataCookWrittenSources=0, strataCookStaleSources=0;
        float strataCookedLoadMs=0, strataBuildMs=0;
        uint64_t strataCompactUploads=0, strataCompactReuses=0, strataPlanReuses=0;
        uint64_t strataPortableSharedReuses=0;
        uint64_t strataPortableDraws=0, strataPortableFallbacks=0;
        uint64_t strataPortableClusters=0;
        uint64_t strataPortableTriangles=0; // triangles of the drawn STRATA aliases (all instances, main view)
        float strataErrorScale=1.f; // triangle budget: current multiplier of the pixel-error threshold
        uint64_t strataSelectionGpuBytes=0; // live compacted selection index buffers (all instances)
        uint64_t strataCompressedCpuIndexBytes=0; // compressed CPU index copies kept for compacted submission
        uint64_t strataPortableGpuIndexBytes=0; // full STRATA index streams uploaded to the GPU (portable route)
        uint64_t strataLodHysteresisTransitions=0;
        uint32_t strataLodHysteresisHistories=0;
        uint32_t strataStreamedSources=0;
        uint64_t strataStreamingIndexBytes=0, strataStreamingEvictions=0;
        uint64_t strataStreamingPrepareFailures=0;
        uint32_t strataStreamingBudgetDeclined=0;
        uint32_t strataIncrementalSources=0;
        // Aggregate active per-source arena reservation, submitted upload
        // bytes/calls and delayed promotions; not physical VRAM telemetry.
        uint64_t strataStreamingArenaBytes=0, strataStreamingUploadedBytes=0;
        uint64_t strataStreamingUploadDeferrals=0, strataStreamingFenceDeferrals=0;
        uint32_t strataDiskArchiveSources=0, strataDiskArchiveDeclined=0;
        uint64_t strataDiskBytesRead=0, strataDiskReadFailures=0;
        uint64_t strataDiskPending=0, strataDiskReadyBytes=0;
        uint64_t strataStreamingGpuTransferBytes=0;
        // Stage18.2: deltas for the current RHI frame; unlike the older
        // streaming counters these are NOT cumulative per-source totals.
        // No GPU timestamps, physical VRAM or bandwidth rate is implied.
        uint64_t strataStreamFrameSerial=0;
        uint64_t strataStreamFrameDemandRecords=0;
        uint64_t strataStreamFrameGpuTransferBytes=0;
        uint64_t strataStreamFrameUploadedBytes=0;
        uint64_t strataStreamFramePromotions=0;
        uint64_t strataStreamFrameEvictions=0;
        uint64_t strataStreamFrameDeferrals=0;
        uint64_t strataStreamFrameFailedUploads=0;
        uint64_t strataStreamPeakGpuTransferBytes=0;
        uint64_t strataStreamPeakActiveIndexBytes=0;
        // Stage16.3 opt-in exclusive GPU indexed raster routing.
        uint64_t strataVulkanGpuCandidates=0, strataVulkanGpuPrepared=0;
        uint64_t strataVulkanGpuDeclined=0;
        uint64_t strataGpuTemporalSubmissions=0;
        uint32_t strataGpuTemporalHistories=0;
        uint64_t strataGpuTemporalBytes=0, strataGpuTemporalColdStarts=0;
        uint64_t strataGpuTemporalRefused=0;
        // All live GeometryScenes sharing one gfx::Device contribute to the
        // same reservation ledger. Excludes source model buffers and in-flight
        // internal backend copies; see Stage 15.4 handoff.
        uint64_t strataDeviceReservedIndexBytes=0;
        uint32_t strataDeviceIndexLeases=0, strataGlobalBudgetDeclined=0;
        uint64_t strataDeviceFrameUploadedBytes=0;
        uint64_t strataDeviceFrameUploadRemainingBytes=0;
        uint32_t strataSharedIoWorkers=0;
        uint64_t strataSharedIoQueuedJobs=0, strataSharedIoActiveJobs=0;
        uint64_t strataSharedCpuReadyBytes=0, strataSharedCpuReadyBudgetBytes=0;
        uint64_t strataArchivedCpuIndexBytesFreed=0;
        uint32_t strataArchivedCpuIndexSources=0;
    };
    void load(gfx::Device& dev, const Json& json, const FileReader& reader);
    // Stage a replacement scene independently, preserving the current scene
    // on validation/model/GPU upload errors. Publish only after success.
    void loadTransactional(gfx::Device& dev, const Json& json, const FileReader& reader);
    // Procedural/offline callers may supply immutable CPU models instead of files.
    // Borrowed image handles must outlive the scene. GPU geometry is owned by the scene.
    void loadPrepared(gfx::Device& dev,const Json& json,std::map<std::string,std::unique_ptr<Model>> models,size_t vertexBudget=2000000);
    // Full Device idle proof is required at final teardown. Live reload
    // instead moves old resident Models/HLOD proxies into the ordered queue.
    void shutdown(gfx::Device& dev);
    // Nonblocking retirement for ordinary Model-only scenes. For portable
    // STRATA owners use the explicit completed-submission fence entry point.
    // Call only after removing this scene from all Renderer3D draw paths.
    bool deferredShutdownEligible()const noexcept;
    bool beginDeferredShutdown(gfx::Device& dev);
    // Owner must detach EVERY draw/alias borrower before capturing a real
    // submission fence covering the last possible use of this scene.
    // A zero fence or unfinished GPU work refuses, never waits idle.
    // Diagnostic GPU culling/metadata/readback and failed stages stay on
    // the explicit idle recovery path.
    bool beginDeferredShutdownAfterFence(gfx::Device& dev,uint64_t lastUseFence);
    bool pollDeferredShutdown(gfx::Device& dev);

    // Discard the current static presentation scene without waiting idle:
    // used on rules/project reload, not final Device teardown.
    void clear(gfx::Device& dev) {retireResident(dev);}
    size_t pendingGpuRetirements() const noexcept;
    // Bound retained GPU owners; a stalled/lost device must not permit an
    // unlimited series of new staging allocations.
    static constexpr size_t kMaxFailedStages=8;
    size_t pendingFailedStages() const noexcept {return failedStages_.size();}
    uint64_t rejectedFailedStageLoads() const noexcept {return rejectedFailedStageLoads_;}
    // Explicit host-controlled full-idle recovery (never from an open pass).
    // Does not retire the currently published scene or its normal queue.
    bool recoverFailedStagesAfterGpuIdle(gfx::Device& dev);
    // Render-owner-thread frame boundary only: poll both live and failed-stage
    // retirement queues, releasing stages only after GPU completion proof.
    bool collectRetired(gfx::Device& dev);
    void draw(Renderer3D& renderer);
    void setRuntimeOptions(bool hlod,bool geometry,bool surfaces,bool cache) { hlod_=hlod;geometryEnabled_=geometry;surfacesEnabled_=surfaces;cacheEnabled_=cache; }
    // Opt-in staging only: prepare auxiliary CPU Strata assets at load time,
    // but keep the existing GeometryScene/Renderer3D draw path authoritative.
    // Must be configured before load/loadPrepared. Defaults OFF.
    void setStrataStagingEnabled(bool value) { strataStagingEnabled_ = value; }
    bool strataStagingEnabled() const { return strataStagingEnabled_; }
    // Experimental metadata-only upload after ordinary Model GPU upload.
    // Requires CPU staging and an existing shared geometry-pool allocation;
    // default OFF. Does not upload Strata index data or change draw calls.
    void setStrataGpuMetadataEnabled(bool enabled) { strataGpuMetadataEnabled_ = enabled; }
    bool strataGpuMetadataEnabled() const { return strataGpuMetadataEnabled_; }
    // Diagnostic GPU culling: caller must execute outside render passes and
    // never consume returned ranges as draw calls until GPU/CPU parity is proven.
    // Requires BOTH other Strata staging flags. Default OFF.
    void setStrataGpuCullingEnabled(bool on) { strataGpuCullingEnabled_ = on; }
    bool strataGpuCullingEnabled() const { return strataGpuCullingEnabled_; }
    // Renderer3D diagnostic-only preview. Legacy ModelDraw is always sent
    // unchanged; no Strata indirect or direct draw is performed. Default OFF.
    void setStrataRenderPreviewEnabled(bool on) { strataRenderPreviewEnabled_ = on; }
    bool strataRenderPreviewEnabled() const { return strataRenderPreviewEnabled_; }
    // Optional actual Strata raster route for rigid opaque sources:
    // CPU selects one conservative frontier for main/sun/reflection, then
    // renders it with THE EXISTING Renderer3D on Metal, Vulkan and GLES3.
    // Source GPU vertex data stays shared; the auxiliary index-only stream
    // is pooled when supported, dedicated on GLES3. Default OFF.
    // Scene must be reloaded after changing this option.
    void setStrataPortableRenderingEnabled(bool on) { strataPortableRenderingEnabled_=on; }
    // strata.select auto: "<model>: strata|lod (<reason>)" per source of the last load.
    const std::vector<std::string>& strataSelectDecisions() const { return strataSelectDecisions_; }
    bool strataPortableRenderingEnabled() const { return strataPortableRenderingEnabled_; }
    // Stage17.3/17.4: one opt-in hysteresis fraction drives BOTH portable
    // CPU and Metal/Vulkan GPU LOD selectors. GPU history is independent,
    // GPU-resident, per-instance/pass, and requires native parity acceptance
    // before enabling production GPU raster. 0 preserves old behavior.
    // Per-instance/main/sun histories reset on source/pass changes and
    // force-finest reflection. Safe to change this setting at runtime.
    bool setStrataLodHysteresisFraction(float fraction) {
        if(!std::isfinite(fraction) || fraction<0.f || fraction>.45f)
            return false;
        if(strataLodHysteresisFraction_!=fraction) {
            strataPortableLodHistories_.clear(); // policy change -> cold start
            // Do not destroy GPU history during an active renderer epoch.
            // A new key revision forces a cold next-frame GPU selection;
            // old GPU resources expire after their completed-frame fence.
            ++strataGpuLodPolicyEpoch_;
            if(!strataGpuLodPolicyEpoch_)++strataGpuLodPolicyEpoch_;
        }
        strataLodHysteresisFraction_=fraction;
        return true;
    }
    float strataLodHysteresisFraction() const {
        return strataLodHysteresisFraction_;
    }
    // Stage16.3 Vulkan-only production experiment. Requires staging,
    // GPU metadata and GPU culling enabled plus EXPLICIT independent
    // selection and raster parity acceptance for the exact deployed build.
    // Disabled by default. Renderer3D remains exclusive draw owner.
    void setStrataVulkanGpuIndirectEnabled(bool enabled) {
        strataVulkanGpuIndirectEnabled_=enabled;
    }
    void setStrataVulkanGpuParityGates(bool selectionVerified,
                                      bool rasterVerified) {
        strataVulkanGpuSelectionParityVerified_=selectionVerified;
        strataVulkanGpuRasterParityVerified_=rasterVerified;
    }
    bool strataVulkanGpuIndirectEnabled() const {
        return strataVulkanGpuIndirectEnabled_;
    }
    // Stage17.4 adds independent acceptance for GPU temporal LOD masks.
    // Earlier stateless GPU pixel/selection parity is NOT sufficient to
    // certify cold/warm temporal transitions. Default false: nonzero
    // hysteresis routes through portable CPU/Legacy until a native
    // temporal CPU/GPU A/B pass has been verified for the exact shader.
    void setStrataGpuTemporalParityVerified(bool verified) {
        strataGpuTemporalParityVerified_=verified;
    }
    // Independent acceptance of real native per-pass pixel/depth parity.
    // All three default false; main-view parity alone cannot certify
    // sun/spot/reflection selections and their different cameras.
    void setStrataVulkanGpuAuxiliaryParityGates(
        bool sunShadowVerified,bool spotShadowVerified,
        bool reflectionVerified) {
        strataVulkanGpuSunShadowParityVerified_=sunShadowVerified;
        strataVulkanGpuSpotShadowParityVerified_=spotShadowVerified;
        strataVulkanGpuReflectionParityVerified_=reflectionVerified;
    }
    // Positive budget enables bounded physical index-only GPU residency per
    // source, instead of eager full Strata sidecars. Must set before load.
    // Zero means classic fully resident mode; this does not affect source VB.
    void setStrataStreamingIndexBudgetBytes(size_t bytes) {
        strataStreamingIndexBudgetBytes_=bytes;
    }
    size_t strataStreamingIndexBudgetBytes() const {
        return strataStreamingIndexBudgetBytes_;
    }
    // Optional aggregate cap on ACTIVE Strata index storage in one scene.
    // Streams share this cap via deterministic load-time apportionment.
    // Scene-level limit=0 means no aggregate cap; per-source limit still applies.
    void setStrataStreamingSceneBudgetBytes(size_t bytes) {
        strataStreamingSceneBudgetBytes_=bytes;
    }
    size_t strataStreamingSceneBudgetBytes() const {
        return strataStreamingSceneBudgetBytes_;
    }
    // Optional cap shared by all live GeometryScenes on the SAME gfx::Device.
    // Must be identical across simultaneously loaded worlds; conflicting
    // limits decline Strata streaming and retain Legacy for that scene.
    // Configure before load. Zero inherits an existing device limit.
    void setStrataGlobalIndexBudgetBytes(size_t bytes) {
        strataGlobalIndexBudgetBytes_=bytes;
    }
    size_t strataGlobalIndexBudgetBytes() const {
        return strataGlobalIndexBudgetBytes_;
    }
    // Cross-world aggregate checked GPU page write cap on the same RHI
    // device and frame serial. All concurrently loaded scenes must choose
    // the same nonzero limit. Zero inherits an existing device policy.
    void setStrataGlobalFrameUploadBudgetBytes(size_t bytes) {
        strataGlobalFrameUploadBudgetBytes_=bytes;
    }
    size_t strataGlobalFrameUploadBudgetBytes() const {
        return strataGlobalFrameUploadBudgetBytes_;
    }
    // Stage 15.2: Metal/Vulkan stable index pages with checked subrange
    // uploads and fence-retired slot reuse. GLES3/GL33 retain generation
    // replacement. Zero upload cap means no per-update throttling.
    // Both settings must be applied before load/loadPrepared.
    void setStrataIncrementalIndexStreamingEnabled(bool enabled) {
        strataIncrementalIndexStreamingEnabled_=enabled;
    }
    bool strataIncrementalIndexStreamingEnabled() const {
        return strataIncrementalIndexStreamingEnabled_;
    }
    void setStrataStreamingUploadBudgetBytes(size_t bytes) {
        strataStreamingUploadBudgetBytes_=bytes;
    }
    size_t strataStreamingUploadBudgetBytes() const {
        return strataStreamingUploadBudgetBytes_;
    }
    // Aggregate cap for checked incremental GPU page transfers in ONE RHI
    // frame, across all sources of this GeometryScene. Zero means unlimited.
    // Requires active Metal/Vulkan incremental streaming; GLES remains on
    // the existing immutable fallback, excluded from this counter.
    void setStrataStreamingFrameUploadBudgetBytes(size_t bytes) {
        strataStreamingFrameUploadBudgetBytes_=bytes;
    }
    size_t strataStreamingFrameUploadBudgetBytes() const {
        return strataStreamingFrameUploadBudgetBytes_;
    }
    // Optional offline-built .strataidx archive directory. Requires
    // Metal/Vulkan incremental streaming + an active index budget.
    // A missing/stale archive keeps the affected source on Legacy. GLES
    // uses its existing CPU/immutable index path instead.
    void setStrataDiskPageDirectory(std::string directory) {
        strataDiskPageDirectory_=std::move(directory);
    }
    const std::string& strataDiskPageDirectory() const {
        return strataDiskPageDirectory_;
    }
    // Stage 15.4 opt-in. After verified on-disk archive open + root GPU
    // upload, discard duplicate staged STRATA CPU indices (source Model
    // retains its original CPU/GPU ownership). Incompatible diagnostics
    // keep the indices. Must set before load; default OFF.
    void setStrataDropArchivedCpuIndicesEnabled(bool enabled) {
        strataDropArchivedCpuIndicesEnabled_=enabled;
    }
    bool strataDropArchivedCpuIndicesEnabled() const {
        return strataDropArchivedCpuIndicesEnabled_;
    }
    bool collectStrataGpuInstances(const std::string& sourceKey,
                                   std::vector<strata::GpuSceneInstance>& out) const;
    bool dispatchStrataGpuView(gfx::Device& device, const std::string& sourceKey,
                               const strata::GpuPassConfig& pass,
                               uint64_t frameSerial,
                               strata::GpuSelectionSubmission& out);
    // Explicit diagnostic readback; callback runs on the device owner thread
    // in a later frame. Do not let test observers mutate scene lifetime here.
    bool requestStrataGpuReadback(gfx::Device& device,
        const strata::GpuSelectionSubmission& submission,
        std::function<void(strata::GpuSelectionReadback)> callback);
    gfx::BufferHandle strataGpuMetadataBuffer(const std::string& sourceKey) const;
    // Device-resident auxiliary Strata indices from the EXISTING shared page.
    // NOT a draw submission; model/node transforms, materials and index
    // ownership remain with Renderer3D and GlobalGeometryPool.
    struct StrataIndexBinding {
        gfx::BufferHandle buffer;
        uint32_t firstIndex=0, indexCount=0;
        int32_t baseVertex=0;
        bool valid() const { return buffer.valid() && indexCount>0; }
    };
    bool strataGpuIndexBinding(const std::string& sourceKey,
                               StrataIndexBinding& out) const;
    bool strataStagingSupported() const;
    const strata::ModelAsset* strataAsset(const std::string& sourceKey) const;
    // Explicit, read-only CPU reference plan for one source primitive.
    // Caller supplies the correct pass-specific node-to-clip transform.
    // Neither this method nor its result submits GPU draw calls.
    bool planStrataView(const std::string& sourceKey, uint32_t mesh,
                        uint32_t primitive, const strata::ViewConfig& config,
                        strata::ViewPlan& out, std::string* error = nullptr) const;
    // Preflight only: prepares selected auxiliary index spans for a single
    // primitive/pass but DOES NOT draw, replace ModelDraw or commit a route.
    // Renderer3D remains the only authority for pass/material/instance data.
    bool prepareStrataRenderCandidate(const std::string& sourceKey,
        uint32_t mesh, uint32_t primitive, strata::RenderPass pass,
        const strata::ViewConfig& view, strata::RenderCandidate& out,
        std::string* error = nullptr) const;
    // STRATA debug visualisation of chunks (selected clusters) and geometry.
    // Runtime toggle, default OFF, presentation-only: draws immediate
    // Renderer3D lines/triangles after the frontier the portable selector
    // already chose; never changes LOD selection, routing or GPU data.
    // Returns false (and keeps the old options) for invalid options.
    bool setStrataDebugView(const strata::DebugViewOptions& options);
    // Debug primitives only in the right half of the screen (side-by-side).
    void setStrataDebugRightHalf(bool on) { strataDebugRightHalf_=on; }
    // Inset (picture-in-picture) debug: the main pass draws no debug
    // primitives; a second, inset pass draws them and is not counted in stats.
    void setStrataDebugSuppressed(bool on) { strataDebugSuppressed_=on; }
    void setStrataInsetPass(bool on) { strataInsetPass_=on; }
    bool strataDebugRightHalf() const { return strataDebugRightHalf_; }
    const strata::DebugViewOptions& strataDebugView() const {
        return strataDebugView_;
    }
    // Wireframe/fill need CPU positions that uploadModel releases. Must be
    // set BEFORE load; copies float3 positions of STRATA sources only.
    void setStrataDebugGeometryCaptureEnabled(bool on) {
        strataDebugGeometryCapture_=on;
    }
    bool strataDebugGeometryCaptureEnabled() const {
        return strataDebugGeometryCapture_;
    }
    uint64_t strataDebugGeometryBytes() const;
    // Counters of the most recent draw() call.
    const strata::DebugDrawStats& strataDebugStats() const {
        return strataDebugStats_;
    }
    const Stats& stats() const { return stats_; }
    const std::vector<std::string>& warnings() const { return warnings_; }
    static void validate(const Json& json);
    size_t instanceCount() const { return instances_.size(); }
private:
    void retireResident(gfx::Device& dev,bool completedSubmission=false);
    void loadImpl(gfx::Device& dev,const Json& json,const FileReader& reader,std::map<std::string,std::unique_ptr<Model>> models,size_t vertexBudget);
    struct Instance { Model* model=nullptr; mat4 world; GeometryPolicy policy; Model* surfaceProxy=nullptr; float proxyPixels=96; StaticModelCache cache,proxyCache; float3 min,max; uint32_t layers=1;
        // Instance "strata": 1 force STRATA, -1 force the LOD path, 0 auto (the
        // per-model decision). strataRoute: this instance draws through STRATA.
        int8_t strataForce=0; bool strataRoute=true; };
    struct Group {
        std::vector<size_t> instances;
        int left=-1,right=-1;
        std::unique_ptr<Model> proxy;
        float3 min,max;
        float error=0;
        StaticModelCache cache;
        bool occlusion=false;
    };
    int buildGroup(gfx::Device& dev, std::vector<size_t> ids);
    void drawGroup(Renderer3D& renderer,int group);
    void drawInstance(Renderer3D& renderer,const Instance& instance);
    bool tryStrataVulkanGpuDraw(Renderer3D& renderer,
                                const Instance& instance,ModelDraw& draw);
    void updateStrataStreamingBeforeDraw(Renderer3D& renderer);
    void drawStrataDebug(Renderer3D& renderer,const Instance& instance,
                         const ModelDraw& draw,bool gpuRoute);
    std::unique_ptr<Model> makeProxy(const std::vector<size_t>& ids,float& error);
    // Copies STRATA policy/budget settings (not resources) for staged loads.
    void copyStrataConfigFrom(const GeometryScene& other);
    NativePresentationRetirement retirement_;
    bool deferredShutdownStarted_=false;
    // Failed transactions may still own GPU models after a refused idle.
    // Retain those retirement queues for a later safe collect/shutdown.
    std::vector<std::unique_ptr<GeometryScene>> failedStages_;
    uint64_t rejectedFailedStageLoads_=0;
    std::map<std::string,std::unique_ptr<Model>> models_;
    std::shared_ptr<strata::SceneStaging> strataStaging_; // null unless explicitly staged
    std::shared_ptr<strata::GpuMetadataScene> strataGpuMetadata_;
    std::shared_ptr<strata::GpuClusterCulling> strataGpuCulling_;
    // Sources live until scene shutdown; aliases live until the following
    // draw epoch, after Renderer3D has consumed all ModelDraw pointers.
    std::map<const Model*, std::shared_ptr<strata::PortableSource>> strataPortableSources_;
    // Reservation tokens are released AFTER GPU sidecars are destroyed.
    std::map<const Model*,uint64_t> strataGlobalIndexLeases_;
    std::map<const Instance*, std::unique_ptr<Model>> strataPortableDrawModels_;
    // Compacted submission: one dynamic index stream per drawn instance.
    std::map<const Instance*, std::shared_ptr<strata::PortableCompactBuffer>> strataCompactBuffers_;
    struct StrataSharedAlias { std::unique_ptr<Model> model; uint64_t lastFrame=0; };
    // (source model, selected-cluster signature) -> shared drawn alias.
    std::map<std::pair<const Model*,uint64_t>,StrataSharedAlias> strataSharedAliases_;
    // Last plan per instance: when the views (camera, sizes, thresholds) and
    // the instance transform are unchanged, the selection is unchanged too,
    // so the alias is reused without re-planning (static cameras, paused or
    // idle views cost no STRATA CPU time).
    struct StrataLastPlan { uint64_t key=0, signature=0; gfx::BufferHandle ib; };
    std::map<const Instance*,StrataLastPlan> strataLastPlans_;
    uint64_t strataDrawFrame_=0;
    std::map<const Instance*,std::shared_ptr<strata::PortableLodHistory>>
        strataPortableLodHistories_;
    gfx::Device* strataGpuDevice_ = nullptr; // borrowed owner-thread validation
    std::vector<Instance> instances_;
    std::vector<Group> groups_;
    std::vector<std::string> warnings_;
    std::vector<std::string> strataSelectDecisions_;
    Stats stats_;
    bool hlod_=false,geometryEnabled_=true,surfacesEnabled_=true,cacheEnabled_=true;
    bool strataStagingEnabled_=false;
    bool strataGpuMetadataEnabled_=false;
    bool strataGpuCullingEnabled_=false;
    bool strataRenderPreviewEnabled_=false;
    bool strataPortableRenderingEnabled_=false;
    // Planar water reflections: true = finest frontier (safe default);
    // false = reuse the main-camera frontier (mirror sees similar distances).
    bool strataReflectionFinest_=true;
    bool strataMergeClusters_=false; // scene "strata" block turns it on
    // Sun shadow pass error tolerance multiplier (the shadow map covers the
    // whole map; per-branch detail there forces the finest frontier).
    float strataShadowErrorScale_=0.f;   // 0: shadows reuse the camera cut; >0: the shadow map is a selection view
    // "strata.select": auto = per-source choice (StrataAutoSelect.h), all =
    // every source. minTriangles overrides the auto threshold (0 = default).
    bool strataAutoSelect_=true;
    uint64_t strataAutoMinTriangles_=0;
    float strataLodHysteresisFraction_=0.f;
    uint64_t strataGpuLodPolicyEpoch_=1;
    bool strataVulkanGpuIndirectEnabled_=false;
    bool strataVulkanGpuSelectionParityVerified_=false;
    bool strataVulkanGpuRasterParityVerified_=false;
    bool strataGpuTemporalParityVerified_=false;
    bool strataVulkanGpuSunShadowParityVerified_=false;
    bool strataVulkanGpuSpotShadowParityVerified_=false;
    bool strataVulkanGpuReflectionParityVerified_=false;
    size_t strataStreamingIndexBudgetBytes_=0;
    size_t strataStreamingSceneBudgetBytes_=0;
    size_t strataGlobalIndexBudgetBytes_=0;
    size_t strataGlobalFrameUploadBudgetBytes_=0;
    bool strataIncrementalIndexStreamingEnabled_=false;
    size_t strataStreamingUploadBudgetBytes_=0;
    size_t strataStreamingFrameUploadBudgetBytes_=0;
    // Borrowed device set during load, never used after shutdown.
    gfx::Device* strataPortableDevice_=nullptr;
    uint64_t strataStreamingFrameSerial_=UINT64_MAX;
    size_t strataStreamingFrameBudgetLeft_=0;
    uint64_t strataStreamingSourceCursor_=0;
    std::string strataDiskPageDirectory_;
    std::string strataCookedDir_; // geometry scene strata.cooked (content-relative)
    // strata.triangleBudget / AIVERSE_STRATA_TRI_BUDGET: automatic quality.
    uint64_t strataTriangleBudget_=0, strataBudgetLastTotal_=0;
    float strataBudgetScale_=1.f;
    bool strataDropArchivedCpuIndicesEnabled_=false;
    strata::DebugViewOptions strataDebugView_{};
    bool strataDebugRightHalf_=false, strataDebugSuppressed_=false, strataInsetPass_=false;
    strata::DebugDrawStats strataDebugStats_{};
    bool strataDebugGeometryCapture_=false;
    std::map<const Model*,std::shared_ptr<strata::DebugGeometry>>
        strataDebugGeometry_;
    float errorPixels_=2, maxPixels_=256;
    size_t proxyVertices_=0;
};
}
