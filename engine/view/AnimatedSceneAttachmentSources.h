// AIVERSE — genre-neutral independent animated attachment sources.
//
// Every animated child is its OWN MotionWeave graph source. No child's graph
// is attached to the parent View::Object or evaluated a second time by View.
// The host owns Model/GPU/Graph publications and the visual parent pose.
#pragma once
#include "SceneAttachmentHost.h"
#include <aiverselib/animation/MotionWeaveSourceBridge.h>
#include <aiverselib/animation/EcsAnimationSystems.h>
#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::view {

class AnimatedSceneAttachmentSources final {
public:
    using AssetSnapshot=AppearanceAssetRegistry::Snapshot;
    struct Key {
        uint64_t scene=0;
        std::string object;
        std::string child;
    };
    struct KeyLess {
        bool operator()(const Key& a,const Key& b) const noexcept {
            if(a.scene!=b.scene)return a.scene<b.scene;
            if(a.object!=b.object)return a.object<b.object;
            return a.child<b.child;
        }
    };
    struct Input {
        Key key;
        AssetSnapshot appearance;
        render::AnimationParameters parameters;
    };
    struct Statistics {
        size_t sources=0;
        lib::animation::AnimationBridgeStats bridge;
        size_t playbackSeeded=0;
        size_t playbackDeclined=0;
        size_t playbackRestored=0;
        size_t playbackRestoreRejected=0;
    };

    // Portable, pointer-free playback only. Parent Model/Graph owners are
    // NEVER included, nor are notifies, physics state or evaluated poses.
    struct PlaybackHandoff {
        render::AnimationGraphInstance::PlaybackSnapshot playback;
        std::vector<std::pair<std::string,int>> rig;
    };

    // Actual worker result from the most recent bridge sync.
    struct RestoreResult {
        uint64_t attempts=0,restored=0,rejected=0;
    };
    RestoreResult restoreResult(const Key& key) const {
        const auto it=active_.find(key);
        if(it==active_.end())return {};
        const Entity entity=bridge_.find(it->second.sourceId);
        const auto* player=animationWorld_.tryGetConst<
            lib::animation::AnimationPlayerComponent>(entity);
        return player
            ?RestoreResult{player->playbackRestoreAttempts,
                           player->playbackRestoreAccepted,
                           player->playbackRestoreDeclined}
            :RestoreResult{};
    }

    std::optional<PlaybackHandoff> capturePlayback(const Key& key) const {
        const auto it=active_.find(key);
        if(it==active_.end() || !it->second.childAppearance ||
           !it->second.childAppearance->model)return std::nullopt;
        const Entity source=bridge_.find(it->second.sourceId);
        const auto* player=animationWorld_.tryGetConst<
            lib::animation::AnimationPlayerComponent>(source);
        const auto* link=animationWorld_.tryGetConst<
            lib::animation::AnimationSourceLinkComponent>(source);
        const auto& asset=it->second.childAppearance;
        if(!player || !link || link->generation!=it->second.generation ||
           link->modelPublicationGeneration!=asset->modelPublicationGeneration ||
           player->restart || !player->instance ||
           !player->instance->healthy() || player->active!=asset->graph ||
           player->instance->graph()!=asset->graph.get())
            return std::nullopt;
        auto playback=player->instance->snapshotPlayback();
        if(playback.state.empty())return std::nullopt;
        PlaybackHandoff saved;
        saved.playback=std::move(playback);
        saved.rig.reserve(asset->model->nodes.size());
        for(const auto& node:asset->model->nodes)
            saved.rig.emplace_back(node.name,node.parent);
        return saved;
    }

