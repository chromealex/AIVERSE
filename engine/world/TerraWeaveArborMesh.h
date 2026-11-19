// TerraWeave Arbor CPU render-mesh builder (presentation only).
// Converts deterministic local-space tree skeletons to bark tubes + leaf cards.
// GPU resources, shaders, LOD impostor baking and RHI uploads belong to adapters.
#pragma once
#include "engine/world/TerraWeaveArbor.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

struct ArborVertex {
    float x = 0, y = 0, z = 0;
    float nx = 0, ny = 1, nz = 0;
    float u = 0, v = 0;
    uint8_t material = 0; // 0 bark, 1 leaves
};

struct ArborMesh {
    std::vector<ArborVertex> vertices;
    std::vector<uint32_t> indices;
};

struct ArborMeshSettings {
    uint32_t radialSegments = 6; // 3..16
    bool doubleSidedLeaves = true;
    uint32_t leafPlanes = 2; // 1: one card, 2: crossed cards
};

namespace arbor_detail {
struct V3 { float x = 0, y = 0, z = 0; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float f) { return {a.x * f, a.y * f, a.z * f}; }
inline V3 cross(V3 a, V3 b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x};
}
inline V3 normalize(V3 v) {
    const float len2 = v.x * v.x + v.y * v.y + v.z * v.z;
    if (len2 < 1.0e-12f) return {0, 1, 0};
    return v * (1.0f / std::sqrt(len2));
}
inline void addTriangle(ArborMesh& mesh, uint32_t a, uint32_t b, uint32_t c) {
    mesh.indices.push_back(a);
    mesh.indices.push_back(b);
    mesh.indices.push_back(c);
}
inline void addLeafQuad(ArborMesh& mesh, V3 center, V3 side, float radius,
                        bool doubleSided) {
    const V3 up{0, radius, 0};
    const V3 horizontal = side * radius;
    const V3 n = normalize(cross(horizontal, up));
    const V3 coords[4] = {
        center - horizontal - up, center + horizontal - up,
        center + horizontal + up, center - horizontal + up
    };
    const float uv[4][2] = {{0, 0}, {1, 0}, {1, 1}, {0, 1}};
    const uint32_t base = static_cast<uint32_t>(mesh.vertices.size());
    for (int i = 0; i < 4; ++i)
        mesh.vertices.push_back({coords[i].x, coords[i].y, coords[i].z,
                                 n.x, n.y, n.z, uv[i][0], uv[i][1], 1});
    addTriangle(mesh, base, base + 1, base + 2);
    addTriangle(mesh, base, base + 2, base + 3);
    if (doubleSided) {
        addTriangle(mesh, base + 2, base + 1, base);
        addTriangle(mesh, base + 3, base + 2, base);
    }
}
} // namespace arbor_detail

inline ArborMesh buildArborMesh(const TreeSkeleton& tree,
                               ArborMeshSettings settings = {}) {
    if (settings.radialSegments < 3 || settings.radialSegments > 16 ||
        settings.leafPlanes < 1 || settings.leafPlanes > 2)
        throw std::invalid_argument("TerraWeave Arbor radial segments");
    ArborMesh out;
    constexpr float kTau = 6.2831853071795864769f;
    using namespace arbor_detail;
    for (const TreeBranch& b : tree.branches) {
        V3 start{float(b.x0), float(b.y0), float(b.z0)};
        V3 end{float(b.x1), float(b.y1), float(b.z1)};
        const V3 delta = end - start;
        const float length = std::sqrt(delta.x * delta.x +
                                       delta.y * delta.y + delta.z * delta.z);
        if (length <= 0.001f) continue;
        const V3 axis = delta * (1.0f / length);
        const V3 helper = std::fabs(axis.y) < 0.9f ? V3{0, 1, 0} : V3{1, 0, 0};
        const V3 tangent = normalize(cross(axis, helper));
        const V3 bitangent = normalize(cross(axis, tangent));
        const uint32_t base = static_cast<uint32_t>(out.vertices.size());
        const uint32_t side = settings.radialSegments + 1u;
        for (uint32_t ring = 0; ring != 2; ++ring) {
            const V3 center = ring == 0 ? start : end;
            const float radius = float(ring == 0 ? b.startRadiusCm : b.endRadiusCm);
            for (uint32_t seg = 0; seg <= settings.radialSegments; ++seg) {
                const float angle = kTau * float(seg) / float(settings.radialSegments);
                const V3 normal = tangent * std::cos(angle) + bitangent * std::sin(angle);
                const V3 at = center + normal * radius;
                out.vertices.push_back({at.x, at.y, at.z,
                                        normal.x, normal.y, normal.z,
                                        float(seg) / float(settings.radialSegments),
                                        ring == 0 ? 0.0f : length / 100.0f, 0});
            }
        }
        for (uint32_t seg = 0; seg < settings.radialSegments; ++seg) {
            const uint32_t a = base + seg, b0 = base + seg + 1u;
            const uint32_t c = base + side + seg, d = base + side + seg + 1u;
            addTriangle(out, a, c, b0);
            addTriangle(out, b0, c, d);
        }
    }
    for (const LeafCluster& leaf : tree.leaves) {
        const V3 at{float(leaf.xCm), float(leaf.yCm), float(leaf.zCm)};
        const float r = float(leaf.radiusCm);
        if (r <= 0) continue;
        // Two crossed alpha-cutout cards per cluster. A later GPU material
        // controls alpha mask, wind and LOD blend; no textures are baked here.
        addLeafQuad(out, at, {1, 0, 0}, r, settings.doubleSidedLeaves);
        if (settings.leafPlanes == 2)
            addLeafQuad(out, at, {0, 0, 1}, r, settings.doubleSidedLeaves);
    }
    return out;
}

} // namespace aiverse::terraweave
