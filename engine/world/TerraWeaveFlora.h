// TerraWeave Flora: CPU-side, genre-independent foliage draw-list preparation.
// This is a presentation-only precursor to gfx::Device instance-buffer uploads.
#pragma once
#include "engine/world/TerraWeaveArbor.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct FloraView {
    int64_t cameraXcm = 0, cameraZcm = 0;
    int32_t highRangeCm = 3000;       // High mesh
    int32_t mediumRangeCm = 9000;     // Medium mesh
    int32_t lowRangeCm = 18000;       // Low mesh
    int32_t cullRangeCm = 50000;      // farther => omitted
    uint32_t farDensityPermille = 650; // stable thinning after lowRange (0..1000)
    // Optional spread of the geometry -> billboard transition across a
    // stable per-tree radius. 0 = legacy hard ring; units are centimetres.
    // This is spatial stochastic switching, NOT a pixel-level crossfade.
    int32_t impostorBlendWidthCm = 0;
};

struct FloraBatch {
    uint32_t speciesId = 0;
    TreeLOD lod = TreeLOD::High;
    std::vector<FoliageInstance> instances;
};

inline std::vector<FloraBatch> buildFloraBatches(
        const std::vector<FoliageInstance>& placements,
        const FloraView& view = {}) {
    if (view.highRangeCm < 0 || view.mediumRangeCm < view.highRangeCm ||
        view.lowRangeCm < view.mediumRangeCm ||
        view.cullRangeCm < view.lowRangeCm ||
        view.cullRangeCm > 100000000 ||
        view.farDensityPermille > 1000 ||
        view.impostorBlendWidthCm < 0 ||
        int64_t(view.lowRangeCm) - view.impostorBlendWidthCm / 2 <
            view.mediumRangeCm ||
        int64_t(view.lowRangeCm) - view.impostorBlendWidthCm / 2 +
            view.impostorBlendWidthCm > view.cullRangeCm)
        throw std::invalid_argument("TerraWeave Flora view");

    // std::map and explicit sort provide stable draw-list composition regardless
    // of scatter chunk completion order. No ECS structural operations here.
    std::map<std::pair<uint32_t, uint8_t>, std::vector<FoliageInstance>> groups;
    const int64_t highSq = int64_t(view.highRangeCm) * view.highRangeCm;
    const int64_t medSq = int64_t(view.mediumRangeCm) * view.mediumRangeCm;
    // One deterministic threshold per individual tree avoids a sudden
    // synchronized circular switch across an entire forest.
    const int64_t bandStart = int64_t(view.lowRangeCm) -
                              view.impostorBlendWidthCm / 2;
    const int64_t cullSq = int64_t(view.cullRangeCm) * view.cullRangeCm;
    for (const FoliageInstance& instance : placements) {
        // Arbitrary caller data can be malicious: keep distance arithmetic
        // bounded before squaring, without undefined signed overflow.
        if (instance.xCm > 1000000000LL || instance.xCm < -1000000000LL ||
            instance.zCm > 1000000000LL || instance.zCm < -1000000000LL ||
            view.cameraXcm > 1000000000LL || view.cameraXcm < -1000000000LL ||
            view.cameraZcm > 1000000000LL || view.cameraZcm < -1000000000LL)
            throw std::out_of_range("TerraWeave Flora world coordinates");
        const int64_t dx = instance.xCm - view.cameraXcm;
        const int64_t dz = instance.zCm - view.cameraZcm;
        if (dx > view.cullRangeCm || dx < -view.cullRangeCm ||
            dz > view.cullRangeCm || dz < -view.cullRangeCm) continue;
        const int64_t d2 = dx * dx + dz * dz;
        if (d2 > cullSq) continue;
        int64_t lowThresholdCm = view.lowRangeCm;
        if (view.impostorBlendWidthCm) {
            const uint64_t offset = detail::mix64(
                instance.key ^ (uint64_t(instance.speciesId) << 32) ^
                0xd6ac447e1007bb31ULL);
            lowThresholdCm = bandStart + static_cast<int64_t>(
                offset % (uint64_t(view.impostorBlendWidthCm) + 1));
        }
        const int64_t lowSq = lowThresholdCm * lowThresholdCm;
        TreeLOD lod = TreeLOD::Impostor;
        if (d2 <= highSq) lod = TreeLOD::High;
        else if (d2 <= medSq) lod = TreeLOD::Medium;
        else if (d2 <= lowSq) lod = TreeLOD::Low;
        else if ((detail::mix64(instance.key ^ 0x7bd64295d234c1e9ULL) % 1000) >=
                 view.farDensityPermille) continue;
        groups[{instance.speciesId, static_cast<uint8_t>(lod)}].push_back(instance);
    }

    std::vector<FloraBatch> output;
    output.reserve(groups.size());
    for (auto& [key, instances] : groups) {
        std::sort(instances.begin(), instances.end(),
                  [](const FoliageInstance& a, const FoliageInstance& b) {
                      if (a.key != b.key) return a.key < b.key;
                      if (a.xCm != b.xCm) return a.xCm < b.xCm;
                      return a.zCm < b.zCm;
                  });
        instances.erase(std::unique(instances.begin(), instances.end(),
                     [](const FoliageInstance& a, const FoliageInstance& b) {
                         return a.key == b.key && a.speciesId == b.speciesId;
                     }), instances.end());
        output.push_back({key.first, static_cast<TreeLOD>(key.second),
                          std::move(instances)});
    }
    return output;
}

} // namespace aiverse::terraweave
