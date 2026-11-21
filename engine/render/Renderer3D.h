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
#include <array>
#include <span>
#include <cstdint>
#include <functional>
#include <utility>
#include <string>
#include <vector>
#include <deque>
#include "../gfx/Gfx.h"
#include "../gfx/VirtualTextureFeedbackPass.h"
#include "Lights.h"
#include "Math3D.h"
#include "NereidFlowBinding.h"
#include "PrismaMotionTextureInput.h"
#include "PrismaGpuRetirement.h"
#include "Model.h"
#include "ModelVirtualTextureSampling.h"
#include "GpuMaterialResourceTable.h"
#include "ModelStaticEligibility.h"
#include "CpuMdiAdaptivePolicy.h"
#include "AutoCpuBatchPolicy.h"
#include "StaticCrossModelBatchPolicy.h"
#include "PersistentGpuSceneUpload.h"
#include "GpuSceneCulling.h"
#include "GpuSceneDrawCommands.h"
#include "RenderDeviceLifetime.h"
#include "GpuProgramRetirement.h"
#include "WindRigShader.h"
#include <memory>
#include <utility>
#include <map>
#include <tuple>
#include <array>
#include <unordered_map>

namespace aiverse::render {
namespace strata {
    struct ModelAsset;
    class GpuMetadataScene;
    class GpuClusterCulling;
}

class GiMainGpuBridge;
class GiMainCpuBridge; // optional per-scene CPU-bounced-light producer

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

// Post-processing hook (engine/render/Prisma.h; PostFx.h is legacy-compatible). With an active hook, render() draws
// the scene into the hook's offscreen target instead of `screenPass`; resolvePrisma() then runs the
// effect chain into `screenPass` and leaves that pass open (UI and overlays drawn after it are not
// post-processed). Without a hook, or with no active effect, nothing changes and it costs nothing.
struct PostFrame { mat4 viewProj; float3 cameraPos; Lighting lighting; int width = 0, height = 0;
                   gfx::TextureHandle sunShadow; mat4 lightViewProj;
                   // Projection metadata for optional linear view-depth post FX.
                   // Ortho projection uses explicit -200..1000 clip bounds.
                   float depthNear = 0.5f, depthFar = 1000.0f;
                   bool orthographic = false;
                 }; // sunShadow: empty when shadows are off
class PostProcess {
public:
    virtual ~PostProcess() = default;
    virtual bool active() const = 0;
    virtual bool linearScene() const { return false; }
    // True only while the post chain is accumulating a reprojection-validated HDR history.
    // Shadow sample scrambling must never be enabled without a temporal resolve pass.
    virtual bool temporalShadowResolve() const { return false; }
    virtual bool wantsMaterialReceiverPass() const { return false; }
    virtual gfx::PassDesc materialReceiverPass(int,int) { return {}; }
    virtual void materialReceiverPublished(gfx::TextureHandle,int,int) {}
    // Optional HIGH-quality PBR normal+roughness receiver. Independent depth
    // attachment; producer must ensure it belongs to this same frame/view.
    virtual bool wantsSurfaceReceiverPass() const { return false; }
    virtual gfx::PassDesc surfaceReceiverPass(int,int) { return {}; }
    virtual void surfaceReceiverPublished(gfx::TextureHandle,int,int) {}
    virtual gfx::PassDesc scenePass(const gfx::PassDesc& out, int w, int h) = 0;
    virtual void resolve(gfx::Device& dev, const gfx::PassDesc& out, const PostFrame& frame) = 0;
    // Optional read-only presentation bridge for object motion vectors.
    virtual bool wantsObjectMotionVectors() const { return false; }
    virtual gfx::TextureHandle objectMotionSceneDepth() const { return {}; }
    // Monotonic per-view camera-cut epoch. Default hook remains inert.
    virtual uint64_t objectMotionEpoch(uint64_t viewId) const { (void)viewId; return 0; }
    virtual bool objectMotionContext(uint64_t& viewId, mat4& previousViewProj) const {
        (void)viewId; (void)previousViewProj; return false;
    }
    virtual void acceptObjectMotionVectors(gfx::TextureHandle) {}
    // Typed bridge: newer producers declare full-size signed RGBA16F.
    // By default, forward to old implementations for compatibility.
    virtual void acceptObjectMotionVectors(MotionTextureInput input) {
        acceptObjectMotionVectors(input.texture);
    }
    // Optional borrowed current-frame reactive coverage (R=0..1).
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
// Optional Stage 16.3 GPU-authored indexed selection packet. All shading,
// transforms, visibility and final draw ownership remain Renderer3D's.
// GeometryScene supplies this ONLY for validated one-primitive Vulkan
// sources, and keeps the buffer/metadata alive through the render epoch.
// Failure to consume this packet falls back to the same Legacy ModelDraw.
enum class GpuIndexedPass : uint8_t {
    MainOpaque=0, SunShadow=1, PlanarReflection=2, SpotShadow=3
};
struct GpuIndexedSelectionPacket {
    gfx::BufferHandle commands{};
    gfx::BufferHandle vertex{}, index{};
    mat4 selectedWorld{};
    // Each GPU selection has its own camera. Shadow or reflection passes
    // must NEVER consume another camera's main-view cluster list.
    mat4 viewProjection{};
    GpuIndexedPass pass=GpuIndexedPass::MainOpaque;
    int viewWidth=0, viewHeight=0;
    uint64_t frameSerial=0, geometryRevision=0, materialRevision=0;
    uint32_t commandCapacity=0, sourceFirstIndex=0, sourceIndexCount=0;
    // Source-local primitive identity, NOT GPU atomic output order.
    uint32_t sourceMesh=0, sourcePrimitive=0, expectedMaterial=0;
    int32_t sourceBaseVertex=0;
    // Borrowed index metadata/selector for JUST-IN-TIME spot/reflection
    // dispatch before those camera-specific graphics encoders begin.
    // GeometryScene must outlive Renderer3D::render() as for source Model.
    gfx::BufferHandle metadata{};
    strata::GpuClusterCulling* selector=nullptr;
    uint32_t instanceToken=0;
    float errorThresholdPixels=2.f;
    // Scene-staged GPU temporal history identity; the source Culling owner
    // holds per-instance/pass triple-buffer masks. Auxiliary cameras use
    // the same source revision but DIFFERENT pass IDs (3/4).
    float lodHysteresisFraction=0.f;
    uint32_t temporalNodeCount=0;
    uint64_t temporalRevision=0;
    bool allowSpotShadow=false, allowReflection=false;
    bool exclusive=false;
    bool valid() const {
        return exclusive && commands.valid() && vertex.valid() &&
               index.valid() && frameSerial && commandCapacity &&
               commandCapacity<=2048 && sourceIndexCount &&
               viewWidth>0 && viewHeight>0;
    }
    // Exact camera identity guard; sun / spot / reflection matrices must
    // NEVER share a cluster selection simply because they share geometry.
    bool matchesView(GpuIndexedPass expected,const mat4& projection,
                     int width,int height) const {
        if(!valid() || pass!=expected ||
           viewWidth!=width || viewHeight!=height)return false;
        for(unsigned i=0;i<16;++i)
            if(viewProjection.m[i]!=projection.m[i])return false;
        return true;
    }
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
    // Appended to preserve existing positional aggregate ModelDraw callers.
    GpuIndexedSelectionPacket gpuIndexed{};
    // Stage16.4a: one independent packet per original material/primitive.
    // Appended to keep every preexisting ModelDraw aggregate initializer
    // backwards compatible. Copies into renderer-owned ModelItems by value.
    std::vector<GpuIndexedSelectionPacket> gpuIndexedPrimitives;
    // Stage16.4b: distinct sun-shadow camera selections. Spot shadows and
    // water reflections retain CPU/Legacy until their own view epochs
    // can be selected before render passes.
    std::vector<GpuIndexedSelectionPacket> gpuShadowPrimitives;
    // Main branch ShaderGraph/source material selector; append to keep
    // all prior positional ModelDraw initializers backward-compatible.
    int primitiveMaterialFilter = -1;
    bool useVertexParameters = false;
    float4 vertexParameters{0, 0, 0, 0};
    // Bounded vertexOffset (metres), zero means unknown displacement.
    float maxVertexOffsetMetres = 0;
    // Host-chosen level of every node's MeshDef::lods chain (0 = base mesh,
    // n = lods[n-1], clamped). Skips projected-size selection and crossfade;
    // used by hosts with their own distance tiers (TerraWeave FloraView).
    // -1 = renderer picks by projected size.
    int lodLevel = -1;
    // Opt-in full ModelVertex (60 bytes) dynamic GPU stream for THIS instance.
    // Bind only on the same gfx::Device, after compute submission and before
    // render passes. Owner keeps resource alive through all colour/shadow draws.
    // Experimental path: Metal dedicated per-model buffers only; no pooled
    // geometry, GPU ICB/MDI, instancing, LOD or legacy shader morph blending.
    gfx::BufferHandle vertexOverride{};
    uint32_t vertexOverrideCount = 0;
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
    // Stage 16.3: main opaque, one-primitive Vulkan STRATA route only.
    uint64_t strataGpuCandidates=0, strataGpuSubmitted=0;
    uint64_t strataGpuRejected=0, strataGpuFallbackDraws=0;
    uint64_t strataGpuSunShadowSubmitted=0;
    uint64_t strataGpuSpotShadowSubmitted=0;
    uint64_t strataGpuReflectionSubmitted=0;
    uint64_t strataGpuSpotShadowPrepared=0;
    uint64_t strataGpuReflectionPrepared=0;
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

// Immutable draw parameters for simple geometry emitted by any scene host.
// Mesh GPU handles remain borrowed; the host owns their GPU retirement.
struct CameraGeometryCommand {
    enum class Kind : uint8_t { Mesh, Line, Triangle, Water, Selection };
    Kind kind=Kind::Mesh;
    const Mesh* mesh=nullptr;
    mat4 world{};
    float4 tint{1,1,1,1};
    uint32_t flags=0;
    float specular=.25f,emissive=0.f;
    float3 a{},b{},c{};
    uint32_t rgba=0xffffffffu;
    // The water and selection variants must preserve their additional
    // shader/outline parameters instead of being flattened to DrawMesh.
    std::array<float4,16> ripples{},wakes{};
    float4 wind{},hydraulics{},rapidColor{};
    uint32_t firstIndex=0,indexCount=0;
    float4 selection{};
    NereidFlowBinding waterFlow{};
};

class Renderer3D {
public:
    void setGpuDeviceLease(RenderDeviceLifetime::Lease lease) {
        gpuDeviceLease_=std::move(lease);
    }
    bool init(gfx::Device& dev);
    void shutdown();
    // Call only at a host frame boundary, outside any active graphics pass.
    // A Pending or Failed token never authorizes GPU program destruction.
    size_t pendingProgramRetirements() const noexcept {
        return programRetirement_?programRetirement_->pendingBatches():0;
    }
    bool collectRetiredPrograms() {
        if(!programRetirement_ || !dev_)return true;
        checkGpuDevice(*dev_);
        return programRetirement_->collect();
    }
    size_t pendingTargetRetirements() const noexcept {
        return targetRetirement_?targetRetirement_->pendingPairs():0;
    }
    uint64_t targetRetirementOrphanPairs() const noexcept {
        return (targetRetirement_?targetRetirement_->orphanedPairs():0) +
            unleasedTargetOrphans_;
    }
    // Exceptional use before shared target owner initialization: a failed
    // true-idle barrier never authorizes deleting still-in-flight textures.
    uint64_t unleasedTargetIdleFailures() const noexcept {
        return unleasedTargetIdleFailures_;
    }
    uint64_t targetRetirementEnqueueFailures() const noexcept {
        return targetRetirement_?targetRetirement_->enqueueFailures():0;
    }
    bool collectRetiredTargets() {
        if(!targetRetirement_ || !dev_)return true;
        checkGpuDevice(*dev_);
        return targetRetirement_->collect();
    }
    uint64_t programRetirementOrphanBatches() const noexcept {
        return programRetirement_?programRetirement_->orphanBatches():0;
    }
    // Optional fixed scene bounds for interior/model viewers; clear to use RTS ground fitting.
    void setShadowBounds(float3 lo, float3 hi) { shadowBoundsMin_=lo; shadowBoundsMax_=hi; hasShadowBounds_=true; }
    void clearShadowBounds() { hasShadowBounds_=false; }
    void setShadowQuality(ShadowQuality q);
    // Desktop/benchmark override of the High-tier map size (2048 default). Low/Medium keep their sizes.
    int shadowMapSize() const { return shadowSize_; }
    // Diagnostic-only asynchronous sun shadow DEPTH readback on backends
    // supporting gfx::Device::readDepthTextureAsync. Call after render()
    // and after the main colour pass is ended; never waits on the GPU.
    // A completed GPU failure arrives as an empty byte vector.
    // Does not expose or change shadow texture ownership.
    bool requestSunShadowDepthReadback(gfx::Device::DepthReadFn callback) const;
    // Diagnostic-only readback of the actual LDR planar water reflection
    // RTT (half-resolution). Not a main-screen screenshot or surrogate.
    // Call after render() and closing its main pass; the callback is async.
    bool requestPlanarReflectionColorReadback(gfx::Device::ColorReadFn callback) const;
    bool setShadowMapSize(int size) {
        highShadowSize_=size;
        setShadowQuality(quality_);
        // A failed allocation keeps the previous shadow texture alive;
        // report that the requested High resolution was not published.
        return quality_!=ShadowQuality::High ||
               (shadowMap_.valid() && shadowSize_==size);
    }
    ShadowQuality shadowQuality() const { return quality_; }
    static ShadowQuality defaultQualityFor(const gfx::Device& dev, bool mobile);

