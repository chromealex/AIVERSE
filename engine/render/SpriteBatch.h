// Batched textured quads for 2D games: sprites (units, buildings, effects, shadow blobs) and tile
// maps, drawn through the RHI with one pipeline: alpha test + alpha blend, depth tested against the
// 3D scene (terrain hides sprites behind hills) but not depth written, painter-sorted per frame.
// Consecutive quads with the same texture are one draw call; tile maps are static meshes.
//
// Frame:  begin() -> quad()... -> flush(dev, viewProj)   (inside an open pass, e.g. from
//         Renderer3D::drawCustom, so sprites sit above opaque meshes and below overlays)
//
// Sun shadows (optional): caster() queues alpha-tested quads for the shadow map, drawn by
// flushCasters() inside Renderer3D's shadow pass. Quads flagged `lit` use the lit sprite shader
// (sun, sun shadows, point lights; the caller binds the lit frame through `LitBinder`), quads
// flagged `decal` lie on a surface (projected ground decals, blob shadows) and are drawn with a
// depth bias towards the camera so they never z-fight with the ground they were cut from.
#pragma once
#include "NativePresentationRetirement.h"
#include <memory>
#include <cstdint>
#include <functional>
#include <vector>
#include "../gfx/Gfx.h"
#include "Math3D.h"
#include "TextureCache.h"

namespace aiverse::render {
class Renderer3D; // optional reactive coverage integration


// rpos: receive point for lighting (lit shader): xyz, w > 0 = upright sprite (see SpriteShaders.h)
struct SpriteVertex { float3 pos; float2 uv; uint32_t color; uint32_t team; float4 rpos; };

enum SpriteQuadFlags : uint32_t { kSpriteLit = 1, kSpriteDecal = 2 };

// Lit draws: binds U1 / U2 / tex0 / tex1 (Renderer3D::bindLitFrame); `ref` = reference light
// (linear rgb) at which a lit sprite shows its painted colours; receiveShadows = sample the sun map.
struct SpriteLighting {
    std::function<void()> bind;
    float3 ref{1, 1, 1};
    bool receiveShadows = true;
    std::function<void(int slot)> bindReflection;   // water decals: the planar reflection into `slot` (black: none)
}; 

// What is drawn of a sprite over the logical uv rect (u0, v0, u1, v1) of its image: a convex
// polygon in rect parameters (a, b: 0..1 from the rect's first corner along u and along v, so a
// mirrored rect mirrors the polygon) and the affine map from (a, b) to texture uv. A plain
// texture: the whole rect. An atlased sprite (TextureCache::resolveSprite): the trimmed content or
// mesh of the frame under the rect's centre, on its page, clipped to the rect. Place corner k of
// the polygon at bilinear(quad corners, ab[k]).
constexpr int kMaxSpriteShape = 40;
struct SpriteShape {
    gfx::TextureHandle tex;
    int count = 0;
    float2 ab[kMaxSpriteShape];
    float m[6] = {1, 0, 0, 0, 1, 0};   // u = m0 a + m1 b + m2, v = m3 a + m4 b + m5
    float2 uv(float2 p) const { return {m[0] * p.x + m[1] * p.y + m[2], m[3] * p.x + m[4] * p.y + m[5]}; }
};
bool spriteShape(const TextureRef& t, float4 uv, SpriteShape& out);   // false: nothing to draw
// point at rect parameters (a, b) of the quad TL, TR, BR, BL
inline float3 bilinear(const float3 (&c)[4], float2 ab) {
    const float3 top = c[0] + (c[1] - c[0]) * ab.x, bot = c[3] + (c[2] - c[3]) * ab.x;
    return top + (bot - top) * ab.y;
}

struct SpriteMesh {          // static quads (4 vertices each), e.g. a tile map
    gfx::BufferHandle vb;
    uint32_t quads = 0;
};

class SpriteBatch {
public:
    class PreparedFrame;
    std::shared_ptr<PreparedFrame> beginFramePreparation();
    void prepareFrameUploads(gfx::Device&);
    void commitFramePreparation(const std::shared_ptr<PreparedFrame>&) noexcept;
    bool collectRetired(gfx::Device& dev) {return frameRetirement_.collect(dev);}

    bool init(gfx::Device& dev);
    void shutdown();
    // For failure recovery and ordinary host teardown: never release an
    // in-flight GPU allocation unless the device confirms an idle boundary.
    // On failure, retain handles for a later retry.
    bool shutdownAfterGpuIdle();

