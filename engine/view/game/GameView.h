#include "engine/render/ModelTextureCache.h"
#include "engine/render/RenderDeviceLifetime.h"
#include "engine/render/FramePublicationEpoch.h"
#include "engine/render/ModelImageUploadTransaction.h"
#include "engine/view/AppearanceAssetRegistry.h"
// LEGACY compatibility adapter for historical RTS/editor scene presentation.
// Generic game-agnostic View is now <engine/view/View.h> / aiverse::View.
// Keep genre-specific authoring/overlays here temporarily; do NOT add new
// common rendering, animation, culling or batching systems to this class.
// Objects are drawn from their type's "view" block in rules.json, e.g.
//   "view": { "shape": "soldier", "scale": 1.0, "color": [0.8, 0.7, 0.5] }
//   "view": { "model": "models/knight.glb", "height": 1.1, "team": ["Cloth"],
//             "animations": { "move": {"clip": "Walk", "speedRef": 2.8}, "attack": "Slash" },
//             "procedural": [{ "type": "aim", "node": "Turret" }] }
// The full format (models, materials, clips, procedural animation, attachments, stages, LOD,
// effects) is in docs/models.md.
// Built-in procedural shapes cover common archetypes and are the fallback when a model fails:
//   units:      worker, soldier, ranged, vehicle, tank, flyer, mushroom, generic
//   structures: hall, house, barracks, workshop, factory, tower, mine, tree, rock, mushroomhouse, structure
#pragma once
namespace kingston { class TowHost; }
namespace kingston { class BulletFxHost; }
namespace kingston { struct CabinResidentDraw; }
#include "engine/render/GeometryScene.h"
#include <algorithm>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#ifndef __EMSCRIPTEN__
#include <future>
#endif
#include <string>
#include <unordered_map>
#include <vector>
#include "../../render/Animation.h"
#include "../../render/Renderer3D.h"
#include "../../sim/Simulation.h"
#include "../../game/GameData.h"
#include "../../game/MapGrid.h"
#include "Game2DView.h"
#include "engine/render/graph/GraphTelemetry.h"
#include "GameGroundFx.h"
#include "GamePaintLayers.h"
#include "GameGroundCanvas.h"
#include "GameLights.h"
#include "GameFog.h"
#include "GameFieldView.h"
#include "GamePlanetGroundFx.h"
#include "GamePlanetRender.h"
#include "GamePlanetFlora.h"
#include "GameOverlays.h"
#include "GameModelResourceProvider.h"
#include "GameModelResourceCache.h"
#include "GameModelPreparationService.h"
#include "GameTerrainRender.h"
#include "EntityFx.h"

namespace aiverse::render { class CameraModelSnapshot; }
namespace aiverse::render::texture_graph { class TextureGraphGpuOwner; }
namespace aiverse::view {
class View;
struct ModelPoseRetarget;
struct CompiledModelPoseRetarget;
}
namespace aiverse::game {

struct Lane;

struct SceneObject {
    Entity entity;               // invalid for editor placements
    uint16_t type = 0;
    uint8_t owner = 0;
    int16_t teamOwner = -1;       // presentation-only override; -1 follows entity owner
    uint8_t colorOwner() const { return teamOwner < 0 ? owner : uint8_t(teamOwner); }
    render::float3 pos, facing{1, 0, 0};
    bool parented = false;
    render::mat4 parentFrame; // resolved visual frame, including inherited tilt/scale