    // Shader presentation clock only; never changes simulation/current velocity.
    // Negative resumes the steady clock. Set before beginScene on the render thread.
    void setPresentationTime(float seconds);
    void beginScene(const Camera3D& cam, int fbW, int fbH);
    // Before render only: drop borrowed packets/callbacks of a failed host
    // preparation before destroying its private CPU owners. No RHI work.
    void discardSceneSubmission() noexcept;
    void drawMeshSnapshot(const Mesh& m, const mat4& model, float4 color, uint32_t flags = 0, float specular = 0.25f, float emissive = 0.0f);

    // Explicit per-view opt-in for CPU dynamic GI on ordinary PBR ModelDraws.
    // Call AFTER beginScene and BEFORE render. Shader Graph and GPU ICB paths
    // remain excluded; runtime never enables this for a view automatically.
    // The host retains the GI owner and its atlas until GPU completion.
    bool setDynamicGiOverlay(const GiMainCpuBridge& bridge);
    bool setDynamicGiOverlay(const GiMainGpuBridge& bridge);
    void clearDynamicGiOverlay() noexcept { dynamicGiAtlas_={}; }
    bool dynamicGiOverlayEnabled() const noexcept { return dynamicGiAtlas_.valid(); }
    // Generic custom terrain/foliage/mesh hook: call ONLY inside a valid
    // custom graphics draw, after its shader pipeline has been bound.
    // That shader must explicitly include GiDynamicShaders.h and declare
    // U6 + tex13 (GLSL) / buffer(7) + texture(13) (Metal). No implicit
    // material inference, no sampling during a shadow/depth-only pass.
    // The producer must stay alive through the GPU completion fence.
    bool bindDynamicGiForCustom() { return bindDynamicGiForCustomAt(13); }
    // Reserve the sampled atlas texture slot explicitly. Models use 13;
    // the stock high-terrain shaders already own 13 for cliff albedo,
    // so their Metal/Vulkan GI variant must use 21 instead.
    // Compact GLES3 terrain uses tex8 only when splat/baked/trace are off.
    bool bindDynamicGiForCustomAt(int textureSlot) {
        if(!dev_ || !dynamicGiAtlas_.valid() ||
           (textureSlot!=8 && textureSlot!=13 && textureSlot!=21))return false;
        dev_->setUniforms(6,dynamicGiUniforms_.data(),
                          dynamicGiUniforms_.size()*sizeof(float));
        dev_->setTexture(textureSlot,dynamicGiAtlas_);
        return true;
    }
    void drawMesh(const Mesh& m, const mat4& model, float4 color, uint32_t flags = 0, float specular = 0.25f, float emissive = 0.0f);
    // Bridge the current opaque immediate-mesh scene queue into VT feedback.
    // The caller supplies a compatible feedback pipeline and per-mesh
    // material binding. This does not cover ModelItem or custom renderers
    // (terrain) until their UV/material binding paths are integrated.
    using FeedbackMeshBinder = std::function<bool(
        gfx::Device&, const Mesh&, const mat4&)>;
    bool collectMeshFeedbackDraws(
        std::vector<gfx::VirtualTextureFeedbackDraw>& out,
        gfx::PipelineHandle pipeline,
        const FeedbackMeshBinder& bind) const {
        // Mesh VT feedback is optional: model and procedural producers can
        // submit feedback even when no immediate-mesh pipeline is installed.
        if (!pipeline.valid() || !bind) return true;
        for (const auto& item : items_) {
            if (!item.mesh || !item.mesh->vb.valid() ||
                !item.mesh->ib.valid() || !item.mesh->indexCount ||
                item.color.w < 0.99f) continue;
            const Mesh* mesh = item.mesh;
            const mat4 transform = item.model;
            gfx::VirtualTextureFeedbackDraw draw;
            draw.pipeline = pipeline;
            draw.vertexBuffer = mesh->vb;
            draw.indexBuffer = mesh->ib;
            draw.count = mesh->indexCount;
            draw.prepare = [bind,mesh,transform](gfx::Device& dev) {
                return bind(dev,*mesh,transform);
            };
            out.push_back(std::move(draw));
        }
        return true;
    }

