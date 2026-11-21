// Generic per-host model/texture residency shared by presentation hosts.
// The cache owns models and decoded/uploaded textures; game rules,
// animation decisions, asset choices and View packet construction live
// outside this class. GPU operations must run on the render thread.
#pragma once
#include "Model.h"
#include "ModelTextureCache.h"
#include "AsyncResourceRetirementQueue.h"
#include "ModelResidentEstimate.h"
#include "RenderDeviceLifetime.h"
#include "ModelReadLease.h"
#ifndef __EMSCRIPTEN__
#include "ModelPreparationService.h"
#endif
#include <cstdint>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <map>
#include <memory>
#include <limits>
#include <stdexcept>
#include <thread>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace aiverse::render {

class ModelAssetProvider;

class ModelResourceCache final {
public:
    // Observer registration has an independent lifetime marker. It expires
    // as soon as destruction begins, before any Model or GPU lease members.
    // Group snapshots and owner destruction must share the device thread.
    ~ModelResourceCache() {
        // Unique-owned resident Models will be destroyed in this
        // destructor. An outstanding reader is a host lifetime bug;
        // fail closed rather than silently creating dangling pointers.
        if(readState_->readers.load(std::memory_order_acquire)!=0) {
            std::fprintf(stderr,
                "ModelResourceCache destroyed with active Model read leases\n");
            std::terminate();
        }
        readState_->alive.store(false,std::memory_order_release);
        lifetimeToken_.reset();
    }
    std::weak_ptr<const void> lifetimeToken() const noexcept {
        return lifetimeToken_;
    }
    // Explicit owner-thread and GPU-device binding. First bind is done
    // by the host, before any resource upload or group registration.
    // Subsequent attempts from another device/thread fail closed.
    void bindResourceDevice(gfx::Device& device) const {
        const auto thread=std::this_thread::get_id();
        if(resourceEpoch_!=0 && !resourceEpochLease_.validFor(device))
            throw std::logic_error("ModelResourceCache expired GPU device epoch");
        if(resourceDevice_ &&
           (resourceDevice_!=&device || resourceThread_!=thread))
            throw std::logic_error("ModelResourceCache device/thread mismatch");
        if(readState_->ownerThread!=std::thread::id{} &&
           readState_->ownerThread!=thread)
            throw std::logic_error("ModelResourceCache read and GPU owner thread mismatch");
        if(!resourceDevice_) {
            resourceDevice_=&device;
            resourceThread_=thread;
        }
    }
    void bindResourceDevice(
            gfx::Device& device,const RenderDeviceLifetime::Lease& lease) const {
        if(!lease.validFor(device))
            throw std::logic_error("ModelResourceCache invalid device generation");
        bindResourceDevice(device);
        if(resourceEpoch_!=0 && resourceEpoch_!=lease.generation())
            throw std::logic_error("ModelResourceCache device generation changed");
        resourceEpoch_=lease.generation();
        resourceEpochLease_=lease;
    }
    uint64_t resourceDeviceGeneration() const noexcept {
        return resourceEpoch_;
    }
    bool boundToResourceDevice(const gfx::Device& device) const noexcept {
        return resourceDevice_==&device &&
               resourceThread_==std::this_thread::get_id() &&
               (resourceEpoch_==0 || resourceEpochLease_.validFor(device));
    }
    using ModelMap=std::map<std::string,std::unique_ptr<render::Model>>;
    // Incremented by every cache-owned insertion, replacement, eviction,
    // and shutdown; the map is exposed read-only so writes cannot bypass it.
    uint64_t generation() const noexcept {return generation_;}

