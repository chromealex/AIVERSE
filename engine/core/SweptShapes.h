#pragma once
// swept-shapes: small Fixed-point swept tests for simulation code.
//  - sweptBoxCircle: does a circle (a pedestrian) moving relative to an oriented box (a car) enter the box,
//    grown by the radius, within a time horizon? Slab test on the box's two axes; gives the entry/exit window.
//  - AxisBox + slideMove: clamp a short move of a walker against axis-aligned boxes (walls, furniture), axis
//    by axis (x first), so the walker slides along a wall instead of stopping.
//  - PolylineBounds: conservative bounds of a polyline (a deck, a lane) grown by a margin, for a cheap broad
//    phase before an exact query.
// Integer / Fixed maths only, no state.
#include "engine/core/Fixed.h"
#include <algorithm>
#include <span>
#include <utility>
#include <vector>

namespace aiverse::lib {

struct SweepWindow {
    bool hit = false;
    Fixed enter{}, exit{};   // time interval (same unit as the velocity's time) inside [0, horizon]
};

// `at`: circle centre relative to the box centre; `velocity`: circle velocity relative to the box;
// `forward`: the box's unit forward axis (xz in Vec2::x / Vec2::y).
inline SweepWindow sweptBoxCircle(Vec2 at, Vec2 velocity, Vec2 forward, Fixed halfLength, Fixed halfWidth,
                                  Fixed radius, Fixed horizon) {
    Fixed lo{}, hi = horizon;
    const Vec2 side{-forward.y, forward.x};
    auto slab = [&](Fixed p, Fixed v, Fixed extent) {
        if (!v.raw) return fxAbs(p) < extent;
        auto a = (-extent - p) / v, b = (extent - p) / v;
        if (a > b) std::swap(a, b);
        lo = fxMax(lo, a); hi = fxMin(hi, b);
        return lo < hi;
    };
    const bool hit = slab(at.dot(forward), velocity.dot(forward), halfLength + radius) &&
                     slab(at.dot(side), velocity.dot(side), halfWidth + radius) && hi > Fixed{} && lo < horizon;
    return hit ? SweepWindow{true, lo, hi} : SweepWindow{};
}

struct AxisBox {
    Fixed minX, maxX, minZ, maxZ;
    bool operator==(const AxisBox&) const = default;
};
inline AxisBox inflate(AxisBox b, Fixed by) { return {b.minX - by, b.maxX + by, b.minZ - by, b.maxZ + by}; }
// Box from a centre and full sizes in 1/1000 units (data files often store millimetres or milli-cells).
inline AxisBox axisBoxMilli(std::int64_t x, std::int64_t z, std::int64_t w, std::int64_t d) {
    auto r = [](std::int64_t n) { return Fixed::fromRaw(n * Fixed::kOne / 2000); };
    return {r(2 * x - w), r(2 * x + w), r(2 * z - d), r(2 * z + d)};
}
inline bool contains(const AxisBox& b, Fixed x, Fixed z) { return x >= b.minX && x <= b.maxX && z >= b.minZ && z <= b.maxZ; }

// Move (x, z) -> (tx, tz), offset (ox, oz) applied to every box: the target is pulled back to the face of any
// box the move would enter. Boxes should already be grown by the walker radius (inflate).
inline void slideMove(Fixed x, Fixed z, Fixed& tx, Fixed& tz, std::span<const AxisBox> boxes, Fixed ox = {}, Fixed oz = {}) {
    for (const auto& b : boxes)
        if (z > oz + b.minZ && z < oz + b.maxZ) {
            if (x <= ox + b.minX && tx > ox + b.minX) tx = fxMin(tx, ox + b.minX);
            else if (x >= ox + b.maxX && tx < ox + b.maxX) tx = fxMax(tx, ox + b.maxX);
        }
    for (const auto& b : boxes)
        if (tx > ox + b.minX && tx < ox + b.maxX) {
            if (z <= oz + b.minZ && tz > oz + b.minZ) tz = fxMin(tz, oz + b.minZ);
            else if (z >= oz + b.maxZ && tz < oz + b.maxZ) tz = fxMax(tz, oz + b.maxZ);
        }
}

struct PolylineBounds {
    bool valid = false;
    AxisBox box{};
};
// Bounds of the points (x, z) grown by `margin`. Empty input gives an invalid bounds that touches everything
// (a broad phase must never reject what it cannot judge).
template <class Point>
PolylineBounds polylineBounds(std::span<const Point> pts, Fixed margin) {
    if (pts.empty()) return {};
    auto zOf = [](const Point& p) { if constexpr (requires { p.z; }) return p.z; else return p.y; };
    AxisBox b{pts[0].x, pts[0].x, zOf(pts[0]), zOf(pts[0])};
    for (const auto& p : pts) {
        b.minX = fxMin(b.minX, p.x); b.maxX = fxMax(b.maxX, p.x);
        b.minZ = fxMin(b.minZ, zOf(p)); b.maxZ = fxMax(b.maxZ, zOf(p));
    }
    return {true, inflate(b, margin)};
}
inline bool mayTouch(const PolylineBounds& b, Fixed x, Fixed z, Fixed radius = {}) {
    return !b.valid || contains(inflate(b.box, radius), x, z);
}

} // namespace aiverse::lib
