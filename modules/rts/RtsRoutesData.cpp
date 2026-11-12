// Route geometry and data parsing (RtsRoutesData.h). A 1:1 port of Bunker Wars Spline / SubSpline / Interpolate
// in fixed point: same node counts, same offsets, same trimming quirks, so the lines units walk match BW.
#include "RtsRoutesData.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {

namespace {
[[noreturn]] void fail(int index, const std::string& msg) { throw std::runtime_error("map route " + std::to_string(index) + ": " + msg); }

bool readVec2(const Json& j, Vec2& out) {
    if (!j.isArray() || j.size() < 2 || !j[size_t(0)].isNumber() || !j[size_t(1)].isNumber()) return false;
    out = {j[size_t(0)].fx(), j[size_t(1)].fx()};
    return true;
}
Json vec2Json(Vec2 v) { Json a = Json::array(); a.push(Json::number(v.x)); a.push(Json::number(v.y)); return a; }

Vec2 normalized(Vec2 v) {
    const Fixed l = v.length();
    return l.raw > 0 ? v / l : Vec2{};
}

// BW Interpolate.CatmullRom (GemsV1 p.266): t = step / stepCount
Vec2 catmullRom(Vec2 p0, Vec2 p1, Vec2 p2, Vec2 p3, int step, int steps) {
    const Fixed t = Fixed::fromInt(step) / steps, t2 = t * t, t3 = t2 * t;
    const Fixed h = Fixed::fromRatio(1, 2), a15 = Fixed::fromRatio(3, 2), a25 = Fixed::fromRatio(5, 2);
    const Fixed c0 = -h * t3 + t2 - h * t;
    const Fixed c1 = a15 * t3 - a25 * t2 + Fixed::fromInt(1);
    const Fixed c2 = -a15 * t3 + t2 * 2 + h * t;
    const Fixed c3 = h * t3 - h * t2;
    return p0 * c0 + p1 * c1 + p2 * c2 + p3 * c3;
}

// BW Interpolate.NewCatmullRom(nodes, slices, loop = false)
std::vector<Vec2> newCatmullRom(const std::vector<Vec2>& nodes, int slices) {
    std::vector<Vec2> out;
    if (nodes.size() < 2) return out;
    out.push_back(nodes[0]);
    const size_t last = nodes.size() - 1;
    const int steps = slices + 1;
    for (size_t cur = 0; cur < last; ++cur) {
        const size_t prev = cur == 0 ? cur : cur - 1, end = cur + 1, next = end == last ? end : end + 1;
        for (int s = 1; s <= steps; ++s) out.push_back(catmullRom(nodes[prev], nodes[cur], nodes[end], nodes[next], s, steps));
    }
    return out;
}

Vec2 moveTowards(Vec2 from, Vec2 to, Fixed step) {
    const Vec2 d = to - from;
    const Fixed l = d.length();
    if (l <= step || l.raw == 0) return to;
    return from + d * (step / l);
}

// BW SubSpline.Recalculate end trimming, quirks included: at the start the moved node survives only when nodes
// within `off` were dropped; at the end the moved node is always dropped again.
void trimEnds(std::vector<Vec2>& list, Fixed off) {
    if (list.size() < 2 || (list.front() - list.back()).length() <= off * 4) return;
    const Fixed sq = off * off;
    {
        const Vec2 first = list[0];
        size_t last = 0;
        for (size_t i = 1; i < list.size(); ++i) {
            const Vec2 next = list[i];
            if ((next - first).lengthSq() <= sq) { last = i; list.erase(list.begin() + ptrdiff_t(i)); --i; }
            else { list[last] = moveTowards(first, next, off); break; }
        }
        list.erase(list.begin());
    }
    if (list.empty()) return;
    {
        const Vec2 first = list.back();
        size_t last = list.size() - 1;
        for (size_t i = list.size() - 1; i-- > 0;) {
            const Vec2 next = list[i];
            if ((next - first).lengthSq() <= sq) { last = i; list.erase(list.begin() + ptrdiff_t(i)); }
            else { list[last] = moveTowards(first, next, off); break; }
        }
        list.pop_back();
    }
}

// BW Spline.GetOffset. Map z = -BW y, so BW's right of a forward (fx, fy), (fy, -fx), is (-dz, dx) here.
Vec2 columnOffset(Vec2 cur, Vec2 prev, Vec2 next, Fixed value, int line, Fixed colWidth) {
    auto right = [](Vec2 f) { return normalized(Vec2{-f.y, f.x}); };
    const Vec2 r1 = right(cur - prev), r2 = right(next - cur);
    Vec2 mid = normalized(r1 + r2);          // Quaternion.Lerp(rotPrev, rotNext, 0.5) * right
    if (mid.x.raw == 0 && mid.y.raw == 0) mid = r1;
    Fixed index = Fixed::fromRatio(1, 2);
    if (line > 1) index = line % 2 == 0 ? Fixed::fromRatio(1, 2) + Fixed::fromRatio(line, 2) : Fixed::fromRatio(line, 2);
    Vec2 off = mid * (colWidth * index) + mid * value;
    if (line % 2 == 0) off = Vec2{} - off;
    return off;
}
} // namespace

