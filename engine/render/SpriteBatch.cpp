#include "SpriteBatch.h"
#include "SpriteShaders.h"
#include "SpriteReactiveShaders.h"
#include "Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace aiverse::render {

namespace {
constexpr uint32_t kMaxQuadsPerDraw = 16384;   // 16-bit indices
struct Uniforms { float viewProj[16]; float params[4]; };
struct LitUniforms { float viewProj[16]; float params[4]; float ref[4]; };
constexpr float kAlphaCut = 0.02f;
constexpr float kCasterCut = 0.5f;
} // namespace

bool SpriteBatch::init(gfx::Device& dev) {
    if (dev_) throw std::logic_error("SpriteBatch init: already initialized");
    dev_ = &dev;
    auto rollback = [&] {
        // A failed init may have created GPU objects. Release only after
        // the backend confirms previous users of this device are idle.
        if (!dev.waitForResourceIdle())
            throw std::runtime_error("SpriteBatch init rollback: GPU idle barrier failed");
        shutdown();
    };
    bool initialized = false;
    try {
        initialized = [&]() -> bool {
    gfx::ShaderDesc sd;
    sd.glslVertex = shaders::kSpriteVS; sd.glslFragment = shaders::kSpriteFS; sd.msl = shaders::kSpriteMSL;
    sd.textureSlots = 2; sd.uniformSlots = 3; sd.label = "sprite";   // U2 + tex1: lights (LightShaders.h)
    shader_ = dev.createShader(sd);
    if (!shader_.valid()) return false;
    gfx::PipelineDesc pd;
    pd.shader = shader_;
    pd.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
    pd.attrs[1] = {1, gfx::VertexFormat::Float2, 12};
    pd.attrs[2] = {2, gfx::VertexFormat::UByte4N, 20};
    pd.attrs[3] = {3, gfx::VertexFormat::UByte4N, 24};
    pd.attrs[4] = {4, gfx::VertexFormat::Float4, 28};
    pd.attrCount = 4; pd.stride = sizeof(SpriteVertex);
    pd.indexType = gfx::IndexType::UInt16;
    pd.cull = gfx::CullMode::None;          // mirrored facings flip the winding
    pd.blend = gfx::BlendMode::Alpha;
    pd.depthTest = true;
    pd.depthWrite = false;                  // painter order between sprites, depth only against the 3D scene
    pd.label = "sprite";
    blendPipe_ = dev.createPipeline(pd);
    // decals are cut from the ground surface: same triangles, pulled towards the camera in depth
    pd.depthBiasConstant = -4.0f; pd.depthBiasSlope = -1.5f;
    pd.label = "sprite decal";
    decalPipe_ = dev.createPipeline(pd);
    pd.depthBiasConstant = 0; pd.depthBiasSlope = 0;
    pd.depthWrite = true;                   // tile ground: later geometry (overlays) tests against it
    pd.label = "sprite ground";
    groundPipe_ = dev.createPipeline(pd);

    // lit variant (optional: a failure only leaves sprites unlit)
    gfx::ShaderDesc ls;
    ls.glslVertex = shaders::kSpriteLitVS; ls.glslFragment = shaders::kSpriteLitFS; ls.msl = shaders::kSpriteLitMSL;
    ls.textureSlots = 3; ls.uniformSlots = 3; ls.label = "sprite lit";
    litShader_ = dev.createShader(ls);
    if (litShader_.valid()) {
        gfx::PipelineDesc lp = pd;
        lp.shader = litShader_;
        lp.attrCount = 5;
        lp.depthWrite = true; lp.label = "sprite lit ground";
        litGroundPipe_ = dev.createPipeline(lp);
        lp.depthWrite = false; lp.label = "sprite lit";
        litPipe_ = dev.createPipeline(lp);
        lp.depthBiasConstant = -4.0f; lp.depthBiasSlope = -1.5f; lp.label = "sprite lit decal";
        litDecalPipe_ = dev.createPipeline(lp);
        gfx::ShaderDesc ws = ls;
        ws.glslFragment = shaders::kSpriteWaterFS; ws.msl = shaders::kSpriteWaterMSL;
        ws.textureSlots = 4; ws.label = "sprite water";
        waterShader_ = dev.createShader(ws);
        if (waterShader_.valid()) { lp.shader = waterShader_; lp.label = "sprite water decal"; waterPipe_ = dev.createPipeline(lp); }
    }
    gfx::ShaderDesc cs;
    cs.glslVertex = shaders::kSpriteCasterVS; cs.glslFragment = shaders::kSpriteCasterFS; cs.msl = shaders::kSpriteCasterMSL;
    cs.textureSlots = 1; cs.uniformSlots = 1; cs.label = "sprite caster";
    casterShader_ = dev.createShader(cs);
    if (casterShader_.valid()) {
        gfx::PipelineDesc cp;
        cp.shader = casterShader_;
        cp.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
        cp.attrs[1] = {1, gfx::VertexFormat::Float2, 12};
        cp.attrCount = 2; cp.stride = sizeof(SpriteVertex);
        cp.indexType = gfx::IndexType::UInt16;
        cp.cull = gfx::CullMode::None;
        cp.depthOnly = true;
        // almost no bias: the quads stand nearly along the light, a slope bias would push their shadow
        // away from the foot (peter-panning). Lit sprites receive away from their own quad (shader).
        cp.depthBiasConstant = 0.5f;
        cp.label = "sprite caster";
        casterPipe_ = dev.createPipeline(cp);
    }

    std::vector<uint16_t> idx(size_t(kMaxQuadsPerDraw) * 6);
    for (uint32_t q = 0; q < kMaxQuadsPerDraw; ++q) {
        uint16_t b = uint16_t(q * 4);
        uint16_t* p = &idx[size_t(q) * 6];
        p[0] = b; p[1] = uint16_t(b + 1); p[2] = uint16_t(b + 2); p[3] = b; p[4] = uint16_t(b + 2); p[5] = uint16_t(b + 3);
    }
    gfx::BufferDesc ib; ib.type = gfx::BufferType::Index; ib.size = idx.size() * sizeof(uint16_t); ib.data = idx.data(); ib.label = "sprite quads";
    ib_ = dev.createBuffer(ib);
    gfx::BufferDesc vb; vb.usage = gfx::Usage::Dynamic;
    // Camera captures can upload several vertex versions within one frame.
    // Keep already submitted GPU reads immutable on snapshot-aware backends.
    vb.snapshotUpdates = true;
    vb.size = 1024 * 4 * sizeof(SpriteVertex); vb.label = "sprites";
    vb_ = dev.createBuffer(vb);
    vb.size = 256 * 4 * sizeof(SpriteVertex); vb.label = "sprite casters";   // distinct snapshot stream
    casterVb_ = dev.createBuffer(vb);
    return blendPipe_.valid() && groundPipe_.valid() && decalPipe_.valid() &&
           ib_.valid() && vb_.valid() && casterVb_.valid();
        }();
    } catch (...) {
        rollback();
        throw;
    }
    if (!initialized) rollback();
    return initialized;
}

