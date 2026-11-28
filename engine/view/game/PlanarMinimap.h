#pragma once
// Planar minimap raster shared by the game client HUD (apps/client/Minimap.cpp) and the Studio overlay
// preview (apps/studio-native/StudioOverlayPlan.h). One owner for authored bounds, terrain colours,
// background blending, fog, unit dots, overlay marks and the camera frame, so both hosts draw the same image.
#include "engine/game/MapGrid.h"
#include "engine/game/RoutesData.h"
#include "engine/view/MapProjection.h"
#include "GameOverlays.h"
#include "GameView.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <vector>

namespace aiverse::game::minimap {

constexpr int kTargetSide = 192;  // image pixels on the longer map side

constexpr uint32_t rgba(int r, int g, int b, int a = 255) {
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24; // bytes in memory: R G B A
}
inline uint32_t darken(uint32_t c, int num, int den) {
    auto ch = [&](int s) { return int((c >> s) & 255u) * num / den; };
    return rgba(ch(0), ch(8), ch(16));
}

// Map "minimap" over game.json "minimap": "bounds" (camera | [left, top, right, bottom]) and
// "cameraFrame" (frustum | rectangle). Throws with an author-facing message on invalid data.
struct Layout {
    ::aiverse::view::MapProjection bounds;
    bool rectangle = false;
};
inline Layout resolveLayout(const Json& map, const Json& manifest, int gridW, int gridH) {
    if (gridW <= 0 || gridH <= 0) throw std::runtime_error("minimap requires a non-empty map");
    Layout layout;
    layout.bounds.set(0, 0, float(gridW), float(gridH));
    const auto grid = layout.bounds;
    auto option = [&](const char* name) -> const Json& {
        return map["minimap"].has(name) ? map["minimap"][name] : manifest["minimap"][name];
    };
    const auto& authored = option("bounds");
    if (authored.isString() && authored.str() == "camera") {
        const auto screen = parseScreen(map["screen"], gridW, gridH);
        if (!screen.enabled) throw std::runtime_error("minimap.bounds camera requires map screen");
        if (!layout.bounds.set(float((screen.center.x - screen.size.x / 2 - screen.left).toDouble()),
                               float((screen.center.y - screen.size.y / 2 - screen.top).toDouble()),
                               float((screen.center.x + screen.size.x / 2 + screen.right).toDouble()),
                               float((screen.center.y + screen.size.y / 2 + screen.bottom).toDouble())))
            throw std::runtime_error("minimap camera bounds must be a finite positive rectangle");
    } else if (!authored.isNull()) {
        if (!authored.isArray() || authored.size() != 4) throw std::runtime_error("minimap.bounds must be camera or [left,top,right,bottom]");
        for (const auto& value : authored.items()) if (!value.isNumber()) throw std::runtime_error("minimap.bounds requires numbers");
        if (!layout.bounds.set(float(authored[0].toDouble()), float(authored[1].toDouble()), float(authored[2].toDouble()), float(authored[3].toDouble())))
            throw std::runtime_error("minimap.bounds must be a finite positive rectangle");
    }
    if (!layout.bounds.clipTo(grid)) throw std::runtime_error("minimap.bounds does not intersect the map");
    const auto& frame = option("cameraFrame");
    if (!frame.isNull() && (!frame.isString() || (frame.str() != "frustum" && frame.str() != "rectangle")))
        throw std::runtime_error("minimap.cameraFrame must be frustum or rectangle");
    layout.rectangle = frame.isString() && frame.str() == "rectangle";
    return layout;
}

// Static part of the image: terrain kinds blended with the optional painted background (RGBA8, may be empty).
struct Raster {
    int width = 0, height = 0, gridW = 0, gridH = 0;
    float cellsPerPixel = 1;
    ::aiverse::view::MapProjection bounds;
    bool rectangle = false;
    std::vector<uint32_t> base;
};
inline Raster build(const MapGrid& grid, const Layout& layout, const std::vector<uint8_t>& image, int iw, int ih) {
    if (grid.w <= 0 || grid.h <= 0) throw std::runtime_error("minimap requires a non-empty map");
    Raster out;
    out.gridW = grid.w; out.gridH = grid.h;
    out.bounds = layout.bounds; out.rectangle = layout.rectangle;
    const auto& bounds = out.bounds;
    const int side = std::max(1, int(std::ceil(std::max(bounds.width(), bounds.height()))));
    const float pxPerCell = side <= kTargetSide ? float(std::max(1, kTargetSide / side)) : float(kTargetSide) / float(side);
    out.cellsPerPixel = 1.0f / pxPerCell;
    out.width = std::max(1, int(std::ceil(bounds.width() * pxPerCell)));
    out.height = std::max(1, int(std::ceil(bounds.height() * pxPerCell)));
    const bool painted = iw > 0 && ih > 0 && image.size() >= size_t(iw) * size_t(ih) * 4;
    out.base.assign(size_t(out.width) * size_t(out.height), 0);
    for (int y = 0; y < out.height; ++y)
        for (int x = 0; x < out.width; ++x) {
            const auto p = bounds.point(float(x) / out.width, float(y) / out.height);
            const int cx = std::clamp(int(p.x), 0, grid.w - 1), cy = std::clamp(int(p.z), 0, grid.h - 1);
            const uint8_t f = grid.flags[size_t(grid.index(cx, cy))];
            uint32_t c = rgba(60, 92, 50);
            if (!(f & kCellGround) && (f & kCellWater)) c = rgba(50, 100, 150);
            else if (!(f & kCellGround)) c = rgba(110, 106, 100);
            if (painted) {
                const size_t sx = std::min(size_t(iw - 1), size_t(std::max(0.0f, p.x) / float(grid.w) * float(iw)));
                const size_t sy = std::min(size_t(ih - 1), size_t(std::max(0.0f, p.z) / float(grid.h) * float(ih)));
                const uint8_t* pixel = image.data() + 4 * (sy * size_t(iw) + sx);
                auto blend = [&](int channel) {
                    const int terrain = int((c >> (channel * 8)) & 255u);
                    return (pixel[channel] * pixel[3] + terrain * (255 - pixel[3]) + 127) / 255;
                };
                c = rgba(blend(0), blend(1), blend(2));
            }
            out.base[size_t(y) * size_t(out.width) + size_t(x)] = c;
        }
    return out;
}

// Dynamic part: fog (vision per cell, 0 unseen / 1 explored), unit dots, overlay marks and the camera frame.
// viewQuad: camera footprint on the ground (nullptr: none).
inline void rasterize(const Raster& raster, const MapGrid& grid, const Rules& rules, const GameScene& scene,
                      const std::vector<uint8_t>* vision, const std::vector<OverlayMinimapMark>* marks,
                      const render::float3 (*viewQuad)[4], std::vector<uint32_t>& pixels) {
    const int w = raster.width, h = raster.height;
    const auto& bounds = raster.bounds;
    pixels.resize(raster.base.size());
    if (grid.w != raster.gridW || grid.h != raster.gridH || raster.base.size() != size_t(w) * size_t(h)) {
        pixels = raster.base;
        return;
    }
    const float scaleX = float(w) / bounds.width(), scaleZ = float(h) / bounds.height();
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const size_t i = size_t(y) * size_t(w) + size_t(x);
            uint32_t c = raster.base[i];
            if (vision && vision->size() == grid.flags.size()) {
                const auto p = bounds.point(float(x) / w, float(y) / h);
                const int cx = std::clamp(int(p.x), 0, grid.w - 1), cy = std::clamp(int(p.z), 0, grid.h - 1);
                const uint8_t v = (*vision)[size_t(grid.index(cx, cy))];
                if (v == 0) c = rgba(12, 14, 18);
                else if (v == 1) c = darken(c, 3, 5);
            }
            pixels[i] = c;
        }
    auto fill = [&](float wx, float wz, float r, uint32_t c) {
        if (!bounds.intersects(wx, wz, r)) return;
        wx -= bounds.left; wz -= bounds.top;
        auto span = [&](float lo, float hi, float scale, int n, int& a, int& b) {
            a = int(std::floor(lo * scale));
            b = std::max(a, int(std::ceil(hi * scale)) - 1);
            a = std::clamp(a, 0, n - 1); b = std::clamp(b, 0, n - 1);
        };
        int x0, x1, y0, y1;
        span(wx - r, wx + r, scaleX, w, x0, x1);
        span(wz - r, wz + r, scaleZ, h, y0, y1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) pixels[size_t(y) * size_t(w) + size_t(x)] = c;
    };
    for (auto& o : scene.objects) {
        if (!o.visible || o.isProjectile || o.type >= rules.types.size()) continue;
        const UnitType& t = rules.types[o.type];
        if (!t.view.getBool("minimap", true)) continue;
        const bool node = o.owner == 0 && t.resourceNode >= 0;
        if (!t.view.has("minimap") && node && t.footprintW <= 1) continue; // trees: too many dots
        const render::float4 pc = GameView::playerColor(o.owner);
        const uint32_t c = node ? rgba(255, 216, 50) : rgba(int(pc.x * 255), int(pc.y * 255), int(pc.z * 255));
        const float r = t.structure ? std::max(1.0f, float(std::max(t.footprintW, t.footprintH)) * 0.5f) : 0.6f;
        fill(o.pos.x, o.pos.z, std::max(r, raster.cellsPerPixel * 0.75f), c);
    }
    if (marks) // overlays with style.space "minimap" (pings, alerts): rings / discs blended over the image
        for (auto& m : *marks) {
            const float a = float(m.rgba >> 24) / 255.0f;
            const float cx = (m.x - bounds.left) * scaleX, cy = (m.z - bounds.top) * scaleZ;
            const float rx = std::max(1.0f, m.radius * scaleX), rz = std::max(1.0f, m.radius * scaleZ);
            const int x0 = std::max(0, int(cx - rx - 1)), x1 = std::min(w - 1, int(cx + rx + 1));
            const int y0 = std::max(0, int(cy - rz - 1)), y1 = std::min(h - 1, int(cy + rz + 1));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float d = std::hypot((x + 0.5f - cx) / rx, (y + 0.5f - cy) / rz);
                    if (m.ring ? std::abs(d - 1.0f) > 0.75f / std::min(rx, rz) : d > 1.0f) continue;
                    uint32_t& px = pixels[size_t(y) * size_t(w) + size_t(x)];
                    auto mix = [&](int s) { return int(float((px >> s) & 255u) * (1 - a) + float((m.rgba >> s) & 255u) * a); };
                    px = rgba(mix(0), mix(8), mix(16));
                }
        }
    if (viewQuad) { // camera footprint in the same projection as pointer input
        const auto& q = *viewQuad;
        const auto rectangle = ::aiverse::view::MapProjection::rectangularFrame(q);
        for (int k = 0; k < 4; ++k) {
            const render::float3 a = raster.rectangle ? rectangle[size_t(k)] : q[k];
            const render::float3 b = raster.rectangle ? rectangle[size_t((k + 1) % 4)] : q[(k + 1) % 4];
            const int steps = int(std::max(std::abs(b.x - a.x) * scaleX, std::abs(b.z - a.z) * scaleZ)) + 1;
            for (int s = 0; s <= steps; ++s) {
                const float t = float(s) / float(steps);
                const int x = int(std::floor((a.x - bounds.left + (b.x - a.x) * t) * scaleX));
                const int y = int(std::floor((a.z - bounds.top + (b.z - a.z) * t) * scaleZ));
                // The HUD can downsample this raster (desktop 190dp, touch 150dp). A one-texel edge can
                // disappear under nearest sampling: cover two texels for every camera footprint.
                for (int dy = 0; dy < 2; ++dy) for (int dx = 0; dx < 2; ++dx)
                    if (x + dx >= 0 && y + dy >= 0 && x + dx < w && y + dy < h)
                        pixels[size_t(y + dy) * size_t(w) + size_t(x + dx)] = rgba(255, 255, 255);
            }
        }
    }
}

} // namespace aiverse::game::minimap
