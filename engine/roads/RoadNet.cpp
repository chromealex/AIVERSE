// Spline road network: map "roadNetwork" parsing and the shared geometry (RtsRoadNet.h, docs/roads.md).
#include "RoadNet.h"
#include <algorithm>
#include <map>
#include <stdexcept>
#include "engine/core/FixedMath.h"

namespace aiverse::roads {

namespace {

const Fixed kOne = Fixed::fromInt(1), kHalf = Fixed::fromRatio(1, 2), kTiny = Fixed::fromRatio(1, 256);

Fixed cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
Vec2 unit(Vec2 v, Vec2 def = {Fixed::fromInt(1), {}}) {
    const Fixed l = v.length();
    return l.raw > 8 ? v / l : def;
}
Vec2 lerp(Vec2 a, Vec2 b, Fixed t) { return a + (b - a) * t; }
Fixed lerpF(Fixed a, Fixed b, Fixed t) { return a + (b - a) * t; }

Vec2 point(const Json& v, const std::string& where) {
    if (!v.isArray() || v.size() < 2 || v.size() > 3) throw std::runtime_error(where + ": [x, z] or [x, z, height]");
    return {v[size_t(0)].fx(), v[size_t(1)].fx()};
}

RoadAttach attachOf(const Json& j, const std::string& where) {
    RoadAttach a;
    if (j.isNull()) return a;
    if (!j.isObject()) throw std::runtime_error(where + ": {\"edge\", \"dir\", \"lanes\": [first, last]}");
    a.edge = int32_t(j.getInt("edge", -1));
    const int64_t d = j.getInt("dir", 1);
    if (d != 1 && d != -1) throw std::runtime_error(where + ".dir: 1 (the parent's forward lanes) or -1");
    a.dir = int8_t(d);
    const Json& l = j["lanes"];
    if (l.isNumber()) a.lane0 = a.lane1 = int8_t(std::clamp<int64_t>(l.integer(), 0, 7));
    else if (l.isArray() && l.size() == 2) {
        a.lane0 = int8_t(std::clamp<int64_t>(l[size_t(0)].integer(), 0, 7));
        a.lane1 = int8_t(std::clamp<int64_t>(l[size_t(1)].integer(), 0, 7));
        if (a.lane1 < a.lane0) std::swap(a.lane0, a.lane1);
    } else throw std::runtime_error(where + ".lanes: a lane index or [first, last] (centre outwards)");
    return a;
}

Json attachJson(const RoadAttach& a) {
    Json j = Json::object();
    j.set("edge", Json::number(int64_t(a.edge)));
    j.set("dir", Json::number(int64_t(a.dir)));
    Json l = Json::array();
    l.push(Json::number(int64_t(a.lane0)));
    l.push(Json::number(int64_t(a.lane1)));
    j.set("lanes", l);
    return j;
}

// signed offset of the centre of lanes [l0, l1] of direction dir of type t (positive: right of forward)
Fixed laneGroupOffset(const RoadTypeDef& t, int dir, int l0, int l1, bool leftHand) {
    return (roadLaneOffset(t, dir, l0, leftHand) + roadLaneOffset(t, dir, l1, leftHand)) / 2;
}

// intersection of p + d u and q + e v: false when (nearly) parallel
bool intersect(Vec2 p, Vec2 d, Vec2 q, Vec2 e, Fixed& u, Fixed& v) {
    const Fixed den = cross(d, e);
    if (fxAbs(den) < Fixed::fromRatio(1, 40)) return false;
    const Vec2 w = q - p;
    u = cross(w, e) / den;
    v = cross(w, d) / den;
    return true;
}

void quadCurve(Vec2 a, Vec2 c, Vec2 b, int n, std::vector<Vec2>& out) {   // a excluded, b included
    for (int i = 1; i <= n; ++i) {
        const Fixed t = Fixed::fromRatio(i, n), u = kOne - t;
        out.push_back(a * (u * u) + c * (u * t * 2) + b * (t * t));
    }
}

} // namespace

Vec2 roadNormal(Vec2 t) { return {-t.y, t.x}; }

Fixed roadBayWidth(const RoadEdgeGeom& g, int side, Fixed s) {
    Fixed w{};
    for (auto& b : g.bays) {
        if (b.side != side || s <= b.s0 || s >= b.s3) continue;
        Fixed k = kOne;
        if (s < b.s1) k = (s - b.s0) / (b.s1 - b.s0);
        else if (s > b.s2) k = (b.s3 - s) / (b.s3 - b.s2);
        k = k * k * (Fixed::fromInt(3) - k * 2);   // smoothstep: the kerb bends in and out gently
        w = fxMax(w, b.width * k);
    }
    return w;
}

Vec2 roadBezier(Vec2 a, Vec2 c1, Vec2 c2, Vec2 b, Fixed t) {
    const Fixed u = kOne - t;
    return a * (u * u * u) + c1 * (u * u * t * 3) + c2 * (u * t * t * 3) + b * (t * t * t);
}
Fixed roadBezierScalar(Fixed a, Fixed c1, Fixed c2, Fixed b, Fixed t) {
    const Fixed u = kOne - t;
    return a * (u * u * u) + c1 * (u * u * t * 3) + c2 * (u * t * t * 3) + b * (t * t * t);
}

Fixed roadLaneOffset(const RoadTypeDef& t, int dir, int k, bool leftHand) {
    const auto& v = t.lanesOf(dir);
    Fixed off = t.median / 2;
    for (int i = 0; i < k && i < int(v.size()); ++i) off += v[size_t(i)].width;
    if (k >= 0 && k < int(v.size())) off += v[size_t(k)].width / 2;
    else off += (v.empty() ? Fixed::fromRatio(7, 8) : v.back().width) / 2;
    // forward lanes on the right of the centre line (right-hand traffic)
    const int s = (dir > 0) != leftHand ? 1 : -1;
    return off * s;
}

// ------------------------------------------------------------------ network

int RoadNetwork::nodeIndex(int32_t id) const {
    for (size_t i = 0; i < nodes.size(); ++i) if (nodes[i].id == id) return int(i);
    return -1;
}
int RoadNetwork::edgeIndex(int32_t id) const {
    for (size_t i = 0; i < edges.size(); ++i) if (edges[i].id == id) return int(i);
    return -1;
}
int32_t RoadNetwork::nextNodeId() const { int32_t m = 0; for (auto& n : nodes) m = std::max(m, n.id); return m + 1; }
int32_t RoadNetwork::nextEdgeId() const { int32_t m = 0; for (auto& e : edges) m = std::max(m, e.id); return m + 1; }

RoadNetwork parseRoadNetwork(const Json& j, const std::vector<RoadTypeDef>& types) {
    RoadNetwork n;
    if (j.isNull()) return n;
    if (!j.isObject()) throw std::runtime_error("map roadNetwork: {\"nodes\": [...], \"edges\": [...]}");
    n.signals = j.getBool("signals", true);
    n.extra = Json::object();
    for (const auto& [k, v] : j.members()) if (k != "nodes" && k != "edges") n.extra.set(k, v);
    const Json& nodes = j["nodes"];
    for (size_t i = 0; i < nodes.size(); ++i) {
        const Json& v = nodes[i];
        const std::string where = "map roadNetwork.nodes[" + std::to_string(i) + "]";
        RoadNode d;
        d.id = int32_t(v.getInt("id", -1));
        if (d.id <= 0) throw std::runtime_error(where + ".id: a positive number");
        if (n.nodeIndex(d.id) >= 0) throw std::runtime_error(where + ": node id " + std::to_string(d.id) + " twice");
        if (!v["x"].isNumber() || !v["z"].isNumber()) throw std::runtime_error(where + ": \"x\", \"z\" (cells)");
        d.p = {v.getFx("x"), v.getFx("z")};
        d.y = v.getFx("y");
        n.nodes.push_back(d);
    }
    const Json& edges = j["edges"];
    for (size_t i = 0; i < edges.size(); ++i) {
        const Json& v = edges[i];
        const std::string where = "map roadNetwork.edges[" + std::to_string(i) + "]";
        RoadEdge e;
        e.id = int32_t(v.getInt("id", -1));
        if (e.id <= 0) throw std::runtime_error(where + ".id: a positive number");
        if (n.edgeIndex(e.id) >= 0) throw std::runtime_error(where + ": edge id " + std::to_string(e.id) + " twice");
        e.a = int32_t(v.getInt("a", -1));
        e.b = int32_t(v.getInt("b", -1));
        if (n.nodeIndex(e.a) < 0 || n.nodeIndex(e.b) < 0) throw std::runtime_error(where + ": \"a\" / \"b\" must be node ids");
        if (e.a == e.b) throw std::runtime_error(where + ": both ends on node " + std::to_string(e.a));
        e.type = v.getStr("type");
        e.typeIndex = findRoadType(types, e.type);
        if (e.typeIndex < 0) throw std::runtime_error(where + ": unknown road type '" + e.type + "'");
        if (v.has("c1") || v.has("c2")) {
            if (!v.has("c1") || !v.has("c2")) throw std::runtime_error(where + ": a curve needs both \"c1\" and \"c2\"");
            e.curved = true;
            e.c1 = point(v["c1"], where + ".c1");
            e.c2 = point(v["c2"], where + ".c2");
        }
        const RoadNode& A = n.nodes[size_t(n.nodeIndex(e.a))];
        const RoadNode& B = n.nodes[size_t(n.nodeIndex(e.b))];
        // heights of the control points: the third value of c1 / c2, else the straight slope between the nodes
        e.y1 = v["c1"].size() == 3 ? v["c1"][size_t(2)].fx() : lerpF(A.y, B.y, Fixed::fromRatio(1, 3));
        e.y2 = v["c2"].size() == 3 ? v["c2"][size_t(2)].fx() : lerpF(A.y, B.y, Fixed::fromRatio(2, 3));
        if (v.has("y1")) e.y1 = v.getFx("y1");
        if (v.has("y2")) e.y2 = v.getFx("y2");
        e.attach[0] = attachOf(v["attachA"], where + ".attachA");
        e.attach[1] = attachOf(v["attachB"], where + ".attachB");
        e.reversed = v.getBool("reversed", false);
        {
            const Json& dw = v["driveways"];
            if (!dw.isNull() && !dw.isArray()) throw std::runtime_error(where + ".driveways: a list of entrances into the yards");
            for (size_t k = 0; k < dw.size(); ++k) {
                const Json& d = dw[k];
                const std::string w = where + ".driveways[" + std::to_string(k) + "]";
                RoadDriveway r;
                r.at = d.getFx("at", Fixed::fromRatio(1, 2));
                if (r.at < Fixed{} || r.at > Fixed::fromInt(1)) throw std::runtime_error(w + ".at: 0..1 along the edge");
                const int side = d.getInt("side", 1);
                if (side != 1 && side != -1) throw std::runtime_error(w + ".side: 1 (right of a -> b) or -1");
                r.side = int8_t(side);
                r.width = d.getFx("width", r.width);
                r.depth = d.getFx("depth", r.depth);
                if (r.width <= Fixed{} || r.depth < Fixed{}) throw std::runtime_error(w + ": width must be positive, depth not negative");
                e.driveways.push_back(r);
            }
        }
        for (const char* key : {"stops", "parking"}) {
        const bool parking = std::string(key) == "parking";
        const Json& bays = v[key];
        if (!bays.isNull() && !bays.isArray()) throw std::runtime_error(where + "." + key + (parking ? ": a list of parking zones" : ": a list of bus stop bays"));
        for (size_t k = 0; k < bays.size(); ++k) {
            const Json& b = bays[k];
            const std::string w = where + "." + key + "[" + std::to_string(k) + "]";
            RoadBay bay;
            bay.parking = parking;
            bay.at = fxClamp(b.getFx("at", Fixed::fromRatio(1, 2)), Fixed{}, kOne);
            const int64_t side = b.getInt("side", 1);
            if (side != 1 && side != -1) throw std::runtime_error(w + ".side: 1 (right of a -> b) or -1");
            bay.side = int8_t(side);
            bay.length = b.getFx("length", Fixed::fromInt(parking ? 24 : 18));
            bay.taper = b.getFx("taper", Fixed::fromInt(parking ? 5 : 12));
            bay.width = b.getFx("width", parking ? Fixed::fromRatio(5, 2) : Fixed{});
            bay.stall = b.getFx("stall", Fixed::fromInt(6));
            if (bay.length < Fixed{} || bay.taper < Fixed{} || bay.width < Fixed{} || bay.stall <= Fixed{}) throw std::runtime_error(w + ": lengths must be positive");
            bay.shelter = !parking && b.getBool("shelter", true);
            bay.inside = parking && b.getBool("inside", false);
            e.bays.push_back(bay);
        }
        }
        n.edges.push_back(e);
    }
    for (size_t i = 0; i < n.edges.size(); ++i)
        for (int end = 0; end < 2; ++end) {
            const RoadAttach& a = n.edges[i].attach[size_t(end)];
            if (a.edge < 0) continue;
            const int pi = n.edgeIndex(a.edge);
            const std::string where = "map roadNetwork edge " + std::to_string(n.edges[i].id);
            if (pi < 0 || pi == int(i)) throw std::runtime_error(where + ": attached to an unknown edge " + std::to_string(a.edge));
            const RoadEdge& p = n.edges[size_t(pi)];
            const int32_t node = end == 0 ? n.edges[i].a : n.edges[i].b;
            if (p.a != node && p.b != node) throw std::runtime_error(where + ": the parent edge " + std::to_string(a.edge) + " does not end at node " + std::to_string(node));
            const RoadTypeDef& pt = types[size_t(p.typeIndex)];
            if (a.lane1 >= pt.lanes(a.dir)) throw std::runtime_error(where + ": the parent edge has " + std::to_string(pt.lanes(a.dir)) + " lanes that way");
        }
    return n;
}

Json roadNetworkToJson(const RoadNetwork& n) {
    Json j = n.extra.isObject() ? n.extra : Json::object();
    Json nodes = Json::array();
    for (auto& d : n.nodes) {
        Json v = Json::object();
        v.set("id", Json::number(int64_t(d.id)));
        v.set("x", Json::number(d.p.x));
        v.set("z", Json::number(d.p.y));
        if (d.y.raw) v.set("y", Json::number(d.y));
        nodes.push(v);
    }
    Json edges = Json::array();
    for (auto& e : n.edges) {
        Json v = Json::object();
        v.set("id", Json::number(int64_t(e.id)));
        v.set("a", Json::number(int64_t(e.a)));
        v.set("b", Json::number(int64_t(e.b)));
        v.set("type", Json::string(e.type));
        if (e.curved) {
            for (int k = 0; k < 2; ++k) {
                Json c = Json::array();
                const Vec2 p = k ? e.c2 : e.c1;
                c.push(Json::number(p.x));
                c.push(Json::number(p.y));
                c.push(Json::number(k ? e.y2 : e.y1));
                v.set(k ? "c2" : "c1", c);
            }
        } else {
            const int ia = n.nodeIndex(e.a), ib = n.nodeIndex(e.b);
            const Fixed ya = ia >= 0 ? n.nodes[size_t(ia)].y : Fixed{}, yb = ib >= 0 ? n.nodes[size_t(ib)].y : Fixed{};
            if (e.y1 != lerpF(ya, yb, Fixed::fromRatio(1, 3))) v.set("y1", Json::number(e.y1));
            if (e.y2 != lerpF(ya, yb, Fixed::fromRatio(2, 3))) v.set("y2", Json::number(e.y2));
        }
        if (e.attach[0].edge >= 0) v.set("attachA", attachJson(e.attach[0]));
        if (e.attach[1].edge >= 0) v.set("attachB", attachJson(e.attach[1]));
        if (e.reversed) v.set("reversed", Json::fromBool(true));
        if (!e.driveways.empty()) {
            Json dw = Json::array();
            for (auto& d : e.driveways) {
                Json o = Json::object();
                o.set("at", Json::number(d.at));
                o.set("side", Json::number(Fixed::fromInt(d.side)));
                if (d.width != Fixed::fromRatio(7, 2)) o.set("width", Json::number(d.width));
                if (d.depth != Fixed::fromInt(6)) o.set("depth", Json::number(d.depth));
                dw.push(o);
            }
            v.set("driveways", dw);
        }
        if (!e.bays.empty()) {
            Json a = Json::array(), pk = Json::array();
            for (auto& b : e.bays) {
                Json o = Json::object();
                o.set("at", Json::number(b.at));
                o.set("side", Json::number(int64_t(b.side)));
                o.set("length", Json::number(b.length));
                o.set("taper", Json::number(b.taper));
                if (b.width.raw) o.set("width", Json::number(b.width));
                if (b.parking) { if (b.stall != Fixed::fromInt(6)) o.set("stall", Json::number(b.stall)); if (b.inside) o.set("inside", Json::fromBool(true)); pk.push(o); continue; }
                if (!b.shelter) o.set("shelter", Json::fromBool(false));
                a.push(o);
            }
            if (a.size()) v.set("stops", a);
            if (pk.size()) v.set("parking", pk);
        }
        edges.push(v);
    }
    j.set("nodes", nodes);
    j.set("edges", edges);
    return j;
}

// ------------------------------------------------------------------ geometry

RoadSample roadSampleAt(const RoadEdgeGeom& g, Fixed s) {
    if (g.pts.empty()) return {};
    if (s <= g.pts.front().s) return g.pts.front();
    if (s >= g.pts.back().s) return g.pts.back();
    size_t lo = 0, hi = g.pts.size() - 1;
    while (hi - lo > 1) { const size_t m = (lo + hi) / 2; (g.pts[m].s <= s ? lo : hi) = m; }
    const RoadSample& a = g.pts[lo];
    const RoadSample& b = g.pts[hi];
    const Fixed span = b.s - a.s;
    const Fixed t = span.raw > 0 ? (s - a.s) / span : Fixed{};
    RoadSample r;
    r.p = lerp(a.p, b.p, t);
    r.t = unit(lerp(a.t, b.t, t), a.t);
    r.s = s;
    r.y = lerpF(a.y, b.y, t);
    r.e = lerpF(a.e, b.e, t);
    return r;
}

void roadProject(const RoadEdgeGeom& g, Vec2 p, Fixed& s, Fixed& lateral) {
    s = {}; lateral = {};
    if (g.pts.size() < 2) return;
    Fixed best{};
    bool have = false;
    for (size_t i = 0; i + 1 < g.pts.size(); ++i) {
        const Vec2 a = g.pts[i].p, d = g.pts[i + 1].p - a;
        const Fixed L2 = d.lengthSq();
        Fixed t = L2.raw > 0 ? fxClamp((p - a).dot(d) / L2, {}, kOne) : Fixed{};
        const Vec2 q = a + d * t;
        const Fixed dist = (p - q).lengthSq();
        if (!have || dist < best) {
            have = true; best = dist;
            s = lerpF(g.pts[i].s, g.pts[i + 1].s, t);
            lateral = (p - q).dot(roadNormal(unit(d)));
        }
    }
}

Vec2 roadLanePoint(const RoadTypeDef& t, const RoadEdgeGeom& g, int dir, int k, Fixed s, bool leftHand) {
    const RoadSample r = roadSampleAt(g, s);
    return r.p + roadNormal(r.t) * roadLaneOffset(t, dir, k, leftHand);
}

void roadStripPolygons(const RoadEdgeGeom& g, Fixed s0, Fixed s1, Fixed left, Fixed right, std::vector<std::vector<Vec2>>& out) {
    if (g.pts.size() < 2 || s1 <= s0) return;
    std::vector<RoadSample> pts;
    pts.push_back(roadSampleAt(g, s0));
    for (auto& p : g.pts) if (p.s > s0 && p.s < s1) pts.push_back(p);
    pts.push_back(roadSampleAt(g, s1));
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        const Vec2 na = roadNormal(pts[i].t), nb = roadNormal(pts[i + 1].t);
        out.push_back({pts[i].p + na * left, pts[i + 1].p + nb * left, pts[i + 1].p + nb * right, pts[i].p + na * right});
    }
}

