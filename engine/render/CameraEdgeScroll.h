#pragma once

#include <algorithm>
#include "engine/data/Json.h"
#include "engine/render/Math3D.h"

namespace aiverse::render {

struct CameraEdgeScroll {
    float zonePx = 3, delaySeconds = 0, speed = 1;

    static CameraEdgeScroll fromManifest(const Json& manifest) {
        const auto& block = manifest["edgeScroll"];
        auto setting = [&](const char* name, float fallback) {
            return block[name].isNumber() ? std::max(0.f, float(block[name].toDouble())) : fallback;
        };
        return {setting("zonePx", 3), setting("delaySeconds", 0), setting("speed", 1)};
    }
};

class EdgeScrollPointer {
    struct Axis {
        int direction = 0;
        float elapsed = 0;

        float sample(float position, float extent, const CameraEdgeScroll& settings, float dt) {
            const float zone = std::min(settings.zonePx, extent * 0.5f);
            const int side = int(position > extent - zone) - int(position < zone);
            if (side != direction) { direction = side; elapsed = 0; }
            if (!side || dt <= 0) return 0;
            const float wait = std::max(0.f, settings.delaySeconds - elapsed);
            elapsed = std::min(settings.delaySeconds, elapsed + dt);
            return side * settings.speed * std::max(0.f, dt - wait) / dt;
        }
    } x_, y_;

public:
    void reset() { x_ = {}; y_ = {}; }

    float2 sample(const CameraEdgeScroll& settings, float x, float y, float w, float h,
                  float dt, bool enabled) {
        if (!enabled || w <= 0 || h <= 0 || x < 0 || y < 0 || x > w || y > h) {
            reset();
            return {};
        }
        return {x_.sample(x, w, settings, dt), -y_.sample(y, h, settings, dt)};
    }
};

} // namespace aiverse::render