class SpriteBatch::PreparedFrame final {
    friend class SpriteBatch;
    SpriteBatch* owner=nullptr;
    gfx::BufferHandle vertex,casters;
    std::function<void()> restore;
    bool committed=false;
public:
    ~PreparedFrame() noexcept {
        if(committed || !owner)return;
        for(auto pair:{std::pair{owner->vb_,vertex},std::pair{owner->casterVb_,casters}})
            if(pair.first.valid() && pair.first!=pair.second)
                try {owner->dev_->destroyBuffer(pair.first);}catch(...) {}
        owner->vb_=vertex;owner->casterVb_=casters;
        restore();owner->preparingFrame_=owner->privateFrameBuffers_=false;
    }
};
std::shared_ptr<SpriteBatch::PreparedFrame> SpriteBatch::beginFramePreparation() {
    if(preparingFrame_)throw std::logic_error("nested sprite frame preparation");
    auto frame=std::make_shared<PreparedFrame>();
    frame->restore=[this,waterReactive=waterReactive_, casters=casters_, casterVerts=casterVerts_, quads=quads_, order=order_, verts=verts_, drawCalls=drawCalls_, drawUploadValid=drawUploadValid_, casterUploadValid=casterUploadValid_]() mutable noexcept {
        using std::swap;
        swap(waterReactive_,waterReactive);
        swap(casters_,casters);
        swap(casterVerts_,casterVerts);
        swap(quads_,quads);
        swap(order_,order);
        swap(verts_,verts);
        swap(drawCalls_,drawCalls);
        swap(drawUploadValid_,drawUploadValid);
        swap(casterUploadValid_,casterUploadValid);
    };
    frame->owner=this;frame->vertex=vb_;frame->casters=casterVb_;
    preparingFrame_=true;privateFrameBuffers_=false;
    return frame;
}
void SpriteBatch::prepareFrameUploads(gfx::Device& dev) {
    if(!preparingFrame_ || dev_!=&dev)
        throw std::logic_error("sprite uploads require a private frame owner");
    if(privateFrameBuffers_)throw std::logic_error("sprite frame prepared twice");
    privateFrameBuffers_=true;
    auto allocate=[&](size_t count,const char* label) {
        constexpr size_t stride=4*sizeof(SpriteVertex);
        if(count>std::numeric_limits<size_t>::max()/stride)
            throw std::length_error("sprite frame payload overflow");
        gfx::BufferDesc desc;desc.usage=gfx::Usage::Dynamic;
        desc.snapshotUpdates=true;desc.size=std::max<size_t>(1,count*stride);desc.label=label;
        const auto buffer=dev.createBuffer(desc);
        if(!buffer.valid())throw std::runtime_error("sprite candidate allocation failed");
        return buffer;
    };
    if(!quads_.empty())vb_=allocate(quads_.size(),"sprite frame candidate");
    if(!casters_.empty())casterVb_=allocate(casters_.size(),"sprite caster frame candidate");
    drawUploadValid_=casterUploadValid_=false;
    prepareDrawUpload(dev);prepareCasterUpload(dev);
}
void SpriteBatch::commitFramePreparation(const std::shared_ptr<PreparedFrame>& frame) noexcept {
    if(!frame || frame->owner!=this || frame->committed)return;
    if(vb_!=frame->vertex)frameRetirement_.retireBuffer(*dev_,frame->vertex);
    if(casterVb_!=frame->casters)frameRetirement_.retireBuffer(*dev_,frame->casters);
    frame->committed=true;preparingFrame_=privateFrameBuffers_=false;
}