void roadFillPolygon(const std::vector<Vec2>& poly, int w, int h, const std::function<void(int)>& fn) {
    if (poly.size() < 3) return;
    Fixed x0 = poly[0].x, x1 = x0, z0 = poly[0].y, z1 = z0;
    for (auto& p : poly) { x0 = fxMin(x0, p.x); x1 = fxMax(x1, p.x); z0 = fxMin(z0, p.y); z1 = fxMax(z1, p.y); }
    const int cx0 = std::max(0, int(x0.floorInt())), cx1 = std::min(w - 1, int(x1.floorInt()));
    const int cz0 = std::max(0, int(z0.floorInt())), cz1 = std::min(h - 1, int(z1.floorInt()));
    std::vector<Fixed> xs;
    for (int z = cz0; z <= cz1; ++z) {
        const Fixed zc = Fixed::fromInt(z) + kHalf;
        xs.clear();
        for (size_t i = 0; i < poly.size(); ++i) {   // crossings of the row through the cell centres (even-odd)
            const Vec2 a = poly[i], b = poly[(i + 1) % poly.size()];
            if ((a.y <= zc) == (b.y <= zc)) continue;
            xs.push_back(a.x + (b.x - a.x) * ((zc - a.y) / (b.y - a.y)));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t k = 0; k + 1 < xs.size(); k += 2)
            for (int x = std::max(cx0, int((xs[k] - kHalf).floorInt()) + 1); x <= cx1; ++x) {
                const Fixed xc = Fixed::fromInt(x) + kHalf;
                if (xc < xs[k]) continue;
                if (xc >= xs[k + 1]) break;
                fn(z * w + x);
            }
    }
}

