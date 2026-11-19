// AIVERSE View — genre-independent presentation scene.
// View owns ONLY rendered scene state. Gameplay modules adapt their entities
// into ViewObjects; the scene doesn't know what genre produced those objects.
#pragma once
#include <aiverselib/animation/MotionWeaveSourceBridge.h>
#include <aiverselib/animation/AnimationBudgetPolicy.h>
#include <aiverselib/motionweave/MotionWeave.h>
#include "engine/render/Renderer3D.h"
#include "SceneLayerProviders.h"
#include <cmath>
#include <cstdint>
#include <memory>
#include <span>
#include <type_traits>
#include <variant>
#include <vector>

namespace aiverse::view {

// RenderObject is a generic draw packet. All GPU models/materials/extra
// pointers remain host-owned and valid until submit() completes.
// The optional graph operates on a separate presentation ECS World.
struct Object {
    uint64_t id=0,generation=0;
    render::ModelDraw draw;
    std::shared_ptr<const render::AnimationGraph> graph;
    render::AnimationParameters parameters;
    lib::animation::AnimationSignificanceComponent significance;
    bool visible=true;
};
// Any host may submit geometry without assuming a Model or skeleton.
// Mesh ownership/lifetime remains external; this packet has no genre data.
struct MeshObject {
    const render::Mesh* mesh=nullptr;
    render::mat4 world;
    render::float4 color{1,1,1,1};
    uint32_t flags=0;
    float specular=.25f,emissive=0.f;
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
class View final {
public:
    explicit View(size_t animationWorkers=
            lib::animation::MotionWeaveWorkerPool::recommendedWorkers())
        :workerCount_(animationWorkers){}
    View(const View&)=delete;
    View& operator=(const View&)=delete;

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
        stats_={};
        std::vector<lib::animation::AnimationSourceFrame> sources;
        sources.reserve(objects.size());
        for(const auto& obj:objects) {
            if(obj.visible)++stats_.visible;
            if(obj.draw.model && obj.visible)++stats_.drawable;
            if(!obj.graph || !obj.draw.model)continue;
            ++stats_.animated;
            lib::animation::AnimationSourceFrame input;
            input.sourceId=obj.id;
            input.generation=obj.generation;
            input.model=obj.draw.model;
            input.graph=obj.graph;
            input.inputs=obj.parameters;
            sources.push_back(std::move(input));
        }
        stats_.source=bridge_.sync(presentation_,sources);
        for(const auto& obj:objects) {
            if(!obj.graph || !obj.draw.model)continue;
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
            if(!scheduler_)scheduler_=std::make_unique<
                lib::animation::MotionWeaveScheduler>(workerCount_);
            stats_.animation=lib::animation::MotionWeave::update(
                presentation_,dt,*scheduler_);
        } else
            lib::animation::MotionWeave::update(presentation_,dt);
        return stats_;
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

    // Call only AFTER update(), before borrowed ModelDraw storage expires.
    // The renderer does not interpret source identity, movement or genre.
    // One draw packet from any host. This is the same renderer pipeline
    // used for streamed, procedural, static and animated objects.
    void submitOne(render::Renderer3D& renderer,const Object& obj) const {
        if(!obj.visible || !obj.draw.model)return;
        render::ModelDraw draw=obj.draw;
        if(obj.graph) {
            const Entity e=bridge_.find(obj.id);
            if(presentation_.alive(e)) {
                const auto* pose=presentation_.tryGetConst<
                    lib::animation::AnimationPoseComponent>(e);
                if(pose && pose->valid) {
                    draw.pose=&pose->local;
                    draw.nodeWorld=&pose->matrices;
                }
            }
        }
        if(!draw.persistentId)draw.persistentId=obj.id;
        submitModel(renderer,draw);
    }
    void submit(render::Renderer3D& renderer,
                std::span<const Object> objects) const {
        for(const auto& obj:objects)submitOne(renderer,obj);
    }
    // Ordinary mesh/collision visualization, terrain tiles, gizmo backing
    // meshes and other plain draw packets use the SAME generic View API.
    static void submitMesh(render::Renderer3D& renderer,
                           const MeshObject& object) {
        if(!object.visible || !object.mesh)return;
        renderer.drawMesh(*object.mesh,object.world,object.color,
            object.flags,object.specular,object.emissive);
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
    void setBudgetPolicy(lib::animation::AnimationBudgetPolicy policy) {
        budgets_=policy;
    }
    void setParallelThreshold(size_t count) {minimumParallelActors_=count;}
    Entity presentationEntity(uint64_t id) const {return bridge_.find(id);}
    // Detach selected presentation sources before their host-owned models
    // are evicted; other graph players keep their clocks and pose state.
    size_t forgetSources(std::span<const uint64_t> ids) {
        return bridge_.forget(presentation_,ids);
    }
    const World& presentation() const noexcept{return presentation_;}
    // For controlled host-only effects such as montage slots. Never
    // snapshot this World or mutate it while update() is running.
    World& presentation() noexcept{return presentation_;}
    void clear() {
        bridge_.clear(presentation_);
        stats_={};
    }
private:
    World presentation_;
    lib::animation::MotionWeaveSourceBridge bridge_;
    std::unique_ptr<lib::animation::MotionWeaveScheduler> scheduler_;
    lib::animation::AnimationBudgetPolicy budgets_;
    size_t workerCount_=0,minimumParallelActors_=16;
    Stats stats_;
    std::vector<Object> packetObjects_;
};
} // namespace aiverse::view

// Preferred user-facing type: aiverse::View.
// Legacy game::RtsView remains a compatibility adapter until all RTS-specific
// terrain/fog/unit event authoring is moved to the owning game module.
namespace aiverse {using View=view::View;}
