#pragma once
#include "engine/mechanics/Namespace.h"
// traffic: deterministic city-traffic kernels (road network from a cell mask, lanes, junction turns, signal phases,
// pedestrian "walk" windows, the bot driver's pedals and the sidewalk walker's paths). Fixed math and integers only,
// no engine state: the owner (engine/game/Traffic.cpp, or a game's native module) keeps the entities, steps the
// vehicles (VehicleControllers.h) and calls these functions.
//
// Coordinates are map cells: cell (x, z) covers [x, x + 1) x [z, z + 1); a point is Vec2{x, z} (Vec2::y is world z).
// Roads are axis-aligned bands of road cells (grid cities: GTA 1/2, Manhattan, SimCity). A road cell is
//   - a junction cell when its horizontal and vertical runs are both longer than maxRoadWidth,
//   - a piece cell of an X road (runs along x) when only its horizontal run is longer, of a Z road likewise,
//   - a junction cell too when both runs are short (a square, a plaza, the bulb of a dead end).
// Connected junction cells form a junction box, connected piece cells of one axis a piece (its bounding box).
// Lanes: each piece has `lanes` lanes per direction next to its centre line (laneWidth apart, the outermost
// `parkingLanes` per side are left to parked cars), right-hand traffic unless leftHand.
#include "engine/core/Fixed.h"
#include "engine/core/FixedMath.h"
#include "engine/mechanics/VehicleControllers.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <vector>

