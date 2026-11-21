// CPU dynamic GI update governor for GLES3/WebGL2 and Vulkan devices without
// compute-storage support. Keeps the ray tracer bounded by BOTH the host's
// configured maximum probe batch and the observed CPU cost. The caller can
// still supply an explicit budget via GiDynamicRuntime::update().
// This is presentation scheduling, never authoritative world/game state.
#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <stdexcept>

namespace aiverse::render {

class GiCpuTraceBudget {
public:
    // A small default suitable for an opt-in mobile fallback. Quality is not
    // implicitly enabled; users must explicitly bind a dynamic GI overlay.
    static constexpr double kDefaultTargetMs=1.25;
    static constexpr size_t kDefaultMaxProbes=4;

    void configure(double targetMilliseconds=kDefaultTargetMs,
                   size_t maxProbes=kDefaultMaxProbes) {
        if (!std::isfinite(targetMilliseconds) || targetMilliseconds<=0 ||
            targetMilliseconds>1000 || maxProbes==0 || maxProbes>1024)
            throw std::invalid_argument("invalid CPU GI trace budget");
        targetMs_=targetMilliseconds;
        maxProbes_=maxProbes;
        costPerProbeMs_=0; // fresh device/scene; do not inherit stale performance data
    }

    double targetMs() const { return targetMs_; }
    size_t maxProbes() const { return maxProbes_; }
    double estimatedProbeMs() const { return costPerProbeMs_; }

    size_t choose(size_t pending) const {
        if (!pending) return 0;
        // Cold-start conservative default. At least one probe moves forward
        // even if one job costs more than the requested CPU frame budget.
        const double unitMs=costPerProbeMs_>0 ? costPerProbeMs_ : 0.5;
        const size_t byTime=std::max<size_t>(1,
            size_t(std::min<double>(double(maxProbes_),
                                    std::floor(targetMs_/unitMs))));
        return std::min(pending,std::min(maxProbes_,byTime));
    }

    // Wall time includes CPU ray tracing and RHI atlas upload. Never infer
    // Vulkan/Metal GPU time from these CPU timings.
    void observe(size_t claimed,double elapsedMilliseconds) {
        if (!claimed || !std::isfinite(elapsedMilliseconds) ||
            elapsedMilliseconds<=0) return;
        const double observed=std::clamp(elapsedMilliseconds/double(claimed),
                                         0.00001,1000.0);
        costPerProbeMs_=costPerProbeMs_>0
            ? .75*costPerProbeMs_+.25*observed : observed;
    }

private:
    double targetMs_=kDefaultTargetMs;
    size_t maxProbes_=kDefaultMaxProbes;
    double costPerProbeMs_=0;
};

} // namespace aiverse::render