namespace {

struct Arm {
    int edge = -1;
    int8_t end = 0;
    Vec2 d{};                     // leaving the node
    Fixed angle{};
    Fixed cwL{}, cwR{}, hwL{}, hwR{};   // carriageway / outer half widths, left / right as seen leaving the node
    bool drivable = false;
};

// Height along an edge through the heights of its four control points (at t = 0, 1/3, 2/3, 1): a clamped cubic
// spline (continuous slope and curvature, level at both ends), so a raised middle makes one smooth arch that leaves
// the ground without a kink, and a lowered one a smooth dip.
Fixed heightProfile(Fixed y0, Fixed y1, Fixed y2, Fixed y3, Fixed t) {
    const Fixed third = Fixed::fromRatio(1, 3);
    // slopes per unit t at the inner keys: m0 = m3 = 0, m(i-1) + 4 m(i) + m(i+1) = 9 (y(i+1) - y(i-1))
    const Fixed A = (y2 - y0) * 9, B = (y3 - y1) * 9;
    const Fixed m[4] = {Fixed{}, (A * 4 - B) / 15, (B * 4 - A) / 15, Fixed{}};
    const Fixed y[4] = {y0, y1, y2, y3};
    int k = int((t * 3).floorInt());
    k = std::clamp(k, 0, 2);
    const Fixed u = fxClamp((t - third * k) * 3, Fixed{}, kOne), u2 = u * u, u3 = u2 * u;
    const Fixed h00 = u3 * 2 - u2 * 3 + kOne, h10 = u3 - u2 * 2 + u, h01 = u2 * 3 - u3 * 2, h11 = u3 - u2;
    return y[k] * h00 + m[k] * third * h10 + y[k + 1] * h01 + m[k + 1] * third * h11;
}

void sampleEdge(const RoadNetwork& n, const RoadEdge& e, const RoadGround& ground, Fixed step, RoadEdgeGeom& g) {
    const RoadNode& A = n.nodes[size_t(n.nodeIndex(e.a))];
    const RoadNode& B = n.nodes[size_t(n.nodeIndex(e.b))];
    const Vec2 c1 = e.curved ? e.c1 : lerp(A.p, B.p, Fixed::fromRatio(1, 3));
    const Vec2 c2 = e.curved ? e.c2 : lerp(A.p, B.p, Fixed::fromRatio(2, 3));
    const Fixed est = (c1 - A.p).length() + (c2 - c1).length() + (B.p - c2).length();
    const bool flatProfile = e.y1 == lerpF(A.y, B.y, Fixed::fromRatio(1, 3)) && e.y2 == lerpF(A.y, B.y, Fixed::fromRatio(2, 3));
    const bool onGround = fxAbs(A.y) < Fixed::fromRatio(1, 20) && fxAbs(B.y) < Fixed::fromRatio(1, 20) && fxAbs(e.y1) < Fixed::fromRatio(1, 20) && fxAbs(e.y2) < Fixed::fromRatio(1, 20);
    const int segs = (e.curved || !flatProfile || ground) ? int(std::clamp<int64_t>((est / step).floorInt() + 1, 1, 512)) : 1;
    // absolute heights of the control points (smooth deck) unless the whole edge lies on the ground (drape)
    // elevated edges run over the straight line between the ground at their two ends (a river bed under the
    // control points does not pull the deck down)
    Fixed ga{}, gb{};
    if (ground) { ga = ground(A.p); gb = ground(B.p); }
    const Fixed g1 = lerpF(ga, gb, Fixed::fromRatio(1, 3)), g2 = lerpF(ga, gb, Fixed::fromRatio(2, 3));
    g.pts.clear();
    Fixed s{};
    Vec2 prev = A.p;
    for (int i = 0; i <= segs; ++i) {
        const Fixed t = Fixed::fromRatio(i, segs), u = kOne - t;
        RoadSample r;
        r.p = roadBezier(A.p, c1, c2, B.p, t);
        const Vec2 der = (c1 - A.p) * (u * u * 3) + (c2 - c1) * (u * t * 6) + (B.p - c2) * (t * t * 3);
        r.t = unit(der, unit(B.p - A.p));
        if (i) s += (r.p - prev).length();
        prev = r.p;
        r.s = s;
        const Fixed gr = ground ? ground(r.p) : Fixed{};
        if (onGround) { r.y = gr; r.e = {}; }
        else if (!ground) { r.e = heightProfile(A.y, e.y1, e.y2, B.y, t); r.y = r.e; }
        else {
            r.y = heightProfile(ga + A.y, g1 + e.y1, g2 + e.y2, gb + B.y, t);
            r.e = r.y - gr;
            // a deck that comes down to the ground sits on it (landings, the ground under a low ramp)
            if (fxAbs(r.e) < Fixed::fromRatio(1, 10)) { r.y = gr; r.e = {}; }
        }
        g.pts.push_back(r);
    }
    g.length = s;
}

void spansOf(RoadEdgeGeom& g) {
    g.spans.clear();
    for (size_t i = 0; i < g.pts.size(); ++i) {
        const Fixed e = g.pts[i].e;
        const int kind = e > kRoadBridgeAbove ? 1 : e < -kRoadTunnelBelow ? 2 : 0;
        if (!kind) continue;
        const Fixed s0 = i ? g.pts[i - 1].s : g.pts[i].s;
        if (!g.spans.empty() && g.spans.back().tunnel == (kind == 2) && g.spans.back().s1 >= s0) { g.spans.back().s1 = i + 1 < g.pts.size() ? g.pts[i + 1].s : g.pts[i].s; continue; }
        g.spans.push_back({s0, i + 1 < g.pts.size() ? g.pts[i + 1].s : g.pts[i].s, kind == 2});
    }
}

} // namespace

