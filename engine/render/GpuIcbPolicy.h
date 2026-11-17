#pragma once
// CPU-side selection policy for optional GPU-encoded Metal ICB batches.
// A conservative, testable heuristic; this is not a learned GPU cost model.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace aiverse::render {

inline uint32_t gpuIcbMinimumCommands(uint32_t eligible, uint32_t requested,
                                      bool adaptive) {
    const uint32_t floor = adaptive ? (eligible < 256u ? 16u : 8u) : 2u;
    return std::max(std::max(requested, 2u), floor);
}

inline bool gpuIcbShouldFallbackToInstancing(bool adaptive,
                                            uint32_t suitableGroups) {
    return adaptive && suitableGroups == 0;
}

// An optional per-group selection policy. Its inputs describe the geometry
// actually present in this compatible GPU state group, not the whole scene.
// The policy refuses to pay an indirect-command setup cost when the same
// primitive can be handled more cheaply by a conventional instanced draw.
// "Runs" counts consecutive instances of the same primitive after sorting.
// No timing estimates or promises of a GPU speedup are inferred from draw count.
enum class DrawGroupMode : uint8_t { Direct, Instancing, CpuMultiDraw, GpuIcb };

inline DrawGroupMode chooseDrawGroupMode(uint32_t instances, uint32_t primitiveRuns,
                                         bool instancing, bool cpuMdi, bool gpuIcb,
                                         uint32_t cpuMinCommands, uint32_t gpuMinCommands) {
    if (!instances || !primitiveRuns) return DrawGroupMode::Direct;
    if (gpuIcb && instances >= gpuMinCommands &&
        primitiveRuns >= gpuMinCommands && primitiveRuns > 1)
        return DrawGroupMode::GpuIcb;
    if (cpuMdi && primitiveRuns >= std::max(2u, cpuMinCommands) &&
        primitiveRuns > 1)
        return DrawGroupMode::CpuMultiDraw;
    return instancing && instances > 1
        ? DrawGroupMode::Instancing : DrawGroupMode::Direct;
}
inline bool shouldUseCpuMultiDraw(uint32_t instances, uint32_t primitiveRuns,
                                  uint32_t minCommands) {
    return chooseDrawGroupMode(instances, primitiveRuns, true, true, false,
                               minCommands, 2) == DrawGroupMode::CpuMultiDraw;
}
inline bool shouldUseGpuIcb(uint32_t instances, uint32_t primitiveRuns,
                            uint32_t minCommands) {
    return chooseDrawGroupMode(instances, primitiveRuns, true, false, true,
                               2, minCommands) == DrawGroupMode::GpuIcb;
}

// Select from fixed-camera balanced Scene CPU measurements. Small differences
// are treated as ties in favor of the cheaper/default earlier mode.
// 0=Instancing, 1=CPU MDI, 2=GPU-authored. This does not assume that full
// editor GPU timestamps measure the isolated Scene renderer.
inline int chooseSceneBatchMode(const std::array<double, 3>& cpuAverages,
                                int modeCount, double minimumGain = 0.005) {
    modeCount = std::clamp(modeCount, 1, 3);
    int best = 0;
    for (int mode = 1; mode < modeCount; ++mode)
        if (std::isfinite(cpuAverages[mode]) && cpuAverages[mode] > 0.0 &&
            cpuAverages[mode] < cpuAverages[best] * (1.0 - minimumGain))
            best = mode;
    return best;
}

} // namespace aiverse::render