    // The cache retains unique_ptr<Model> ownership. This temporary
    // lease does NOT move a Model into shared ownership; it blocks
    // eviction/replacement/shutdown while a host reads its pointers.
    // Acquire on the cache render-owner thread, AFTER cache binding
    // (or before first binding, on the future owner thread).
    ModelReadLease acquireModelRead(const Model* model) const {
        requireNoDetachCallback("acquireModelRead");
        if(!model)return {};
        const auto thread=std::this_thread::get_id();
        if(resourceDevice_ && resourceThread_!=thread)
            throw std::logic_error("ModelResourceCache read on foreign GPU thread");
        if(readState_->ownerThread==std::thread::id{})
            readState_->ownerThread=thread;
        if(readState_->ownerThread!=thread ||
           !readState_->alive.load(std::memory_order_acquire))
            throw std::logic_error("ModelResourceCache reader owner expired or wrong thread");
        for(const auto& [key,owned]:models_) {
            (void)key;
            if(owned.get()==model)return ModelReadLease(readState_,model);
        }
        return {}; // rejected: Model belongs to another cache or was retired
    }
    ModelReadLease acquireModelRead(const std::string& key) const {
        requireNoDetachCallback("acquireModelRead");
        // Validate the owner BEFORE touching models_. A foreign-thread
        // lookup would otherwise race with the render owner's map writes
        // even though acquireModelRead(Model*) subsequently rejects it.
        const auto thread=std::this_thread::get_id();
        if((resourceDevice_ && resourceThread_!=thread) ||
           (readState_->ownerThread!=std::thread::id{} &&
            readState_->ownerThread!=thread) ||
           !readState_->alive.load(std::memory_order_acquire))
            throw std::logic_error("ModelResourceCache key read on foreign or expired owner");
        const auto it=models_.find(key);
        return it==models_.end()?ModelReadLease{}:
            acquireModelRead(it->second.get());
    }
    // One owner-thread read transaction protects ALL currently resident
    // Models across a legacy multi-model draw. It does not return a Model*
    // or change ownership, and never authorizes GPU resource destruction.
    // Acquire after synchronous asset loads, before accessing authored
    // pointers; release before attempting cache eviction/replacement.
    ModelCacheReadScope acquireFrameReadScope() const {
        requireNoDetachCallback("acquireFrameReadScope");
        const auto thread=std::this_thread::get_id();
        if(resourceDevice_ && resourceThread_!=thread)
            throw std::logic_error("ModelResourceCache frame read on foreign GPU thread");
        if(readState_->ownerThread==std::thread::id{})
            readState_->ownerThread=thread;
        if(readState_->ownerThread!=thread ||
           !readState_->alive.load(std::memory_order_acquire))
            throw std::logic_error("ModelResourceCache frame reader invalid or expired");
        return ModelCacheReadScope(readState_);
    }
    size_t activeModelReaders() const noexcept {
        return readState_->readers.load(std::memory_order_acquire);
    }
    struct ModelReadSafetyStats {
        size_t activeReaders=0;
        size_t peakReaders=0;
        uint64_t blockedMutations=0;
    };
    ModelReadSafetyStats modelReadSafetyStats() const noexcept {
        return {
            activeModelReaders(),
            readState_->peakReaders.load(std::memory_order_relaxed),
            readState_->blockedMutations.load(std::memory_order_relaxed)
        };
    }

    struct Stats {
        size_t models=0,textures=0,decodedCpuBytes=0;
    };

