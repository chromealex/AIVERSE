// Platform-neutral conversion from browser CSS coordinates to native view bounds.
// Does NOT depend on Cocoa, ImGui, the renderer or a particular game genre.
#pragma once
#include <algorithm>
#include <cmath>
#include <initializer_list>

namespace aiverse::studio {
struct ViewportRect {
    double x=0, y=0, width=0, height=0, dpr=1;
    bool visible=false;
};
struct NativeViewportPlacement {
    double x=0, y=0, width=0, height=0; // Cocoa/AppKit points, origin at bottom left.
    int pixelsW=0, pixelsH=0;
    bool visible=false;
};
inline NativeViewportPlacement viewportPlacement(const ViewportRect& v,
                                                 double hostWidth,
                                                 double hostHeight,
                                                 double backingScale) {
    NativeViewportPlacement out;
    if (!v.visible || !std::isfinite(hostWidth) || !std::isfinite(hostHeight) ||
        hostWidth <= 0 || hostHeight <= 0 || !std::isfinite(backingScale) ||
        backingScale < 0.5 || backingScale > 8.0) return out;
    for (double value : {v.x, v.y, v.width, v.height, v.dpr}) {
        if (!std::isfinite(value)) return out;
    }
    if (v.width <= 1 || v.height <= 1 || v.dpr < 0.5 || v.dpr > 8.0) return out;
    const double left = std::clamp(v.x, 0.0, hostWidth);
    const double top = std::clamp(v.y, 0.0, hostHeight);
    const double right = std::clamp(v.x + v.width, 0.0, hostWidth);
    const double bottom = std::clamp(v.y + v.height, 0.0, hostHeight);
    if (right <= left || bottom <= top) return out;
    out.x = left;
    out.y = hostHeight - bottom;
    out.width = right - left;
    out.height = bottom - top;
    out.pixelsW = int(std::ceil(out.width * backingScale));
    out.pixelsH = int(std::ceil(out.height * backingScale));
    out.visible = out.pixelsW > 0 && out.pixelsH > 0;
    return out;
}
}
