// Authored routes between garrison points (Bunker Wars / Mushroom Wars 2 RoutesModule, ported 1:1 in fixed point):
// map "routes" with control points, per-point column offsets and optional raw per-line nodes, the sub-spline
// builder (Catmull-Rom with BW's 10 nodes between control points, column offsets, end trimming), the rules keys
// (lanes.routes / lanes.groups) and the map "screen" block (MultiscreenModule). GPU-free and module-free so the
// editor and tools share the exact geometry the simulation walks (docs/game-format.md § Routes).
#pragma once
#include <array>
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/Fixed.h"
#include "../../engine/data/Json.h"

namespace aiverse::game {

// rules.json "lanes": {"routes": {...}, "groups": {...}}. Defaults are the BW BattleParametersInfo values at
// 40 BW units per cell (tools/importers/bunkerwars): unitsInRow 6, columnsPadding 20, flyColumnsPadding 45,
// pathPointOffset 10, rowsOffset 70, flyingRowsOffset 120, defaultInOutLength 100.
struct RouteRules {
    int lines = 6;                                    // parallel sub-splines per route (BW common.unitsInRow)
    Fixed columnPadding = Fixed::fromRatio(1, 2);     // offset step between columns (BW units.columnsPadding)
    Fixed flyColumnPadding = Fixed::fromRatio(9, 8); // flying routes (BW units.flyColumnsPadding)
    Fixed trim = Fixed::fromRatio(1, 4);              // cut from both ends of a sub-spline (BW routeVisualizer.pathPointOffset)
    int smooth = 10;                                  // spline nodes between control points (BW Spline.BETWEEN_NODE_COUNT)
    Fixed inOut = Fixed::fromRatio(5, 2);                  // flying routes: straight in / out part (BW MapRoutesModule.defaultInOutLength)
    Fixed jitter = Fixed::fromRatio(1, 8);            // random diagonal offset per unit (BW units.positionRandomOffset 5)
    Fixed fade = Fixed::fromRatio(1, 2);              // alpha fade length at both ends (BW units.alphaFadeThreshold 20)
    Fixed coverageStep = Fixed::fromRatio(5, 4);      // tower coverage: range step of the table (BW Spline.towerCoverageRangeStep 50)
    // BW groups (UnitsModule Group): rows of `lines` units, the garrison pays per row, sends merge, owner change closes
    bool groups = false;
    Fixed rowSpacing = Fixed::fromRatio(7, 4);        // distance walked between rows (BW units.rowsOffset)
    Fixed flyRowSpacing = Fixed::fromInt(3);    // flying groups (BW units.flyingRowsOffset)
};
// Parses the optional "routes" / "groups" members of the rules "lanes" object (throws readable errors).
void loadRouteRules(const Json& lanes, RouteRules& out);

// One authored route (map "routes" entry). Positions are map cells (x, z).
struct RouteDef {
    std::string from, to;                             // entity ids (map "entities"[i].id) or "#<index>"
    std::vector<Vec2> points;                         // control points (BW SplineRouteInfo.points); first / last = the points
    std::vector<std::array<Fixed, 8>> offsets;        // per control point: extra offset per line (BW Spline.Offset)
    std::vector<std::vector<Vec2>> lines;             // optional raw nodes per line (BW rawSplineLines)
    std::vector<uint32_t> zones;                      // per control point: zone flags (BW ZoneType; 2 = slowdown)
    std::vector<Fixed> slowdown;                      // per control point: slowdown coefficient (BW; AI length only)
    std::vector<std::string> blockers;                // entity ids that close the route while alive (BW closedRoutes)
    // precomputed tower coverage (BW SplineRouteInfo.towersCoverage): per tower point, covered route length for tower
    // ranges 0, step, 2 step ... 20 step (stored tail-aligned like BW: leading zeros dropped)
    std::vector<std::pair<std::string, std::vector<Fixed>>> coverage;
    bool flying = false;                              // flying route: fly column padding (BW Route.isFlying)
};
// Parses one map "routes" entry; throws std::runtime_error("map route N: ...") on bad data.
RouteDef parseRoute(const Json& j, int index);
Json routeToJson(const RouteDef& r);

// The sub-splines (lines) of a route exactly as BW Spline.SetPoints + SubSpline.Recalculate build them: line k
// follows its raw nodes when it has at least two, else the control points with the column offset of k applied to
// the inner points; each is a Catmull-Rom node list, trimmed at both ends when long enough.
std::vector<std::vector<Vec2>> buildRouteLines(const RouteDef& r, const RouteRules& rr);
Fixed polylineLength(const std::vector<Vec2>& pts);
// BW AI length: control-point distances, slowdown segments stretched (Spline.InitZoneNodes).
Fixed routeAiLength(const RouteDef& r);
// Lane offset label of line k: even lines to the left (-1, -2, ...), odd to the right (+1, +2, ...), so sorting by
// |offset| keeps the line order (RtsModule::lanesBetween).
inline int routeLineOffset(int k) { return (k % 2 == 0) ? -(k / 2 + 1) : (k / 2 + 1); }
// Straight flying route between two points (BW MapRoutesModule.CreateRouteInfo with makePointsBetween).
RouteDef flyingRoute(Vec2 from, Vec2 to, const RouteRules& rr);

// Map "screen" (BW MultiscreenModule): the playable view is a screen-sized window over a larger map; points outside
// it do not produce. center = BW -offset; left / right / top / bottom = how far the center may move from its start
// (BW MapHeader.gameplayRectOffset, map cells; top = towards -z).
struct ScreenDef {
    bool enabled = false;
    Vec2 size{};                                      // screen size in cells (BW 2720 x 1520 = 68 x 38)
    Vec2 center{};                                    // start center
    Fixed left{}, right{}, top{}, bottom{};           // movement bounds of the center
    Fixed smooth = Fixed::fromInt(10);                // view: follow rate per second (BW world.multiscreenMovementSmooth)
};
ScreenDef parseScreen(const Json& j, int mapW, int mapH);
// BW PointInfo.CompareScreenGroups: both empty -> true; one empty -> false; else any shared group.
bool screenGroupsMatch(const std::vector<int>& a, const std::vector<int>& b);

} // namespace aiverse::game