    // Model-cache keys include CPU geometry-policy variants; the source
    // file identity is independent of that variant and of load success.
    static std::string sourceForKey(std::string_view key) {
        constexpr std::string_view geometry="#geometry";
        if(key.ends_with(geometry))key.remove_suffix(geometry.size());
        if(key.empty())return {};
        return std::filesystem::path(std::string(key)).lexically_normal().generic_string();
    }
    struct AffectedModel {
        std::string key;
        const render::Model* model=nullptr; // null: failed-load tombstone
    };
    // Stable cache-key list for a changed source, including geometry
    // variants and failed/null cached loads. No assets are loaded here.
    std::vector<AffectedModel> affectedBySource(
            std::string_view source) const {
        requireReadOwnerThread("affectedBySource");
        std::vector<AffectedModel> result;
        const std::string normalized=sourceForKey(source);
        if(normalized.empty() || normalized.rfind("shape:",0)==0)return result;
        for(const auto& [key,model]:models_)
            if(sourceForKey(key)==normalized)
                result.push_back({key,model.get()});
        return result;
    }
    // Render-thread transaction. The host MUST detach TypeLook, pose/graph,
    // View packet, preview and other borrowed references in onDetach BEFORE
    // any GPU model is released. A throwing callback aborts without eviction.
    // Do not call while renderer submissions are in flight.
    template<class OnDetach>
    size_t invalidateSources(gfx::Device& dev,
                             const std::vector<std::string>& sources,
                             OnDetach&& onDetach) {
        requireNoDetachCallback("invalidateSources");
        bindResourceDevice(dev); // model must belong to this owner/device
        requireNoModelReaders("invalidateSources");
        std::unordered_set<std::string> changed;
        for(const auto& source:sources)
            if(!source.empty() && source.rfind("shape:",0)!=0)
                changed.insert(sourceForKey(source));
        if(changed.empty())return 0;
        std::vector<AffectedModel> affected;
        for(const auto& [key,model]:models_)
            if(changed.contains(sourceForKey(key)))
                affected.push_back({key,model.get()});
        if(affected.empty())return 0;
#ifndef __EMSCRIPTEN__
        preparation_.reset(); // join CPU worker before cache mutation
#endif
        // Callback can inspect Models and detach external references but
        // must not mutate this cache or retain fresh reader leases.
        // The RAII guard restores state even if onDetach throws.
        {
            ModelDetachCallbackScope callback(*this);
            onDetach(affected);
        }
        requireNoModelReaders("invalidateSources");
        // Retirement diagnostics execute synchronously from enqueue().
        // Keep them inside the same no-mutation scope as the detach hook.
        ModelDetachCallbackScope retirementCallback(*this);
        for(const auto& item:affected) {
            const auto it=models_.find(item.key);
            if(it==models_.end())continue;
            if(it->second)retired_.enqueueFrom(it->second);
            models_.erase(it);
            ++generation_;
        }
        // GPU handles from stale models remain alive until collectRetired()
        // observes a definitive completed-work barrier on the owner thread.
        return affected.size();
    }

#ifndef __EMSCRIPTEN__
    ModelPreparationService& preparation() noexcept {return preparation_;}
    const ModelPreparationService& preparation() const noexcept {
        return preparation_;
    }
#endif
    render::Model* find(const std::string& key) {
        requireReadOwnerThread("find");
        const auto it=models_.find(key);
        return it==models_.end()?nullptr:it->second.get();
    }
    const render::Model* find(const std::string& key) const {
        requireReadOwnerThread("find");
        const auto it=models_.find(key);
        return it==models_.end()?nullptr:it->second.get();
    }
    bool contains(const std::string& key) const {
        requireReadOwnerThread("contains");
        return models_.count(key)!=0; // null entries cache failed loads
    }
    // Insert without replacing a resident or a cached failed load.
    // The source model may already have been uploaded on the render thread.
    render::Model* insertIfAbsent(std::string key,
                                  std::unique_ptr<render::Model> model) {
        requireNoDetachCallback("insertIfAbsent");
        // New-key streaming may run under active read scopes, but map
        // mutation must still occur on the cache's owner/render thread.
        // Check BEFORE try_emplace: std::map is not concurrent-read/write.
        const auto thread=std::this_thread::get_id();
        if((resourceDevice_ && resourceThread_!=thread) ||
           (readState_->ownerThread!=std::thread::id{} &&
            readState_->ownerThread!=thread) ||
           !readState_->alive.load(std::memory_order_acquire))
            throw std::logic_error(
                "ModelResourceCache insert on foreign or expired owner");
        auto [it,inserted]=models_.try_emplace(std::move(key),std::move(model));
        if(inserted)++generation_;
        return it->second.get();
    }
    // For generated fallback assets and explicit reloads. Release the old
    // GPU buffers and invalidate host references before replacing them.
    template<class OnRelease>
    render::Model* replace(gfx::Device& dev,std::string key,
                           std::unique_ptr<render::Model> replacement,
                           OnRelease&& onRelease) {
        requireNoDetachCallback("replace");
        bindResourceDevice(dev); // old model moves into GPU-safe retirement queue
        requireNoModelReaders("replace");
        auto it=models_.find(key);
        if(it!=models_.end()) {
            if(it->second) {
                {
                    ModelDetachCallbackScope callback(*this);
                    onRelease(it->second.get());
                }
                requireNoModelReaders("replace");
                // The enqueue diagnostic observer must not recursively
                // modify this cache while the resident iterator is live.
                ModelDetachCallbackScope retirementCallback(*this);
                retired_.enqueueFrom(it->second);
            }
            it->second=std::move(replacement);
            ++generation_;
            return it->second.get();
        }
        return insertIfAbsent(std::move(key),std::move(replacement));
    }

