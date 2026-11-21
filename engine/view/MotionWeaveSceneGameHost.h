// AIVERSE — concrete SceneLoadCoordinator / Simulation native MotionWeave
// owner. No genre modules and no cosmetic animation in authoritative ECS.
// This is the final presentation-side adapter to the canonical scene
// headers synchronized from main, not a duplicate scene implementation.
#pragma once

#include "MotionWeaveSceneCoordinatorAdapter.h"
#include "engine/scene/SceneLoadCoordinator.h"
#include "engine/scene/SceneRuntime.h"
#include "engine/sim/Simulation.h"
#include <cstdint>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

class MotionWeaveSceneGameHost final {
public:
    using Owner=MotionWeaveSceneOwnedHost::Owner;
    using Object=MotionWeaveSceneOwnedHost::Object;
    using Statistics=MotionWeaveSceneOwnedHost::Statistics;
    using FrameStatistics=AnimatedSceneAttachmentSources::Statistics;

    // The native host owns the Simulation, its SceneLoadCoordinator, the
    // gfx::Device and its RenderDeviceLifetime. These MUST outlive us.
    // Construct only on the render/scene structural owner thread.
    MotionWeaveSceneGameHost(
            Simulation& simulation,
            scene::SceneLoadCoordinator& scenes,
            gfx::Device& device,
            render::RenderDeviceLifetime::Lease lease)
        :simulation_(simulation),scenes_(scenes),device_(device),
         visualHost_(std::move(lease)),adapter_(device_,visualHost_) {}

    MotionWeaveSceneGameHost(const MotionWeaveSceneGameHost&)=delete;
    MotionWeaveSceneGameHost& operator=(const MotionWeaveSceneGameHost&)=delete;

    // The scene coordinator's asynchronous parse and deterministic
    // activation are NOT hidden. Request/poll are still host-driven.
    scene::SceneTicket request(std::string asset,
            scene::LoadMode mode=scene::LoadMode::Additive,
            bool persistent=false) {
        return scenes_.request(std::move(asset),mode,persistent);
    }
    void poll() {scenes_.poll();}
    bool ready(scene::SceneTicket ticket) const {
        return scenes_.ready(ticket);
    }

    // Builder:
    // bool(const scene::SceneAsset&,Owner& sceneOwnedProvider,
    //      std::vector<Object>& publishedVisuals,std::string& error)
    //
    // A builder MUST load every parent, static child and independently
    // animated child Model through the SAME new dedicated provider.
    // Models and immutable Graph snapshots remain cosmetic and are never
    // inserted into Simulation::world().
    //
    // Only call for a Ready ticket at the structural owner barrier.
    template<class Build>
    bool commitReady(scene::SceneTicket ticket,Build&& build,
                     Statistics& stats,std::string* error=nullptr) {
        return adapter_.commitReady(
            scenes_,ticket,std::forward<Build>(build),
            [this] {
                const World& authoritative=simulation_.world();
                return MotionWeaveSceneCoordinatorAdapter::
                    canonicalActiveSceneIds(
                        authoritative.resourceConst<scene::SceneState>());
            },stats,error);
    }

    // Opt-in shared GPU residency across multiple Active scenes:
    // Build(manifest, const residentProviderOrNull, newProviderOutput,
    //       objectsOutput, error). The first scene must prewarm and return
    // a new owner. Later scenes reuse its already resident immutable models
    // without allocating a second native GPU Model provider.
    template<class Build>
    bool commitReadyShared(scene::SceneTicket ticket,
                           const std::string& residencyKey,Build&& build,
                           Statistics& stats,std::string* error=nullptr) {
        return adapter_.commitReadyShared(
            scenes_,ticket,residencyKey,std::forward<Build>(build),
            [this] {
                const World& authoritative=simulation_.world();
                return MotionWeaveSceneCoordinatorAdapter::
                    canonicalActiveSceneIds(
                        authoritative.resourceConst<scene::SceneState>());
            },stats,error);
    }