void loadRouteRules(const Json& lanes, RouteRules& r) {
    if (const Json& j = lanes["routes"]; !j.isNull()) {
        if (!j.isObject()) throw std::runtime_error("rules lanes.routes: expected an object");
        r.lines = std::clamp<int>(int(j.getInt("lines", r.lines)), 1, 8);
        r.columnPadding = j.getFx("columnPadding", r.columnPadding);
        r.flyColumnPadding = j.getFx("flyColumnPadding", r.flyColumnPadding);
        r.trim = fxMax(Fixed{}, j.getFx("trim", r.trim));
        r.smooth = std::clamp<int>(int(j.getInt("smooth", r.smooth)), 0, 64);
        r.inOut = fxMax(Fixed{}, j.getFx("inOut", r.inOut));
    }
    if (const Json& j = lanes["groups"]; !j.isNull()) {
        const bool isBool = j.type() == Json::Type::Bool;
        if (!isBool && !j.isObject()) throw std::runtime_error("rules lanes.groups: expected an object or true");
        r.groups = j.isObject() ? j.getBool("enabled", true) : j.boolean(false);
        if (j.isObject()) {
            r.rowSpacing = j.getFx("rowSpacing", r.rowSpacing);
            r.flyRowSpacing = j.getFx("flyRowSpacing", r.flyRowSpacing);
            if (r.rowSpacing.raw <= 0 || r.flyRowSpacing.raw <= 0) throw std::runtime_error("rules lanes.groups: rowSpacing must be > 0");
        }
    }
}

RouteDef parseRoute(const Json& j, int index) {
    RouteDef r;
    if (!j.isObject()) fail(index, "expected an object");
    r.from = j.getStr("from");
    r.to = j.getStr("to");
    if (r.from.empty() || r.to.empty()) fail(index, "needs \"from\" and \"to\" (entity ids)");
    if (r.from == r.to) fail(index, "\"from\" and \"to\" are the same entity");
    const Json& pts = j["points"];
    for (size_t i = 0; i < pts.size(); ++i) {
        const Json& p = pts[i];
        Vec2 v;
        std::array<Fixed, 8> off{};
        uint32_t zone = 0;
        Fixed slow{};
        if (p.isObject()) {
            if (!readVec2(p["at"], v)) fail(index, "point " + std::to_string(i) + ": \"at\" must be [x, z]");
            const Json& o = p["offsets"];
            if (!o.isNull() && !o.isArray()) fail(index, "point " + std::to_string(i) + ": \"offsets\" must be an array of numbers");
            for (size_t k = 0; k < o.size() && k < 8; ++k) off[k] = o[k].fx();
            zone = uint32_t(std::max<int64_t>(0, p.getInt("zone", 0)));
            slow = p.getFx("slowdown");
        } else if (!readVec2(p, v)) fail(index, "point " + std::to_string(i) + ": expected [x, z] or {\"at\": [x, z], ...}");
        r.points.push_back(v);
        r.offsets.push_back(off);
        r.zones.push_back(zone);
        r.slowdown.push_back(slow);
    }
    if (r.points.size() < 2) fail(index, "needs at least 2 points");
    for (size_t k = 0; k < j["lines"].size(); ++k) {
        const Json& ln = j["lines"][k];
        std::vector<Vec2> nodes;
        for (size_t i = 0; i < ln.size(); ++i) {
            Vec2 v;
            if (!readVec2(ln[i], v)) fail(index, "line " + std::to_string(k) + " node " + std::to_string(i) + ": expected [x, z]");
            nodes.push_back(v);
        }
        r.lines.push_back(std::move(nodes));
    }
    for (auto& b : j["blockers"].items()) r.blockers.push_back(b.str());
    r.flying = j.getBool("flying", false);
    return r;
}

Json routeToJson(const RouteDef& r) {
    Json j = Json::object();
    j.set("from", Json::string(r.from));
    j.set("to", Json::string(r.to));
    Json pts = Json::array();
    for (size_t i = 0; i < r.points.size(); ++i) {
        const bool plain = (i >= r.offsets.size() || std::all_of(r.offsets[i].begin(), r.offsets[i].end(), [](Fixed f) { return f.raw == 0; })) &&
                           (i >= r.zones.size() || r.zones[i] == 0) && (i >= r.slowdown.size() || r.slowdown[i].raw == 0);
        if (plain) { pts.push(vec2Json(r.points[i])); continue; }
        Json p = Json::object();
        p.set("at", vec2Json(r.points[i]));
        Json o = Json::array();
        for (Fixed f : r.offsets[i]) o.push(Json::number(f));
        p.set("offsets", o);
        if (r.zones[i]) p.set("zone", Json::number(int64_t(r.zones[i])));
        if (r.slowdown[i].raw) p.set("slowdown", Json::number(r.slowdown[i]));
        pts.push(p);
    }
    j.set("points", pts);
    if (!r.lines.empty()) {
        Json ls = Json::array();
        for (auto& l : r.lines) { Json a = Json::array(); for (auto v : l) a.push(vec2Json(v)); ls.push(a); }
        j.set("lines", ls);
    }
    if (!r.blockers.empty()) { Json b = Json::array(); for (auto& s : r.blockers) b.push(Json::string(s)); j.set("blockers", b); }
    if (r.flying) j.set("flying", Json::fromBool(true));
    return j;
}

