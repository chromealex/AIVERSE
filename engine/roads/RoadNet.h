// Spline road network (docs/roads.md): road types (rules "roadTypes" over the built-in library), the map's
// "roadNetwork" (nodes, cubic Bezier edges with a height at every control point, branches that leave from chosen
// lanes of another road) and the geometry everybody shares: sampled centre lines, lanes, junction shapes, side gaps,
// bridge / tunnel spans and the cells a road covers. Fixed math only, GPU-free and module-free, so the simulation
// (traffic, decks), the view (meshes) and the editor (tool, MCP) see the same roads.
//
// Coordinates: map cells, Vec2{x, z} (Vec2::y is world z). Heights: metres relative to the ground under the point.
// Lanes are counted from the centre line outwards per direction (index 0 next to the median), as in the traffic
// library. "Forward" is the direction from node a to node b of an edge; right-hand traffic drives forward on the right
// side of the centre line (left-hand: on the left).
#pragma once
#include <array>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>
#include "engine/core/Fixed.h"
#include "engine/data/Json.h"

namespace aiverse::roads {

enum class RoadLaneUse : uint8_t { Car = 0, Bus, Parking, Bike, Tram };
enum class RoadCurb : uint8_t { None = 0, Low, High, Sloped, Shoulder };     // edge of the carriageway
enum class RoadBarrier : uint8_t { None = 0, Guardrail, Concrete, Fence, Wall, Hedge };
enum class RoadSurface : uint8_t { Asphalt = 0, Concrete, Cobble, Paving, Gravel, Dirt, Grass };
enum class RoadMarking : uint8_t { None = 0, Dashed, Solid, Double, Yellow };   // centre line

struct RoadLaneDef {
    RoadLaneUse use = RoadLaneUse::Car;
    Fixed width = Fixed::fromInt(1);         // cells
};

// One side of the road, from the carriageway edge outwards: curb, shoulder, sidewalk, barrier at the outer edge.
struct RoadSideDef {
    RoadCurb curb = RoadCurb::None;
    Fixed curbHeight = Fixed::fromRatio(3, 20);   // metres
    Fixed shoulder{};                        // cells of gravel / grass verge next to the carriageway
    RoadSurface shoulderSurface = RoadSurface::Gravel;
    Fixed sidewalk{};                        // cells
    RoadSurface sidewalkSurface = RoadSurface::Paving;
    RoadBarrier barrier = RoadBarrier::None; // at the outer edge (on a bridge: the railing)
    Fixed barrierHeight{};                   // metres, 0: the kind's default
};

// Decorations along a road: a model of the game, a unit type of the game (a map object the simulation places, so it
// collides and shines at night), or a built-in shape (lamp, tree, bush, flowerbed, bollard, pole).
struct RoadPropDef {
    std::string shape;                       // built-in shape id (when neither model nor type is set)
    std::string model;                       // models/... (view only)
    std::string type;                        // unit type id (placed by the simulation)
    int8_t side = 0;                         // -1 left, 1 right, 0 both sides, 2 median
    Fixed spacing = Fixed::fromInt(6);       // cells between props along the road
    Fixed phase{};                           // cells from the start of the edge to the first prop
    Fixed offset{};                          // cells from the inner side of the sidewalk / shoulder outwards (median: from its centre)
    Fixed scale = Fixed::fromInt(1);
    bool onBridges = true;
};

struct RoadTypeDef {
    std::string id, name;
    std::string category = "city";           // city | rural | highway | path (editor palette groups)
    std::vector<RoadLaneDef> forward, backward;   // centre outwards
    Fixed median{};                          // cells between the two directions
    RoadSurface medianSurface = RoadSurface::Concrete;
    RoadBarrier medianBarrier = RoadBarrier::None;
    RoadSideDef left, right;                 // seen in the forward direction
    RoadSurface surface = RoadSurface::Asphalt;
    RoadMarking centre = RoadMarking::Dashed;
    bool laneLines = true;                   // dashed lines between lanes of one direction, edge lines
    bool crossings = true;                   // zebra crossings at signalised junctions
    Fixed speed = Fixed::fromInt(13);        // m/s
    std::vector<RoadPropDef> props;
    Json look = Json::object();              // view keys: "textures": {"asphalt": ..., ...}, "colors": {...}
    bool builtin = false;

