// Light baker input and settings (agent-lightbake). A bake scene is plain geometry in object space
// (meshes with lightmap UVs), instances placed in the world, materials (albedo / emissive, optional
// texture for albedo and alpha test), the sun, the sky and point / spot lights.
//
// Units follow the lit shaders (Renderer3D): a surface's outgoing light = albedo * irradiance, where a
// directional light adds color * max(n.l, 0) and a uniform sky of radiance s adds s (no 1 / pi).
// The sky radiance towards direction d is mix(ground, sky, 0.5 + 0.75 d.y), clamped: a flat open plane
// then bakes to exactly the realtime ambient mix(ground, sky, n.y * 0.5 + 0.5).
#pragma once
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include "../render/Model.h"
#include "LightmapData.h"

namespace aiverse::bake {

using render::float2;
using render::float3;
using render::mat4;

struct BakeMaterial {
    float3 albedo{0.8f, 0.8f, 0.8f};        // linear, multiplies the texture
    float3 emissive{0, 0, 0};               // linear radiance (already * strength)
    std::shared_ptr<const render::Image> texture;   // sRGB RGBA8 base colour (CPU pixels), optional
    float alphaCutoff = -1;                 // >= 0: alpha-tested against the texture alpha (foliage)
    bool doubleSided = false;
};

struct BakeMesh {
    std::vector<float3> pos, nrm;           // object space
    std::vector<float2> uv, uv2;            // uv2: lightmap UVs in [0, 1] (empty: the mesh only casts)
    std::vector<uint32_t> idx;
    std::vector<uint16_t> triMaterial;      // per triangle, index into BakeScene::materials
};

struct BakeInstance {
    int mesh = -1;
    mat4 world;
    uint64_t key = 0;                       // lightmapRegionKey of the drawn node
    bool receive = true;                    // gets a lightmap region (needs uv2)
    bool cast = true;                       // blocks / bounces light
    float scale = 1;                        // texel density multiplier
    std::string name;                       // for messages
    std::vector<uint16_t> materials;        // mesh material index -> BakeScene::materials (empty: the indices are scene materials)
};

struct BakeLight {
    float3 pos, color{1, 1, 1};             // color = linear rgb * intensity
    float radius = 4;                       // reaches zero here: (1 - d^2 / r^2)^2, as the realtime lights
    float3 dir{0, -1, 0};
    float cosOuter = -2, cosInner = -2;     // spot when cosOuter > -1
    bool bakedOnly = true;                  // false (mixed): only its bounce goes into the lightmap
};

struct BakeScene {
    std::vector<BakeMaterial> materials;
    std::vector<BakeMesh> meshes;
    std::vector<BakeInstance> instances;
    struct Sun { bool on = true; float3 dir{0.55f, 1.0f, 0.35f}; float3 color{1, 0.86f, 0.64f}; float angle = 1.5f; } sun;   // dir towards the sun, angle: degrees (penumbra)
    struct Sky { float3 sky{0.25f, 0.34f, 0.49f}, ground{0.11f, 0.08f, 0.05f}; float intensity = 1; } sky;
    std::vector<BakeLight> lights;
    // terrain: an instance whose uv2 is its normalized world xz over [origin, origin + extent]
    struct Terrain { int instance = -1; float origin[2] = {0, 0}, extent[2] = {1, 1}; } terrain;
    std::vector<std::string> sources;       // files the scene came from (hashed by the caller)
    uint64_t sourceHash = 0;                // hash of those files' bytes (models, textures, map)
};

struct BakeSettings {
    BakedMode mode = BakedMode::Shadowmask;
    float texelsPerUnit = 4;                // lightmap texels per world unit
    int pageSize = 1024;                    // max atlas page size (power of two)
    int minRegion = 4, maxRegion = 512;     // texels per side of one instance
    int padding = 2;                        // texels between charts / regions
    int samples = 64;                       // hemisphere samples per texel
    int bounces = 2;                        // indirect bounces
    float aoDistance = 0;                   // > 0: extra ambient occlusion within this distance
    float aoStrength = 1;
    bool denoise = true;
    float indirectIntensity = 1;
    int maskSamples = 16;                   // sun visibility samples (soft shadow mask / direct sun)
    float probeSpacing = 2;                 // world units between light probes (0 = no probes)
    float probeHeight = 4;                  // probe volume above the scene bounds' floor
    int probeSamples = 256;
    int maxProbes = 65536;
    int threads = 0;                        // 0 = hardware threads
    uint32_t seed = 1;
    float rgbmRange = 8;
};

// Fraction 0..1 of the stage; return false to cancel. Called from the baking thread.
using ProgressFn = std::function<bool(const std::string& stage, float fraction)>;

// Deterministic content hash of the scene and settings (+ sourceHash): equal hash = same bake.
uint64_t hashBakeInput(const BakeScene& scene, const BakeSettings& settings);

struct BakeStats { int instances = 0, regions = 0, pages = 0, texels = 0, probes = 0, triangles = 0; double seconds = 0; };

// Bakes lightmaps + probes. Output is bit-identical for the same input regardless of thread count.
bool bakeLighting(const BakeScene& scene, const BakeSettings& settings, LightmapData& out, std::string& error,
                  const ProgressFn& progress = {}, BakeStats* stats = nullptr);

// Settings <-> JSON (editor, MCP, the bake header). Unknown keys are ignored; values are clamped.
std::string settingsToJson(const BakeSettings& s);
bool settingsFromJson(const std::string& json, BakeSettings& s, std::string& error);

// Writes a bake folder (lightmap.json + PNG pages + probes.bin) through `write` (host only).
bool saveLightmapData(const std::string& folder, const LightmapData& data, const WriteFn& write, std::string& error);

} // namespace aiverse::bake