namespace aiverse::lib::mechanics {

enum class TrafficAxis : uint8_t { X = 0, Z = 1 };
enum class TrafficLight : uint8_t { Green = 0, Yellow = 1, Red = 2 };

struct TrafficNetConfig {
    Fixed laneWidth = Fixed::fromInt(1);   // cells
    int parkingLanes = 0;                  // outer lanes per side left to parked cars
    int maxRoadWidth = 6;                  // cells: a run longer than this is "along" the road
    bool leftHand = false;
};

// sides of a junction box, by the direction pointing out of it
inline constexpr int kTrafficWest = 0, kTrafficEast = 1, kTrafficNorth = 2, kTrafficSouth = 3;   // -x, +x, -z, +z

struct TrafficPiece {
    TrafficAxis axis = TrafficAxis::X;
    int along0 = 0, along1 = 0;            // cells along the axis, inclusive
    int across0 = 0, across1 = 0;          // cells across, inclusive
    int lanes = 0;                         // driving lanes per direction (0: too narrow for traffic)
    std::array<int, 2> junction{-1, -1};   // junction at the low (along0 - 1) / high (along1 + 1) end, -1: none
    // Curved piece (a spline road, built by the owner from its own road graph): `line` is the centre line from
    // along 0 to along `len` (`at`: distance at each point); along runs over [0, len], across is the signed offset
    // from the centre line (positive: right of the +along direction), so centre() is 0. axis is the main direction
    // of its chord (signal phases of junctions use the arm's side instead). Grid pieces leave `line` empty.
    std::vector<Vec2> line;
    std::vector<Fixed> at;
    Fixed len{};
    Fixed laneW{}, median{};               // lane width (0: the net's), cells between the two directions
    std::array<int8_t, 2> side{-1, -1};    // the junction side at end 0 / 1 (kTrafficWest..)
    std::array<int8_t, 2> dirLanes{-1, -1};   // driving lanes going -along / +along (-1: `lanes` both ways)
    bool curved() const { return !line.empty(); }
    Fixed centre() const { return curved() ? Fixed{} : Fixed::fromRatio(across0 + across1 + 1, 2); }
    int width() const { return across1 - across0 + 1; }
    int length() const { return along1 - along0 + 1; }
};

struct TrafficJunction {
    int x0 = 0, z0 = 0, x1 = 0, z1 = 0;     // cells, inclusive
    std::array<int, 4> piece{-1, -1, -1, -1};   // attached piece by side (kTrafficWest..)
    bool signal = false;                   // three or more driven arms
    int arms() const { int n = 0; for (int p : piece) n += p >= 0; return n; }
    Vec2 centre() const { return {Fixed::fromRatio(x0 + x1 + 1, 2), Fixed::fromRatio(z0 + z1 + 1, 2)}; }
};

struct TrafficNet {
    int w = 0, h = 0;
    TrafficNetConfig cfg;
    std::vector<TrafficPiece> pieces;      // sorted by (axis, along0, across0) of discovery: row-major scan order
    std::vector<TrafficJunction> junctions;
    std::vector<int32_t> pieceAt, junctionAt;   // per cell: -1 or index
};

namespace trafficDetail {
inline Vec2 normalised(Vec2 v) { const Fixed l = v.length(); return l.raw > 0 ? v / l : Vec2{}; }
inline Fixed cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
// curved pieces: segment index for an along value, point and unit tangent there
inline size_t curveSeg(const TrafficPiece& p, Fixed along) {
    size_t lo = 0, hi = p.at.size() - 1;
    while (hi - lo > 1) { const size_t m = (lo + hi) / 2; (p.at[m] <= along ? lo : hi) = m; }
    return lo;
}
inline Vec2 curveTangent(const TrafficPiece& p, Fixed along) {
    if (p.line.size() < 2) return {Fixed::fromInt(1), {}};
    const size_t i = curveSeg(p, along);
    return normalised(p.line[i + 1] - p.line[i]);
}
inline Vec2 curvePoint(const TrafficPiece& p, Fixed along) {
    if (p.line.size() < 2) return p.line.empty() ? Vec2{} : p.line[0];
    along = fxClamp(along, Fixed{}, p.len);
    const size_t i = curveSeg(p, along);
    const Fixed span = p.at[i + 1] - p.at[i];
    const Fixed t = span.raw > 0 ? (along - p.at[i]) / span : Fixed{};
    return p.line[i] + (p.line[i + 1] - p.line[i]) * t;
}
inline Vec2 at(const TrafficPiece& p, Fixed along, Fixed across) {
    if (p.curved()) { const Vec2 t = curveTangent(p, along); return curvePoint(p, along) + Vec2{-t.y, t.x} * across; }
    return p.axis == TrafficAxis::X ? Vec2{along, across} : Vec2{across, along};
}
inline Vec2 axisDir(TrafficAxis a, int dir) { const Fixed s = Fixed::fromInt(dir); return a == TrafficAxis::X ? Vec2{s, {}} : Vec2{{}, s}; }
} // namespace trafficDetail

// Direction of travel `dir` (+1 / -1 along) on the piece at `along` (grid pieces: the axis).
inline Vec2 trafficDir(const TrafficPiece& p, Fixed along, int dir) {
    if (!p.curved()) return trafficDetail::axisDir(p.axis, dir);
    const Vec2 t = trafficDetail::curveTangent(p, along);
    return dir > 0 ? t : Vec2{-t.x, -t.y};
}
// Driving lanes going `dir` (+1 / -1 along).
inline int trafficLanes(const TrafficPiece& p, int dir) {
    const int8_t v = p.dirLanes[dir > 0 ? 1 : 0];
    return v >= 0 ? int(v) : p.lanes;
}

// Side of junction j that piece p (attached at its `end`: 0 low, 1 high) touches: the piece's low end touches the
// junction's high side and the other way round.
inline int trafficSideOf(const TrafficPiece& p, int end) {
    if (p.curved()) return p.side[size_t(end)];
    if (p.axis == TrafficAxis::X) return end == 0 ? kTrafficEast : kTrafficWest;
    return end == 0 ? kTrafficSouth : kTrafficNorth;
}
// Direction (+1 / -1 along the piece axis) of traffic leaving a junction through `side`.
inline int trafficExitDir(int side) { return (side == kTrafficEast || side == kTrafficSouth) ? 1 : -1; }
// Signal axis of traffic arriving at junction end `end` of the piece (grid: the piece axis; curved: the arm's side).
inline TrafficAxis trafficApproachAxis(const TrafficPiece& p, int end) {
    if (!p.curved()) return p.axis;
    return p.side[size_t(end)] >= 0 && p.side[size_t(end)] < 2 ? TrafficAxis::X : TrafficAxis::Z;
}

// Across coordinate of lane k (0 = next to the centre line) for traffic moving `dir` (+1 / -1) along the piece.
inline Fixed trafficLaneAcross(const TrafficNet& n, const TrafficPiece& p, int dir, int k) {
    if (p.curved()) {
        const Fixed lw = p.laneW.raw > 0 ? p.laneW : n.cfg.laneWidth;
        const Fixed off = p.median / 2 + lw * k + lw / 2;
        return n.cfg.leftHand ? -off * dir : off * dir;
    }
    const Fixed off = n.cfg.laneWidth * k + n.cfg.laneWidth / 2;
    int s = p.axis == TrafficAxis::X ? dir : -dir;   // right of +x is +z, right of +z is -x (z grows down the screen)
    if (n.cfg.leftHand) s = -s;
    return p.centre() + off * s;
}
// Point of lane (dir, k) at `along`.
inline Vec2 trafficLanePoint(const TrafficNet& n, const TrafficPiece& p, int dir, int k, Fixed along) {
    return trafficDetail::at(p, along, trafficLaneAcross(n, p, dir, k));
}
// Along coordinate where lane traffic moving `dir` enters the piece (start) / leaves it (end).
inline Fixed trafficPieceStart(const TrafficPiece& p, int dir) {
    if (p.curved()) return dir > 0 ? Fixed{} : p.len;
    return Fixed::fromInt(dir > 0 ? p.along0 : p.along1 + 1);
}
inline Fixed trafficPieceEnd(const TrafficPiece& p, int dir) {
    if (p.curved()) return dir > 0 ? p.len : Fixed{};
    return Fixed::fromInt(dir > 0 ? p.along1 + 1 : p.along0);
}
namespace trafficDetail {
// curved pieces: nearest point of the centre line: along and signed across
inline void curveProject(const TrafficPiece& p, Vec2 q, Fixed& along, Fixed& across) {
    along = {}; across = {};
    Fixed best{};
    bool have = false;
    for (size_t i = 0; i + 1 < p.line.size(); ++i) {
        const Vec2 a = p.line[i], d = p.line[i + 1] - a;
        const Fixed L2 = d.lengthSq();
        const Fixed t = L2.raw > 0 ? fxClamp((q - a).dot(d) / L2, Fixed{}, Fixed::fromInt(1)) : Fixed{};
        const Vec2 c = a + d * t;
        const Fixed dist = (q - c).lengthSq();
        if (!have || dist < best) {
            have = true; best = dist;
            along = p.at[i] + (p.at[i + 1] - p.at[i]) * t;
            const Vec2 tn = normalised(d);
            across = (q - c).dot(Vec2{-tn.y, tn.x});
        }
    }
}
} // namespace trafficDetail
inline Fixed trafficAlongOf(const TrafficPiece& p, Vec2 q) {
    if (p.curved()) { Fixed a, c; trafficDetail::curveProject(p, q, a, c); return a; }
    return p.axis == TrafficAxis::X ? q.x : q.y;
}
inline Fixed trafficAcrossOf(const TrafficPiece& p, Vec2 q) {
    if (p.curved()) { Fixed a, c; trafficDetail::curveProject(p, q, a, c); return c; }
    return p.axis == TrafficAxis::X ? q.y : q.x;
}
// Points of lane (dir, k) of a curved piece from `from` to its end, about one cell apart (neither end included), for
// a route to follow the curve; empty for grid pieces (their lanes are straight).
inline std::vector<Vec2> trafficLaneCurve(const TrafficNet& n, const TrafficPiece& p, int dir, int k, Fixed from) {
    std::vector<Vec2> out;
    if (!p.curved()) return out;
    const Fixed end = dir > 0 ? p.len : Fixed{}, step = Fixed::fromInt(dir);
    for (Fixed a = from + step; dir > 0 ? a < end - Fixed::fromRatio(1, 2) : a > end + Fixed::fromRatio(1, 2); a += step)
        out.push_back(trafficLanePoint(n, p, dir, k, a));
    return out;
}
// Points of lane (dir, k) strictly between along `from` and `to` (travelling dir), about one cell apart and at least
// half a cell from both ends; empty for grid pieces. For a route that stops part way along a curved piece.
inline std::vector<Vec2> trafficLaneBetween(const TrafficNet& n, const TrafficPiece& p, int dir, int k, Fixed from, Fixed to) {
    std::vector<Vec2> out;
    if (!p.curved()) return out;
    const Fixed step = Fixed::fromInt(dir), half = Fixed::fromRatio(1, 2);
    for (Fixed a = from + step; dir > 0 ? a < to - half : a > to + half; a += step) out.push_back(trafficLanePoint(n, p, dir, k, a));
    return out;
}
// Length of the piece in cells (grid: cells along, curved: the centre line).
inline Fixed trafficPieceLength(const TrafficPiece& p) { return p.curved() ? p.len : Fixed::fromInt(p.length()); }
// A point half a cell beyond the carriageway edge at `along`, on the high (`side` > 0, larger across) or low side:
// where a bin, a stop sign or a waiting pedestrian stands next to the road.
inline Fixed trafficKerbAcross(const TrafficNet& n, const TrafficPiece& p, int side) {
    if (!p.curved()) return side > 0 ? Fixed::fromInt(p.across1 + 1) + Fixed::fromRatio(1, 2) : Fixed::fromInt(p.across0) - Fixed::fromRatio(1, 2);
    const Fixed lw = p.laneW.raw > 0 ? p.laneW : n.cfg.laneWidth;
    // the lanes on this side are the ones moving +along (right-hand traffic) when side > 0
    const int dir = (side > 0) != n.cfg.leftHand ? 1 : -1;
    const Fixed edge = p.median / 2 + lw * trafficLanes(p, dir) + Fixed::fromRatio(1, 2);
    return side > 0 ? edge : -edge;
}
inline Vec2 trafficKerbPoint(const TrafficNet& n, const TrafficPiece& p, Fixed along, int side) {
    return trafficDetail::at(p, along, trafficKerbAcross(n, p, side));
}
// Direction leaving junction j through `side` along the piece attached there (+1 / -1 along it).
inline int trafficExitDirAt(const TrafficNet& n, int j, int side) {
    const int q = j >= 0 && size_t(j) < n.junctions.size() ? n.junctions[size_t(j)].piece[size_t(side)] : -1;
    if (q < 0 || !n.pieces[size_t(q)].curved()) return trafficExitDir(side);
    const TrafficPiece& p = n.pieces[size_t(q)];
    return p.junction[0] == j && p.side[0] == side ? 1 : -1;
}

// Builds the network. `road` has w * h cells (row-major, non-zero = road). Deterministic: row-major scans.
inline TrafficNet buildTrafficNet(int w, int h, const std::vector<uint8_t>& road, const TrafficNetConfig& cfg) {
    TrafficNet n;
    n.w = w; n.h = h; n.cfg = cfg;
    if (w <= 0 || h <= 0 || road.size() != size_t(w) * size_t(h)) return n;
    const size_t N = road.size();
    std::vector<int> hr(N, 0), vr(N, 0);
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w;) {
            if (!road[size_t(z * w + x)]) { ++x; continue; }
            int e = x; while (e < w && road[size_t(z * w + e)]) ++e;
            for (int i = x; i < e; ++i) hr[size_t(z * w + i)] = e - x;
            x = e;
        }
    for (int x = 0; x < w; ++x)
        for (int z = 0; z < h;) {
            if (!road[size_t(z * w + x)]) { ++z; continue; }
            int e = z; while (e < h && road[size_t(e * w + x)]) ++e;
            for (int i = z; i < e; ++i) vr[size_t(i * w + x)] = e - z;
            z = e;
        }
    // kind: 0 none, 1 X piece, 2 Z piece, 3 junction
    std::vector<uint8_t> kind(N, 0);
    for (size_t i = 0; i < N; ++i) {
        if (!road[i]) continue;
        const bool lx = hr[i] > cfg.maxRoadWidth, lz = vr[i] > cfg.maxRoadWidth;
        kind[i] = lx && !lz ? 1 : (lz && !lx ? 2 : 3);
    }
    n.pieceAt.assign(N, -1);
    n.junctionAt.assign(N, -1);
    std::vector<int> stack;
    for (int z = 0; z < h; ++z)
        for (int x = 0; x < w; ++x) {
            const size_t i0 = size_t(z * w + x);
            const uint8_t k = kind[i0];
            if (!k || (k == 3 ? n.junctionAt[i0] : n.pieceAt[i0]) >= 0) continue;
            const int id = k == 3 ? int(n.junctions.size()) : int(n.pieces.size());
            int bx0 = x, bx1 = x, bz0 = z, bz1 = z;
            stack.assign(1, int(i0));
            (k == 3 ? n.junctionAt : n.pieceAt)[i0] = id;
            while (!stack.empty()) {
                const int c = stack.back(); stack.pop_back();
                const int cx = c % w, cz = c / w;
                bx0 = std::min(bx0, cx); bx1 = std::max(bx1, cx); bz0 = std::min(bz0, cz); bz1 = std::max(bz1, cz);
                const int nx[4] = {cx - 1, cx + 1, cx, cx}, nz[4] = {cz, cz, cz - 1, cz + 1};
                for (int d = 0; d < 4; ++d) {
                    if (nx[d] < 0 || nz[d] < 0 || nx[d] >= w || nz[d] >= h) continue;
                    const size_t ni = size_t(nz[d] * w + nx[d]);
                    if (kind[ni] != k) continue;
                    auto& slot = (k == 3 ? n.junctionAt : n.pieceAt)[ni];
                    if (slot >= 0) continue;
                    slot = id;
                    stack.push_back(int(ni));
                }
            }
            if (k == 3) { TrafficJunction j; j.x0 = bx0; j.x1 = bx1; j.z0 = bz0; j.z1 = bz1; n.junctions.push_back(j); continue; }
            TrafficPiece p;
            p.axis = k == 1 ? TrafficAxis::X : TrafficAxis::Z;
            if (p.axis == TrafficAxis::X) { p.along0 = bx0; p.along1 = bx1; p.across0 = bz0; p.across1 = bz1; }
            else { p.along0 = bz0; p.along1 = bz1; p.across0 = bx0; p.across1 = bx1; }
            const int perSide = (Fixed::fromInt(p.width()) / 2 / fxMax(cfg.laneWidth, Fixed::fromRatio(1, 8))).floorInt();
            p.lanes = std::max(0, perSide - std::min(cfg.parkingLanes, std::max(0, perSide - 1))); // Kingston: no parking lane on narrow streets
            n.pieces.push_back(p);
        }
    // links: the cell just past each end, at the middle of the band
    for (size_t pi = 0; pi < n.pieces.size(); ++pi) {
        TrafficPiece& p = n.pieces[pi];
        const int mid = (p.across0 + p.across1) / 2;
        for (int end = 0; end < 2; ++end) {
            const int a = end == 0 ? p.along0 - 1 : p.along1 + 1;
            const int cx = p.axis == TrafficAxis::X ? a : mid, cz = p.axis == TrafficAxis::X ? mid : a;
            if (cx < 0 || cz < 0 || cx >= w || cz >= h) continue;
            const int j = n.junctionAt[size_t(cz * w + cx)];
            if (j < 0) continue;
            p.junction[size_t(end)] = j;
            auto& slot = n.junctions[size_t(j)].piece[size_t(trafficSideOf(p, end))];
            if (slot < 0 || n.pieces[size_t(slot)].lanes < p.lanes) slot = int(pi);
        }
    }
    for (auto& j : n.junctions) {
        int driven = 0;
        for (int p : j.piece) driven += p >= 0 && n.pieces[size_t(p)].lanes > 0;
        j.signal = driven >= 3;
    }
    return n;
}

