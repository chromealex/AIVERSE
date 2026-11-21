// AIVERSE MotionWeave — structural-barrier adapter for the real
// scene::SceneLoadCoordinator API (on feature/scene-runtime-multiworld).
//
// This is a template intentionally independent of peer branch headers:
// real native hosts instantiate it with SceneLoadCoordinator + SceneTicket.
// The template CALLS the peer API's prepared(), commit(), cancel(), unload(),
// invalidateAfterRestore() and queueRestoredResidency() directly. It does
// not invent/duplicate those APIs or import gameplay ownership into View.
#pragma once
#include "MotionWeaveSceneOwnedHost.h"
#include <cstdint>
#include <exception>
#include <memory>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

class MotionWeaveSceneCoordinatorAdapter final {
public:
    using Host=MotionWeaveSceneOwnedHost;
    using Owner=Host::Owner;
    using Object=Host::Object;
    using Statistics=Host::Statistics;

    MotionWeaveSceneCoordinatorAdapter(gfx::Device& device,Host& host)
        :device_(device),host_(host){}
    MotionWeaveSceneCoordinatorAdapter(
        const MotionWeaveSceneCoordinatorAdapter&)=delete;
    MotionWeaveSceneCoordinatorAdapter& operator=(
        const MotionWeaveSceneCoordinatorAdapter&)=delete;

    // Native scene::SceneState lives on a peer branch. This helper accepts
    // its actual stateView()/World.resourceConst pointer without copying or
    // linking its authoritative types into presentation code. SceneRecord
    // has an enum ScenePhase with Active; map traversal is canonical by ID.
    template<class State>
    static std::vector<uint64_t> canonicalActiveSceneIds(
            const State* state) {
        std::vector<uint64_t> ids;
        if(!state)return ids;
        ids.reserve(state->scenes.size());
        for(const auto& [id,record]:state->scenes)
            if(record.phase==decltype(record.phase)::Active)
                ids.push_back(id);
        return ids;
    }

    // Builder signature:
    // bool(const scene::SceneAsset&, Owner& outProvider,
    //      std::vector<Object>& outObjects, std::string& error)
    //
    // The builder resolves each stable SceneObjectAsset::id to its visual
    // authored layout and parent/child appearance snapshots, published in
    // ONE dedicated provider for that scene. It may allocate/upload Models.
    // The adapter takes responsibility for retiring even a failed builder's
    // partial provider via the GPU completion queue.
    //
    // ActiveIds signature: std::vector<uint64_t>() or std::span<const
    // uint64_t>() containing COMPLETE, CANONICAL ACTIVE IDs read from
    // authoritative World SceneState AFTER coordinator.commit() succeeds.
    // This includes still-active persistent scenes under Replace.
    //
    // Invoke once at native owner/tick barrier for a Ready SceneTicket.
    // Never call inside asynchronous manifest poll or an in-flight draw.
    template<class Coordinator,class Ticket,class Build,class ActiveIds>
    bool commitReady(Coordinator& coordinator,const Ticket& ticket,
                     Build&& build,ActiveIds&& activeIds,
                     Statistics& stats,std::string* error=nullptr) {
        if(error)error->clear();
        Owner provider;
        bool staged=false;
        bool committed=false;
        try {
            if(!ticket.valid())
                throw std::invalid_argument("scene ticket invalid");
            const auto* manifest=coordinator.prepared(ticket);
            if(!manifest) {
                if(error)*error="scene coordinator ticket is not Ready";
                return false;
            }

            std::vector<Object> objects;
            std::string reason;
            bool built=false;
            try {
                built=build(*manifest,provider,objects,reason);
            }catch(const std::exception& e) {
                reason=e.what();
            }catch(...) {
                reason="unknown scene appearance builder exception";
            }
            if(!built || !provider) {
                host_.retireUnstaged(device_,provider);
                if(reason.empty())reason="scene builder returned no provider";
                if(error)*error=reason;
                // A failed native preparation must not leave an
                // authoritative scene in Loading indefinitely.
                (void)coordinator.cancel(ticket);
                return false;
            }
            // The scene provider is consumed by stage() on every path.
            if(!host_.stage(device_,ticket.scene.value,objects,
                            std::move(provider),stats,&reason)) {
                if(error)*error=reason;
                (void)coordinator.cancel(ticket);
                return false;
            }
            staged=true;

            // This is the TRUE authoritative scene commit.
            // It may release SceneResourceAdapter dependencies; the
            // dedicated provider in host_.slots_ is independent and alive.
            if(!coordinator.commit(ticket)) {
                host_.discard(device_,ticket.scene.value);
                const std::string reason=coordinator.error(ticket);
                // SceneRuntime may still contain the rejected Loading
                // scene. Cancel at the same authoritative structural
                // barrier, without touching any successful prior scene.
                // Preserve the original commit failure for diagnostics.
                try {(void)coordinator.cancel(ticket);}catch(...) {}
                if(error)*error=reason.empty()
                    ? "authoritative scene commit rejected":reason;
                return false;
            }
            committed=true;
            if(!host_.commit(device_,ticket.scene.value))
                throw std::runtime_error(
                    "authoritative scene activated but visual commit failed");

            // The only source of truth for Replace/persistent residency
            // is SceneState AFTER the authoritative coordinator commit.
            auto ids=activeIds();
            const std::span<const uint64_t> canonical(ids.data(),ids.size());
            host_.unloadAbsent(device_,canonical);
            return true;
        }catch(const std::exception& e) {
            if(error)*error=e.what();
        }catch(...) {
            if(error)*error="unknown scene coordinator adapter failure";
        }

        // Never "rollback" authoritative state after commit() returned
        // true; the host must diagnose/reconcile instead of destructively
        // unloading a scene that gameplay now owns.
        if(!committed) {
            host_.retireUnstaged(device_,provider);
            if(staged)host_.discard(device_,ticket.scene.value);
        }
        return false;
    }

