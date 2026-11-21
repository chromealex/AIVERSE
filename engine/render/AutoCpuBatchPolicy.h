#pragma once
// Opt-in, renderer-local CPU-only crossover policy. Never estimates GPU time.
// Shadow and opaque lanes have independent sample histories; each lane is
// bucketed by post-culling item count to avoid mixing unrelated workloads.
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace aiverse::render {
struct AutoCpuBatchPolicy {
    struct Lane {
        double directMs = 0.0, mdiMs = 0.0;
        uint32_t directSamples = 0, mdiSamples = 0;
        uint32_t opportunities = 0;
        uint32_t mdiNoSubmission = 0;
    };
    std::array<std::array<Lane, 3>, 2> lanes{};
    static size_t bucket(size_t items) {
        return items < 128 ? 0 : items < 512 ? 1 : 2;
    }
    // Periodic controlled probes avoid making a stale choice permanent.
    // A full phase must use exactly one CPU policy to make its sample useful.
    bool useMdi(size_t phase, size_t items) {
        if (phase >= 2 || items < 64) return false;
        auto& lane = lanes[phase][bucket(items)];
        const uint32_t n = ++lane.opportunities;
        // A candidate count alone is not a valid indirect command count.
        // If the renderer repeatedly found no valid MDI submission, stay on
        // direct/instancing and periodically probe again as content changes.
        if (lane.mdiNoSubmission >= 3)
            return n % 32u == 0u;
        // Do not freeze the choice after only a handful of MDI probes.
        // The first MDI frame may allocate/pipeline-warm Metal ICB resources,
        // so gather a useful consecutive window before trusting its EWMA.
        if (lane.directSamples < 12) return (n & 1u) == 0u;
        if (lane.mdiSamples < 32) return true;
        // Require a measurable advantage before switching to MDI. While
        // Direct is selected, sample an 8-pass contiguous MDI probe every
        // 64 opportunities; isolated single probes can amplify cold-start
        // effects and keep the estimator stuck on Direct indefinitely.
        const bool mdiFaster = lane.mdiMs < lane.directMs * 0.92;
        if (!mdiFaster && n % 64u < 8u) return true;
        if (mdiFaster && n % 64u == 0u) return false;
        return mdiFaster;
    }
    void recordNoSubmission(size_t phase, size_t items) {
        if (phase >= 2 || items < 64) return;
        auto& lane = lanes[phase][bucket(items)];
        if (lane.mdiNoSubmission < 1000000u) ++lane.mdiNoSubmission;
    }
    void observe(size_t phase, size_t items, bool mdi, double ms) {
        if (phase >= 2 || items < 64 || !std::isfinite(ms) || ms <= 0.0)
            return;
        auto& lane = lanes[phase][bucket(items)];
        if (mdi) lane.mdiNoSubmission = 0;
        auto& avg = mdi ? lane.mdiMs : lane.directMs;
        auto& count = mdi ? lane.mdiSamples : lane.directSamples;
        // Ignore first two samples of each lane to limit pipeline warmup.
        if (count < 2) { ++count; return; }
        if (count == 2) avg = ms;
        else avg += 0.25 * (ms - avg);
        if (count < 1000000u) ++count;
    }
};

// Per-pass CPU MDI admission under AIVERSE_BATCH_AUTO. A learning pass asks
// the policy (exactly one useMdi() call per pass). A non-learning pass that
// is large enough keeps ordinary CPU MDI (as without AUTO); only workloads
// below the conservative small-item threshold stay on the direct path.
inline bool autoCpuMdiPermit(AutoCpuBatchPolicy& policy, bool autoMode,
                             bool learning, size_t phase, size_t items,
                             size_t minimumItems) {
    if (!autoMode) return true;
    if (learning) return policy.useMdi(phase, items);
    return items >= minimumItems;
}
} // namespace aiverse::render