// ------------------------------------------------------------------ signals

struct TrafficSignalTiming {
    uint32_t green = 200, yellow = 60, allRed = 20;   // ticks, per axis
    uint32_t walk = 140;                              // ticks of "walk" at the start of a green (crossing the other axis)
    uint32_t cycle() const { return 2 * (green + yellow + allRed); }
};
// Phase time of `axis` in its own half-cycle: [0, green) green, then yellow, then red until its next green.
inline uint32_t trafficPhaseTime(const TrafficSignalTiming& t, uint32_t offset, uint32_t tick, TrafficAxis axis) {
    const uint32_t c = std::max<uint32_t>(t.cycle(), 1);
    uint32_t ph = uint32_t((uint64_t(tick) + offset) % c);
    if (axis == TrafficAxis::Z) ph = (ph + c - (t.green + t.yellow + t.allRed)) % c;
    return ph;
}
inline TrafficLight trafficLight(const TrafficSignalTiming& t, uint32_t offset, uint32_t tick, TrafficAxis axis) {
    const uint32_t ph = trafficPhaseTime(t, offset, tick, axis);
    return ph < t.green ? TrafficLight::Green : ph < t.green + t.yellow ? TrafficLight::Yellow : TrafficLight::Red;
}
// May a pedestrian start crossing a road of axis `roadAxis` at this junction now? (the traffic across it is green,
// its own traffic red, and at least `margin` ticks of the walk window of that green are left)
inline bool trafficWalk(const TrafficSignalTiming& t, uint32_t offset, uint32_t tick, TrafficAxis roadAxis, uint32_t margin = 0) {
    const TrafficAxis other = roadAxis == TrafficAxis::X ? TrafficAxis::Z : TrafficAxis::X;
    const uint32_t window = std::min(t.walk, t.green);
    return trafficPhaseTime(t, offset, tick, other) + margin < window;
}
// Ticks until `axis` turns green (0: green now).
inline uint32_t trafficUntilGreen(const TrafficSignalTiming& t, uint32_t offset, uint32_t tick, TrafficAxis axis) {
    const uint32_t ph = trafficPhaseTime(t, offset, tick, axis);
    return ph < t.green ? 0 : t.cycle() - ph;
}

