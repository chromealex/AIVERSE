// AIVERSE View — genre-independent presentation scene.
// View owns ONLY rendered scene state. Gameplay modules adapt their entities
// into ViewObjects; the scene doesn't know what genre produced those objects.
#pragma once
#include "engine/animation/MotionWeaveSourceBridge.h"
#include "engine/animation/MotionWeaveTelemetry.h"
#include "engine/animation/AnimationBudgetPolicy.h"
#include "engine/animation/MotionWeave.h"
#include "engine/render/Renderer3D.h"
#include "SceneLayerProviders.h"
#include "ModelComposition.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <stdexcept>
#include <unordered_map>
#include <type_traits>
#include <variant>
#include <vector>

namespace aiverse::view {

// RenderObject is a generic draw packet. All GPU models/materials/extra
// pointers remain host-owned and valid until submit() completes.
// The optional graph operates on a separate presentation ECS World.
struct Object {
    // Preserve existing aggregate initialization order for native project
    // code and earlier View adapters; all Stage 121 fields are appended.
    uint64_t id=0,generation=0;
    render::ModelDraw draw;
    std::shared_ptr<const render::AnimationGraph> graph;
    render::AnimationParameters parameters;
    lib::animation::AnimationSignificanceComponent significance;
    bool visible=true;
    // The graph's source rig is stable even if a whole-asset visual LOD
    // replaces draw.model. Null uses draw.model as before (compatible API).
    const render::Model* animationModel=nullptr;
    // Optional whole-model visual LOD. Native Renderer3D mesh LOD stays active.
    // Switching these never creates, resets or advances an animation player.
    std::vector<ModelLod> lods;
    float lodHysteresis=0.1f;
    // Child model draws attached to named/indexed nodes of animationModel.
    // Attachments reuse the parent's already evaluated pose, no extra update.
    std::vector<ModelAttachment> attachments;
    // Monotonically increasing asset-publication version. Must change
    // on source Model replacement, even if its memory address is reused.
    // Zero opts out of compiled retargeting for older/native hosts.
    uint64_t animationModelGeneration=0;
};
inline const render::Model* animationSourceModel(const Object& obj) noexcept {
    return obj.animationModel?obj.animationModel:obj.draw.model;
}
// Any host may submit geometry without assuming a Model or skeleton.
// Mesh ownership/lifetime remains external; this packet has no genre data.
struct MeshObject {
    const render::Mesh* mesh=nullptr;
    render::mat4 world;
    render::float4 color{1,1,1,1};
    uint32_t flags=0;
    float specular=.25f,emissive=0.f;
    // Generic selection/hover silhouette: an opt-in visual style, never
    // a genre-specific selection component. Uses the renderer's existing
    // mask/outline pass, preserving native editor parity.
    bool selectionOutline=false;
    render::float4 selectionColor{1,1,1,1};
    bool visible=true;
};
struct LineObject {
    render::float3 start{},end{};
    uint32_t rgba=0xffffffffu;
};
struct TriangleObject {
    render::float3 a{},b{},c{};
    uint32_t rgba=0xffffffffu;
};
// A simple visual ring in the scene's XZ plane. The host chooses whether
// it means selection, targeting, a debug gizmo, an indicator or anything else.
struct CircleXZObject {
    render::float3 center{};
    float radius=1.f;
    uint32_t rgba=0xffffffffu;
    int segments=40;
    float y=0.03f;
};
// Ground-aligned ribbon between two scene-space points. Used by paths,
// guides and markers in any project, without any simulation semantics.
struct QuadXZObject {
    render::float3 start{},end{};
    float width=1.f;
    uint32_t rgba=0xffffffffu;
    float y=0.02f;
};
// A draw-command stream can interleave skinned models, ordinary meshes
// and debug primitives without changing the host's original draw order.
// All ModelDraw pointers are borrowed until a synchronous submit returns.
using ScenePacket=std::variant<Object,MeshObject,LineObject,TriangleObject,CircleXZObject,QuadXZObject>;
class ScenePacketBatch final {
public:
    void reserve(size_t n){packets_.reserve(n);}
    template<class Packet>
    void add(Packet&& packet){
        packets_.emplace_back(std::forward<Packet>(packet));
    }
    const std::vector<ScenePacket>& packets() const noexcept{return packets_;}
    size_t size() const noexcept{return packets_.size();}
    void clear()noexcept{packets_.clear();}
private:
    std::vector<ScenePacket> packets_;
};
struct Stats {
    lib::animation::AnimationBridgeStats source;
    lib::animation::MotionWeaveScheduleStats animation;
    lib::animation::AnimationBudgetStats budget;
    size_t visible=0,drawable=0,animated=0;
};
// Submission diagnostics are not animation clocks or gameplay counters.
// Values reset at each View::update / beginPreparedFrame, and count actual
// submitted source poses during the following synchronous View draws.
struct ModelRetargetStats {
    uint64_t compiledUsed=0,compiledRejected=0;
    uint64_t exactPoseReused=0,legacyValidated=0,bindPoseFallback=0;
    uint64_t compiledMissingGeneration=0;
    uint64_t compiledExpiredGeneration=0;
    uint64_t compiledModelMismatch=0;
    uint64_t compiledInvalidPoseOrShape=0;
    // Capacity trims are per-frame diagnostics; CPU profiling required
    // before interpreting them as measured allocation savings.
    uint64_t scratchTrims=0;
};
class View final {
public:
    explicit View(size_t animationWorkers=
            lib::animation::MotionWeaveWorkerPool::recommendedWorkers())
        :workerCount_(animationWorkers){}
    View(const View&)=delete;
    View& operator=(const View&)=delete;

