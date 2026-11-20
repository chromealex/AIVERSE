// Forward renderer for RTS scenes: lit meshes with real-time sun shadows, plus immediate-mode
// colored lines/triangles for overlays (selection, roads, fog). Built only on the RHI, so it runs on
// every backend unchanged.
//
// Frame:  beginScene(camera) -> drawMesh()/line()/... -> render(pass)  [shadow pass + main pass;
//         the main pass is left open so the caller can draw UI, then calls device.endPass()]
#pragma once
#include "Geometry.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>
#include "../gfx/Gfx.h"
#include "Lights.h"
#include "Math3D.h"
#include "PrismaMotionTextureInput.h"
#include "Model.h"
#include "ModelStaticEligibility.h"
#include "CpuMdiAdaptivePolicy.h"
#include "AutoCpuBatchPolicy.h"
#include "StaticCrossModelBatchPolicy.h"
#include "PersistentGpuSceneUpload.h"
#include "GpuSceneCulling.h"
#include "GpuSceneDrawCommands.h"
#include <memory>
#include <unordered_map>

namespace aiverse::render {

class GiDynamicProbeTexture;
class GiClipmapValidityTexture;

struct Mesh {
    gfx::BufferHandle vb, ib;
    uint32_t indexCount = 0;
    float3 boundsMin, boundsMax;
};

struct MeshVertex { float3 pos; float3 nrm; uint32_t rgba; };

inline uint32_t rgba(uint8_t r, uint8_t g, uint8_t b, uint8_t a = 255) { return uint32_t(r) | (uint32_t(g) << 8) | (uint32_t(b) << 16) | (uint32_t(a) << 24); }
inline uint32_t rgbaf(float4 c) {
    auto q = [](float v) { return uint8_t(v <= 0 ? 0 : v >= 1 ? 255 : v * 255.0f + 0.5f); };
    return rgba(q(c.x), q(c.y), q(c.z), q(c.w));
}

// CPU-side mesh assembly: primitives with transforms and per-vertex colors, merged into one mesh.
class MeshBuilder {
public:
    MeshBuilder& color(uint32_t c) { color_ = c; return *this; }
    MeshBuilder& transform(const mat4& m) { xf_ = m; return *this; }
    MeshBuilder& box(float3 center, float3 size);
    MeshBuilder& cylinder(float3 base, float radius, float height, int segments = 16, bool caps = true, float topRadius = -1);
    MeshBuilder& cone(float3 base, float radius, float height, int segments = 16);
    MeshBuilder& sphere(float3 center, float radius, int rings = 8, int segments = 14);
    MeshBuilder& disc(float3 center, float radius, int segments = 24); // facing +Y
    MeshBuilder& quad(float3 a, float3 b, float3 c, float3 d);          // CCW seen from the front
    // Large meshes (terrain) use 32-bit indices automatically.
    Mesh upload(gfx::Device& dev) const;
    bool empty() const { return idx_.empty(); }
    size_t vertexCount() const { return verts_.size(); }
    void clear() { verts_.clear(); idx_.clear(); }
    const std::vector<MeshVertex>& vertices() const { return verts_; }
    const std::vector<uint32_t>& indices() const { return idx_; }

