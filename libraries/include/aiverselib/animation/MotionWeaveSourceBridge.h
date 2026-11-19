// MotionWeave source-to-presentation ECS adapter.
// The library deliberately knows NOTHING about genres, input devices,
// replicated actor layouts, weapon mechanics or game-module components.
#pragma once
#include "EcsAnimationSystems.h"
#include <cstdint>
#include <map>
#include <memory>
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
};
struct AnimationBridgeStats {
    size_t created=0, updated=0, restarted=0, removed=0;
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
        for(const auto& input:sources) {
            if(!keys.insert(input.sourceId).second)
                throw std::invalid_argument("MotionWeave bridge: duplicate source ID");
            if(!input.model || !input.graph ||
               input.graph->model()!=input.model)
                throw std::invalid_argument("MotionWeave bridge: invalid graph/model");
        }
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
                        source.sourceId,source.generation});
                auto& asset=presentation.add<AnimationAssetComponent>(entity);
                asset.model=source.model;
                asset.graph=source.graph;
                presentation.add<AnimationInputsComponent>(entity);
                presentation.add<AnimationPlayerComponent>(entity);
                presentation.add<AnimationPoseComponent>(entity);
                presentation.add<AnimationBudgetComponent>(entity);
                presentation.add<AnimationDiagnosticsComponent>(entity);
                ++stats.created;
            } else {
                entity=existing->second;
                auto& link=presentation.get<AnimationSourceLinkComponent>(entity);
                auto& asset=presentation.get<AnimationAssetComponent>(entity);
                if(link.generation!=source.generation ||
                   asset.model!=source.model || asset.graph!=source.graph) {
                    presentation.get<AnimationPlayerComponent>(entity).restart=true;
                    if(auto* overlays=presentation.tryGet<AnimationMontageSlotsComponent>(entity)) {
                        overlays->players.clear();
                        overlays->events.clear();
                    }
                    if(auto* eventStream=presentation.tryGet<AnimationGraphNotifiesComponent>(entity))
                        eventStream->events.clear();
                    ++stats.restarted;
                }
                link.generation=source.generation;
                asset.model=source.model;
                asset.graph=source.graph;
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
    // Transactional resource invalidation: forget only selected source
    // identities before the host destroys their borrowed Model/Graph.
    // This is a presentation-thread structural operation, never a worker job.
    size_t forget(World& presentation,std::span<const uint64_t> sourceIds) {
        size_t removed=0;
        for(const uint64_t id:sourceIds) {
            auto it=links_.find(id);
            if(it==links_.end())continue;
            if(presentation.alive(it->second))presentation.destroy(it->second);
            links_.erase(it);
            ++removed;
        }
        return removed;
    }
    void clear(World& presentation) {
        for(const auto& [id,entity]:links_) {
            (void)id;
            if(presentation.alive(entity))presentation.destroy(entity);
        }
        links_.clear();
    }
private:
    std::map<uint64_t,Entity> links_;
};
} // namespace aiverse::lib::animation