// ------------------------------------------------------------------ the bot driver

struct TrafficDriverParams {
    Fixed steerGain = Fixed::fromInt(5);              // steer per turn of heading error
    Fixed throttleGain = Fixed::fromRatio(1, 2);      // throttle per m/s below the target
    Fixed brakeGain = Fixed::fromRatio(1, 3);         // brake per m/s above the target
    Fixed deadband = Fixed::fromRatio(3, 10);         // m/s
};
// Pedals and steering that bring the car (yaw in library turns, forward speed m/s, position in cells) toward the
// pursuit point `target` at `targetSpeed` m/s. Below 0.5 m/s it never touches the brake pedal (the wheeled automatic
// would select reverse): it holds the handbrake or pulls away.
inline VehicleInput trafficDriverInput(const TrafficDriverParams& prm, Fixed yaw, Fixed speed, Vec2 pos, Vec2 target, Fixed targetSpeed) {
    VehicleInput in;
    const Vec2 d = target - pos;
    if (d.lengthSq().raw > 0) {
        const Fixed want = fxAtan2Turns(d.x, d.y);   // yaw 0 faces +z, a quarter turn +x
        const Fixed err = vehicleDetail::wrap(want - yaw);
        in.steer = fxClamp(err * prm.steerGain, -1_fx, 1_fx);
        // Slow down BEFORE chasing a point beside/behind the vehicle.
        const Fixed angle=fxAbs(err);
        if(angle>Fixed::fromRatio(1,16))
            targetSpeed=fxMin(targetSpeed,fxMax(Fixed::fromRatio(3,2),Fixed::fromInt(6)*(1_fx-fxMin(angle*4,1_fx))));
    }
    const Fixed dv = targetSpeed - speed;
    // near a standstill the brake pedal would select reverse: hold the handbrake instead, or pull away
    if (speed < Fixed::fromRatio(1, 2) && speed.raw >= 0 && dv <= prm.deadband) { in.handbrake = true; return in; }
    if (dv > prm.deadband) in.throttle = fxClamp(dv * prm.throttleGain, Fixed::fromRatio(1, 5), 1_fx);
    else if (dv < -prm.deadband) in.brake = fxClamp(-dv * prm.brakeGain, Fixed::fromRatio(1, 10), 1_fx);
    else in.throttle = Fixed::fromRatio(1, 10);
    if (speed.raw < 0) { in.brake = {}; in.throttle = Fixed::fromRatio(1, 2); }   // rolled back: drive forward again
    return in;
}
// Highest speed (m/s) from which the car still stops within `metres` at `decel` m/s^2.
inline Fixed trafficStopSpeed(Fixed metres, Fixed decel) { return metres.raw <= 0 ? Fixed{} : fxSqrt(metres * decel * 2); }

