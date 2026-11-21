#pragma once
// PRISMA Stages 45-46. CPU-only hysteresis for presentation render scale.
// Samples may be asynchronously completed whole-frame GPU timings paired with
// true GPU fences, or completed scoped GPU query results paired with per-view
// sample sequence numbers. Real GPU-completed fences also permit bounded
// known-memory pressure to lower the quality even without frame timings.
// Never use CPU frame time, FPS or an invented completion token.
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::render {
struct PrismaAdaptiveResolution {
    float scale = 1.0f;
    uint64_t lastCompletedFence = 0;
    bool lastSampleSourceScoped = false; // query sequence and GPU fence are different domains
    unsigned slowSamples = 0, fastSamples = 0, cooldown = 0;
    bool initialized = false;

    void reset(float authoredScale) {
        scale = quantize(authoredScale);
        slowSamples = fastSamples = cooldown = 0;
        lastCompletedFence = 0;
        lastSampleSourceScoped = false;
        initialized = true;
    }
    static float quantize(float s) {
        return std::round(std::clamp(s,0.5f,1.0f)*16.0f)/16.0f;
    }
    // 'ceiling' is an author-chosen renderScale. The controller never raises
    // fidelity beyond it and never drops below the configured floor.
    // The token must represent a new actual GPU completion: a confirmed
    // full-frame GPU fence, or a new completed GL33 scoped query sequence.
    // Duplicate/queued CPU frame updates never advance the controller.
    bool observe(double gpuMs, uint64_t completedFence, float targetGpuMs,
                 float floor, float ceiling, bool memoryPressure = false,
                 bool scopedSample = false,
                 uint8_t viewPriority = 128) {
        if (!initialized) reset(ceiling);
        // A scoped query sequence may start at 1 while the whole-frame
        // GPU fence is already large. Never compare tokens from these
        // distinct domains or the policy can stall permanently.
        if (lastSampleSourceScoped != scopedSample) {
            lastCompletedFence = 0;
            slowSamples = fastSamples = 0;
            lastSampleSourceScoped = scopedSample;
        }
        const float hi=quantize(ceiling);
        const float lo=std::min(hi,quantize(floor));
        scale=std::clamp(scale,lo,hi);
        // A completed scoped GPU query has its own monotonic sample
        // sequence; whole-frame fallback uses a genuine completed GPU fence.
        // Memory pressure alone may step DOWN only with a fresh verified GPU
        // completion; it never promotes quality without valid GPU timing.
        if (!completedFence || completedFence<=lastCompletedFence ||
            !std::isfinite(targetGpuMs) || targetGpuMs<4.0f ||
            targetGpuMs>50.0f) return false;
        const bool validMs=std::isfinite(gpuMs) && gpuMs>0.0 && gpuMs<=250.0;
        if (!validMs && !memoryPressure) return false;
        lastCompletedFence=completedFence;
        if (cooldown) { --cooldown; slowSamples=fastSamples=0; return false; }
        // 15% slowdown is required for a downward step, but recovery
        // requires 20% GPU headroom for considerably longer.
        if (memoryPressure || (validMs && gpuMs>double(targetGpuMs)*1.15)) {
            ++slowSamples; fastSamples=0;
        } else if (validMs && gpuMs<double(targetGpuMs)*0.80) {
            ++fastSamples;slowSamples=0;
        } else { slowSamples=fastSamples=0; }
        // Under global memory pressure, low-priority views yield first;
        // persistent GPU slowness still reduces any view after 12 samples.
        const bool gpuSlow=validMs && gpuMs>double(targetGpuMs)*1.15;
        const unsigned pressureThreshold =
            viewPriority<85 ? 6u : viewPriority<170 ? 12u : 30u;
        const unsigned slowThreshold=memoryPressure && !gpuSlow ?
            pressureThreshold : 12u;
        float next=scale;
        if (slowSamples>=slowThreshold) next=std::max(lo,scale-1.0f/16.0f);
        if (fastSamples>=36) next=std::min(hi,scale+1.0f/16.0f);
        if (next==scale) return false;
        scale=next;
        cooldown=48;
        slowSamples=fastSamples=0;
        return true;
    }
};
} // namespace aiverse::render