    // Commit an entire set of already uploaded, unpublished native Models.
    // Both maps have the SAME node type; after validating all keys, node
    // extraction/transfer does not allocate GPU objects or CPU map nodes.
    // Model* addresses remain stable in appearance packets and attachments.
    void publishStagedModels(gfx::Device& dev,ModelMap& staged) {
        requireNoDetachCallback("publishStagedModels");
        bindResourceDevice(dev);
        for(const auto& [key,model]:staged) {
            (void)model;
            if(models_.contains(key))
                throw std::logic_error("speculative Model key already resident: "+key);
        }
        while(!staged.empty()) {
            auto node=staged.extract(staged.begin());
            models_.insert(std::move(node));
            ++generation_;
        }
    }

    // A failed appearance transaction must not destroy GPU geometry or
    // model textures while previous frame submissions can still use them.
    // Move a scratch Model into the SAME GPU-fenced retirement queue used
    // for ordinary resident models. On allocator failure the caller still
    // owns the Model and an explicit full-idle fallback is required.
    void retireUnpublished(gfx::Device& dev,std::unique_ptr<Model>& model) noexcept {
        if(!model)return;
        try {
            // A Model ownership visitor may currently be enumerating the
            // retirement queue. Do not append under that borrowed iterator.
            // On rejection the catch path requires explicit GPU idle before
            // releasing scratch resources; otherwise they are orphaned.
            requireNoDetachCallback("retireUnpublished");
            bindResourceDevice(dev);
            ModelDetachCallbackScope retirementCallback(*this);
            retired_.enqueueFrom(model);
        }catch(...) {
            ++stagingQueueFailures_;
            if(!boundToResourceDevice(dev)) {
                // The render host's generation was already invalidated:
                // do not even ask a potentially destroyed Device to wait.
                (void)model.release();
                ++stagingOrphanModels_;
                return;
            }
            try {
                if(dev.waitForResourceIdle()) {
                    render::releaseModel(dev,*model);
                    model.reset();
                } else {
                    // A failed idle barrier is not a release authorization.
                    // Keep handles alive until device teardown rather than
                    // potentially freeing GPU-visible memory.
                    (void)model.release();
                    ++stagingOrphanModels_;
                }
            }catch(...) {
                (void)model.release();
                ++stagingOrphanModels_;
            }
        }
    }
    // Authoring opt-in, set before upload. Games keep CPU release by default.
    void setRetainPickingGeometry(bool value){retainPickingGeometry_=value;}
    bool retainPickingGeometry()const{return retainPickingGeometry_;}
    uint64_t stagingQueueFailures() const noexcept{return stagingQueueFailures_;}
    uint64_t stagingOrphanModels() const noexcept{return stagingOrphanModels_;}

