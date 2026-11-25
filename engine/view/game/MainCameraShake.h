#pragma once
#include "../../data/Json.h"
#include "../../render/CameraRailPath.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace aiverse::game {
class MainCameraShake {
    struct Impulse {
        CameraShakeComponent shake;
        double age = 0;
        float x, z, radius;
    };
    std::vector<Impulse> active_;
    uint32_t sequence_ = 0;

public:
    void clear() { active_.clear(); sequence_ = 0; }

    void start(const Json& options, float x, float z) {
        CameraShakeComponent shake;
        shake.positionAmplitude = options.getFx("amplitude", Fixed::fromRatio(1,4));
        shake.frequency = options.getFx("frequency", Fixed::fromInt(12));
        shake.duration = options.getFx("duration", Fixed::fromRatio(1,3));
        const float radius = float(options.getFx("radius").toDouble());
        if (shake.positionAmplitude.raw <= 0 || shake.frequency.raw <= 0 ||
            shake.duration.raw <= 0 || radius < 0 || !std::isfinite(x) || !std::isfinite(z)) return;
        shake.style = CameraShakeStyle::Noise;
        shake.seed = ++sequence_;
        if (active_.size() >= 64) active_.erase(active_.begin());
        active_.push_back({shake, 0, x, z, radius});
    }

    render::Camera sample(const render::Camera& navigation, double dt) {
        render::Camera camera = navigation;
        for (auto& impulse : active_) {
            float attenuation = 1.f;
            if (impulse.radius > 0)
                attenuation = std::max(0.f, 1.f - std::hypot(navigation.target.x - impulse.x,
                    navigation.target.z - impulse.z) / impulse.radius);
            const auto offset = render::cameraShakeOffset(impulse.shake, impulse.age);
            camera.target += offset.position * attenuation;
            impulse.age += std::max(0., dt);
        }
        active_.erase(std::remove_if(active_.begin(), active_.end(), [](const Impulse& impulse) {
            return impulse.age >= impulse.shake.duration.toDouble();
        }), active_.end());
        return camera;
    }
};
} // namespace aiverse::game
