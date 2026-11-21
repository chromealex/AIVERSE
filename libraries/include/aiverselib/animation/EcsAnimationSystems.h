// Universal presentation ECS animation runtime. No simulation/checksum writes.
#pragma once
#include "engine/core/ECS.h"
#include "engine/render/AnimationGraph.h"
#include "AnimationMontage.h"
#include "PhysicalPoseBlend.h"
#include "PoseInertialization.h"
#include "FacialControlRig.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>
#include <utility>
namespace aiverse::lib::animation {
// All components belong in an independent presentation World.
// The model must outlive its graph and all instances.
struct AnimationAssetComponent {
    const render::Model* model = nullptr;
    std::shared_ptr<const render::AnimationGraph> graph;
    Entity gameEntity = kNullEntity;
};
struct AnimationInputsComponent { render::AnimationParameters overrides; };
struct AnimationPlayerComponent {
    std::shared_ptr<const render::AnimationGraph> active;
    std::shared_ptr<render::AnimationGraphInstance> instance;
    bool restart = false;
    // One-shot, presentation-only clip/state transfer on compatible
    // Model/AnimationGraph asset replacement. Never persisted in sim ECS.
    std::optional<render::AnimationGraphInstance::PlaybackSnapshot>
        pendingPlayback;
    // Telemetry for the most recent attempted named playback restore;
    // queued is not synonymous with successfully restored.
    uint64_t playbackRestoreAttempts=0;
    uint64_t playbackRestoreAccepted=0;
    uint64_t playbackRestoreDeclined=0;
};
struct AnimationPoseComponent {
    render::Pose local;
    std::vector<render::mat4> matrices;
    bool valid = false;
};
// Optional masked overlay slots shared by every kind of animated entity.
// Per-frame notifies from AnimationGraph Clip nodes; cosmetic only.
struct AnimationGraphNotifiesComponent {
    std::vector<render::AnimationGraphNotify> events;
};
struct AnimationMontageSlotsComponent {
    std::vector<MontagePlayer> players;
    std::vector<AnimationNotifyEvent> events; // last frame, for audio/VFX only
};
inline bool playAnimationSlot(AnimationMontageSlotsComponent& slots,
        std::shared_ptr<const MontageDefinition> asset) {
    if(!asset)return false;
    for(auto& player:slots.players)
        if(player.slot()==asset->slot)return player.play(std::move(asset));
    if(slots.players.size()>=8)return false;
    slots.players.emplace_back();
    if(!slots.players.back().play(std::move(asset))) {
        slots.players.pop_back();return false;
    }
    return true;
}
// Host-supplied LOCAL bone pose from an independent physics simulation.
struct AnimationPhysicalPoseComponent {
    render::Pose local;
    std::vector<float> boneWeights;
    float weight = 1.f;
    bool enabled = false;
};
// When ragdoll/physical pose finishes, supply the last physical pose in
// from, disable AnimationPhysicalPoseComponent and set requested=true.
struct AnimationRecoveryComponent {
    render::Pose from;
    PoseInertializer inertializer;
    float duration=0.3f;
    bool requested=false;
};
// Per-source cosmetic facial layer. Each entity has an independent smoothed
// control/IK state; no second Animator, simulated physics or network authority.
// Set controls in this component even when the parent AnimationGraph has no
// face-specific parameter declarations. Do not snapshot this presentation World.
struct AnimationFacialControlComponent {
    std::shared_ptr<const FacialControlRig> rig;
    render::AnimationParameters controls;
    FacialControlState state;
    float weight=1.f;
    bool enabled=true;
};
// One-off normalized clip-phase seed after switching CPU/GPU backends.
// Only applied on first GraphInstance initialization or an explicit request.
struct AnimationPhaseSeedComponent {
    float normalized=-1.f; // -1 means not requested
};
struct AnimationBudgetComponent {
    float interval = 0.f, elapsed = 0.f;
    bool visible = true;
};
struct AnimationDiagnosticsComponent {
    std::string state, error;
    bool healthy = true;
    uint64_t updates = 0, evaluations = 0;
};

// A reusable, source-neutral graph-only hot-reload transaction. A host
// supplies its immutable Model and newly compiled Graph; no gameplay state
// or host-specific meaning enters the presentation ECS.
// This must run on the presentation owner, after worker join, and while
// the previous compiled Graph and CPU Model remain readable.
struct AnimationGraphRebindResult {
    bool replaced=false;
    bool playbackCaptured=false; // not a guarantee of named restore success
};
inline AnimationGraphRebindResult rebindAnimationPresentationGraph(
        World& presentation, Entity source, const render::Model& model,
        std::shared_ptr<const render::AnimationGraph> next) {
    if(!presentation.alive(source) || !next || next->model()!=&model)
        return {};
    auto* asset=presentation.tryGet<AnimationAssetComponent>(source);
    if(!asset || asset->model!=&model)return {};
    if(asset->graph==next)return {};
    auto* player=presentation.tryGet<AnimationPlayerComponent>(source);
    if(!player)return {};
    // Drop any pending older publication seed before trying to retain the
    // current healthy graph's named state+clip phases. The old Graph remains
    // the active writer until the next normal MotionWeave update.
    player->pendingPlayback.reset();
    bool captured=false;
    if(asset->graph && !player->restart && player->instance &&
       player->instance->healthy() &&
       player->active==asset->graph &&
       player->instance->graph()==asset->graph.get()) {
        try {
            auto snapshot=player->instance->snapshotPlayback();
            if(!snapshot.state.empty()) {
                player->pendingPlayback=std::move(snapshot);
                captured=true;
            }
        }catch(...) {
            // Optional presentation continuity must never block a valid
            // compiled-graph publication.
            player->pendingPlayback.reset();
        }
    }
    asset->graph=std::move(next);
    player->restart=true;
    if(auto* pose=presentation.tryGet<AnimationPoseComponent>(source))
        pose->valid=false;
    if(auto* overlays=presentation.tryGet<AnimationMontageSlotsComponent>(source)) {
        overlays->players.clear();
        overlays->events.clear();
    }
    if(auto* stream=presentation.tryGet<AnimationGraphNotifiesComponent>(source))
        stream->events.clear();
    // Physical pose recovery and clip evaluation state are reset by the
    // existing MotionWeave worker on its next player initialization.
    return {true,captured};
}
// Use once per presentation frame after reading the latest gameplay snapshot.
// Graph state updates every frame; pose work may be throttled independently.
// All writer pools are detached/uniquified BEFORE dispatching any worker.
// No component insertion/removal or World snapshotting until the join barrier.
struct AnimationPreparedPools {
    Pool<AnimationPlayerComponent>* players=nullptr;
    Pool<AnimationPoseComponent>* poses=nullptr;
    Pool<AnimationBudgetComponent>* budgets=nullptr;
    Pool<AnimationDiagnosticsComponent>* statuses=nullptr;
    Pool<AnimationMontageSlotsComponent>* montages=nullptr;
    Pool<AnimationGraphNotifiesComponent>* graphNotifies=nullptr;
    Pool<AnimationRecoveryComponent>* recoveries=nullptr;
    Pool<AnimationPhaseSeedComponent>* phaseSeeds=nullptr;
    Pool<AnimationFacialControlComponent>* facialControls=nullptr;
};
inline AnimationPreparedPools prepareAnimationPresentation(World& world) {
    AnimationPreparedPools p;
    p.players=world.prepareExistingWrite<AnimationPlayerComponent>();
    p.poses=world.prepareExistingWrite<AnimationPoseComponent>();
    p.budgets=world.prepareExistingWrite<AnimationBudgetComponent>();
    p.statuses=world.prepareExistingWrite<AnimationDiagnosticsComponent>();
    p.montages=world.prepareExistingWrite<AnimationMontageSlotsComponent>();
    p.graphNotifies=world.prepareExistingWrite<AnimationGraphNotifiesComponent>();
    p.recoveries=world.prepareExistingWrite<AnimationRecoveryComponent>();
    p.phaseSeeds=world.prepareExistingWrite<AnimationPhaseSeedComponent>();
    p.facialControls=world.prepareExistingWrite<AnimationFacialControlComponent>();
    return p;
}
inline void evaluateAnimationPresentationRange(World& world,
        const AnimationPreparedPools& p,float dt,size_t begin,size_t end,
        const std::vector<size_t>* selected=nullptr) {
    if(!p.players || !p.poses || !std::isfinite(dt) || dt<0.f)return;
    auto* players=p.players;
    auto* poses=p.poses;
    auto* budgets=p.budgets;
    auto* statuses=p.statuses;
    auto* montages=p.montages;
    auto* graphNotifies=p.graphNotifies;
    auto* recoveries=p.recoveries;
    auto* phaseSeeds=p.phaseSeeds;
    auto* faceControls=p.facialControls;
    const World& read=world;
    const size_t available=selected?selected->size():players->size();
    for(size_t k=begin;k<std::min(end,available);++k) {
        const size_t i=selected?(*selected)[k]:k;
        if(i>=players->size())continue;
        const Entity e=players->entityAt(i);
        if (!world.alive(e)) continue;
        auto* pose=poses->tryGet(e.index);
        if (!pose) continue;
        auto& player=players->dataAt(i);
        auto* budget=budgets?budgets->tryGet(e.index):nullptr;
        auto* status=statuses?statuses->tryGet(e.index):nullptr;
        auto* slots=montages?montages->tryGet(e.index):nullptr;
        const auto* physical=read.tryGetConst<AnimationPhysicalPoseComponent>(e);
        auto* recovery=recoveries?recoveries->tryGet(e.index):nullptr;
        auto* phaseSeed=phaseSeeds?phaseSeeds->tryGet(e.index):nullptr;
        auto* face=faceControls?faceControls->tryGet(e.index):nullptr;
        std::string faceError;
        const auto* asset=read.tryGetConst<AnimationAssetComponent>(e);
        if (!asset || !asset->model || !asset->graph ||
            asset->graph->model()!=asset->model) {
            player.active.reset(); player.instance.reset();
            player.pendingPlayback.reset();
            if(face)face->state.reset();
            pose->local.clear(); pose->matrices.clear(); pose->valid=false;
            if (status) {
                status->healthy=false;
                status->state.clear();
                status->error="missing or mismatched animation asset";
            }
            continue;
        }
        if (player.active!=asset->graph || !player.instance || player.restart) {
            player.active=asset->graph;
            player.instance=std::make_shared<render::AnimationGraphInstance>();
            player.instance->reset(asset->graph);
            if(player.pendingPlayback) {
                try {
                    // One-shot portable state/clip clocks only. A missing
                    // renamed state/clip or allocation failure simply
                    // leaves the freshly reset graph at authored defaults.
                    // Never replay old notifies, contacts or root motion.
                    ++player.playbackRestoreAttempts;
                    if(player.instance->restorePlayback(*player.pendingPlayback))
                        ++player.playbackRestoreAccepted;
                    else ++player.playbackRestoreDeclined;
                }catch(...) {
                    // CPU-only cosmetic handoff is optional. The new graph's
                    // rig/pose must remain valid even after a failed restore.
                    ++player.playbackRestoreDeclined;
                }
                player.pendingPlayback.reset();
            }
            player.restart=false;
            pose->valid=false;
            if(recovery)recovery->inertializer.reset();
            if(face)face->state.reset();
            if (budget) budget->elapsed=0;
        }
        auto& inst=*player.instance;
        if(phaseSeed && std::isfinite(phaseSeed->normalized) &&
           phaseSeed->normalized>=0.f) {
            const float phase=std::clamp(phaseSeed->normalized,0.f,.999999f);
            for(const auto& node:asset->graph->nodes())
                if(node.kind==render::AnimationGraph::Kind::Clip &&
                   node.clip>=0 && size_t(node.clip)<asset->model->clips.size())
                    inst.seekClipNode(node.id,
                        phase*asset->model->clips[size_t(node.clip)].duration);
            phaseSeed->normalized=-1.f;
        }
        inst.parameters()=asset->graph->defaults();
        bool validInputs=true;
        if (const auto* input=read.tryGetConst<AnimationInputsComponent>(e)) {
            for (const auto& [name,v]:input->overrides.values)
                validInputs=inst.parameters().set(name,v)&&validInputs;
            for (const auto& [name,v]:input->overrides.vectors)
                validInputs=inst.parameters().set(name,v)&&validInputs;
            inst.parameters().environment=input->overrides.environment;
        }
        inst.update(dt);
        if(auto* events=graphNotifies?graphNotifies->tryGet(e.index):nullptr)
            events->events=inst.notifies();
        if (slots) {
            slots->events.clear();
            for (auto& montage:slots->players) {
                montage.update(dt);
                const auto& notifications=montage.events();
                slots->events.insert(slots->events.end(),notifications.begin(),notifications.end());
            }
        }
        if (status) ++status->updates;
        bool evaluate=!pose->valid || !budget ||
            (physical && physical->enabled && physical->weight>0.f) ||
            (recovery && (recovery->requested || recovery->inertializer.active()));
        if (!evaluate && budget->visible) {
            if (!std::isfinite(budget->interval) || budget->interval<=0.f)
                evaluate=true;
            else {
                budget->elapsed+=dt;
                evaluate=budget->elapsed>=budget->interval;
            }
        }
        if (evaluate) {
            // Save the previous evaluated pose only on a requested recovery.
            render::Pose previous;
            if(recovery && recovery->requested)previous=pose->local;
            inst.evaluate(pose->local);
            pose->valid=inst.healthy();
            if (pose->valid) {
                if (slots) for (auto& montage:slots->players)
                    montage.apply(*asset->model,pose->local);
                if (physical && physical->enabled &&
                    !blendPhysicalPose(pose->local,physical->local,
                        physical->boneWeights,physical->weight)) {
                    validInputs=false;
                }
                if(recovery && (!physical || !physical->enabled)) {
                    if(recovery->requested) {
                        const auto& from=recovery->from.size()==pose->local.size()
                            ?recovery->from:previous;
                        if(from.size()==pose->local.size() &&
                           std::isfinite(recovery->duration) &&
                           recovery->duration>0.f) {
                            recovery->inertializer.begin(from,pose->local,
                                                         recovery->duration);
                        } else validInputs=false;
                        recovery->requested=false;
                        recovery->from.clear();
                    }
                    if(recovery->inertializer.active()) {
                        recovery->inertializer.advance(dt);
                        recovery->inertializer.apply(pose->local);
                    }
                }
                // Exactly one post-Graph facial IK/morph layer. The generic
                // compiled control rig reuses AnimationRig::execute rather
                // than spawning an independent graph pose writer.
                if(face && face->enabled && face->rig) {
                    const float sampledDt=(budget && budget->elapsed>dt)
                        ?budget->elapsed:dt;
                    if(!face->rig->apply(
                            *asset->model,face->controls,pose->local,
                            face->state,sampledDt,face->weight,&faceError))
                        validInputs=false;
                }
                render::worldMatrices(*asset->model,pose->local,pose->matrices);
            }
            else { pose->local.clear(); pose->matrices.clear(); }
            if (budget) budget->elapsed=0;
            if (status) ++status->evaluations;
        }
        if (status) {
            status->healthy=validInputs && inst.healthy();
            status->error=validInputs?inst.error():
                (faceError.empty()?"invalid animation, physical or facial input":faceError);
            status->state=inst.state();
        }
    }
}
inline void stepAnimationPresentation(World& world,float dt) {
    if(!std::isfinite(dt) || dt<0.f)return;
    auto prepared=prepareAnimationPresentation(world);
    evaluateAnimationPresentationRange(world,prepared,dt,0,
        prepared.players?prepared.players->size():0);
}
} // namespace aiverse::lib::animation