bool SpriteBatch::shutdownAfterGpuIdle() {
    if (!dev_) return true;
    if (!dev_->waitForResourceIdle()) return false;
    shutdown();
    return true;
}

void SpriteBatch::shutdown() {
    drawUploadValid_ = casterUploadValid_ = false;
    if (!dev_) return;
    if(!frameRetirement_.completeForShutdown(*dev_))
        throw std::runtime_error("SpriteBatch frame retirement requires GPU idle");
    for (auto h : {vb_, ib_, casterVb_})
        if (h.valid()) dev_->destroyBuffer(h);
    for (auto p : {blendPipe_, groundPipe_, decalPipe_, litPipe_, litGroundPipe_, litDecalPipe_, casterPipe_, waterPipe_})
        if (p.valid()) dev_->destroyPipeline(p);
    for (auto sh : {shader_, litShader_, casterShader_, waterShader_})
        if (sh.valid()) dev_->destroyShader(sh);
    vb_ = ib_ = casterVb_ = {};
    blendPipe_ = groundPipe_ = decalPipe_ = litGroundPipe_ = {};
    litPipe_ = litDecalPipe_ = casterPipe_ = waterPipe_ = {};
    shader_ = litShader_ = casterShader_ = waterShader_ = {};
    if (waterReactivePipe_.valid()) dev_->destroyPipeline(waterReactivePipe_);
    if (waterReactiveShader_.valid()) dev_->destroyShader(waterReactiveShader_);
    waterReactivePipe_ = {};
    waterReactiveShader_ = {};
    waterReactive_.clear();
    dev_ = nullptr;
}

