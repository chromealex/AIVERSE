// AIVERSE LENS: authoring components for cinematic and procedural cameras.
// Fixed-point ECS data; no graphics handles or presentation clocks stored here.
#pragma once
#include "CommonComponents.h"
#include <array>
#include <cstdint>

namespace aiverse {
enum class CameraRigMode : uint8_t { Static, LookAt, Follow, Orbit, Rail };
struct CameraRigComponent {
    CameraRigMode mode=CameraRigMode::Static;
    Entity target=kNullEntity;                // stable generation checked target
    Vec3 offset{0_fx,2_fx,-5_fx};            // world-space follow offset
    Fixed distance=5_fx, yaw=0_fx, pitch=0_fx; // angles in radians
    Fixed smoothing=8_fx;                    // presentation damping, 0 = instant
    bool trackTarget=true;
    bool enabled=true;
    constexpr bool operator==(const CameraRigComponent&) const = default;
};
struct CameraRailComponent {
    std::array<Vec3,8> points{};             // local view-world points
    uint8_t count=0;
    Fixed duration=5_fx;
    bool loop=false;
    constexpr bool operator==(const CameraRailComponent&) const = default;
};
struct CameraShakeComponent {
    Fixed positionAmplitude=0_fx, rotationAmplitude=0_fx; // m / radians
    Fixed frequency=12_fx;
    Fixed startTime=0_fx, duration=0_fx;     // presentation seconds
    constexpr bool operator==(const CameraShakeComponent&) const = default;
};
// These are physical-lens metadata. Sensor/focal length determine the vertical
// FOV; f-number, shutter and ISO feed per-camera post processing later.
struct CameraLensComponent {
    Fixed sensorHeightMm=24_fx, focalLengthMm=50_fx;
    Fixed fNumber=Fixed::fromRatio(28,10);
    Fixed focusDistance=10_fx;
    Fixed shutterSeconds=Fixed::fromRatio(1,60);
    Fixed iso=100_fx;
    bool physicalFov=false;
    constexpr bool operator==(const CameraLensComponent&) const = default;
};
} // namespace aiverse