    // Queue a handoff on a BRAND NEW generation before its first sync.
    // The caller validates stable scene/object/child authored identity.
    // A mismatched rig declines without touching the existing graph state.
    bool queuePlayback(const Key& key,const PlaybackHandoff& snapshot,
                       const render::Model& incoming) {
        if(key.scene==0 || key.object.empty() || key.child.empty() ||
           snapshot.playback.state.empty() ||
           snapshot.rig.size()!=incoming.nodes.size() ||
           active_.find(key)!=active_.end())return false;
        for(size_t i=0;i<incoming.nodes.size();++i)
            if(snapshot.rig[i].first!=incoming.nodes[i].name ||
               snapshot.rig[i].second!=incoming.nodes[i].parent)
                return false;
        return pendingPlayback_.emplace(key,snapshot.playback).second;
    }

    // Move-only, frame-scoped CPU model leases. The borrowed local pose and
    // matrices reside inside animationWorld_ and remain valid ONLY until
    // the NEXT sync/step or structural mutation of this source host.
    struct Prepared {
        render::ModelReadLease parentRead;
        render::ModelReadLease childRead;
        AssetSnapshot parentAppearance;
        AssetSnapshot childAppearance;
        Object object;
        Prepared()=default;
        Prepared(const Prepared&)=delete;
        Prepared& operator=(const Prepared&)=delete;
        Prepared(Prepared&&)=default;
        Prepared& operator=(Prepared&&)=default;
    };

    AnimatedSceneAttachmentSources()=default;
    AnimatedSceneAttachmentSources(const AnimatedSceneAttachmentSources&)=delete;
    AnimatedSceneAttachmentSources& operator=(const AnimatedSceneAttachmentSources&)=delete;