    // Opt-in per-surface ripple normals. Positions are render-origin-relative
    // X/Z; z = propagating radius, w = signed amplitude. No new RHI buffers.
    void drawNereidRippleMesh(const Mesh& mesh,const mat4& model,float4 color,
                              float specular,float refraction,
                              const std::array<float4,16>& ripples,
                              uint32_t firstIndex=0,uint32_t indexCount=0,
                              const std::array<float4,16>& wakeShapes={},
                              float4 wind={},float4 hydraulics={},
                               float4 rapidColor={0.84f,0.93f,0.94f,1.f},
                               NereidFlowBinding flow={});
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
    enum class PipelineWarmStatus : uint8_t { Ready, Pending, Failed };
    // Strict authoring preflight distinguishes an asynchronous GPU compiler
    // still working from a rejected shader/pipeline. A rejected appearance
    // must retain its last-good Model/Material/AnimationGraph publication.
    PipelineWarmStatus warmModelPipelinesChecked(
        const Model& model,const std::vector<Material>* materials=nullptr);
    // Poll only jobs already staged by a previous candidate. This lets
    // editor hot-reload hold the last-good appearance without reimporting
    // scratch Models/textures every time a GPU compiler is still Pending.
    bool hasPendingModelPipelineJobs() const noexcept;
    bool pollPendingModelPipelineJobs();
    struct GpuWarmStats {
        size_t shaderJobs=0,pipelineJobs=0,colorVariants=0;
        uint64_t colorPollAttempts=0,colorVariantsReady=0,colorVariantsFailed=0;
    };
    GpuWarmStats gpuWarmStats() const noexcept;
    bool warmModelPipelines(const Model& model, const std::vector<Material>* materials = nullptr);
    void drawModel(const ModelDraw& d);
    // Diagnostic-only per-ModelDraw Strata preflight (legacy draws untouched).
    // Only GeometryScene's opt-in native static scene calls this in Stage 10.
    // No render pass, GPU commands, source cache or ECS state is mutated.
    struct StrataPreviewStats {
        uint64_t modelDraws=0, attempted=0, ready=0, rejected=0;
        uint64_t selectedClusters=0, fullyCulled=0, skippedUnsafeDraws=0;
    };
    void previewStrataModel(const ModelDraw& d, const strata::ModelAsset& staged,
                           const strata::GpuMetadataScene& pooled,
                           const std::string& sourceKey);
    StrataPreviewStats strataPreviewStats() const { return strataPreviewStats_; }
    // Optional VT sampling for built-in model material shaders. Resolver
    // returns the physical atlas/page-table domain for a given model and
    // material; it must only query existing GPU allocations (render thread).
    // Shader Graph materials keep their existing texture slot contract.
    using ModelVirtualTextureResolver = std::function<bool(
        const Model&, const Material&, ModelVirtualTextureBinding&)>;
    void setModelVirtualTextureResolver(ModelVirtualTextureResolver resolver) {
        modelVirtualTextureResolver_ = std::move(resolver);
    }
    void clearModelVirtualTextureResolver() {
        modelVirtualTextureResolver_ = {};
    }
    bool resolveModelVirtualTexture(const Model& model,
                                    const Material& material,
                                    ModelVirtualTextureBinding& out) const {
        out = {};
        if (!modelVirtualTextureResolver_ ||
            !modelVirtualTextureResolver_(model,material,out) ||
            !out.valid()) {
            out = {};
            return false;
        }
        return true;
    }

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
    // Hierarchical wind masks (WindRigShader.h). Scene wind drives every
    // rigged draw that does not supply its own vertexParameters; the debug
    // view (editors only) recolours rigged materials by part/depth/weight.
    void setSceneWind(const wind::SceneWind& value) { sceneWind_ = value; }
    const wind::SceneWind& sceneWind() const { return sceneWind_; }
    void setWindRigDebugView(wind::WindDebugView view) {
        if (view != windDebugView_) windDebugMaterials_.clear();
        windDebugView_ = view;
    }
    wind::WindDebugView windRigDebugView() const { return windDebugView_; }
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
    // Frame-local provenance of Renderer3D's ACTUAL drawModel geometry/LOD
    // decisions, taken after a source model was submitted to this scene.
    // It is pre-render submission data, NOT proof an eventual draw passed
    // GPU occlusion or pipeline compilation. Never cache pointers across
    // beginScene, asset republish or Device resource retirement.
    struct ModelViewPrimitive {
        const Model* model=nullptr;
        uint32_t nodeIndex=0,meshIndex=0,primitiveIndex=0;
        uint64_t drawInvocation=0; // one renderer drawModel() instance
        uint32_t firstIndex=0,indexCount=0;
        gfx::BufferHandle vertexOverride{};
        uint32_t vertexOverrideCount=0;
        bool visible=false,colourCandidate=false;
        bool gpuLodConditional=false,lodFade=false,batched=false;
        bool alternativeMeshLod=false;
        bool skinning=false,baked=false,shaderVertexOffset=false;
        bool translucent=false; // real colour-pass tint/material/flag classification
        bool customGpuStream=false;
        // Stage 7BA: exact sampled Mask state used by the normal colour
        // draw. No raw Material* escapes the frame. A later model/material
        // hot-reload must NOT silently alter VIVID's alpha/depth source.
        bool maskedMaterial=false,maskVertexColors=true;
        gfx::TextureHandle maskBaseTexture{};
        float maskAlpha=1.f,maskCutoff=.5f,maskUvRotation=0.f;
        float2 maskUvOffset{0,0},maskUvScale{1,1};
        mat4 world{};
    };
    struct ModelViewProvenance {
        uint64_t frameSerial=0;
        bool hasModel=false;
        bool complete=false; // false if renderer source view is unsuitable
        std::vector<ModelViewPrimitive> primitives;
    };
    // Source-owned introspection; no cached parallel picking scene database.
    // Does not dereference async data or issue RHI operations.
    ModelViewProvenance modelViewProvenance(const Model& model) const;
    // Stage 7AW: opt-in, frame-local record of *encoded main-colour*
    // indexed commands, not an extrapolation from staged ModelItems.
    // Only one queried model is tracked, avoiding a per-world audit cost.
    enum class ModelSubmissionKind : uint8_t {
        Direct, DirectInstanced, IndexedIndirect, GpuIndirect, GpuIcb
    };
    struct ModelSubmittedPrimitive {
        ModelViewPrimitive source{};
        ModelSubmissionKind kind=ModelSubmissionKind::Direct;
        uint8_t phase=0; // 1 opaque main, 2 blended, 4 transparent depth
        gfx::PipelineHandle pipeline{};
    };
    struct ModelSubmissionProvenance {
        uint64_t frameSerial=0;
        bool mainPassEncoded=false;
        bool truncated=false;
        std::vector<ModelSubmittedPrimitive> commands;
    };
    // Stage 7BA: at most 1 selected model + 4 candidate blockers.
    // Opt-in bounded, frame-local diagnostics; no global scene registry.
    // The producer supplies candidate model identities before staging
    // draws. Every unique pointer is borrowed only through this frame.
    static constexpr std::size_t kMaxTrackedSubmissionModels=5u;
    void trackModelSubmissionsMany(
        std::span<const Model* const> models) noexcept {
        trackedModelCount_=0;
        for(auto& model:trackedModels_)model=nullptr;
        modelSubmitted_.clear();
        modelSubmissionTruncated_=
            modelDrawInvocation_!=0 || modelSubmissionMainEncoded_ ||
            models.empty() || models.size()>trackedModels_.size();
        if(modelSubmissionTruncated_)return;
        for(const auto* model:models) {
            if(!model) {
                modelSubmissionTruncated_=true;
                return;
            }
            for(std::size_t i=0;i<trackedModelCount_;++i)
                if(trackedModels_[i]==model) {
                    modelSubmissionTruncated_=true;
                    return;
                }
            trackedModels_[trackedModelCount_++]=model;
        }
    }
    // Compatible with existing 7AW single and 7AZ paired callers.
    void trackModelSubmissions(const Model* model,
                               const Model* occluder=nullptr) noexcept {
        const std::array<const Model*,2> models{{model,occluder}};
        trackModelSubmissionsMany(
            std::span<const Model* const>(models.data(),
                                        occluder?2u:1u));
    }
    ModelSubmissionProvenance modelSubmissionProvenance(
        const Model& model) const;
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
        // Counts only successful Metal ICB executions, not GPU staging.
        uint32_t perCommandVtExecutions = 0, mixedVtExecutions = 0;
        // Host-accepted ICB submissions; actual GPU completion is async.
        uint32_t perCommandPbrSubmissions = 0, mixedPbrSubmissions = 0;
        uint32_t shaderGraphIcbSubmissions = 0, mixedGraphSourceSubmissions = 0;
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
                gpuModelCommandsReady_,
                gpuIcbPerCommandVtExecuted_, gpuIcbMixedVtExecuted_,
                gpuIcbPerCommandPbrSubmitted_, gpuIcbMixedPbrSubmitted_,
                gpuIcbGraphSubmitted_, gpuIcbMixedGraphSubmitted_};
    }
    void render(const gfx::PassDesc& screenPass, PostProcess* post = nullptr); // shadow pass + opens the main pass and draws everything
    // With a post hook: ends the scene pass, runs the effects into screenPass (left open). No-op otherwise.
    // Call after everything that belongs to the 3D scene (particles), before UI.
    void resolvePrisma(); // canonical image processing resolve; leaves UI pass open
    // Presentation-only camera cut / teleport / view replacement hooks.
    // Invalidating a view keeps GPU target allocations but discards all
    // previous transform and deformation history. No ECS mutation.
    void resetObjectMotionHistory(uint64_t viewId);
    void resetAllObjectMotionHistory();
    // Explicit view closure after GPU-idle synchronization. NEVER call while
    // submissions referencing the view textures can still be in flight.
    void releaseObjectMotionViewAfterGpuIdle(uint64_t viewId);
    // Optional non-blocking GPU-lifetime path. Fence tokens MUST be issued
    // by the backend for the last GPU submission that may use a texture.
    // Never use CPU frameSerial() or a guessed frame delay as completion.
    void setObjectMotionSubmittedFence(uint64_t lastSubmittedFence) {
        motionLastSubmittedFence_ = lastSubmittedFence;
    }
    void retireObjectMotionView(uint64_t viewId, uint64_t lastSubmittedFence = 0);
    // Call at the end of a host frame with ALL live presentation camera IDs.
    // Retire orphaned per-view motion targets behind backend submission fence
    // (or quarantine until confirmed idle if the backend supplies no token).
    // Cameras sharing pixel dimensions remain independent by ID.
    size_t retireUnregisteredObjectMotionViews(
        const std::vector<uint64_t>& liveViewIds);
    size_t collectCompletedObjectMotion(uint64_t completedGpuFence);
    // Valid only after the host has explicitly confirmed GPU idle.
    size_t drainObjectMotionAfterGpuIdle();
    struct ObjectMotionStats {
        uint64_t viewId = 0;
        uint32_t rigidInstances = 0;
        uint32_t rigidDraws = 0;
        uint32_t cutoutDraws = 0;
        uint32_t skinnedDraws = 0;
        uint32_t bakedDraws = 0;
        uint32_t reactiveDraws = 0;
        uint32_t reactiveAnimatedDraws = 0;
        uint32_t reactiveCallbacks = 0; // invoked by the current-frame mask pass
        uint32_t reactiveCallbacksDropped = 0; // scene/post/depth or pipeline unavailable
        uint32_t reactiveMeshDraws = 0; // includes RTS water sheets
        uint32_t cutoutReactiveDraws = 0; // per-pixel alpha cutout on rigid/animated
        uint32_t dissolveReactiveDraws = 0; // current-deformed object-space discard
        uint32_t actorRevealReactiveDraws = 0; // camera-local aperture mask
        uint32_t lodReactiveDraws = 0;
        uint32_t rejected = 0;
        bool submitted = false; // actual object velocity draw, NEVER reactive-only
        bool velocitySeedOnly = false; // cut/first/resize, do not fabricate prior pose
        bool reactiveSubmitted = false; // independent current-frame mask encoded
    };
    const ObjectMotionStats& objectMotionStats() const { return objectMotionStats_; }
    struct MotionRetirementStats {
        size_t liveViews = 0;
        size_t fencedTextures = 0;
        size_t unfencedTextures = 0;
        uint64_t knownPendingBytes = 0;
        uint64_t lastGpuCompletedFence = 0;
    };
    MotionRetirementStats motionRetirementStats() const;
    // Read-only lower-bound of GPU resources owned DIRECTLY by Renderer3D.
    // Unique texture/buffer handles are counted once, including scene
    // capture aliases and per-view motion buffers; deferred motion is
    // reported separately. Excludes externally owned Model/Material assets,
    // External Model/Material assets and PRISMA are always excluded;
    // LightSystem, scene upload, culling and GPU-authored pools are included
    // via their exclusive owner telemetry, never through borrowed aliases.
    struct ResidentMemoryStats {
        uint64_t liveTextureKnownBytes = 0;
        uint64_t liveBufferKnownBytes = 0;
        uint64_t sceneCaptureKnownBytes = 0;
        uint64_t objectMotionKnownBytes = 0;
        uint64_t pendingMotionKnownBytes = 0;
        // Subowners not part of the direct Renderer3D handle list; each
        // owns disjoint allocations and must not include borrowed assets.
        uint64_t lightSystemKnownBytes = 0;
        uint64_t persistentUploadKnownBytes = 0;
        uint64_t persistentCullKnownBytes = 0;
        uint64_t gpuAuthoredKnownBytes = 0;
        size_t liveTextures = 0, liveBuffers = 0;
        size_t unknownTextures = 0, unknownBuffers = 0;
        size_t multiVersionBuffers = 0;
    };
    ResidentMemoryStats residentMemoryStats() const;
    // Generic presentation-only reactive-mask hook for water, particles and
    // user render modules. Register AFTER beginScene() and BEFORE resolvePrisma().
    // Called once with the reactive RGBA8 render pass already open. The callback
    // only submits draws (do not beginPass/endPass), writes R=coverage [0..1],
    // binds its own pipeline/buffers and may sample the READ-ONLY scene depth.
    // Prefer blending which preserves earlier coverage; keep all owned GPU
    // resources alive through resolvePrisma. Zero callbacks => no extra pass.
    using MotionReactiveDraw = std::function<void(gfx::Device&, gfx::TextureHandle sceneDepth,
                                                   int width, int height)>;
    void addMotionReactiveDraw(MotionReactiveDraw fn) {
        if (fn) motionReactiveDraws_.push_back(std::move(fn));
    }
    void resolvePostFx() { resolvePrisma(); } // legacy API
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
    void drawCustomOpaque(CustomDraw fn) { customOpaque_.push_back(std::move(fn)); }
    // Optional rigid-model feedback. Animated/skinned/morphed model
    // primitives require a matching deformation pipeline and are excluded
    // until such a pipeline is registered. Callback supplies UV/material
    // uniforms for the exact visible primitive and world transform.
    using FeedbackModelBinder = std::function<bool(
        gfx::Device&, const Model&, const Primitive&, const Material&,
        const mat4&)>;
    void setVirtualTextureModelFeedback(gfx::PipelineHandle pipeline,
                                        FeedbackModelBinder binder) {
        feedbackModelPipeline_ = pipeline;
        feedbackModelBinder_ = std::move(binder);
    }
    void clearVirtualTextureModelFeedback() {
        feedbackModelPipeline_ = {};
        feedbackModelBinder_ = {};
        clearVirtualTextureMaskedModelFeedback();
        clearVirtualTextureLodModelFeedback();
        clearVirtualTextureMaskedLodModelFeedback();
        clearVirtualTextureDeformedModelFeedback();
    }

