// Genre-neutral cross-backend GI/light capability and CPU work admission.
// Do not confuse shader availability with actual GPU feature support: Metal
// has the current compute BVH kernels, while Vulkan/GLES3 use existing
// LightSystem raster direct and optional CPU-updated, shader-sampled GI L1.
#pragma once
#include "../gfx/Gfx.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace aiverse::render {

struct LumaLightingBackendCaps {
    gfx::Backend backend=gfx::Backend::GLES3;
    bool metalComputeCandidate=false;  // not a promise createCompute succeeds
    bool portableCpuProbeAtlas=false;  // can sample an RGBA32F L1 atlas
    size_t defaultCpuProbeBudget=0;   // jobs per frame, NOT time/FPS
};

struct LumaWeaveBackendPolicy {
    static constexpr size_t cpuProbeLimit(gfx::Backend backend) {
        switch(backend) {
        case gfx::Backend::GLES3: return 2; // mobile/WebGL2; throttle reference rays
        case gfx::Backend::Vulkan: return 16;
        case gfx::Backend::GL33: return 8;
        case gfx::Backend::Metal: return 8; // optional CPU fallback only
        }
        return 0;
    }
    static LumaLightingBackendCaps inspect(const gfx::Device& device) {
        const auto backend=device.info().backend;
        return {backend,
                backend==gfx::Backend::Metal && device.supportsCompute() &&
                    device.supports(gfx::PixelFormat::RGBA16F),
                device.supports(gfx::PixelFormat::RGBA32F),
                cpuProbeLimit(backend)};
    }
    // A CPU GI atlas is presentationally valid only after the host published
    // an immutable trace BVH. With an expected scene generation supplied,
    // reject an older atlas rather than show bounce through changed walls.
    // expectedRevision=0 keeps existing hosts source-compatible, but callers
    // managing dynamic geometry should always pass their current generation.
    static constexpr bool portableGiGeometryCurrent(bool hasGeometry,
                                                    uint64_t acceptedRevision,
                                                    uint64_t expectedRevision=0) {
        return hasGeometry && acceptedRevision!=0 &&
               (!expectedRevision || acceptedRevision==expectedRevision);
    }

    // Zero means no jobs. Upper bound of 256 prevents accidental unlimited CPU
    // tracing when the app passes a huge budget; use a targeted host profiler
    // before opting into a higher per-backend setting in the future.
    static size_t cpuBudget(gfx::Backend backend,size_t requested) {
        return std::min(requested,cpuProbeLimit(backend));
    }
};

} // namespace aiverse::render