    // CPU preparation runs on workers; GPU upload and map admission happen
    // only on the render owner. Reject a failed/partial upload instead of
    // publishing unusable geometry or destroying GPU-visible scratch memory.
    // A duplicate resident key returns false BEFORE upload; the caller's
    // unuploaded PreparedModel may then be destroyed normally.
    template<class Prepared>
    bool publishPrepared(gfx::Device& dev,const std::string& key,
                         Prepared&& prepared) {
        requireNoDetachCallback("publishPrepared");
        bindResourceDevice(dev);
        if(models_.count(key))return false;
        try {
            if(prepared.model && !render::uploadModel(dev,*prepared.model,false,true,retainPickingGeometry_)) {
                ++preparedUploadFailures_;
                retireUnpublished(dev,prepared.model);
                return false;
            }
            insertIfAbsent(key,std::move(prepared.model));
            return true;
        }catch(...) {
            // A device or allocation exception may leave one GPU buffer
            // created before another failed. Preserve every partial Model
            // behind the same explicit GPU retirement barrier.
            ++preparedUploadFailures_;
            if(prepared.model)retireUnpublished(dev,prepared.model);
            throw;
        }
    }
    uint64_t preparedUploadFailures() const noexcept {
        return preparedUploadFailures_;
    }

    // Returned references are valid only on the same owner thread while
    // the cache remains alive. The check cannot police later use of a
    // borrowed reference; do not hand models() to a background worker.
    const ModelMap& models() const {
        requireReadOwnerThread("models");
        return models_;
    }
    std::shared_ptr<render::ModelTextureCache> textures() const noexcept {
        return textures_;
    }
    Stats stats() const {
        requireReadOwnerThread("stats");
        return {models_.size(),textures_->gpuCount(),textures_->decodedBytes()};
    }
    std::vector<std::string> modelFiles() const {
        requireReadOwnerThread("modelFiles");
        std::vector<std::string> files;
        std::unordered_set<std::string> seen;
        for(const auto& [key,model]:models_) {
            (void)model;
            if(key.rfind("shape:",0)==0)continue;
            const std::string source=sourceForKey(key);
            if(!source.empty() && seen.insert(source).second)
                files.push_back(source);
        }
        return files;
    }

    // Retain shape fallbacks and any model still referenced by active
    // appearances, including attachments/LOD variants. Callback lets hosts
    // invalidate their own animation/graph caches BEFORE GPU destruction.
    template<class OnRelease>
    size_t trimUnused(gfx::Device& dev,
                      const std::unordered_set<const render::Model*>& used,
                      OnRelease&& onRelease) {
        requireNoDetachCallback("trimUnused");
        bindResourceDevice(dev); // release happens at the host frame boundary
        requireNoModelReaders("trimUnused");
        size_t released=0;
        for(auto it=models_.begin();it!=models_.end();) {
            if(it->first.rfind("shape:",0)==0 ||
               used.contains(it->second.get())) {
                ++it;
                continue;
            }
            if(it->second) {
                {
                    ModelDetachCallbackScope callback(*this);
                    onRelease(it->second.get());
                }
                requireNoModelReaders("trimUnused");
                ModelDetachCallbackScope retirementCallback(*this);
                retired_.enqueueFrom(it->second);
            }
            it=models_.erase(it);
            ++released;
            ++generation_;
        }
        return released;
    }

