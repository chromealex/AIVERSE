// Minimap: rasterized on the CPU (terrain, fog, units, camera frustum) into a small texture that
// the HUD shows as <img src="#minimap"/>. Cheap enough to redo every frame on weak devices.
#include <algorithm>
#include <cmath>

#include "GameClient.h"
#include "engine/game/RoutesData.h"
#include "engine/render/TextureCache.h"
#include "engine/world/WorldAssets.h"

namespace aiverse::client {

namespace {

constexpr int kTargetSide = 192;  // image pixels on the longer map side

constexpr uint32_t rgba(int r, int g, int b, int a = 255) {
    return uint32_t(r) | uint32_t(g) << 8 | uint32_t(b) << 16 | uint32_t(a) << 24; // bytes in memory: R G B A
}
uint32_t darken(uint32_t c, int num, int den) {
    auto ch = [&](int s) { return int((c >> s) & 255u) * num / den; };
    return rgba(ch(0), ch(8), ch(16));
}

} // namespace

void GameClient::buildMinimap(const Json& map) {
    if (planet()) minimap_.buildPlanet(*dev_, view_);
    else {
        const auto& gs=mod().state(*sim_);
        ::aiverse::view::MapProjection bounds;
        bounds.set(0,0,float(gs.grid.w),float(gs.grid.h));
        const auto grid=bounds;
        auto option=[&](const char* name)->const Json& {
            return map["minimap"].has(name) ? map["minimap"][name] : manifest_["minimap"][name];
        };
        const auto& authored=option("bounds");
        if(authored.isString() && authored.str()=="camera") {
            const auto screen=parseScreen(map["screen"],gs.grid.w,gs.grid.h);
            if(!screen.enabled)throw std::runtime_error("minimap.bounds camera requires map screen");
            if(!bounds.set(float((screen.center.x-screen.size.x/2-screen.left).toDouble()),
                float((screen.center.y-screen.size.y/2-screen.top).toDouble()),
                float((screen.center.x+screen.size.x/2+screen.right).toDouble()),
                float((screen.center.y+screen.size.y/2+screen.bottom).toDouble())))
                throw std::runtime_error("minimap camera bounds must be a finite positive rectangle");
        } else if(!authored.isNull()) {
            if(!authored.isArray() || authored.size()!=4)throw std::runtime_error("minimap.bounds must be camera or [left,top,right,bottom]");
            for(const auto& value:authored.items())if(!value.isNumber())throw std::runtime_error("minimap.bounds requires numbers");
            if(!bounds.set(float(authored[0].toDouble()),float(authored[1].toDouble()),float(authored[2].toDouble()),float(authored[3].toDouble())))
                throw std::runtime_error("minimap.bounds must be a finite positive rectangle");
        }
        if(!bounds.clipTo(grid))throw std::runtime_error("minimap.bounds does not intersect the map");
        const auto& frame=option("cameraFrame");
        if(!frame.isNull() && (!frame.isString() || (frame.str()!="frustum" && frame.str()!="rectangle")))
            throw std::runtime_error("minimap.cameraFrame must be frustum or rectangle");
        minimap_.build(*dev_,gs,map.getStr("minimapBackground",manifest_.getStr("minimapBackground")),bounds,frame.str()=="rectangle");
    }
    ui_.setDynamicTexture("minimap", minimap_.texture(), minimap_.width(), minimap_.height());
}

void Minimap::build(gfx::Device& dev, const GameState& gs, const std::string& background, const ::aiverse::view::MapProjection& bounds, bool rectangle) {
    release(dev);
    gridW_ = gs.grid.w; gridH_ = gs.grid.h;
    bounds_=bounds; rectangle_=rectangle;
    const int side = std::max(1,int(std::ceil(std::max(bounds_.width(),bounds_.height()))));
    const float pxPerCell = side <= kTargetSide ? float(std::max(1, kTargetSide / side)) : float(kTargetSide) / float(side);
    cellsPerPx_ = 1.0f / pxPerCell;
    w_ = std::max(1, int(std::ceil(bounds_.width() * pxPerCell)));
    h_ = std::max(1, int(std::ceil(bounds_.height() * pxPerCell)));
    atlas::Image backgroundImage;
    const bool painted = world::safeAssetPath(background) &&
                         render::loadContentImage(background, backgroundImage) &&
                         backgroundImage.width > 0 && backgroundImage.height > 0;
    const auto& image = backgroundImage.rgba;
    const int iw = backgroundImage.width, ih = backgroundImage.height;
    base_.assign(size_t(w_) * size_t(h_), 0);
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            const int cx = std::min(gridW_ - 1, int(bounds_.point(float(x)/w_,float(y)/h_).x)), cy = std::min(gridH_ - 1, int(bounds_.point(float(x)/w_,float(y)/h_).z));
            const uint8_t f = gs.grid.flags[size_t(gs.grid.index(cx, cy))];
            uint32_t c = rgba(60, 92, 50);
            if (!(f & kCellGround) && (f & kCellWater)) c = rgba(50, 100, 150);
            else if (!(f & kCellGround)) c = rgba(110, 106, 100);
            if (painted) {
                const size_t sx = std::min(size_t(iw-1),size_t(bounds_.point(float(x)/w_,float(y)/h_).x / gridW_ * iw));
                const size_t sy = std::min(size_t(ih-1),size_t(bounds_.point(float(x)/w_,float(y)/h_).z / gridH_ * ih));
                const uint8_t* pixel = image.data() + 4 * (sy * size_t(iw) + sx);
                auto blend = [&](int channel) {
                    const int terrain = int((c >> (channel * 8)) & 255u);
                    return (pixel[channel] * pixel[3] + terrain * (255 - pixel[3]) + 127) / 255;
                };
                c = rgba(blend(0), blend(1), blend(2));
            }
            base_[size_t(y) * size_t(w_) + size_t(x)] = c;
        }
    pixels_ = base_;
    gfx::TextureDesc td;
    td.width = w_; td.height = h_; td.filter = gfx::Filter::Nearest; td.data = pixels_.data(); td.label = "minimap";
    tex_ = dev.createTexture(td);
}