// Pure pursuit point: `pos` projected onto the polyline pts[0] -> pts[1] -> ... (pts[0] = the last point passed),
// then `ahead` cells further along it (the last point if the polyline is shorter). A car beside its lane is pulled
// back onto it within about `ahead` cells.
inline Vec2 trafficPursuit(Vec2 pos, const std::vector<Vec2>& pts, Fixed ahead) {
    if (pts.empty()) return pos;
    if (pts.size() == 1) return pts[0];
    // the closest point over the first segments
    size_t seg = 0; Fixed bestT{}, bestD{};
    for (size_t i = 0; i + 1 < pts.size() && i < 3; ++i) {
        const Vec2 ab = pts[i + 1] - pts[i];
        const Fixed l2 = ab.lengthSq();
        const Fixed t = l2.raw > 0 ? fxClamp((pos - pts[i]).dot(ab) / l2, Fixed{}, 1_fx) : Fixed{};
        const Fixed d = (pos - (pts[i] + ab * t)).lengthSq();
        if (i == 0 || d < bestD) { bestD = d; bestT = t; seg = i; }
    }
    Vec2 a = pts[seg] + (pts[seg + 1] - pts[seg]) * bestT;
    for (size_t i = seg + 1; i < pts.size(); ++i) {
        const Vec2 b = pts[i], d = b - a;
        const Fixed l = d.length();
        if (l >= ahead && l.raw > 0) return a + d * (ahead / l);
        ahead = ahead - l;
        a = b;
    }
    return a;
}