    // raw access for terrain-like meshes
    uint32_t addVertex(float3 p, float3 n, uint32_t c) { verts_.push_back({p, n, c}); return uint32_t(verts_.size() - 1); }
    void addTriangle(uint32_t a, uint32_t b, uint32_t c) { idx_.push_back(a); idx_.push_back(b); idx_.push_back(c); }

private:
    uint32_t add(float3 p, float3 n);
    void face(uint32_t a, uint32_t b, uint32_t c, float3 n);
    std::vector<MeshVertex> verts_;
    std::vector<uint32_t> idx_;
    uint32_t color_ = 0xffffffffu;
    mat4 xf_;
};

struct ColorVertex { float3 pos; uint32_t rgba; };

struct Lighting {
    float3 sunDir{0.55f, 1.0f, 0.35f};    // towards the sun
    float sunIntensity = 1.05f;
    float3 sunColor{1.0f, 0.93f, 0.80f};
    float3 skyColor{0.50f, 0.58f, 0.70f};
    float3 groundColor{0.33f, 0.29f, 0.23f};
    float3 fogColor{0.62f, 0.70f, 0.80f};
    float fogDensity = 0.0025f;
    float shadowSoftness = 0.22f;          // maximum penumbra radius in world units; medium/high harden at contact
    bool shadowContactHardening = true;    // medium/high: sharper contacts; false keeps a fixed radius
    bool tonemap = true;                   // false: lit shaders clamp instead of the ACES fit (flat / unlit art styles)
    bool srgbModels = false;               // true: model shaders encode their output with the exact sRGB curve (as a
                                           // linear-colour-space Unity project) instead of the engine's sqrt (gamma 2)
    // traced terrain shadows (trace tier): how far the sun ray is marched over the height field and
    // how fast the penumbra widens with the distance to the occluder (tangent of the half angle)
    float terrainShadowDistance = 96.0f;
    float terrainShadowSoftness = 0.05f;
};

// Height field of the drawn terrain for traced shadows (Renderer3D::setHeightField): a 16-bit
// height map (RGBA8: R high byte, G low byte) over the rectangle [origin, origin + extent] in world xz.
struct HeightField {
    gfx::TextureHandle tex;
    float originX = 0, originZ = 0, extentX = 1, extentZ = 1;
    float heightMin = 0, heightRange = 0;   // range <= 0: nothing bound
};

enum class ShadowQuality { Off, Low, Medium, High };
// Model materials: Low = Blinn-Phong with material factors and base colour texture only;
// Medium = GGX + metallic-roughness, normal and emissive maps; High = + occlusion maps.
enum class MaterialQuality { Low, Medium, High };

struct NodePose;
class BakedLighting;   // baked lightmaps + light probes of a map (BakedLighting.h, agent-lightbake)
enum class BakedQuality { Off, Low, High };

// Post-processing hook (engine/render/PostFx.h implements it). With an active hook, render() draws
// the scene into the hook's offscreen target instead of `screenPass`; resolvePostFx() then runs the
// effect chain into `screenPass` and leaves that pass open (UI and overlays drawn after it are not
// post-processed). Without a hook, or with no active effect, nothing changes and it costs nothing.
struct PostFrame { mat4 viewProj; float3 cameraPos; Lighting lighting; int width = 0, height = 0;
                   gfx::TextureHandle sunShadow; mat4 lightViewProj; }; // sunShadow: empty when shadows are off
class PostProcess {
public:
    virtual ~PostProcess() = default;
    virtual bool active() const = 0;
    virtual bool linearScene() const { return false; }
    // True only while the post chain is accumulating a reprojection-validated HDR history.
    // Shadow sample scrambling must never be enabled without a temporal resolve pass.
    virtual bool temporalShadowResolve() const { return false; }
    virtual gfx::PassDesc scenePass(const gfx::PassDesc& out, int w, int h) = 0;
    // Optional side-channel material receiver: linear base RGB + metallic alpha,
    // with separate depth. Default PostProcess implementations pay ZERO cost.
    // This pass is requested once per 3D scene before the normal color pass.
    virtual bool wantsMaterialReceiverPass() const { return false; }
    virtual gfx::PassDesc materialReceiverPass(int,int) { return {}; }
    virtual void materialReceiverPublished(gfx::TextureHandle,int,int) {}
    // Optional HIGH-quality PBR normal+roughness receiver. Independent depth
    // attachment; producer must ensure it belongs to this same frame/view.
    virtual bool wantsSurfaceReceiverPass() const { return false; }
    virtual gfx::PassDesc surfaceReceiverPass(int,int) { return {}; }
    virtual void surfaceReceiverPublished(gfx::TextureHandle,int,int) {}
    virtual void resolve(gfx::Device& dev, const gfx::PassDesc& out, const PostFrame& frame) = 0;
    // These match the current MAIN Renderer3D PRISMA object motion bridge.
    // Older backends stay inert; the producer renders AFTER scene depth
    // exists but BEFORE resolve(), so no camera-only fake velocity is used.
    virtual bool wantsObjectMotionVectors() const { return false; }
    virtual gfx::TextureHandle objectMotionSceneDepth() const { return {}; }
    virtual uint64_t objectMotionEpoch(uint64_t) const { return 0; }
    virtual bool objectMotionContext(uint64_t& viewId,mat4& previousViewProj) const {
        (void)viewId;(void)previousViewProj;return false;
    }
    virtual void acceptObjectMotionVectors(gfx::TextureHandle) {}
    virtual void acceptObjectMotionVectors(MotionTextureInput input) {
        acceptObjectMotionVectors(input.texture);
    }
    virtual void acceptMotionReactiveMask(gfx::TextureHandle) {}
    virtual void acceptMotionReactiveMask(MotionTextureInput input) {
        acceptMotionReactiveMask(input.texture);
    }
};

// Baked skinning sample (Animator::baseSample()): clip A blends to clip B by `mix`; clip -1 = bind pose.
struct BakedSample { int clipA = -1, clipB = -1; float timeA = 0, timeB = 0, mix = 1; bool loopA = true, loopB = true; };

// One model instance. Node matrices come from an animated pose (Animation.h) or the bind pose.
// Rigid parts and baked-skinned meshes of identical instances are drawn instanced (28 with 4 KB uniforms / up to 256 with large uniform buffers).
// Immutable rigid source cache. Rebuild after changing the model or its transform.
struct StaticModelCache {
    const Model* model=nullptr;
    mat4 world;
    float3 min,max;
    std::vector<mat4> transforms;
    std::vector<size_t> primitiveOffsets;
    bool displaced=false;
    void prepare(const Model& source,const mat4& transform);
};
struct ModelDraw {
    const StaticModelCache* staticCache=nullptr;
    GeometryPolicy geometry;
    const Model* model = nullptr;
    // Optional stable entity identity; 0 means transient/immediate draw.
    // Attachments and multiple draws per entity need distinct IDs.
    uint64_t persistentId = 0;
    // Explicit revision/opt-in for immutable CPU render packets: zero disables
    // packet reuse. Increase when any caller-owned material/appearance state
    // changes in-place (including pointer-stable material vectors).
    uint64_t staticPacketRevision = 0;
    // Optional versioned material-owner contract (both must be nonzero).
    // Independent static objects sharing one immutable material set can
    // reuse its GPU-state signature across draws, without hashing every
    // material per instance. Owner ID is namespaced within a Model.
    // Advance materialOwnerRevision on any override changes, including
    // edits behind a stable pointer, texture content or shader graph edits.
    // Also advance Model::materialRevision for model-owned state changes.
    // Without this explicit contract the renderer retains exact GPU state
    // equality checks on every packet candidate.
    uint64_t materialOwnerKey = 0;
    uint64_t materialOwnerRevision = 0;
    mat4 world;                                    // model space -> world
    const std::vector<mat4>* nodeWorld = nullptr;  // pose, model space; null = bind pose
    const std::vector<Material>* materials = nullptr; // per-instance override (model->materials first, extra ones after)
    const std::vector<int>* nodeMaterial = nullptr;   // per node: index into materials for its meshes (-1 = own)
    float4 tint{1, 1, 1, 1};                       // rgb multiply (linear), a = opacity (< 1 draws blended)
    float3 team{1, 1, 1};                          // team colour, linear
    float flash = 0;                               // additive highlight (hit feedback)
    float teamGlow = 0;                            // emission = team colour (linear) * teamGlow on team-coloured surfaces
    int variant = -1;                              // KHR_materials_variants index
    uint32_t flags = 0;                            // DrawFlags (no cast / no receive shadow)
    const std::vector<NodePose>* pose = nullptr;   // morph weights (node weights when null)
    const BakedSample* baked = nullptr;            // skin from Model::baked instead of nodeWorld
    float dissolve = 0;                            // 0..1: burn away (spawn, death, construction)
    float4 outline{0, 0, 0, 0};                    // rgb (linear) + width in world units; a = 0: none
};

enum DrawFlags : uint32_t {
    kDrawUnlit = 1,
    kDrawNoCastShadow = 2,
    kDrawNoReceiveShadow = 4,
    // drawn blended because of its opacity (tint.w < 1) but still writing depth: a faded solid object (view.entity_fx)
    // stays in front of the ground layers drawn after the translucent pass (paint layers, ground canvas, sprites)
    kDrawFadeDepth = 8,
    kDrawActorReveal = 128, // local main-view cutaway; never changes sun shadows
    // transparent object (Unity's transparent queue, ZWrite Off): every primitive goes to the blended pass even when opaque,
    // sorted back to front one by one (ortho cameras: depth along the view direction), without depth write, so a nearer
    // part drawn earlier is painted over by a farther part drawn later; casts no shadow. Its depth is written afterwards
    // (colour mask off), so ground layers drawn after the translucent pass stay under it. Also per material: "transparent"
    kDrawTransparent = 16,
    // static scenery (does not move for the whole map): receives a baked lightmap and is part of the scene
    // the light baker captures (Renderer3D::setModelCapture). Dynamic objects get light probes instead.
    kDrawStatic = 32,
    kDrawKeepShadow = 64, // preserve opaque shadow casting while the camera image fades
    kDrawWaterSurface = 256, // opt-in NEREID water shader at arbitrary world Y; not a genre flag
    kDrawNereidCascade = 512, // opt-in vertically flowing water; never a planar reflection source
};

struct ModelRenderStats {
    uint64_t nodes = 0, culledNodes = 0, lodNodes[4] = {};
    // Main-camera source nodes only; excludes shadow/reflection repetitions.
    uint64_t cameraLodNodes[4] = {};
    uint64_t cameraLodChains = 0, cameraNoLodChains = 0;
    uint64_t cameraLodBlocked = 0, cameraShadowLimitedLod = 0;
    uint64_t triangles = 0, instancedDraws = 0, drawnInstances = 0;
    uint64_t multiDrawSubmissions = 0, indirectCommands = 0;
    // Submitted indexed MDI batches using an exact validated persistent
    // layout, without copying cached CPU commands into transient scratch.
    uint64_t mdiLayoutZeroCopySubmissions = 0;
    uint64_t mdiLayoutCopyAvoidedBytes = 0;
    // Exact-layout caching declined because the per-pass bounded cache is
    // full or this batch exceeds the maximum cached signature length.
    uint64_t mdiLayoutCacheBypassed = 0;
    // Indirect indexed command bytes *submitted to the RHI*. Metal/Vulkan
    // snapshot/dirty policies may avoid CPU copies; not GPU execution bytes.
    uint64_t mdiIndirectCommandBytes = 0;
    uint64_t mdiIndirectPrefixOverwrites = 0;
    // Backend-observed CPU operations taken before/after renderModels(0)
    // sun-shadow and the main-view renderModels(1) opaque pass. Aggregated
    // within one beginScene; reset with this ModelRenderStats struct.
    // These are deltas of monotonic counters, not GPU bytes/time. All other
    // model phases and reflection/auxiliary views are excluded.
    std::array<gfx::BufferUpdateStats, 2> mdiBackendPassTransfers{};
    std::array<uint64_t, 2> mdiBackendPassSamples{};
    // Opt-in Scene profile timings: shadow = 0, main-camera opaque = 1.
    // Timed only while profiling_ is enabled; these are synchronous CPU
    // durations and NEVER GPU execution times. Reset by beginScene().
    struct CpuMdiStageProfile {
        uint64_t passes = 0;           // eligible renderModels invocations
        uint64_t candidateScans = 0;   // MDI candidate groups inspected
        uint64_t indirectAttempts = 0; // reached exact command packing
        uint64_t submitted = 0;        // successful indirect API submissions
        uint64_t pipelineWarmups = 0;  // pipeline not yet ready, no MDI draw
        uint64_t underThreshold = 0;   // encoded command count too small
        uint64_t backendFallbacks = 0; // indirect failed, ordinary draws used
        double totalMs = 0.0;
        double selectionMs = 0.0;
        double groupingSortMs = 0.0;
        double candidateScanMs = 0.0;
        double commandPackingMs = 0.0;
        double pipelineSetupMs = 0.0;
        double bufferUploadMs = 0.0;
        double backendSubmitMs = 0.0;
    };
    std::array<CpuMdiStageProfile, 2> mdiCpuStages{};
    // CPU MDI groups spanning at least two different Model instances
    // using the SAME pooled vertex/index arena, after independent LOD and
    // per-view culling. These are successful submissions, not candidates.
    uint64_t staticCrossModelSubmissions = 0;
    uint64_t staticCrossModelCommands = 0;
    uint64_t staticCrossModelItems = 0;
    // Current-frame, per-pass page/material preflight. Groups are necessary
    // conditions; only actual indirect submissions count above.
    uint64_t staticCrossModelPreflightGroups = 0;
    uint64_t staticCrossModelPreflightReady = 0;
    uint64_t staticCrossModelPreflightRejected = 0;
    uint64_t staticCrossModelPreflightItems = 0;
    // Number of distinct permitted static page/material groups ordered
    // once per pass, rather than comparing full model/page state per item.
    uint64_t staticCrossModelRankedGroups = 0;
    // CPU ordering stats (not real draw calls); only large ranked prefixes
    // bucketized by exact page/material group contribute.
    uint64_t staticCrossModelBucketedItems = 0;
    uint64_t staticCrossModelBucketedGroups = 0;
    // Retained high-water workspace bytes, not per-frame allocation count.
    size_t staticCrossModelPreflightWorkspaceBytes = 0;
    uint64_t staticCrossModelAdaptiveSkippedPasses = 0;
    uint64_t staticCrossModelAdaptiveProbePasses = 0;
    uint64_t staticCrossModelAdaptiveMeasuredAttempts = 0;
    uint64_t staticCrossModelAdaptiveSkippedGroups = 0;
    uint64_t staticCrossModelAdaptiveProbeGroups = 0;
    // A subset of CPU MDI submissions: different rigid meshes and materials
    // share shader/texture state, with per-command base/PBR factors in U3.
    uint64_t mdiPaletteSubmissions = 0, mdiPaletteCommands = 0;
    // GPU-state grouping work across all model passes in a single scene.
    // Reused covers per-item or same-(model,material)-pointer cache hits;
    // resolved counts new GPU-state hashes/representative comparisons.
    uint64_t materialGroupsReused = 0, materialGroupsResolved = 0;
    // Per-frame diagnostic counters: only the model pass, not custom
    // renderers. Eligible scans are distinct from actual GPU submissions.
    uint64_t mdiScans = 0, mdiShort = 0, mdiBelowThreshold = 0;
    uint64_t mdiIneligible = 0, mdiFailed = 0;
    // Exclusive reason per ineligible MDI scan attempt (not per entity).
    uint64_t mdiIneligibleGeometry = 0, mdiIneligibleLighting = 0;
    uint64_t mdiIneligibleMaterial = 0, mdiIneligibleDeform = 0;
    uint64_t mdiIneligibleAlpha = 0;
    uint64_t mdiGeometryBreaks = 0, mdiMaterialBreaks = 0;
    uint64_t mdiStateBreaks = 0;
};

class Renderer3D {
public:
    bool init(gfx::Device& dev);
    void shutdown();
    // Optional fixed scene bounds for interior/model viewers; clear to use RTS ground fitting.
    void setShadowBounds(float3 lo, float3 hi) { shadowBoundsMin_=lo; shadowBoundsMax_=hi; hasShadowBounds_=true; }
    void clearShadowBounds() { hasShadowBounds_=false; }
    void setShadowQuality(ShadowQuality q);
    // Desktop/benchmark override of the High-tier map size (2048 default). Low/Medium keep their sizes.
    int shadowMapSize() const { return shadowSize_; }
    bool setShadowMapSize(int size) { highShadowSize_=size; setShadowQuality(quality_); return quality_!=ShadowQuality::High||shadowMap_.valid(); }
    ShadowQuality shadowQuality() const { return quality_; }
    static ShadowQuality defaultQualityFor(const gfx::Device& dev, bool mobile);

