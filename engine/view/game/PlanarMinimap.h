#pragma once
#include "engine/game/MapGrid.h"
#include "engine/render/TextureCache.h"
#include "GameView.h"
#include <algorithm>
#include <cmath>

namespace aiverse::game::minimap {

constexpr int kTargetSide = 192;  // image pixels on the longer map side

constexpr uint32_t rgba(int r, int g, int b, int a = 255) {
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24; // bytes in memory: R G B A
}
inline uint32_t darken(uint32_t c, int num, int den) {
    auto ch = [&](int s) { return int((c >> s) & 255u) * num / den; };
    return rgba(ch(0), ch(8), ch(16));
}

inline void build(const MapGrid& grid, const std::string& bytes, int& width, int& height,
                  float& cellsPerPixel, std::vector<uint32_t>& base) {
    const int side = std::max(1, std::max(grid.w, grid.h));
    const float pxPerCell = side <= kTargetSide ? float(std::max(1, kTargetSide / side)) : float(kTargetSide) / float(side);
    cellsPerPixel = 1.0f / pxPerCell;
    width = std::max(1, int(std::ceil(float(grid.w) * pxPerCell)));
    height = std::max(1, int(std::ceil(float(grid.h) * pxPerCell)));
    std::vector<uint8_t> image;
    int iw = 0, ih = 0;
    const bool painted = !bytes.empty() &&
                         render::decodeImage(bytes, iw, ih, image) && iw > 0 && ih > 0;
    base.assign(size_t(width) * size_t(height), 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const int cx = std::min(grid.w - 1, int(float(x) * cellsPerPixel)), cy = std::min(grid.h - 1, int(float(y) * cellsPerPixel));
            const uint8_t f = grid.flags[size_t(grid.index(cx, cy))];
            uint32_t c = rgba(60, 92, 50);
            if (!(f & kCellGround) && (f & kCellWater)) c = rgba(50, 100, 150);
            else if (!(f & kCellGround)) c = rgba(110, 106, 100);
            if (painted) {
                const size_t sx = size_t(x) * size_t(iw) / size_t(width);
                const size_t sy = size_t(y) * size_t(ih) / size_t(height);
                const uint8_t* pixel = image.data() + 4 * (sy * size_t(iw) + sx);
                auto blend = [&](int channel) {
                    const int terrain = int((c >> (channel * 8)) & 255u);
                    return (pixel[channel] * pixel[3] + terrain * (255 - pixel[3]) + 127) / 255;
                };
                c = rgba(blend(0), blend(1), blend(2));
            }
            base[size_t(y) * size_t(width) + size_t(x)] = c;
        }
}

inline void rasterize(const MapGrid& grid, const Rules& rules, const GameScene& scene,
                      const std::vector<uint8_t>* vision, const std::vector<OverlayMinimapMark>* marks,
                      int width, int height, float cellsPerPixel, const std::vector<uint32_t>& base,
                      std::vector<uint32_t>& pixels) {
    pixels.resize(base.size());
    const float ppc = 1.0f / cellsPerPixel;
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x) {
            const size_t i = size_t(y) * size_t(width) + size_t(x);
            uint32_t c = base[i];
            if (vision) {
                const int cx = std::min(grid.w - 1, int(float(x) * cellsPerPixel)), cy = std::min(grid.h - 1, int(float(y) * cellsPerPixel));
                const uint8_t v = (*vision)[size_t(grid.index(cx, cy))];
                if (v == 0) c = rgba(12, 14, 18);
                else if (v == 1) c = darken(c, 3, 5);
            }
            pixels[i] = c;
        }
    auto fill = [&](float wx, float wz, float r, uint32_t c) {
        auto span = [&](float lo, float hi, int n, int& a, int& b) {
            a = int(std::floor(lo * ppc));
            b = std::max(a, int(std::ceil(hi * ppc)) - 1);
            a = std::clamp(a, 0, n - 1); b = std::clamp(b, 0, n - 1);
        };
        int x0, x1, y0, y1;
        span(wx - r, wx + r, width, x0, x1);
        span(wz - r, wz + r, height, y0, y1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) pixels[size_t(y) * size_t(width) + size_t(x)] = c;
    };
    for (auto& o : scene.objects) {
        if (!o.visible || o.isProjectile) continue;
        const UnitType& t = rules.types[o.type];
        const bool node = o.owner == 0 && t.resourceNode >= 0;
        if (node && t.footprintW <= 1) continue; // trees: too many dots
        const render::float4 pc = GameView::playerColor(o.owner);
        const uint32_t c = node ? rgba(255, 216, 50) : rgba(int(pc.x * 255), int(pc.y * 255), int(pc.z * 255));
        const float r = t.structure ? std::max(1.0f, float(std::max(t.footprintW, t.footprintH)) * 0.5f) : 0.6f;
        fill(o.pos.x, o.pos.z, std::max(r, cellsPerPixel * 0.75f), c);
    }
    if (marks) // overlays with style.space "minimap" (pings, alerts): rings / discs blended over the image
        for (auto& m : *marks) {
            const float a = float(m.rgba >> 24) / 255.0f;
            const float cx = m.x * ppc, cy = m.z * ppc, r = std::max(1.0f, m.radius * ppc);
            const int x0 = std::max(0, int(cx - r - 1)), x1 = std::min(width - 1, int(cx + r + 1));
            const int y0 = std::max(0, int(cy - r - 1)), y1 = std::min(height - 1, int(cy + r + 1));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float d = std::hypot(x + 0.5f - cx, y + 0.5f - cy);
                    if (m.ring ? std::abs(d - r) > 0.75f : d > r) continue;
                    uint32_t& px = pixels[size_t(y) * size_t(width) + size_t(x)];
                    auto mix = [&](int s) { return int(float((px >> s) & 255u) * (1 - a) + float((m.rgba >> s) & 255u) * a); };
                    px = rgba(mix(0), mix(8), mix(16));
                }
        }
}

} // namespace aiverse::game::minimap
