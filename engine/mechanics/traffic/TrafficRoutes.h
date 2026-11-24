#pragma once
#include "engine/mechanics/Namespace.h"
// city-traffic routes: choices for large / emergency vehicles on top of the traffic road network (Traffic.h).
//  - trafficCarriageway: driven width of a piece in cells (kerb to kerb minus one cell of kerbs);
//  - trafficWideExit: at a junction, the exit that leads to the destination piece over pieces at least
//    `minWidth` wide (fire engines, lorries, buses avoid narrow lanes when a wide route exists; the first and the
//    last piece are always allowed on the route, so stations and addresses on narrow streets stay reachable; the
//    exit taken at this junction must itself be wide unless it is the destination);
//  - trafficLocalDetour: a short bounded route around a local blockage (a jam, a wreck) on a small lattice
//    around the vehicle, simplified to straight legs that the caller's `clear` test accepts.
// Integers / Fixed only and the immutable road net: the same inputs give the same choice everywhere.
#include "engine/core/Graphs.h"
#include "engine/mechanics/Traffic.h"
#include <algorithm>
#include <vector>

namespace aiverse::lib::mechanics {

inline Fixed trafficCarriageway(const TrafficNet& net, const TrafficPiece& p) {
    return trafficKerbAcross(net, p, 1) - trafficKerbAcross(net, p, -1) - Fixed::fromInt(1);
}

// junction: where the vehicle is; incoming: the side it entered by (never chosen); from / destination: piece
// indices; fallback: the side the ordinary router chose. Returns a side (kTrafficWest..kTrafficSouth).
inline int trafficWideExit(const TrafficNet& net, int junction, int incoming, int from, int destination, int fallback,
                           Fixed minWidth = Fixed::fromInt(4)) {
    if (from == destination || junction < 0 || junction >= int(net.junctions.size())) return fallback;
    const std::size_t sink = net.junctions.size();
    Graph graph(sink + 1);
    auto usable = [&](int i) {
        const auto& p = net.pieces[std::size_t(i)];
        return p.lanes > 0 && (trafficCarriageway(net, p) >= minWidth || i == from || i == destination);
    };
    for (std::size_t i = 0; i < net.pieces.size(); ++i) {
        if (!usable(int(i))) continue;
        const auto& p = net.pieces[i];
        const int a = p.junction[0], b = p.junction[1];
        if (a >= 0 && b >= 0) { graph[std::size_t(a)].push_back(std::size_t(b)); graph[std::size_t(b)].push_back(std::size_t(a)); }
        if (int(i) == destination) for (int j : p.junction) if (j >= 0) graph[sink].push_back(std::size_t(j));
    }
    auto route = bfs(graph, sink);
    if (!route) return fallback;
    std::size_t best = kUnreachable;
    int exit = fallback;
    for (int side = 0; side < 4; ++side) {
        const int q = net.junctions[std::size_t(junction)].piece[side];
        if (side == incoming || q < 0) continue;
        const auto& p = net.pieces[std::size_t(q)];
        if (p.lanes <= 0 || (trafficCarriageway(net, p) < minWidth && q != destination)) continue;   // the exit itself must be wide
        if (q == destination) return side;
        const int other = p.junction[0] == junction ? p.junction[1] : p.junction[0];
        if (other >= 0 && route->distance[std::size_t(other)] < route->distance[std::size_t(junction)] &&
            route->distance[std::size_t(other)] < best) {
            best = route->distance[std::size_t(other)]; exit = side;
        }
    }
    return exit;
}

struct TrafficDetourConfig {
    int radius = 24;                              // lattice half size in steps
    Fixed step = Fixed::fromRatio(1, 4);          // lattice step, cells
    Fixed goalTolerance = Fixed::fromRatio(3, 2); // the nearest reachable point must be this close to the goal
    Fixed maxLeg = Fixed::fromInt(2);             // longest straight leg after simplification
    int samplesPerCell = 8;                       // line-of-sight samples
};

// Route start -> (near) goal over points where clear(Vec2) is true, 8-connected without cutting corners.
// Empty: no route within the lattice. The first point is `start`.
template <class Clear>
std::vector<Vec2> trafficLocalDetour(Vec2 start, Vec2 goal, Clear clear, const TrafficDetourConfig& c = {}) {
    const int r = std::max(c.radius, 1), n = r * 2 + 1, count = n * n;
    auto point = [&](int i) { return start + Vec2{c.step * (i % n - r), c.step * (i / n - r)}; };
    std::vector<char> free(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) free[std::size_t(i)] = clear(point(i)) ? 1 : 0;
    const int origin = r * n + r;
    free[std::size_t(origin)] = 1;
    Graph graph(static_cast<std::size_t>(count));
    for (int y = 1; y < n - 1; ++y)
        for (int x = 1; x < n - 1; ++x) {
            const int i = y * n + x;
            if (!free[std::size_t(i)]) continue;
            for (int dy = -1; dy <= 1; ++dy)
                for (int dx = -1; dx <= 1; ++dx) {
                    if (!dx && !dy) continue;
                    const int j = (y + dy) * n + x + dx;
                    if (free[std::size_t(j)] && free[std::size_t(y * n + x + dx)] && free[std::size_t((y + dy) * n + x)])
                        graph[std::size_t(i)].push_back(std::size_t(j));
                }
        }
    auto paths = bfs(graph, std::size_t(origin));
    if (!paths) return {};
    int target = -1;
    Fixed best = Fixed::fromInt(100000);
    for (int i = 0; i < count; ++i)
        if (paths->distance[std::size_t(i)] != kUnreachable) {
            const Fixed d = (point(i) - goal).lengthSq();
            if (d < best) { best = d; target = i; }
        }
    if (target < 0 || best > c.goalTolerance * c.goalTolerance) return {};
    std::vector<Vec2> route;
    for (int i = target; i != origin; i = int(paths->parent[std::size_t(i)])) route.push_back(point(i));
    route.push_back(start);
    std::reverse(route.begin(), route.end());
    auto visible = [&](Vec2 a, Vec2 b) {
        const auto delta = b - a;
        const int samples = std::max(1, int((delta.length() * c.samplesPerCell).floorInt() + 1));
        for (int i = 1; i <= samples; ++i) if (!clear(a + delta * Fixed::fromRatio(i, samples))) return false;
        return true;
    };
    std::vector<Vec2> simple{start};
    for (std::size_t i = 0; i + 1 < route.size();) {
        std::size_t j = i + 1;
        while (j + 1 < route.size() && (route[j + 1] - route[i]).lengthSq() <= c.maxLeg * c.maxLeg && visible(route[i], route[j + 1])) ++j;
        simple.push_back(route[j]);
        i = j;
    }
    return simple;
}

} // namespace aiverse::lib::mechanics