    int lanes(int dir) const { return int(dir > 0 ? forward.size() : backward.size()); }
    const std::vector<RoadLaneDef>& lanesOf(int dir) const { return dir > 0 ? forward : backward; }
    Fixed carriageway(int dir) const;        // cells from the centre line to this direction's carriageway edge
    Fixed halfWidth(int side) const;         // cells from the centre line to the outer edge of side -1 (left) / 1 (right)
    Fixed halfWidth() const { return fxMax(halfWidth(-1), halfWidth(1)); }
    bool drivable() const;                   // has at least one car / bus lane
    bool unpaved() const { return surface == RoadSurface::Gravel || surface == RoadSurface::Dirt || surface == RoadSurface::Grass; }
};

// Parses rules "roadTypes" (an object of id -> definition, or a list with "id") on top of the built-in library.
// An entry may "extends" another (built-in or the game's), "builtin": false in rules "roadTypes" drops the library.
// Throws std::runtime_error("roadTypes 'x': ...") on bad data.
std::vector<RoadTypeDef> loadRoadTypes(const Json& rulesRoadTypes);
Fixed roadCellMeters(const Json& rulesRoadTypes);   // metres per map cell of the road data (default 4)
const Json& builtinRoadTypesJson();          // the library (docs/roads.md § Built-in types)
int findRoadType(const std::vector<RoadTypeDef>& types, const std::string& id);
Json roadTypeToJson(const RoadTypeDef& t);
const char* roadSurfaceName(RoadSurface s);
const char* roadBarrierName(RoadBarrier b);
const char* roadCurbName(RoadCurb c);

// ------------------------------------------------------------------ the network (map "roadNetwork")

struct RoadNode {
    int32_t id = 0;
    Vec2 p{};
    Fixed y{};                               // metres over the ground (negative: under it)
};

// Where an end of an edge leaves another road: lanes [lane0, lane1] of direction `dir` of edge `edge` (the branch
// starts on their centre, tangent to that road) at the node the end sits on.
struct RoadAttach {
    int32_t edge = -1;                       // id of the parent edge (-1: none, the end is an ordinary node end)
    int8_t dir = 1;                          // the parent's direction those lanes drive
    int8_t lane0 = 0, lane1 = 0;             // inclusive, centre outwards
};

// A bus stop bay ("карман"): the carriageway widens at the kerb so a bus stops off the lane; the sidewalk gives way.
struct RoadBay {
    Fixed at = Fixed::fromRatio(1, 2);       // middle of the bay, fraction of the edge length (0..1)
    int8_t side = 1;                         // 1 right / -1 left of the edge (a -> b)
    Fixed length = Fixed::fromInt(18);       // metres at full width
    Fixed taper = Fixed::fromInt(12);        // metres of the way in / out at each end
    Fixed width{};                           // metres (0: the outermost lane's width)
    bool shelter = true;                     // a shelter on the sidewalk
    bool parking = false;                    // a parking zone ("parking" in the map), not a bus stop
    Fixed stall = Fixed::fromInt(6);         // parking: metres per marked space along the kerb
    bool inside = false;                     // parking: marked inside the carriageway along the kerb ("inside": true), no bay
};

// An entrance from the road into a yard: the kerb and the sidewalk come down to the road over a short flare, an
// apron of asphalt crosses the sidewalk and runs `depth` metres on into the plot. Map: edge "driveways":
// [{"at": 0..1, "side": 1 | -1, "width": metres (3.5), "depth": metres (6)}].
struct RoadDriveway {
    Fixed at{};
    int8_t side = 1;
    Fixed width = Fixed::fromRatio(7, 2);
    Fixed depth = Fixed::fromInt(6);
};
struct RoadEdge {
    int32_t id = 0;
    int32_t a = 0, b = 0;                    // node ids
    std::string type;
    int typeIndex = -1;                      // resolved
    bool curved = false;                     // c1 / c2 set (else a straight line)
    Vec2 c1{}, c2{};                         // Bezier control points (cells)
    Fixed y1{}, y2{};                        // heights of the control points (metres over the ground)
    std::array<RoadAttach, 2> attach{};      // end a (0) / end b (1)
    bool reversed = false;                   // the type's forward lanes drive b -> a (one-way roads drawn backwards)
    std::vector<RoadBay> bays;               // bus stop bays
    std::vector<RoadDriveway> driveways;     // entrances into the yards behind the sidewalk ("driveways")
};

struct RoadNetwork {
    std::vector<RoadNode> nodes;             // map order
    std::vector<RoadEdge> edges;
    bool signals = true;                     // "signals": false — the game puts up its own traffic lights
    Json extra;                              // the other keys of the map's roadNetwork (replaces, signals...), kept as they are
    bool empty() const { return edges.empty(); }
    int nodeIndex(int32_t id) const;         // -1: none (linear: networks are a few thousand nodes)
    int edgeIndex(int32_t id) const;
    int32_t nextNodeId() const;
    int32_t nextEdgeId() const;
};

// Parses map "roadNetwork" (null: empty). Unknown types throw; `types` resolves typeIndex.
RoadNetwork parseRoadNetwork(const Json& j, const std::vector<RoadTypeDef>& types);
Json roadNetworkToJson(const RoadNetwork& n);

// ------------------------------------------------------------------ geometry

struct RoadSample {
    Vec2 p{};                                // centre line
    Vec2 t{};                                // unit tangent (forward)
    Fixed s{};                               // distance along the edge, cells
    Fixed y{};                               // roadway height, metres (absolute when the geometry had the ground)
    Fixed e{};                               // metres over the ground (bridge > 0, tunnel < 0)
};

// A span of an edge that is not on the ground: a bridge (deck over the ground) or a tunnel.
struct RoadSpan { Fixed s0{}, s1{}; bool tunnel = false; };

// a bay in cells along the edge: widening over [s0, s1], full width over [s1, s2], narrowing over [s2, s3]
struct RoadDrivewayGeom { Fixed s{}; int8_t side = 1; Fixed half{}; Fixed flare{}; Fixed depth{}; };  // cells along / deep

struct RoadBayGeom { Fixed s0{}, s1{}, s2{}, s3{}; int8_t side = 1; Fixed width{}; bool shelter = true; bool parking = false; bool inLane = false; Fixed laneWidth{}; Fixed stall{}; };
// inLane: a parking zone on a road with a parking lane on that side: the spaces are marked in the lane (width 0, no
// bay), laneWidth = that lane's width

struct RoadEdgeGeom {
    std::vector<RoadSample> pts;             // whole edge, a -> b
    Fixed length{};
    Fixed trim0{}, trim1{};                  // cells cut at end a / end b by the junction there
    std::array<Vec2, 2> lateral{};           // centre offset at each end (branches start on their lanes)
    std::array<std::vector<std::pair<Fixed, Fixed>>, 2> gaps;   // left (0) / right (1) side: [s0, s1) where a branch leaves
    std::vector<RoadSpan> spans;
    std::vector<RoadBayGeom> bays;
    std::vector<RoadDrivewayGeom> driveways;
    // a paved road that runs on as an unpaved one: its last stretch at that end wears out (broken asphalt) and
    // narrows to the unpaved road's width. Length in cells (0: none) and the unpaved neighbour's type index.
    // Also any road that runs on as a narrower one tapers to its width there (worn = false: no wear, only the taper).
    std::array<Fixed, 2> wear{};
    std::array<int, 2> wearInto{-1, -1};
    std::array<bool, 2> worn{false, false};
};
// how far the kerb on `side` (-1 / 1, edge frame) is pushed out by bays at distance s (cells)
Fixed roadBayWidth(const RoadEdgeGeom& g, int side, Fixed s);

struct RoadJunctionGeom {
    int32_t node = 0;
    Vec2 centre{};
    Fixed y{};
    std::vector<int> arms;                   // edge indices, by angle (counterclockwise seen from above with z down: atan2 order)
    std::vector<int8_t> armEnd;              // which end of the edge touches the node (0 a, 1 b)
    std::vector<Vec2> outline;               // carriageway outline (closed polygon), fillets at the corners
    std::vector<std::vector<Vec2>> corners;  // sidewalk corner strips between neighbouring arms (inner curve, outer curve interleaved)
    std::vector<std::pair<int, int>> cornerArms;   // per corner: the arm (index into arms) whose right side it starts from, the next one's
    bool signal = false;                     // three or more driven arms
    bool split = false;                      // a branch leaves lanes here: no junction box, the parent runs through
};

struct RoadGeometry {
    std::vector<RoadEdgeGeom> edges;         // per network edge
    std::vector<RoadJunctionGeom> junctions; // nodes where three or more roads meet (or two of different types)
    std::vector<int32_t> nodeJunction;       // per network node: index into junctions or -1
    std::vector<std::string> issues;         // readable problems (the editor shows them; the game ignores them)
    // carriageways around nodes where branches leave lanes: the sides of the other roads are cut along them
    std::vector<std::vector<Vec2>> cuts;
    std::vector<int> cutEdge;                // the edge each cut polygon belongs to
};

// Builds the geometry of every edge and junction. `step` is the sampling length of curves (cells). `groundAt`
// (metres at a cell point; empty: flat 0) makes heights absolute: an edge whose points all lie on the ground drapes
// over it, an elevated one runs a smooth deck through the absolute heights of its control points.
using RoadGround = std::function<Fixed(Vec2)>;
// `cellMetres` (metres per cell) only serves the slope check of the issues.
RoadGeometry buildRoadGeometry(const RoadNetwork& n, const std::vector<RoadTypeDef>& types, const RoadGround& groundAt = {},
                               Fixed step = Fixed::fromRatio(1, 2), Fixed cellMetres = Fixed::fromInt(4));
// Thresholds: a stretch more than this over / under the ground is a bridge / a tunnel.
inline constexpr Fixed kRoadBridgeAbove = Fixed::fromRatio(3, 2), kRoadTunnelBelow = Fixed::fromRatio(5, 2);
// Calls fn(cell index) for every cell whose centre lies inside the polygon (any winding).
void roadFillPolygon(const std::vector<Vec2>& poly, int w, int h, const std::function<void(int)>& fn);
// Carriageway quads [left, right offsets] of an edge between s0 and s1 (trimmed part), as polygons.
void roadStripPolygons(const RoadEdgeGeom& g, Fixed s0, Fixed s1, Fixed left, Fixed right, std::vector<std::vector<Vec2>>& out);

// Point / tangent / height of an edge at distance s (clamped).
RoadSample roadSampleAt(const RoadEdgeGeom& g, Fixed s);
// Closest point of an edge to p: distance along it and signed lateral offset (positive: right of forward).
void roadProject(const RoadEdgeGeom& g, Vec2 p, Fixed& s, Fixed& lateral);
// Centre of lane k (centre outwards) of direction dir (+1 forward, -1 backward) at distance s.
Vec2 roadLanePoint(const RoadTypeDef& t, const RoadEdgeGeom& g, int dir, int k, Fixed s, bool leftHand = false);
Fixed roadLaneOffset(const RoadTypeDef& t, int dir, int k, bool leftHand = false);   // signed, positive: right of forward
Vec2 roadNormal(Vec2 t);                     // right of the tangent (z down the screen: (-t.z, t.x))

// Cubic Bezier helpers (also used by the editor preview).
Vec2 roadBezier(Vec2 a, Vec2 c1, Vec2 c2, Vec2 b, Fixed t);
Fixed roadBezierScalar(Fixed a, Fixed c1, Fixed c2, Fixed b, Fixed t);

// Cells a network covers, for maps without road tiles: per cell 0 nothing, 1 carriageway, 2 sidewalk / shoulder,
// 3 junction. Bridge and tunnel spans are left out (the ground under them stays what it is).
std::vector<uint8_t> rasterizeRoads(const RoadNetwork& n, const std::vector<RoadTypeDef>& types, const RoadGeometry& g, int w, int h);

// Bridge / tunnel decks of the network for the driving module (map "decks" format: id, width, path [x, z, metres]).
// The geometry must have been built with the ground (absolute heights).
Json roadDecks(const RoadNetwork& n, const std::vector<RoadTypeDef>& types, const RoadGeometry& g);

} // namespace aiverse::roads