    void beginScene(const RTSCamera& cam, int fbW, int fbH);
    void drawMesh(const Mesh& m, const mat4& model, float4 color, uint32_t flags = 0, float specular = 0.25f, float emissive = 0.0f);
    // Opt-in per-surface ripple normals. Positions are render-origin-relative
    // X/Z; z = propagating radius, w = signed amplitude. No new RHI buffers.
    void drawNereidRippleMesh(const Mesh& mesh,const mat4& model,float4 color,
                              float specular,float refraction,
                              const std::array<float4,16>& ripples,
                              uint32_t firstIndex=0,uint32_t indexCount=0,
                              const std::array<float4,16>& wakeShapes={},
                              float4 wind={},float4 hydraulics={},
                               float4 rapidColor={0.84f,0.93f,0.94f,1.f});
    // Editor Scene only: render selected geometry into a depth-tested mask
    // and composite its exterior silhouette in screen pixels.
    void setSceneSelectionOutline(bool enabled) { sceneSelectionOutline_ = enabled; }
    bool sceneSelectionOutlineEnabled() const { return sceneSelectionOutline_; }
    void drawSelectionMesh(const Mesh& m, const mat4& model,
                           float4 color, float4 selection);
    void line(float3 a, float3 b, uint32_t color);
    void triangle(float3 a, float3 b, float3 c, uint32_t color);    // flat, alpha-blended
    void quadXZ(float3 a, float3 b, float width, uint32_t color, float y = 0.02f);
    void circleXZ(float3 c, float radius, uint32_t color, int segments = 40, float y = 0.03f);
    bool warmModelPipelines(const Model& model, const std::vector<Material>* materials = nullptr);
    void drawModel(const ModelDraw& d);
    // Opt-in CPU cache of per-node rigid bind transforms for stationary objects
    // with stable persistentId. Does NOT cache visibility, LOD, materials or draws.
    struct StaticModelCacheStats {
        uint64_t hits = 0, rebuilds = 0, declined = 0, evicted = 0;
        size_t live = 0;
    };
    StaticModelCacheStats staticModelCacheStats() const {
        auto stats = staticModelCacheStats_;
        stats.live = staticModelCaches_.size();
        return stats;
    }
    // Invariant model eligibility is classified once per geometry revision
    // instead of traversing node and primitive arrays for every instance.
    struct StaticEligibilityStats {
        uint64_t hits = 0, misses = 0, evicted = 0;
        size_t live = 0;
    };
    StaticEligibilityStats staticEligibilityStats() const {
        auto stats = staticEligibilityStats_;
        stats.live = staticModelEligibility_.size();
        return stats;
    }
    // Call before/after in-place model geometry or bind-hierarchy edits, or
    // after an asset reload retaining the same model pointer and GPU handles.
    void invalidateStaticModelCaches() {
        clearCpuMdiRollingHistory(); // a full scene reload starts a new comparison window
        staticModelCaches_.clear();
        staticRenderPackets_.clear();
        staticModelEligibility_.clear();
        staticMaterialOwners_.clear();
        cpuMdiPages_.clear(); // reset page-specific costs on whole-scene reload
        cpuMdiAdaptive_ = {};
        autoCpuBatch_ = {};
        staticCrossPreflight_.release(); // retire temporary scene identities/capacity
        clearIndirectLayoutCaches(); // release borrowed primitive/model references
        staticRenderPacketBytes_ = 0;
        staticMaterialOwnerBytes_ = 0;
    }
    // Targeted cache retirement for native ECS entity destruction/streaming.
    // Invoke on the renderer thread before Model resources are released and
    // before starting another draw submission; no GPU resources are owned here.
    void invalidateStaticModelCache(uint64_t persistentId);
    void invalidateModelCachesFor(const Model* model);
    // Drop one shared material-owner signature before its native source is
    // replaced or destroyed. Existing packets automatically rebuild when the
    // owner returns with a new signature serial; other owners are unaffected.
    void invalidateSharedMaterialState(const Model* model, uint64_t materialOwnerKey);
    struct StaticRenderPacketStats {
        uint64_t hits = 0, rebuilds = 0, declined = 0, evicted = 0;
        size_t live = 0, retainedBytes = 0;
    };
    struct SharedMaterialStateStats {
        uint64_t hits = 0, misses = 0, evicted = 0;
        size_t live = 0, retainedBytes = 0;
    };
    SharedMaterialStateStats sharedMaterialStateStats() const {
        auto stats = sharedMaterialStateStats_;
        stats.live = staticMaterialOwners_.size();
        stats.retainedBytes = staticMaterialOwnerBytes_;
        return stats;
    }
    StaticRenderPacketStats staticRenderPacketStats() const {
        auto s = staticRenderPacketStats_;
        s.live = staticRenderPackets_.size();
        s.retainedBytes = staticRenderPacketBytes_;
        return s;
    }
    // Diagnostic, opt-in persistent scene upload stats. This staging path is
    // not yet bound to model shaders or used for GPU culling.
    struct PersistentSceneStats {
        uint32_t live = 0, ranges = 0, records = 0;
        size_t bytes = 0;
        bool gpuFrustumDispatched = false;
    };
    PersistentSceneStats persistentSceneStats() const { return persistentStats_; }
    struct GpuModelDrawStats {
        uint32_t eligible = 0, submitted = 0;
        uint32_t dynamicRigidEligible = 0, gpuLodEligible = 0;
        uint32_t gpuEncodedSubmissions = 0, gpuEncodedCommands = 0;
        uint32_t gpuIcbCandidates = 0, gpuIcbPrepared = 0;
        uint32_t gpuIcbRejected = 0, gpuIcbExecuteFailed = 0;
        uint32_t uploadedRecords = 0, uploadRanges = 0;
        size_t uploadBytes = 0;
        uint32_t preflightGroups = 0, batchCacheHits = 0, batchCacheMisses = 0;
        bool adaptiveInstancing = false;
        bool compacted = false, prepared = false;
    };
    GpuModelDrawStats gpuModelDrawStats() const {
        const auto upload = gpuModelCommandsReady_ ?
            gpuModelCommands_.uploadStats(gpuModelFrame_, gpuModelSubmission_) :
            GpuSceneDrawCommands::UploadStats{};
        return {gpuModelEligible_, gpuModelDrawn_,
                gpuModelDynamicEligible_, gpuModelLodEligible_,
                gpuEncodedDrawCalls_, gpuEncodedCommands_,
                gpuIcbCandidates_, gpuIcbPrepared_,
                gpuIcbRejected_, gpuIcbExecuteFailed_,
                upload.records, upload.ranges, upload.bytes,
                gpuPreflightGroups_, gpuBatchCacheHits_, gpuBatchCacheMisses_,
                gpuAdaptiveInstancingFallback_,
                gpuModelCommandsReady_ && gpuModelCommands_.compactedReady(gpuModelFrame_, gpuModelSubmission_),
                gpuModelCommandsReady_};
    }
    void render(const gfx::PassDesc& screenPass, PostProcess* post = nullptr); // shadow pass + opens the main pass and draws everything
    // With a post hook: ends the scene pass, runs the effects into screenPass (left open). No-op otherwise.
    // Call after everything that belongs to the 3D scene (particles), before UI.
    void resolvePostFx();
    // Compatibility seam for the separate PRISMA/Renderer3D motion producer.
    // The current MAIN renderer has its native rigid/skinned/animated object
    // pass and uses PostProcess hooks directly. This older renderer branch
    // can install that existing producer (or a native equivalent) ONCE,
    // without manually passing velocity on every LumaWeave frame.
    // Callback runs OUTSIDE the HDR scene pass, immediately BEFORE resolve.
    // It must publish real MotionTextureInput with accurate metadata; never
    // fabricate motion. Do not install simultaneously with MAIN's built-in
    // renderObjectMotionVectors after conflict-aware reconciliation.
    using ObjectMotionProducer=std::function<void(Renderer3D&,PostProcess&)>;
    void setObjectMotionProducer(ObjectMotionProducer fn) {
        objectMotionProducer_=std::move(fn);
    }
    // Extra renderers (sprites, tiles) that draw with their own pipelines inside the main pass: run
    // after opaque and translucent meshes, before overlays (fog of war stays on top).
    // Cleared by beginScene.
    using CustomDraw = std::function<void(gfx::Device&)>;
    void drawCustom(CustomDraw fn) { custom_.push_back(std::move(fn)); }
    // Inside a custom draw: bind what the lit shaders share (U1 frame, U2 + tex1 point lights, tex0
    // shadow map), so custom lit shaders built on AIVERSE_*_SHADOW / AIVERSE_*_LIGHTS get the same lighting.
    void bindLitFrame();
    // Opaque custom renderers (terrain): run first in the main pass, before meshes and models, so
    // translucent meshes (water) blend over them. Same lifetime rules as drawCustom.
    void drawCustomOpaque(CustomDraw fn) { customOpaque_.push_back(std::move(fn)); }
    // Optional per-view native surface callbacks for LumaWeave:
    // - Material: RGBA16F (linear diffuse base RGB, metallic A)
    // - Surface:  RGBA16F (signed world normal XYZ, roughness A)
    // Both callbacks must draw the EXACT same frontmost opaque geometry and
    // depth as their main scene submission; they never execute on default
    // PostProcess paths that did not request the corresponding receiver pass.
    // Call after beginScene; callbacks are cleared automatically next beginScene.
    void drawCustomMaterialReceiver(CustomDraw fn) {
        customMaterialReceiver_.push_back(std::move(fn));
    }
    void drawCustomSurfaceReceiver(CustomDraw fn) {
        customSurfaceReceiver_.push_back(std::move(fn));
    }
    // Extra shadow casters (sprites): run inside the sun shadow pass (only when shadows are on) with
    // the target bound; draw depth with lightViewProj(). Same lifetime rules as drawCustom.
    void drawCustomShadow(CustomDraw fn) { customShadow_.push_back(std::move(fn)); }
    const mat4& lightViewProj() const { return lightViewProj_; }
    const RTSCamera& camera() const { return cam_; }
    float4 actorReveal{}; // world-space actor centre + aperture radius, zero disables

