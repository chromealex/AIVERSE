// Genre-independent screen layout for ECS cameras. Viewport coordinates are
// normalized to the output (0..1) and do not own GPU resources.
#pragma once
#include "CommonComponents.h"
#include <cstdint>
namespace aiverse {
struct CameraViewportComponent {
    Fixed x{},y{},width=1_fx,height=1_fx; // normalized top-left pixels
    bool enabled=false;                 // false = full screen
    constexpr bool operator==(const CameraViewportComponent&)const=default;
};
struct CameraStackComponent {
    int16_t order=0;                   // increasing order = frontmost
    Fixed opacity=1_fx;               // 0..1, alpha composition
    bool overlay=false;               // enable full-screen overlay
    constexpr bool operator==(const CameraStackComponent&)const=default;
};
} // namespace aiverse
