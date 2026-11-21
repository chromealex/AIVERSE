// MotionWeave source-to-presentation ECS adapter.
// The library deliberately knows NOTHING about genres, input devices,
// replicated actor layouts, weapon mechanics or game-module components.
#pragma once
#include "EcsAnimationSystems.h"
#include <cstdint>
#include <map>
#include <memory>
#include <functional>
#include <iterator>
#include <limits>
#include <string>
#include <vector>
#include <set>
#include <span>
#include <stdexcept>
#include <utility>

namespace aiverse::lib::animation {

// Stable identity for an animation source in any host application.
// generation is host-owned; increment on logical replacement/respawn.
struct AnimationSourceLinkComponent {
    uint64_t sourceId=0;
    uint64_t generation=0;
    // Optional host Model asset-publication identity. Independent from
    // logical entity respawn and AnimationGraph compilation generation.
    // A zero generation preserves compatibility with older native hosts.
    uint64_t modelPublicationGeneration=0;
};

// A snapshot of authored animation inputs, not a gameplay entity object.
// Parameter names such as "speed", "cast", or "open" are chosen in the graph;
// the animation library never interprets their domain-specific meaning.
struct AnimationSourceFrame {
    uint64_t sourceId=0;
    uint64_t generation=0;
    const render::Model* model=nullptr;
    std::shared_ptr<const render::AnimationGraph> graph;
    render::AnimationParameters inputs;
    // Appended after legacy aggregate members to preserve older native
    // source callers. Hosts with hot reload must supply nonzero epochs.
    uint64_t modelPublicationGeneration=0;
};
struct AnimationBridgeStats {
    size_t created=0, updated=0, restarted=0, removed=0;
    size_t assetEpochRestarts=0;
    size_t playbackCaptured=0; // safe named state+clip handoff accepted
    size_t playbackDeclined=0; // identity or rig not compatible
};

// One bridge per presentation World / view. Hosts own an independent
// simulation and project any state into AnimationSourceFrame every frame.
// sync() owns entity lifetime for the sources passed to it, but intentionally
// never calls an animation updater, gameplay callbacks, or a renderer.
// No World structural changes are allowed during MotionWeave worker dispatch.
class MotionWeaveSourceBridge {
public:
    // Reject an invalid/duplicate snapshot before making changes to World.
    // Missing sources are removed only when removeAbsent=true.
    AnimationBridgeStats sync(World& presentation,
                              std::span<const AnimationSourceFrame> sources,
                              bool removeAbsent=true) {
        std::set<uint64_t> keys;
        // Preflight fresh immutable node-name/parent signatures before any
        // presentation World mutation. One allocation per distinct Model
        // publication per sync, not per actor sharing that Model.
        std::map<uint64_t,std::shared_ptr<const RigHierarchy>> incomingRigs;
        std::map<RigKey,std::shared_ptr<const RigHierarchy>,RigKeyLess> distinctRigs;
        for(const auto& input:sources) {
            // Zero is the absence of a stable logical identity in View.
            // Never let unrelated anonymous visual objects share clocks
            // or inherit a prior asset-reimport snapshot.
            if(input.sourceId==0)
                throw std::invalid_argument("MotionWeave bridge: source ID must be nonzero");
            if(!keys.insert(input.sourceId).second)
                throw std::invalid_argument("MotionWeave bridge: duplicate source ID");
            if(!input.model || !input.graph ||
               input.graph->model()!=input.model)
                throw std::invalid_argument("MotionWeave bridge: invalid graph/model");
            // A Model mutated/reimported in place cannot keep using its old
            // compiled graph, even when the C++ Model address is unchanged.
            // Validate before any World structural changes are made.
            const auto it=links_.find(input.sourceId);
            bool changed=it==links_.end() ||
                         !presentation.alive(it->second);
            if(!changed) {
                const auto& previous=presentation.get<
                    AnimationSourceLinkComponent>(it->second);
                const auto& asset=presentation.get<
                    AnimationAssetComponent>(it->second);
                changed=previous.generation!=input.generation ||
                    previous.modelPublicationGeneration!=
                        input.modelPublicationGeneration ||
                    asset.model!=input.model || asset.graph!=input.graph;
                if(input.modelPublicationGeneration!=0 &&
                   previous.modelPublicationGeneration!=0 &&
                   previous.modelPublicationGeneration!=
                       input.modelPublicationGeneration &&
                   asset.model==input.model &&
                   asset.graph==input.graph)
                    throw std::invalid_argument(
                        "MotionWeave bridge: Model asset epoch changed without recompiling AnimationGraph");
            }
            if(changed) {
                const RigKey key{input.model,input.modelPublicationGeneration};
                auto found=distinctRigs.find(key);
                if(found==distinctRigs.end())
                    found=distinctRigs.emplace(
                        key,captureHierarchy(*input.model)).first;
                incomingRigs.emplace(input.sourceId,found->second);
            }
        }
        // A retirement handoff remains valid for at most 120 complete
        // source-sync calls. All records are values; no retired Model or
        // Graph is dereferenced during this expiration sweep.
        if(syncSerial_!=std::numeric_limits<uint64_t>::max())++syncSerial_;
        pruneRetirementSnapshots();
        AnimationBridgeStats stats;
        for(const auto& source:sources) {
            auto existing=links_.find(source.sourceId);
            Entity entity;
            if(existing==links_.end() ||
               !presentation.alive(existing->second)) {
                entity=presentation.create();
                links_[source.sourceId]=entity;
                presentation.add<AnimationSourceLinkComponent>(
                    entity,AnimationSourceLinkComponent{
                        source.sourceId,source.generation,
                        source.modelPublicationGeneration});
                auto& asset=presentation.add<AnimationAssetComponent>(entity);
                asset.model=source.model;
                asset.graph=source.graph;
                presentation.add<AnimationInputsComponent>(entity);
                presentation.add<AnimationPlayerComponent>(entity);
                presentation.add<AnimationPoseComponent>(entity);
                presentation.add<AnimationBudgetComponent>(entity);
                presentation.add<AnimationDiagnosticsComponent>(entity);
                sourceRigs_.insert_or_assign(
                    source.sourceId,incomingRigs.at(source.sourceId));
                // The opt-in pre-retirement snapshot was taken BEFORE the
                // old CPU Model was freed. The recreated source must be the
                // same logical generation but a distinct, explicitly
                // published Model with an identical copied rig hierarchy.
                // Reject the SAME address (including allocator reuse) and
                // a reused epoch conservatively; never read an old pointer.
                if(auto saved=pendingRetirements_.find(source.sourceId);
                   saved!=pendingRetirements_.end()) {
                    const auto& old=saved->second;
                    const auto newRig=incomingRigs.at(source.sourceId);
                    // Two safe, explicit publication transitions:
                    // (1) new Model object + advanced nonzero epoch, or
                    // (2) new compiled Graph for the SAME immutable Model
                    //     within an unchanged nonzero publication epoch.
                    // The host contract forbids in-place Model mutation
                    // without bumping its epoch; an ambiguous transition
                    // always starts playback from authored defaults.
                    const auto newAddress=reinterpret_cast<std::uintptr_t>(source.model);
                    const bool freshModel=old.publication!=source.modelPublicationGeneration &&
                        old.modelAddress!=newAddress;
                    const bool sameModelGraphReload=old.publication==source.modelPublicationGeneration &&
                        old.modelAddress==newAddress &&
                        old.graphAddress!=reinterpret_cast<std::uintptr_t>(source.graph.get());
                    const bool compatible=old.generation==source.generation &&
                        old.publication!=0 &&
                        source.modelPublicationGeneration!=0 &&
                        (freshModel || sameModelGraphReload) &&
                        old.rig && newRig && *old.rig==*newRig;
                    if(compatible) {
                        try {
                            presentation.get<AnimationPlayerComponent>(entity)
                                .pendingPlayback=std::move(saved->second.playback);
                            ++stats.playbackCaptured;
                        }catch(...) {
                            ++stats.playbackDeclined;
                        }
                    } else ++stats.playbackDeclined;
                    pendingRetirements_.erase(saved); // consume even on mismatch
                }
                ++stats.created;
            } else {
                // A non-detached live source must never claim a pending
                // retirement snapshot intended for a recreated entity.
                if(pendingRetirements_.erase(source.sourceId))
                    ++stats.playbackDeclined;
                entity=existing->second;
                auto& link=presentation.get<AnimationSourceLinkComponent>(entity);
                auto& asset=presentation.get<AnimationAssetComponent>(entity);
                const bool assetEpochChanged=
                    link.modelPublicationGeneration!=
                        source.modelPublicationGeneration;
                if(link.generation!=source.generation ||
                   assetEpochChanged || asset.model!=source.model ||
                   asset.graph!=source.graph) {
                    auto& player=presentation.get<AnimationPlayerComponent>(entity);
                    player.pendingPlayback.reset();
                    // Compare COPIED node hierarchies, never dereference a
                    // potentially retired previous Model just to compare
                    // its bones. A same-address in-place GPU reimport may
                    // have invalidated the old graph's clip metadata, so
                    // allow cross-epoch carryover only for distinct Models
                    // with two explicit nonzero publication generations.
                    const auto oldRig=sourceRigs_.find(source.sourceId);
                    const auto newRig=incomingRigs.find(source.sourceId);
                    const bool rigMatches=oldRig!=sourceRigs_.end() &&
                        newRig!=incomingRigs.end() && oldRig->second &&
                        newRig->second && *oldRig->second==*newRig->second;
                    // A graph-only recompile may retain playback on
                    // the SAME immutable Model and epoch. A different
                    // Model requires BOTH distinct nonzero publication
                    // epochs, regardless of whether the host forgot to
                    // increment the publication ID. A same-address in-place
                    // reimport always restarts.
                    const bool modelChanged=asset.model!=source.model;
                    const bool epochSafe=(!modelChanged && !assetEpochChanged) ||
                        (modelChanged && assetEpochChanged &&
                         link.modelPublicationGeneration!=0 &&
                         source.modelPublicationGeneration!=0);
                    const bool eligible=link.generation==source.generation &&
                        rigMatches && epochSafe && asset.model && asset.graph &&
                        !player.restart && player.instance &&
                        player.instance->healthy() &&
                        player.active==asset.graph &&
                        player.instance->graph()==asset.graph.get();
                    if(eligible) {
                        try {
                            // The host must keep the old compiled Model
                            // CPU-readable until this synchronous sync()
                            // returns. This snapshots only state and phases,
                            // not prior native rig/pose/contact/notify data.
                            auto snapshot=player.instance->snapshotPlayback();
                            if(!snapshot.state.empty()) {
                                player.pendingPlayback=std::move(snapshot);
                                ++stats.playbackCaptured;
                            } else ++stats.playbackDeclined;
                        }catch(...) {
                            // Optional cosmetic continuity must not fail
                            // a valid new asset publication.
                            ++stats.playbackDeclined;
                        }
                    } else ++stats.playbackDeclined;
                    player.restart=true;
                    // The previous pose was evaluated for the OLD asset.
                    // Invalidate it synchronously: even if the next animation
                    // update is skipped (invalid dt, LOD budget or suspend),
                    // no renderer can submit stale bone matrices.
                    if(auto* pose=presentation.tryGet<AnimationPoseComponent>(entity))
                        pose->valid=false;
                    if(auto* overlays=presentation.tryGet<AnimationMontageSlotsComponent>(entity)) {
                        overlays->players.clear();
                        overlays->events.clear();
                    }
                    if(auto* eventStream=presentation.tryGet<AnimationGraphNotifiesComponent>(entity))
                        eventStream->events.clear();
                    ++stats.restarted;
                    if(assetEpochChanged)++stats.assetEpochRestarts;
                }
                link.generation=source.generation;
                link.modelPublicationGeneration=
                    source.modelPublicationGeneration;
                asset.model=source.model;
                asset.graph=source.graph;
                if(auto rig=incomingRigs.find(source.sourceId);
                   rig!=incomingRigs.end())
                    sourceRigs_.insert_or_assign(source.sourceId,rig->second);
                ++stats.updated;
            }
            presentation.get<AnimationInputsComponent>(entity).overrides=
                source.inputs;
        }
        if(removeAbsent) {
            for(auto it=links_.begin();it!=links_.end();) {
                if(keys.count(it->first)) {++it;continue;}
                if(presentation.alive(it->second))
                    presentation.destroy(it->second);
                sourceRigs_.erase(it->first);
                it=links_.erase(it);
                ++stats.removed;
            }
        }
        return stats;
    }
    Entity find(uint64_t sourceId) const {
        const auto it=links_.find(sourceId);
        return it==links_.end()?kNullEntity:it->second;
    }
    size_t size() const noexcept {return links_.size();}
    // Default invalidation erases every old state and pending handoff.
    // Use this for despawns, removals, and Model detach without continuity.
    // Call on the presentation owner, never during worker dispatch.
    size_t forget(World& presentation,std::span<const uint64_t> sourceIds) {
        return detach(presentation,sourceIds,false);
    }
    // Opt-in hot-reimport handshake: call while the OLD CPU Model/Graph
    // and its player are still readable, BEFORE retiring those assets.
    // Captures only named playback, then destroys the old ECS entity.
    // A later sync may consume it once for an identical logical generation
    // and rig after either a fresh new-epoch Model publication or a
    // same-epoch Graph reload of the original immutable Model.
    // Unmatched snapshots expire within 120 sync calls, capped at 8192.
    size_t forgetForReimport(World& presentation,
                            std::span<const uint64_t> sourceIds) {
        return detach(presentation,sourceIds,true);
    }
    void clear(World& presentation) {
        for(const auto& [id,entity]:links_) {
            (void)id;
            if(presentation.alive(entity))presentation.destroy(entity);
        }
        links_.clear();
        sourceRigs_.clear();
        pendingRetirements_.clear();
        syncSerial_=0;
    }
private:
    // Immutable source rig identity, shared by every actor using the same
    // Model within one sync. Exact name and parent comparisons (no hash
    // collisions); contains no borrowed Model, Bone or native Device ptr.
    using RigHierarchy=std::vector<std::pair<std::string,int>>;
    struct RigKey {
        const render::Model* model=nullptr;
        uint64_t publication=0;
    };
    struct RigKeyLess {
        bool operator()(const RigKey& a,const RigKey& b) const noexcept {
            const auto less=std::less<const render::Model*>{};
            if(less(a.model,b.model))return true;
            if(less(b.model,a.model))return false;
            return a.publication<b.publication;
        }
    };
    static std::shared_ptr<const RigHierarchy> captureHierarchy(
            const render::Model& model) {
        auto nodes=std::make_shared<RigHierarchy>();
        nodes->reserve(model.nodes.size());
        for(const auto& node:model.nodes)
            nodes->emplace_back(node.name,node.parent);
        return nodes;
    }
    // No owning/borrowed Model or Graph pointers survive pre-retirement.
    // The old address is an integer-only conservative identity guard:
    // never dereferenced, and an allocator-reused address rejects handoff.
    struct RetiredPlayback {
        uint64_t generation=0;
        uint64_t publication=0;
        std::uintptr_t modelAddress=0;
        std::uintptr_t graphAddress=0; // equality guard only; never dereference
        std::shared_ptr<const RigHierarchy> rig;
        render::AnimationGraphInstance::PlaybackSnapshot playback;
        uint64_t syncSerial=0;
    };
    static constexpr size_t kMaxRetiredPlayback=8192;
    static constexpr uint64_t kRetiredPlaybackSyncLifetime=120;
    void pruneRetirementSnapshots() {
        for(auto it=pendingRetirements_.begin();
            it!=pendingRetirements_.end();) {
            if(syncSerial_-it->second.syncSerial>kRetiredPlaybackSyncLifetime)
                it=pendingRetirements_.erase(it);
            else ++it;
        }
    }
    void saveRetirementSnapshot(const World& presentation,
                                uint64_t id,Entity entity) {
        try {
            if(!presentation.alive(entity))return;
            const auto* link=presentation.tryGetConst<AnimationSourceLinkComponent>(entity);
            const auto* asset=presentation.tryGetConst<AnimationAssetComponent>(entity);
            const auto* player=presentation.tryGetConst<AnimationPlayerComponent>(entity);
            const auto rig=sourceRigs_.find(id);
            if(!link || !asset || !player ||
               link->sourceId!=id || link->modelPublicationGeneration==0 ||
               !asset->model || !asset->graph ||
               asset->graph->model()!=asset->model ||
               rig==sourceRigs_.end() || !rig->second ||
               player->restart || !player->instance ||
               player->active!=asset->graph ||
               player->instance->graph()!=asset->graph.get() ||
               !player->instance->healthy())return;
            auto snapshot=player->instance->snapshotPlayback();
            if(snapshot.state.empty())return;
            if(pendingRetirements_.size()>=kMaxRetiredPlayback) {
                // Reimports are rare, and this only executes at detach;
                // evict the oldest record instead of growing without bound.
                auto oldest=pendingRetirements_.begin();
                for(auto it=std::next(oldest);it!=pendingRetirements_.end();++it)
                    if(it->second.syncSerial<oldest->second.syncSerial)
                        oldest=it;
                pendingRetirements_.erase(oldest);
            }
            RetiredPlayback retired;
            retired.generation=link->generation;
            retired.publication=link->modelPublicationGeneration;
            retired.modelAddress=reinterpret_cast<std::uintptr_t>(asset->model);
            retired.graphAddress=reinterpret_cast<std::uintptr_t>(asset->graph.get());
            retired.rig=rig->second;
            retired.playback=std::move(snapshot);
            retired.syncSerial=syncSerial_;
            pendingRetirements_.insert_or_assign(id,std::move(retired));
        }catch(...) {
            // Optional cosmetic phase retention must not block Model detach.
            pendingRetirements_.erase(id);
        }
    }
    size_t detach(World& presentation,std::span<const uint64_t> sourceIds,
                  bool preservePortablePlayback) {
        if(preservePortablePlayback)pruneRetirementSnapshots();
        size_t removed=0;
        for(const uint64_t id:sourceIds) {
            auto it=links_.find(id);
            if(it==links_.end()) {
                // Repeated IDs in a pre-retirement batch must not erase
                // the snapshot just captured by the first occurrence.
                // Plain forget remains a cancellation even without a
                // live ECS source.
                if(!preservePortablePlayback)pendingRetirements_.erase(id);
                continue;
            }
            pendingRetirements_.erase(id);
            if(preservePortablePlayback)
                saveRetirementSnapshot(presentation,id,it->second);
            if(presentation.alive(it->second))presentation.destroy(it->second);
            sourceRigs_.erase(id);
            links_.erase(it);
            ++removed;
        }
        return removed;
    }
    std::map<uint64_t,Entity> links_;
    std::map<uint64_t,std::shared_ptr<const RigHierarchy>> sourceRigs_;
    std::map<uint64_t,RetiredPlayback> pendingRetirements_;
    uint64_t syncSerial_=0;
};
} // namespace aiverse::lib::animation