    // Traced lighting ("trace" tier, Renderer3DTrace.cpp): sun rays marched over the terrain height
    // field (long soft shadows of cliffs and mountains, every lit shader) and screen-space
    // reflections on water. On by default with High shadows; AIVERSE_TRACE=off|on overrides.
    void setTraceEnabled(bool on) { traceWanted_ = on; }
    void setModelReflectionsEnabled(bool on) { modelReflections_ = on; }
    bool traceEnabled() const { return traceWanted_; }
    bool traceActive() const;                              // this frame: wanted, High shadows, height field bound
    static bool defaultTraceFor(const gfx::Device& dev, bool mobile);
    void setHeightField(const HeightField& hf) { heightField_ = hf; }   // the terrain renderer owns the texture
    const HeightField& heightField() const { return heightField_; }
    // Inside a custom lit draw (terrain): bind the height field for AIVERSE_*_TRACE_AT(slot).
    void bindTrace(int textureSlot);
    bool ssrActive() const { return ssrThisFrame_; }       // the scene targets are bound this frame (water SSR)

    void setMaterialQuality(MaterialQuality q);
    MaterialQuality materialQuality() const { return matQuality_; }
    static MaterialQuality defaultMaterialQualityFor(const gfx::Device& dev, bool mobile);
    const std::string& modelError() const { return modelError_; }   // last model shader failure

