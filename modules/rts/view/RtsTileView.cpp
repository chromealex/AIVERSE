// Tile maps (Rts2DView, tile part): the map's "tiles" layer resolved through the tileset's
// autotile rules into one static mesh of atlas quads (at most two per cell: `under` + top).
#include <algorithm>
#include <cstdio>
#include "../../../engine/platform/Platform.h"
#include "Rts2DView.h"

namespace aiverse::game {

using namespace render;

void Rts2DView::setTiles(const Json& tiles) {
    if (!tiles.isObject()) { tileset_.reset(); tilesetPath_.clear(); tilesJson_ = Json(); return; }
    tilesJson_ = tiles;
    std::string path = tiles.getStr("tileset");
    if (tileset_ && path == tilesetPath_) return;
    tileset_.reset();
    tilesetPath_ = path;
    std::string text;
    try {
        if (!platform::readContentFile(base_ + path, text)) throw std::runtime_error("tileset not found: " + path);
        tileset_ = std::make_unique<Tileset>(loadTileset(Json::parse(text)));
    } catch (const std::exception& e) {
        std::fprintf(stderr, "[tiles] %s\n", e.what());
    }
}

int Rts2DView::depletedOf(int terrain) const {
    const auto& t = tileset_->terrains[size_t(terrain)];
    return t.fieldResource.empty() ? -1 : t.depleted;
}

void Rts2DView::buildTiles(gfx::Device& dev, const MapGrid& grid, float lh) {
    if(preparingFrame_ && frameTilesReplaced_)
        throw std::logic_error("sprite tiles prepared twice");
    if (!tileset_ || grid.w == 0) {
        if(!preparingFrame_)tileRetirement_.retireBuffer(dev,tileMesh_.vb);
        tileMesh_={};fieldCells_.clear();fieldState_.clear();
        if(preparingFrame_)frameTilesReplaced_=true;
        return;
    }
    render::TextureRef nextAtlas;
    TileLayer nextLayer;
    std::vector<uint32_t> nextCells;
    std::vector<uint8_t> nextState;
    const Tileset& ts = *tileset_;
    nextAtlas = textures_.resolve(ts.atlas, ts.nearest, base_);
    if (!nextAtlas.valid()) throw std::runtime_error("sprite tile atlas unavailable");
    nextLayer = loadTileLayer(tilesJson_, ts, grid.w, grid.h);
    // used-up resource cells show their "depleted" terrain (view only; the sim owns the amounts)
    TileLayer shown = nextLayer;
    for (int i = 0; i < grid.w * grid.h; ++i) {
        int d = depletedOf(nextLayer.terrain[size_t(i)]);
        if (d < 0) continue;
        bool has = grid.fieldAmt[size_t(i)].raw > 0;
        nextCells.push_back(uint32_t(i));
        nextState.push_back(has);
        if (!has) shown.terrain[size_t(i)] = uint8_t(d);
    }
    const float tw = float(ts.tileW) / float(nextAtlas.width), th = float(ts.tileH) / float(nextAtlas.height);
    const float insetX = 0.5f / float(nextAtlas.width), insetY = 0.5f / float(nextAtlas.height); // Kingston: clamp bilinear footprint to tile texel centres
    auto uvOf = [&](int tile) {
        float u = float(tile % ts.columns) * tw, v = float(tile / ts.columns) * th;
        return float4{u + insetX, v + insetY, u + tw - insetX, v + th - insetY};
    };
    // Presentation mask only: retain terrain and navigation for the simulation.
    std::vector<uint8_t> covered(size_t(grid.w * grid.h), 0);
    for (const auto& cell : tilesJson_["coveredCells"].items()) {
        const int x = cell[0].integer(-1), y = cell[1].integer(-1);
        if (x >= 0 && y >= 0 && x < grid.w && y < grid.h)
            covered[size_t(grid.index(x, y))] = 1;
    }
    std::vector<SpriteVertex> verts;
    verts.reserve(size_t(grid.w * grid.h) * 4);
    for (int y = 0; y < grid.h; ++y)
        for (int x = 0; x < grid.w; ++x) {
            size_t i = size_t(grid.index(x, y));
            if (covered[i]) continue;
            float gy = float(grid.level[i]) * lh + float(grid.height[i]) / 16.0f;
            const float3 c[4] = {{float(x), gy, float(y)}, {float(x + 1), gy, float(y)}, {float(x + 1), gy, float(y + 1)}, {float(x), gy, float(y + 1)}};
            CellTiles ct = resolveCell(ts, shown, x, y);
            if (ct.under >= 0) SpriteBatch::appendQuad(verts, nextAtlas, c, uvOf(ct.under), 0xffffffffu);
            if (ct.top >= 0) SpriteBatch::appendQuad(verts, nextAtlas, c, uvOf(ct.top), 0xffffffffu);
        }
    auto nextMesh=batch_.buildMesh(dev,verts);
    if(!verts.empty() && !nextMesh.vb.valid())
        throw std::runtime_error("sprite tile candidate allocation failed");
    if(!preparingFrame_)tileRetirement_.retireBuffer(dev,tileMesh_.vb);
    tileMesh_=nextMesh;
    layer_=std::move(nextLayer);atlas_=nextAtlas;
    fieldCells_.swap(nextCells);fieldState_.swap(nextState);
    if(preparingFrame_)frameTilesReplaced_=true;
}

void Rts2DView::refreshTiles(gfx::Device& dev, const MapGrid& grid, float lh) {
    if (!tileset_ || fieldCells_.empty()) return;
    for (size_t k = 0; k < fieldCells_.size(); ++k) {
        bool has = grid.fieldAmt[fieldCells_[k]].raw > 0;
        if (has != bool(fieldState_[k])) { buildTiles(dev, grid, lh); return; }
    }
}

} // namespace aiverse::game