void SpriteBatch::appendQuad(std::vector<SpriteVertex>& out, const TextureRef& t, const float3 (&c)[4], float4 uv, uint32_t color) {
    const float2 st[4] = {{t.u(uv.x), t.v(uv.y)}, {t.u(uv.z), t.v(uv.y)}, {t.u(uv.z), t.v(uv.w)}, {t.u(uv.x), t.v(uv.w)}};
    for (int i = 0; i < 4; ++i) out.push_back({c[i], st[i], color, 0, {c[i].x, c[i].y, c[i].z, 0}});
}

namespace {
// Sutherland-Hodgman against the half-plane dot(n, p) <= d
int clipHalf(const float2* in, int n, float2 nrm, float d, float2* out) {
    int k = 0;
    for (int i = 0; i < n; ++i) {
        const float2 p = in[i], q = in[(i + 1) % n];
        const float vp = d - (nrm.x * p.x + nrm.y * p.y), vq = d - (nrm.x * q.x + nrm.y * q.y);
        if (vp >= 0 && k < kMaxSpriteShape) out[k++] = p;
        if ((vp >= 0) != (vq >= 0) && k < kMaxSpriteShape) { const float t = vp / (vp - vq); out[k++] = {p.x + (q.x - p.x) * t, p.y + (q.y - p.y) * t}; }
    }
    return k;
}
} // namespace

bool spriteShape(const TextureRef& t, float4 uv, SpriteShape& o) {
    o.tex = t.tex;
    if (!t.sprite) {
        o.count = 4;
        o.ab[0] = {0, 0}; o.ab[1] = {1, 0}; o.ab[2] = {1, 1}; o.ab[3] = {0, 1};
        const float du = t.u(uv.z) - t.u(uv.x), dv = t.v(uv.w) - t.v(uv.y);
        const float m[6] = {du, 0, t.u(uv.x), 0, dv, t.v(uv.y)};
        for (int i = 0; i < 6; ++i) o.m[i] = m[i];
        return true;
    }
    const AtlasSprite& s = *t.sprite;
    const float W = float(t.width), H = float(t.height);
    if (W <= 0 || H <= 0 || uv.z == uv.x || uv.w == uv.y) return false;
    // the frame under the rect's centre
    const int col = std::clamp(int(std::floor((uv.x + uv.z) * 0.5f * float(s.cols))), 0, s.cols - 1);
    const int row = std::clamp(int(std::floor((uv.y + uv.w) * 0.5f * float(s.rows))), 0, s.rows - 1);
    const AtlasFrame& f = s.frames[size_t(row * s.cols + col)];
    const atlas::Region& r = f.region;
    o.tex = f.tex;
    // content pixels -> rect parameters: logical px = cell origin + offset + content
    const float ox = float(col) * W / float(s.cols) + float(r.offsetX), oy = float(row) * H / float(s.rows) + float(r.offsetY);
    auto toAB = [&](float cx, float cy) { return float2{((ox + cx) / W - uv.x) / (uv.z - uv.x), ((oy + cy) / H - uv.y) / (uv.w - uv.y)}; };
    float2 a[kMaxSpriteShape], b[kMaxSpriteShape];
    int n = 0;
    if (r.mesh.empty()) {
        const float rw = float(r.w), rh = float(r.h);
        a[0] = toAB(0, 0); a[1] = toAB(rw, 0); a[2] = toAB(rw, rh); a[3] = toAB(0, rh);
        n = 4;
    } else {
        for (size_t i = 0; i + 1 < r.mesh.size() && n < kMaxSpriteShape; i += 2) a[n++] = toAB(r.mesh[i], r.mesh[i + 1]);
    }
    // only the part inside the rect (a sub-rect of one frame, a half-open grid)
    n = clipHalf(a, n, {-1, 0}, 0, b);
    n = clipHalf(b, n, {1, 0}, 1, a);
    n = clipHalf(a, n, {0, -1}, 0, b);
    n = clipHalf(b, n, {0, 1}, 1, a);
    if (n < 3) return false;
    for (int i = 0; i < n; ++i) o.ab[i] = a[i];
    o.count = n;
    // affine (a, b) -> page uv from three points (content pixels are affine in a and b)
    auto contentOf = [&](float pa, float pb) { return float2{(uv.x + (uv.z - uv.x) * pa) * W - ox, (uv.y + (uv.w - uv.y) * pb) * H - oy}; };
    float u[3], v[3];
    const float2 p3[3] = {contentOf(0, 0), contentOf(1, 0), contentOf(0, 1)};
    for (int i = 0; i < 3; ++i) atlas::regionPointUV(r, p3[i].x, p3[i].y, f.pageW, f.pageH, u[i], v[i]);
    const float m[6] = {u[1] - u[0], u[2] - u[0], u[0], v[1] - v[0], v[2] - v[0], v[0]};
    for (int i = 0; i < 6; ++i) o.m[i] = m[i];
    return true;
}