    float hp = 1;                // 0..1
    float build = 1;             // construction progress 0..1
    int garrison = -1;           // shown when >= 0
    float carry = 0;             // > 0: carrying resources
    std::string heldWeapon; bool weaponAim=false; // Kingston selected arsenal item
    int16_t carryRes = -1;       // resource index of the cargo
    int8_t shotWeapon = -1;      // projectiles: weapon index (of the shooter's type) that fired it; -1: unknown
    bool selected = false, hovered = false, visible = true, isProjectile = false;
    bool blip = false;           // not visible, but a radar / sonar contact of the viewer (Sensors.h): drawn as a dot
    float fade = 1;              // 0..1 opacity from the sim (route walkers fade at the route ends)
    bool unpowered = false;      // disabled by a power deficit / owner in low power (electric lights dim)
    uint8_t action = 0;          // SpriteAction (sprite animations): idle, move, attack, work
    // animation hints (sceneFromSim fills them; editor placements leave the defaults)
    uint8_t order = 0;           // current order: 0 none, else OrderType + 1
    bool moving = false;
    bool producing = false;      // structure training / researching
    bool hasTarget = false;      // combat target (turrets aim at it)
    float steer = 0;             // vehicles (rules "driving"): steering angle of the front wheels, rad, + = left
    render::float3 target;
    Entity source;               // projectiles: who fired (muzzle point)
    uint16_t shooterType = UINT16_MAX; // projectiles: type of the unit that fired it (lights)
    // planets: body index and pos / facing / target body-local (origin = body centre); -1 = flat map
    // coordinates; kBodyTransit = between bodies: pos is the world position, facing the travel direction
    int16_t body = -1;
    static constexpr int16_t kBodyTransit = -2;
    // Optional stable identity supplied by editor/native scene sources whose
    // SceneObject::entity is invalid. Zero uses ECS entity identity or transient.
    uint64_t persistentKey = 0;
    // Studio-authored Transform (sim AuthoredPose): absolute height, X/Z tilt in
    // degrees and per-axis scale, applied about the object's ground point.
    bool posed = false, poseHasY = false;
    float poseY = 0, poseRotX = 0, poseRotZ = 0;
    render::float3 poseScale{1, 1, 1};
};

// World-space premultiplier for SceneObject's authored pose. `facingYaw` is the
// RTS draw yaw (models face +X: yaw = authored Y rotation + 90 degrees), `p`
// the object's pivot. Identity when the object has no tilt / scale.
inline render::mat4 authoredPoseFrame(const SceneObject& o, float facingYaw, render::float3 p) {
    using render::mat4;
    if (!o.posed) return mat4{};
    const bool tilt = o.poseRotX != 0.0f || o.poseRotZ != 0.0f;
    const bool scaled = o.poseScale.x != 1.0f || o.poseScale.y != 1.0f || o.poseScale.z != 1.0f;
    if (!tilt && !scaled) return mat4{};
    // Studio order R = Ry(yaw) * Rx * Rz with yaw the authored Y rotation; the
    // RTS draw already applies Ry(facingYaw) = Ry(yaw + 90 deg), so tilt/scale
    // are expressed in the authored frame and conjugated by the authored yaw.
    constexpr float kDeg = 0.017453292f;
    const float yaw = facingYaw - 1.5707963f;
    return mat4::translate(p) * mat4::rotateY(yaw) * mat4::rotateX(o.poseRotX * kDeg) *
           mat4::rotateZ(o.poseRotZ * kDeg) * mat4::scale(o.poseScale) * mat4::rotateY(-yaw) * mat4::translate(-p);
}


struct GameScene {
    std::shared_ptr<render::PaintLayers> paintLayers;
    // view.entity_fx overrides (scale, opacity, colour) by entity; null = none (EntityFx.h)
    std::shared_ptr<EntityFxStore> entityFx;
    std::vector<SceneObject> objects;
    std::map<uint64_t, render::mat4> entityFrames; // optional final frames supplied by a native presentation host
    std::vector<std::pair<render::float3, render::float3>> roads;
    std::vector<render::float3> moveMarkers;
    // View-only facing history per unit (sceneFromSim): the facing of the last two simulation ticks, so a
    // turning unit (a steered car) rotates smoothly between ticks like its position does. Not simulation state.
    struct FacingTrack { uint32_t tick = 0; uint32_t seen = 0; render::float3 prev{}, cur{}; };
    std::unordered_map<uint64_t, FacingTrack> facingTracks;
    uint32_t facingFrame = 0;
    // events of the last simulation tick (attacks, hits, deaths, spawns) for animations; `tick`
    // tells the view whether they are new
    std::vector<SimEvent> events;
    uint32_t tick = 0;
    double seconds = 0;          // game time (tick / tick rate): drives the day/night cycle
    // Day/night phase from the simulation (0..1, 0 = midnight, 0.5 = noon), when a gameplay day cycle
    // exists (mechanic E, rules.cycle); < 0: the lighting block's own clock. Visual and gameplay
    // night then always agree.
    float cycle = -1;
    // ground overlays (GameGroundFx): the fog-of-war viewer (0 = everything) and the sim's lanes (read only)
    uint8_t viewer = 0;
    const std::vector<Lane>* lanes = nullptr;
    uint32_t laneSignature = 0;
    // grid fields (GameFieldView): definitions (with map overrides) and the layers to draw (read only, valid this frame)
    const std::vector<GridFieldDef>* fieldDefs = nullptr;
    std::vector<FieldSceneLayer> fields;
    // drag-to-send arrows (Mushroom-Wars input) and box selection are drawn by the client
    bool gameplayOverlays=true; // authoring Scene opts out; native game hosts retain module HUD
};

// Things the view noticed this frame, for sound and particles: clip events ("step", "hit"),
// "launch" (a projectile left a muzzle), "death", "spawn". Positions in world space.
struct ViewEvent {
    std::string name;
    Entity entity;
    uint16_t type = 0;
    uint8_t owner = 0;
    render::float3 pos;
};
// Effect anchors active this frame ("fx" in view blocks: smoke on a damaged building, exhaust
// on a moving tank). A particle system spawns at `pos` while the anchor stays listed.
struct ViewEffect {
    std::string effect;
    Entity entity;
    uint16_t type = 0;
    uint8_t owner = 0;
    render::float3 pos;
    float intensity = 1;
};

}  // namespace aiverse::game
namespace aiverse::roads { class RoadView; }
namespace aiverse::game {
class GameShoreView;

class GameView : public view_adapter::ModelResourceProvider {
    friend class ::kingston::TowHost;
    friend class ::kingston::BulletFxHost;
    friend struct ::kingston::CabinResidentDraw;
public:
    // Owner-thread only, outside prepared animation transactions/draw. Copies
    // diagnostic values; never returns an instance, graph or model pointer.
    // The caller must establish the current world/entity generation.
    Json sharedAnimationGraphDiagnostic(uint64_t entityKey,uint64_t worldEpoch,graph_telemetry::History* history=nullptr) const;
    Json sharedAnimationProfile(uint64_t entityKey,uint64_t worldEpoch,const std::string& action);
    void clearSharedAnimationProfile();
    Json animationGraphDiagnostic(uint64_t entityKey) const {
        Json out=Json::object();
        out.set("entity",Json::string(std::to_string(entityKey)));
        out.set("viewFrame",Json::string(std::to_string(frame_)));
        const auto it=anims_.find(entityKey);
        if(it==anims_.end()||!it->second.initialized||it->second.dead||
           it->second.genericViewPose||it->second.seen!=frame_||
           !it->second.last.entity.valid()||it->second.last.entity.packed()!=entityKey||
           !it->second.graph.graph()){
            out.set("available",Json::fromBool(false));
            out.set("reason",Json::string("No current legacy view graph instance for this entity; culled/generic/uninitialized instances are not reported."));
            return out;
        }
        const auto& instance=it->second.graph;
        const auto* graph=instance.graph();
        out.set("available",Json::fromBool(true));
        out.set("state",Json::string(instance.state()));
        out.set("stateTime",Json::number(double(instance.stateTime()),6));
        out.set("healthy",Json::fromBool(instance.healthy()));
        out.set("errorMessage",Json::string(instance.error()));
        out.set("poseValid",Json::fromBool(it->second.poseValid));
        out.set("typeIndex",Json::number(int64_t(it->second.type)));
        Json active=Json::array();
        for(const auto& state:graph->states())if(state.id==instance.state()){
            for(int index:state.program)active.push(Json::string(graph->nodes()[size_t(index)].id));
            break;
        }
        out.set("activeProgram",active);
        Json events=Json::array();
        const auto& notifies=instance.notifies();
        for(size_t i=0;i<notifies.size()&&i<32;++i){
            Json n=Json::object();n.set("node",Json::string(notifies[i].node));n.set("name",Json::string(notifies[i].name));events.push(n);
        }
        out.set("notifyCount",Json::number(int64_t(notifies.size())));out.set("notifies",events);
        out.set("note",Json::string("Last committed view state; animation LOD may defer evaluation. Active program is not a per-node execution trace."));
        return out;
    }
    GameView();
    ~GameView();
    GameView(const GameView&) = delete;
    GameView& operator=(const GameView&) = delete;
    // reader: content files (models, textures); default platform::readContentFile.
    bool init(gfx::Device& dev, const Rules& rules, render::FileReader reader = {});
    // Editor also opts in to deferred assets: actual map objects and previews
    // load their own types on demand, instead of allocating the entire catalog.
    void setDeferredLoading(bool on) { deferredLoading_ = on; }
    // Independent from type residency: the editor requests full appearance
    // variants for any type it opens, while clients may stream alternatives.
    void setDeferAppearanceVariants(bool on) { deferAppearanceVariants_ = on; }
    // Safe migration switch for legacy authored whole-model LODs without
    // a validated compiled bone map. Preserve historical visual behavior
    // until native GPU parity is established, or opt into bind-pose fallback
    // immediately. This affects only RTS adapter presentation, not sim ECS.
    enum class LegacyLodFallbackMode : uint8_t {
        PreserveWorldMatrices, BindPose
    };
    void setLegacyLodFallbackMode(LegacyLodFallbackMode mode) noexcept {
        legacyLodFallbackMode_=mode;
    }
    LegacyLodFallbackMode legacyLodFallbackMode() const noexcept {
        return legacyLodFallbackMode_;
    }
    struct LegacyLodFallbackStats {
        uint64_t worldMatrixFallbacks=0;
        uint64_t bindPoseFallbacks=0;
        uint64_t rejectedCompiledPlans=0;
    };
    LegacyLodFallbackStats legacyLodFallbackStats() const noexcept {
        return legacyLodFallbackStats_;
    }
    // Legacy RTS renderer holds one cache read scope during draw().
    // The counter is normally zero after the draw has returned;
    // use for host frame-lifetime and teardown diagnostics only.
    size_t activeModelReadLeases() const noexcept {
        return resources_.activeModelReaders();
    }
    render::ModelResourceCache::ModelReadSafetyStats modelReadSafetyStats() const noexcept {
        return resources_.modelReadSafetyStats();
    }
    // Budget only retained transient Pose/matrix capacity in the generic
    // MotionWeave View. The largest single actor still renders normally.
    void setGenericRetargetScratchBudgetBytes(size_t bytes);
    size_t genericRetargetScratchBudgetBytes() const noexcept {
        return genericRetargetScratchBudgetBytes_;
    }
    size_t genericRetargetScratchCapacityBytes() const noexcept;
    struct AssetResidencyStats {
        size_t loadedTypes = 0, cachedModels = 0, gpuTextures = 0, decodedCpuBytes = 0;
        size_t retiredModels=0,retirementBatches=0,retirementPeak=0;
        size_t retirementSoftLimit=0;
        uint64_t estimatedRetiredGpuBytes=0,estimatedRetirementByteLimit=0;
        uint64_t residentModelKnownBytes=0,sharedTextureRgba8Equivalent=0;
        size_t uniqueSharedTextures=0,compressedSharedTextures=0;
        size_t sharedGeometryModels=0;
        uint64_t retirementTokenFailures=0,retirementFallbackFailures=0;
        uint64_t retirementFailedFenceBatches=0;
        size_t unresolvedFailedFences=0;
        size_t pendingRolledBackImages=0;
        uint64_t imageRollbackEnqueueFailures=0;
        uint64_t imageRollbackOrphanBatches=0;
        uint64_t stagingQueueFailures=0,stagingOrphanModels=0;
        bool publicationBackpressured=false;
    };
    // Excludes the terrain, special renderers, FX, dedicated buffers and
    // textures not owned by ModelTextureCache.
    AssetResidencyStats assetResidencyStats() const {
        const auto cached=resources_.stats();
        AssetResidencyStats stats;
        for(bool loaded:loadedTypes_)if(loaded)++stats.loadedTypes;
        stats.cachedModels=cached.models;
        stats.gpuTextures=cached.textures;
        stats.decodedCpuBytes=cached.decodedCpuBytes;
        const auto retired=resources_.retirementStats();
        stats.retiredModels=retired.pendingResources;
        stats.retirementBatches=retired.pendingBatches;
        stats.retirementPeak=retired.highWatermark;
        stats.retirementSoftLimit=resources_.retirementSoftLimit();
        stats.estimatedRetiredGpuBytes=resources_.estimatedRetiredGpuBytes();
        stats.estimatedRetirementByteLimit=
            resources_.estimatedRetirementByteLimit();
        const auto owner=resources_.gpuOwnerEstimate();
        stats.residentModelKnownBytes=owner.residentModelDedicatedEstimate;
        stats.sharedTextureRgba8Equivalent=
            owner.sharedTextureRgba8Equivalent;
        stats.uniqueSharedTextures=owner.uniqueSharedTextures;
        stats.compressedSharedTextures=owner.compressedSharedTextures;
        stats.sharedGeometryModels=owner.sharedGeometryResidentModels;
        stats.retirementTokenFailures=retired.tokenFailures;
        stats.retirementFallbackFailures=retired.idleFallbackFailures;
        stats.retirementFailedFenceBatches=retired.failedFenceBatches;
        stats.unresolvedFailedFences=retired.unresolvedFailedFences;
        stats.pendingRolledBackImages=imageRollback_.pendingImages();
        stats.imageRollbackEnqueueFailures=imageRollback_.enqueueFailures();
        stats.imageRollbackOrphanBatches=imageRollback_.unrecoverableBatches();
        stats.stagingQueueFailures=resources_.stagingQueueFailures();
        stats.stagingOrphanModels=resources_.stagingOrphanModels();
        stats.publicationBackpressured=resources_.publicationBackpressured();
        return stats;
    }
    // Enumerates actual GPU assets owned by this presentation view, not
    // temporary draw references. Pooled geometry pages are counted by
    // GlobalGeometryPool, and cache-borrowed images by ModelTextureCache.
    // Graphics-owner thread only, since GPU caches are mutated there.
    render::GpuOwnedMemoryStats ownedModelGpuMemoryStats(
            const gfx::Device& device) const {
        render::GpuOwnedMemoryCounter counter(device);
        // This cache owns both resident and GPU-retired Models. Borrowed
        // page/image handles are excluded by appendModelOwnedGpuMemory.
        resources_.forEachOwnedModel([&](const render::Model& model,bool) {
            render::appendModelOwnedGpuMemory(model,counter);
        });
        // Account for the shared texture cache once, not for every
        // Model/Image that borrows one of its resident handles.
        if (const auto textures=resources_.textures())
            textures->appendOwnedGpuMemory(counter);
        return counter.stats();
    }
    // Asset-owner view includes model caches, terrain chunk/detail
    // renderer, and planet renderer (all separate owners). An additive
    // lower bound, not physical device VRAM. Draw-item borrowings excluded.
    render::GpuOwnedMemoryStats ownedPresentationGpuMemoryStats(
            const gfx::Device& device) const;
    // An optional host-owned device generation protects cached models even
    // when a gfx::Device address is recycled. The lease never owns the Device.
    // Supply it before init; hosted clients share the editor's generation.
    void setGpuDeviceLease(render::RenderDeviceLifetime::Lease lease) {
        gpuDeviceLease_=std::move(lease);
        terrain_.setGpuDeviceLease(gpuDeviceLease_);
        groundFx_.setGpuDeviceLease(gpuDeviceLease_);
        paintLayers_.setGpuDeviceLease(gpuDeviceLease_);
    }
    bool typeLoaded(uint16_t type) const { return type < loadedTypes_.size() && loadedTypes_[type]; }
    void loadType(uint16_t type);
    void trimTypes(const std::vector<bool>& keep, bool compact = false);
    // Render-thread hot reload for changed model SOURCE files. Detach only
    // affected looks/graph players before evicting their cached GPU models.
    // Call between submitted frames, never during presentation worker dispatch.
    // Returns the number of cache keys invalidated (including failed loads).
    size_t reloadModelSources(const std::vector<std::string>& sources);
    // Rebuild appearances for affected authored types without evicting or
    // re-uploading their Model resources. Graph and material changes are
    // staged first; on compile/parse failure existing animations stay alive.
    // Returns true for an accepted transaction (including zero resident types).
    using AppearancePipelineValidator=std::function<
        render::Renderer3D::PipelineWarmStatus(
            const render::Model&,const std::vector<render::Material>&)>;
    // The host owns GPU shader caches. Stage all candidate appearance
    // pipelines before committing new materials or Models to the live View.
    void setAppearancePipelineValidator(AppearancePipelineValidator validator) {
        appearancePipelineValidator_=std::move(validator);
    }
    bool gpuAppearancePending() const noexcept {return gpuAppearancePending_;}
    bool reloadAppearanceTypes(const std::set<size_t>& types,
                               bool reloadMaterialLibrary=false,
                               size_t* refreshed=nullptr);
    // Thin compatibility adapter: the shared engine appearance transaction
    // owns publication; this adapter only resolves an already resident choice.
    struct GraphMaterialTargetInfo {
        view::LiveAppearanceMaterialTarget identity;
        std::string modelPath,materialName;
        int variant=-1;
        size_t proceduralLive=0,proceduralTombstones=0,pendingImageRetirement=0;
    };
    std::optional<GraphMaterialTargetInfo> graphMaterialTarget(size_t type,size_t choice,size_t material) const;
    using GraphMaterialApply=std::function<bool(view::AppearanceAssetRegistry::Snapshot&,
        std::vector<render::Material>&,render::Model&,gfx::Device&,
        const render::RenderDeviceLifetime::Lease&,render::ModelImageRollbackPool&,std::string*)>;
    bool applyGraphMaterial(size_t type,size_t choice,const view::LiveAppearanceMaterialTarget& target,
        const GraphMaterialApply& apply,std::string* error=nullptr);
    // Thin shared adapter for native hosts and editor, never owns GPU work.
    bool applyCompletedGraphMaterial(size_t type,size_t choice,const view::LiveAppearanceMaterialTarget& target,
        render::texture_graph::TextureGraphGpuOwner& owner,const std::string& scope,const std::string& request,
        std::string* error=nullptr);
    // Explicit external root proof required: the adapter only enumerates its
    // own current choices, not renderer caches, external pins or packets.
    render::texture_graph::GraphImageReclaimResult collectGraphMaterialImages(size_t type,size_t choice,size_t material,
        const view::LiveAppearanceMaterialTarget& target,const render::texture_graph::GraphImageRoots& external);
    bool typeCurrent(uint16_t t) const { return typeLoaded(t) && requestedModels_[t]==loadedModels_[t]; }
    bool detailRequested(uint16_t t) const { return t<requestedModels_.size() && requestedModels_[t].empty(); }
    void requestDetail(uint16_t type, bool detailed);
    bool warmPipelines(render::Renderer3D& renderer, uint16_t type);
    void setStreamingTypes(bool enabled) { streamingTypes_ = enabled; }
    std::shared_ptr<render::ModelTextureCache> textureCache() const { return resources_.textures(); }
    // Transitional native-host access to the independent resource owner.
    // Borrowed Model* and materials must not outlive trim/shutdown/hot reload.
    const view_adapter::GameModelResourceCache& modelResources() const noexcept {
        return resources_;
    }
    // The editor may subscribe to cache GPU-retirement diagnostics without
    // gaining mutable access to the model owner or its resident assets.
    // The underlying cache enforces owner-thread and detach-callback guards.
    // Host frame-boundary retirement: call before beginning render submissions
    // for this frame. No draw happens here; previous GPU work must complete.
    // Includes road/shore Models AND SpriteMesh buffers so existing client/
    // editor frame hooks automatically poll every View-owned resource queue.
    size_t pendingModelRetirements() const;
    bool collectRetiredModels();
    size_t pendingRolledBackImages() const noexcept {
        return imageRollback_.pendingImages();
    }
    // Generic canvas GPU buffers/images/programs retire at host frame boundaries
    // alongside model/renderer/PostFx queues, before new render passes.
    size_t pendingPaintGpuRetirements() const noexcept {
        return paintLayers_.pendingGpuRetirements();
    }
    bool collectRetiredPaintGpuResources() {
        return paintLayers_.collectRetiredGpuResources();
    }
    uint64_t paintGpuRetirementOrphans() const noexcept {
        return paintLayers_.gpuRetirementOrphans();
    }
    render::CanvasGpuRetirement::Stats paintGpuRetirementStats() const noexcept {
        return paintLayers_.gpuRetirementStats();
    }
    uint64_t paintGpuRetirementEnqueueFailures() const noexcept {
        return paintLayers_.gpuRetirementEnqueueFailures();
    }
    render::RenderTexture::SubmissionStats paintGpuCanvasSubmitStats() const noexcept {
        return paintLayers_.canvasSubmitStats();
    }
    void setModelRetirementDiagnosticObserver(
            render::ModelResourceCache::RetirementDiagnosticObserver observer) {
        resources_.setRetirementDiagnosticObserver(std::move(observer));
    }
    void setImageRetirementDiagnosticObserver(
            render::ModelImageRollbackPool::DiagnosticObserver observer) {
        imageRollback_.setRetirementDiagnosticObserver(std::move(observer));
    }
    // An independent, worker-safe reader enables CPU decoding off the render thread.
    void setAsyncReader(render::FileReader reader) { asyncReader_ = std::move(reader); }
    bool prepareType(uint16_t type, bool allVariants = false);
    void streamModelVariants(double budgetMs = 2);
    size_t pendingModelVariants() const;
    size_t modelChoiceCount(uint16_t type) const { return type < looks_.size() ? looks_[type].models.size() : 0; }
    float typePreparationProgress() const {
#ifndef __EMSCRIPTEN__
        return resources_.preparation().progress();
#else
        return 0.0f;
#endif
    }
    const std::string& preparingResource() const {
#ifndef __EMSCRIPTEN__
        return resources_.preparation().resource();
#else
        static const std::string empty;
        return empty;
#endif
    }
    std::string typeResource(uint16_t type) const;
    void shutdown(gfx::Device& dev);
    // Terrain from the grid (chunked: only chunks whose cells changed are rebuilt); call again after
    // the editor changes the grid. Resource fields / creep changing in a match need no call (the
    // terrain shader reads them from per-cell textures).
    void buildTerrain(gfx::Device& dev, const MapGrid& grid);
    // Opt-in fully staged editor sculpt/detail update. Unlike the normal
    // sparse buildTerrain path this regenerates the full terrain candidate,
    // road/shore height samples, GroundFX grid geometry state and Fog Sheet
    // before a single five-owner commit. Existing Fog R8/visibility history
    // survives same-size edits. Higher temporary GPU memory; use only for
    // explicit transactional edits, not every brush sample.
    void buildTerrainAtomic(gfx::Device& dev,const MapGrid& grid);
    // Opt-in scene-aware map transaction. Requires a configured primary
    // Renderer3D camera/scene and a previously primed Terrain GPU kit.
    // Builds the visible GroundFX chunks against the UNPUBLISHED Terrain,
    // bounded by the existing 16-chunk/128 MiB GPU preparation budget.
    // Does not publish a camera FramePublicationEpoch; the next draw does.
    void buildTerrainAtomicScene(gfx::Device& dev,const MapGrid& grid,
        render::Renderer3D& renderer,const GameScene& scene,double time,
        const std::vector<uint8_t>* vision);
    // Stage a one-shot unpublished Terrain+Roads+Shores+GroundFX+Fog map
    // and camera-visible GroundFX mesh bundle WITHOUT publishing any owner.
    // Must follow renderer.beginScene(); returns false when initial Terrain
    // kit, planet mode, hierarchical scene or 2D tiles forbid this path.
    // A matching next primary draw commits once before it evaluates poses;
    // a stale lease is discarded and rebuilt from the current source.
    bool prepareTerrainAtomicFrame(gfx::Device& dev,const MapGrid& grid,
        render::Renderer3D& renderer,const GameScene& scene,double time,
        const std::vector<uint8_t>* vision);
    // Optional painted material layer per cell (map key "terrainMaterial": rows of digits 0..3, '.' =
    // by cell kind; see docs/game-format.md). Takes effect at the next buildTerrain().
    void setTerrainMaterial(const Json& rows, int w, int h) {
        framePublication_.invalidate();
        terrain_.setMaterialLayer(parseMaterialRows(rows, w, h));
    }
    // Visual detail heightmap of the map (terrain.detail, docs/terrain.md; files read with the view's
    // reader). Without one, terrain.style "smooth" synthesises the detail. Takes effect at buildTerrain().
    void setMapSurfaceEnabled(bool enabled){terrain_.setMapSurfaceEnabled(enabled);}
    void setTerrainDetail(const Json& map);
    void setTerrainDetail(const terrain::DetailSurface* d, const std::vector<uint8_t>* splat) {
        framePublication_.invalidate();
        terrain_.setDetail(d, splat);
    }
    const TerrainRenderer& terrainRenderer() const { return terrain_; }
    // Water drawn by NEREID from the map cells this frame (0 = legacy or none).
    const world::NereidWaterScene& nereidMapWater() const { return nereidLegacy_; }
    // After a synchronous map-size transition, all resource owners already
    // agree on dimensions. GroundFX mesh fills still await primary draw.
    bool surfacePresentationMapReady(const MapGrid& grid) const noexcept {
        return terrain_.mapMatches(grid.w,grid.h) &&
               groundFx_.mapMatches(grid.w,grid.h) &&
               fog_.sheetMatches(grid.w,grid.h);
    }
    // Optional one-shot render-owner prewarm for a built map. A successful
    // call keeps all five owners' GPU/CPU candidates UNPUBLISHED in a lease.
    // The next matching primary draw consumes and publishes them exactly once,
    // with no second GPU preparation. Any changed map, scene, camera, viewer,
    // visibility, owner revision or time drops the lease and re-prepares.
    // Neither prewarm nor its lease makes a secondary camera epoch ready.
    void setTerrainTopologyPreparationBudgetBytes(size_t bytes) {
        terrain_.setTopologyPreparationBudgetBytes(bytes);
    }
    size_t terrainTopologyPreparationBytes() const noexcept {
        return terrain_.topologyPreparationBytes();
    }
    bool prepareMapFrame(render::Renderer3D& renderer,const GameScene& scene,
                         double time,const std::vector<uint8_t>* vision,
                         const MapGrid& grid);
    struct SurfacePrewarmStats {
        uint64_t staged=0,consumed=0,discarded=0;
        uint64_t sourceSnapshots=0; // full dependency snapshots retained across calls
    };
    SurfacePrewarmStats surfacePrewarmStats() const noexcept {
        return surfacePrewarmStats_;
    }
    void draw(render::Renderer3D& r, const GameScene& s, double time, const std::vector<uint8_t>* vision, const MapGrid& grid);
    // The generic frame fence is committed ONLY after all primary-frame
    // owners finish. Secondary RT cameras must consume exactly this epoch.
    render::FramePublicationEpoch::Snapshot publishedVisualFrame() const noexcept {
        return framePublication_.published();
    }
    bool visualFrameReadyFor(const MapGrid& grid,double seconds) const noexcept {
        return framePublication_.readyFor(grid.w,grid.h,seconds);
    }
    // Secondary camera: emits prepared terrain and fog without updating
    // simulation, animation clocks, field textures or fog transitions.
    // Requires a primary draw() in the same presentation frame.
    void drawPreparedTerrainFog(render::Renderer3D& renderer,
                                const MapGrid& grid,
                                const std::vector<uint8_t>* vision,
                                double seconds,uint64_t expectedEpoch=0);
    // Same-frame sprite/tile draw-only pass, without sprite state changes.
    void drawPreparedSprites(render::Renderer3D& renderer,uint64_t expectedEpoch=0);
    // Optional LENS one-frame capture of pre-LOD MotionWeave model packets.
    // Owner must disable this before resources can be reimported or destroyed.
    void setCameraPreparedCapture(render::CameraModelSnapshot* target) noexcept {
        cameraPreparedCapture_=target;
    }
    // spline roads (map "roadNetwork", docs/roads.md): set with the map (setTerrainDetail(map) does it too)
    void setRoadMap(const Json& map);
    roads::RoadView& roads() { return *roads_; }
    const roads::RoadView& roads() const { return *roads_; }
    GameShoreView& shores() { return *shores_; }
    const GameShoreView& shores() const { return *shores_; }
    // ImGui: bars, counts. ox/oy: top-left of the view on screen; drawList: ImDrawList* (default: background list)
    void drawOverlays(const render::Camera& cam, const GameScene& s, float viewW, float viewH, float ox = 0, float oy = 0, void* drawList = nullptr);
    // view.draw triangles of the view scripts (ViewCanvas.h), after drawOverlays
    void drawCanvas(const std::vector<ViewDrawBatch>& batches, float ox = 0, float oy = 0, void* drawList = nullptr) { overlays_.drawCanvas(batches, ox, oy, drawList); }
    // view.draw_ground triangles of the view scripts (GameGroundCanvas.h): call between draw() and Renderer3D::render
    // Also draws the types' "groundShadow" images collected by the last draw() (layer -1).
    void drawGround(render::Renderer3D& r, const std::vector<ViewGroundBatch>& batches);
    const std::vector<ViewGroundBatch>& groundShadows() const { return shadowDraws_; }
    // Building placement preview: the type's model, green when `ok`, red otherwise.
    void drawGhost(render::Renderer3D& r, uint16_t type, render::float3 pos, bool ok, const MapGrid& grid, double time);
    // One object at the origin playing its idle clip (portraits, editor previews).
    void drawPreview(render::Renderer3D& r, uint16_t type, uint8_t owner, double time, float yaw = 0.6f);
    // Borrowed model/resource interface for independent View consumers.
    bool resolveStatic(uint16_t type,uint8_t owner,
                       view_adapter::StaticPreviewModel& output) override;
    static render::float4 playerColor(int player);
    float selectionRadius(uint16_t type) const;
    float heightAt(const MapGrid& grid, float x, float z) const;
    // What feet and wheels stand on: the drawn spline-road surface where there is one (asphalt over the ground,
    // kerbs, raised sidewalks, junctions), otherwise the ground (heightAt). Moving units are drawn at it; a game that
    // draws its own vehicles samples it under each wheel (docs/roads.md § Surface for wheels).
    float groundAt(const MapGrid& grid, float x, float z) const;
    // Where a drawn unit's shots leave its model: the look's "muzzle" (node + offset) in world space and the unit's
    // facing; false when the unit is not drawn yet or its model has no muzzle (effects then use the simulation point).
    bool muzzleOf(Entity e, render::float3& pos, render::float3& dir) const;
    // Problems found while loading view blocks ("type 'knight': models/knight.glb: file not found").
    const std::vector<std::string>& warnings() const { return warnings_; }
    // Read-only static geometry scene (HLOD / opt-in STRATA counters).
    const render::GeometryScene& geometryScene() const { return geometryScene_; }
    // Geometry-only test view (AIVERSE_GEOMETRY_ONLY): draw the static
    // geometry scene and nothing of the map surface (terrain, water, roads,
    // ground effects). Used for clean STRATA/LOD geometry benchmarks.
    void setGeometryOnly(bool on) { geometryOnly_ = on; }
    // Corner panel for STRATA benchmarks/screenshots: route (STRATA/LOD),
    // cooked or built, debug view mode, this frame's triangles, draw calls
    // and frame time. Drawn when AIVERSE_GEOMETRY_ONLY or AIVERSE_STRATA_DEBUG is set.
    void drawStrataHud(float viewW, float viewH);
    // AIVERSE_STRATA_DEBUG default presentation: picture-in-picture. The host
    // renders drawGeometryInset() into its own target and passes the texture.
    bool strataDebugInset() const { return strataDebugInset_; }
    void drawGeometryInset(render::Renderer3D& r);
    void setStrataInsetTexture(uint32_t textureId, bool flipY) { insetTexture_ = textureId; insetFlipY_ = flipY; }
    // Host reports the renderer's numbers AFTER Renderer3D::render (the
    // profile is filled during render, not during GameView::draw).
    void noteRenderedFrame(uint64_t cameraTriangles, uint64_t shadowTriangles, uint64_t drawCalls) {
        renderedTris_ = double(cameraTriangles); renderedShadowTris_ = double(shadowTriangles); renderedDraws_ = double(drawCalls);
    }
    bool geometryOnly() const { return geometryOnly_; }
    const render::Model* modelOf(uint16_t type, size_t choice = 0) const;
    const std::vector<ViewEvent>& events() const { return events_; }
    const std::vector<ViewEffect>& effects() const { return effects_; }
    void setSelectionOutline(bool on) { outline_ = on; }
    void setRetainPickingGeometry(bool on){resources_.setRetainPickingGeometry(on);}
    // Register immutable native pose operations before init().
    void setAnimationOperations(render::AnimationOperations operations) { animationOperations_ = std::move(operations); }
    // Optional host physics queries override the default terrain / visible AABB adapter.
    void setAnimationEnvironment(std::function<void(Entity,render::AnimationEnvironment&)> query) { animationEnvironmentQuery_=std::move(query); }
    void setAnimationTraits(render::AnimationTraits traits) { animationTraits_=std::move(traits); }
    void setAnimationLod(bool on) { animLod_ = on; }
    // Temporary compatibility: the new aiverse::View takes ownership of
    // simple Graph poses, while legacy specialized authoring stays in this
    // game adapter. Once all specialized presentation is extracted, this
    // wrapper can be renamed/removed without changing the generic View.
    void setGenericViewEnabled(bool on) { genericViewEnabled_=on; }
    // Model files referenced by the view blocks (editor hot reload watches them).
    std::vector<std::string> modelFiles() const;