    const mat4& viewProj() const { return viewProj_; }
    const float3& cameraPos() const { return camPos_; }
    Lighting lighting;
    LightSystem lights;   // dynamic point lights: add between beginScene() and render()
    struct GeometryStats { uint32_t clustersTested=0, clustersCulled=0, coarseClusters=0, occluded=0, occluderTriangles=0; };
    const GeometryStats& geometryStats() const { return geometryStats_; }
    bool clipDepthZeroToOne() const { return dev_&&dev_->info().clipDepthZeroToOne; }
    int sceneWidth() const { return sceneW_; }
    int sceneHeight() const { return sceneH_; }
    uint32_t drawCalls() const { return drawCalls_; }
    // Studio Scene diagnostic: true colours each main-pass GPU draw submission.
    // One instanced batch is one colour. This does not change gameplay or simulation.
    void setDrawCallDebug(bool enabled) { drawCallDebug_ = enabled; }
    bool drawCallDebug() const { return drawCallDebug_; }
    // Scene-only visualization. 0: shaded, 1: draw calls, 2..9: GPU material/geometry views.
    void setStudioDebugMode(int mode) { studioDebugMode_ = mode; drawCallDebug_ = mode == 1; }
    int studioDebugMode() const { return studioDebugMode_; }
    void setModelCullingEnabled(bool on) { modelCullingEnabled_ = on; }
    void setModelInstancingEnabled(bool on) { modelInstancingEnabled_ = on; }
    void setModelMultiDrawEnabled(bool on) { modelMultiDrawEnabled_ = on; }
    // Explicit editor-view override for the opt-in pooled static CPU MDI
    // feature. Environment preserves the process-level opt-in (OFF by
    // default), and Forbid/Enable are scoped by the caller to one view.
    // This affects only eligible static CPU MDI ordering/admission; the
    // authoritative geometry/material/LOD/backend checks remain.
    enum class StaticCrossModelBatchMode : uint8_t {
        Environment = 0, Enable = 1, Forbid = 2
    };
    void setStaticCrossModelBatchMode(StaticCrossModelBatchMode mode) {
        staticCrossModelBatchMode_ = mode;
    }
    StaticCrossModelBatchMode staticCrossModelBatchMode() const {
        return staticCrossModelBatchMode_;
    }
    bool staticCrossModelBatchesEnabled() const {
        return staticCrossModelBatchesEnabled_;
    }
    // Runtime CPU MDI policies shared by every genre/host. Environment
    // preserves existing defaults and flags; Enabled/Forbid affect the
    // renderer's next beginScene only, never global process settings.
    enum class CpuMdiPolicyMode : uint8_t {
        Environment = 0, Enable = 1, Forbid = 2
    };
    void setCpuMdiAdaptiveMode(CpuMdiPolicyMode mode) { cpuMdiAdaptiveMode_ = mode; }
    void setMdiLayoutCacheMode(CpuMdiPolicyMode mode) { mdiLayoutCacheMode_ = mode; }
    void setMdiGeometrySortMode(CpuMdiPolicyMode mode) { mdiGeometrySortMode_ = mode; }
    CpuMdiPolicyMode cpuMdiAdaptiveMode() const { return cpuMdiAdaptiveMode_; }
    CpuMdiPolicyMode mdiLayoutCacheMode() const { return mdiLayoutCacheMode_; }
    CpuMdiPolicyMode mdiGeometrySortMode() const { return mdiGeometrySortMode_; }
    bool mdiLayoutCacheEnabled() const { return mdiLayoutCacheEnabled_; }
    bool mdiGeometrySortRequested() const { return mdiGeometrySortRequested_; }
    // 0 means follow AIVERSE_CPU_MDI_MAX_US_PER_COMMAND, otherwise accept
    // the same conservative [1,1000] microseconds-per-command range.
    // This is an admission-policy budget, NOT a GPU timing guarantee.
    void setCpuMdiAdaptiveBudgetOverrideUs(double us) {
        if (us == 0.0 || (us >= 1.0 && us <= 1000.0))
            cpuMdiAdaptiveBudgetOverrideUs_ = us;
    }
    double cpuMdiAdaptiveBudgetOverrideUs() const {
        return cpuMdiAdaptiveBudgetOverrideUs_;
    }
    // Reusable genre-neutral context namespace: two independent camera
    // hosts may share a GPU geometry page yet have different CPU costs.
    // Zero preserves legacy behavior. Used only in the per-page adaptive
    // feedback key; never in a shader or actual draw command.
    void setCpuMdiAdaptiveScope(uint64_t scope) { cpuMdiAdaptiveScope_ = scope; }
    uint64_t cpuMdiAdaptiveScope() const { return cpuMdiAdaptiveScope_; }
    // Value-type configuration snapshot lets a host preserve its existing
    // renderer policy while a different viewport temporarily overrides it.
    // This is CPU-only and does not serialize GPU buffers or dynamic feedback.
    struct CpuMdiPolicyOverrides {
        StaticCrossModelBatchMode crossModel = StaticCrossModelBatchMode::Environment;
        CpuMdiPolicyMode adaptive = CpuMdiPolicyMode::Environment;
        CpuMdiPolicyMode layoutCache = CpuMdiPolicyMode::Environment;
        CpuMdiPolicyMode geometrySort = CpuMdiPolicyMode::Environment;
        double adaptiveBudgetUs = 0.0;
        uint64_t adaptiveScope = 0;
    };
    CpuMdiPolicyOverrides cpuMdiPolicyOverrides() const {
        return {staticCrossModelBatchMode_, cpuMdiAdaptiveMode_,
            mdiLayoutCacheMode_, mdiGeometrySortMode_,
            cpuMdiAdaptiveBudgetOverrideUs_, cpuMdiAdaptiveScope_};
    }
    void setCpuMdiPolicyOverrides(const CpuMdiPolicyOverrides& value) {
        setStaticCrossModelBatchMode(value.crossModel);
        setCpuMdiAdaptiveMode(value.adaptive);
        setMdiLayoutCacheMode(value.layoutCache);
        setMdiGeometrySortMode(value.geometrySort);
        setCpuMdiAdaptiveBudgetOverrideUs(value.adaptiveBudgetUs);
        setCpuMdiAdaptiveScope(value.adaptiveScope);
    }
    // Scoped policy override for any native render host (RTS/FPS/editor/etc).
    // Snapshot only CPU batching configuration, not transient GPU/scene state.
    // Nested scopes restore in LIFO order, including on an exception or an
    // early return. The host must not outlive the referenced Renderer3D.
    class ScopedCpuMdiPolicyOverride {
    public:
        ScopedCpuMdiPolicyOverride(Renderer3D& renderer,
                                   const CpuMdiPolicyOverrides& overrides)
            : renderer_(renderer), saved_(renderer.cpuMdiPolicyOverrides()) {
            renderer_.setCpuMdiPolicyOverrides(overrides);
        }
        ~ScopedCpuMdiPolicyOverride() {
            renderer_.setCpuMdiPolicyOverrides(saved_);
        }
        ScopedCpuMdiPolicyOverride(const ScopedCpuMdiPolicyOverride&) = delete;
        ScopedCpuMdiPolicyOverride& operator=(const ScopedCpuMdiPolicyOverride&) = delete;
        ScopedCpuMdiPolicyOverride(ScopedCpuMdiPolicyOverride&&) = delete;
        ScopedCpuMdiPolicyOverride& operator=(ScopedCpuMdiPolicyOverride&&) = delete;
    private:
        Renderer3D& renderer_;
        CpuMdiPolicyOverrides saved_;
    };
    // Independently control GPU-authored draws for 3-way Scene benchmarking.
    // The process-wide experimental environment flag remains mandatory.
    void setGpuAuthoredDrawEnabled(bool on) { gpuAuthoredDrawEnabled_ = on; }
    bool modelMultiDrawSupported() const { return dev_ && dev_->supportsMultiDrawIndirect(); }
    const ModelRenderStats& modelStats() const { return modelStats_; }
    const AutoCpuBatchPolicy& autoCpuBatchStats() const { return autoCpuBatch_; }
    static constexpr size_t kCpuMdiRollingWindow = 120;
    // Diagnostic heuristic, not a performance target. Require enough
    // samples and both relative and absolute P95 excursion to avoid
    // alarming about nanosecond-scale noise on otherwise quiet stages.
    static constexpr size_t kCpuMdiSpikeMinSamples = 30;
    static constexpr double kCpuMdiSpikeMinP95Ms = 0.25;
    static constexpr double kCpuMdiSpikeMinExcessMs = 0.15;
    static constexpr double kCpuMdiSpikeP95ToMeanRatio = 1.8;
    // Independent steady-cost heuristic: a high median indicates that
    // at least half of the sampled invocations spend real CPU time here.
    // Also require material share of the mean *whole* model pass. These
    // limits diagnose candidates only and never change batching policy.
    static constexpr double kCpuMdiSustainedMinMedianMs = 0.35;
    static constexpr double kCpuMdiSustainedMinMeanShare = 0.15;
    // The eight independent CPU durations are: total model pass,
    // visibility selection, material grouping+sort, candidate scanning,
    // CPU command packing, pipeline setup, indirect buffer update, backend
    // API submission. P95 is nearest-rank for the last <=120 eligible
    // model-pass invocations, not frame time or GPU work.
    struct CpuMdiRollingSummary {
        size_t samples = 0;
        std::array<double, 8> meanMs{};
        std::array<double, 8> p50Ms{}; // nearest-rank median, not an average
        std::array<double, 8> p95Ms{};
        // Index 0 is total model-pass wall time; indexes 1..7 are the
        // specific CPU stages. Total may spike without any tracked stage.
        std::array<bool, 8> spike{};
        size_t highlightedStages = 0; // excludes overlapping total
        int dominantStage = -1;       // 1..7, -1 if none
        double dominantExcessMs = 0.0;
        // Independent steady-cost candidates. Index 0 is always false:
        // the whole model pass is an overlapping total, not an MDI stage.
        std::array<bool, 8> sustained{};
        size_t sustainedStages = 0;
        int dominantSustainedStage = -1; // 1..7, -1 when no expensive stage
        double dominantSustainedP50Ms = 0.0;
        double dominantSustainedShare = 0.0; // ratio of means, not GPU occupancy
        bool hasBaseline = false;     // at least 30 eligible samples
    };
    CpuMdiRollingSummary cpuMdiRollingSummary(size_t phase) const;
    // Read-only history reset for a new world/scene. Does not touch any
    // cached geometry, render commands, in-flight resources or profiling.
    void clearCpuMdiRollingHistory() { cpuMdiRolling_ = {}; }
    struct IndirectLayoutCacheStats {
        size_t entries = 0, retainedBytes = 0;
        uint64_t retired = 0;
    };
    // Retained CPU layout storage only; does not include GPU command buffers.
    IndirectLayoutCacheStats indirectLayoutCacheStats() const;
    // Backend-observed CPU memcpy/memcmp and snapshot/version allocation
    // counts for our own dynamic indirect buffer, cumulative since creation.
    // Unsupported backends or an as-yet-uncreated buffer return unavailable.
    gfx::BufferUpdateStats indirectBufferUpdateStats() const {
        return dev_ && indirectCommands_.valid()
            ? dev_->bufferUpdateStats(indirectCommands_) : gfx::BufferUpdateStats{};
    }
    // 0 = sun shadow, 1 = opaque main view; CPU time only, no GPU timestamps.
    const std::array<CpuMdiAdaptiveFeedback, 2>& cpuMdiAdaptiveFeedback() const {
        return cpuMdiAdaptive_;
    }
    bool cpuMdiAdaptiveEnabled() const { return cpuMdiAdaptiveEnabled_; }
    double cpuMdiAdaptiveBudgetUs() const { return cpuMdiAdaptiveBudgetUs_; }
    struct CpuMdiPageStats {
        uint64_t reused = 0, created = 0, evicted = 0, untracked = 0;
        size_t live = 0, slowShadow = 0, slowOpaque = 0;
    };
    CpuMdiPageStats cpuMdiPageStats() const {
        auto stats = cpuMdiPageStats_;
        stats.live = cpuMdiPages_.size();
        for (const auto& [key, entry] : cpuMdiPages_) {
            (void)key;
            if (entry.lifetime.expired()) continue;
            for (size_t phase = 0; phase < 2; ++phase) {
                const auto& feedback = entry.byPass[phase];
                if (feedback.samples >= 8 &&
                    feedback.avgUsPerCommand > cpuMdiAdaptiveBudgetUs_) {
                    if (phase == 0) ++stats.slowShadow;
                    else ++stats.slowOpaque;
                }
            }
        }
        return stats;
    }
    struct RenderProfile {
        double submitMs=0,occlusionMs=0;
        std::array<double,5> selectMs{},sortMs{},drawMs{};
        std::array<uint32_t,5> draws{},instances{},triangles{};
        // One current-camera colour view only (opaque + blended).
        // Excludes shadow/reflection, extra capture of opaque, outlines and
        // depth-only passes. Drawn instances count multiplicatively.
        // Custom renderers (terrain, sprites etc.) are not accounted for.
        uint64_t cameraModelTriangles=0;
        uint64_t cameraMeshTriangles=0;
        uint64_t cameraOverlayTriangles=0;
        uint32_t cachedModels=0,lodNodes=0,lodTransitionNodes=0;
    };
    void setProfiling(bool enabled) { profiling_=enabled; }
    const RenderProfile& profile() const { return profile_; }
    // Set before rendering; zero chooses the backend limit (28 with 4 KB uniforms / 256 Metal).
    void setInstanceLimit(uint32_t limit);
    uint32_t instanceLimit() const { return instanceLimit_; }
    // Debug exactly one selected editor model instance, not every entity.
    // This keeps the runtime default free of per-entity trace allocations.
    struct LodDebugSample {
        bool matched = false;
        uint64_t visibleNodes = 0, withChain = 0, withoutChain = 0;
        uint64_t shadowLimited = 0;
        std::array<uint64_t,4> levels{};
    };
    void setLodDebugTarget(uint64_t id) { lodDebugTarget_ = id; }
    void setLodDiagnosticsEnabled(bool enabled) { lodDiagnosticsEnabled_ = enabled; }
    const LodDebugSample& lodDebugSample() const { return lodDebugSample_; }
    void setModelLodEnabled(bool enabled) { modelLodEnabled_=enabled; }
    bool modelLodEnabled() const { return modelLodEnabled_; }
    // Relative screen-size band around each threshold; 0 restores instant switches.
    void setModelLodFadeWidth(float width) { modelLodFadeWidth_=std::clamp(width,0.0f,0.49f); }
    float modelLodFadeWidth() const { return modelLodFadeWidth_; }

    // Native renderer hosts may choose different instancing/indirect/LOD
    // settings per view. Save the configured state, not resolved GPU commands.
    struct ModelRenderPolicyOverrides {
        bool instancing = true;
        bool multiDraw = true;
        bool gpuAuthored = true;
        bool culling = true;
        bool lod = true;
        float lodFadeWidth = 0.15f;
    };
    ModelRenderPolicyOverrides modelRenderPolicyOverrides() const {
        return {modelInstancingEnabled_, modelMultiDrawEnabled_,
                gpuAuthoredDrawEnabled_, modelCullingEnabled_,
                modelLodEnabled_, modelLodFadeWidth_};
    }
    void setModelRenderPolicyOverrides(const ModelRenderPolicyOverrides& value) {
        setModelInstancingEnabled(value.instancing);
        setModelMultiDrawEnabled(value.multiDraw);
        setGpuAuthoredDrawEnabled(value.gpuAuthored);
        setModelCullingEnabled(value.culling);
        setModelLodEnabled(value.lod);
        setModelLodFadeWidth(value.lodFadeWidth);
    }
    // Restores the original model batching and culling/LOD settings on
    // any exit, even when the view's benchmark changes them mid-scope.
    // This does not restore instanceLimit (shader-pipeline setting), caches
    // or diagnostics; the referenced renderer must outlive the guard.
    class ScopedModelRenderPolicyOverride {
    public:
        explicit ScopedModelRenderPolicyOverride(Renderer3D& renderer)
            : renderer_(renderer), saved_(renderer.modelRenderPolicyOverrides()) {}
        ScopedModelRenderPolicyOverride(
            Renderer3D& renderer, const ModelRenderPolicyOverrides& overridePolicy)
            : ScopedModelRenderPolicyOverride(renderer) {
            renderer_.setModelRenderPolicyOverrides(overridePolicy);
        }
        ~ScopedModelRenderPolicyOverride() {
            renderer_.setModelRenderPolicyOverrides(saved_);
        }
        ScopedModelRenderPolicyOverride(const ScopedModelRenderPolicyOverride&) = delete;
        ScopedModelRenderPolicyOverride& operator=(const ScopedModelRenderPolicyOverride&) = delete;
        ScopedModelRenderPolicyOverride(ScopedModelRenderPolicyOverride&&) = delete;
        ScopedModelRenderPolicyOverride& operator=(ScopedModelRenderPolicyOverride&&) = delete;
    private:
        Renderer3D& renderer_;
        ModelRenderPolicyOverrides saved_;
    };

