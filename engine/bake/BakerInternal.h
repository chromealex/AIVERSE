// Light baker internals shared by Baker.cpp (lightmaps), BakerProbes.cpp and Denoise.cpp.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>
#include "BakeScene.h"
#include "Bvh.h"

namespace aiverse::bake::detail {

using render::cross;
using render::dot;
using render::normalize;

inline float3 mulv(float3 a, float3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
inline float luma(float3 c) { return 0.2126f * c.x + 0.7152f * c.y + 0.0722f * c.z; }

// PCG32 keyed by the texel / probe: every sample stream is a pure function of its key (thread-count independent).
struct Rng {
    uint64_t state;
    explicit Rng(uint64_t key) : state(key * 6364136223846793005ull + 1442695040888963407ull) { next(); next(); }
    uint32_t next() {
        const uint64_t old = state;
        state = old * 6364136223846793005ull + 1442695040888963407ull;
        const uint32_t xs = uint32_t(((old >> 18u) ^ old) >> 27u), rot = uint32_t(old >> 59u);
        return (xs >> rot) | (xs << ((32u - rot) & 31u));
    }
    float uniform() { return float(next() >> 8) * (1.0f / 16777216.0f); }
};
inline uint64_t mixKey(uint64_t a, uint64_t b) {
    uint64_t h = a ^ (b + 0x9e3779b97f4a7c15ull + (a << 6) + (a >> 2));
    h ^= h >> 33; h *= 0xff51afd7ed558ccdull; h ^= h >> 33; h *= 0xc4ceb9fe1a85ec53ull; h ^= h >> 33;
    return h;
}

inline void basis(float3 n, float3& t, float3& b) {
    const float s = n.z >= 0 ? 1.0f : -1.0f, a = -1.0f / (s + n.z), bb = n.x * n.y * a;
    t = {1.0f + s * n.x * n.x * a, s * bb, -s * n.x};
    b = {bb, s + n.y * n.y * a, -n.y};
}
inline float3 cosineDir(float3 n, float u1, float u2) {
    const float r = std::sqrt(u1), phi = 6.2831853f * u2;
    float3 t, b; basis(n, t, b);
    const float z = std::sqrt(std::max(0.0f, 1.0f - u1));
    return normalize(t * (r * std::cos(phi)) + b * (r * std::sin(phi)) + n * z);
}
inline float3 coneDir(float3 axis, float cosMax, float u1, float u2) {
    const float c = 1.0f - u1 * (1.0f - cosMax), s = std::sqrt(std::max(0.0f, 1.0f - c * c)), phi = 6.2831853f * u2;
    float3 t, b; basis(axis, t, b);
    return normalize(t * (s * std::cos(phi)) + b * (s * std::sin(phi)) + axis * c);
}

// World-space triangle data for shading hits.
struct Tri {
    float3 n0, n1, n2;     // world vertex normals
    float3 fn;             // world face normal (unit)
    float2 t0, t1, t2;     // texture uv
    uint16_t material;
    int32_t instance;
};

struct Surface { float3 pos, n, albedo, emissive; bool backface; };

struct SceneRT {
    const BakeScene* scene = nullptr;
    const BakeSettings* settings = nullptr;
    Bvh bvh;
    std::vector<Tri> tris;
    float3 bmin, bmax;
    float eps = 1e-3f;              // ray origin offset
    float3 sunDir, sunColor;
    float sunCos = 1;
    std::vector<float3> linearTexCache;   // unused placeholder for future texture caches

    float3 sky(float3 d) const {
        const auto& s = scene->sky;
        const float t = 0.5f + 0.75f * d.y;
        const float3 c = s.ground + (s.sky - s.ground) * t;
        return float3{std::max(c.x, 0.0f), std::max(c.y, 0.0f), std::max(c.z, 0.0f)} * s.intensity;
    }
    float3 albedoAt(const Tri& t, float u, float v) const;
    Surface surface(const BvhHit& h, float3 org, float3 dir) const;
    // Direct light at a point (sun + lights) with one shadow ray each. bakedOnlyLights: only lights with
    // bakedOnly; withSun: include the sun.
    float3 direct(float3 pos, float3 n, Rng& rng, bool withSun, int lightsFilter) const;   // lightsFilter: 0 none, 1 baked-only, 2 all
    // Radiance arriving along dir from org (sky, emissive, bounced direct light).
    float3 radiance(float3 org, float3 dir, Rng& rng, int bounces, bool* backfaceFirst = nullptr, float* firstT = nullptr) const;
};

bool buildSceneRT(const BakeScene& scene, const BakeSettings& settings, SceneRT& rt, std::string& error);
void bakeProbes(const SceneRT& rt, LightProbeGrid& out, const ProgressFn& progress, bool& cancelled);
// Edge-aware a-trous filter of `rgb` (w x h, 3 floats per texel) guided by normals / positions; texels with id < 0 are skipped.
void denoiseTexels(std::vector<float>& rgb, const std::vector<float3>& nrm, const std::vector<float3>& pos, const std::vector<int32_t>& id,
                   int w, int h, float texelWorld);
// Runs fn(i) for i in [0, n) on `threads` threads; returns false when progress() cancelled.
bool parallelFor(int n, int threads, const std::function<void(int)>& fn, const std::string& stage, const ProgressFn& progress);

} // namespace aiverse::bake::detail