    enum AnimKind { kAnimIdle, kAnimMove, kAnimAttack, kAnimGather, kAnimBuild, kAnimCarry, kAnimDie, kAnimSpawn,
                    kAnimHit, kAnimWork, kAnimConstruct, kAnimCast, kAnimCount };
    static const char* animName(int kind);

    // ---- 2D games (Rts2DView): sprite types and tile maps
    // Prefix for content paths (editor: "<project dir>/"); call before init().
    void setContentBase(const std::string& base) { contentBase_ = base; }
    // The map's "tiles" block (null = none); takes effect at the next buildTerrain().
    void setTiles(const Json& tiles) { if (twoD_) twoD_->setTiles(tiles); }
    bool hasTiles() const { return twoD_ && twoD_->hasTiles(); }
    // Screen-space hit test for sprite objects (they are drawn above their ground point).
    // drawn as a 2D sprite (grounded by Rts2DView's own projected blobs / sun-shadow casters)
    bool isSprite(uint16_t type) const { return twoD_ && twoD_->hasSprite(type); }
    bool spriteHit(const render::Camera& cam, const SceneObject& o, const MapGrid& grid, float sx, float sy, float w, float h) const;
    GameLights& lights() { return lights_; }
    const FogLook& fogLook() const { return fog_.look(); }

