#pragma once
#include "engine/mechanics/Namespace.h"
// vehicle-fx: tyre marks. Each wheel leaves short segments while it slips (drift, wheel-spin, hard cornering);
// segments live a fixed time and fade at the end; the total count is bounded. The view samples the wheel contact
// points every frame and draws the segments (two quads: a soft wide one and a dark narrow one look right from
// above). Presentation only: no renderer dependency, the caller converts to its own vector type.
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <map>
#include <set>

namespace aiverse::lib::mechanics {

struct TrailPoint { float x = 0, y = 0, z = 0; };

struct TyreTrailsConfig {
    std::size_t limit = 1536;          // segments kept at most (oldest dropped)
    double lifetime = 35, fadeTail = 10;   // seconds; the last fadeTail seconds fade out
    float minStep = 0.045f;            // shorter moves are merged into the next segment
    float maxJump = 2.0f, maxRise = 0.2f;   // a teleport / a step up breaks the trail
    float slipSpeed = 4, slipLateral = 5, slipSteer = 0.08f;   // slipping(): speed, yaw rate x speed, steer
};

struct TyreTrails {
    struct Segment { TrailPoint a, b; double born = 0; };
    struct Wheel { TrailPoint p; bool active = false; };
    std::deque<Segment> segments;
    std::map<std::uint64_t, Wheel> wheels;   // ordered: deterministic iteration in tests / captures

    // A wheel slips when the car is on the ground and fast enough, and drifts, spins a wheel (slipMask) or
    // turns hard (yaw rate in turns per second x speed) with the wheel turned.
    static bool slipping(float speed, float yawRate, float steer, bool grounded, bool drift, std::uint32_t slipMask,
                         const TyreTrailsConfig& c = {}) {
        return grounded && speed > c.slipSpeed &&
               (drift || slipMask || (std::abs(yawRate) * 6.2831853f * speed > c.slipLateral && std::abs(steer) > c.slipSteer));
    }
    void sample(std::uint64_t wheel, TrailPoint p, bool active, double now, const TyreTrailsConfig& c = {}) {
        auto& old = wheels[wheel];
        const float d = std::hypot(p.x - old.p.x, p.z - old.p.z);
        if (!active || !old.active || d > c.maxJump || std::abs(p.y - old.p.y) > c.maxRise) { old = {p, active}; return; }
        if (d < c.minStep) return;
        segments.push_back({old.p, p, now}); old = {p, true};
        while (segments.size() > c.limit) segments.pop_front();
    }
    void expire(double now, const TyreTrailsConfig& c = {}) {
        while (!segments.empty() && now - segments.front().born > c.lifetime) segments.pop_front();
    }
    // Forget wheels not sampled this frame (the car left the view or was removed).
    void keepOnly(const std::set<std::uint64_t>& present) {
        for (auto i = wheels.begin(); i != wheels.end();) i = present.count(i->first) ? std::next(i) : wheels.erase(i);
    }
    float fade(const Segment& s, double now, const TyreTrailsConfig& c = {}) const {
        return float(std::clamp((c.lifetime - (now - s.born)) / std::max(c.fadeTail, 1e-9), 0.0, 1.0));
    }
    void clear() { segments.clear(); wheels.clear(); }
};

// Contact point of wheel 0..3 (0 front-left, 1 front-right, 2 rear-left, 3 rear-right) of a body at `pos` facing
// `forward` (unit, xz): halfTrack across, halfBase along, `lift` above the body origin.
inline TrailPoint wheelContact(TrailPoint pos, float forwardX, float forwardZ, float halfTrack, float halfBase,
                               int wheel, float lift = 0.036f) {
    const float side = wheel % 2 ? 1.f : -1.f, along = wheel < 2 ? 1.f : -1.f;
    const float rightX = forwardZ, rightZ = -forwardX;
    return {pos.x + rightX * side * halfTrack + forwardX * along * halfBase, pos.y + lift,
            pos.z + rightZ * side * halfTrack + forwardZ * along * halfBase};
}

} // namespace aiverse::lib::mechanics