    // Optional material-aware pipeline for alpha-masked rigid models.
    // This pipeline MUST discard fragments using the same alpha texture,
    // UV transform and cutoff as the regular model material shader.
    // Without this explicit pipeline, masked geometry is excluded.
    void setVirtualTextureMaskedModelFeedback(
        gfx::PipelineHandle pipeline, FeedbackModelBinder binder) {
        feedbackMaskedModelPipeline_ = pipeline;
        feedbackMaskedModelBinder_ = std::move(binder);
    }
    void clearVirtualTextureMaskedModelFeedback() {
        feedbackMaskedModelPipeline_ = {};
        feedbackMaskedModelBinder_ = {};
    }

    // Optional LOD-crossfade feedback variant. The renderer must use the
    // exact same per-fragment dither/coverage function and thresholds as
    // its visible model pass; otherwise the pages would be overrequested.
    // The callback receives the incoming/outgoing selection explicitly.
    // RHI fragments use pixel centers; preserve alignment with the scene
    // 4x4 ordered-dither grid when feedback and scene sizes differ.
    struct FeedbackPixelGrid {
        float scaleX = 1.0f, scaleY = 1.0f;
        float offsetX = 0.0f, offsetY = 0.0f;
    };
    FeedbackPixelGrid feedbackPixelGrid(const gfx::VirtualTextureFeedbackPass& pass) const {
        const float sx = pass.width() > 0 && sceneW_ > 0 ?
            float(sceneW_) / float(pass.width()) : 1.0f;
        const float sy = pass.height() > 0 && sceneH_ > 0 ?
            float(sceneH_) / float(pass.height()) : 1.0f;
        return {sx, sy, 0.5f * (1.0f - sx), 0.5f * (1.0f - sy)};
    }
    using FeedbackLodBinder = std::function<bool(
        gfx::Device&, const Model&, const Primitive&, const Material&,
        const mat4&, float threshold, bool invert,
        const FeedbackPixelGrid&)>;
    void setVirtualTextureLodModelFeedback(
        gfx::PipelineHandle pipeline, FeedbackLodBinder binder) {
        feedbackLodModelPipeline_ = pipeline;
        feedbackLodModelBinder_ = std::move(binder);
    }
    void clearVirtualTextureLodModelFeedback() {
        feedbackLodModelPipeline_ = {};
        feedbackLodModelBinder_ = {};
    }

