// Road kit of the view: procedural tiling textures (asphalt, concrete, cobbles, paving, gravel, dirt, grass) and the
// built-in prop shapes (lamp, tree, bush, flowerbed, bollard, pole, bench, sign) as triangles per kit material.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "engine/render/Math3D.h"
#include "engine/render/Model.h"
#include "RoadNet.h"

namespace aiverse::roads::roadkit {

// RGBA8 tile of `size` x `size` (seamless) for a kit material (RoadView::Mat); empty: plain colour material
render::Image texture(int mat, int size = 256);
// base colour (linear) and roughness / metallic of a kit material
render::Material material(int mat);

struct Tri { int mat; render::float3 a, b, c; };

// The carriageway texture of a road type with its markings painted in: u runs across the whole carriageway (type
// frame: 0 at the left edge of the backward lanes, 1 at the right edge of the forward lanes, the median included),
// v along the road, one period of the dash pattern (kRoadPaintPeriod metres) per texture height.
// variant bits: 1 = no right edge line, 2 = no left edge line (a branch or a bus stop bay opens that side). 4 = no lines at all, 8 = no wheel tracks, 16 = no centre line (a driveway crosses).
inline constexpr float kRoadPaintPeriod = 12.0f;
render::Image roadSurface(const RoadTypeDef& t, int variant);
// the texture reaches this far (cells) past each carriageway edge (plain asphalt: bus stop bays, tapers)
float paintMargin(const RoadTypeDef& t);
// Verges of roads without a curb: one strip texture per surface (gravel, grass, dirt side by side, 256 x 256 each),
// u across (0: over the asphalt edge, ragged; 1: faded into the ground), v along (tiles every kVergePeriod metres).
inline constexpr float kVergePeriod = 6.0f;
inline constexpr int kVergeSlots = 10;       // gravel, grass, dirt verges; asphalt wearing into dirt; a wheel track; dirt road surface; plain white (wet film); a puddle; an asphalt patch; a crack
inline constexpr int kVergeWearSlot = 3, kVergeTrackSlot = 4, kVergeDirtSlot = 5, kVergeFilmSlot = 6, kVergePuddleSlot = 7, kVergePatchSlot = 8, kVergeCrackSlot = 9;
std::vector<uint32_t> vergeAtlas(int cell);
inline int vergeSlot(RoadSurface s) { return s == RoadSurface::Grass ? 1 : s == RoadSurface::Dirt ? 2 : 0; }
bool roadSurfacePainted(const RoadTypeDef& t);   // the type has lines worth a texture of its own

// Road paint decals: one atlas of white marks (tinted per quad) in a 4 x 4 grid. Arrows and text point to v = 0.
enum Mark : int { kMarkLine = 0, kMarkArrowS, kMarkArrowL, kMarkArrowR, kMarkArrowSL, kMarkArrowSR, kMarkStop, kMarkBus, kMarkArrowLR, kMarkCount };
std::vector<uint32_t> markingAtlas(int cell);          // (4 * cell)^2 RGBA8 (r in the low byte), white + alpha
inline render::float4 markUv(int mark) {                // u0, v0, u1, v1 of a mark (with a small inset)
    const float c = 0.25f, x = float(mark % 4) * c, y = float(mark / 4) * c, in = c * 0.02f;
    return {x + in, y + in, x + c - in, y + c - in};
}
// a built-in prop shape at the origin, up +y, facing +x (the road's forward), `s` world units per metre
void shape(const std::string& id, float s, std::vector<Tri>& out);

} // namespace aiverse::roads::roadkit