    // ---- planets (GamePlanetView.cpp): maps with a `world` block (docs/planets.md)
    // Reads map["world"]; no block = the flat view, unchanged. Call before buildTerrain().
    void setWorld(const Json& map);
    // Editor: the world block changed (sculpt / paint): same bodies -> only changed chunks rebuild and the
    // camera stays; different bodies -> setWorld.
    void updateWorld(const Json& map);
    bool planetMode() const { return planets_.active(); }
    PlanetCamera& planetCamera() { return planetCam_; }
    const PlanetCamera& planetCamera() const { return planetCam_; }
    PlanetRenderer& planets() { return planets_; }
    const PlanetRenderer& planets() const { return planets_; }
    // Per frame before beginScene: orbit camera smoothing; returns the Camera to render with (view world).
    const render::Camera& updatePlanetCamera(double seconds, float dt, int viewH);
    // Screen point -> body + body-local surface point (terrain / ocean picking).
    bool pickPlanet(const render::Camera& cam, float sx, float sy, float w, float h, int& body, render::float3& local) const;
    // Match maps: the simulation's Surface is the source of bodies, orbits, corner heights and cell flags
    // (map JSON only for visual keys). Call after the sim is set up, before buildTerrain().
    void setSimWorld(std::shared_ptr<const Surface> surf, const MapGrid& grid, const Json& map);
    // Sim-space positions (anchor(body) + local, Surface.h) <-> body + body-local position.
    static bool simToBody(render::float3 sim, int& body, render::float3& local);
    static render::float3 bodyToSim(int body, render::float3 local) { return anchorF(body) + local; }
    // Body-local surface point of a face position (cells), e.g. a map "at" {body, face, x, y}.
    bool facePoint(int body, int face, float x, float y, render::float3& local) const;
    // Where the view draws an object (view world), `lift` above its ground point. Flat maps: {x, lift, z}.
    render::float3 objectViewPos(const SceneObject& o, float lift) const;
    render::float3 bodyToView(int body, render::float3 local) const;
    // Body and body-local position of a scene object (false: not on a body, e.g. in transit).
    bool objectOnBody(const SceneObject& o, int& body, render::float3& local) const {
        if (o.body >= 0) { body = o.body; local = o.pos; return true; }
        return false;
    }
    // Fog of war of a body as drawn (6 * N * N, 0 unexplored .. 255 visible); empty = no fog.
    const std::vector<uint8_t>& planetVision(int body) const { static const std::vector<uint8_t> none; return body >= 0 && size_t(body) < planetVision_.size() ? planetVision_[size_t(body)] : none; }
    double planetSeconds() const { return planetSeconds_; }
    // Particles on planets live in the body-local space of the body the camera looks at (moves with it):
    // the body and its body-local -> view-world matrix this frame (after updatePlanetCamera).
    int planetFxBody() const { return planetCam_.body(); }
    render::mat4 planetFxSpace() const;
    // particle placement: sim position -> body-local of the camera body, `lift` above the ground (lift < -1e8:
    // keep the position as it is, e.g. a flyer); false on other bodies. Ground altitude of a body-local point.
    bool planetFxPlace(render::float3 sim, float lift, render::float3& out, render::float3& up) const;
    float planetFxAltitude(render::float3 local) const;
    // Object frame in the view world (columns: east, up, south, ground point); false when not on a body.
    bool planetPlacement(const SceneObject& o, render::mat4& out) const;
    void setFogLook(const FogLook& l) {
        framePublication_.invalidate();
        fog_.setLook(l);
        fogMeshDirty_=true;
    }
    // Overlays (GameOverlays): selection rings, bars, markers and every game-defined helper element.
    // The client / editor pass the local input state once per frame, before draw().
    void setOverlayContext(const OverlayContext& c) { overlays_.setContext(c); }
    GameOverlays& overlays() { return overlays_; }
    GameFieldView& fieldView() { return fieldView_; }   // grid field overlays; takeEffects() = particles for the host
    const GameOverlays& overlays() const { return overlays_; }
    // Screen point above an object where its bars / labels go (false: behind the camera).
    bool overlayTop(const render::Camera& cam, const SceneObject& o, size_t index, float w, float h, render::float2& out) const;

private:
    render::AnimationOperations animationOperations_;
    render::AnimationTraits animationTraits_;
    struct AnimationCached { std::shared_ptr<const render::AnimationGraph> graph; std::string error; };
    std::map<const render::Model*, std::map<std::shared_ptr<const AnimationDefinition>, AnimationCached>> animationCache_;
    struct ClipSpec {
        std::vector<int> clips;    // random pick per play
        float speed = 1, speedRef = 0, fade = 0.2f;
        int layer = 0;             // 0 base, 1 overlay (attack / cast), 2 overlay (hit)
        int mask = -1;             // overlay limited to this node's subtree
        bool loop = true;
        std::vector<std::pair<float, std::string>> events;   // clip time -> event name
        struct BlendPoint { int clip; float speed; };
        std::vector<BlendPoint> blend;                      // blend space by speed (move / carry)
    };
    struct WeaponPose { std::string weapon; int hold=-1,aim=-1,attack=-1; };
    struct Attachment {
        std::string weapon;
        render::Model* model = nullptr;
        std::vector<render::Material> materials;
        int node = -1;                 // socket node of the host model (-1: object origin)
        render::mat4 local;
        uint32_t when = 0;             // ProcState bits, 0 = always
        int carry = -2;                // -2 not cargo, -1 any cargo, >= 0 resource index
    };
    struct Fx { std::string effect; int node = -1; render::float3 offset; uint32_t when = 0; float hpBelow = 2, intensity = 1; };
    struct AdditiveSpec { int clip = -1; uint32_t when = 0; float weight = 1; };
    struct LegIK { int hip = -1, knee = -1, foot = -1; };
    struct ModelChoice {
        std::shared_ptr<const render::AnimationGraph> graph;
        std::string graphSignature; // parsed graph definition incl. external content
        view::AppearanceAssetRegistry::Snapshot appearanceSnapshot;
        render::GeometryPolicy geometry;
        render::Model* model = nullptr;
        std::vector<render::Material> materials;  // per-type copy with overrides applied
        std::vector<int> nodeMaterial;            // "assign": node -> index into materials (-1 = the model's)
        render::mat4 fix;                         // forward axis, fit, offset
        ClipSpec clips[kAnimCount];
        bool hasClip[kAnimCount] = {};
        std::vector<render::ProcAnim> procs;
        bool programmableAnimation = false;
        bool needsPose = false;                   // a CPU pose is needed (palette skinning, node procs, sockets, IK)
        bool baked = false;                       // skinned from the baked clip texture (instanced crowds)
        bool staticPlacementEligible = false;    // immutable type/choice properties, computed by finishChoice
        bool staticAnimateEligible = false;      // additionally excludes authored model clips
        int variant = -1;                         // KHR_materials_variants index
        std::vector<Attachment> attach;
        std::vector<WeaponPose> weaponPoses; int weaponMask=-1;
        std::vector<Fx> fx;
        std::vector<AdditiveSpec> additive;
        std::vector<LegIK> legs;
        int muzzle = -1;                          // projectile launch node
        render::float3 muzzleOffset;
        float lodDistance = 0;                    // LOD: used beyond this camera distance
        std::vector<int> clipMap, nodeMap;        // LOD: base clip / node -> this model's
        std::shared_ptr<const view::ModelPoseRetarget> poseRetarget; // validated once during authoring
        // Reload-safe model publication identity; not gameplay entity life.
        // A new choice/asset publication must receive a new nonzero value.
        uint64_t modelGeneration=0;
        std::shared_ptr<const view::CompiledModelPoseRetarget> compiledPoseRetarget;
    };
    struct Stage { float build = -1, hp = -1; ModelChoice choice; };
    enum class Construction : uint8_t { Grow, Dissolve, Stages };
    struct TypeLook {
        std::vector<size_t> pendingVariants;
        size_t nextVariant = 0;
        std::vector<ModelChoice> models;          // variants (picked per object)
        std::vector<ModelChoice> lods;            // farther LODs of models[0] (increasing distance)
        std::vector<Stage> stages;                // construction / damage replacements
        float scale = 1;
        render::float4 tint{1, 1, 1, 1};          // linear
        bool air = false, flyer = false, structure = false, shape = true;
        float sx = 1, sz = 1;                     // shapes: footprint scaling
        bool tracer = false;                      // optional visual bullets for attack events
        std::vector<float> projectileScale;       // "projectileScale": size of the projectile ball by weapon index (missing: 1; 0: not drawn)
        float projectileScaleDefault = 1;
        float corpse = 2.5f;                      // seconds a dead object stays visible
        float corpseFade = 0.6f;                  // its last seconds fade out (0: drawn whole until removed)
        float corpseAlpha(float age) const { return GameView::corpseAlpha(corpse, corpseFade, age); }
        float turnSpeed = 10;                     // rad/s, 0 = instant
        float flyHeight = 2.4f;
        float unitSpeed = 0;
        Construction construction = Construction::Grow;
        bool dissolveSpawn = false, dissolveDeath = false;
        bool transparent = false;                 // "transparent": Unity transparent queue (sorted, no depth write among its parts)
        // "groundShadow": a flat image under the model (OpenWar's footprint sprites), turning with the drawn model
        struct GroundShadow { std::string image; float w = 0, d = 0, ox = 0, oz = 0, lift = 0.05f; uint32_t rgba = 0x0000008cu; bool flipU = false, flipV = false; } shadow;
    };
    struct EntityAnimState {
        uint16_t type = 0;
        int choice = 0;
        render::Animator anim;
        // One-shot named clock snapshot from model/source reload. Restore
        // only AFTER resolving a possible per-entity programmable override,
        // never prematurely against the TypeLook's default graph.
        std::optional<render::AnimationGraphInstance::PlaybackSnapshot>
            pendingReloadPlayback;
        bool genericViewPose=false,wasGenericViewPose=false;
        uint64_t genericViewId=0;
        render::Pose pose;
        std::vector<render::mat4> world;
        std::vector<render::ProcRuntime> rt;
        render::mat4 objXf;                       // procedural whole-object offset
        float poseLift = 0; bool poseLiftSet = false; // authored Y minus ground at first draw
        render::mat4 drawn;                       // last world transform of the host model
        // Reuse finished non-animated scenery placement without rebuilding
        // translate/rotate/scale matrices. Every varying input is validated
        // before reuse; animated/procedural/FX objects always use legacy code.
        struct PlacementCache {
            bool valid = false;
            render::float3 pos{};
            float ground = 0, yaw = 0, scale = 0, sx = 0, sz = 0;
            float flyHeight = 0;
            bool structure = false, shape = false, air = false;
            const render::Model* model = nullptr;
            render::mat4 fix{};
            render::mat4 world{};
            render::mat4 drawn{};
        } placement;
        // Camera-independent height for stationary objects. Revalidated when
        // terrain or road surface is rebuilt, or when placement/type changes.
        struct GroundCache {
            bool valid = false;
            const MapGrid* grid = nullptr;
            uint64_t terrainGeneration = 0, roadGeneration = 0;
            render::float3 pos{};
            uint16_t type = 0;
            bool roadSurface = false;
            float height = 0;
        } ground;
        float yaw = 0, speed = 0, distance = 0, turnRate = 0, accel = 0, phase = 0;
        float eventAge[render::kProcEvCount] = {1e9f, 1e9f, 1e9f, 1e9f};
        render::float3 attackDirection{};          // last shot in the animation tangent frame
        float castAge = 1e9f;
        render::float3 lastPos;
        bool initialized = false, dead = false;
        float deathAge = 0;
        int baseKind = -1;
        uint64_t seen = 0;
        // Camera culling (GameView::draw): bounds of the last submission, relative
        // to the ground point under the object, and clock time not yet animated.
        bool cullValid = false;
        render::float3 cullOffset{};
        float cullRadius = 0;
        float culledDt = 0;
        SceneObject last;
        int prevClip[render::Animator::kLayers] = {-1, -1, -1};
        float prevTime[render::Animator::kLayers] = {0, 0, 0};
        float lodAccum = 0;                       // animation LOD: time not yet applied to the pose
        bool poseValid = false;
        render::float3 launch;                    // projectiles: offset from the muzzle, decays
        bool launched = false;
        double lastSeenTime = -1;                 // previews keep their own clock
    };

