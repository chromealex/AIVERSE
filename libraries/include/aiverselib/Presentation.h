#pragma once
#include <algorithm>
#include <cmath>
#include <optional>
namespace aiverse::lib::presentation {
inline float smoothstep(float t) { t=std::clamp(t,0.0f,1.0f); return t*t*(3.0f-2.0f*t); }
inline float lerp(float a,float b,float t) { return a+(b-a)*t; }
inline std::optional<float> timeline(float now,float start,float duration) {
    if (!std::isfinite(now) || !std::isfinite(start) || !std::isfinite(duration) || duration<0) return std::nullopt;
    if (duration==0) return now>=start ? 1.0f : 0.0f;
    return std::clamp((now-start)/duration,0.0f,1.0f);
}
}
