// Forward renderer for RTS scenes: lit meshes with real-time sun shadows, plus immediate-mode
// colored lines/triangles for overlays (selection, roads, fog). Built only on the RHI, so it runs on
// every backend unchanged.
//
// Frame:  beginScene(camera) -> drawMesh()/line()/... -> render(pass)  [shadow pass + main pass;
//         the main pass is left open so the caller can draw UI, then calls device.endPass()]
#pragma once
#include "Geometry.h"
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "../gfx/Gfx.h"
#include "Lights.h"
#include "Math3D.h"
#include "Model.h"

namespace aiverse::render {

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
    virtual gfx::PassDesc scenePass(const gfx::PassDesc& out, int w, int h) = 0;
    virtual void resolve(gfx::Device& dev, const gfx::PassDesc& out, const PostFrame& frame) = 0;
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
    // transparent object (Unity's transparent queue, ZWrite Off): every primitive goes to the blended pass even when opaque,
    // sorted back to front one by one (ortho cameras: depth along the view direction), without depth write, so a nearer
    // part drawn earlier is painted over by a farther part drawn later; casts no shadow. Its depth is written afterwards
    // (colour mask off), so ground layers drawn after the translucent pass stay under it. Also per material: "transparent"
    kDrawTransparent = 16,
    // static scenery (does not move for the whole map): receives a baked lightmap and is part of the scene
    // the light baker captures (Renderer3D::setModelCapture). Dynamic objects get light probes instead.
    kDrawStatic = 32,
};

struct ModelRenderStats {
    uint64_t nodes = 0, culledNodes = 0, lodNodes[4] = {};
    uint64_t triangles = 0, instancedDraws = 0, drawnInstances = 0;
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
    void line(float3 a, float3 b, uint32_t color);
    void triangle(float3 a, float3 b, float3 c, uint32_t color);    // flat, alpha-blended
    void quadXZ(float3 a, float3 b, float width, uint32_t color, float y = 0.02f);
    void circleXZ(float3 c, float radius, uint32_t color, int segments = 40, float y = 0.03f);
    void drawModel(const ModelDraw& d);
    void render(const gfx::PassDesc& screenPass, PostProcess* post = nullptr); // shadow pass + opens the main pass and draws everything
    // With a post hook: ends the scene pass, runs the effects into screenPass (left open). No-op otherwise.
    // Call after everything that belongs to the 3D scene (particles), before UI.
    void resolvePostFx();
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
    // Extra shadow casters (sprites): run inside the sun shadow pass (only when shadows are on) with
    // the target bound; draw depth with lightViewProj(). Same lifetime rules as drawCustom.
    void drawCustomShadow(CustomDraw fn) { customShadow_.push_back(std::move(fn)); }
    const mat4& lightViewProj() const { return lightViewProj_; }
    const RTSCamera& camera() const { return cam_; }

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
    void setModelCullingEnabled(bool on) { modelCullingEnabled_ = on; }
    void setModelInstancingEnabled(bool on) { modelInstancingEnabled_ = on; }
    const ModelRenderStats& modelStats() const { return modelStats_; }
    struct RenderProfile {
        double submitMs=0,occlusionMs=0;
        std::array<double,5> selectMs{},sortMs{},drawMs{};
        std::array<uint32_t,5> draws{},instances{},triangles{};
        uint32_t cachedModels=0,lodNodes=0,lodTransitionNodes=0;
    };
    void setProfiling(bool enabled) { profiling_=enabled; }
    const RenderProfile& profile() const { return profile_; }
    // Set before rendering; zero chooses the backend limit (28 with 4 KB uniforms / 256 Metal).
    void setInstanceLimit(uint32_t limit);
    uint32_t instanceLimit() const { return instanceLimit_; }
    void setModelLodEnabled(bool enabled) { modelLodEnabled_=enabled; }
    // Relative screen-size band around each threshold; 0 restores instant switches.
    void setModelLodFadeWidth(float width) { modelLodFadeWidth_=std::clamp(width,0.0f,0.49f); }

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
    // Inside a custom lit draw (terrain): bind the terrain lightmap for AIVERSE_*_BAKED_TERRAIN (BakedShaders.h):
    // uniform block `uniformSlot`, RGBM page at `lightmapSlot`, shadow mask at `maskSlot` (placeholders when none).
    void bindBakedTerrain(int uniformSlot, int lightmapSlot, int maskSlot);

private:
    struct DrawItem { const Mesh* mesh; mat4 model; float4 color; uint32_t flags; float specular, emissive; };
    struct ModelItem {
        const Model* model; const Primitive* prim; const Material* mat;
        bool rigid = false, castShadow = false;
        GeometryPolicy geometry;
        size_t geometryPrimitive = 0;
        uint32_t firstIndex = 0, indexCount = 0;
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
    };
    void initModels();
    void shutdownModels();
    gfx::PipelineHandle modelPipeline(const ModelItem& it, int phase, bool instanced);
    void drawModelBatch(const uint32_t* items, uint32_t count, int phase);
    void renderModels(int phase);   // 0 shadow, 1 opaque, 2 blended, 3 outlines, 4 depth of transparent objects
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
    RTSCamera cam_;
    mat4 viewProj_, lightViewProj_;
    float3 camPos_;
    uint32_t drawCalls_ = 0;
    int sceneW_ = 0, sceneH_ = 0;
    PostProcess* post_ = nullptr;      // set by render() when the hook is active, cleared by resolvePostFx()
    gfx::PassDesc postOut_;

    // trace tier
    bool traceWanted_ = true;
    bool ssrThisFrame_ = false;
    bool modelReflections_ = false; // opt-in opaque PBR SSR; High trace + HDR only
    bool hdrFrame_ = false;
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
    bool modelCullingEnabled_ = true, modelInstancingEnabled_ = true;
    bool auxiliaryModelPass_ = false;
    bool profiling_=false,modelLodEnabled_=true;
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
    struct ShaderEntry { gfx::ShaderHandle shader; bool failed = false; };
    std::vector<std::pair<ModelShaderKey, ShaderEntry>> modelShaders_;
    std::vector<std::pair<ModelShaderKey, gfx::PipelineHandle>> modelPipes_;
    gfx::TextureHandle white_, black_, flatNormal_;
    gfx::PipelineHandle currentPipe_;
    std::string modelError_;
    // baked lighting (Renderer3DBaked.cpp)
    const BakedLighting* baked_ = nullptr;
    BakedQuality bakedQuality_ = BakedQuality::High;
    uint32_t bakedLightmapped_ = 0, bakedProbed_ = 0, bakedCountLm_ = 0, bakedCountPr_ = 0;
    ModelCapture capture_;
    void bakedItem(ModelItem& it, const ModelDraw& d, int node);
    void bindBakedGI(const ModelItem& it);
};

} // namespace aiverse::render