    // Owner-thread candidate for an all-owner frame transaction. Copying World
    // alone is insufficient: AnimationPlayer contains a mutable shared graph.
    // External host components/callbacks must not mutate shared external state.
    // Keep the source Models/Graphs leased until candidate submission completes.
    std::unique_ptr<View> cloneForPreparation() const {
        if(retargetScratchBusy_)
            throw std::logic_error("View preparation during model submission");
        auto next=std::make_unique<View>(workerCount_);
        next->presentation_=presentation_;
        next->presentation_.eachExisting<lib::animation::AnimationPlayerComponent>(
            [](Entity,auto& player) {
                if(player.instance)player.instance=player.instance->cloneForPreparation();
            });
        next->bridge_=bridge_;
        next->budgets_=budgets_;
        next->minimumParallelActors_=minimumParallelActors_;
        next->stats_=stats_;
        next->retargetStats_=retargetStats_;
        next->retargetScratchBudgetBytes_=retargetScratchBudgetBytes_;
        next->frameStamp_=frameStamp_;
        next->lodState_=lodState_;
        next->cameraLodState_=cameraLodState_;
        // Scheduler workers have no persistent pose authority: share them
        // with the candidate instead of respawning worker threads for every
        // prepared frame (update() unshares if both run at once). Per-submit
        // scratch is still built lazily in the independent candidate.
        next->scheduler_=scheduler_;
        return next;
    }
    std::unique_ptr<View> prepareUpdate(std::span<const Object> objects,float dt) const {
        auto next=cloneForPreparation();
        next->update(objects,dt);
        return next; // publish via the host's noexcept unique_ptr swap
    }
    std::unique_ptr<View> prepareUpdate(const ScenePacketBatch& batch,float dt) const {
        auto next=cloneForPreparation();
        next->update(batch,dt);
        return next;
    }

    // Canonical genre-independent camera entry. Renderer3D keeps ownership
    // of the frame and the RHI, View forwards a camera with no game rules.
    void beginScene(render::Renderer3D& renderer,
                    const render::Camera3D& camera,int fbWidth,int fbHeight) const {
        renderer.beginScene(camera,fbWidth,fbHeight);
    }

    // Canonical presentation frame-layer barrier. Providers remain host-
    // owned and may capture terrain, renderer or editor state by reference;
    // View consumes all callbacks synchronously on the calling thread.
    // The queue owns the original insertion order and exception cleanup.
    static view::LayerQueueStats submitLayers(view::ViewLayerQueue& queue) {
        return queue.flush();
    }
    static view::LayerQueueStats submitLayers(
            const view::ViewLayerProviders& providers,
            const view::ViewFrameInfo& frame) {
        view::ViewLayerQueue queue;
        providers.collect(queue,frame);
        return submitLayers(queue);
    }