    using RetirementQueue=
        AsyncResourceRetirementQueue<std::unique_ptr<Model>>;
    using RetirementStats=RetirementQueue::Stats;
    using RetirementDiagnosticObserver=RetirementQueue::DiagnosticObserver;
    // The host may attach a read-only token trace, e.g. in a GPU debug
    // panel. Callback exceptions cannot interrupt resource retirement.
    void setRetirementDiagnosticObserver(RetirementDiagnosticObserver observer) {
        requireReadOwnerThread("setRetirementDiagnosticObserver");
        requireNoDetachCallback("setRetirementDiagnosticObserver");
        retired_.setDiagnosticObserver(std::move(observer));
    }
    size_t pendingRetirements() const {
        requireReadOwnerThread("pendingRetirements");
        return retired_.pending();
    }
    RetirementStats retirementStats() const {
        requireReadOwnerThread("retirementStats");
        return retired_.stats();
    }
    // 0 disables backpressure. This is an object-count bound, NOT an exact
    // byte/VRAM quota (shared images/geometry pools have other owners).
    void setRetirementSoftLimit(size_t count) noexcept {
        retirementSoftLimit_=count;
    }
    size_t retirementSoftLimit() const noexcept {
        return retirementSoftLimit_;
    }
    uint64_t estimatedRetiredGpuBytes() const {
        requireReadOwnerThread("estimatedRetiredGpuBytes");
        return retired_.estimatedPendingBytes(
            [](const std::unique_ptr<Model>& model)->uint64_t {
                return model?estimateModelResident(*model).knownBytes():0;
            });
    }
    struct GpuOwnerEstimate {
        uint64_t residentModelDedicatedEstimate=0;
        uint64_t retiredModelDedicatedEstimate=0;
        uint64_t sharedTextureRgba8Equivalent=0;
        size_t uniqueSharedTextures=0,compressedSharedTextures=0;
        size_t sharedGeometryResidentModels=0;
        // Shared geometry-pool bytes belong to the device-level pool,
        // NOT this host owner. Query geometryPoolStats(device) separately.
    };
    // Read-only enumeration for owner-group accounting. Each Model belongs
    // to this cache or its pending GPU-retirement batches; no pointer is
    // retained by the visitor unless the host guarantees its lifetime.
    // Source aliases/tombstones do not count as distinct live GPU Models.
    template<class Visit>
    void forEachOwnedModel(Visit&& visit) const {
        requireReadOwnerThread("forEachOwnedModel");
        // A visitor may inspect and even recursively enumerate Models, but
        // must not replace/evict/publish while the outer iterator is live.
        ModelDetachCallbackScope callback(*this);
        for(const auto& [key,model]:models_) {
            (void)key;
            if(model)visit(*model,false); // resident
        }
        retired_.forEachPending([&](const std::unique_ptr<Model>& model) {
            if(model)visit(*model,true); // waiting on its own GPU token
        });
    }

    // Models can share geometry pages across multiple hosts. Give the
    // device-level geometry pool ONE combined model set when computing
    // page capacity; include retired models until their GPU token completes.
    std::vector<const Model*> geometryPoolModelReferences() const {
        requireReadOwnerThread("geometryPoolModelReferences");
        std::vector<const Model*> result;
        for(const auto& [key,model]:models_) {
            (void)key;
            if(model && model->geometryPoolPage)result.push_back(model.get());
        }
        retired_.forEachPending([&](const std::unique_ptr<Model>& model) {
            if(model && model->geometryPoolPage)result.push_back(model.get());
        });
        return result;
    }
    GpuOwnerEstimate gpuOwnerEstimate() const {
        requireReadOwnerThread("gpuOwnerEstimate");
        GpuOwnerEstimate result;
        const auto max=std::numeric_limits<uint64_t>::max();
        auto add=[](uint64_t& dst,uint64_t value) {
            dst=value>max-dst?max:dst+value;
        };
        for(const auto& [key,model]:models_) {
            (void)key;
            if(!model)continue;
            const auto estimate=estimateModelResident(*model);
            add(result.residentModelDedicatedEstimate,
                estimate.knownBytes());
            result.sharedGeometryResidentModels+=estimate.sharedGeometryModels;
        }
        result.retiredModelDedicatedEstimate=estimatedRetiredGpuBytes();
        const auto shared=textures_->gpuOwnerEstimate();
        result.sharedTextureRgba8Equivalent=
            shared.rgba8BaseMipEquivalentBytes;
        result.uniqueSharedTextures=shared.uniqueTextures;
        result.compressedSharedTextures=shared.compressedTextures;
        return result;
    }
    // 0 disables the estimated-byte admission limit. The estimate excludes
    // shared geometry pages, borrowed textures, morph height, driver padding
    // and hidden RHI allocations. Not an actual VRAM usage counter.
    void setEstimatedRetirementByteLimit(uint64_t value) noexcept {
        estimatedByteLimit_=value;
    }
    uint64_t estimatedRetirementByteLimit() const noexcept {
        return estimatedByteLimit_;
    }
    bool publicationBackpressured() const {
        requireReadOwnerThread("publicationBackpressured");
        return retired_.saturated(retirementSoftLimit_) ||
            (estimatedByteLimit_!=0 &&
             estimatedRetiredGpuBytes()>=estimatedByteLimit_);
    }
    // Schedule async completion tokens and poll old batches without waiting
    // for whole-GPU idle. Each batch covers exactly the resources queued
    // before its marker; newer evictions can NEVER ride an older fence.
    // Returns true only once all pending resources have been released.
    bool collectRetired(gfx::Device& dev) {
        requireNoDetachCallback("collectRetired");
        bindResourceDevice(dev);
        if(retired_.empty())return true;
        const auto before=retired_.pending();
        // GPU token diagnostics may synchronously call back into this host.
        // They may inspect residency but must never publish, evict or poll
        // retirement recursively while the queue owns live iterators.
        ModelDetachCallbackScope retirementCallback(*this);
        const bool done=retired_.collect(
            [&]{return dev.beginResourceRetirement();},
            [&](uint64_t token){return dev.resourceRetirementComplete(token);},
            [&](uint64_t token) {
                // On normal completed batches close the native marker.
                // If a later unsupported-marker fallback proved full
                // device idle, a previous FAILED marker can be abandoned
                // without freeing any in-flight resource prematurely.
                if(dev.resourceRetirementStatus(token)==
                   gfx::ResourceRetirementStatus::Complete)
                    dev.endResourceRetirement(token);
                else
                    dev.abandonRetirementTokenAfterIdle(token);
            },
            [&]{return dev.waitForResourceIdle();},
            [&](std::unique_ptr<Model>& model) {
                if(model)render::releaseModel(dev,*model);
            },
            [&](uint64_t token) {
                return dev.resourceRetirementStatus(token)==
                    gfx::ResourceRetirementStatus::Failed;
            });
        if(retired_.pending()<before)textures_->trim(dev);
        return done;
    }