    // Combined alpha-test + LOD dither feedback variant. The binder must
    // reproduce both the base-color alpha cutoff and screen-space LOD
    // coverage of the main material shader.
    void setVirtualTextureMaskedLodModelFeedback(
        gfx::PipelineHandle pipeline, FeedbackLodBinder binder) {
        feedbackMaskedLodModelPipeline_ = pipeline;
        feedbackMaskedLodModelBinder_ = std::move(binder);
    }
    void clearVirtualTextureMaskedLodModelFeedback() {
        feedbackMaskedLodModelPipeline_ = {};
        feedbackMaskedLodModelBinder_ = {};
    }

    // Deformed model feedback is submitted through a dedicated callback,
    // because skinning palettes, baked animation and morph targets use
    // distinct vertex layouts and GPU resources. The callback must draw
    // the same deformed silhouette as the visible model pass.
    struct FeedbackDeformedModel {
        const Model& model;
        const Primitive& primitive;
        const Material& material;
        const mat4& world;
        int32_t jointOffset;
        int32_t morphOffset;
        bool baked;
        float4 animation;
        float lodThreshold;
        bool lodInvert;
        uint32_t firstIndex;
        uint32_t indexCount;
        uint32_t gpuFirstIndex;
        int32_t gpuBaseVertex;
        FeedbackPixelGrid pixelGrid;
        const std::vector<float>& jointData;
        const std::vector<float>& morphData;
    };
    using FeedbackDeformedModelDraw = std::function<bool(
        gfx::Device&, const FeedbackDeformedModel&)>;
    void setVirtualTextureDeformedModelFeedback(
        FeedbackDeformedModelDraw draw) {
        feedbackDeformedModelDraw_ = std::move(draw);
    }
    void clearVirtualTextureDeformedModelFeedback() {
        feedbackDeformedModelDraw_ = {};
    }

    // Per-scene feedback producers for terrain, foliage and procedural
    // renderers that do not use the immediate Mesh queue. Each producer
    // binds its own compatible feedback pipeline, geometry and uniforms.
    // Unlike the main scene's CustomDraw, these callbacks execute only
    // when renderVirtualTextureFeedback() is explicitly invoked.
    using FeedbackCustomDraw = std::function<bool(gfx::Device&)>;
    void drawCustomFeedback(FeedbackCustomDraw fn) {
        if (fn) customFeedback_.push_back(std::move(fn));
    }