void SpriteBatch::polygon(gfx::TextureHandle tex, const SpriteVertex* v, int n, int layer, float sortKey, uint32_t flags) {
    if (!tex.valid() || n < 3) return;
    for (int i = 1; i + 1 < n; i += 2) {   // quad (0, i, i+1, i+2) = triangles (0, i, i+1) + (0, i+1, i+2)
        const SpriteVertex q[4] = {v[0], v[i], v[i + 1], i + 2 < n ? v[i + 2] : v[i + 1]};
        quadEx(tex, q, layer, sortKey, flags);
    }
}

void SpriteBatch::casterPolygon(gfx::TextureHandle tex, const float3* c, const float2* uv, int n) {
    if (!tex.valid() || n < 3) return;
    for (int i = 1; i + 1 < n; i += 2) {
        const int k = i + 2 < n ? i + 2 : i + 1;
        const float3 q[4] = {c[0], c[i], c[i + 1], c[k]};
        const float2 st[4] = {uv[0], uv[i], uv[i + 1], uv[k]};
        caster(tex, q, st);
    }
}

void SpriteBatch::quad(const TextureRef& t, const float3 (&c)[4], float4 uv, uint32_t color, uint32_t team, int layer, float sortKey) {
    if (!t.valid()) return;
    drawUploadValid_ = false;
    if (t.sprite) {
        SpriteShape sh;
        if (!spriteShape(t, uv, sh)) return;
        SpriteVertex v[kMaxSpriteShape];
        for (int i = 0; i < sh.count; ++i) {
            const float3 p = bilinear(c, sh.ab[i]);
            v[i] = {p, sh.uv(sh.ab[i]), color, team, {p.x, p.y, p.z, 0}};
        }
        polygon(sh.tex, v, sh.count, layer, sortKey, 0);
        return;
    }
    Quad q;
    q.tex = t.tex; q.layer = layer; q.key = sortKey; q.seq = uint32_t(quads_.size()); q.flags = 0;
    const float2 st[4] = {{t.u(uv.x), t.v(uv.y)}, {t.u(uv.z), t.v(uv.y)}, {t.u(uv.z), t.v(uv.w)}, {t.u(uv.x), t.v(uv.w)}};
    for (int i = 0; i < 4; ++i) q.v[i] = {c[i], st[i], color, team, {c[i].x, c[i].y, c[i].z, 0}};
    quads_.push_back(q);
}

void SpriteBatch::quadEx(gfx::TextureHandle tex, const SpriteVertex (&v)[4], int layer, float sortKey, uint32_t flags) {
    if (!tex.valid()) return;
    drawUploadValid_ = false;
    Quad q;
    q.tex = tex; q.layer = layer; q.key = sortKey; q.seq = uint32_t(quads_.size()); q.flags = flags;
    for (int i = 0; i < 4; ++i) q.v[i] = v[i];
    quads_.push_back(q);
}

