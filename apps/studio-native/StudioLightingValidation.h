// Small, genre-neutral allowlist for Studio Lighting operations.
// Client-side validation improves UX; this authoritative check runs in C++.
#pragma once
#include <cmath>
#include <string_view>

namespace aiverse::studio {
inline bool validLightingScalar(std::string_view key, double value) {
    if (!std::isfinite(value)) return false;
    if (key == "time" || key == "clouds" || key == "storm" || key == "nightLevel")
        return value >= 0 && value <= 1;
    if (key == "dayLength") return value >= 0 && value <= 100000000;
    return false;
}
inline bool validLightScalar(std::string_view key, double value) {
    if (!std::isfinite(value)) return false;
    if (key == "x" || key == "z") return value >= -1000000 && value <= 1000000;
    if (key == "height") return value >= -1000 && value <= 10000;
    if (key == "radius") return value > 0 && value <= 100000;
    if (key == "halo") return value >= 0 && value <= 5;
    if (key == "flicker") return value >= 0 && value <= 1;
    if (key == "rate") return value >= .01 && value <= 30;
    if (key == "angle") return value >= 2 && value <= 170;
    if (key == "pitch") return value >= -10 && value <= 90;
    if (key == "yaw") return value >= -360 && value <= 360;
    if (key == "sweep") return value >= 0 && value <= 180;
    if (key == "sweepSpeed") return value >= 1 && value <= 180;
    if (key == "intensity") return value >= 0 && value <= 10000;
    return false;
}
inline bool validLightText(std::string_view key, std::string_view value) {
    if (key == "pattern") return value == "steady" || value == "pulse" || value == "strobe" || value == "beacon";
    if (key == "name") return value.size() <= 128;
    if (key == "type") return value == "point" || value == "spot";
    if (key == "bake") return value == "realtime" || value == "mixed" || value == "baked";
    return false;
}
inline bool validLightBoolean(std::string_view key) {
    return key == "nightOnly" || key == "electric";
}
} // namespace aiverse::studio