RoadGeometry buildRoadGeometry(const RoadNetwork& n, const std::vector<RoadTypeDef>& types, const RoadGround& ground, Fixed step, Fixed cellMetres) {
    RoadGeometry G;
    G.edges.resize(n.edges.size());
    G.nodeJunction.assign(n.nodes.size(), -1);
    if (step < Fixed::fromRatio(1, 8)) step = Fixed::fromRatio(1, 8);
    for (size_t i = 0; i < n.edges.size(); ++i) sampleEdge(n, n.edges[i], ground, step, G.edges[i]);

    // branches: shift the end that leaves the lanes of its parent onto those lanes (smoothly over a few cells)
    for (size_t i = 0; i < n.edges.size(); ++i) {
        const RoadEdge& e = n.edges[i];
        RoadEdgeGeom& g = G.edges[i];
        for (int end = 0; end < 2; ++end) {
            const RoadAttach& at = e.attach[size_t(end)];
            if (at.edge < 0) continue;
            const int pi = n.edgeIndex(at.edge);
            const RoadEdge& P = n.edges[size_t(pi)];
            const RoadEdgeGeom& pg = G.edges[size_t(pi)];
            const RoadTypeDef& pt = types[size_t(P.typeIndex)];
            const int32_t node = end == 0 ? e.a : e.b;
            const RoadSample ps = P.a == node ? pg.pts.front() : pg.pts.back();
            // the parent's chosen lanes (type direction: the parent may be drawn reversed)
            const int pdir = P.reversed ? -at.dir : at.dir;
            const Fixed off = laneGroupOffset(pt, pdir, at.lane0, at.lane1, false);
            // which way those lanes drive at the node, and which way the branch leaves it
            const Vec2 travel = ps.t * Fixed::fromInt(at.dir);
            const RoadSample bs = end == 0 ? g.pts.front() : g.pts.back();
            const Vec2 leave = end == 0 ? bs.t : Vec2{-bs.t.x, -bs.t.y};
            const bool diverge = travel.dot(leave) >= Fixed{};
            // the branch's lanes that join them: driving away from the node (diverge) or toward it (merge);
            // in the branch's a -> b sense that is +1 for lanes leaving end a
            const int edgeDir = (end == 0) == diverge ? 1 : -1;
            const RoadTypeDef& bt = types[size_t(e.typeIndex)];
            const int btDir = e.reversed ? -edgeDir : edgeDir;
            const int nb = bt.lanes(btDir);
            Fixed bOff{};
            if (nb > 0) bOff = laneGroupOffset(bt, btDir, 0, nb - 1, false);
            else G.issues.push_back("edge " + std::to_string(e.id) + ": no lanes drive " + (diverge ? "out of" : "into") + " edge " + std::to_string(P.id) + " here (reverse it)");
            // put the branch's lanes on the parent's lanes: target point minus where the branch lanes are now
            const Vec2 shift = roadNormal(ps.t) * off - roadNormal(bs.t) * bOff;
            g.lateral[size_t(end)] = shift;
            const Fixed fade = fxMax(fxMin(g.length / 2, Fixed::fromInt(8)), kOne);
            for (auto& p : g.pts) {
                const Fixed d = end == 0 ? p.s : g.length - p.s;
                if (d >= fade) continue;
                Fixed k = kOne - d / fade;
                k = k * k * (Fixed::fromInt(3) - k * 2);   // smoothstep
                p.p = p.p + shift * k;
            }
            // recompute the arc lengths and tangents of the shifted points
            Fixed s{};
            for (size_t k = 0; k < g.pts.size(); ++k) {
                if (k) s += (g.pts[k].p - g.pts[k - 1].p).length();
                g.pts[k].s = s;
                const Vec2 a = g.pts[k ? k - 1 : 0].p, b = g.pts[std::min(k + 1, g.pts.size() - 1)].p;
                g.pts[k].t = unit(b - a, g.pts[k].t);
            }
            g.length = s;
        }
    }

    // arms per node
    std::vector<std::vector<Arm>> arms(n.nodes.size());
    for (size_t i = 0; i < n.edges.size(); ++i) {
        const RoadEdge& e = n.edges[i];
        const RoadTypeDef& t = types[size_t(e.typeIndex)];
        const RoadEdgeGeom& g = G.edges[i];
        if (g.pts.size() < 2) continue;
        for (int end = 0; end < 2; ++end) {
            Arm a;
            a.edge = int(i); a.end = int8_t(end);
            a.d = end == 0 ? g.pts.front().t : Vec2{-g.pts.back().t.x, -g.pts.back().t.y};
            a.angle = fxAtan2Turns(a.d.y, a.d.x);
            // leaving through end a the arm's right is the edge's right; through end b it is the edge's left
            const int rightSide = end == 0 ? 1 : -1;
            const int fwdSide = e.reversed ? -1 : 1;   // lanes drawn reversed: the forward lanes lie on the left
            auto cw = [&](int side) { return t.carriageway(side * fwdSide); };
            auto hw = [&](int side) { const RoadSideDef& s = (side * fwdSide) > 0 ? t.right : t.left; return cw(side) + s.shoulder + s.sidewalk; };
            a.cwR = cw(rightSide); a.cwL = cw(-rightSide);
            a.hwR = hw(rightSide); a.hwL = hw(-rightSide);
            a.drivable = !t.forward.empty() || !t.backward.empty();
            arms[size_t(n.nodeIndex(end == 0 ? e.a : e.b))].push_back(a);
        }
    }

    for (size_t ni = 0; ni < n.nodes.size(); ++ni) {
        auto& A = arms[ni];
        // two roads meeting at a sharp bend (over 35 degrees off straight) get a junction box: fillets and kerb
        // corners round the bend instead of two cross-sections overlapping
        const bool sharp = A.size() == 2 && A[0].d.dot(A[1].d) > -Fixed::fromRatio(82, 100);
        if (A.size() < 3 && !sharp) {
            if (A.size() == 2) {   // one road runs on as another: an unpaved one only out of a narrow paved road
                const RoadTypeDef& t0 = types[size_t(n.edges[size_t(A[0].edge)].typeIndex)];
                const RoadTypeDef& t1 = types[size_t(n.edges[size_t(A[1].edge)].typeIndex)];
                for (int k = 0; k < 2; ++k) {
                    const RoadTypeDef& paved = k ? t1 : t0;
                    const RoadTypeDef& rough = k ? t0 : t1;
                    if (paved.unpaved() || !rough.unpaved() || !paved.drivable()) continue;
                    const Arm& pa = A[size_t(k)];
                    if (paved.lanes(1) + paved.lanes(-1) > 2 || paved.category == "highway") {
                        G.issues.push_back("node " + std::to_string(n.nodes[ni].id) + ": a " + rough.id + " cannot run on from the " + paved.id +
                                           " (" + std::to_string(paved.lanes(1) + paved.lanes(-1)) + " lanes): join it at a junction, or run it on from a road of 1-2 lanes");
                        continue;
                    }
                    RoadEdgeGeom& g = G.edges[size_t(pa.edge)];
                    g.wear[size_t(pa.end)] = fxMin(Fixed::fromInt(15) / cellMetres, g.length * Fixed::fromRatio(2, 5));
                    // the asphalt only breaks up on the ground: never on a bridge deck or in a tunnel
                    for (auto& sp : g.spans) {
                        const Fixed room = pa.end == 0 ? sp.s0 : g.length - sp.s1;
                        if (room > Fixed{}) g.wear[size_t(pa.end)] = fxMin(g.wear[size_t(pa.end)], room);
                    }
                    g.wearInto[size_t(pa.end)] = n.edges[size_t(A[size_t(1 - k)].edge)].typeIndex;
                    g.worn[size_t(pa.end)] = true;
                }
                // two roads of different widths, both paved or both not: the wider one narrows to the other over
                // its last stretch
                if (t0.unpaved() == t1.unpaved() && t0.id != t1.id) {
                    const Fixed w0 = t0.carriageway(1) + t0.carriageway(-1), w1 = t1.carriageway(1) + t1.carriageway(-1);
                    if (w0 != w1) {
                        const size_t wide = w0 > w1 ? 0 : 1;
                        RoadEdgeGeom& g = G.edges[size_t(A[wide].edge)];
                        g.wear[size_t(A[wide].end)] = fxMin(Fixed::fromInt(20) / cellMetres, g.length * Fixed::fromRatio(2, 5));
                        g.wearInto[size_t(A[wide].end)] = n.edges[size_t(A[1 - wide].edge)].typeIndex;
                    }
                }
            }
            continue;
        }
        // a node where a branch leaves lanes of a road that runs through: no junction box
        bool split = false;
        for (auto& a : A) if (n.edges[size_t(a.edge)].attach[size_t(a.end)].edge >= 0) split = true;
        std::sort(A.begin(), A.end(), [&](const Arm& x, const Arm& y) { return x.angle != y.angle ? x.angle < y.angle : n.edges[size_t(x.edge)].id < n.edges[size_t(y.edge)].id; });
        RoadJunctionGeom J;
        J.node = n.nodes[ni].id;
        J.centre = n.nodes[ni].p;
        J.y = G.edges[size_t(A[0].edge)].pts[A[0].end == 0 ? 0 : G.edges[size_t(A[0].edge)].pts.size() - 1].y;
        J.split = split;
        int driven = 0;
        for (auto& a : A) { J.arms.push_back(a.edge); J.armEnd.push_back(a.end); driven += a.drivable; }
        J.signal = driven >= 3 && !split;
        if (split) {
            // side gaps: the parent arm the branch runs along loses its curb / sidewalk / props on the branch's side
            for (auto& br : A) {
                const RoadAttach& at = n.edges[size_t(br.edge)].attach[size_t(br.end)];
                if (at.edge < 0) continue;
                const int pi = n.edgeIndex(at.edge);
                // the parent piece (either half at this node) running most like the branch
                const Arm* along = nullptr;
                Fixed best{};
                for (auto& a : A) {
                    if (&a == &br || n.edges[size_t(a.edge)].attach[size_t(a.end)].edge >= 0) continue;
                    const RoadEdge& ae = n.edges[size_t(a.edge)];
                    if (ae.id != at.edge && n.edges[size_t(pi)].type != ae.type) continue;
                    const Fixed d = a.d.dot(br.d);
                    if (!along || d > best) { best = d; along = &a; }
                }
                if (!along) continue;
                RoadEdgeGeom& pg = G.edges[size_t(along->edge)];
                const RoadEdgeGeom& bg = G.edges[size_t(br.edge)];
                const RoadTypeDef& bt = types[size_t(n.edges[size_t(br.edge)].typeIndex)];
                const Fixed bHalf = fxMax(bt.carriageway(1), bt.carriageway(-1));
                // walk the branch away from the node until it clears the parent's carriageway
                Fixed reach{};
                int side = 0;
                for (size_t k = 0; k < bg.pts.size(); ++k) {
                    const RoadSample& bs = br.end == 0 ? bg.pts[k] : bg.pts[bg.pts.size() - 1 - k];
                    Fixed s, lat;
                    roadProject(pg, bs.p, s, lat);
                    if (!side && fxAbs(lat) > kTiny) side = lat > Fixed{} ? 1 : -1;
                    const Fixed edge = side > 0 ? along->end == 0 ? along->cwR : along->cwL : along->end == 0 ? along->cwL : along->cwR;
                    reach = along->end == 0 ? s : pg.length - s;
                    if (fxAbs(lat) - bHalf > edge) break;
                }
                if (!side) continue;
                const int edgeSide = side > 0 ? 1 : 0;
                const Fixed s0 = along->end == 0 ? Fixed{} : pg.length - reach, s1 = along->end == 0 ? reach : pg.length;
                pg.gaps[size_t(edgeSide)].push_back({s0, s1});
                // the branch's own side toward the parent is inside the parent road for the same stretch
                RoadEdgeGeom& bgm = G.edges[size_t(br.edge)];
                Fixed bs0{}, bs1{};
                if (br.end == 0) { bs0 = {}; bs1 = fxMin(reach + kOne, bgm.length); } else { bs0 = fxMax(bgm.length - reach - kOne, Fixed{}); bs1 = bgm.length; }
                // which side of the branch faces the parent: the parent lies opposite to the branch's offset
                const Fixed latB = (G.edges[size_t(br.edge)].lateral[size_t(br.end)]).dot(roadNormal(br.end == 0 ? bgm.pts.front().t : bgm.pts.back().t));
                bgm.gaps[size_t(latB > Fixed{} ? 0 : 1)].push_back({bs0, bs1});
            }
            G.nodeJunction[ni] = int32_t(G.junctions.size());
            G.junctions.push_back(std::move(J));
            continue;
        }
        // trims: each arm is cut back past the point where its carriageway edge meets the neighbour's, plus the corner
        const size_t m = A.size();
        std::vector<Fixed> trim(m, Fixed{});
        std::vector<Fixed> radius(m, Fixed{});
        for (size_t i = 0; i < m; ++i) {
            const Arm& a = A[i];
            const Arm& b = A[(i + 1) % m];
            const RoadSideDef& sa = types[size_t(n.edges[size_t(a.edge)].typeIndex)].right;
            const Fixed R = fxMax(fxMax(a.hwR - a.cwR, b.hwL - b.cwL), kOne) + kHalf;   // corner radius: the sidewalk plus a margin
            radius[i] = R;
            (void)sa;
            // a dirt or gravel road running into a paved one: no kerb fillet flaring out round it; the paved road's
            // sidewalk runs on to the dirt road's edge and ends square there, the dirt road starts behind it
            const bool ua = types[size_t(n.edges[size_t(a.edge)].typeIndex)].unpaved(), ub = types[size_t(n.edges[size_t(b.edge)].typeIndex)].unpaved();
            Fixed Ra = R, Rb = R;
            if (ua != ub && a.drivable && b.drivable) {
                const Fixed tight = Fixed::fromRatio(1, 4) / cellMetres;
                if (ua) { Ra = fxMax(b.hwL - b.cwL, tight); Rb = tight; }
                else { Rb = fxMax(a.hwR - a.cwR, tight); Ra = tight; }
            }
            Fixed u, v;
            const Vec2 pa = J.centre + roadNormal(a.d) * a.cwR, pb = J.centre - roadNormal(b.d) * b.cwL;
            if (intersect(pa, a.d, pb, b.d, u, v)) {
                if (u > Fixed{} && v > Fixed{} && u < Fixed::fromInt(60) && v < Fixed::fromInt(60)) {
                    trim[i] = fxMax(trim[i], u + Ra);
                    trim[(i + 1) % m] = fxMax(trim[(i + 1) % m], v + Rb);
                }
            }
        }
        for (size_t i = 0; i < m; ++i) {
            const RoadEdgeGeom& g = G.edges[size_t(A[i].edge)];
            Fixed widest{};
            for (size_t k = 0; k < m; ++k) if (k != i && A[k].drivable) widest = fxMax(widest, fxMax(A[k].cwL, A[k].cwR));
            if (!A[i].drivable) trim[i] = fxMax(widest, Fixed{}) + fxMax(A[i].hwL, A[i].hwR) / 4;   // paths stop at the road's edge
            trim[i] = fxMax(trim[i], widest * Fixed::fromRatio(3, 4));
            // half the edge when there is a junction at its other end too; most of it when the other end is free
            const RoadEdge& ae = n.edges[size_t(A[i].edge)];
            const int other = n.nodeIndex(A[i].end == 0 ? ae.b : ae.a);
            const Fixed limit = g.length * (other >= 0 && arms[size_t(other)].size() >= 3 ? Fixed::fromRatio(9, 20) : Fixed::fromRatio(9, 10));
            if (trim[i] > limit) { G.issues.push_back("node " + std::to_string(J.node) + ": edge " + std::to_string(n.edges[size_t(A[i].edge)].id) + " is too short for the junction"); trim[i] = limit; }
            RoadEdgeGeom& gm = G.edges[size_t(A[i].edge)];
            (A[i].end == 0 ? gm.trim0 : gm.trim1) = trim[i];
        }
        if (driven > 4) G.issues.push_back("node " + std::to_string(J.node) + ": " + std::to_string(driven) + " roads meet; traffic uses four arms at most");
        // outline: per arm its left and right carriageway corner at the trim, fillets between neighbours
        auto armPt = [&](size_t i, Fixed lat) {
            const RoadEdgeGeom& g = G.edges[size_t(A[i].edge)];
            const RoadSample r = roadSampleAt(g, A[i].end == 0 ? trim[i] : g.length - trim[i]);
            const Vec2 d = A[i].end == 0 ? r.t : Vec2{-r.t.x, -r.t.y};
            return std::pair<Vec2, Vec2>{r.p + roadNormal(d) * lat, d};
        };
        for (size_t i = 0; i < m; ++i) {
            if (!A[i].drivable) continue;
            const auto [L, dl] = armPt(i, -A[i].cwL);
            const auto [Rr, dr] = armPt(i, A[i].cwR);
            (void)dl;
            J.outline.push_back(L);
            J.outline.push_back(Rr);
            // next drivable arm
            size_t k = (i + 1) % m;
            while (!A[k].drivable && k != i) k = (k + 1) % m;
            if (k == i) break;
            const auto [Ln, dn] = armPt(k, -A[k].cwL);
            Fixed u, v;
            if (intersect(Rr, Vec2{-dr.x, -dr.y}, Ln, Vec2{-dn.x, -dn.y}, u, v) && u > Fixed{} && v > Fixed{}) {
                const Vec2 C = Rr - dr * u;
                quadCurve(Rr, C, Ln, 6, J.outline);
                J.outline.pop_back();   // Ln is pushed by the next arm
            }
            // sidewalk corner: inner curve = the carriageway corner, outer = the same at the outer edge
            const auto [Ro, dro] = armPt(i, A[i].hwR);
            const auto [Lo, dlo] = armPt(k, -A[k].hwL);
            std::vector<Vec2> inner{Rr}, outer{Ro};
            Fixed u2, v2;
            if (intersect(Rr, Vec2{-dr.x, -dr.y}, Ln, Vec2{-dn.x, -dn.y}, u, v) && u > Fixed{} && v > Fixed{}) quadCurve(Rr, Rr - dr * u, Ln, 8, inner);
            else for (int q = 1; q <= 8; ++q) inner.push_back(lerp(Rr, Ln, Fixed::fromRatio(q, 8)));
            // the outer edge (where the plots behind the sidewalk begin) keeps a square corner: the plots stay
            // rectangles, only the kerb is rounded
            // (also when the two outer lines meet behind the arm ends: the arms' own sidewalks then end short of
            // the corner and the strip fills it out to the square)
            if (intersect(Ro, Vec2{-dro.x, -dro.y}, Lo, Vec2{-dlo.x, -dlo.y}, u2, v2) && fxAbs(u2) < Fixed::fromInt(6) && fxAbs(v2) < Fixed::fromInt(6)) {
                const Vec2 C = Ro - dro * u2;
                for (int q = 1; q <= 4; ++q) outer.push_back(lerp(Ro, C, Fixed::fromRatio(q, 4)));
                for (int q = 1; q <= 4; ++q) outer.push_back(lerp(C, Lo, Fixed::fromRatio(q, 4)));
            } else for (int q = 1; q <= 8; ++q) outer.push_back(lerp(Ro, Lo, Fixed::fromRatio(q, 8)));
            std::vector<Vec2> strip;
            for (size_t q = 0; q < inner.size(); ++q) { strip.push_back(inner[q]); strip.push_back(outer[q]); }
            J.corners.push_back(std::move(strip));
            J.cornerArms.push_back({int(i), int(k)});
        }
        G.nodeJunction[ni] = int32_t(G.junctions.size());
        G.junctions.push_back(std::move(J));
    }
    // a branch's carriageway cuts the curbs, sidewalks, barriers and props of whatever it crosses near where it
    // leaves: the parent's sides, the next edge's, the corners of a junction close by
    for (size_t bi = 0; bi < n.edges.size(); ++bi) {
        const RoadEdge& B = n.edges[bi];
        if (B.attach[0].edge < 0 && B.attach[1].edge < 0) continue;
        const RoadTypeDef& bt = types[size_t(B.typeIndex)];
        const int bfs = B.reversed ? -1 : 1;
        std::vector<std::vector<Vec2>> polys;
        roadStripPolygons(G.edges[bi], Fixed{}, G.edges[bi].length, -bt.carriageway(-bfs), bt.carriageway(bfs), polys);
        for (auto& p : polys) { G.cuts.push_back(p); G.cutEdge.push_back(int(bi)); }
        // the parent's carriageway near the node cuts the branch's own inner side
        for (int end = 0; end < 2; ++end) {
            if (B.attach[size_t(end)].edge < 0) continue;
            const int32_t node = end == 0 ? B.a : B.b;
            for (size_t pi = 0; pi < n.edges.size(); ++pi) {
                const RoadEdge& P = n.edges[pi];
                if (pi == bi || (P.a != node && P.b != node)) continue;
                const RoadTypeDef& pt = types[size_t(P.typeIndex)];
                const int pfs = P.reversed ? -1 : 1;
                const Fixed reach = fxMin(G.edges[pi].length, G.edges[bi].length + Fixed::fromInt(4));
                std::vector<std::vector<Vec2>> pp;
                if (P.a == node) roadStripPolygons(G.edges[pi], Fixed{}, reach, -pt.carriageway(-pfs), pt.carriageway(pfs), pp);
                else roadStripPolygons(G.edges[pi], G.edges[pi].length - reach, G.edges[pi].length, -pt.carriageway(-pfs), pt.carriageway(pfs), pp);
                for (auto& p : pp) { G.cuts.push_back(p); G.cutEdge.push_back(int(pi)); }
            }
        }
        Fixed x0 = Fixed::fromInt(1 << 20), x1 = -x0, z0 = x0, z1 = -x0;
        for (auto& p : polys) for (auto& v : p) { x0 = fxMin(x0, v.x); x1 = fxMax(x1, v.x); z0 = fxMin(z0, v.y); z1 = fxMax(z1, v.y); }
        auto inside = [&](Vec2 q) {
            if (q.x < x0 || q.x > x1 || q.y < z0 || q.y > z1) return false;
            for (auto& p : polys) {   // convex quads: same side of every edge
                int pos = 0, neg = 0;
                for (size_t k = 0; k < p.size(); ++k) { const Fixed c = cross(p[(k + 1) % p.size()] - p[k], q - p[k]); pos += c.raw > 0; neg += c.raw < 0; }
                if (!pos || !neg) return true;
            }
            return false;
        };
        for (size_t ei = 0; ei < n.edges.size(); ++ei) {
            if (ei == bi) continue;
            const RoadTypeDef& t = types[size_t(n.edges[ei].typeIndex)];
            const int fs = n.edges[ei].reversed ? -1 : 1;
            RoadEdgeGeom& g = G.edges[ei];
            for (int side = 0; side < 2; ++side) {
                const int sd = side ? 1 : -1;   // edge frame: -1 left, 1 right
                const RoadSideDef& S = (sd * fs) > 0 ? t.right : t.left;
                const Fixed inner = t.carriageway(sd * fs), outer = inner + S.shoulder + S.sidewalk;
                if (outer <= inner) continue;
                Fixed from = -kOne;
                const Fixed step = Fixed::fromRatio(1, 4);
                for (Fixed s = g.trim0; ; s += step) {
                    const bool last = s >= g.length - g.trim1;
                    const Fixed sc = last ? g.length - g.trim1 : s;
                    const RoadSample r = roadSampleAt(g, sc);
                    const Vec2 nrm = roadNormal(r.t) * Fixed::fromInt(sd);
                    const bool hit = inside(r.p + nrm * ((inner + outer) / 2)) || inside(r.p + nrm * (inner + kTiny * 8)) || inside(r.p + nrm * outer);
                    if (hit && from < Fixed{}) from = fxMax(sc - step, Fixed{});
                    if ((!hit || last) && from >= Fixed{}) { g.gaps[size_t(side)].push_back({from, fxMin(sc + step, g.length)}); from = -kOne; }
                    if (last) break;
                }
            }
        }
        for (auto& J : G.junctions)
            for (auto& c : J.corners) {
                bool hit = false;
                for (auto& v : c) hit = hit || inside(v);
                if (hit) c.clear();   // the corner is under the branch: no curb there
            }
    }
    // an arm meets its junction box at the box's height: under the box and over the first metres past it the
    // road is held at the node's height and only then rises or falls into its own profile (a bridge or a ramp
    // that starts at a junction otherwise leaves its deck end standing over the box with a gap under it)
    for (auto& J : G.junctions) {
        if (J.split) continue;
        for (size_t k = 0; k < J.arms.size(); ++k) {
            RoadEdgeGeom& g = G.edges[size_t(J.arms[k])];
            if (g.pts.size() < 2) continue;
            const int end = J.armEnd[k];
            const RoadSample& at = end == 0 ? g.pts.front() : g.pts.back();
            const Fixed y0 = at.y, e0 = at.e;
            const Fixed trim = end == 0 ? g.trim0 : g.trim1;
            // the run-out: long enough for an easy grade (6 %) to the highest point the road reaches, at least
            // 12 m, at most a little under half the edge (its other end may hold to a junction too)
            Fixed rise{};
            for (auto& r : g.pts) rise = fxMax(rise, fxAbs(r.y - y0));
            Fixed L = fxMax(rise / Fixed::fromRatio(6, 100), Fixed::fromInt(12)) / cellMetres;
            L = fxMax(fxMin(L, (g.length - trim) * Fixed::fromRatio(45, 100)), Fixed{});
            for (auto& r : g.pts) {
                const Fixed d = end == 0 ? r.s : g.length - r.s;
                if (d >= trim + L) continue;
                Fixed u = L > Fixed{} ? (d - trim) / L : kOne;
                if (u < Fixed{}) u = Fixed{};
                const Fixed w = u * u * (Fixed::fromInt(3) - u * 2);   // 0 at the box .. 1 on the road's own profile
                r.y = y0 + (r.y - y0) * w;
                r.e = e0 + (r.e - e0) * w;
            }
        }
    }
    for (auto& g : G.edges) spansOf(g);
    // bus stop bays in cells
    for (size_t i = 0; i < n.edges.size(); ++i) {
        const RoadEdge& e = n.edges[i];
        RoadEdgeGeom& g = G.edges[i];
        const RoadTypeDef& t = types[size_t(e.typeIndex)];
        const int fs = e.reversed ? -1 : 1;
        for (auto& b : e.bays) {
            RoadBayGeom bg;
            bg.side = b.side;
            const auto& lanes = t.lanesOf(b.side * fs);
            bg.width = b.width.raw ? b.width / cellMetres : (lanes.empty() ? Fixed::fromInt(3) / cellMetres : lanes.back().width);
            bg.shelter = b.shelter;
            bg.parking = b.parking;
            bg.stall = b.stall / cellMetres;
            if (b.parking && !lanes.empty() && lanes.back().use == RoadLaneUse::Parking) {   // marked in the parking lane
                bg.inLane = true; bg.laneWidth = lanes.back().width; bg.width = Fixed{};
            } else if (b.inside) {   // marked inside the kerb lane, as wide as asked
                bg.inLane = true; bg.laneWidth = b.width / cellMetres; bg.width = Fixed{};
            }
            const Fixed mid = g.length * b.at, half = b.length / cellMetres / 2, tp = b.taper / cellMetres;
            bg.s1 = fxMax(mid - half, g.trim0 + tp); bg.s2 = fxMin(mid + half, g.length - g.trim1 - tp);
            bg.s0 = bg.s1 - tp; bg.s3 = bg.s2 + tp;
            if (bg.inLane) { bg.s0 = bg.s1 = fxMax(mid - half, g.trim0); bg.s2 = bg.s3 = fxMin(mid + half, g.length - g.trim1); }
            if (bg.s2 <= bg.s1) { G.issues.push_back("edge " + std::to_string(e.id) + (b.parking ? ": too short for its parking zone" : ": too short for its bus stop bay")); continue; }
            g.bays.push_back(bg);
        }
        for (auto& d : e.driveways) {
            RoadDrivewayGeom dg;
            dg.side = d.side;
            dg.half = d.width / cellMetres / 2;
            dg.flare = Fixed::fromRatio(3, 2) / cellMetres;
            dg.depth = d.depth / cellMetres;
            const Fixed lo = g.trim0 + dg.half + dg.flare, hi = g.length - g.trim1 - dg.half - dg.flare;
            if (hi < lo) { G.issues.push_back("edge " + std::to_string(e.id) + ": too short for its driveway"); continue; }
            dg.s = fxClamp(g.length * d.at, lo, hi);
            g.driveways.push_back(dg);
        }
    }
    // too steep for a road: say where (the editor shows it; a bridge needs a longer run for its height)
    for (size_t i = 0; i < G.edges.size(); ++i) {
        const auto& P = G.edges[i].pts;
        Fixed worst{};
        for (size_t k = 1; k < P.size(); ++k) {
            const Fixed run = (P[k].s - P[k - 1].s) * cellMetres;
            if (run.raw <= 0) continue;
            worst = fxMax(worst, fxAbs(P[k].y - P[k - 1].y) / run);
        }
        if (worst > Fixed::fromRatio(12, 100))
            G.issues.push_back("edge " + std::to_string(n.edges[i].id) + ": slope " + std::to_string(int((worst * 100).toInt())) + "% (more than 12%): lengthen the run or lower the height");
    }
    for (size_t i = 0; i < n.edges.size(); ++i)
        if (G.edges[i].trim0 + G.edges[i].trim1 >= G.edges[i].length && G.edges[i].length.raw > 0)
            G.issues.push_back("edge " + std::to_string(n.edges[i].id) + ": shorter than its two junctions");
    return G;
}