void SpriteBatch::caster(gfx::TextureHandle tex, const float3 (&c)[4], const float2 (&uv)[4]) {
    if (!tex.valid()) return;
    casterUploadValid_ = false;
    Caster k;
    k.tex = tex;
    for (int i = 0; i < 4; ++i) k.v[i] = {c[i], uv[i], 0, 0, {}};
    casters_.push_back(k);
}

void SpriteBatch::drawRange(gfx::Device& dev, gfx::BufferHandle vb, uint32_t first, uint32_t count) {
    while (count > 0) {
        uint32_t n = std::min(count, kMaxQuadsPerDraw);
        dev.setVertexBuffer(vb);
        dev.setIndexBuffer(ib_);
        dev.drawIndexed(n * 6, 0, int32_t(first * 4));
        ++drawCalls_;
        first += n; count -= n;
    }
}

void SpriteBatch::prepareCasterUpload(gfx::Device& dev) {
    if(casters_.empty() || !casterPipe_.valid())return;
    // group by texture (no painter order in a depth pass)
    if (!casterUploadValid_) {
        std::stable_sort(casters_.begin(), casters_.end(), [](const Caster& a, const Caster& b) { return a.tex.id < b.tex.id; });
        casterVerts_.clear();
        casterVerts_.reserve(casters_.size() * 4);
        for (auto& k : casters_) for (auto& v : k.v) casterVerts_.push_back(v);
        if (casterVerts_.size() > std::numeric_limits<size_t>::max() / sizeof(SpriteVertex))
            throw std::length_error("SpriteBatch: caster vertex upload size overflow");
        dev.updateBuffer(casterVb_, casterVerts_.data(), casterVerts_.size() * sizeof(SpriteVertex));
        casterUploadValid_ = true;
    }
}
void SpriteBatch::prepareDrawUpload(gfx::Device& dev) {
    if(quads_.empty())return;
    if (!drawUploadValid_) {
        order_.resize(quads_.size());
        for (uint32_t i = 0; i < order_.size(); ++i) order_[i] = i;
        std::sort(order_.begin(), order_.end(), [&](uint32_t a, uint32_t b) {
            const Quad& A = quads_[a]; const Quad& B = quads_[b];
            if (A.layer != B.layer) return A.layer < B.layer;
            if (A.key != B.key) return A.key < B.key;
            return A.seq < B.seq;
        });
        verts_.clear();
        verts_.reserve(quads_.size() * 4);
        for (uint32_t i : order_) for (auto& v : quads_[i].v) verts_.push_back(v);
        if (verts_.size() > std::numeric_limits<size_t>::max() / sizeof(SpriteVertex))
            throw std::length_error("SpriteBatch: sprite vertex upload size overflow");
        dev.updateBuffer(vb_, verts_.data(), verts_.size() * sizeof(SpriteVertex));
        drawUploadValid_ = true;
    }
}

void SpriteBatch::flushCasters(gfx::Device& dev, const mat4& lightViewProj) {
    if (casters_.empty() || !casterPipe_.valid()) return;
    prepareCasterUpload(dev);
    dev.setPipeline(casterPipe_);
    Uniforms u{};
    std::memcpy(u.viewProj, lightViewProj.m, sizeof u.viewProj);
    u.params[0] = kCasterCut;
    dev.setUniforms(0, &u, sizeof u);
    uint32_t start = 0;
    while (start < casters_.size()) {
        gfx::TextureHandle tex = casters_[start].tex;
        uint32_t end = start + 1;
        while (end < casters_.size() && casters_[end].tex == tex) ++end;
        dev.setTexture(0, tex);
        drawRange(dev, casterVb_, start, end - start);
        start = end;
    }
}