    struct EntityAnim : EntityAnimState {
        render::AnimationGraphInstance graph;
        EntityAnim cloneForPreparation() const {
            EntityAnim next;
            static_cast<EntityAnimState&>(next)=*this;
            next.graph=std::move(*graph.cloneForPreparation());
            return next;
        }
    };
    class PreparedAnimationLease;

    // Short-lived, Model-pointer-free presentation clock handoff while a
    // source/graph/visual LOD reimport destroys and rebuilds a TypeLook.
    // Indexed by full entity generation ID, not genre-specific Model paths.
    // The old Graph/Animator/pose/native solver state never escapes.
    struct ReloadPlayback {
        uint16_t type=0;
        int choice=0;
        uint64_t capturedFrame=0;
        render::AnimationGraphInstance::PlaybackSnapshot graph;
        // Legacy clip-only actors can preserve one unblended base-layer
        // phase by stable clip NAME, never by the previous Model's index.
        std::string baseClip;
        float basePhase=0.f;
        bool baseLoop=true;
        int baseKind=-1;
        float eventAge[render::kProcEvCount] = {};
        float castAge=1e9f,deathAge=0.f,phase=0.f,yaw=0.f,speed=0.f;
        render::float3 lastPos{};
        bool dead=false;
    };
    void rememberReloadPlayback(uint64_t key,const EntityAnim& state) noexcept;

