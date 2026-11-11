#include "RtsGroundCanvas.h"
#include <algorithm>
#include <cstdio>

namespace aiverse::game {

using namespace render;

void RtsGroundCanvas::shutdown() {
    if (dev_) { batch_.shutdown(); textures_.shutdown(); }
    dev_ = nullptr; white_ = {}; failed_ = false; triangles_ = 0;
}

void RtsGroundCanvas::draw(Renderer3D& r, gfx::Device& dev, const std::vector<ViewGroundBatch>& batches, const std::string& base) {
    triangles_ = 0;
    if (batches.empty() || failed_) return;
    if (dev_ && dev_ != &dev) shutdown();
    if (!dev_) {
        dev_ = &dev;
        textures_.init(dev);
        if (!batch_.init(dev)) { std::fprintf(stderr, "[ground canvas] pipeline: %s\n", dev.lastError().c_str()); failed_ = true; return; }
        white_ = textures_.addGenerated("builtin:white", 4, 4, std::vector<uint32_t>(16, 0xffffffffu), true);
    }
    std::vector<size_t> order(batches.size());
    for (size_t i = 0; i < order.size(); ++i) order[i] = i;
    std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b) { return batches[a].layer < batches[b].layer; });
    batch_.begin();
    uint32_t seq = 0;
    for (size_t bi : order) {
        const ViewGroundBatch& b = batches[bi];
        TextureRef t = white_;
        const bool textured = !b.image.empty();
        if (textured) {
            t = textures_.resolve(b.image, false, base);
            if (!t.valid()) {
                if (std::find(warned_.begin(), warned_.end(), b.image) == warned_.end()) {
                    warned_.push_back(b.image);
                    std::fprintf(stderr, "view.draw_ground: %s: %s\n", b.image.c_str(), textures_.lastError().c_str());
                }
                continue;
            }
        }
        const float wu = white_.u(.5f), wv = white_.v(.5f);
        for (size_t i = 0; i + 2 < b.verts.size(); i += 3) {
            SpriteVertex v[4];
            for (int k = 0; k < 3; ++k) {
                const ViewGroundVertex& g = b.verts[i + size_t(k)];
                const uint32_t c = g.rgba;   // 0xRRGGBBAA -> ubyte4n in memory order r, g, b, a
                v[k].pos = {g.x, g.y, g.z};
                v[k].uv = textured ? float2{t.u(g.u), t.v(g.v)} : float2{wu, wv};
                v[k].color = ((c >> 24) & 255u) | (((c >> 16) & 255u) << 8) | (((c >> 8) & 255u) << 16) | ((c & 255u) << 24);
                v[k].team = 0;
                v[k].rpos = {g.x, g.y, g.z, 0};
            }
            v[3] = v[2];
            // layer, then submission order (the sort key is constant: no painter sorting inside a batch)
            batch_.quadEx(t.tex, v, b.layer, float(seq++) * 0.0f, kSpriteDecal);
            ++triangles_;
        }
    }
    if (batch_.size() == 0) return;
    const mat4 vp = r.viewProj();
    r.drawCustom([this, vp](gfx::Device& d) { batch_.flush(d, vp); });
}

} // namespace aiverse::game