    // Full shutdown requires true device-wide idle; external callers must
    // NOT pass an arbitrary boolean claiming a fence was completed.
    void shutdown(gfx::Device& dev) {
        shutdownImpl(dev,false);
    }
private:
    friend class ModelAssetProvider;
    // Reachable only through ModelAssetProvider's private, GPU-token
    // approved whole-scene retirement path. No earlier Model retirement
    // batches are allowed: they may own separate incomplete/failed tokens.
    // This path releases all live cache Models and shared textures only
    // once the OUTER scene token proved prior GPU command completion.
    void shutdownAfterCompletedSceneToken(gfx::Device& dev) {
        if(!retired_.empty())
            throw std::logic_error(
                "pending nested model retirements require full GPU-idle shutdown");
        shutdownImpl(dev,true);
    }
    void shutdownImpl(gfx::Device& dev,bool completedSceneToken) {
        requireNoDetachCallback("shutdown");
        bindResourceDevice(dev);
        requireNoModelReaders("shutdown");
#ifndef __EMSCRIPTEN__
        preparation_.reset(); // join all texture/model CPU workers before release
#endif
        // Fail closed: no GPU objects (nor borrowed CPU Model storage) may
        // be destroyed if the backend cannot prove prior GPU completion.
        if(!completedSceneToken && !dev.waitForResourceIdle())
            throw std::runtime_error("model cache shutdown: GPU retirement barrier failed");
        for(auto& [key,model]:models_) {
            (void)key;
            if(model)render::releaseModel(dev,*model);
        }
        models_.clear();
        ++generation_;
        // The successful barrier above makes every token safe to close,
        // including callbacks whose status notification is still queued.
        // Also guard diagnostics emitted by the full-idle shutdown drain.
        ModelDetachCallbackScope retirementCallback(*this);
        if(!retired_.drain([]{return true;},
            [&](uint64_t token) {
                // A successful waitForResourceIdle already proved safety.
                dev.abandonRetirementTokenAfterIdle(token);
            },
            [&](std::unique_ptr<Model>& model) {
                if(model)render::releaseModel(dev,*model);
            }))
            throw std::runtime_error("model cache: retired GPU resources not released");
        textures_->shutdown(dev);
        // The cache is empty and its GPU leases have been released. A
        // later init may bind this same host to a NEW device only after
        // every scoped observer for the old device has been released.
        resourceDevice_=nullptr;
        resourceThread_={};
        resourceEpoch_=0;
        resourceEpochLease_={};
        // An empty owner may be rebound on a different render thread
        // after explicit shutdown. All readers were checked above.
        // Keeping the previous reader thread would permanently reject a
        // new Device owner even though all cached GPU assets are gone.
        readState_->ownerThread={};
    }
private:
    bool retainPickingGeometry_=false;
    // All map readers require the render/reader owner. The map is NOT a
    // concurrent-read/write container, including during new-key streaming.
    // Before first binding/reader acquisition the host must serialize its
    // initial population itself; afterward foreign threads fail before
    // touching map storage. Do not call concurrently with first bind.
    void requireReadOwnerThread(const char* operation) const {
        const auto thread=std::this_thread::get_id();
        if((resourceDevice_ && resourceThread_!=thread) ||
           (readState_->ownerThread!=std::thread::id{} &&
            readState_->ownerThread!=thread) ||
           !readState_->alive.load(std::memory_order_acquire))
            throw std::logic_error(
                std::string("ModelResourceCache ")+operation+
                " on foreign or expired owner thread");
    }
    // Both host detach/release callbacks AND borrowed-Model enumeration
    // visitors hold this owner-thread callback guard. Nested read-only
    // inspection is valid, hence a depth counter rather than a boolean.
    // Cache writes or fresh CPU reader leases remain forbidden at ANY depth.
    class ModelDetachCallbackScope final {
    public:
        explicit ModelDetachCallbackScope(const ModelResourceCache& cache) noexcept
            :cache_(cache) {
            ++cache_.cacheCallbackDepth_;
        }
        ~ModelDetachCallbackScope(){--cache_.cacheCallbackDepth_;}
        ModelDetachCallbackScope(const ModelDetachCallbackScope&)=delete;
        ModelDetachCallbackScope& operator=(const ModelDetachCallbackScope&)=delete;
    private:
        const ModelResourceCache& cache_;
    };
    void requireNoDetachCallback(const char* operation) const {
        if(cacheCallbackDepth_!=0)
            throw std::logic_error(
                std::string("ModelResourceCache cannot ")+operation+
                " inside Model detach/release callback");
    }
    void requireNoModelReaders(const char* operation) const {
        if(activeModelReaders()!=0) {
            readState_->blockedMutations.fetch_add(
                1,std::memory_order_relaxed);
            throw std::logic_error(std::string("ModelResourceCache cannot ") +
                operation + " while Model read leases are active");
        }
    }
    mutable size_t cacheCallbackDepth_=0; // render-owner-thread only
    std::shared_ptr<ModelReadLeaseState> readState_=
        std::make_shared<ModelReadLeaseState>();
    std::shared_ptr<const void> lifetimeToken_=
        std::make_shared<const uint8_t>(1);
    mutable gfx::Device* resourceDevice_=nullptr;
    mutable std::thread::id resourceThread_;
    mutable uint64_t resourceEpoch_=0;
    mutable RenderDeviceLifetime::Lease resourceEpochLease_;
#ifndef __EMSCRIPTEN__
    ModelPreparationService preparation_;
#endif
    uint64_t generation_=0;
    uint64_t stagingQueueFailures_=0,stagingOrphanModels_=0;
    uint64_t preparedUploadFailures_=0;
    size_t retirementSoftLimit_=128;
    uint64_t estimatedByteLimit_=0; // optional; never claims exact VRAM usage
    ModelMap models_;
    AsyncResourceRetirementQueue<std::unique_ptr<Model>> retired_;
    std::shared_ptr<render::ModelTextureCache> textures_=
        std::make_shared<render::ModelTextureCache>();
};

} // namespace aiverse::render