std::vector<uint8_t> rasterizeRoads(const RoadNetwork& n, const std::vector<RoadTypeDef>& types, const RoadGeometry& G, int w, int h) {
    std::vector<uint8_t> out(size_t(std::max(0, w)) * size_t(std::max(0, h)), 0);
    if (out.empty()) return out;
    std::vector<std::vector<Vec2>> polys;
    auto onGround = [&](const RoadEdgeGeom& g, Fixed s0, Fixed s1, std::vector<std::pair<Fixed, Fixed>>& parts) {
        Fixed cur = s0;
        for (auto& sp : g.spans) {
            if (sp.s1 <= cur || sp.s0 >= s1) continue;
            if (sp.s0 > cur) parts.push_back({cur, sp.s0});
            cur = fxMax(cur, sp.s1);
        }
        if (cur < s1) parts.push_back({cur, s1});
    };
    for (int pass = 0; pass < 2; ++pass)   // sidewalks first, carriageways over them
        for (size_t i = 0; i < n.edges.size(); ++i) {
            const RoadTypeDef& t = types[size_t(n.edges[i].typeIndex)];
            const RoadEdgeGeom& g = G.edges[i];
            const int fs = n.edges[i].reversed ? -1 : 1;
            std::vector<std::pair<Fixed, Fixed>> parts;
            onGround(g, g.trim0, g.length - g.trim1, parts);
            for (auto& [s0, s1] : parts) {
                polys.clear();
                if (pass == 0) roadStripPolygons(g, s0, s1, -t.halfWidth(-fs), t.halfWidth(fs), polys);
                else roadStripPolygons(g, s0, s1, -t.carriageway(-fs), t.carriageway(fs), polys);
                const uint8_t v = pass == 0 ? 2 : (t.drivable() ? 1 : 2);
                for (auto& p : polys) roadFillPolygon(p, w, h, [&](int c) { if (pass == 0 && out[size_t(c)]) return; out[size_t(c)] = v; });
            }
        }
    for (auto& J : G.junctions) {
        for (auto& c : J.corners)
            for (size_t q = 0; q + 3 < c.size(); q += 2) roadFillPolygon({c[q], c[q + 2], c[q + 3], c[q + 1]}, w, h, [&](int k) { if (!out[size_t(k)]) out[size_t(k)] = 2; });
        roadFillPolygon(J.outline, w, h, [&](int k) { out[size_t(k)] = 3; });
    }
    return out;
}