    // Must be called after beginScene/geometry submission and outside
    // any render pass. This combines the regular opaque mesh queue with
    // custom terrain/foliage feedback producers in one depth-tested pass.
    // ModelItem geometry still requires its own feedback bridge.
    bool renderVirtualTextureFeedback(
        gfx::VirtualTextureFeedbackPass& pass,
        gfx::PipelineHandle meshPipeline,
        const FeedbackMeshBinder& bind) {
        std::vector<gfx::VirtualTextureFeedbackDraw> meshes;
        if (!collectMeshFeedbackDraws(meshes,meshPipeline,bind)) return false;
        const auto pixelGrid = feedbackPixelGrid(pass);
        return pass.renderAndCapture([&](gfx::Device& device) {
            for (const auto& draw : meshes) {
                device.setPipeline(draw.pipeline);
                device.setVertexBuffer(draw.vertexBuffer,draw.vertexOffset);
                device.setIndexBuffer(draw.indexBuffer,draw.indexOffset);
                if (draw.prepare && !draw.prepare(device)) return false;
                device.drawIndexed(draw.count,draw.first,draw.baseVertex,draw.instances);
            }
            if ((feedbackModelPipeline_.valid() && feedbackModelBinder_) ||
                (feedbackMaskedModelPipeline_.valid() && feedbackMaskedModelBinder_) ||
                (feedbackLodModelPipeline_.valid() && feedbackLodModelBinder_) ||
                (feedbackMaskedLodModelPipeline_.valid() && feedbackMaskedLodModelBinder_) ||
                feedbackDeformedModelDraw_) {
                for (const auto& item : modelItems_) {
                    if (!item.inView || !item.model ||
                        !item.prim || !item.mat || !item.indexCount ||
                        !item.model->vb.valid() || !item.model->ib.valid() ||
                        item.tint.w < 0.99f || item.dissolve > 0.0f ||
                        item.mat->alpha == AlphaMode::Blend ||
                        item.mat->transparent ||
                        item.materialRole == 1 || item.shadowRole == 2)
                        continue;
                    if (!item.rigid || item.joints >= 0 ||
                        item.baked || item.morph >= 0) {
                        if (!feedbackDeformedModelDraw_) continue;
                        const FeedbackDeformedModel info{
                            *item.model,*item.prim,*item.mat,item.world,
                            item.joints,item.morph,item.baked,item.anim,
                            item.lodThreshold,item.lodInvert,
                            item.firstIndex,item.indexCount,
                            item.model->gpuFirstIndex,
                            int32_t(item.model->gpuBaseVertex),
                            pixelGrid,jointData_,morphData_};
                        if (!feedbackDeformedModelDraw_(device,info))
                            return false;
                        continue;
                    }
                    const bool masked = item.mat->alpha == AlphaMode::Mask;
                    const bool fading = item.lodThreshold < 1.0f;
                    const auto pipeline = masked && fading ?
                        feedbackMaskedLodModelPipeline_ :
                        fading ? feedbackLodModelPipeline_ :
                        masked ? feedbackMaskedModelPipeline_ : feedbackModelPipeline_;
                    if (!pipeline.valid()) continue;
                    device.setPipeline(pipeline);
                    device.setVertexBuffer(item.model->vb);
                    device.setIndexBuffer(item.model->ib);
                    if (fading) {
                        const auto& lodBinder = masked ?
                            feedbackMaskedLodModelBinder_ : feedbackLodModelBinder_;
                        if (!lodBinder ||
                            !lodBinder(device,*item.model,
                                *item.prim,*item.mat,item.world,
                                item.lodThreshold,item.lodInvert,pixelGrid))
                            return false;
                    } else {
                        const auto& binder = masked ? feedbackMaskedModelBinder_
                                                    : feedbackModelBinder_;
                        if (!binder ||
                            !binder(device,*item.model,*item.prim,
                                    *item.mat,item.world))
                            return false;
                    }
                    device.drawIndexed(item.indexCount,
                        item.firstIndex + item.model->gpuFirstIndex,
                        int32_t(item.model->gpuBaseVertex));
                }
            }
            for (const auto& custom : customFeedback_)
                if (!custom(device)) return false;
            return true;
        });
    }

    // Opt-in automatic feedback submission during Renderer3D::render(),
    // after shadow passes and before the main scene pass begins.
    // The feedback pass and its GPU resources must outlive this renderer.
    // Only enable when the owning application has initialized the pass
    // and configured a compatible material feedback pipeline.
    void setVirtualTextureFeedback(
        gfx::VirtualTextureFeedbackPass* pass,
        gfx::PipelineHandle meshPipeline,
        FeedbackMeshBinder binder) {
        feedbackPass_ = pass;
        feedbackMeshPipeline_ = meshPipeline;
        feedbackMeshBinder_ = std::move(binder);
    }
    void clearVirtualTextureFeedback() {
        feedbackPass_ = nullptr;
        feedbackMeshPipeline_ = {};
        feedbackMeshBinder_ = {};
        clearVirtualTextureModelFeedback();
    }

    // Optional owner-thread scheduler for multiple independent VT feedback
    // domains. Invoked after shadows and before the main scene pass starts.
    // When installed it takes precedence over the single feedbackPass_.
    // Must be detached before destroying the scheduler/captured resources.
    using VirtualTextureFeedbackScheduler = std::function<void(Renderer3D&)>;
    void setVirtualTextureFeedbackScheduler(VirtualTextureFeedbackScheduler fn) {
        feedbackScheduler_ = std::move(fn);
    }
    void clearVirtualTextureFeedbackScheduler() { feedbackScheduler_ = {}; }

