// AIVERSE — genre-neutral native scene visual owner.
// Combines transactional cosmetic scene publication with REAL per-scene
// ModelAssetProvider ownership and the GPU-fenced provider retirement queue.
// No authoritative ECS/SceneOwner or gameplay components are imported.
#pragma once
#include "MotionWeaveScenePublication.h"
#include "engine/render/SceneModelProviderRetirement.h"
#include "engine/render/SceneModelProviderResidency.h"
#include <cstdint>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

class MotionWeaveSceneOwnedHost final {
public:
    using Owner=std::unique_ptr<render::ModelAssetProvider>;
    using Publisher=MotionWeaveScenePublication;
    using Object=Publisher::Object;
    using Statistics=Publisher::Statistics;

    explicit MotionWeaveSceneOwnedHost(
            render::RenderDeviceLifetime::Lease lease)
        :deviceLease_(lease),retirement_(std::move(lease)) {}
    MotionWeaveSceneOwnedHost(const MotionWeaveSceneOwnedHost&)=delete;
    MotionWeaveSceneOwnedHost& operator=(const MotionWeaveSceneOwnedHost&)=delete;

    ~MotionWeaveSceneOwnedHost() {
        // A missing/failed explicit shutdown is NOT GPU-completion proof.
        // Destroy cosmetic Graph/World data first, then deliberately retain
        // unresolved provider owners until native device teardown. This is
        // a diagnosed orphan rather than freeing GPU-visible resources.
        visuals_.clear();
        for(auto& [id,slot]:slots_) {
            (void)id;
            if(slot.live) { (void)slot.live.release();++orphanedLive_; }
            if(slot.staged){(void)slot.staged.release();++orphanedLive_;}
        }
    }

    // A rejected host manifest/prewarm may already have GPU buffers
    // allocated in a provider BEFORE a stageScene() call. Never destroy
    // such a provider from the adapter's local unique_ptr destructor.
    void retireUnstaged(gfx::Device& device,Owner& owner) noexcept {
        retirement_.retire(device,owner);
    }

    // Full CPU+GPU Model owner for exactly ONE scene. All parent, static
    // child and independent child Model pointers in objects MUST belong to
    // incoming->resources(); the bridge performs read-lease validation.
    // Incoming is consumed on success OR failure. On failure it is retired
    // behind a completion token; never destroy it directly.
    // Stage before authoritative SceneLoadCoordinator::commit(ticket).
    bool stage(gfx::Device& device,uint64_t sceneId,
               std::span<const Object> objects,Owner incoming,
               Statistics& stats,std::string* error=nullptr) {
        if(error)error->clear();
        if(!incoming || sceneId==0 ||
           !retirement_.validDevice(device)) {
            if(error)*error=
                "scene stage needs valid GPU epoch, nonzero ID and Model owner";
            if(incoming)retirement_.retire(device,incoming);
            return false;
        }
        try {
            // Adopt the SAME real device generation used by the host and
            // retirement queue. Reject an old provider after RHI recreate
            // even if the allocator reused the gfx::Device address.
            incoming->setGpuDeviceLease(deviceLease_);
            incoming->resources().bindResourceDevice(device,deviceLease_);
            auto [it,inserted]=slots_.try_emplace(sceneId);
            (void)inserted;
            // Discard a prior uncommitted candidate FIRST. It cannot be
            // destroyed directly because it may contain uploaded GPU data.
            visuals_.discardStaged(sceneId);
            releaseStaged(device,it->second);
            if(!visuals_.stageScene(sceneId,objects,incoming->resources(),
                                    stats,error)) {
                retirement_.retire(device,incoming);
                return false;
            }
            // Move is noexcept. The map slot was reserved before any
            // appearance graph publication.
            it->second.staged=std::move(incoming);
            return true;
        }catch(const std::exception& ex) {
            visuals_.discardStaged(sceneId);
            if(auto it=slots_.find(sceneId);it!=slots_.end())
                releaseStaged(device,it->second);
            retirement_.retire(device,incoming);
            if(error)*error=ex.what();
            return false;
        }catch(...) {
            visuals_.discardStaged(sceneId);
            if(auto it=slots_.find(sceneId);it!=slots_.end())
                releaseStaged(device,it->second);
            retirement_.retire(device,incoming);
            if(error)*error="unknown scene provider staging failure";
            return false;
        }
    }

