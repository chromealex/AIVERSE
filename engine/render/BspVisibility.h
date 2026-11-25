// Quake III potentially visible sets. Coordinates of the planes are in model space.
#pragma once
#include "Math3D.h"
#include <cstdint>
#include <vector>

namespace aiverse::render {
struct BspVisibility {
    struct Plane { float3 normal; float distance; };
    struct Branch { uint32_t plane; int32_t child[2]; };
    std::vector<Plane> planes;
    std::vector<Branch> branches;
    std::vector<int32_t> leafClusters;
    std::vector<std::vector<uint32_t>> primitiveClusters;
    std::vector<uint8_t> rows;
    uint32_t rowBytes = 0;

    int cameraCluster(float3 eye) const {
        int32_t node = 0;
        // Excessively deep or cyclic trees conservatively disable visibility culling.
        for (unsigned depth = 0; depth < 256 && node >= 0; ++depth) {
            if (size_t(node) >= branches.size()) return -1;
            const auto& branch = branches[size_t(node)];
            const auto& plane = planes[branch.plane];
            const float side = dot(plane.normal, eye) - plane.distance;
            if (!std::isfinite(side)) return -1;
            node = branch.child[side < 0];
        }
        return node < 0 ? leafClusters[size_t(~node)] : -1;
    }

    bool visible(int camera, size_t primitive) const {
        if (camera < 0 || !rowBytes || size_t(camera) >= rows.size()/rowBytes ||
            primitive >= primitiveClusters.size() || primitiveClusters[primitive].empty())
            return true;
        const auto* row = rows.data() + size_t(camera) * rowBytes;
        for (uint32_t cluster : primitiveClusters[primitive])
            if (cluster == uint32_t(camera) || (row[cluster/8] & (1u << (cluster%8)))) return true;
        return false;
    }
};
} // namespace aiverse::render