    struct Tracer { SceneObject from, to; float age = 0; };
    std::vector<Tracer> tracers_;
    void drawTracers(render::Renderer3D& r, float dt, const MapGrid* grid = nullptr);
    void buildShapes(gfx::Device& dev);
    void buildLook(gfx::Device& dev, const UnitType& t, TypeLook& out);
public:
    // "groundShadow" of a type's view (empty image = none / invalid); public for tests
    static TypeLook::GroundShadow parseGroundShadow(const Json& v);
    // view "corpse" / "corpseFade": opacity of a corpse `age` seconds after the death
    static float corpseAlpha(float corpse, float corpseFade, float age) {
        return corpseFade > 0 ? std::clamp((corpse - age) / corpseFade, 0.0f, 1.0f) : 1.0f;
    }
private:
    bool loadChoice(gfx::Device& dev, const UnitType& t, const Json& view, const std::string& path, ModelChoice& out);
    void configureChoice(gfx::Device& dev, const UnitType& t, const Json& view, ModelChoice& c, bool isShape);
    // Material library (materials.json): a reference ("steel", {"use": "steel", ...}) flattened into
    // one override object; library entries inherit with "base".
    Json resolveMaterial(const UnitType& t, const Json& ref, int depth = 0);
public:
    // A material reference / override object applied to `mt` (textures load into `m`).
    void applyMaterial(gfx::Device& dev, const UnitType& t, render::Model& m, render::Material& mt, const Json& ref);
    const Json& materialLibrary() const { return materialLib_; }
private:
    void configureExtras(gfx::Device& dev, const UnitType& t, const Json& view, ModelChoice& c);
    render::Model* loadModelCached(gfx::Device& dev, const UnitType& t, const std::string& path);
    int loadTexture(gfx::Device& dev, const UnitType& t, render::Model& m, const std::string& path);
    void parseProcedural(const UnitType& t, const Json& list, ModelChoice& c);
    void defaultProcedural(const UnitType& t, const TypeLook& look, ModelChoice& c);
    void finishChoice(const UnitType& t, ModelChoice& c);
    void warn(const UnitType& t, const std::string& msg);
    void handleEvents(const GameScene& s);
    EntityAnim& stateFor(uint64_t key, const SceneObject& o);
    // viewPos (planets): where the object is drawn (animation LOD); o is then in its body's tangent frame
    void animate(EntityAnim& st, const SceneObject& o, float dt, const MapGrid* grid, const render::float3* viewPos = nullptr);
    void drawPlanets(render::Renderer3D& r, const GameScene& s, double time, const std::vector<uint8_t>* vision, const MapGrid& grid);
    // body-local placement of an object: ground point, unit up, tangent frame (east, south)
    bool placeObject(const SceneObject& o, int& body, render::float3& ground, render::float3& up, render::float3& east, render::float3& south, float& lift) const;
    // placement (planets): object frame in the view world (local +Y = up); null = flat (pos, ground)
    void drawObject(render::Renderer3D& r, EntityAnim& st, const SceneObject& o, float ground, float alpha, const render::float4* tintOverride = nullptr,
                    const render::mat4* placement = nullptr);
    void drawProjectile(render::Renderer3D& r, uint64_t key, const SceneObject& o, float dt);
    void emitClipEvents(EntityAnim& st, const ModelChoice& ch, const SceneObject& o);
    render::float3 nodeWorldPos(const EntityAnim& st, const ModelChoice& ch, int node, render::float3 offset) const;
    uint32_t stateBits(const EntityAnim& st, const SceneObject& o, bool moving) const;

