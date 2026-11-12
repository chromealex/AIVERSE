// Presentation smoothing for rollback corrections (float OK: never feeds the simulation).
//
// After a rollback the re-simulated position of a remote hero can differ from what was drawn a
// frame ago. Drawing the jump looks like teleporting. RollbackSession::setRollbackHook() calls the
// game right before a rollback and right after the re-simulation, both at the same tick: the
// difference of an object's position between the two calls is exactly the correction. Feed it to
// correct(); draw at sim position + offset(); the offset decays with a half-life, so corrections
// blend in over ~100 ms. Large ones (respawn, teleport) snap.
#pragma once
#include <cmath>
#include <cstdint>
#include <map>

namespace aiverse::net {

struct SmoothingConfig {
    float halfLifeMs = 50.0f;      // the offset halves every this many ms
    float snapDistance = 4.0f;     // world units: corrections larger than this are not smoothed
};

class CorrectionSmoother {
public:
    explicit CorrectionSmoother(SmoothingConfig cfg = {}) : cfg_(cfg) {}
    // before - after of one object across a rollback (id: packed entity or any stable key).
    void correct(uint64_t id, float dx, float dy) {
        Offset& o = offsets_[id];
        o.x += dx; o.y += dy;
        if (o.x * o.x + o.y * o.y > cfg_.snapDistance * cfg_.snapDistance) offsets_.erase(id);
    }
    // Once per rendered frame.
    void advance(float dtMs) {
        const float k = cfg_.halfLifeMs > 0 ? std::exp2(-dtMs / cfg_.halfLifeMs) : 0.0f;
        for (auto it = offsets_.begin(); it != offsets_.end();) {
            it->second.x *= k; it->second.y *= k;
            it = it->second.x * it->second.x + it->second.y * it->second.y < 1e-6f ? offsets_.erase(it) : std::next(it);
        }
    }
    // Draw position = sim (interpolated) position + this.
    void offset(uint64_t id, float& dx, float& dy) const {
        auto it = offsets_.find(id);
        dx = it == offsets_.end() ? 0.0f : it->second.x;
        dy = it == offsets_.end() ? 0.0f : it->second.y;
    }
    size_t active() const { return offsets_.size(); }
    void clear() { offsets_.clear(); }
private:
    struct Offset { float x = 0, y = 0; };
    SmoothingConfig cfg_;
    std::map<uint64_t, Offset> offsets_;
};

} // namespace aiverse::net