    // Every call is the FULL set of active animated children for this host.
    // It must occur exactly once at the presentation-owner update boundary,
    // after parent scene publication and BEFORE View/Renderer3D submission.
    // Passing an empty span retires previous child graph players.
    // The native project must provide an immutable already-compiled
    // published child Graph; this class never imports or invents a Graph.
    bool sync(const SceneAttachmentHost& scenes,
              const render::ModelResourceCache& cache,
              std::span<const Input> inputs,
              float dt,
              Statistics& stats,
              std::string* error=nullptr) {
        if(error)error->clear();
        try {
            if(!std::isfinite(dt) || dt<0.f)
                throw std::invalid_argument(
                    "animated attachment dt must be finite and nonnegative");
            std::map<Key,Entry,KeyLess> next;
            std::vector<lib::animation::AnimationSourceFrame> frames;
            std::vector<render::ModelReadLease> reads;
            frames.reserve(inputs.size());
            reads.reserve(inputs.size()*2u);
            for(const auto& input:inputs) {
                const Key& key=input.key;
                if(key.scene==0 || key.object.empty() || key.child.empty())
                    throw std::invalid_argument(
                        "animated child needs scene/object/stable child ID");
                if(next.find(key)!=next.end())
                    throw std::invalid_argument(
                        "duplicate animated child graph source identity");

                const auto* scene=scenes.find(key.scene,key.object);
                const Entity childEntity=scenes.resolveChild(
                    key.scene,key.object,key.child);
                if(!scene || !childEntity.valid() || !scene->bindings.source)
                    throw std::invalid_argument(
                        "animated child has no live presentation scene entity");
                const AttachmentLayoutItem* authored=nullptr;
                for(const auto& item:scene->document.attachments)
                    if(item.id==key.child) {
                        authored=&item; break;
                    }
                if(!authored || !authored->enabled || !authored->animated)
                    throw std::invalid_argument(
                        "child is not authored as an enabled independent graph");
                const auto& parent=scene->bindings.source;
                const auto& child=input.appearance;
                if(!parent || !parent->model ||
                   !parent->modelPublicationGeneration ||
                   !child || !child->model ||
                   !child->modelPublicationGeneration ||
                   !child->graph || child->graph->model()!=child->model)
                    throw std::invalid_argument(
                        "child needs published Model and matching compiled Graph");

                auto parentRead=cache.acquireModelRead(parent->model);
                auto childRead=cache.acquireModelRead(child->model);
                if(!parentRead || !childRead)
                    throw std::invalid_argument(
                        "animated child or parent Model is not cache resident");
                auto existing=active_.find(key);
                Entry record;
                if(existing!=active_.end())record=existing->second;
                else {
                    if(nextSourceId_==0 ||
                       nextSourceId_==std::numeric_limits<uint64_t>::max())
                        throw std::overflow_error(
                            "animated child source identity allocator exhausted");
                    record.sourceId=nextSourceId_++;
                }
                record.generation=childEntity.packed();
                record.childEntity=childEntity;
                record.childAppearance=child;
                record.parentAppearance=parent;
                record.authored=*authored;
                if(!record.socket ||
                   !record.socket->matches(
                       *parentRead.get(),
                       parent->modelPublicationGeneration,
                       authored->socket,-1)) {
                    record.socket=compileModelSocket(
                        *parentRead.get(),parent->modelPublicationGeneration,
                        authored->socket,-1);
                    if(!record.socket)
                        throw std::invalid_argument(
                            "animated child named parent socket not found");
                }
                frames.push_back({
                    record.sourceId,record.generation,
                    child->model,child->graph,input.parameters,
                    child->modelPublicationGeneration
                });
                next.emplace(key,std::move(record));
                reads.push_back(std::move(parentRead));
                reads.push_back(std::move(childRead));
            }

            // The bridge owns all and ONLY this animated-child set, never
            // the parent/static source player. One graph evaluator for
            // each active ID in animationWorld_ and no View::update later.
            const auto bridgeStats=bridge_.sync(
                animationWorld_,frames,true);
            size_t seeded=0,declined=0;
            for(const auto& [key,playback]:pendingPlayback_) {
                const auto entry=next.find(key);
                if(entry==next.end()) {++declined;continue;}
                const Entity entity=bridge_.find(entry->second.sourceId);
                auto* player=animationWorld_.tryGet<
                    lib::animation::AnimationPlayerComponent>(entity);
                if(!player || player->instance || player->pendingPlayback) {
                    ++declined;
                    continue;
                }
                player->pendingPlayback=playback;
                ++seeded;
            }
            // A seed is one-shot even if the Graph declines a named
            // state/clip in restorePlayback. Never replay old notifies.
            pendingPlayback_.clear();
            lib::animation::stepAnimationPresentation(animationWorld_,dt);
            active_.swap(next);
            size_t restored=0,rejected=0;
            for(const auto& [key,record]:active_) {
                (void)key;
                const auto* player=animationWorld_.tryGetConst<
                    lib::animation::AnimationPlayerComponent>(
                        bridge_.find(record.sourceId));
                if(!player)continue;
                restored+=size_t(player->playbackRestoreAccepted);
                rejected+=size_t(player->playbackRestoreDeclined);
            }
            stats={active_.size(),bridgeStats,seeded,declined,
                   restored,rejected};
            return true;
        }catch(const std::exception& ex) {
            // Preflight errors leave active_ intact. If the underlying ECS
            // graph evaluation threw after a bridge sync, its World may have
            // changed; next owner sync should reconcile before drawing.
            if(error)*error=ex.what();
            return false;
        }
    }