    // Frame staging is a separate phase: material/geometry and GPU calls
    // never overlap worker animation updates. Hosts may use the same
    // generic scene for characters, buildings, props and cinematics.
    Stats update(std::span<const Object> objects,float dt) {
        // Anonymous objects may be submitted with host-computed poses,
        // but a View-owned Graph must have a durable, nonzero source ID.
        // Validate the complete batch before advancing any ECS clocks.
        for(const auto& obj:objects)
            if(obj.graph && animationSourceModel(obj) && obj.id==0)
                throw std::invalid_argument(
                    "View animation requires a stable nonzero object ID");
        std::vector<lib::animation::AnimationSourceFrame> sources;
        sources.reserve(objects.size());
        for(const auto& obj:objects) {
            const auto* animationModel=animationSourceModel(obj);
            if(!obj.graph || !animationModel)continue;
            lib::animation::AnimationSourceFrame input;
            input.sourceId=obj.id;
            input.generation=obj.generation;
            input.model=animationModel;
            input.graph=obj.graph;
            input.inputs=obj.parameters;
            input.modelPublicationGeneration=obj.animationModelGeneration;
            sources.push_back(std::move(input));
        }
        // The bridge validates the whole batch (duplicate IDs, graph/model
        // mismatch, epoch without recompile) before it mutates the World.
        // Run it before touching stats, frame stamp or LOD hysteresis so a
        // rejected batch leaves this View exactly as it was.
        const auto sourceStats=bridge_.sync(presentation_,sources);
        stats_={};
        retargetStats_={};
        if(++frameStamp_==0) {
            frameStamp_=1; lodState_.clear();cameraLodState_.clear();
        }
        for(const auto& obj:objects) {
            if(obj.visible)++stats_.visible;
            if(obj.draw.model && obj.visible)++stats_.drawable;
            if(obj.id!=0) {
                if(obj.lods.empty()) {
                    // No whole-model LOD configuration is published now.
                    // A later reintroduction must start fresh.
                    lodState_.erase(obj.id);
                } else {
                    auto& selection=lodState_[obj.id];
                    if(selection.generation!=obj.generation ||
                       selection.sourcePublication!=obj.animationModelGeneration ||
                       selection.sourceModel!=animationSourceModel(obj) ||
                       selection.baseModel!=obj.draw.model) {
                        selection.level=0;
                        selection.revisions.clear();
                    }
                    selection.generation=obj.generation;
                    selection.sourcePublication=obj.animationModelGeneration;
                    selection.sourceModel=animationSourceModel(obj);
                    selection.baseModel=obj.draw.model;
                    selection.seen=frameStamp_;
                }
            }
            if(obj.graph && animationSourceModel(obj))++stats_.animated;
        }
        stats_.source=sourceStats;
        for(auto it=lodState_.begin();it!=lodState_.end();) {
            if(it->second.seen!=frameStamp_)it=lodState_.erase(it);
            else ++it;
        }
        pruneCameraLodState(frameStamp_-1);
        for(const auto& obj:objects) {
            if(!obj.graph || !animationSourceModel(obj))continue;
            const Entity e=bridge_.find(obj.id);
            if(!presentation_.alive(e))continue;
            if(!presentation_.has<lib::animation::AnimationSignificanceComponent>(e))
                presentation_.add<lib::animation::AnimationSignificanceComponent>(e);
            auto& significance=
                presentation_.get<lib::animation::AnimationSignificanceComponent>(e);
            significance=obj.significance;
            significance.visible=obj.visible && obj.significance.visible;
        }
        stats_.budget=lib::animation::assignAnimationBudgets(presentation_,budgets_);
        if(stats_.animated>=minimumParallelActors_) {
            if(!scheduler_)scheduler_=std::make_shared<SharedScheduler>(workerCount_);
            if(scheduler_->busy.exchange(true,std::memory_order_acquire)) {
                // A View sharing this scheduler is updating concurrently:
                // never re-enter it, take a private one instead.
                scheduler_=std::make_shared<SharedScheduler>(workerCount_);
                scheduler_->busy.store(true,std::memory_order_relaxed);
            }
            struct Release {
                std::shared_ptr<SharedScheduler> held;
                ~Release(){held->busy.store(false,std::memory_order_release);}
            } release{scheduler_};
            stats_.animation=lib::animation::MotionWeave::update(
                presentation_,dt,release.held->scheduler);
        } else
            lib::animation::MotionWeave::update(presentation_,dt);
        return stats_;
    }

    // Native/FPS hosts with precomputed poses do not call update() here:
    // their own presentation World already advanced the animation graph.
    // Begin one host submission frame to prune stale LOD hysteresis IDs,
    // without invoking any ECS animation system or GPU command.
    void beginPreparedFrame() {
        retargetStats_={};
        const uint64_t previous=frameStamp_;
        if(++frameStamp_==0) {
            frameStamp_=1;lodState_.clear();cameraLodState_.clear();return;
        }
        for(auto it=lodState_.begin();it!=lodState_.end();) {
            if(it->second.seen<previous)it=lodState_.erase(it);
            else ++it;
        }
        pruneCameraLodState(previous);
    }

    // A mixed packet stream can be the single source of animation inputs
    // and draw order. Extract only model packets; static meshes and debug
    // primitives do not enter the presentation ECS World. The scratch
    // storage is reused across frames and is never retained by the bridge.
    // Call exactly once per frame before submitPackets(), not in addition
    // to update(objects,dt), or animations would advance twice.
    Stats update(const ScenePacketBatch& batch,float dt) {
        packetObjects_.clear();
        packetObjects_.reserve(batch.size());
        for(const ScenePacket& packet:batch.packets()) {
            if(const auto* object=std::get_if<Object>(&packet))
                packetObjects_.push_back(*object);
        }
        return update(std::span<const Object>(
            packetObjects_.data(),packetObjects_.size()),dt);
    }