    void begin() {
        drawUploadValid_ = casterUploadValid_ = false;
        quads_.clear(); verts_.clear(); casters_.clear();
        waterReactive_.clear(); // never retain draw ranges from a previous frame
    }
    // Corners in order top-left, top-right, bottom-right, bottom-left (as seen on screen);
    // uv = logical rect (u0, v0, u1, v1) inside `tex` (mapped through TextureRef, so atlases work;
    // an atlased sprite draws its spriteShape).
    // Sorted by layer, then sortKey (smaller first = further back), then submission order.
    void quad(const TextureRef& tex, const float3 (&corners)[4], float4 uv, uint32_t color, uint32_t team, int layer, float sortKey);
    // Full control: per-corner texture-space uv and receive points (rpos), SpriteQuadFlags.
    // A triangle is a quad whose last two corners are equal.
    void quadEx(gfx::TextureHandle tex, const SpriteVertex (&v)[4], int layer, float sortKey, uint32_t flags);
    // `lighting` is required when any quad is flagged kSpriteLit (otherwise those draw unlit).
    void flush(gfx::Device& dev, const mat4& viewProj, const SpriteLighting* lighting = nullptr);
    // Convex polygon (fan around v[0]), queued as quads like quadEx (one sort position).
    void polygon(gfx::TextureHandle tex, const SpriteVertex* v, int n, int layer, float sortKey, uint32_t flags);
    // shadow casters (texture-space uv, alpha-tested at 0.5)
    void caster(gfx::TextureHandle tex, const float3 (&corners)[4], const float2 (&uv)[4]);
    void casterPolygon(gfx::TextureHandle tex, const float3* corners, const float2* uv, int n);
    size_t casterCount() const { return casters_.size(); }
    void flushCasters(gfx::Device& dev, const mat4& lightViewProj);   // inside the shadow pass
    size_t size() const { return quads_.size(); }
    uint32_t drawCalls() const { return drawCalls_; }
    // GPU ownership is only the sprite batch's own dynamic/static/caster
    // buffers; TextureRef, atlas pages and SpriteMesh are caller-owned.
    GpuOwnedMemoryStats ownedGpuMemoryStats() const {
        if (!dev_) return {};
        GpuOwnedMemoryCounter counter(*dev_);
        counter.buffer(vb_);counter.buffer(ib_);counter.buffer(casterVb_);
        return counter.stats();
    }

    // Static meshes: build once, draw every frame before the dynamic sprites.
    static void appendQuad(std::vector<SpriteVertex>& out, const TextureRef& tex, const float3 (&corners)[4], float4 uv, uint32_t color);
    SpriteMesh buildMesh(gfx::Device& dev, const std::vector<SpriteVertex>& verts);
    void destroyMesh(gfx::Device& dev, SpriteMesh& m);
    // decal: drawn with the decal pipelines (depth bias toward the camera: paint on a surface, e.g. road markings)
    void drawMesh(gfx::Device& dev, const SpriteMesh& m, gfx::TextureHandle tex, const mat4& viewProj, const SpriteLighting* lighting = nullptr, bool decal = false);
    // Standing water decals (puddles): the sheet's alpha is the pool, its colour the water's tint; sky, sun, lights
    // and the planar reflection on top (SpriteShaders.h kSpriteWaterFS). Needs `lighting`; without the water
    // pipeline (a failed shader) it draws as a lit decal.
    void drawWater(gfx::Device& dev, const SpriteMesh& m, gfx::TextureHandle tex, const mat4& viewProj, const SpriteLighting& lighting);
    // Stage 32: after drawWater, register all current-frame water decal
    // ranges with the renderer's optional reactive pass. This does not
    // invoke GPU drawing now. The project must call it before resolvePrisma.
    // Only live water mesh/textures may be registered; if the producer
    // destroys those resources before resolve, the callback is invalid.
    void registerWaterReactive(Renderer3D& renderer);
    bool hasWaterReactive() const { return !waterReactive_.empty(); }
    // Called only inside an already-open RGBA8 reactive pass.
    uint32_t renderWaterReactive(gfx::Device& dev,
                                 gfx::TextureHandle sceneDepth, int w, int h,
                                 const SpriteMesh& mesh, gfx::TextureHandle tex,
                                 const mat4& viewProj);

private:
    struct Quad { gfx::TextureHandle tex; int layer; float key; uint32_t seq; uint32_t flags; SpriteVertex v[4]; };
    struct Caster { gfx::TextureHandle tex; SpriteVertex v[4]; };
    struct WaterReactive {
        SpriteMesh mesh;
        gfx::TextureHandle texture;
        mat4 viewProj;
    };
    void drawRange(gfx::Device& dev, gfx::BufferHandle vb, uint32_t firstQuad, uint32_t count);
    void setUniforms(gfx::Device& dev, const mat4& viewProj, bool lit, const SpriteLighting* lighting);

    void prepareDrawUpload(gfx::Device&);
    void prepareCasterUpload(gfx::Device&);
    bool preparingFrame_=false, privateFrameBuffers_=false;
    NativePresentationRetirement frameRetirement_;
    gfx::Device* dev_ = nullptr;
    gfx::ShaderHandle shader_, litShader_, casterShader_, waterShader_;
    gfx::ShaderHandle waterReactiveShader_;
    gfx::PipelineHandle waterReactivePipe_;
    std::vector<WaterReactive> waterReactive_;
    gfx::PipelineHandle blendPipe_, groundPipe_, decalPipe_, litPipe_, litGroundPipe_, litDecalPipe_, casterPipe_, waterPipe_;
    gfx::BufferHandle vb_, ib_, casterVb_;
    std::vector<Caster> casters_;
    std::vector<SpriteVertex> casterVerts_;
    std::vector<Quad> quads_;
    std::vector<uint32_t> order_;
    std::vector<SpriteVertex> verts_;
    uint32_t drawCalls_ = 0;
    // GPU uploads remain valid across repeated draws of unchanged geometry.
    // Mutation or begin() invalidates them; camera projection is uniforms only.
    bool drawUploadValid_ = false, casterUploadValid_ = false;
};

} // namespace aiverse::render
