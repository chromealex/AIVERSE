// Traced lighting ("trace" tier, Shaders.h AIVERSE_*_TRACE / AIVERSE_*_SSR). No ray-tracing API: the GLES 3.0
// floor has none, so the rays are marched in the fragment shaders against data every backend has.
//
//   * Terrain shadows: every lit shader marches the sun ray over a 16-bit height map of the whole
//     map (built by the terrain renderer, Renderer3D::setHeightField). Cliffs and mountains cast long,
//     soft shadows at dawn and dusk even far outside the shadow map, which only fits the visible ground.
//   * Screen-space reflections on water: with water in the scene the opaque part of the main pass is
//     drawn into colour + depth textures, blitted (restoring depth) into the real main pass, and the
//     water's lit shader marches the reflected view ray against that depth. A hit replaces the planar
//     reflection at the pixel (banks, cliffs, terrain - everything the mirrored pass skips).
//
// Cost: ~20 height-map samples per lit pixel + one fullscreen blit + up to 20 depth samples per water
// pixel. High shadows only (desktop, recent phones); the quality governor turns it off with them.
#include "Renderer3D.h"
#include "Shaders.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <string>

namespace aiverse::render {

namespace {
struct BlitUniforms { float params[4]; };
} // namespace

bool Renderer3D::defaultTraceFor(const gfx::Device& dev, bool mobile) {
    if (const char* e = std::getenv("AIVERSE_TRACE")) { std::string v = e; return v != "off" && v != "0"; }
    return defaultQualityFor(dev, mobile) == ShadowQuality::High;
}

bool Renderer3D::traceActive() const {
    if (!traceWanted_ || quality_ != ShadowQuality::High || !dev_) return false;
    if (const char* e = std::getenv("AIVERSE_TRACE")) { std::string v = e; if (v == "off" || v == "0") return false; }
    return true;
}

bool Renderer3D::initTrace() {
    gfx::ShaderDesc bs; bs.glslVertex = shaders::kBlitVS; bs.glslFragment = shaders::kBlitFS; bs.msl = shaders::kBlitMSL;
    bs.uniformSlots = 1; bs.textureSlots = 2; bs.label = "scene blit";
    blitShader_ = dev_->createShader(bs);
    if (!blitShader_.valid()) return false;
    gfx::PipelineDesc p;
    p.shader = blitShader_;
    p.attrs[0] = {0, gfx::VertexFormat::Float2, 0};
    p.attrCount = 1; p.stride = 8;
    p.indexType = gfx::IndexType::None;
    p.cull = gfx::CullMode::None;
    p.depthTest = true; p.depthWrite = true;    // GL writes depth only with the test on: the main pass clears to 1, LEQUAL always passes
    p.label = "scene blit";
    blitPipe_ = dev_->createPipeline(p);
    const float tri[6] = {-1, -1, 3, -1, -1, 3};
    gfx::BufferDesc vb; vb.size = sizeof tri; vb.data = tri; vb.label = "scene blit";
    blitVB_ = dev_->createBuffer(vb);
    // placeholders for the scene slots when nothing was captured (depth 1 = nothing hit) and for the
    // height field (range 0 in the uniforms keeps the march off anyway)
    gfx::TextureDesc dd; dd.width = dd.height = 4; dd.format = gfx::PixelFormat::Depth; dd.renderTarget = true;
    dd.filter = gfx::Filter::Nearest; dd.label = "trace dummy depth";
    dummyDepth_ = dev_->createTexture(dd);
    const uint32_t px[16] = {};
    gfx::TextureDesc hd; hd.width = hd.height = 4; hd.data = px; hd.filter = gfx::Filter::Nearest; hd.label = "trace dummy height";
    dummyHeight_ = dev_->createTexture(hd);
    // (the dummy depth is never sampled: the shaders test trace2.w first; its contents may stay undefined)
    return blitPipe_.valid() && blitVB_.valid() && dummyDepth_.valid() && dummyHeight_.valid();
}

void Renderer3D::shutdownTrace() {
    if (!dev_) return;
    for (auto& t : sceneTargets_) { dev_->destroyTexture(t.color); dev_->destroyTexture(t.depth); }
    sceneTargets_.clear();
    for (auto t : {dummyDepth_, dummyHeight_}) if (t.valid()) dev_->destroyTexture(t);
    sceneColor_ = sceneDepth_ = dummyDepth_ = dummyHeight_ = {};
    if (blitPipe_.valid()) dev_->destroyPipeline(blitPipe_);
    if (blitShader_.valid()) dev_->destroyShader(blitShader_);
    if (blitVB_.valid()) dev_->destroyBuffer(blitVB_);
    blitPipe_ = {}; blitShader_ = {}; blitVB_ = {};
}

void Renderer3D::bindTrace(int slot) {
    const bool on = traceActive() && heightField_.tex.valid() && heightField_.heightRange > 0;
    dev_->setTexture(slot, on ? heightField_.tex : dummyHeight_);
}

void Renderer3D::bindSceneTextures(int colorSlot, int depthSlot) {
    const bool on = ssrThisFrame_ && sceneColor_.valid() && sceneDepth_.valid();
    dev_->setTexture(colorSlot, on ? sceneColor_ : black_);
    dev_->setTexture(depthSlot, on ? sceneDepth_ : dummyDepth_);
}

bool Renderer3D::beginSceneCapture(int w, int h) {
    if (!blitPipe_.valid() || w <= 0 || h <= 0) return false;
    ++traceFrame_;
    SceneTarget* t = nullptr;
    for (auto& s : sceneTargets_) if (s.w == w && s.h == h && s.hdr == hdrFrame_) { t = &s; break; }
    if (!t) {
        // drop targets of sizes not used for a while (resized window); keep a few (editor views)
        for (size_t i = 0; i < sceneTargets_.size();) {
            if (traceFrame_ - sceneTargets_[i].lastUse > 120 || sceneTargets_.size() > 4) {
                dev_->destroyTexture(sceneTargets_[i].color); dev_->destroyTexture(sceneTargets_[i].depth);
                sceneTargets_.erase(sceneTargets_.begin() + long(i));
            } else ++i;
        }
        gfx::TextureDesc cd; cd.width = w; cd.height = h; cd.renderTarget = true; cd.filter = gfx::Filter::Linear; cd.label = "scene colour";
        gfx::TextureDesc dd; dd.width = w; dd.height = h; dd.format = gfx::PixelFormat::Depth; dd.renderTarget = true;
        dd.filter = gfx::Filter::Nearest; dd.label = "scene depth";
        cd.format = hdrFrame_ ? gfx::PixelFormat::RGBA16F : gfx::PixelFormat::RGBA8;
        SceneTarget n; n.hdr = hdrFrame_; n.w = w; n.h = h; n.color = dev_->createTexture(cd); n.depth = dev_->createTexture(dd);
        if (!n.color.valid() || !n.depth.valid()) { if (n.color.valid()) dev_->destroyTexture(n.color); if (n.depth.valid()) dev_->destroyTexture(n.depth); return false; }
        sceneTargets_.push_back(n);
        t = &sceneTargets_.back();
    }
    t->lastUse = traceFrame_;
    sceneColor_ = t->color; sceneDepth_ = t->depth;
    gfx::PassDesc p;
    p.colorTarget = sceneColor_;
    p.depthTarget = sceneDepth_;
    std::memcpy(p.clearColor, postOut_.clearColor, sizeof p.clearColor);
    if (hdrFrame_) for (int i = 0; i < 3; ++i) {
        const float c = std::max(p.clearColor[i], 0.0f);
        p.clearColor[i] = c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
    }
    p.clearColorEnabled = true; p.clearDepthEnabled = true;
    dev_->beginPass(p);
    return true;
}

void Renderer3D::endSceneCapture(const gfx::PassDesc& mainPass, int w, int h) {
    dev_->endPass();
    ssrThisFrame_ = true;
    gfx::PassDesc mp = mainPass;
    mp.clearColorEnabled = false;   // the blit covers the whole target (colour and depth)
    mp.clearDepthEnabled = true;
    dev_->beginPass(mp);
    dev_->setViewport({0, 0, w, h});
    dev_->setPipeline(blitPipe_);
    BlitUniforms u{};
    u.params[0] = dev_->info().renderTargetFlipY ? 1.0f : 0.0f;
    dev_->setUniforms(0, &u, sizeof u);
    dev_->setTexture(0, sceneColor_);
    dev_->setTexture(1, sceneDepth_);
    dev_->setVertexBuffer(blitVB_);
    dev_->draw(3);
    ++drawCalls_;
}

} // namespace aiverse::render