    // This is a short-lived structural-barrier lookup. The returned
    // Model pointers are not an extra lease and must never outlive the
    // current scene operation or render-device generation.
    const render::ModelAssetProvider* sharedProvider(
            const std::string& residencyKey) const noexcept {
        return visualHost_.sharedProvider(residencyKey);
    }
    size_t sharedProviderReferences(
            const std::string& residencyKey) const noexcept {
        return visualHost_.sharedProviderReferences(residencyKey);
    }

    bool cancel(scene::SceneTicket ticket) {
        return adapter_.cancel(scenes_,ticket);
    }
    bool unload(scene::SceneId id) {
        return adapter_.unload(scenes_,id);
    }

    // Do not use SceneLoadCoordinator's async worker timing as a
    // deterministic scene activation signal. On rollback/restore call
    // only AFTER Simulation::loadState(), then await each returned
    // residency-only SceneTicket without respawning gameplay entities.
    std::vector<scene::SceneTicket> restoreResidencyAfterRollback() {
        return adapter_.restoreResidencyAfterRollback(scenes_);
    }

    // Live child Graphs are updated ONCE per frame on the presentation
    // owner thread. After this call, prepareParent/prepareChild may borrow
    // the current matrices until the next sync or structural mutation.
    bool sync(scene::SceneId id,float dt,FrameStatistics& stats,
              std::string* error=nullptr) {
        const auto* cache=visualHost_.liveResources(id.value);
        if(!cache) {
            if(error)*error="visual scene has no live Model provider";
            return false;
        }
        return visualHost_.visuals().sync(
            id.value,*cache,dt,stats,error);
    }

    // The parent Graph pose is still owned by the native host/Animation
    // bridge; prepareParent NEVER evaluates it a second time.
    bool prepareParent(scene::SceneId id,const std::string& objectKey,
                       const render::mat4& world,
                       ImportedAttachmentAuthoring::Prepared& out,
                       std::string* error=nullptr) const {
        const auto* cache=visualHost_.liveResources(id.value);
        if(!cache) {
            if(error)*error="visual scene has no live Model provider";
            return false;
        }
        return visualHost_.visuals().prepareParent(
            id.value,objectKey,*cache,world,out,error);
    }

    bool prepareChild(scene::SceneId id,const std::string& objectKey,
                      const std::string& childId,
                      const render::ModelDraw& parentDraw,
                      std::span<const render::mat4> parentNodes,
                      AnimatedSceneAttachmentSources::Prepared& out,
                      std::string* error=nullptr) const {
        const auto* cache=visualHost_.liveResources(id.value);
        if(!cache) {
            if(error)*error="visual scene has no live Model provider";
            return false;
        }
        return visualHost_.visuals().prepareChild(
            id.value,objectKey,childId,*cache,parentDraw,parentNodes,
            out,error);
    }

    // Call after the render packets have been submitted and their
    // move-only CPU ModelReadLease objects have been destroyed.
    bool collectRetired() noexcept {
        return visualHost_.collectRetired(device_);
    }

    // May block on GPU idle for nested pending retirement or explicit
    // device teardown. Do not destroy gfx::Device until this succeeds.
    bool completeForShutdown() {
        return visualHost_.completeForShutdown(device_);
    }

    size_t activeSceneCount() const noexcept {
        return visualHost_.activeSceneCount();
    }
    size_t pendingModelOwners() const noexcept {
        return visualHost_.pendingModelOwners();
    }
    uint64_t orphanedOwners() const noexcept {
        return visualHost_.orphanedOwners();
    }
    uint64_t completionProvenShutdowns() const noexcept {
        return visualHost_.completionProvenProviderShutdowns();
    }

private:
    Simulation& simulation_; // authority: READ SceneState only
    scene::SceneLoadCoordinator& scenes_;
    gfx::Device& device_;
    MotionWeaveSceneOwnedHost visualHost_;
    MotionWeaveSceneCoordinatorAdapter adapter_;
};

} // namespace aiverse::view