    // Precomputed model packets need no animation evaluation or source
    // registration. This is the canonical synchronous path for host-supplied
    // poses, baked samples, stage/LOD models and attachments.
    static void submitModel(render::Renderer3D& renderer,
                            const render::ModelDraw& draw) {
        if(draw.model)renderer.drawModel(draw);
    }

    // This is independently testable without GPU or Renderer3D. It is the
    // only write to per-camera LOD hysteresis during a model submission.
    // Camera scope zero is the legacy single-camera state map.
    size_t selectCameraLodLevel(const Object& obj,float distance,
                                uint64_t cameraScope=0) const {
        if(obj.lods.empty())return 0;
        size_t prior=0;
        LodSelection* saved=nullptr;
        const auto* rig=animationSourceModel(obj);
        if(obj.id!=0) {
            auto& state=cameraScope
                ? cameraLodState_[cameraScope][obj.id]
                : lodState_[obj.id];
            const bool revised=state.generation!=obj.generation ||
                state.sourcePublication!=obj.animationModelGeneration ||
                state.sourceModel!=rig ||
                state.baseModel!=obj.draw.model ||
                state.hysteresis!=obj.lodHysteresis ||
                !matchesLodRevisions(state.revisions,obj.lods);
            if(revised) {
                state.level=0;
                captureLodRevisions(state.revisions,obj.lods);
            }
            state.generation=obj.generation;
            state.sourcePublication=obj.animationModelGeneration;
            state.sourceModel=rig;
            state.baseModel=obj.draw.model;
            state.hysteresis=obj.lodHysteresis;
            saved=&state;
            prior=state.level;
        }
        const size_t level=selectModelLodLevel(obj.lods,distance,prior,
                                              saved?obj.lodHysteresis:0.f);
        if(saved) {
            saved->level=level;
            saved->seen=frameStamp_;
        }
        return level;
    }