    // Baked global illumination (agent-lightbake): lightmaps for static models (matched by model path,
    // node and world transform), light probes for everything else, mixed with the realtime sun by the
    // set's mode (indirect / shadowmask / subtractive). Null = realtime only. The set must outlive its use.
    void setBakedLighting(const BakedLighting* b) { baked_ = b; }
    const BakedLighting* bakedLighting() const { return baked_; }
    // Opt-in dynamic-only indirect probe overlay. Same gfx::Device as this renderer.
    // No lightmap/cache change; the caller must keep texture alive while bound.
    // Existing shader-graph texture slots 13+ take priority, so models using graph
    // textures currently fall back to their existing GI path.
    bool setDynamicGiOverlay(const GiDynamicProbeTexture* texture, float3 origin,
                             float spacing, int nx, int ny, int nz,
                             int ringX = 0, int ringY = 0, int ringZ = 0,
                             const GiClipmapValidityTexture* validity = nullptr);
    // Optional dual-cascade far volume. Call only after binding a fine atlas
    // WITH an R8 validity mask. The far atlas must be fully GPU-complete.
    bool setDynamicGiCascadeOverlay(const GiDynamicProbeTexture* far,
                                    float3 origin, float spacing,
                                    int nx, int ny, int nz,
                                    int ringX=0, int ringY=0, int ringZ=0);
    // Extra Metal-only third clipmap (tex16, U8). Disabled for GL/GLES/Vulkan
    // because their exposed sampled texture slot limits are not assumed >16.
    bool setDynamicGiThirdCascadeOverlay(const GiDynamicProbeTexture* third,
                                         float3 origin, float spacing,
                                         int nx, int ny, int nz,
                                         int ringX=0, int ringY=0, int ringZ=0);
    void clearDynamicGiOverlay() {
        dynamicGiOverlay_ = nullptr;
        dynamicGiValidity_ = nullptr;
        dynamicGiFarOverlay_ = nullptr;
        dynamicGiThirdOverlay_ = nullptr;
    }
    bool dynamicGiOverlayActive() const { return dynamicGiOverlay_ != nullptr; }
    void setBakedQuality(BakedQuality q);
    BakedQuality bakedQuality() const { return bakedQuality_; }
    static BakedQuality defaultBakedQualityFor(const gfx::Device& dev, bool mobile);
    bool bakedActive() const { return baked_ && bakedQuality_ != BakedQuality::Off; }
    // model primitives of the last scene that took a lightmap / the probes (diagnostics, lightbake_status)
    uint32_t bakedLightmapped() const { return bakedLightmapped_; }
    uint32_t bakedProbed() const { return bakedProbed_; }
    // Light baker capture: every drawModel with kDrawStatic is reported (world matrix per node) until reset.
    using ModelCapture = std::function<void(const ModelDraw& d, const std::vector<mat4>& nodeWorld)>;
    void setModelCapture(ModelCapture fn) { capture_ = std::move(fn); }
    // Inside a custom lit draw (terrain): bind the terrain lightmap for AIVERSE_*_BAKED_TERRAIN (BakedShaders.h):
    // uniform block `uniformSlot`, RGBM page at `lightmapSlot`, shadow mask at `maskSlot` (placeholders when none).
    void bindBakedTerrain(int uniformSlot, int lightmapSlot, int maskSlot);

private:
    struct DrawItem {
        const Mesh* mesh; mat4 model; float4 color;
        uint32_t flags; float specular, emissive;
        float4 selection{0, 0, 0, 0};
        std::array<float4,16> waterRipples{};
        std::array<float4,16> waterWakeShapes{};
        float4 waterWind{};
        float4 waterHydraulics{};
        float4 waterRapidColor{};
        uint32_t waterFirstIndex=0,waterIndexCount=0;
    };
    struct ModelItem {
        const Model* model; const Primitive* prim; const Material* mat;
        int32_t materialIndex = -1; // remap material pointers on packet replay
        bool rigid = false, castShadow = false;
        GeometryPolicy geometry;
        size_t geometryPrimitive = 0;
        uint32_t firstIndex = 0, indexCount = 0;
        // Optional two-level, single-primitive GPU LOD choice. Always preserve
        // the CPU-selected primitive for legacy draws and shadow passes.
        bool gpuLodAvailable = false;
        gfx::DrawIndexedIndirectCommand gpuLodNear{}, gpuLodFar{};
        float gpuLodMaxPixels = 0, gpuLodError = 0;
        float3 gpuLodBoundsMin{}, gpuLodBoundsMax{};
        mat4 world; float4 tint; float3 team; float flash; uint32_t flags;
        int32_t joints;     // offset into jointData_ (floats), -1 when not palette-skinned
        bool baked;         // skinned from the baked texture (rows in anim)
        float4 anim;        // baked row A, row B, mix
        float dissolve, teamGlow;
        float lodThreshold=1; bool lodInvert=false;
        float4 outline;
        int32_t morph;      // offset into morphData_ (8 weights), -1 = none
        int16_t lmPage = -1;  // baked lightmap page (anim = uv2 scale / offset), -1 = none
        uint8_t gi = 0;       // 0 realtime ambient, 1 lightmap, 2 light probes
        const std::vector<Material>* paletteMaterials = nullptr;
        uint8_t materialRole = 0; // 1 original colour suppressed, 2 palette batch (colour only)
        uint8_t shadowRole = 0;   // 0 normal, 1 part covered by its mesh's shadow batch, 2 the shadow batch
        bool inView, inShadow; float depth;
        int32_t sourceNode = -1; // producer node for per-view packet replay
    };
    void initModels();
    void shutdownModels();
    gfx::PipelineHandle modelPipeline(const ModelItem& it, int phase, bool instanced,
                                      bool gpuInstances = false, bool instancePalette = false);
    bool drawModelBatch(const uint32_t* items, uint32_t count, int phase,
                        bool submit = true, bool gpuInstances = false,
                        bool instancePalette = false);
    void packModelInstance(const ModelItem& item, int phase,
                           const float4& callColour, float* destination,
                           uint32_t instanceMaterial = 0) const;
    bool drawModelIndirectBatch(const uint32_t* items, uint32_t count, int phase,
                                bool instancePalette = false);
    void prepareGpuModelDraws();
    void renderModels(int phase);   // 0 shadow, 1 opaque, 2 blended, 3 outlines, 4 depth of transparent objects
    bool initSelectionOutline();
    void renderSelectionOutline(const gfx::PassDesc& scenePass, int width, int height);

    void renderReflection();        // water planar reflection (Renderer3DReflect.cpp, lighting agent)
    void renderSpotShadow();        // depth of the casters seen from the main spot light (same file)
    void computeShadowMatrix(const RTSCamera& cam, float aspect);
    void uploadFrameUniforms(bool forShadow);
    // trace tier (Renderer3DTrace.cpp)
    bool initTrace();
    void shutdownTrace();
    bool beginSceneCapture(int w, int h);   // SSR: opens the opaque pass into the scene targets
    void endSceneCapture(const gfx::PassDesc& mainPass, int w, int h);   // blits colour + depth into the main pass
    void bindSceneTextures(int colorSlot, int depthSlot);   // after the capture: the scene targets, else placeholders
    void bindTraceTextures();                                // lit pipeline: tex3 height, tex4/tex5 scene

    gfx::Device* dev_ = nullptr;
    gfx::ShaderHandle litShader_, shadowShader_, colorShader_;
    gfx::PipelineHandle litPipe_, litBlendPipe_, shadowPipe_, linePipe_, triPipe_;
    gfx::BufferHandle lineVB_, triVB_;
    gfx::TextureHandle shadowMap_;
    gfx::TextureHandle sceneSelectionMask_;
    gfx::ShaderHandle sceneSelectionEdgeShader_;
    gfx::PipelineHandle sceneSelectionEdgePipe8_, sceneSelectionEdgePipeHdr_;
    gfx::PipelineHandle sceneSelectionPrimitiveMaskPipe_; // scene-depth-tested, ZWrite off
    gfx::BufferHandle sceneSelectionTriangle_;
    int sceneSelectionWidth_ = 0, sceneSelectionHeight_ = 0;
    bool sceneSelectionOutline_ = false, selectionMaskPass_ = false;

    int shadowSize_ = 0;
    int highShadowSize_ = 2048;
    float shadowTexelWorld_ = 0.05f;   // world size of one shadow-map texel
    float shadowExtent_ = 50.0f;       // world size covered by the shadow map
    bool hasShadowBounds_ = false;
    float3 shadowBoundsMin_{}, shadowBoundsMax_{};
    ShadowQuality quality_ = ShadowQuality::Medium;
    std::vector<DrawItem> items_;
    std::vector<ColorVertex> lines_, tris_;
    std::vector<CustomDraw> custom_, customOpaque_, customShadow_;
    std::vector<CustomDraw> customMaterialReceiver_, customSurfaceReceiver_;
    ObjectMotionProducer objectMotionProducer_; // optional PRISMA producer bridge
    RTSCamera cam_;
    mat4 viewProj_, lightViewProj_;
    float3 camPos_;
    uint32_t drawCalls_ = 0;
    bool drawCallDebug_ = false;
    int studioDebugMode_ = 0;
    uint32_t debugMainDrawId_ = 0;
    static float4 drawCallDebugColor(uint32_t drawId);
    int sceneW_ = 0, sceneH_ = 0;
    PostProcess* post_ = nullptr;      // set by render() when the hook is active, cleared by resolvePostFx()
    gfx::PassDesc postOut_;