void SpriteBatch::setUniforms(gfx::Device& dev, const mat4& viewProj, bool lit, const SpriteLighting* lighting) {
    if (lit) {
        lighting->bind();
        LitUniforms u{};
        std::memcpy(u.viewProj, viewProj.m, sizeof u.viewProj);
        u.params[0] = kAlphaCut;
        u.params[1] = lighting->receiveShadows ? 1.0f : 0.0f;
        u.ref[0] = std::max(lighting->ref.x, 1e-3f); u.ref[1] = std::max(lighting->ref.y, 1e-3f); u.ref[2] = std::max(lighting->ref.z, 1e-3f);
        dev.setUniforms(0, &u, sizeof u);
    } else {
        Uniforms u{};
        std::memcpy(u.viewProj, viewProj.m, sizeof u.viewProj);
        u.params[0] = kAlphaCut;
        dev.setUniforms(0, &u, sizeof u);
    }
}

void SpriteBatch::flush(gfx::Device& dev, const mat4& viewProj, const SpriteLighting* lighting) {
    drawCalls_ = 0;
    if (quads_.empty()) return;
    const bool canLight = lighting && lighting->bind && litPipe_.valid() && litDecalPipe_.valid();
    prepareDrawUpload(dev);
    // runs of equal texture and pipeline; the lit shader takes the sheet in slot 2 (0/1: shadow + lights)
    auto flagsOf = [&](uint32_t qi) { uint32_t f = quads_[qi].flags; if (!canLight) f &= ~uint32_t(kSpriteLit); return f; };
    uint32_t curFlags = ~0u;
    uint32_t start = 0;
    while (start < order_.size()) {
        const gfx::TextureHandle tex = quads_[order_[start]].tex;
        const uint32_t fl = flagsOf(order_[start]);
        uint32_t end = start + 1;
        while (end < order_.size() && quads_[order_[end]].tex == tex && flagsOf(order_[end]) == fl) ++end;
        const bool lit = fl & kSpriteLit;
        if (fl != curFlags) {
            const bool decal = fl & kSpriteDecal;
            dev.setPipeline(lit ? (decal ? litDecalPipe_ : litPipe_) : (decal ? decalPipe_ : blendPipe_));
            setUniforms(dev, viewProj, lit, lighting);
            curFlags = fl;
        }
        dev.setTexture(lit ? 2 : 0, tex);
        drawRange(dev, vb_, start, end - start);
        start = end;
    }
}

SpriteMesh SpriteBatch::buildMesh(gfx::Device& dev, const std::vector<SpriteVertex>& verts) {
    SpriteMesh m;
    if (verts.empty()) return m;
    gfx::BufferDesc vb; vb.size = verts.size() * sizeof(SpriteVertex); vb.data = verts.data(); vb.label = "sprite mesh";
    m.vb = dev.createBuffer(vb);
    m.quads = uint32_t(verts.size() / 4);
    return m;
}

void SpriteBatch::destroyMesh(gfx::Device& dev, SpriteMesh& m) {
    if (m.vb.valid()) dev.destroyBuffer(m.vb);
    m = {};
}

void SpriteBatch::drawMesh(gfx::Device& dev, const SpriteMesh& m, gfx::TextureHandle tex, const mat4& viewProj, const SpriteLighting* lighting, bool decal) {
    if (!m.vb.valid() || !tex.valid()) return;
    const bool lit = lighting && lighting->bind && (decal ? litDecalPipe_ : litGroundPipe_).valid();
    dev.setPipeline(lit ? (decal ? litDecalPipe_ : litGroundPipe_) : (decal ? decalPipe_ : groundPipe_));
    setUniforms(dev, viewProj, lit, lighting);
    dev.setTexture(lit ? 2 : 0, tex);
    drawRange(dev, m.vb, 0, m.quads);
}