    // Extra shadow casters (sprites): run inside the sun shadow pass (only when shadows are on) with
    // the target bound; draw depth with lightViewProj(). Same lifetime rules as drawCustom.
    void drawCustomShadow(CustomDraw fn) { customShadow_.push_back(std::move(fn)); }
    const mat4& lightViewProj() const { return lightViewProj_; }
    const Camera3D& camera() const { return cam_; }
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
    // Diagnostic-only CPU MDI compatible-run cap; 0 means current renderer
    // instance limit. Does not resize shader uniform arrays or change pipes.
    // Never exposed as a production batching quality setting.
    void setDiagnosticCpuMdiGroupCap(uint32_t cap) {
        diagnosticCpuMdiGroupCap_ = cap == 0 ? 0 : std::clamp(cap, 16u, 256u);
    }
    // Diagnostic only: control which *model* phases emit GPU draws while
    // retaining both render encoders, other scene draws and pass clears.
    // Bit 0 = shadow models, bit 1 = main opaque models. Defaults to both.
    // Do not expose this as a quality setting or use it for production frames.
    void setDiagnosticModelPassMask(uint8_t mask) { diagnosticModelPassMask_ = mask & 3u; }
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
    // Synchronous tap for finalized models, animations and attachments.
    // Copy borrowed pointers before drawModel returns; not light-baker capture.
    using ModelPacketTap = std::function<void(const ModelDraw&)>;
    void setModelPacketTap(ModelPacketTap tap) { modelPacketTap_=std::move(tap); }
    ModelPacketTap exchangeModelPacketTap(ModelPacketTap tap){return std::exchange(modelPacketTap_,std::move(tap));}
    // Optional synchronous capture of mesh/line/triangle commands.
    // No callbacks retained by the renderer across frames; client removes it.
    using GeometryPacketTap=std::function<void(const CameraGeometryCommand&)>;
    void setGeometryPacketTap(GeometryPacketTap tap) {
        geometryPacketTap_=std::move(tap);
    }
    // Inside a custom lit draw (terrain): bind the terrain lightmap for AIVERSE_*_BAKED_TERRAIN (BakedShaders.h):
    // uniform block `uniformSlot`, RGBM page at `lightmapSlot`, shadow mask at `maskSlot` (placeholders when none).
    void bindBakedTerrain(int uniformSlot, int lightmapSlot, int maskSlot);

private:
    wind::SceneWind sceneWind_{};
    wind::WindDebugView windDebugView_ = wind::WindDebugView::Off;
    struct WindDebugMaterials { uint64_t revision = 0; size_t count = 0; std::vector<Material> materials; };
    std::unordered_map<const void*, WindDebugMaterials> windDebugMaterials_;
    bool applyWindRig(const ModelDraw& d);
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
        NereidFlowBinding waterFlow{};
    };
    struct ModelItem {
        const Model* model; const Primitive* prim; const Material* mat;
        int32_t materialIndex = -1; // remap material pointers on packet replay
        // Captured at selection time, before colour/LOD batching. Stores
        // real renderer-selected mesh and source primitive ordinals.
        uint32_t nodeIndex=0,meshIndex=0,primitiveIndex=0;
        uint64_t drawInvocation=0;
        uint32_t vertexOverrideCount=0;
        bool displacedVertexSource=false;
        // Full per-instance ModelVertex buffer; empty for the legacy path.
        gfx::BufferHandle vertexOverride{};
        bool rigid = false, castShadow = false;
        GeometryPolicy geometry;
        // Copied by value from a transient ModelDraw into this frame's
        // renderer-owned item. Never stores a pointer to GeometryScene.
        GpuIndexedSelectionPacket gpuIndexed{};
        GpuIndexedSelectionPacket gpuShadowIndexed{};
        GpuIndexedSelectionPacket gpuSpotShadowIndexed{};
        GpuIndexedSelectionPacket gpuReflectionIndexed{};
        size_t geometryPrimitive = 0;
        uint32_t firstIndex = 0, indexCount = 0;
        // Optional two-level, single-primitive GPU LOD choice. Always preserve
        // the CPU-selected primitive for legacy draws and shadow passes.
        bool gpuLodAvailable = false;
        gfx::DrawIndexedIndirectCommand gpuLodNear{}, gpuLodFar{};
        float gpuLodMaxPixels = 0, gpuLodError = 0;
        float3 gpuLodBoundsMin{}, gpuLodBoundsMax{};
        mat4 world; float4 tint; float3 team; float flash; uint32_t flags;
        uint64_t motionPersistentId = 0; // 0: transient, never tracked
        int motionNodeIndex = -1;
        int32_t joints;     // offset into jointData_ (floats), -1 when not palette-skinned
        bool baked;         // skinned from the baked texture (rows in anim)
        float4 anim;        // baked row A, row B, mix
        float dissolve, teamGlow;
        float lodThreshold=1; bool lodInvert=false;
        float4 outline;
        int32_t morph;      // offset into morphData_ (sparse weights + channels + count), -1 = none
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
    void retireRenderTargets(gfx::TextureHandle color,
                             gfx::TextureHandle depth={});
    // Quality/instance-limit changes retire old shaders and pipelines
    // without freeing GPU-visible handles until their own completion token.
    void retireModelPrograms();
    gfx::PipelineHandle modelPipeline(const ModelItem& it, int phase, bool instanced,
                                      bool gpuInstances = false, bool instancePalette = false);
    void recordModelSubmission(const ModelItem& item,int phase,
                               ModelSubmissionKind kind,
                               gfx::PipelineHandle pipeline);
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
    // Stage16.4c: compute for auxiliary cameras ONLY outside the active
    // render pass; no duplicate graphics submission or CPU GPU wait.
    void prepareStrataAuxiliaryGpuPackets(GpuIndexedPass pass,
                                          const mat4& projection,
                                          int width,int height);
    struct ObjectMotionHistory; // defined with per-view GPU owners below
    void renderObjectMotionVectors(PostProcess& post); // RHI pass, rigid + deformed models
    // Camera-current reactive coverage is NOT motion history: still encode
    // it on first frame, camera cut, resize or unavailable RGBA16F velocity.
    // Borrowed scene depth is only sampled; never owned by this renderer.
    void renderCurrentFrameReactiveMask(PostProcess& post,
        ObjectMotionHistory& history, gfx::TextureHandle sceneDepth,
        int width, int height);
    bool initSelectionOutline();
    void renderSelectionOutline(const gfx::PassDesc& scenePass, int width, int height);

    void renderReflection();        // water planar reflection (Renderer3DReflect.cpp, lighting agent)
    void renderSpotShadow();        // depth of the casters seen from the main spot light (same file)
    void computeShadowMatrix(const Camera3D& cam, float aspect);
    void uploadFrameUniforms(bool forShadow);
    // trace tier (Renderer3DTrace.cpp)
    bool initTrace();
    void shutdownTrace();
    bool beginSceneCapture(int w, int h);   // SSR: opens the opaque pass into the scene targets
    void endSceneCapture(const gfx::PassDesc& mainPass, int w, int h);   // blits colour + depth into the main pass
    void bindSceneTextures(int colorSlot, int depthSlot);   // after the capture: the scene targets, else placeholders
    void bindTraceTextures();                                // lit pipeline: tex3 height, tex4/tex5 scene

    void checkGpuDevice(const gfx::Device& dev) const {
        if(gpuDeviceLease_.device() && !gpuDeviceLease_.validFor(dev))
            throw std::logic_error("Renderer3D GPU device generation expired");
    }
    RenderDeviceLifetime::Lease gpuDeviceLease_;
    std::unique_ptr<GpuProgramRetirement> programRetirement_;
    // One render-target owner is shared with LightSystem. It must outlive
    // the light, trace and selection GPU resource owners.
    std::unique_ptr<OffscreenTargetRetirement> targetRetirement_;
    uint64_t unleasedTargetOrphans_=0,unleasedTargetIdleFailures_=0;
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
    std::deque<Mesh> meshSnapshots_;
    std::vector<ColorVertex> lines_, tris_;
    bool lumaMaterialCapture_=false,lumaSurfaceCapture_=false;
    std::vector<CustomDraw> customMaterialReceiver_,customSurfaceReceiver_;
    std::vector<CustomDraw> custom_, customOpaque_, customShadow_;
    std::vector<FeedbackCustomDraw> customFeedback_;
    VirtualTextureFeedbackScheduler feedbackScheduler_;
    gfx::VirtualTextureFeedbackPass* feedbackPass_ = nullptr; // non-owning
    gfx::PipelineHandle feedbackMeshPipeline_;
    FeedbackMeshBinder feedbackMeshBinder_;
    gfx::PipelineHandle feedbackModelPipeline_;
    FeedbackModelBinder feedbackModelBinder_;
    gfx::PipelineHandle feedbackMaskedModelPipeline_;
    FeedbackModelBinder feedbackMaskedModelBinder_;
    gfx::PipelineHandle feedbackLodModelPipeline_;
    FeedbackLodBinder feedbackLodModelBinder_;
    gfx::PipelineHandle feedbackMaskedLodModelPipeline_;
    FeedbackLodBinder feedbackMaskedLodModelBinder_;
    FeedbackDeformedModelDraw feedbackDeformedModelDraw_;
    ModelVirtualTextureResolver modelVirtualTextureResolver_;
    Camera3D cam_;
    mat4 viewProj_, lightViewProj_;
    float3 camPos_;
    uint32_t drawCalls_ = 0;
    bool drawCallDebug_ = false;
    int studioDebugMode_ = 0;
    uint32_t debugMainDrawId_ = 0;
    static float4 drawCallDebugColor(uint32_t drawId);
    int sceneW_ = 0, sceneH_ = 0;
    PostProcess* post_ = nullptr;      // set by render() when the hook is active, cleared by resolvePrisma()
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
    HeightField heightField_;
    struct SceneTarget { int w = 0, h = 0; bool hdr = false; gfx::TextureHandle color, depth; uint64_t lastUse = 0; };
    std::vector<SceneTarget> sceneTargets_;   // one per view size (editor: Scene + Game views)
    gfx::TextureHandle sceneColor_, sceneDepth_;   // this frame's capture
    gfx::TextureHandle dummyDepth_, dummyHeight_;
    double graphEpoch_ = -1;
    float graphTime_ = 0;
    float presentationTime_ = -1;
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
    uint32_t diagnosticCpuMdiGroupCap_ = 0; // 0: unchanged default grouping
    uint8_t diagnosticModelPassMask_ = 3u; // full output by default
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
    StrataPreviewStats strataPreviewStats_{};
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
    struct MotionDeformPose {
        const Model* sourceModel = nullptr; // identity only, never dereference after frame
        mat4 world;
        std::array<float, size_t(kMaxPaletteJoints) * 12> joints{};
        std::array<float, kMaxMorphTargets> weights{};
        bool hasSkin = false;
        bool baked = false;
        float4 animation{}; // baked clip A/B rows and interpolation for this view
    };
    struct MotionRigidPose {
        const Model* sourceModel = nullptr;
        mat4 world;
    };
    struct ObjectMotionHistory {
        gfx::TextureHandle target;
        gfx::TextureHandle depth;
        gfx::TextureHandle reactive; // current-frame translucent coverage; no temporal content
        int width = 0, height = 0;
        uint64_t epoch = 0; // last observed PostProcess camera-cut generation
        bool valid = false;
        std::map<std::pair<uint64_t, int>, MotionRigidPose> previous;
        // Same node can draw different primitives with distinct skin palettes.
        std::map<std::tuple<uint64_t, int, size_t>, MotionDeformPose> previousDeformed;
    };
    void queueObjectMotionTexture(gfx::TextureHandle& texture,
                                  uint64_t lastSubmittedFence);
    std::map<uint64_t, ObjectMotionHistory> objectMotionViews_;
    PrismaGpuRetirementQueue<gfx::TextureHandle> motionRetired_;
    std::vector<gfx::TextureHandle> motionUnfenced_; // quarantined until GPU idle
    uint64_t motionLastSubmittedFence_ = 0; // 0: unknown, never guess
    ObjectMotionStats objectMotionStats_{};
    std::vector<MotionReactiveDraw> motionReactiveDraws_;
    gfx::ShaderHandle objectMotionShader_;
    gfx::PipelineHandle objectMotionPipeline_;
    gfx::ShaderHandle objectMotionInstancedShader_;
    gfx::PipelineHandle objectMotionInstancedPipeline_;
    gfx::ShaderHandle objectMotionDeformedShader_;
    gfx::PipelineHandle objectMotionDeformedPipeline_;
    gfx::ShaderHandle objectMotionBakedShader_;
    gfx::PipelineHandle objectMotionBakedPipeline_;
    gfx::ShaderHandle objectMotionMaskedShader_;
    gfx::PipelineHandle objectMotionMaskedPipeline_;
    gfx::ShaderHandle objectMotionReactiveShader_;
    gfx::PipelineHandle objectMotionReactivePipeline_;
    gfx::ShaderHandle objectMotionReactiveCutoutShader_;
    gfx::PipelineHandle objectMotionReactiveCutoutPipeline_;
    gfx::PipelineHandle objectMotionReactiveMeshPipeline_; // MeshVertex water/translucency
    gfx::ShaderHandle objectMotionReactiveSkinShader_;
    gfx::PipelineHandle objectMotionReactiveSkinPipeline_;
    gfx::ShaderHandle objectMotionReactiveBakedShader_;
    gfx::PipelineHandle objectMotionReactiveBakedPipeline_;

