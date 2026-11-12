// Baked lighting of a map, CPU side (agent-lightbake): lightmap pages, where each static model node
// sits in them, the terrain lightmap and a light probe grid for dynamic objects. GPU-free: the editor,
// the MCP server and tests read it; engine/render/BakedLighting.h uploads it.
//
// Stored with the map as a folder "maps/<map>.bake/" (rpak-friendly, every file already compressed):
//   lightmap.json   header: version, content hash, mode, pages, regions, terrain, probes, settings, stats
//   lm<i>.png       RGBM lightmap page i (linear rgb * a * rgbmRange)
//   mask<i>.png     shadow mask page i (r = sun visibility; shadowmask mode)
//   probes.bin      light probes: float16 x 12 per probe (ambient a.rgb, gradient bx.rgb, by.rgb, bz.rgb:
//                   irradiance(n) = max(a + bx n.x + by n.y + bz n.z, 0), the lit shaders' ambient convention)
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include "../render/Math3D.h"

namespace aiverse::bake {

using render::float3;
using render::mat4;

// How baked and realtime light mix (Unity's mixed lighting modes):
//   Indirect     lightmap = indirect light (+ baked-only lights); the sun stays realtime with realtime shadows
//   Shadowmask   as Indirect + a baked sun shadow mask: static shadows beyond the shadow distance / without shadows
//   Subtractive  lightmap = everything incl. the direct sun; realtime shadows of dynamic objects darken it
enum class BakedMode : uint8_t { Indirect, Shadowmask, Subtractive };
const char* bakedModeName(BakedMode m);
bool parseBakedMode(const std::string& s, BakedMode& out);

struct LightmapPage {
    int width = 0, height = 0;
    std::vector<uint8_t> rgbm;   // RGBA8
    std::vector<uint8_t> mask;   // RGBA8 (r = sun visibility), empty without a mask
};
struct LightmapRegion { uint16_t page = 0; float st[4] = {1, 1, 0, 0}; };   // page uv = uv2 * st.xy + st.zw

struct LightProbeGrid {
    float3 origin, cell{1, 1, 1};
    int nx = 0, ny = 0, nz = 0;
    std::vector<float> coeffs;   // 12 per probe, probe index = x + nx * (y + ny * z)
    bool empty() const { return coeffs.empty(); }
    // CPU evaluation (tests, tools): trilinear in the grid, clamped at its border.
    float3 irradiance(float3 pos, float3 n) const;
};

struct LightmapData {
    int version = 1;
    uint64_t hash = 0;                     // content hash of everything the bake depended on (rebake skip)
    BakedMode mode = BakedMode::Shadowmask;
    float rgbmRange = 8, intensity = 1;
    float3 shadowColor{0.08f, 0.09f, 0.12f};   // subtractive: darkest a realtime shadow makes the lightmap (linear)
    std::vector<LightmapPage> pages;
    std::map<uint64_t, LightmapRegion> regions;   // lightmapRegionKey -> region
    struct Terrain { int page = -1; float st[4] = {1, 1, 0, 0}; float origin[2] = {0, 0}, extent[2] = {1, 1}; } terrain;
    LightProbeGrid probes;
    std::string settingsJson = "{}", statsJson = "{}";   // what it was baked with / how long it took (info)
};

// Key of one static model node: model path, node index and the node's world matrix (quantized, so the
// same placement computed again on another run maps to the same region).
uint64_t lightmapRegionKey(const std::string& modelPath, int node, const mat4& world);
uint64_t fnv1a(const void* data, size_t n, uint64_t h = 1469598103934665603ull);
std::string hex64(uint64_t v);
bool parseHex64(const std::string& s, uint64_t& v);

// RGBM (linear, range r): encode / decode one texel.
void encodeRgbm(float3 c, float range, uint8_t out[4]);
float3 decodeRgbm(const uint8_t in[4], float range);

using ReadFn = std::function<bool(const std::string& path, std::string& out)>;
using WriteFn = std::function<bool(const std::string& path, const std::string& bytes)>;
// "maps/<id>.json" -> "maps/<id>.bake"
std::string bakeFolderFor(const std::string& mapPath);
// Reads a bake folder. False (with a readable error) when missing or broken; pages decoded to RGBA8.
bool loadLightmapData(const std::string& folder, const ReadFn& read, LightmapData& out, std::string& error);
// Only the header (hash, mode, counts): cheap check whether a rebake is needed.
bool readLightmapHeader(const std::string& folder, const ReadFn& read, LightmapData& out, std::string& error);

} // namespace aiverse::bake