Json roadDecks(const RoadNetwork& n, const std::vector<RoadTypeDef>& types, const RoadGeometry& G) {
    Json list = Json::array();
    for (size_t i = 0; i < n.edges.size(); ++i) {
        const RoadEdgeGeom& g = G.edges[i];
        const RoadTypeDef& t = types[size_t(n.edges[i].typeIndex)];
        int k = 0;
        for (auto& sp : g.spans) {
            // the deck reaches a little past the span onto the ground, so cars drive up onto it
            const Fixed s0 = fxMax(sp.s0 - Fixed::fromInt(2), Fixed{}), s1 = fxMin(sp.s1 + Fixed::fromInt(2), g.length);
            Json d = Json::object();
            d.set("id", Json::string("road_" + std::to_string(n.edges[i].id) + "_" + std::to_string(k++)));
            d.set("width", Json::number((t.carriageway(1) + t.carriageway(-1)) + Fixed::fromRatio(1, 2)));
            Json path = Json::array();
            Fixed last = Fixed::fromInt(-1000);
            auto add = [&](const RoadSample& r) {
                if (r.s - last < Fixed::fromRatio(1, 4)) return;
                last = r.s;
                Json p = Json::array();
                p.push(Json::number(r.p.x)); p.push(Json::number(r.p.y)); p.push(Json::number(r.e));
                path.push(p);
            };
            add(roadSampleAt(g, s0));
            for (auto& p : g.pts) if (p.s > s0 && p.s < s1) add(p);
            add(roadSampleAt(g, s1));
            if (path.size() < 2) continue;
            d.set("path", path);
            if (sp.tunnel) d.set("tunnel", Json::fromBool(true));
            list.push(d);
        }
    }
    return list;
}

} // namespace aiverse::roads