    // Opt-in shared REAL provider lifetime across additive/replaced scenes.
    // First caller supplies a fully prewarmed new provider; subsequent
    // callers supply nullptr and snapshots already resident in that SAME
    // provider. Never create a second provider for an occupied key.
    // Stage/commit/rollback still preserve separate cosmetic ECS Worlds.
    bool stageShared(gfx::Device& device,uint64_t sceneId,
                     const std::string& providerKey,
                     std::span<const Object> objects,Owner incoming,
                     Statistics& stats,std::string* error=nullptr) {
        if(error)error->clear();
        if(sceneId==0 || providerKey.empty() || providerKey.size()>256 ||
           !retirement_.validDevice(device)) {
            if(error)*error="shared scene provider key, ID or GPU epoch invalid";
            retirement_.retire(device,incoming);
            return false;
        }
        SharedLease lease;
        try {
            const bool exists=shared_.find(providerKey)!=nullptr;
            if(exists==bool(incoming)) {
                if(error)*error=exists
                    ? "shared scene provider key already resident"
                    : "shared scene provider key not resident";
                retirement_.retire(device,incoming);
                return false;
            }
            // Reserve the slot before acquiring ownership, but do not
            // drop the OLD staged ref until a NEW shared ref is held.
            auto [it,inserted]=slots_.try_emplace(sceneId);
            (void)inserted;
            if(incoming) {
                incoming->setGpuDeviceLease(deviceLease_);
                incoming->resources().bindResourceDevice(
                    device,deviceLease_);
                if(!shared_.adopt(providerKey,incoming,lease))
                    throw std::runtime_error(
                        "shared scene provider registration rejected");
            }else if(!shared_.acquire(providerKey,lease)) {
                throw std::runtime_error(
                    "shared scene provider acquire rejected");
            }
            const auto* provider=shared_.find(lease);
            if(!provider)
                throw std::runtime_error(
                    "shared scene provider lease became stale");
            visuals_.discardStaged(sceneId);
            releaseStaged(device,it->second);
            if(!visuals_.stageScene(
                    sceneId,objects,provider->resources(),stats,error)) {
                shared_.release(device,lease);
                return false;
            }
            it->second.stagedShared=std::move(lease);
            return true;
        }catch(const std::exception& ex) {
            visuals_.discardStaged(sceneId);
            if(auto it=slots_.find(sceneId);it!=slots_.end())
                releaseStaged(device,it->second);
            shared_.release(device,lease);
            retirement_.retire(device,incoming);
            if(error)*error=ex.what();
            return false;
        }catch(...) {
            visuals_.discardStaged(sceneId);
            if(auto it=slots_.find(sceneId);it!=slots_.end())
                releaseStaged(device,it->second);
            shared_.release(device,lease);
            retirement_.retire(device,incoming);
            if(error)*error="unknown shared scene stage failure";
            return false;
        }
    }

    // Phase two: ONLY AFTER the authoritative coordinator has committed.
    // Old cosmetic Graph/World data is destroyed before its Model provider
    // enters GPU retirement. The new owner remains alive for all future
    // parent/child read leases and draw packets until the next lifecycle op.
    bool commit(gfx::Device& device,uint64_t sceneId) {
        if(!retirement_.validDevice(device))return false;
        const auto it=slots_.find(sceneId);
        if(it==slots_.end() ||
           (!it->second.staged && !it->second.stagedShared) ||
           !visuals_.commitStaged(sceneId))return false;
        Owner retired=std::move(it->second.live);
        SharedLease sharedRetired=std::move(it->second.liveShared);
        it->second.live=std::move(it->second.staged);
        it->second.liveShared=std::move(it->second.stagedShared);
        retirement_.retire(device,retired);
        shared_.release(device,sharedRetired);
        return true;
    }

    // A failed authoritative scene commit must discard the candidate,
    // not the previous visible generation or the previous Model provider.
    bool discard(gfx::Device& device,uint64_t sceneId) {
        const auto it=slots_.find(sceneId);
        if(it==slots_.end())return false;
        const bool visual=visuals_.discardStaged(sceneId);
        const bool hadOwner=bool(it->second.staged) ||
                            bool(it->second.stagedShared);
        releaseStaged(device,it->second);
        return visual || hadOwner;
    }

    // Host must have finished its last frame-scoped Prepared packet and
    // submitted the corresponding Renderer3D command buffer BEFORE unload.
    // This first detaches animation/Graph owners, then queues the matching
    // actual Model provider (including Model GPU/texture/rollback owners).
    bool unload(gfx::Device& device,uint64_t sceneId) {
        const bool visible=visuals_.unload(sceneId);
        const auto it=slots_.find(sceneId);
        if(it==slots_.end())return visible;
        releaseStaged(device,it->second);
        releaseLive(device,it->second);
        slots_.erase(it);
        return visible;
    }

