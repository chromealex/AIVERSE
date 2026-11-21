// Genre-independent approximation for editor preview lighting (not gameplay time).
#pragma once
#include <algorithm>
#include <cmath>

namespace aiverse::studio {
struct PreviewDayLighting { float sun = 1, ambient = 1, clouds = 0, night = 0; };
inline PreviewDayLighting studioPreviewDayLighting(double time, double nightLevel,
                                                  double clouds, double storm) {
    const auto clamp = [](double v, double fallback) {
        return std::isfinite(v) ? std::clamp(v, 0.0, 1.0) : fallback;
    };
    time=clamp(time,.5); nightLevel=clamp(nightLevel,.18);
    clouds=clamp(clouds,0); storm=clamp(storm,0);
    constexpr double tau = 6.283185307179586;
    const double daylight=std::max(0.0,std::sin((time-.25)*tau));
    const double overcast=std::max(clouds,storm*.85);
    PreviewDayLighting value;
    value.sun=float((.08+.97*daylight)*(1.0-overcast*.68));
    value.ambient=float(nightLevel+(1.0-nightLevel)*(.2+.8*daylight));
    value.clouds=float(overcast);
    value.night=float(1.0-daylight);
    return value;
}
} // namespace aiverse::studio