// Quadratic Bezier from a through b (control) to c, `steps` points without a (ending at c).
inline void trafficCurve(Vec2 a, Vec2 b, Vec2 c, int steps, std::vector<Vec2>& out) {
    for (int i = 1; i <= steps; ++i) {
        const Fixed t = Fixed::fromRatio(i, steps), u = 1_fx - t;
        out.push_back(a * (u * u) + b * (u * t * 2) + c * (t * t));
    }
}

// ------------------------------------------------------------------ pedestrians

// Breadth-first distances (steps, -1 unreachable) over walkable cells from `start`, 4 neighbours in the fixed
// order west, east, north, south; `parent` gets the previous cell on a shortest path.
inline void trafficWalkField(int w, int h, const std::vector<uint8_t>& walk, int start, std::vector<int32_t>& dist, std::vector<int32_t>& parent, int32_t maxSteps = INT32_MAX) {
    const size_t N = size_t(w) * size_t(h);
    dist.assign(N, -1); parent.assign(N, -1);
    if (start < 0 || size_t(start) >= N || walk.size() != N || !walk[size_t(start)]) return;
    std::vector<int32_t> q; q.reserve(256);
    q.push_back(start); dist[size_t(start)] = 0;
    for (size_t head = 0; head < q.size(); ++head) {
        const int c = q[head], cx = c % w, cz = c / w;
        if (dist[size_t(c)] >= maxSteps) continue;
        const int nx[4] = {cx - 1, cx + 1, cx, cx}, nz[4] = {cz, cz, cz - 1, cz + 1};
        for (int d = 0; d < 4; ++d) {
            if (nx[d] < 0 || nz[d] < 0 || nx[d] >= w || nz[d] >= h) continue;
            const int ni = nz[d] * w + nx[d];
            if (!walk[size_t(ni)] || dist[size_t(ni)] >= 0) continue;
            dist[size_t(ni)] = dist[size_t(c)] + 1; parent[size_t(ni)] = c;
            q.push_back(ni);
        }
    }
}
// Cells from the start (excluded) to `goal` (included) through `parent`; empty when unreachable.
inline std::vector<int32_t> trafficWalkPath(const std::vector<int32_t>& parent, const std::vector<int32_t>& dist, int goal) {
    std::vector<int32_t> out;
    if (goal < 0 || size_t(goal) >= dist.size() || dist[size_t(goal)] <= 0) return out;
    for (int c = goal; c >= 0 && dist[size_t(c)] > 0; c = parent[size_t(c)]) out.push_back(c);
    std::reverse(out.begin(), out.end());
    return out;
}