    // trace tier
    bool traceWanted_ = true;
    bool ssrThisFrame_ = false;
    // NEREID: only the selected water plane can sample this frame's planar RTT.
    // Reset every beginScene; secondary elevations use SSR/environment only.
    bool reflectionPlaneValid_ = false;
    float reflectionPlaneY_ = 0.f;
    bool modelReflections_ = false; // opt-in opaque PBR SSR; High trace + HDR only
    bool hdrFrame_ = false;
    bool lumaMaterialCapture_ = false; // opt-in PBR material receiver only
    bool lumaSurfaceCapture_ = false;  // opt-in signed normal.xyz + roughness.w
    HeightField heightField_;
    struct SceneTarget { int w = 0, h = 0; bool hdr = false; gfx::TextureHandle color, depth; uint64_t lastUse = 0; };
    std::vector<SceneTarget> sceneTargets_;   // one per view size (editor: Scene + Game views)
    gfx::TextureHandle sceneColor_, sceneDepth_;   // this frame's capture
    gfx::TextureHandle dummyDepth_, dummyHeight_;
    double graphEpoch_ = -1;
    float graphTime_ = 0;
    uint64_t traceFrame_ = 0;
    gfx::ShaderHandle blitShader_;
    gfx::PipelineHandle blitPipe_;
    gfx::BufferHandle blitVB_;
    mat4 invViewProj_;
    float3 camFwd_;

