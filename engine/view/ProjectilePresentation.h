#pragma once
#include "engine/render/Math3D.h"
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace aiverse::view {
// Host adapters supply an explicit segment transition. Simulation owns collision,
// segment identity and origin; this object owns only visual convergence.
enum class FlightOrigin : uint8_t { Emission, Split, Ricochet, Continuation };
struct FlightSegment {
    uint64_t id = 0;
    FlightOrigin origin = FlightOrigin::Emission;
    render::float3 start{};
};
struct FlightSample {
    render::float3 position{};
    render::float3 direction{};
    bool terminal = false;
};
class ProjectilePresentation {
public:
    void begin(const FlightSegment& segment, render::float3 muzzle) {
        segment_ = segment;
        offset_ = segment.origin == FlightOrigin::Emission ? muzzle - segment.start : render::float3{};
        elapsed_ = 0;
        initialized_ = true;
        sample_ = {segment.start + offset_, {}, false};
    }
    // Called once by the presentation owner. Every visual consumer reads sample().
    // Elapsed is absolute segment time, so consumer count and frame partitioning
    // cannot apply launch convergence a second time.
    void evaluate(render::float3 simulated, render::float3 direction, float elapsed) {
        if (!initialized_ || sample_.terminal) return;
        elapsed_ = std::max(elapsed_, std::max(0.f, elapsed));
        sample_ = {simulated + offset_ * std::exp(-7.f * elapsed_), direction, false};
    }
    void evaluateStraight(render::float3 simulated, render::float3 impact) {
        if (!initialized_ || sample_.terminal) return;
        const auto delta = impact - segment_.start;
        const render::float3 flat{delta.x, 0, delta.z};
        const float squared = render::dot(flat, flat);
        const float t = squared > 1e-8f
            ? std::clamp(render::dot(simulated - segment_.start, flat) / squared, 0.f, 1.f) : 1.f;
        const auto launch = segment_.start + offset_;
        sample_ = {launch + (impact - launch) * t, impact - launch, false};
    }
    // Publish contact before retiring any consumer, including a trail whose
    // authoritative entity disappeared between rendered frames.
    void finish(render::float3 contact) {
        if (!initialized_) return;
        sample_.position = contact;
        sample_.terminal = true;
    }
    const FlightSample& sample() const { return sample_; }
    const FlightSegment& segment() const { return segment_; }
private:
    FlightSegment segment_{};
    FlightSample sample_{};
    render::float3 offset_{};
    float elapsed_ = 0;
    bool initialized_ = false;
};
} // namespace aiverse::view
