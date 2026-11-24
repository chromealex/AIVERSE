// Legacy cell water -> NEREID surfaces (docs/nereid/legacy-water-migration.md).
//
// The legacy RTS water renderer (TerrainRenderer::buildWater) draws the map's
// baked water cells ('~', kCellWater) either as one sheet under the whole map
// or as one surface per connected body at its lowest shore. This converter
// reads exactly that layout (TerrainRenderer::legacyWaterLayout - the same
// bodies, cells and levels the legacy mesh uses) and writes a canonical
// `nereidWater` manifest, parsed by the ordinary NEREID parser:
//   sheet  -> one Box over the map and its apron at the sheet height;
//   body   -> one Box over the body's bounding cells when the terrain hides
//             every other cell of that rectangle (all ground >= level), else
//             an exact disjoint rectangle cover of the body's cells.
// More than NereidWaterScene::kMaxSurfaces surfaces, or a manifest the parser
// rejects, fails closed: the caller keeps the legacy mesh and reports why.
// Presentation only: the simulation keeps reading the baked cells; nothing
// here writes the map, its cells, waterNetwork or the checksum.
#pragma once
#include "engine/data/Json.h"
#include "engine/render/Renderer3D.h"
#include "engine/world/NereidWaterManifest.h"
#include "engine/world/nereid/NereidRasterImport.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace aiverse::game {

struct LegacyWaterLayout {
    struct Body {
        std::vector<int> cells;  // sorted row-major cells the legacy mesh covers (incl. grown shore)
        std::vector<int> water;  // the body's own water cells
        float level = 0;         // legacy surface height (lowest shore + waterOffset)
    };
    int width = 0, height = 0;
    float apron = 0;
    bool sheet = false, any = false;
    float sheetY = 0;
    std::vector<Body> bodies;
};

namespace water_nereid_detail {
inline Json num(double v) { return Json::number(v, 4); }
inline Json arr(std::initializer_list<double> v) { Json a = Json::array(); for (double x : v) a.push(num(x)); return a; }
// Surface keys a game may override; geometry and identity stay converter-owned.
inline bool overridable(const std::string& k) {
    return k != "id" && k != "shape" && k != "center" && k != "halfExtents" && k != "surfaceY" &&
           k != "bottomY" && k != "points" && k != "style" && k != "enabled";
}
} // namespace water_nereid_detail

// Default NEREID look derived from the legacy tint (rgb + alpha). `overrides`
// is terrain.water.nereid: surface keys (lod, uvScale, ...) and a "style" object.
inline Json legacyWaterNereidStyle(render::float4 color, const Json& overrides) {
    using namespace water_nereid_detail;
    Json style = Json::object();
    // Keep the legacy tint readable: strong depth tint and absorption over the
    // shallow dug cells, a thin contact-foam line instead of a vertex rim.
    style.set("deepColor", arr({std::clamp(color.x * 0.8, 0.0, 1.0), std::clamp(color.y * 0.9, 0.0, 1.0),
                                std::clamp(color.z * 0.95, 0.0, 1.0)}));
    style.set("opacity", num(std::clamp(double(color.w), 0.001, 0.98)));
    style.set("absorption", num(1.2));
    style.set("depthTint", num(0.85));
    style.set("shoreFoamDepth", num(0.12));
    style.set("foamStrength", num(0.0));
    if (overrides.isObject() && overrides["style"].isObject())
        for (const auto& [k, v] : overrides["style"].members()) style.set(k, v);
    return style;
}

inline std::optional<Json> legacyWaterToNereidManifest(
        const LegacyWaterLayout& layout, render::float4 color, const Json& overrides,
        const std::function<float(float, float)>& groundAt, std::string* why) {
    using namespace water_nereid_detail;
    auto fail = [&](std::string reason) -> std::optional<Json> { if (why) *why = std::move(reason); return std::nullopt; };
    if (layout.width <= 0 || layout.height <= 0) return fail("empty map");
    Json surfaces = Json::array();
    const Json style = legacyWaterNereidStyle(color, overrides);
    int64_t nextId = 1;
    auto box = [&](double x0, double z0, double x1, double z1, double level) {
        Json s = Json::object();
        s.set("id", Json::number(nextId++));
        s.set("kind", Json::string("lake"));
        s.set("shape", Json::string("box"));
        s.set("center", arr({(x0 + x1) * 0.5, level, (z0 + z1) * 0.5}));
        s.set("halfExtents", arr({(x1 - x0) * 0.5, (z1 - z0) * 0.5}));
        s.set("surfaceY", num(level));
        s.set("bottomY", num(level - 4.0));
        s.set("foamWidth", num(0.0));   // box edges are map edges or under ground: no vertex foam
        s.set("lod", Json::string("medium"));
        if (overrides.isObject())
            for (const auto& [k, v] : overrides.members()) if (overridable(k)) s.set(k, v);
        s.set("style", style);
        surfaces.push(std::move(s));
    };
    if (layout.sheet) {
        if (layout.any) {
            const double a = layout.apron;
            box(-a, -a, layout.width + a, layout.height + a, layout.sheetY);
        }
    } else {
        const int W = layout.width, H = layout.height;
        for (const auto& b : layout.bodies) {
            if (b.cells.empty()) continue;
            int x0 = W, z0 = H, x1 = -1, z1 = -1;
            std::vector<uint8_t> in(size_t(W) * size_t(H), 0);
            for (int c : b.cells) {
                in[size_t(c)] = 1;
                x0 = std::min(x0, c % W); x1 = std::max(x1, c % W);
                z0 = std::min(z0, c / W); z1 = std::max(z1, c / W);
            }
            // A bounding box is exact when the terrain covers every other cell
            // of it (ground at all corners and the centre above the surface).
            bool hidden = true;
            for (int z = z0; z <= z1 && hidden; ++z)
                for (int x = x0; x <= x1 && hidden; ++x) {
                    if (in[size_t(z) * size_t(W) + size_t(x)]) continue;
                    for (auto [px, pz] : {std::pair{0.f, 0.f}, std::pair{1.f, 0.f}, std::pair{0.f, 1.f},
                                          std::pair{1.f, 1.f}, std::pair{.5f, .5f}})
                        if (groundAt(float(x) + px, float(z) + pz) < b.level + 0.02f) { hidden = false; break; }
                }
            if (hidden) { box(x0, z0, x1 + 1, z1 + 1, b.level); continue; }
            for (const auto& r : lib::nereid::rectangleWaterCells(in, W, H))
                box(r.x0, r.z0, r.x1, r.z1, b.level);
        }
    }
    if (surfaces.size() > world::NereidWaterScene::kMaxSurfaces)
        return fail("needs " + std::to_string(surfaces.size()) + " NEREID surfaces (limit " +
                    std::to_string(world::NereidWaterScene::kMaxSurfaces) + ")");
    Json water = Json::object();
    water.set("surfaces", std::move(surfaces));
    Json root = Json::object();
    root.set("nereidWater", std::move(water));
    try { (void)world::parseNereidWaterManifest(root); }
    catch (const std::exception& e) { return fail(std::string("NEREID parser rejected the conversion: ") + e.what()); }
    return root;
}

} // namespace aiverse::game