    // models
    ModelRenderStats modelStats_;
    // Fixed storage persists across beginScene() without allocating on the
    // render hot path; full scene invalidation/shutdown retires this history.
    struct CpuMdiRollingHistory {
        std::array<std::array<double, 8>, kCpuMdiRollingWindow> samples{};
        size_t next = 0;
        size_t count = 0;
    };
    std::array<CpuMdiRollingHistory, 2> cpuMdiRolling_{};
    void recordCpuMdiStageSample(
        size_t phase, const ModelRenderStats::CpuMdiStageProfile& before);
    gfx::BufferHandle indirectCommands_;
    // Reuse command storage across submissions; only contents/size change.
    std::vector<gfx::DrawIndexedIndirectCommand> indirectCommandScratch_;
    // Experimental persistent indirect command layout. Cache entries are
    // validated against the full per-item geometry signature on every use.
    struct IndirectLayoutItem {
        // Borrowed only while the asset is loaded. Model unload explicitly
        // retires dependent layouts before these pointers can be recycled.
        const Model* model = nullptr;
        uint64_t geometryRevision = 0;
        uint32_t vertexBuffer = 0, indexBuffer = 0;
        const void* primitive = nullptr;
        uint32_t indexCount = 0, firstIndex = 0, gpuFirstIndex = 0, gpuBaseVertex = 0;
        bool operator==(const IndirectLayoutItem&) const = default;
    };
    struct IndirectLayoutEntry {
        std::vector<IndirectLayoutItem> signature;
        std::vector<gfx::DrawIndexedIndirectCommand> commands;
        uint64_t triangles = 0;
        uint64_t lastUsedScene = 0; // ageing across beginScene epochs
    };
    // There are five model phases (shadow, opaque, blended, outline, depth).
    // Limit both positional entries and per-entry signature size: at most
    // 96 * 5 * 256 retained item signatures/commands across all phases.
    // Larger scenes still render normally using indirectCommandScratch_.
    static constexpr size_t kMaxMdiLayoutsPerPhase = 96;
    static constexpr uint32_t kMaxMdiCachedItems = 256;
    static constexpr uint64_t kMdiLayoutIdleScenes = 120;
    std::vector<IndirectLayoutEntry> indirectLayoutCache_[5];
    size_t indirectLayoutCursor_[5] = {};
    uint64_t indirectLayoutSceneEpoch_ = 0;
    uint64_t indirectLayoutsRetired_ = 0;
    void clearIndirectLayoutCaches();
    void retireIndirectLayoutsFor(const Model* model);
    void maintainIndirectLayoutCaches();
    bool modelMultiDrawEnabled_ = true;
    // Optional CPU MDI sorter for static rigid meshes sharing one GPU arena.
    // Never bypass per-object culling, LOD, material or shader compatibility.
    bool staticCrossModelBatchesEnabled_ = false;
    StaticCrossModelBatchMode staticCrossModelBatchMode_ =
        StaticCrossModelBatchMode::Environment;
    CpuMdiPolicyMode cpuMdiAdaptiveMode_ = CpuMdiPolicyMode::Environment;
    CpuMdiPolicyMode mdiLayoutCacheMode_ = CpuMdiPolicyMode::Environment;
    CpuMdiPolicyMode mdiGeometrySortMode_ = CpuMdiPolicyMode::Environment;
    double cpuMdiAdaptiveBudgetOverrideUs_ = 0.0; // 0: env (default 25 us)
    uint64_t cpuMdiAdaptiveScope_ = 0; // default namespace, not an Entity ID
    bool mdiLayoutCacheEnabled_ = true;
    bool mdiGeometrySortRequested_ = false;
    // Per-renderer reusable hash directory, group storage and item mode map.
    // All records are invalidated at each independent render pass.
    StaticCrossModelPreflightWorkspace staticCrossPreflight_;
    bool cpuMdiAdaptiveEnabled_ = false;
    double cpuMdiAdaptiveBudgetUs_ = 25.0;
    // Aggregate display-only diagnostics, not used to suppress any page.
    std::array<CpuMdiAdaptiveFeedback, 2> cpuMdiAdaptive_{};
    AutoCpuBatchPolicy autoCpuBatch_{}; // reset with world/cache invalidation
    struct CpuMdiPageEntry {
        std::weak_ptr<GeometryPoolPage> lifetime;
        std::array<CpuMdiAdaptiveFeedback, 2> byPass{};
        uint64_t seen = 0;
        // A page receives at most one adaptive decision per render-scene
        // epoch and pass, regardless of how many material groups it contains.
        std::array<uint64_t, 2> decisionEpoch{};
        std::array<bool, 2> permitted{true, true};
    };
    std::unordered_map<CpuMdiPageKey, CpuMdiPageEntry, CpuMdiPageKeyHash>
        cpuMdiPages_;
    uint64_t cpuMdiPageEpoch_ = 0;
    CpuMdiPageStats cpuMdiPageStats_{};
    CpuMdiPageEntry* cpuMdiPageEntry(const Model& model);
    bool gpuAuthoredDrawEnabled_ = true;
    bool modelCullingEnabled_ = true, modelInstancingEnabled_ = true;
    bool auxiliaryModelPass_ = false;
    bool profiling_=false,modelLodEnabled_=true;
    uint64_t lodDebugTarget_ = 0;
    bool lodDiagnosticsEnabled_ = false;
    LodDebugSample lodDebugSample_;
    float modelLodFadeWidth_=.15f;
    RenderProfile profile_;
    uint32_t instanceLimit_=32;
    GeometryStats geometryStats_;
    GeometryOcclusion geometryOcclusion_;
    void prepareOcclusion();
    void drawGeometryBatch(const uint32_t* items, uint32_t count, int phase);
    bool geometryMainView_ = true;
    int geometryShadowSize_=0;
    MaterialQuality matQuality_ = MaterialQuality::High;
    std::vector<ModelItem> modelItems_;
    // Immutable rigid packet templates; views / shadows / depth are replayed
    // for the current camera. Stored items own no GPU or Model resources.
    struct StaticPacketEntry {
        ModelDraw draw;
        uint64_t geometryRevision = 0;
        uint64_t materialRevision = 0;
        // 0: conservative per-packet GPU-state equality. Nonzero: shared
        // versioned-owner signature, checked via a monotonic renderer serial.
        uint64_t sharedMaterialSerial = 0;
        uint32_t vertexBuffer = 0, indexBuffer = 0;
        uint64_t seen = 0;
        std::vector<Material> materialsSnapshot;
        std::vector<uint64_t> materialHashes;
        std::vector<int> selectedMeshes; // by node index; -1 = node without mesh
        // Node-level material assignments may be owned by a transient caller.
        // Compare a value snapshot on replay; never retain vector pointers.
        bool hasNodeMaterialOverride = false;
        std::vector<int> nodeMaterialSnapshot;
        std::vector<ModelItem> items;
    };
    static size_t staticPacketBytes(const StaticPacketEntry& packet) {
        return packet.items.size() * sizeof(ModelItem) +
            packet.materialsSnapshot.size() * sizeof(Material) +
            packet.materialHashes.size() * sizeof(uint64_t) +
            packet.selectedMeshes.size() * sizeof(int) +
            packet.nodeMaterialSnapshot.size() * sizeof(int);
    }
    // Reused per-draw scratch: hot packet replay must not allocate one vector
    // for every visible entity. Renderer3D submission is non-reentrant.
    struct StaticPacketNodeState {
        bool inView = false, inShadow = false;
        int mesh = -1;
        size_t level = 0;
        bool transitioning = false;
    };
    std::vector<StaticPacketNodeState> staticPacketNodeScratch_;
    // Immutable GPU material signatures shared by many static draw packets.
    // Key is an explicit owner token scoped to a Model, never Material* or a
    // temporary vector allocation address. Revisions are owner supplied.
    struct MaterialOwnerKey {
        const Model* model = nullptr;
        uint64_t ownerKey = 0;
        bool operator==(const MaterialOwnerKey&) const = default;
    };
    struct MaterialOwnerKeyHash {
        size_t operator()(const MaterialOwnerKey& p) const {
            const size_t a = std::hash<const Model*>{}(p.model);
            const size_t b = std::hash<uint64_t>{}(p.ownerKey);
            return a ^ (b + size_t(0x9e3779b9) + (a << 6) + (a >> 2));
        }
    };
    struct SharedMaterialEntry {
        uint64_t ownerRevision = 0, modelMaterialRevision = 0;
        uint64_t geometryRevision = 0, serial = 0, seen = 0;
        size_t count = 0;
        int minVertexOffsetQuality = 2147483647; // INT_MAX = no displacement
        std::vector<uint64_t> hashes; // resolved GPU state, computed once/revision
    };
    std::unordered_map<MaterialOwnerKey, SharedMaterialEntry, MaterialOwnerKeyHash>
        staticMaterialOwners_;
    uint64_t staticMaterialOwnerSerial_ = 0;
    uint64_t staticMaterialOwnerEpoch_ = 0;
    size_t staticMaterialOwnerBytes_ = 0;
    SharedMaterialStateStats sharedMaterialStateStats_{};
    const SharedMaterialEntry* cachedSharedMaterialState(
        const Model& model, const ModelDraw& draw,
        const std::vector<Material>& materials);
    std::unordered_map<uint64_t, StaticPacketEntry> staticRenderPackets_;
    size_t staticRenderPacketBytes_ = 0;
    StaticRenderPacketStats staticRenderPacketStats_{};
    bool staticRenderPacketsEnabled_ = false;
    uint64_t staticRenderPacketEpoch_ = 0;
    struct StaticCacheEntry {
        StaticModelCache cache;
        uint64_t seen = 0;
        uint32_t vertexBuffer = 0, indexBuffer = 0;
        uint64_t geometryRevision = 0;
    };
    std::unordered_map<uint64_t, StaticCacheEntry> staticModelCaches_;
    uint64_t staticModelCacheEpoch_ = 0;
    bool staticModelCacheEnabled_ = false;
    StaticModelCacheStats staticModelCacheStats_{};
    struct StaticEligibilityEntry {
        ModelStaticEligibility eligibility{};
        uint64_t geometryRevision = 0;
        uint64_t seen = 0;
        uint32_t vb = 0, ib = 0;
        size_t nodes = 0, meshes = 0, bindNodes = 0;
        size_t skins = 0, materials = 0;
        const GeometryData* geometry = nullptr;
    };
    std::unordered_map<const Model*, StaticEligibilityEntry> staticModelEligibility_;
    uint64_t staticEligibilityEpoch_ = 0;
    StaticEligibilityStats staticEligibilityStats_{};
    ModelStaticEligibility cachedStaticModelEligibility(const Model& source);
    // Material GPU-state equivalence is pass-independent within one scene.
    // Resolve each distinct (model, material) once for shadows, colour,
    // reflection and outline. All containers are cleared at beginScene(),
    // retaining capacity to avoid per-pass allocations. Distinct pointers
    // still require exact GPU-state equivalence, never pointer-only batching.
    struct MaterialStatePointer {
        const Material* material = nullptr;
        const Model* model = nullptr;
        bool operator==(const MaterialStatePointer&) const = default;
    };
    struct MaterialStatePointerHash {
        size_t operator()(const MaterialStatePointer& p) const {
            const size_t a = std::hash<const Material*>{}(p.material);
            const size_t b = std::hash<const Model*>{}(p.model);
            return a ^ (b + size_t(0x9e3779b9) + (a << 6) + (a >> 2));
        }
    };
    std::unordered_map<MaterialStatePointer, uint32_t, MaterialStatePointerHash> materialStateIds_;
    std::unordered_map<uint64_t, std::vector<uint32_t>> materialHashCandidates_;
    std::vector<uint32_t> materialRepresentatives_;
    std::vector<uint32_t> materialItemIds_; // UINT32_MAX = unclassified
    // Opt-in actual GPU-authored draw commands for static opaque primitives.
    GpuSceneDrawCommands gpuModelCommands_;
    std::vector<GpuPrimitiveDrawRecord> gpuModelRecords_;
    std::vector<uint32_t> gpuModelSlots_;
    bool gpuModelCommandsReady_ = false;
    // Adaptive GPU ICB may choose the baseline instancing renderer before
    // expensive material grouping and GPU command preparation.
    bool gpuAdaptiveInstancingFallback_ = false;
    uint32_t gpuPreflightGroups_ = 0;
    uint32_t gpuBatchCacheHits_ = 0, gpuBatchCacheMisses_ = 0;
    struct GpuBatchPlanItem {
        const Model* model = nullptr;
        const Material* material = nullptr;
        uint32_t itemIndex = 0, flags = 0, vb = 0, ib = 0;
        uint32_t bakedTex = 0, morphTex = 0;
        uint64_t materialHash = 0;
        bool operator==(const GpuBatchPlanItem&) const = default;
    };
    struct GpuBatchPlan {
        std::vector<GpuBatchPlanItem> signature;
        std::vector<std::vector<uint32_t>> groups;
    };
    GpuBatchPlan gpuBatchPlan_;
    uint64_t gpuModelFrame_ = 0;
    size_t gpuModelSubmission_ = 0;
    struct GpuEncodedBatch {
        std::vector<uint32_t> items;
        uint32_t token = 0;
    };
    std::vector<GpuEncodedBatch> gpuEncodedBatches_;
    // Retained CPU packing scratch, never shared with the in-flight GPU buffers.
    std::vector<uint32_t> gpuIcbSourceSlotsScratch_;
    std::vector<float> gpuIcbInstancesScratch_;
    // Conservative automatic CPU/GPU authoring: previous successful batches
    // are feedback, not a GPU-duration estimate.
    uint32_t gpuIcbAutoMinimum_ = 64;
    uint32_t gpuIcbAutoMisses_ = 0;
    std::vector<uint8_t> gpuEncodedCovered_;
    uint32_t gpuEncodedDrawCalls_ = 0, gpuEncodedCommands_ = 0;
    uint32_t gpuIcbCandidates_ = 0, gpuIcbPrepared_ = 0;
    uint32_t gpuIcbRejected_ = 0, gpuIcbExecuteFailed_ = 0;
    uint32_t gpuModelEligible_ = 0, gpuModelDrawn_ = 0;
    uint32_t gpuModelDynamicEligible_ = 0, gpuModelLodEligible_ = 0;
    PersistentGpuScene persistentScene_;
    std::unique_ptr<PersistentGpuSceneUpload> persistentUpload_;
    GpuSceneCulling persistentCull_;
    struct PersistentEntry { GpuSceneHandle handle; uint64_t seen = 0; };
    std::unordered_map<uint64_t, PersistentEntry> persistentIds_;
    uint64_t persistentSceneEpoch_ = 0;
    PersistentSceneStats persistentStats_{};
    bool persistentSceneEnabled_ = false;
    void recordPersistentModel(const ModelDraw& d);
    void syncPersistentModels();
    std::vector<float> jointData_, morphData_, instData_;
    const ModelItem* lastItem_ = nullptr;   // state already bound (material, model textures, palette)
    std::vector<mat4> palette_;
    std::vector<uint32_t> order_;
    std::vector<std::pair<uint32_t,uint32_t>> geometryRuns_;
    struct ModelShaderKey {
        uint32_t variant;
        std::shared_ptr<const graph::Program> graph;
        bool operator==(const ModelShaderKey& other) const {
            if (variant != other.variant) return false;
            if (graph == other.graph) return true;
            return graph && other.graph && graph->glsl == other.graph->glsl && graph->vertexGlsl == other.graph->vertexGlsl && graph->textures.size() == other.graph->textures.size() && graph->outputs == other.graph->outputs;
        }
    };
    struct ShaderEntry { gfx::ShaderHandle shader; bool failed = false; gfx::Device::ShaderJob job; };
    bool warmingPipelines_ = false, warmingPending_ = false;
    std::vector<std::pair<ModelShaderKey, gfx::Device::PipelineJob>> pendingModelPipes_;
    std::vector<std::pair<ModelShaderKey, ShaderEntry>> modelShaders_;
    std::vector<std::pair<ModelShaderKey, gfx::PipelineHandle>> modelPipes_;
    gfx::TextureHandle white_, black_, flatNormal_;
    gfx::PipelineHandle currentPipe_;
    std::string modelError_;
    // baked lighting (Renderer3DBaked.cpp)
    const BakedLighting* baked_ = nullptr;
    const GiDynamicProbeTexture* dynamicGiOverlay_ = nullptr;
    const GiClipmapValidityTexture* dynamicGiValidity_ = nullptr;
    const GiDynamicProbeTexture* dynamicGiFarOverlay_ = nullptr;
    const GiDynamicProbeTexture* dynamicGiThirdOverlay_ = nullptr;
    float4 dynamicGiOrigin_{};
    float4 dynamicGiSize_{};
    float4 dynamicGiRing_{};
    float4 dynamicGiFarOrigin_{};
    float4 dynamicGiFarSize_{};
    float4 dynamicGiFarRing_{};
    float4 dynamicGiThirdOrigin_{};
    float4 dynamicGiThirdSize_{};
    float4 dynamicGiThirdRing_{};
    BakedQuality bakedQuality_ = BakedQuality::High;
    uint32_t bakedLightmapped_ = 0, bakedProbed_ = 0, bakedCountLm_ = 0, bakedCountPr_ = 0;
    ModelCapture capture_;
    void bakedItem(ModelItem& it, const ModelDraw& d, int node);
    void bindBakedGI(const ModelItem& it);
    bool dynamicGiEligible(const ModelItem& it, int phase, bool gpuInstances, bool instancePalette) const;
};

} // namespace aiverse::render
