#include "SpineRender.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include "../platform/Platform.h"
#include "../render/TextureCache.h"
#include "SpineShaders.h"

namespace aiverse::spine {

namespace {

std::string dirOf(const std::string& p) {
    size_t s = p.find_last_of('/');
    return s == std::string::npos ? std::string() : p.substr(0, s + 1);
}

std::string withoutExt(const std::string& p) {
    size_t s = p.find_last_of('/'), d = p.find_last_of('.');
    if (d == std::string::npos || (s != std::string::npos && d < s)) return p;
    return p.substr(0, d);
}

} // namespace

bool SpineRenderer::init(gfx::Device& dev) {
    dev_ = &dev;
    gfx::ShaderDesc sd;
    sd.glslVertex = shaders::kSpineVS;
    sd.glslFragment = shaders::kSpineFS;
    sd.msl = shaders::kSpineMSL;
    sd.textureSlots = 1;
    sd.uniformSlots = 1;
    sd.label = "spine";
    shader_ = dev.createShader(sd);
    if (!shader_.valid()) return false;
    for (int depth = 0; depth < 2; depth++)
        for (int mul = 0; mul < 2; mul++) {
            gfx::PipelineDesc p;
            p.shader = shader_;
            p.attrs[0] = {0, gfx::VertexFormat::Float2, 0};
            p.attrs[1] = {1, gfx::VertexFormat::Float2, 8};
            p.attrs[2] = {2, gfx::VertexFormat::UByte4N, 16};
            p.attrCount = 3;
            p.stride = sizeof(DrawVertex);
            p.indexType = gfx::IndexType::UInt16;
            p.cull = gfx::CullMode::None;
            p.blend = mul ? gfx::BlendMode::Multiply : gfx::BlendMode::Premultiplied;
            p.depthTest = depth != 0;
            p.depthWrite = false;
            p.label = "spine";
            pipes_[depth][mul] = dev.createPipeline(p);
            if (!pipes_[depth][mul].valid()) return false;
        }
    return true;
}

void SpineRenderer::shutdown() {
    if (!dev_) return;
    for (auto& row : pipes_)
        for (auto& p : row) if (p.valid()) dev_->destroyPipeline(p);
    if (shader_.valid()) dev_->destroyShader(shader_);
    if (vb_.valid()) dev_->destroyBuffer(vb_);
    if (ib_.valid()) dev_->destroyBuffer(ib_);
    *this = SpineRenderer{};
}

void SpineRenderer::draw(gfx::Device& dev, const DrawList& list, const std::vector<gfx::TextureHandle>& pages,
                         const std::vector<uint8_t>& pma, const float mvp[16], bool depthTest, float alpha) {
    drawCalls_ = 0;
    if (!shader_.valid() || list.batches.empty()) return;
    size_t vbBytes = list.vertices.size() * sizeof(DrawVertex), ibBytes = list.indices.size() * sizeof(uint16_t);
    if (!vb_.valid() || vbBytes > vbSize_) {
        if (vb_.valid()) dev.destroyBuffer(vb_);
        vbSize_ = std::max<size_t>(vbBytes * 2, 64 * 1024);
        gfx::BufferDesc d;
        d.usage = gfx::Usage::Dynamic;
        d.size = vbSize_;
        d.label = "spine vertices";
        d.snapshotUpdates = true;   // several skeletons per frame share the buffer
        vb_ = dev.createBuffer(d);
    }
    if (!ib_.valid() || ibBytes > ibSize_) {
        if (ib_.valid()) dev.destroyBuffer(ib_);
        ibSize_ = std::max<size_t>(ibBytes * 2, 32 * 1024);
        gfx::BufferDesc d;
        d.type = gfx::BufferType::Index;
        d.usage = gfx::Usage::Dynamic;
        d.size = ibSize_;
        d.label = "spine indices";
        d.snapshotUpdates = true;
        ib_ = dev.createBuffer(d);
    }
    dev.updateBuffer(vb_, list.vertices.data(), vbBytes);
    dev.updateBuffer(ib_, list.indices.data(), ibBytes);
    struct U { float mvp[16]; float params[4]; float tint[4]; } u;
    std::memcpy(u.mvp, mvp, sizeof(u.mvp));
    u.tint[0] = u.tint[1] = u.tint[2] = 1;
    u.tint[3] = alpha;
    int lastPipe = -1;
    for (size_t i = 0; i < list.batches.size(); i++) {
        const DrawBatch& b = list.batches[i];
        if (b.page < 0 || size_t(b.page) >= pages.size() || !pages[size_t(b.page)].valid() || b.indexCount == 0) continue;
        int mul = b.blend == BlendMode::Multiply ? 1 : 0;
        if (mul != lastPipe) { dev.setPipeline(pipes_[depthTest ? 1 : 0][mul]); lastPipe = mul; }
        dev.setVertexBuffer(vb_, size_t(list.baseVertex[i]) * sizeof(DrawVertex));
        dev.setIndexBuffer(ib_);
        dev.setTexture(0, pages[size_t(b.page)]);
        u.params[0] = size_t(b.page) < pma.size() && pma[size_t(b.page)] ? 1.f : 0.f;
        u.params[1] = b.blend == BlendMode::Additive ? 1.f : 0.f;
        u.params[2] = float(mul);
        u.params[3] = b.blend == BlendMode::Screen ? 1.f : 0.f;
        dev.setUniforms(0, &u, sizeof(u));
        dev.drawIndexed(b.indexCount, b.firstIndex, 0);
        drawCalls_++;
    }
}

SpineAsset loadSpineAsset(const std::string& skeleton, const std::string& atlasPath, float scale) {
    std::string json;
    if (!platform::readContentFile(skeleton, json)) throw std::runtime_error("spine: can't read '" + skeleton + "'");
    std::string atlasText, used = atlasPath;
    if (used.empty()) {
        std::string base = withoutExt(skeleton);
        for (const char* ext : {".atlas", ".atlas.txt"})
            if (platform::readContentFile(base + ext, atlasText)) { used = base + ext; break; }
        if (used.empty()) throw std::runtime_error("spine: no atlas next to '" + skeleton + "' (" + base + ".atlas)");
    } else if (!platform::readContentFile(used, atlasText)) {
        throw std::runtime_error("spine: can't read atlas '" + used + "'");
    }
    auto atlas = std::make_shared<Atlas>(parseAtlas(atlasText));
    SpineAsset a;
    for (const AtlasPage& p : atlas->pages) a.pagePaths.push_back(dirOf(used) + p.name);
    a.data = parseSkeleton(json, atlas, scale);
    a.data->name = skeleton;
    return a;
}

SpinePlayer::SpinePlayer(const SpineAsset& asset)
    : skeleton(asset.data), state(asset.data), pagePaths_(asset.pagePaths) {
    for (const AtlasPage& p : asset.data->atlas->pages) pma_.push_back(p.pma ? 1 : 0);
}

void SpinePlayer::update(float dt) {
    state.update(dt);
    state.apply(skeleton);
    skeleton.updateWorldTransform();
    skeleton.buildDrawList(list);
}

void SpinePlayer::draw(gfx::Device& dev, SpineRenderer& r, render::TextureCache& textures, const float mvp[16], bool depthTest, float alpha) {
    if (pages_.size() != pagePaths_.size()) {
        pages_.clear();
        for (const std::string& p : pagePaths_) pages_.push_back(textures.resolve(p, false).tex);
    }
    r.draw(dev, list, pages_, pma_, mvp, depthTest, alpha);
}

void poseBounds(const Skeleton& sk, float out[4]) {
    sk.bounds(out[0], out[1], out[2], out[3]);
    const SkeletonData& d = sk.data();
    if (out[2] <= out[0] && d.width > 0) { out[0] = d.x; out[1] = d.y; out[2] = d.x + d.width; out[3] = d.y + d.height; }
}

void fitMatrix(const float bounds[4], int width, int height, float pad, bool flipY, float out[16]) {
    float x0 = std::min(bounds[0], bounds[2]), x1 = std::max(bounds[0], bounds[2]);
    float y0 = std::min(bounds[1], bounds[3]), y1 = std::max(bounds[1], bounds[3]);
    float w = std::max(1.f, float(width) - 2 * pad), h = std::max(1.f, float(height) - 2 * pad);
    float s = std::min(w / std::max(x1 - x0, 1e-3f), h / std::max(y1 - y0, 1e-3f));
    float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f;
    // skeleton -> pixels around the centre, then pixels -> clip
    float sx = 2 * s / float(std::max(1, width)), sy = 2 * s / float(std::max(1, height)) * (flipY ? -1.f : 1.f);
    std::memset(out, 0, sizeof(float) * 16);
    out[0] = sx;
    out[5] = sy;
    out[10] = 1;
    out[12] = -cx * sx;
    out[13] = -cy * sy;
    out[15] = 1;
}

} // namespace aiverse::spine
