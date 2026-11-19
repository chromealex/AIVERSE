// TerraWeave Arbor: seeded, genre-independent procedural tree skeletons.
// The integer skeleton is authoritative for replayable world generation; GPU
// tessellation, leaf materials and impostor atlas baking are separate stages.
#pragma once

#include "engine/world/TerraWeave.h"
#include <algorithm>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::terraweave {

enum class TreeLOD : uint8_t { High, Medium, Low, Impostor };
enum class CrownShape : uint8_t { Broadleaf, Conifer, Columnar };

struct TreeProfile {
    uint32_t speciesId = 1;
    int32_t minHeightCm = 800;
    int32_t maxHeightCm = 1500;
    int32_t trunkRadiusCm = 24;
    uint32_t crownStartPermille = 280;
    uint32_t primaryBranches = 22;
    uint32_t branchLengthPermille = 380;
    uint32_t branchRisePermille = 220;
    uint32_t leafRadiusCm = 95;
    CrownShape shape = CrownShape::Broadleaf;
};

struct TreeBranch {
    int32_t x0 = 0, y0 = 0, z0 = 0;
    int32_t x1 = 0, y1 = 0, z1 = 0;
    int32_t startRadiusCm = 0, endRadiusCm = 0;
    int32_t parent = -1; // index of parent branch; trunk has parent=-1
    constexpr bool operator==(const TreeBranch&) const = default;
};

struct LeafCluster {
    int32_t xCm = 0, yCm = 0, zCm = 0;
    uint32_t radiusCm = 0, variation = 0;
    int32_t parentBranch = -1; // structural owner, independent per-branch LOD
    constexpr bool operator==(const LeafCluster&) const = default;
};

struct TreeSkeleton {
    uint32_t speciesId = 0;
    uint64_t seed = 0;
    int32_t heightCm = 0;
    int32_t crownRadiusCm = 0;
    std::vector<TreeBranch> branches;
    std::vector<LeafCluster> leaves;
};

inline TreeSkeleton generateTree(const TreeProfile& p, uint64_t instanceSeed,
                                 TreeLOD lod = TreeLOD::High) {
    if (p.minHeightCm < 100 || p.maxHeightCm < p.minHeightCm ||
        p.maxHeightCm > 100000 || p.trunkRadiusCm <= 0 ||
        p.trunkRadiusCm > p.minHeightCm / 2 ||
        p.primaryBranches > 256 || p.crownStartPermille >= 1000 ||
        p.branchLengthPermille > 2000 || p.branchRisePermille > 2000 ||
        p.leafRadiusCm > 100000 ||
        (p.shape != CrownShape::Broadleaf &&
         p.shape != CrownShape::Conifer && p.shape != CrownShape::Columnar))
        throw std::invalid_argument("TerraWeave Arbor profile");

    // Variation is derived only from species id and stable world instance key.
    const uint64_t seed = detail::mix64(instanceSeed ^ (uint64_t(p.speciesId) << 32));
    const uint64_t range = uint64_t(p.maxHeightCm) - p.minHeightCm + 1;
    const int32_t height = static_cast<int32_t>(p.minHeightCm + seed % range);
    TreeSkeleton out;
    out.speciesId = p.speciesId;
    out.seed = instanceSeed;
    out.heightCm = height;
    // Conservative visual bounds also available for the impostor tier.
    out.crownRadiusCm = static_cast<int32_t>(
        int64_t(height) * p.branchLengthPermille / 1000 + p.leafRadiusCm);
    if (lod == TreeLOD::Impostor) return out;

    // Root trunk first, allowing parent indices to be stable per generated LOD.
    out.branches.push_back({0, 0, 0, 0, height, 0,
                            p.trunkRadiusCm, std::max<int32_t>(1, p.trunkRadiusCm / 12), -1});

    const uint32_t count = (lod == TreeLOD::High) ? p.primaryBranches :
        (lod == TreeLOD::Medium) ? (p.primaryBranches + 1u) / 2u :
        (p.primaryBranches + 3u) / 4u;
    const int64_t crownBase = int64_t(height) * p.crownStartPermille / 1000;
    const int64_t available = height - crownBase;
    for (uint32_t i = 0; i < count; ++i) {
        // A separate hash per branch prevents a change to LOD from shifting
        // any tree generation elsewhere in the world.
        const uint64_t h = detail::mix64(seed + uint64_t(i) * 0x9e3779b97f4a7c15ULL);
        int64_t dx = static_cast<int64_t>(h & 0xffffULL) - 32768;
        int64_t dz = static_cast<int64_t>((h >> 16) & 0xffffULL) - 32768;
        const int64_t ax = dx < 0 ? -dx : dx, az = dz < 0 ? -dz : dz;
        const int64_t denom = std::max<int64_t>(1, std::max(ax, az));
        // Ordered trunk heights; azimuth jittered independently. All integer.
        const int64_t baseY = crownBase + available * (2 * i + 1) / (2 * count);
        const int64_t remain = height - baseY;
        int64_t length = remain * p.branchLengthPermille / 1000;
        if (p.shape == CrownShape::Conifer) length = length * 80 / 100;
        if (p.shape == CrownShape::Columnar) length = length * 35 / 100;
        length = length * (850 + static_cast<int64_t>((h >> 32) % 301)) / 1000;
        const int32_t endX = static_cast<int32_t>(dx * length / denom);
        const int32_t endZ = static_cast<int32_t>(dz * length / denom);
        const int32_t endY = static_cast<int32_t>(
            std::min<int64_t>(height + p.leafRadiusCm,
                baseY + length * p.branchRisePermille / 1000));
        const int32_t startRadius = std::max<int32_t>(
            1, static_cast<int32_t>(int64_t(p.trunkRadiusCm) * remain / height));
        const int32_t endRadius = std::max<int32_t>(1, startRadius / 4);
        out.branches.push_back({0, static_cast<int32_t>(baseY), 0,
                                endX, endY, endZ, startRadius, endRadius, 0});
        // 1 leaf cluster on Medium/Low, 2 on High; foliage meshes may replace
        // them with camera-facing leaf cards or more detailed leaf geometry.
        const uint32_t leafCount = lod == TreeLOD::High ? 2u : 1u;
        for (uint32_t j = 0; j < leafCount; ++j) {
            const int32_t fraction = leafCount == 1 ? 1000 : (j == 0 ? 720 : 1000);
            const int32_t y = static_cast<int32_t>(
                baseY + (int64_t(endY) - baseY) * fraction / 1000);
            const int32_t x = static_cast<int32_t>(int64_t(endX) * fraction / 1000);
            const int32_t z = static_cast<int32_t>(int64_t(endZ) * fraction / 1000);
            const uint64_t leafSeed = detail::mix64(h + j * 0xd1b54a32d192ed03ULL);
            const uint32_t radius = p.leafRadiusCm *
                (80u + static_cast<uint32_t>(leafSeed % 41u)) / 100u;
            out.leaves.push_back({x, y, z, radius,
                static_cast<uint32_t>(leafSeed >> 32), static_cast<int32_t>(i + 1)});
        }
    }
    return out;
}

// Converts one stable foliage prototype placement into a deterministic tree.
// Tree geometry is local-space; use FoliageInstance world position/yaw/scale
// to render it. Never use GPU vertex data for authoritative collision.
inline TreeSkeleton generateTree(const TreeProfile& profile,
                                 const FoliageInstance& instance, TreeLOD lod) {
    if (instance.speciesId != profile.speciesId)
        throw std::invalid_argument("TerraWeave Arbor species mismatch");
    return generateTree(profile, instance.key, lod);
}

} // namespace aiverse::terraweave