std::vector<std::vector<Vec2>> buildRouteLines(const RouteDef& r, const RouteRules& rr) {
    std::vector<std::vector<Vec2>> out;
    const size_t n = r.points.size();
    const Fixed colWidth = r.flying ? rr.flyColumnPadding : rr.columnPadding;
    for (int k = 0; k < rr.lines; ++k) {
        std::vector<Vec2> ctrl;
        if (size_t(k) < r.lines.size() && r.lines[size_t(k)].size() > 1) ctrl = r.lines[size_t(k)];
        else {
            ctrl = r.points;
            for (size_t j = 1; j + 1 < n; ++j) {
                const Fixed v = j < r.offsets.size() ? r.offsets[j][size_t(k)] : Fixed{};
                ctrl[j] = ctrl[j] + columnOffset(r.points[j], r.points[j - 1], r.points[j + 1], v, k, colWidth);
            }
        }
        std::vector<Vec2> nodes = newCatmullRom(ctrl, rr.smooth);
        trimEnds(nodes, rr.trim);
        if (nodes.size() < 2) nodes = {ctrl.front(), ctrl.back()};
        out.push_back(std::move(nodes));
    }
    return out;
}

Fixed polylineLength(const std::vector<Vec2>& pts) {
    Fixed l{};
    for (size_t i = 1; i < pts.size(); ++i) l += (pts[i] - pts[i - 1]).length();
    return l;
}

Fixed routeAiLength(const RouteDef& r) {
    Fixed l{};
    for (size_t i = 1; i < r.points.size(); ++i) {
        Fixed d = (r.points[i] - r.points[i - 1]).length();
        const uint32_t z = (i < r.zones.size() ? r.zones[i] : 0) & (r.zones.size() > i - 1 ? r.zones[i - 1] : 0);
        if (z & 2u) {
            const Fixed s = fxMax(r.slowdown[i - 1], r.slowdown[i]);
            if (s.raw > 0) d = d * s;
        }
        l += d;
    }
    return l;
}

RouteDef flyingRoute(Vec2 from, Vec2 to, const RouteRules& rr) {
    RouteDef r;
    r.flying = true;
    const Vec2 d = to - from;
    const Fixed len = d.length();
    const Vec2 dir = normalized(d);
    const Vec2 p2 = from + dir * rr.inOut, p3 = from + dir * (len - rr.inOut);
    r.points = {from, p2, (p2 + p3) / Fixed::fromInt(2), p3, to};
    r.offsets.assign(r.points.size(), {});
    r.zones.assign(r.points.size(), 0);
    r.slowdown.assign(r.points.size(), Fixed{});
    return r;
}

ScreenDef parseScreen(const Json& j, int mapW, int mapH) {
    ScreenDef s;
    if (j.isNull()) return s;
    if (!j.isObject()) throw std::runtime_error("map screen: expected an object");
    if (!readVec2(j["size"], s.size) || s.size.x.raw <= 0 || s.size.y.raw <= 0) throw std::runtime_error("map screen: \"size\" must be [w, h] > 0");
    s.center = {Fixed::fromInt(mapW) / 2, Fixed::fromInt(mapH) / 2};
    if (j.has("center") && !readVec2(j["center"], s.center)) throw std::runtime_error("map screen: \"center\" must be [x, z]");
    if (const Json& b = j["bounds"]; b.isArray()) {
        if (b.size() != 4) throw std::runtime_error("map screen: \"bounds\" must be [left, right, top, bottom]");
        s.left = b[size_t(0)].fx(); s.right = b[size_t(1)].fx(); s.top = b[size_t(2)].fx(); s.bottom = b[size_t(3)].fx();
    } else if (b.isObject()) {
        s.left = b.getFx("left"); s.right = b.getFx("right"); s.top = b.getFx("top"); s.bottom = b.getFx("bottom");
    }
    s.smooth = fxMax(Fixed{}, j.getFx("smooth", s.smooth));
    s.enabled = true;
    return s;
}

bool screenGroupsMatch(const std::vector<int>& a, const std::vector<int>& b) {
    if (a.empty() && b.empty()) return true;
    if (a.empty() || b.empty()) return false;
    for (int x : a) if (std::find(b.begin(), b.end(), x) != b.end()) return true;
    return false;
}

} // namespace aiverse::game