    // After an authoritative Replace, supply the COMPLETE set of active
    // SceneState IDs. Persistent scenes survive if still active. Reject
    // invalid IDs BEFORE changing any visual/resource ownership.
    size_t unloadAbsent(gfx::Device& device,
                        std::span<const uint64_t> activeIds) {
        std::set<uint64_t> keep;
        for(uint64_t id:activeIds)
            if(id==0 || !keep.insert(id).second)
                throw std::invalid_argument(
                    "duplicate or zero active authoritative scene ID");
        const size_t removed=visuals_.unloadAbsent(activeIds);
        for(auto it=slots_.begin();it!=slots_.end();) {
            if(keep.count(it->first)) {++it;continue;}
            releaseStaged(device,it->second);
            releaseLive(device,it->second);
            it=slots_.erase(it);
        }
        return removed;
    }

    // After simulation rollback/loadState, all presentation sources,
    // snapshots and old provider generations are stale. No gameplay state
    // is read or modified by this operation.
    void resetAfterRollback(gfx::Device& device) {
        visuals_.resetAfterRollback();
        for(auto& [id,slot]:slots_) {
            (void)id;
            releaseStaged(device,slot);
            releaseLive(device,slot);
        }
        slots_.clear();
    }

    // Poll once at a device owner frame boundary AFTER render submission,
    // not while any scene is recording GPU work.
    bool collectRetired(gfx::Device& device) noexcept {
        return retirement_.collect(device);
    }

    // Explicit final barrier BEFORE destroying gfx::Device or retiring its
    // RenderDeviceLifetime generation. On failure, pending owners stay in
    // the retirement queue; NEVER destruct/destroy the Device first.
    bool completeForShutdown(gfx::Device& device) {
        resetAfterRollback(device);
        return retirement_.completeForShutdown(device);
    }

    // Short-lived view of the real GPU resource owner for a committed
    // scene. Never retain across stage/commit/unload/rollback or GPU epoch
    // changes. The native host uses it only at a presentation frame barrier.
    const render::ModelResourceCache* liveResources(
            uint64_t sceneId) const noexcept {
        const auto it=slots_.find(sceneId);
        if(it==slots_.end() || !visuals_.contains(sceneId))
            return nullptr;
        if(it->second.live)return &it->second.live->resources();
        const auto* shared=shared_.find(it->second.liveShared);
        return shared?&shared->resources():nullptr;
    }
    // Structural-barrier-only lookups for builders resolving a second
    // scene from already resident shared Model/Graph snapshots.
    const render::ModelAssetProvider* sharedProvider(
            const std::string& key) const noexcept {
        return shared_.find(key);
    }
    size_t sharedProviderReferences(
            const std::string& key) const noexcept {
        return shared_.references(key);
    }
    size_t sharedProviderCount() const noexcept {
        return shared_.ownerCount();
    }
    Publisher& visuals() noexcept{return visuals_;}
    const Publisher& visuals() const noexcept{return visuals_;}
    bool contains(uint64_t sceneId) const noexcept {
        return visuals_.contains(sceneId);
    }
    size_t activeSceneCount() const noexcept {
        return visuals_.sceneCount();
    }
    size_t pendingModelOwners() const noexcept {
        return retirement_.pendingOwners();
    }
    uint64_t orphanedOwners() const noexcept {
        return orphanedLive_+retirement_.orphanedOwners();
    }
    uint64_t gpuShutdownFailures() const noexcept {
        return retirement_.shutdownFailures();
    }
    uint64_t completionProvenProviderShutdowns() const noexcept {
        return retirement_.completionProvenShutdowns();
    }
    uint64_t cpuLoaderShutdownFailures() const noexcept {
        return retirement_.cpuQueueShutdownFailures();
    }
    uint64_t providerEnqueueFailures() const noexcept {
        return retirement_.enqueueFailures();
    }
    uint64_t providerPollFailures() const noexcept {
        return retirement_.pollFailures();
    }

private:
    using SharedLease=render::SceneModelProviderResidency::Lease;
    struct Slot {
        Owner live;
        Owner staged;
        SharedLease liveShared;
        SharedLease stagedShared;
    };
    void releaseStaged(gfx::Device& device,Slot& slot) noexcept {
        retirement_.retire(device,slot.staged);
        shared_.release(device,slot.stagedShared);
    }
    void releaseLive(gfx::Device& device,Slot& slot) noexcept {
        retirement_.retire(device,slot.live);
        shared_.release(device,slot.liveShared);
    }
    Publisher visuals_;
    std::map<uint64_t,Slot> slots_;
    render::RenderDeviceLifetime::Lease deviceLease_;
    render::SceneModelProviderRetirement retirement_;
    // Must be destroyed BEFORE retirement_ (destruction order reversed).
    render::SceneModelProviderResidency shared_{retirement_};
    uint64_t orphanedLive_=0;
};

} // namespace aiverse::view