    // Shared GPU residency mode. The builder gets a READ-ONLY already
    // resident provider (or nullptr on first publication), and returns a
    // freshly prepared candidate Owner ONLY for first publication.
    // bool(const SceneAsset&,const ModelAssetProvider* resident,
    //      Owner& newProvider,std::vector<Object>&,std::string&).
    // A reused provider may be inspected but MUST NOT be invalidated,
    // imported into or modified during another live scene's draw lifetime.
    template<class Coordinator,class Ticket,class Build,class ActiveIds>
    bool commitReadyShared(Coordinator& coordinator,const Ticket& ticket,
                           const std::string& providerKey,
                           Build&& build,ActiveIds&& activeIds,
                           Statistics& stats,std::string* error=nullptr) {
        if(error)error->clear();
        Owner incoming;
        bool staged=false;
        bool committed=false;
        try {
            if(!ticket.valid() || providerKey.empty())
                throw std::invalid_argument(
                    "shared scene ticket or provider key invalid");
            const auto* manifest=coordinator.prepared(ticket);
            if(!manifest) {
                if(error)*error="scene coordinator ticket is not Ready";
                return false;
            }
            const auto* resident=host_.sharedProvider(providerKey);
            std::vector<Object> objects;
            std::string reason;
            bool built=false;
            try {
                built=build(*manifest,resident,incoming,objects,reason);
            }catch(const std::exception& e) {
                reason=e.what();
            }catch(...) {
                reason="unknown shared scene builder exception";
            }
            // New key MUST return a real owner; reuse MUST NOT return
            // another owner under the same key (would leak ownership).
            if(!built || bool(incoming)==bool(resident)) {
                host_.retireUnstaged(device_,incoming);
                if(reason.empty())
                    reason=resident
                        ? "shared builder returned duplicate provider"
                        : "shared builder returned no new provider";
                if(error)*error=reason;
                (void)coordinator.cancel(ticket);
                return false;
            }
            if(!host_.stageShared(
                    device_,ticket.scene.value,providerKey,objects,
                    std::move(incoming),stats,&reason)) {
                if(error)*error=reason;
                (void)coordinator.cancel(ticket);
                return false;
            }
            staged=true;
            if(!coordinator.commit(ticket)) {
                host_.discard(device_,ticket.scene.value);
                const std::string reason=coordinator.error(ticket);
                try {(void)coordinator.cancel(ticket);}catch(...) {}
                if(error)*error=reason.empty()
                    ? "authoritative shared scene commit rejected":reason;
                return false;
            }
            committed=true;
            if(!host_.commit(device_,ticket.scene.value))
                throw std::runtime_error(
                    "authoritative scene activated but shared visual commit failed");
            auto ids=activeIds();
            host_.unloadAbsent(
                device_,std::span<const uint64_t>(ids.data(),ids.size()));
            return true;
        }catch(const std::exception& e) {
            if(error)*error=e.what();
        }catch(...) {
            if(error)*error="unknown shared scene coordinator failure";
        }
        if(!committed) {
            host_.retireUnstaged(device_,incoming);
            if(staged)host_.discard(device_,ticket.scene.value);
        }
        return false;
    }

    // Cancel a ready/preparing ticket on the authoritative owner barrier,
    // and detach only the matching staged visual provider afterward.
    template<class Coordinator,class Ticket>
    bool cancel(Coordinator& coordinator,const Ticket& ticket) {
        if(!ticket.valid())return false;
        bool success=false;
        try {
            success=coordinator.cancel(ticket);
        }catch(...) {
            // The candidate has no authority over gameplay if native
            // cancellation throws. Never leak its GPU Model provider.
            host_.discard(device_,ticket.scene.value);
            throw;
        }
        host_.discard(device_,ticket.scene.value);
        return success;
    }

    // Authoritative unload first (only if SceneRuntime accepts it), then
    // detach that cosmetic scene and GPU-retire its dedicated provider.
    // ResourceAdapter::release cannot destroy this separately held owner.
    template<class Coordinator,class SceneId>
    bool unload(Coordinator& coordinator,SceneId id) {
        if(!id.valid() || !coordinator.unload(id))return false;
        host_.unload(device_,id.value);
        return true;
    }

    // MUST be called AFTER Simulation::loadState() and BEFORE resume.
    // Reset derived presentation sources before coordinator drops its
    // old resource residency and requeues restored authoritative scenes.
    // Returned SceneTickets are host-only residency jobs to prewarm/commit
    // and MUST not create duplicate gameplay entities on restore.
    template<class Coordinator>
    auto restoreResidencyAfterRollback(Coordinator& coordinator) {
        host_.resetAfterRollback(device_);
        coordinator.invalidateAfterRestore();
        return coordinator.queueRestoredResidency();
    }

private:
    gfx::Device& device_;
    Host& host_;
};

} // namespace aiverse::view