    // Compose a child object's WORLD matrix from its parent source rig,
    // evaluated parent bone matrices, and the authored local/child transforms.
    // The child pose is read from this host's ONE presentation World player;
    // object.graph is cleared before returning so generic View never
    // schedules a second evaluation. The parent pose belongs to the host.
    bool prepare(const SceneAttachmentHost& scenes,
                 const render::ModelResourceCache& cache,
                 const Key& key,
                 const render::ModelDraw& parentDraw,
                 std::span<const render::mat4> parentNodeMatrices,
                 Prepared& out,std::string* error=nullptr) const {
        if(error)error->clear();
        try {
            const auto it=active_.find(key);
            if(it==active_.end())
                throw std::invalid_argument(
                    "animated child source is not active");
            const Entry& entry=it->second;
            const auto* scene=scenes.find(key.scene,key.object);
            if(!scene || !scene->bindings.source ||
               scene->bindings.source.get()!=
                   entry.parentAppearance.get() ||
               scenes.resolveChild(
                   key.scene,key.object,key.child)!=entry.childEntity ||
               !entry.authored.enabled || !entry.authored.animated)
                throw std::invalid_argument(
                    "animated child scene revision or identity is stale");
            const auto& parent=entry.parentAppearance;
            const auto& child=entry.childAppearance;
            if(!parent || !parent->model || !child || !child->model ||
               !entry.socket ||
               entry.socket->rig!=parent->model ||
               entry.socket->publication!=parent->modelPublicationGeneration)
                throw std::invalid_argument(
                    "animated child published parent or socket epoch changed");

            Prepared next;
            next.parentRead=cache.acquireModelRead(parent->model);
            next.childRead=cache.acquireModelRead(child->model);
            if(!next.parentRead || !next.childRead)
                throw std::invalid_argument(
                    "animated child Model cache lease unavailable");
            next.parentAppearance=parent;
            next.childAppearance=child;
            const Entity e=bridge_.find(entry.sourceId);
            const auto* link=animationWorld_.tryGetConst<
                lib::animation::AnimationSourceLinkComponent>(e);
            const auto* pose=animationWorld_.tryGetConst<
                lib::animation::AnimationPoseComponent>(e);
            if(!link || !pose || !pose->valid ||
               link->generation!=entry.generation ||
               link->modelPublicationGeneration!=
                   child->modelPublicationGeneration ||
               pose->local.size()!=child->model->nodes.size() ||
               pose->matrices.size()!=child->model->nodes.size())
                throw std::invalid_argument(
                    "animated child has no current evaluated Graph pose");
            Object draw;
            if(!child->packet(entry.authored.childWorld,draw))
                throw std::invalid_argument(
                    "animated child cannot form an appearance packet");
            render::ModelDraw composed;
            if(!composeAttachment(
                parentDraw,*next.parentRead.get(),parentNodeMatrices,
                draw.draw,entry.socket->resolvedSocket,
                entry.authored.local,composed))
                throw std::invalid_argument(
                    "animated child parent socket pose unavailable");
            draw.draw=std::move(composed);
            draw.id=entry.sourceId;
            draw.generation=entry.generation;
            draw.animationModel=child->model;
            draw.animationModelGeneration=
                child->modelPublicationGeneration;
            draw.draw.pose=&pose->local;
            draw.draw.nodeWorld=&pose->matrices;
            draw.graph.reset(); // child Graph was evaluated ONCE above
            next.object=std::move(draw);
            out=std::move(next);
            return true;
        }catch(const std::exception& ex) {
            if(error)*error=ex.what();
            return false;
        }
    }

    // Cancel all independent child players before parent scene unload or
    // CPU Model/Graph retirement. The caller joins worker jobs beforehand.
    void clear() {
        bridge_.clear(animationWorld_);
        active_.clear();
        pendingPlayback_.clear();
    }
    size_t size() const noexcept{return active_.size();}
    uint64_t sourceId(const Key& key) const {
        const auto it=active_.find(key);
        return it==active_.end()?0:it->second.sourceId;
    }

private:
    struct Entry {
        uint64_t sourceId=0;
        uint64_t generation=0;
        Entity childEntity{};
        AssetSnapshot parentAppearance;
        AssetSnapshot childAppearance;
        AttachmentLayoutItem authored;
        std::shared_ptr<const CompiledModelSocket> socket;
    };
    std::map<Key,Entry,KeyLess> active_;
    std::map<Key,render::AnimationGraphInstance::PlaybackSnapshot,KeyLess>
        pendingPlayback_;
    uint64_t nextSourceId_=1; // monotonic; NEVER reuses a retired source ID
    World animationWorld_;
    lib::animation::MotionWeaveSourceBridge bridge_;
};

} // namespace aiverse::view
