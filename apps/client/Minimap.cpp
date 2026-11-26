// Minimap: rasterized on the CPU (terrain, fog, units, camera frustum) into a small texture that
// the HUD shows as <img src="#minimap"/>. Cheap enough to redo every frame on weak devices.
#include <algorithm>
#include <cmath>

#include "GameClient.h"
#include "engine/view/game/PlanarMinimap.h"
#include "engine/world/WorldAssets.h"

namespace aiverse::client {

void GameClient::buildMinimap(const Json& map) {
    if (planet()) minimap_.buildPlanet(*dev_, view_);
    else minimap_.build(*dev_, mod().state(*sim_), map.getStr("minimapBackground", manifest_.getStr("minimapBackground")));
    ui_.setDynamicTexture("minimap", minimap_.texture(), minimap_.width(), minimap_.height());
}

void Minimap::build(gfx::Device& dev, const GameState& gs, const std::string& background) {
    release(dev);
    gridW_ = gs.grid.w; gridH_ = gs.grid.h;
    std::string bytes;
    if (world::safeAssetPath(background)) platform::readContentFile(background, bytes);
    minimap::build(gs.grid, bytes, w_, h_, cellsPerPx_, base_);
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
    minimap::rasterize(gs.grid, rules, scene, vision, marks, w_, h_, cellsPerPx_, base_, pixels_);
    const float ppc = 1.0f / cellsPerPx_;
    if (haveQuad) { // camera frustum on the ground
        for (int k = 0; k < 4; ++k) {
            const float3 a = q[k], b = q[(k + 1) % 4];
            const int steps = int(std::max(std::abs(b.x - a.x), std::abs(b.z - a.z)) * ppc) + 1;
            for (int s = 0; s <= steps; ++s) {
                const float t = float(s) / float(steps);
                const int x = int((a.x + (b.x - a.x) * t) * ppc), y = int((a.z + (b.z - a.z) * t) * ppc);
                if (x >= 0 && y >= 0 && x < w_ && y < h_) pixels_[size_t(y) * size_t(w_) + size_t(x)] = minimap::rgba(255, 255, 255);
            }
        }
    }
    dev.updateTexture(tex_, 0, 0, w_, h_, pixels_.data(), w_ * 4);
}

} // namespace aiverse::client