void Minimap::release(gfx::Device& dev) {
    if (tex_.valid()) dev.destroyTexture(tex_);
    tex_ = {};
}

void Minimap::update(gfx::Device& dev, const GameState& gs, const Rules& rules, const GameScene& scene, const std::vector<uint8_t>* vision,
                     const float3 (&q)[4], bool haveQuad, const std::vector<OverlayMinimapMark>* marks) {
    if (!tex_.valid() || gs.grid.w != gridW_ || gs.grid.h != gridH_) return;
    const float scaleX = float(w_) / bounds_.width(), scaleZ = float(h_) / bounds_.height();
    for (int y = 0; y < h_; ++y)
        for (int x = 0; x < w_; ++x) {
            const size_t i = size_t(y) * size_t(w_) + size_t(x);
            uint32_t c = base_[i];
            if (vision) {
                const int cx = std::min(gridW_ - 1, int(bounds_.point(float(x)/w_,float(y)/h_).x)), cy = std::min(gridH_ - 1, int(bounds_.point(float(x)/w_,float(y)/h_).z));
                const uint8_t v = (*vision)[size_t(gs.grid.index(cx, cy))];
                if (v == 0) c = rgba(12, 14, 18);
                else if (v == 1) c = darken(c, 3, 5);
            }
            pixels_[i] = c;
        }
    auto fill = [&](float wx, float wz, float r, uint32_t c) {
        if(!bounds_.intersects(wx,wz,r))return;
        wx-=bounds_.left; wz-=bounds_.top;
        auto span = [&](float lo, float hi, float scale, int n, int& a, int& b) {
            a = int(std::floor(lo * scale));
            b = std::max(a, int(std::ceil(hi * scale)) - 1);
            a = std::clamp(a, 0, n - 1); b = std::clamp(b, 0, n - 1);
        };
        int x0, x1, y0, y1;
        span(wx - r, wx + r, scaleX, w_, x0, x1);
        span(wz - r, wz + r, scaleZ, h_, y0, y1);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) pixels_[size_t(y) * size_t(w_) + size_t(x)] = c;
    };
    for (auto& o : scene.objects) {
        if (!o.visible || o.isProjectile || o.type>=rules.types.size()) continue;
        const UnitType& t = rules.types[o.type];
        if(!t.view.getBool("minimap",true))continue;
        const bool node = o.owner == 0 && t.resourceNode >= 0;
        if (!t.view.has("minimap") && node && t.footprintW <= 1) continue; // trees: too many dots
        const float4 pc = GameView::playerColor(o.owner);
        const uint32_t c = node ? rgba(255, 216, 50) : rgba(int(pc.x * 255), int(pc.y * 255), int(pc.z * 255));
        const float r = t.structure ? std::max(1.0f, float(std::max(t.footprintW, t.footprintH)) * 0.5f) : 0.6f;
        fill(o.pos.x, o.pos.z, std::max(r, cellsPerPx_ * 0.75f), c);
    }
    if (marks) // overlays with style.space "minimap" (pings, alerts): rings / discs blended over the image
        for (auto& m : *marks) {
            const float a = float(m.rgba >> 24) / 255.0f;
            const float cx = (m.x-bounds_.left) * scaleX, cy = (m.z-bounds_.top) * scaleZ;
            const float rx = std::max(1.0f, m.radius * scaleX), rz = std::max(1.0f, m.radius * scaleZ);
            const int x0 = std::max(0, int(cx - rx - 1)), x1 = std::min(w_ - 1, int(cx + rx + 1));
            const int y0 = std::max(0, int(cy - rz - 1)), y1 = std::min(h_ - 1, int(cy + rz + 1));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float d = std::hypot((x + 0.5f - cx)/rx, (y + 0.5f - cy)/rz);
                    if (m.ring ? std::abs(d - 1.0f) > 0.75f/std::min(rx,rz) : d > 1.0f) continue;
                    uint32_t& px = pixels_[size_t(y) * size_t(w_) + size_t(x)];
                    auto mix = [&](int s) { return int(float((px >> s) & 255u) * (1 - a) + float((m.rgba >> s) & 255u) * a); };
                    px = rgba(mix(0), mix(8), mix(16));
                }
        }
    if (haveQuad) { // camera footprint in the same projection as pointer input
        const auto rectangle=::aiverse::view::MapProjection::rectangularFrame(q);
        for (int k = 0; k < 4; ++k) {
            const float3 a = rectangle_ ? rectangle[size_t(k)] : q[k], b = rectangle_ ? rectangle[size_t((k+1)%4)] : q[(k + 1) % 4];
            const int steps = int(std::max(std::abs(b.x - a.x)*scaleX, std::abs(b.z - a.z)*scaleZ)) + 1;
            for (int s = 0; s <= steps; ++s) {
                const float t = float(s) / float(steps);
                const int x = int(std::floor((a.x - bounds_.left + (b.x - a.x) * t) * scaleX)), y = int(std::floor((a.z - bounds_.top + (b.z - a.z) * t) * scaleZ));
                // The HUD can downsample this raster (desktop 190dp, touch
                // 150dp). A one-texel edge can disappear under nearest sampling.
                // Cover two texels for every camera footprint, not just rectangles.
                for (int dy=0;dy<2;++dy) for (int dx=0;dx<2;++dx)
                    if (x+dx >= 0 && y+dy >= 0 && x+dx < w_ && y+dy < h_)
                        pixels_[size_t(y+dy) * size_t(w_) + size_t(x+dx)] = rgba(255, 255, 255);
            }
        }
    }
    dev.updateTexture(tex_, 0, 0, w_, h_, pixels_.data(), w_ * 4);
}

} // namespace aiverse::client