void SpriteBatch::drawWater(gfx::Device& dev, const SpriteMesh& m, gfx::TextureHandle tex, const mat4& viewProj, const SpriteLighting& lighting) {
    if (!m.vb.valid() || !tex.valid() || m.quads == 0) return;
    // Capture exact source mesh/alpha texture; this is just draw metadata.
    // The optional reactive render reuses the same uploaded buffers later.
    waterReactive_.push_back({m,tex,viewProj});
    if (!waterPipe_.valid() || !lighting.bind) {
        drawMesh(dev, m, tex, viewProj, &lighting, true);
        return;
    }
    dev.setPipeline(waterPipe_);
    setUniforms(dev, viewProj, true, &lighting);
    dev.setTexture(2, tex);
    if (lighting.bindReflection) lighting.bindReflection(3);
    drawRange(dev, m.vb, 0, m.quads);
}


void SpriteBatch::registerWaterReactive(Renderer3D& renderer) {
    // Each view callback owns only borrowed handles, captured by value.
    // Owner must keep SpriteBatch, mesh buffers and textures alive through
    // this frame's synchronous Renderer3D::resolvePrisma.
    auto draws = std::move(waterReactive_);
    waterReactive_.clear();
    for (const auto& d : draws) {
        renderer.addMotionReactiveDraw([this,d](gfx::Device& device,
                                    gfx::TextureHandle depth,int w,int h) {
            renderWaterReactive(device,depth,w,h,
                                d.mesh,d.texture,d.viewProj);
        });
    }
}

uint32_t SpriteBatch::renderWaterReactive(
    gfx::Device& dev, gfx::TextureHandle sceneDepth,
    int w, int h, const SpriteMesh& m,
    gfx::TextureHandle tex, const mat4& viewProj) {
    if (&dev != dev_ || !sceneDepth.valid() || !m.vb.valid() ||
        !ib_.valid() || !tex.valid() || !m.quads || w <= 0 || h <= 0)
        return 0;
    if (!waterReactiveShader_.valid()) {
        gfx::ShaderDesc sd;
        sd.glslVertex = sprite_reactive_shaders::kVS;
        sd.glslFragment = sprite_reactive_shaders::kFS;
        sd.msl = sprite_reactive_shaders::kMSL;
        sd.uniformSlots = 1;
        sd.textureSlots = 2;
        sd.label = "prisma sprite water reactive";
        waterReactiveShader_ = dev.createShader(sd);
    }
    if (!waterReactiveShader_.valid()) return 0;
    if (!waterReactivePipe_.valid()) {
        gfx::PipelineDesc pd;
        pd.shader = waterReactiveShader_;
        pd.attrs[0] = {0,gfx::VertexFormat::Float3,0};
        pd.attrs[1] = {1,gfx::VertexFormat::Float2,12};
        pd.attrs[2] = {2,gfx::VertexFormat::UByte4N,20};
        pd.attrCount = 3;
        pd.stride = sizeof(SpriteVertex);
        pd.indexType = gfx::IndexType::UInt16;
        pd.cull = gfx::CullMode::None;
        pd.depthTest = false;
        pd.depthWrite = false;
        pd.blend = gfx::BlendMode::Screen;
        pd.colorFormat = gfx::PixelFormat::RGBA8;
        pd.label = "prisma sprite water reactive";
        waterReactivePipe_ = dev.createPipeline(pd);
    }
    if (!waterReactivePipe_.valid()) return 0;
    struct U { float viewProj[16], params[4]; };
    U u{};
    std::memcpy(u.viewProj,viewProj.m,sizeof u.viewProj);
    u.params[0]=float(w);
    u.params[1]=float(h);
    dev.setPipeline(waterReactivePipe_);
    dev.setViewport({0,0,w,h});
    dev.setUniforms(0,&u,sizeof u);
    dev.setTexture(0,tex);
    dev.setTexture(1,sceneDepth);
    dev.setVertexBuffer(m.vb);
    dev.setIndexBuffer(ib_);
    uint32_t draws=0;
    for(uint32_t q=0;q<m.quads;q+=kMaxQuadsPerDraw){
        const uint32_t n=std::min(kMaxQuadsPerDraw,m.quads-q);
        dev.drawIndexed(n*6,0,int32_t(q*4));
        ++draws;
    }
    return draws;
}

} // namespace aiverse::render