    // Call only AFTER update(), before borrowed ModelDraw storage expires.
    // The renderer does not interpret source identity, movement or genre.
    // One draw packet from any host. This is the same renderer pipeline
    // used for streamed, procedural, static and animated objects.
    // Camera scope 0 retains legacy behavior; nonzero camera keys isolate LOD hysteresis.
    void submitOne(render::Renderer3D& renderer,const Object& obj,
                   uint64_t cameraScope=0) const {
        if(!obj.visible)return;
        const render::Model* rig=animationSourceModel(obj);
        const lib::animation::AnimationPoseComponent* animatedPose=nullptr;
        if(obj.graph && rig && obj.graph->model()==rig) {
            const Entity e=bridge_.find(obj.id);
            if(presentation_.alive(e)) {
                const auto* link=presentation_.tryGetConst<
                    lib::animation::AnimationSourceLinkComponent>(e);
                const auto* asset=presentation_.tryGetConst<
                    lib::animation::AnimationAssetComponent>(e);
                const auto* pose=presentation_.tryGetConst<
                    lib::animation::AnimationPoseComponent>(e);
                if(link && link->generation==obj.generation &&
                   link->modelPublicationGeneration==
                       obj.animationModelGeneration &&
                   asset && asset->model==rig && asset->graph==obj.graph &&
                   pose && pose->valid &&
                   pose->local.size()==rig->nodes.size() &&
                   pose->matrices.size()==rig->nodes.size())
                    animatedPose=pose;
            }
        }
        // FPS, native and cinematic hosts may have evaluated this pose
        // already in their own presentation World. Do not step the graph
        // twice just because another visual LOD is selected here.
        const render::Pose* sourcePose=nullptr;
        const std::vector<render::mat4>* sourceMatrices=nullptr;
        if(animatedPose) {
            sourcePose=&animatedPose->local;
            sourceMatrices=&animatedPose->matrices;
        }else if(!obj.graph && rig && obj.draw.pose && obj.draw.nodeWorld &&
                 obj.draw.pose->size()==rig->nodes.size() &&
                 obj.draw.nodeWorld->size()==rig->nodes.size()) {
            sourcePose=obj.draw.pose;
            sourceMatrices=obj.draw.nodeWorld;
        }
        render::ModelDraw draw=obj.draw;
        const ModelLod* chosenLod=nullptr;
        if(draw.model && !obj.lods.empty()) {
            const render::float3 origin=draw.world.transformPoint({0,0,0});
            const float distance=render::length(renderer.cameraPos()-origin);
            const size_t level=selectCameraLodLevel(
                obj,distance,cameraScope);
            // Missing streamed distant LODs retain the nearest available
            // representation. The base graph still advances every frame.
            size_t visual=level;
            while(visual>0 && !obj.lods[visual-1].model)--visual;
            if(visual>0) {
                chosenLod=&obj.lods[visual-1];
                draw.model=chosenLod->model;
            }
        }
        if(draw.model!=obj.draw.model) {
            // Overrides and cached GPU geometry are Model-generation
            // specific. Rebinding these across whole-asset LODs is unsafe.
            draw.geometry={};
            draw.staticCache=nullptr;
            draw.materials=nullptr;
            draw.nodeMaterial=nullptr;
            draw.variant=-1;
            draw.pose=nullptr;
            draw.nodeWorld=nullptr;
            draw.baked=nullptr;
        }
        // Renderer3D::drawModel consumes pose/matrices synchronously and
        // copies the GPU draw items by value. Keep scratch capacity across
        // thousands of draws in the same View, without sharing buffers
        // between Editor, FPS or native View instances.
        //
        // A Model capture callback may reenter submitOne; its nested draw
        // must use independent stack scratch rather than overwrite pointers
        // still being read by the outer synchronous submission.
        RetargetScratch reentrantScratch;
        struct ReleaseScratch {
            bool* busy=nullptr;
            RetargetScratch* retained=nullptr;
            size_t budget=0;
            uint64_t* trims=nullptr;
            ~ReleaseScratch() noexcept {
                if(retained) {
                    // Budget limits RETAINED capacity, never the in-flight
                    // actor's required pose size. Release only after the
                    // synchronous Renderer3D submission and any capture
                    // callbacks have finished reading these buffers.
                    const size_t poseLimit=budget/sizeof(render::NodePose);
                    const bool poseOver=retained->pose.capacity()>poseLimit;
                    const size_t poseBytes=poseOver?budget:
                        retained->pose.capacity()*sizeof(render::NodePose);
                    const bool worldOver=retained->matrices.capacity()>
                        (budget-poseBytes)/sizeof(render::mat4);
                    if(poseOver || worldOver) {
                        render::Pose{}.swap(retained->pose);
                        std::vector<render::mat4>{}.swap(retained->matrices);
                        if(trims)++*trims;
                    }
                }
                if(busy)*busy=false;
            }
        } releaseScratch;
        RetargetScratch* scratch=&reentrantScratch;
        if(!retargetScratchBusy_) {
            retargetScratchBusy_=true;
            releaseScratch.busy=&retargetScratchBusy_;
            releaseScratch.retained=&retargetScratch_;
            releaseScratch.budget=retargetScratchBudgetBytes_;
            releaseScratch.trims=&retargetStats_.scratchTrims;
            scratch=&retargetScratch_;
        }
        auto& retargetPose=scratch->pose;
        auto& retargetMatrices=scratch->matrices;
        if(sourcePose && sourceMatrices && rig && draw.model) {
            if(chosenLod && chosenLod->compiledRetarget) {
                // Compiled maps are bound to BOTH model identity and
                // publication generation. Never execute an expired plan,
                // even when the allocator has reused a Model pointer.
                if(retargetCompiledModelPose(*rig,*draw.model,
                        obj.animationModelGeneration,
                        chosenLod->visualModelGeneration,
                        std::span<const render::NodePose>(
                            sourcePose->data(),sourcePose->size()),
                        *chosenLod->compiledRetarget,retargetPose,
                        retargetMatrices)) {
                    draw.pose=&retargetPose;
                    draw.nodeWorld=&retargetMatrices;
                    ++retargetStats_.compiledUsed;
                }else {
                    draw.pose=nullptr;
                    draw.nodeWorld=nullptr;
                    ++retargetStats_.compiledRejected;
                    // These checks use pointer identity + generation only;
                    // they never access the retired compiled plan's model
                    // pointers or revalidate the authored node hierarchy.
                    const auto status=chosenLod->compiledRetarget->status(
                        *rig,*draw.model,obj.animationModelGeneration,
                        chosenLod->visualModelGeneration);
                    switch(status) {
                        case CompiledRetargetStatus::MissingSourceGeneration:
                        case CompiledRetargetStatus::MissingVisualGeneration:
                            ++retargetStats_.compiledMissingGeneration;break;
                        case CompiledRetargetStatus::SourceGenerationChanged:
                        case CompiledRetargetStatus::VisualGenerationChanged:
                            ++retargetStats_.compiledExpiredGeneration;break;
                        case CompiledRetargetStatus::SourceModelChanged:
                        case CompiledRetargetStatus::VisualModelChanged:
                            ++retargetStats_.compiledModelMismatch;break;
                        default:
                            ++retargetStats_.compiledInvalidPoseOrShape;break;
                    }
                }
            }else if(compatibleModelPose(*rig,*draw.model)) {
                draw.pose=sourcePose;
                draw.nodeWorld=sourceMatrices;
                ++retargetStats_.exactPoseReused;
            }else if(chosenLod && chosenLod->retarget &&
                     retargetModelPose(*rig,*draw.model,
                         std::span<const render::NodePose>(
                             sourcePose->data(),sourcePose->size()),
                         *chosenLod->retarget,retargetPose,
                         retargetMatrices)) {
                // Compatibility path for legacy callers without a compiled
                // plan; Stage 123 native clients should compile at publish.
                draw.pose=&retargetPose;
                draw.nodeWorld=&retargetMatrices;
                ++retargetStats_.legacyValidated;
            }else {
                // A mismatched source rig MUST NOT be applied to the
                // visual Model. Keep the visual's own bind pose.
                draw.pose=nullptr;
                draw.nodeWorld=nullptr;
                ++retargetStats_.bindPoseFallback;
            }
        }else if(draw.model && rig && draw.model!=rig) {
            draw.pose=nullptr;
            draw.nodeWorld=nullptr;
            ++retargetStats_.bindPoseFallback;
        }
        if(draw.model) {
            if(!draw.persistentId)draw.persistentId=obj.id;
            submitModel(renderer,draw);
        }
        if(!rig || obj.attachments.empty())return;
        const std::span<const render::mat4> socketMatrices=
            sourceMatrices ? std::span<const render::mat4>(
                sourceMatrices->data(),sourceMatrices->size())
                         : std::span<const render::mat4>();
        // Sockets always follow the base source rig, not the visual
        // LOD, including when the visual has fewer bones.
        for(size_t i=0;i<obj.attachments.size();++i) {
            render::ModelDraw attached;
            if(!composeAttachment(obj.draw,*rig,socketMatrices,
                                  obj.attachments[i],
                                  obj.animationModelGeneration,attached))continue;
            if(!attached.persistentId && obj.id)
                attached.persistentId=attachmentDrawId(
                    obj.id,obj.generation,obj.animationModelGeneration,i);
            submitModel(renderer,attached);
        }
    }
    void submit(render::Renderer3D& renderer,
                std::span<const Object> objects,
                uint64_t cameraScope=0) const {
        for(const auto& obj:objects)submitOne(renderer,obj,cameraScope);
    }
    void forgetCameraScope(uint64_t cameraScope) {
        if(cameraScope)cameraLodState_.erase(cameraScope);
    }
    // Ordinary mesh/collision visualization, terrain tiles, gizmo backing
    // meshes and other plain draw packets use the SAME generic View API.
    static void submitMesh(render::Renderer3D& renderer,
                           const MeshObject& object) {
        if(!object.visible || !object.mesh)return;
        if(object.selectionOutline) {
            renderer.drawSelectionMesh(*object.mesh,object.world,object.color,
                                       object.selectionColor);
        } else {
            renderer.drawMesh(*object.mesh,object.world,object.color,
                object.flags,object.specular,object.emissive);
        }
    }
    static void submitMeshes(render::Renderer3D& renderer,
                             std::span<const MeshObject> objects) {
        for(const auto& object:objects)submitMesh(renderer,object);
    }
    // Immediate geometry is genre-free: editors, diagnostics, debug
    // guides and any game host pass the same line/triangle packets.
    static void submitLine(render::Renderer3D& renderer,const LineObject& line) {
        renderer.line(line.start,line.end,line.rgba);
    }
    static void submitTriangle(render::Renderer3D& renderer,
                               const TriangleObject& triangle) {
        renderer.triangle(triangle.a,triangle.b,triangle.c,triangle.rgba);
    }
    static void submitCircle(render::Renderer3D& renderer,
                             const CircleXZObject& circle) {
        if(circle.radius>0.f && circle.segments>=3)
            renderer.circleXZ(circle.center,circle.radius,circle.rgba,
                              circle.segments,circle.y);
    }
    static void submitQuadXZ(render::Renderer3D& renderer,
                             const QuadXZObject& quad) {
        if(quad.width>0.f && std::isfinite(quad.width) &&
           std::isfinite(quad.y))
            renderer.quadXZ(quad.start,quad.end,quad.width,quad.rgba,quad.y);
    }
    // Synchronous ordered dispatch for arbitrary mixed scene packets.
    // The renderer retains its own internal batching and pass order.
    void submitPacket(render::Renderer3D& renderer,
                      const ScenePacket& packet) const {
        std::visit([&](const auto& item) {
            using Item=std::decay_t<decltype(item)>;
            if constexpr(std::is_same_v<Item,Object>)
                submitOne(renderer,item);
            else if constexpr(std::is_same_v<Item,MeshObject>)
                submitMesh(renderer,item);
            else if constexpr(std::is_same_v<Item,LineObject>)
                submitLine(renderer,item);
            else if constexpr(std::is_same_v<Item,TriangleObject>)
                submitTriangle(renderer,item);
            else if constexpr(std::is_same_v<Item,CircleXZObject>)
                submitCircle(renderer,item);
            else if constexpr(std::is_same_v<Item,QuadXZObject>)
                submitQuadXZ(renderer,item);
        },packet);
    }
    void submitPackets(render::Renderer3D& renderer,
                       std::span<const ScenePacket> packets) const {
        for(const auto& packet:packets)submitPacket(renderer,packet);
    }
    void submitPackets(render::Renderer3D& renderer,
                       const ScenePacketBatch& batch) const {
        submitPackets(renderer,batch.packets());
    }

