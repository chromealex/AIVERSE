// Small helpers shared by the RTS module implementation files (not part of the public API).
#pragma once
#include "RtsModule.h"
#include "RtsProgression.h"   // in-match progression hooks (rules "progression")

namespace aiverse::game::detail {

inline const Fixed kHalf = Fixed::fromRatio(1, 2);
inline const Fixed kOne = Fixed::fromInt(1);

// Ground distance: flat maps ignore height (xz); on planets the 3D chord (body-local positions; positions
// on different bodies are kBodySpacing apart, so never in range of each other).
inline Fixed dist2D(Vec3 a, Vec3 b) { return Surface::onSphere(a) ? (a - b).length() : (a - b).xz().length(); }
// Drop the height of a flat-map position (planet positions are 3D and kept as they are).
inline Vec3 flat(Vec3 v) { if (Surface::onSphere(v)) return v; v.y = Fixed{}; return v; }
inline bool canHit(const WeaponDef& w, uint8_t layer) { return layerHit(w.targets, layer); }
inline Fixed maxWeaponRange(const UnitType& t) {
    Fixed r{};
    for (auto& w : t.weapons) r = fxMax(r, w.range);
    return r;
}
// Point b inside the ellipse around a with x radius r and z radius r * aspect (BW Math.IsInEllipse; flat maps).
inline bool inRangeEllipse(Vec3 a, Vec3 b, Fixed r, Fixed aspect) {
    const Fixed ry = r * aspect;
    if (r.raw <= 0 || ry.raw <= 0) return false;
    const Fixed dx = (b.x - a.x) / r, dz = (b.z - a.z) / ry;
    return dx * dx + dz * dz <= kOne;
}
inline Fixed secondsToTicks(Fixed s, uint32_t rate) { return s * int64_t(rate); }

} // namespace aiverse::game::detail