    const Rules* rules_ = nullptr;
    render::FileReader read_;
    view_adapter::GameModelResourceCache resources_;
    Json materialLib_ = Json::object();   // materials.json "materials"
    gfx::Device* dev_ = nullptr;
    render::RenderDeviceLifetime::Lease gpuDeviceLease_;

    render::GeometryScene geometryScene_;
    bool geometryOnly_ = false, strataDebugInset_ = false, insetFlipY_ = false;
    uint32_t insetTexture_ = 0;
    struct StrataHudFrame { double tris = 0, legacyTris = 0, draws = 0, ms = 0; } strataHud_;
    uint64_t strataHudLastTris_ = 0;
    double renderedTris_ = 0, renderedShadowTris_ = 0, renderedDraws_ = 0;
    std::vector<TypeLook> looks_;
    // Immutable per-type reveal policy; avoid string prefix checks on every draw.
    std::vector<uint8_t> actorRevealTypes_;
    std::vector<bool> loadedTypes_;
    std::vector<std::string> requestedModels_, loadedModels_;
    // Reused across draw() calls: O(scene types) work with no per-frame
    // temporary allocation/clear of the full rules type catalog.
    std::vector<uint32_t> drawTypeStamps_;
    std::vector<uint16_t> drawTypes_;
    uint32_t drawTypeEpoch_ = 0;
    bool deferredLoading_ = false, streamingTypes_ = false;
    bool deferAppearanceVariants_ = true; // client default; editor disables
    bool strictAppearanceReload_ = false; // temporary CPU preflight during authored reload
    bool gpuAppearancePending_=false; // retry only unfinished async compiler work
    AppearancePipelineValidator appearancePipelineValidator_;
    render::ModelImageRollbackPool imageRollback_;
    render::ModelImageUploadTransaction* activeImageTransaction_ = nullptr; // borrowed during staging only
    // Unpublished Model owners during appearance reload: no model-cache
    // tombstones or speculative GPU residency until the outer transaction
    // succeeds. The scope clears this borrowed pointer before returning.
    render::ModelResourceCache::ModelMap* activeStagedModels_ = nullptr;
    render::FileReader asyncReader_;
    // Model resources (including async worker lifetime) are owned by
    // resources_; GameView only supplies appearance rules and GPU publication.
    std::vector<std::string> warnings_;
    // Only entities first seen while variants stream need a stable choice override.
    std::unordered_map<uint64_t, std::pair<uint16_t, int>> streamingChoices_;
    std::unordered_map<uint64_t, EntityAnim> anims_;   // presentation only: iteration order never matters
    std::unordered_map<uint64_t, ReloadPlayback> reloadPlayback_;
    EntityAnim ghostAnim_; // transient placement preview state; never shared across Views
    // Reused on the device-owning presentation thread. No global/static
    // scratch shared between Editor Scene, GameClient or native hosts.
    render::Pose lodRetargetPoseScratch_;
    std::vector<render::mat4> lodWorldScratch_;
    LegacyLodFallbackMode legacyLodFallbackMode_=
        LegacyLodFallbackMode::PreserveWorldMatrices;
    LegacyLodFallbackStats legacyLodFallbackStats_;
    size_t genericRetargetScratchBudgetBytes_=512u*1024u;
    std::unique_ptr<::aiverse::view::View> genericView_;
    render::CameraModelSnapshot* cameraPreparedCapture_=nullptr;
    bool genericViewEnabled_=true; // opt-in per-object safety predicate
    render::Mesh projectile_;
    TerrainRenderer terrain_;    // chunked terrain + water (GameTerrainRender.cpp)
    world::NereidWaterScene nereidLegacy_;   // map water cells drawn by NEREID (GameWaterNereid.h)
    std::string nereidLegacySignature_;
    void drawNereidLegacyWater(render::Renderer3D& r,bool terrainReady);
    bool hasTerrain_ = false;
    std::vector<ViewEvent> events_;
    std::vector<ViewEffect> effects_;
    bool outline_ = true, animLod_ = true;
    std::unique_ptr<Rts2DView> twoD_;
    // Failed staging may retain GPU allocations when device idle is refused.
    // Keep it owned until a host-confirmed idle/recovery, never let a local
    // unique_ptr silently drop the recovery path.
    std::unique_ptr<Rts2DView> pendingSpriteRecovery_;
    SpriteDefaults spriteDefaults_;
    float modelLodScale_=1.f, shadowLodScale_=1.f, reflectionCullPixels_=0.f, lodFadeWidth_=-1.f; // game.json "lod"
    std::string contentBase_;
    std::vector<render::float3> bars_;   // per scene object: health bar anchor of sprite objects
    double lastTime_ = -1;
    uint32_t lastTick_ = ~0u;
    uint64_t frame_ = 0;
    render::FramePublicationEpoch framePublication_;
    uint32_t placementCacheHits_ = 0, placementCacheMisses_ = 0;
    uint32_t groundCacheHits_ = 0, groundCacheMisses_ = 0;
    uint64_t terrainGeneration_ = 1;
    uint32_t staticAnimateFastHits_ = 0;
    GameLights lights_;
    GamePaintLayers paintLayers_;
    GameGroundCanvas groundCanvas_;
    std::vector<ViewGroundBatch> shadowDraws_, groundMerged_;   // "groundShadow" quads of this frame (one batch per image)
    void addGroundShadow(const TypeLook& L, const EntityAnim& st, render::float3 pos, float ground, const EntityFxValues* fx = nullptr);
    std::function<void(Entity,render::AnimationEnvironment&)> animationEnvironmentQuery_;
    struct AnimationObstacle { const SceneObject* object=nullptr; Entity entity; render::float3 lo,hi,localMin,localMax; uint64_t surface; render::mat4 transform; };
    std::vector<AnimationObstacle> animationObstacles_;
    std::shared_ptr<std::map<uint64_t,render::mat4>> animationSurfaceFrames_;
    std::shared_ptr<const render::AnimationEnvironment> animationEnvironment(const SceneObject&,const EntityAnim&,const ModelChoice&,const MapGrid&);
    EntityFxStore* fx_ = nullptr;   // this frame's view.entity_fx store (GameScene::entityFx)
    bool fxFor(const SceneObject& o, EntityFxValues& out) const { return fx_ && !fx_->entries.empty() && o.entity.valid() && fx_->lookup(o.entity.packed(), out); }
    // an object that left the scene without a death event (silent removal, or a death inside a frame that ran several
    // ticks) becomes a corpse when its view.entity_fx carries corpse_age
    bool fxCorpse(uint64_t key, EntityAnim& st) const {
        EntityFxValues v;
        if (st.dead || !st.initialized || !fx_ || !fx_->lookup(key, v) || v.v[EntityFxValues::kCorpseAge] < 0) return false;
        st.dead = true; st.deathAge = v.v[EntityFxValues::kCorpseAge];
        return true;
    }
    // a corpse's age this frame: view.entity_fx corpse_age (the script's clock; the pose holds: animDt 0) or + dt
    float corpseAge(uint64_t key, float age, float dt, float& animDt) const {
        EntityFxValues v;
        if (fx_ && fx_->lookup(key, v) && v.v[EntityFxValues::kCorpseAge] >= 0) { animDt = 0; return v.v[EntityFxValues::kCorpseAge]; }
        animDt = dt;
        return age + dt;
    }
    void buildTerrainImpl(gfx::Device& dev,const MapGrid& grid,
                          bool forceAtomic,render::Renderer3D* renderer=nullptr,
                          const GameScene* scene=nullptr,double time=0,
                          const std::vector<uint8_t>* vision=nullptr);
    // Same conservative fingerprint as the scene prewarm lease, shared
    // across C++ translation units to reject mid-staging editor changes.
    static uint64_t mapSourceFingerprint(const MapGrid& grid) noexcept;
    static uint64_t sceneSourceFingerprint(const GameScene& scene) noexcept;
    static uint64_t cameraSourceFingerprint(const render::Renderer3D& renderer);
    static uint64_t visibilitySourceFingerprint(
        const std::vector<uint8_t>* vision) noexcept;
    // RAII candidate bundle is defined in GameView.cpp before the view's
    // out-of-line destructor. It never owns external scene/grid pointers.
    struct PreparedSurfaceLease;
    enum class SurfaceLeaseUse { Immediate, Retained };
    std::unique_ptr<PreparedSurfaceLease> stageSurfaceFrame(
        render::Renderer3D& renderer,const GameScene& scene,double time,
        const std::vector<uint8_t>* vision,const MapGrid& grid,
        SurfaceLeaseUse use=SurfaceLeaseUse::Retained);
    // Commit a matching one-shot prewarm lease, or rebuild on a stale lease.
    // This completes only the surface owner cohort, NOT FramePublicationEpoch.
    bool publishSurfaceFrame(render::Renderer3D& renderer,const GameScene& scene,
                             double time,const std::vector<uint8_t>* vision,
                             const MapGrid& grid);
    std::optional<bool> publishPendingTerrainFrame(render::Renderer3D& renderer,
        const GameScene& scene,double time,
        const std::vector<uint8_t>* vision,const MapGrid& grid);
    // Must be defined out-of-line: the opaque lease owns private GPU handles.
    void discardPendingSurfaceLease() noexcept;
    std::unique_ptr<PreparedSurfaceLease> pendingSurfaceLease_;
    SurfacePrewarmStats surfacePrewarmStats_;
    std::unique_ptr<roads::RoadView> roads_;   // spline roads: map roadNetwork (engine/roads/RoadView.cpp)
    std::unique_ptr<GameShoreView> shores_; // water shores: map waterNetwork (GameShoreView.cpp, docs/water.md)
    // Staged GPU kits may still own RHI handles after a refused full-idle
    // rollback. Do not drop their owner until a later host recovery/shutdown.
    std::unique_ptr<roads::RoadView> pendingRoadRecovery_;
    std::unique_ptr<GameShoreView> pendingShoreRecovery_;
    GameGroundFx groundFx_;   // territory, diggable rock, lanes, roads (GameGroundFx.cpp)
    FogOfWar fog_;           // fog of war look + sheet (GameFog.cpp)
    GameOverlays overlays_;   // data-driven overlays (GameOverlays.cpp)
    GameFieldView fieldView_; // grid field overlays (GameFieldView.cpp)
    const std::vector<GridFieldDef>* fieldDefsSeen_ = nullptr;
    size_t fieldDefsCount_ = 0;
    bool fogMeshDirty_ = false;
    // Strictly transient scope for scene mesh staging; never survives an
    // GameGroundFx::prepareMapSceneFrame call, including its throw path.
    const MapGrid* stagedHeightGrid_ = nullptr;
    const std::function<float(float,float)>* stagedHeightFn_ = nullptr;
    const std::function<float(float,float,float)>* stagedRoadFn_ = nullptr;
    render::mat4 viewProj_;
    render::float3 camPos_;
    // planets
    PlanetRenderer planets_;
    GamePlanetGroundFx planetGroundFx_;   // territory, rock, roads, lanes on sphere faces
    PlanetFlora planetFlora_;            // TerraWeave trees and rocks on planet bodies
    void rebuildPlanetFlora();
    PlanetCamera planetCam_;
    render::SphereViewFrame pframe_;
    double planetSeconds_ = 0;
    std::vector<uint8_t> barOk_;
    std::vector<std::vector<uint8_t>> planetVision_;   // per body
    struct PlanetPlace { int body = -1; render::float3 ground; float lift = 0; };
    std::unordered_map<uint64_t, PlanetPlace> planetLast_;   // corpses: where an object was last drawn
    bool planetNoShadow_ = false;
};

// Build a scene from the live simulation (positions interpolated with alpha). `viewer` filters
// enemies by fog of war (0 = see everything).
void sceneFromSim(Simulation& sim, GameScene& out, float alpha, uint8_t viewer);
// Planet maps (sceneFromSim calls it): objects with body-local positions, units in transit with world positions.
class GameCore;
void sceneFromSphere(Simulation& sim, GameCore& m, const Surface& surf, GameScene& out, float alpha, uint8_t viewer);

} // namespace aiverse::game
