// AIVERSE Studio — CPU terrain preview adapter for the current MapDoc document schema.
// No ImGui, GPU APIs, or genre-specific rendering calls. Not a replacement for
// the engine's full terrain/detail/water/planet renderer.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include "engine/render/Renderer3D.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace aiverse::studio {
struct TerrainPreviewStats {
    int step = 0;
    size_t quads = 0;
};

inline float terrainPreviewCorner(const editor::MapData& d, int x, int z, float levelHeight = 1.5f) {
    if (d.w <= 0 || d.h <= 0) return 0.0f;
    x = std::clamp(x, 0, d.w);
    z = std::clamp(z, 0, d.h);
    const size_t cornerCount = size_t(d.w + 1) * size_t(d.h + 1);
    if (d.corners.size() == cornerCount)
        return float(d.corners[size_t(z) * size_t(d.w + 1) + size_t(x)]) / 16.0f;
    const int cx = std::clamp(x, 0, d.w - 1);
    const int cz = std::clamp(z, 0, d.h - 1);
    const size_t offset = size_t(cz) * size_t(d.w) + size_t(cx);
    const float fine = offset < d.heights.size() ? float(d.heights[offset]) / 16.0f : 0.0f;
    const float cliff = offset < d.levels.size() ? float(d.levels[offset]) * levelHeight : 0.0f;
    return fine + cliff;
}

inline float terrainPreviewHeight(const editor::MapData& d, float x, float z, float levelHeight = 1.5f) {
    if (d.w <= 0 || d.h <= 0) return 0;
    x = std::clamp(x, 0.0f, float(d.w));
    z = std::clamp(z, 0.0f, float(d.h));
    const int x0 = int(std::floor(x)), z0 = int(std::floor(z));
    const int x1 = std::min(x0 + 1, d.w), z1 = std::min(z0 + 1, d.h);
    const float tx = x - float(x0), tz = z - float(z0);
    const float a = terrainPreviewCorner(d, x0, z0, levelHeight);
    const float b = terrainPreviewCorner(d, x1, z0, levelHeight);
    const float c = terrainPreviewCorner(d, x1, z1, levelHeight);
    const float e = terrainPreviewCorner(d, x0, z1, levelHeight);
    return (a * (1-tx) + b * tx) * (1-tz) + (e * (1-tx) + c * tx) * tz;
}

inline uint32_t terrainPreviewColor(char kind) {
    using render::rgba;
    switch (kind) {
        case '~': return rgba(36,87,137);  // water
        case '=': return rgba(65,116,150); // shallow
        case '#': return rgba(105,113,127); // blocked/cliff
        case 'R': return rgba(112,102,94); // stone
        case ',': return rgba(154,128,79); // no build
        case '/': return rgba(118,130,107); // ramp
        case '+': return rgba(113,103,89); // road
        default:  return rgba(86,122,98); // ground or custom terrain
    }
}

// Tessellate the actual document grid with per-cell terrain colors and height.
// Limit each dimension to ~256 samples to prevent pathological preview uploads.
inline TerrainPreviewStats buildTerrainPreview(const editor::MapData& d,
                                               render::MeshBuilder& out,
                                               float levelHeight = 1.5f) {
    TerrainPreviewStats stats;
    if(d.extra.getStr("terrainPresentation","legacy")=="entities")return stats;
    if (d.planet() || d.w <= 0 || d.h <= 0 || d.w > 200000 || d.h > 200000) return stats;
    const int longest = std::max(d.w, d.h);
    const int step = std::max(1, (longest + 255) / 256);
    stats.step = step;
    for (int z = 0; z < d.h; z += step) {
        const int endZ = std::min(d.h, z + step);
        for (int x = 0; x < d.w; x += step) {
            const int endX = std::min(d.w, x + step);
            using render::float3;
            const float3 a{float(x), terrainPreviewCorner(d,x,z,levelHeight), float(z)};
            const float3 b{float(endX), terrainPreviewCorner(d,endX,z,levelHeight), float(z)};
            const float3 c{float(endX), terrainPreviewCorner(d,endX,endZ,levelHeight), float(endZ)};
            const float3 e{float(x), terrainPreviewCorner(d,x,endZ,levelHeight), float(endZ)};
            const float3 n = render::normalize(render::cross(c-a,b-a));
            const size_t colorOffset = size_t(z)*size_t(d.w)+size_t(x);
            const char kind = colorOffset < d.cells.size() ? d.cells[colorOffset] : '.';
            const uint32_t color=terrainPreviewColor(kind);
            const uint32_t v0=out.addVertex(a,n,color);
            const uint32_t v1=out.addVertex(b,n,color);
            const uint32_t v2=out.addVertex(c,n,color);
            const uint32_t v3=out.addVertex(e,n,color);
            out.addTriangle(v0,v2,v1);
            out.addTriangle(v0,v3,v2);
            ++stats.quads;
        }
    }
    return stats;
}
} // namespace aiverse::studio