// Shortest sidewalk route over the whole map (A*, 4 neighbours, Manhattan heuristic): the way round a river to its
// bridge. `search` keeps the work arrays between calls (a call touches only the cells it visits, whatever the size of
// the city). `out`: cells from the start (excluded) to the goal (included). False: no way, or more than `maxNodes`
// cells would have to be looked at. Deterministic: ties go to the lower cell index.
struct TrafficWalkSearch {
    std::vector<uint32_t> seen;
    std::vector<int32_t> g, parent;
    uint32_t stamp = 0;
    struct Node { int32_t f, h, cell; };
    std::vector<Node> heap;
};
inline bool trafficWalkRoute(int w, int h, const std::vector<uint8_t>& walk, int start, int goal, TrafficWalkSearch& s, std::vector<int32_t>& out, int maxNodes = 200000) {
    out.clear();
    const size_t N = size_t(w) * size_t(h);
    if (start < 0 || goal < 0 || size_t(start) >= N || size_t(goal) >= N || walk.size() != N || !walk[size_t(start)] || !walk[size_t(goal)]) return false;
    if (start == goal) return true;
    if (s.seen.size() != N) { s.seen.assign(N, 0); s.g.assign(N, 0); s.parent.assign(N, -1); s.stamp = 0; }
    if (++s.stamp == 0) { std::fill(s.seen.begin(), s.seen.end(), 0); s.stamp = 1; }
    const int gx = goal % w, gz = goal / w;
    auto est = [&](int c) { const int dx = c % w - gx, dz = c / w - gz; return (dx < 0 ? -dx : dx) + (dz < 0 ? -dz : dz); };
    auto after = [](const TrafficWalkSearch::Node& a, const TrafficWalkSearch::Node& b) {
        return a.f != b.f ? a.f > b.f : a.h != b.h ? a.h > b.h : a.cell > b.cell;
    };
    s.heap.clear();
    s.seen[size_t(start)] = s.stamp; s.g[size_t(start)] = 0; s.parent[size_t(start)] = -1;
    s.heap.push_back({est(start), est(start), start});
    int nodes = 0;
    bool found = false;
    while (!s.heap.empty()) {
        std::pop_heap(s.heap.begin(), s.heap.end(), after);
        const TrafficWalkSearch::Node n = s.heap.back();
        s.heap.pop_back();
        const int c = n.cell;
        if (n.f - n.h > s.g[size_t(c)]) continue;   // a better way to it was found since
        if (c == goal) { found = true; break; }
        if (++nodes > maxNodes) break;
        const int cx = c % w, cz = c / w;
        const int nx[4] = {cx - 1, cx + 1, cx, cx}, nz[4] = {cz, cz, cz - 1, cz + 1};
        for (int d = 0; d < 4; ++d) {
            if (nx[d] < 0 || nz[d] < 0 || nx[d] >= w || nz[d] >= h) continue;
            const int ni = nz[d] * w + nx[d];
            if (!walk[size_t(ni)]) continue;
            const int32_t ng = s.g[size_t(c)] + 1;
            if (s.seen[size_t(ni)] == s.stamp && s.g[size_t(ni)] <= ng) continue;
            s.seen[size_t(ni)] = s.stamp; s.g[size_t(ni)] = ng; s.parent[size_t(ni)] = c;
            const int hh = est(ni);
            s.heap.push_back({ng + hh, hh, ni});
            std::push_heap(s.heap.begin(), s.heap.end(), after);
        }
    }
    if (!found) return false;
    for (int c = goal; c != start; c = s.parent[size_t(c)]) out.push_back(c);
    std::reverse(out.begin(), out.end());
    return true;
}

} // namespace aiverse::lib::mechanics
