// Minimap: rasterized on the CPU (terrain, fog, units, camera frustum) into a small texture that
// the HUD shows as <img src="#minimap"/>. Cheap enough to redo every frame on weak devices.
#include <algorithm>
#include <cmath>

#include "GameClient.h"
#include "engine/view/game/PlanarMinimap.h"
#include "engine/render/TextureCache.h"
#include "engine/world/WorldAssets.h"

namespace aiverse::client {

void GameClient::buildMinimap(const Json& map) {
    if (planet()) minimap_.buildPlanet(*dev_, view_);
    else {
        const auto& gs=mod().state(*sim_);
        const auto layout=game::minimap::resolveLayout(map,manifest_,gs.grid.w,gs.grid.h);
        minimap_.build(*dev_,gs,map.getStr("minimapBackground",manifest_.getStr("minimapBackground")),layout);
    }
    ui_.setDynamicTexture("minimap", minimap_.texture(), minimap_.width(), minimap_.height());
}

void Minimap::build(gfx::Device& dev, const GameState& gs, const std::string& background, const game::minimap::Layout& layout) {
    release(dev);
    atlas::Image image;
    const bool painted = world::safeAssetPath(background) && render::loadContentImage(background, image);
    raster_ = game::minimap::build(gs.grid, layout, painted ? image.rgba : std::vector<uint8_t>{},
                                   painted ? image.width : 0, painted ? image.height : 0);
    gridW_ = raster_.gridW; gridH_ = raster_.gridH;
    bounds_ = raster_.bounds; rectangle_ = raster_.rectangle;
    cellsPerPx_ = raster_.cellsPerPixel;
    w_ = raster_.width; h_ = raster_.height;
    base_ = raster_.base;
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
    if (!tex_.valid() || gs.grid.w != gridW_ || gs.grid.h != gridH_ || raster_.base.empty()) return;
    game::minimap::rasterize(raster_, gs.grid, rules, scene, vision, marks, haveQuad ? &q : nullptr, pixels_);
    dev.updateTexture(tex_, 0, 0, w_, h_, pixels_.data(), w_ * 4);
}

} // namespace aiverse::client
