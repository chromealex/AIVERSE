#include "BakedLighting.h"
#include <algorithm>

namespace aiverse::render {

BakedLighting::~BakedLighting() { release(); }

void BakedLighting::release() {
    if (!dev_) return;
    for (auto& p : pages_) {
        if (p.lightmap.valid()) dev_->destroyTexture(p.lightmap);
        if (p.mask.valid()) dev_->destroyTexture(p.mask);
    }
    pages_.clear();
    if (probeTex_.valid()) dev_->destroyTexture(probeTex_);
    probeTex_ = {};
    probeWidth_ = 0;
    dev_ = nullptr;
}

bool BakedLighting::upload(gfx::Device& dev, bake::LightmapData data) {
    release();
    dev_ = &dev;
    data_ = std::move(data);
    for (auto& pg : data_.pages) {
        Page out;
        gfx::TextureDesc td;
        td.width = pg.width; td.height = pg.height;
        td.format = gfx::PixelFormat::RGBA8;
        td.filter = gfx::Filter::Linear;
        td.data = pg.rgbm.data();
        td.label = "lightmap";
        out.lightmap = dev.createTexture(td);
        if (!pg.mask.empty()) {
            td.data = pg.mask.data(); td.label = "shadow mask";
            out.mask = dev.createTexture(td);
        }
        pages_.push_back(out);
        pg.rgbm.clear(); pg.rgbm.shrink_to_fit(); pg.mask.clear(); pg.mask.shrink_to_fit();
    }
    const auto& g = data_.probes;
    if (!g.empty()) {
        const size_t count = g.coeffs.size() / 12, texels = count * 3;
        probeWidth_ = 3 * 256;
        const int height = int((texels + size_t(probeWidth_) - 1) / size_t(probeWidth_));
        std::vector<float> px(size_t(probeWidth_) * size_t(height) * 4, 0.0f);
        for (size_t i = 0; i < count; ++i) {
            const float* c = &g.coeffs[i * 12];
            // a.rgb, bx.rgb, by.rgb, bz.rgb -> 3 RGBA texels in that order
            std::copy(c, c + 12, px.begin() + std::ptrdiff_t(i * 12));
        }
        gfx::TextureDesc td;
        td.width = probeWidth_; td.height = height;
        td.format = gfx::PixelFormat::RGBA32F;
        td.filter = gfx::Filter::Nearest;
        td.data = px.data();
        td.label = "light probes";
        probeTex_ = dev.createTexture(td);
    }
    return !pages_.empty() || probeTex_.valid();
}

const bake::LightmapRegion* BakedLighting::find(const std::string& modelPath, int node, const mat4& world) const {
    if (data_.regions.empty()) return nullptr;
    auto it = data_.regions.find(bake::lightmapRegionKey(modelPath, node, world));
    return it == data_.regions.end() || it->second.page >= pages_.size() ? nullptr : &it->second;
}

void BakedLighting::giBlock(float o[16]) const {
    std::fill(o, o + 16, 0.0f);
    const auto& g = data_.probes;
    if (probeTex_.valid()) {
        o[0] = g.origin.x; o[1] = g.origin.y; o[2] = g.origin.z; o[3] = 1;
        o[4] = 1.0f / g.cell.x; o[5] = 1.0f / g.cell.y; o[6] = 1.0f / g.cell.z;
        o[8] = float(g.nx); o[9] = float(g.ny); o[10] = float(g.nz);
        o[15] = float(probeWidth_);
    } else {
        o[4] = o[5] = o[6] = 1; o[8] = o[9] = o[10] = 1; o[15] = 1;
    }
    o[7] = data_.rgbmRange * data_.intensity;
    o[11] = float(modeIndex());
    o[12] = data_.shadowColor.x; o[13] = data_.shadowColor.y; o[14] = data_.shadowColor.z;
}

bool BakedLighting::terrainBlock(float o[12]) const {
    std::fill(o, o + 12, 0.0f);
    const auto& t = data_.terrain;
    if (t.page < 0 || size_t(t.page) >= pages_.size()) return false;
    // page uv = ((xz - origin) / extent) * st.xy + st.zw
    o[0] = t.st[0] / t.extent[0]; o[1] = t.st[1] / t.extent[1];
    o[2] = t.st[2] - t.origin[0] * o[0]; o[3] = t.st[3] - t.origin[1] * o[1];
    o[4] = 1; o[5] = data_.rgbmRange * data_.intensity; o[6] = float(modeIndex());
    o[8] = data_.shadowColor.x; o[9] = data_.shadowColor.y; o[10] = data_.shadowColor.z;
    return true;
}

} // namespace aiverse::render