    const Stats& stats() const noexcept{return stats_;}
    ModelRetargetStats modelRetargetStats() const noexcept{return retargetStats_;}
    // Bounds retained CPU pose/matrix memory per View; a single large
    // synchronous draw is still allowed and releases excess after submit.
    // Zero disables retention. Must be called on the render-owner thread
    // outside submitOne()/nested capture callbacks.
    size_t retargetScratchBudgetBytes() const noexcept {
        return retargetScratchBudgetBytes_;
    }
    size_t retargetScratchCapacityBytes() const noexcept {
        return retargetScratch_.pose.capacity()*sizeof(render::NodePose) +
               retargetScratch_.matrices.capacity()*sizeof(render::mat4);
    }
    void setRetargetScratchBudgetBytes(size_t bytes) {
        if(retargetScratchBusy_)
            throw std::logic_error("View scratch budget cannot change during model submit");
        retargetScratchBudgetBytes_=bytes;
        if(retargetScratchCapacityBytes()>bytes) {
            render::Pose{}.swap(retargetScratch_.pose);
            std::vector<render::mat4>{}.swap(retargetScratch_.matrices);
            ++retargetStats_.scratchTrims;
        }
    }
    void setBudgetPolicy(lib::animation::AnimationBudgetPolicy policy) {
        budgets_=policy;
    }
    void setParallelThreshold(size_t count) {minimumParallelActors_=count;}
    // Diagnostic: a preparation candidate reuses the published View's
    // animation worker scheduler instead of spawning new threads.
    bool sharesAnimationScheduler(const View& other) const noexcept {
        return scheduler_ && scheduler_==other.scheduler_;
    }
    Entity presentationEntity(uint64_t id) const {return bridge_.find(id);}
    // Host calls this on the published View, not on a preparation candidate.
    // The epoch is host-owned and must change on world replacement.
    std::optional<motionweave_telemetry::Sample> animationTelemetry(
            uint64_t sourceId,uint64_t worldEpoch) const {
        return motionweave_telemetry::read(presentation_,bridge_,sourceId,worldEpoch,frameStamp_);
    }
    bool captureAnimationTelemetry(uint64_t sourceId,uint64_t worldEpoch,
            graph_telemetry::History& history) const {
        auto sample=animationTelemetry(sourceId,worldEpoch);
        return sample&&history.publish(sample->identity,sample->sequence,sample->value);
    }
    // Detach selected presentation sources before their host-owned models
    // are evicted; other graph players keep their clocks and pose state.
    size_t forgetSources(std::span<const uint64_t> ids) {
        const size_t removed=bridge_.forget(presentation_,ids);
        for(const auto id:ids) {
            lodState_.erase(id);
            for(auto& [scope,states]:cameraLodState_) {
                (void)scope;
                states.erase(id);
            }
        }
        return removed;
    }
    // Opt-in source reimport, while the old CPU Model and compiled Graph
    // remain readable. Keeps only a bounded portable named playback seed;
    // subsequent compatible new-epoch sync consumes it once or restarts.
    size_t forgetSourcesForReimport(std::span<const uint64_t> ids) {
        const size_t removed=bridge_.forgetForReimport(presentation_,ids);
        // The next published source starts with a fresh visual-LOD state,
        // independently of optional named animation-clock continuity.
        for(const auto id:ids) {
            lodState_.erase(id);
            for(auto& [scope,states]:cameraLodState_) {
                (void)scope;
                states.erase(id);
            }
        }
        return removed;
    }
    const World& presentation() const noexcept{return presentation_;}
    // For controlled host-only effects such as montage slots. Never
    // snapshot this World or mutate it while update() is running.
    World& presentation() noexcept{return presentation_;}
    void clear() {
        if(retargetScratchBusy_)
            throw std::logic_error("View cannot clear during nested model submission");
        bridge_.clear(presentation_);
        lodState_.clear();
        cameraLodState_.clear();
        stats_={};
        retargetStats_={};
        // Never preserve transient model-space poses across a View owner
        // reset; subsequent draw will repopulate from its new rig.
        retargetScratch_.pose.clear();
        retargetScratch_.matrices.clear();
    }
private:
    struct RetargetScratch {
        render::Pose pose;
        std::vector<render::mat4> matrices;
    };
    // Exact layout identity, including levels not currently selected.
    // Pointers are comparison-only and never dereferenced after retirement.
    struct LodRevision {
        const render::Model* model=nullptr;
        float minDistance=0.f;
        uint64_t publication=0;
        const ModelPoseRetarget* retarget=nullptr;
        const CompiledModelPoseRetarget* compiledRetarget=nullptr;
    };
    static bool matchesLodRevisions(
            const std::vector<LodRevision>& cached,
            std::span<const ModelLod> authored) noexcept {
        if(cached.size()!=authored.size())return false;
        for(size_t i=0;i<authored.size();++i) {
            const auto& a=authored[i];
            const auto& c=cached[i];
            if(c.model!=a.model || c.minDistance!=a.minDistance ||
               c.publication!=a.visualModelGeneration ||
               c.retarget!=a.retarget.get() ||
               c.compiledRetarget!=a.compiledRetarget.get())return false;
        }
        return true;
    }
    static void captureLodRevisions(
            std::vector<LodRevision>& cached,
            std::span<const ModelLod> authored) {
        // Reserve first so an allocation failure does not commit partial
        // changes to the previous snapshot.
        std::vector<LodRevision> next;
        next.reserve(authored.size());
        for(const auto& a:authored)
            next.push_back({a.model,a.minDistance,
                            a.visualModelGeneration,a.retarget.get(),
                            a.compiledRetarget.get()});
        cached.swap(next);
    }
    struct LodSelection {
        uint64_t generation=0;
        uint64_t sourcePublication=0;
        const render::Model* sourceModel=nullptr;
        const render::Model* baseModel=nullptr;
        float hysteresis=0.1f;
        size_t level=0;
        uint64_t seen=0;
        std::vector<LodRevision> revisions;
    };
    static uint64_t attachmentDrawId(uint64_t id,uint64_t generation,
                                     uint64_t modelPublication,
                                     size_t attachmentIndex) noexcept {
        uint64_t x=id*0x9e3779b97f4a7c15ULL ^
                   generation*0xbf58476d1ce4e5b9ULL ^
                   modelPublication*0xd6e8feb86659fd93ULL ^
                   (uint64_t(attachmentIndex)+1)*0x94d049bb133111ebULL;
        x^=x>>30;
        x*=0xbf58476d1ce4e5b9ULL;
        x^=x>>27;
        x*=0x94d049bb133111ebULL;
        x^=x>>31;
        return x?x:1;
    }
    World presentation_;
    lib::animation::MotionWeaveSourceBridge bridge_;
    // Worker threads + per-frame scratch only (no World state); shared
    // between a published View and its preparation candidates.
    struct SharedScheduler {
        explicit SharedScheduler(size_t workers):scheduler(workers){}
        lib::animation::MotionWeaveScheduler scheduler;
        std::atomic<bool> busy{false};
    };
    std::shared_ptr<SharedScheduler> scheduler_;
    lib::animation::AnimationBudgetPolicy budgets_;
    size_t workerCount_=0,minimumParallelActors_=16;
    Stats stats_;
    mutable ModelRetargetStats retargetStats_;
    // Render-owner thread only, never accessed by the ECS worker pool.
    mutable RetargetScratch retargetScratch_;
    // A 512 KiB default holds common 1-500 bone visual rigs while
    // avoiding indefinite retention of an exceptionally huge rig.
    size_t retargetScratchBudgetBytes_=512u*1024u;
    mutable bool retargetScratchBusy_=false;
    uint64_t frameStamp_=0;
    mutable std::unordered_map<uint64_t,LodSelection> lodState_;
    mutable std::unordered_map<uint64_t,
        std::unordered_map<uint64_t,LodSelection>> cameraLodState_;
    void pruneCameraLodState(uint64_t lastFrame) {
        for(auto scope=cameraLodState_.begin();scope!=cameraLodState_.end();) {
            auto& entries=scope->second;
            for(auto it=entries.begin();it!=entries.end();) {
                if(it->second.seen<lastFrame)it=entries.erase(it);
                else ++it;
            }
            if(entries.empty())scope=cameraLodState_.erase(scope);
            else ++scope;
        }
    }
    std::vector<Object> packetObjects_;
};
} // namespace aiverse::view

// Preferred user-facing type: aiverse::View.
// Legacy game::GameView remains a compatibility adapter until all RTS-specific
// terrain/fog/unit event authoring is moved to the owning game module.
namespace aiverse {using View=view::View;}