    uint64_t modelDrawInvocation_=0; // reset per scene, no persistent identity
    std::array<const Model*,kMaxTrackedSubmissionModels> trackedModels_{};
    std::size_t trackedModelCount_=0;
    std::vector<ModelSubmittedPrimitive> modelSubmitted_;
    bool modelSubmissionTruncated_=false;
    bool modelSubmissionMainEncoded_=false;
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
        // Capture the exact external resources when GPU commands are
        // encoded. A Horizon revision switch can change bindings between
        // prepareGpuModelDraws() and the actual scene render pass.
        ModelVirtualTextureBinding virtualTexture;
        gfx::TextureHandle alphaCoverage;
        bool usesVirtualTexture = false;
        bool perCommandVt = false; // Metal opt-in per-command fragment ABI
        bool perCommandPbr = false; // exact per-command U2 and slots 1..6
        bool mixedVtResources = false; // actual different VT bindings
        bool mixedPbrResources = false;
        bool shaderGraph = false;
        bool mixedGraphSources = false;
        Material pbrPipelineSnapshot; // frozen pipeline-only safety contract
        std::vector<GpuMaterialResourceTicket> perCommandTickets;
        // CPU table snapshot validates each command before ICB execution. The Metal GPU ICB
        // still inherits homogeneous fragment resources per submission.
        GpuMaterialResourceTicket materialResources;
    };
    GpuMaterialResourceTable gpuMaterialResources_{4096};
    // Set only around an opt-in per-command ICB draw submission.
    // Non-ICB direct/MDI batches must continue exact VT grouping.
    bool gpuIcbPerCommandMaterialSubmit_ = false;
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
    uint32_t gpuIcbPerCommandVtExecuted_ = 0, gpuIcbMixedVtExecuted_ = 0;
    uint32_t gpuIcbPerCommandPbrSubmitted_ = 0, gpuIcbMixedPbrSubmitted_ = 0;
    uint32_t gpuIcbGraphSubmitted_ = 0, gpuIcbMixedGraphSubmitted_ = 0;
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
            if (!graph || !other.graph) return false;
            // Material fragment and vertex shader source are backend-specific.
            // A GL-only comparison may alias two distinct MSL shader programs;
            // a mere texture count also loses the source binding identity.
            // Preserve the Virtual Texture permutation flag explicitly,
            // including graphs with an unused/disconnected VT node.
            return graph->domain == other.graph->domain &&
                   graph->glsl == other.graph->glsl &&
                   graph->msl == other.graph->msl &&
                   graph->vertexGlsl == other.graph->vertexGlsl &&
                   graph->vertexMsl == other.graph->vertexMsl &&
                   graph->textures == other.graph->textures &&
                   graph->outputs == other.graph->outputs &&
                   graph->usesVirtualTexture == other.graph->usesVirtualTexture &&
                   graph->minQuality == other.graph->minQuality;
        }
    };
    struct ShaderEntry { gfx::ShaderHandle shader; bool failed = false; gfx::Device::ShaderJob job; };
    bool warmingPipelines_ = false, warmingPending_ = false;
    bool warmingFailed_ = false;
    std::vector<std::pair<ModelShaderKey, gfx::Device::PipelineJob>> pendingModelPipes_;
    // Backend colour-target warmup may be asynchronous even when it returns
    // no ShaderJob/PipelineJob. Poll these handles without reloading Models.
    std::vector<gfx::PipelineHandle> pendingColorWarms_;
    // Do not re-poll or repeatedly count an HDR variant rejected by its backend.
    // Invalidated alongside its original model pipeline generation.
    std::vector<gfx::PipelineHandle> failedColorWarms_;
    uint64_t colorWarmPollAttempts_=0,colorWarmReady_=0,colorWarmFailed_=0;
    std::vector<std::pair<ModelShaderKey, ShaderEntry>> modelShaders_;
    std::vector<std::pair<ModelShaderKey, gfx::PipelineHandle>> modelPipes_;
    gfx::TextureHandle white_, black_, flatNormal_;
    // One-view borrowed atlas; beginScene drops it before the next view.
    // U6 (nine float4, GiDynamicShaders.h): per cascade origin+1/spacing,
    // dimensions+probe offset, ring(+cascade count in cascade 0).
    gfx::TextureHandle dynamicGiAtlas_{};
    std::array<float,36> dynamicGiUniforms_{};
    gfx::PipelineHandle currentPipe_;
    std::string modelError_;
    // baked lighting (Renderer3DBaked.cpp)
    const BakedLighting* baked_ = nullptr;
    BakedQuality bakedQuality_ = BakedQuality::High;
    uint32_t bakedLightmapped_ = 0, bakedProbed_ = 0, bakedCountLm_ = 0, bakedCountPr_ = 0;
    ModelCapture capture_;
    ModelPacketTap modelPacketTap_;
    GeometryPacketTap geometryPacketTap_;
    bool suppressGeometryTap_=false;
    void bakedItem(ModelItem& it, const ModelDraw& d, int node);
    void bindBakedGI(const ModelItem& it);
};

} // namespace aiverse::render
