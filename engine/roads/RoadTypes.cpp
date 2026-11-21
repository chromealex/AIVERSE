// Road types: the built-in library and rules "roadTypes" (docs/roads.md § Road types). Widths in the data are metres,
// turned into cells with "cellMeters" (rules roadTypes.cellMeters, default 4); heights stay metres.
#include "RoadNet.h"
#include <algorithm>
#include <map>
#include <stdexcept>

namespace aiverse::roads {

namespace {

// Widths in metres. Speeds in km/h. Props: spacing / offset in metres.
const char* kBuiltin = R"JSON({
  "_city": {"abstract": true, "category": "city", "surface": "asphalt", "speed": 50,
            "lanes": {"width": 3.5}, "sides": {"curb": "low", "sidewalk": 2.5, "sidewalkSurface": "paving"}},
  "_rural": {"abstract": true, "category": "rural", "surface": "asphalt", "speed": 80, "crossings": false,
             "lanes": {"width": 3.25}, "sides": {"curb": "shoulder", "shoulder": 1.5, "shoulderSurface": "gravel"}},
  "_highway": {"abstract": true, "category": "highway", "surface": "asphalt", "speed": 110, "crossings": false, "centre": "none",
               "lanes": {"width": 3.75}, "median": {"width": 3, "surface": "concrete", "barrier": "concrete"},
               "sides": {"curb": "none", "shoulder": 2.5, "shoulderSurface": "asphalt", "barrier": "guardrail"}},
  "_path": {"abstract": true, "category": "path", "speed": 5, "centre": "none", "laneLines": false, "crossings": false,
            "lanes": {"forward": 0, "backward": 0}, "sides": {"curb": "none"}},

  "street_2": {"extends": "_city", "name": "Улица, 2 полосы", "lanes": {"forward": 1, "backward": 1},
               "props": [{"shape": "lamp", "side": 0, "spacing": 24, "offset": 0.4}]},
  "street_2_parking": {"extends": "_city", "name": "Улица с парковкой", "lanes": {"forward": ["car", "parking"], "backward": ["car", "parking"]},
               "props": [{"shape": "lamp", "side": 0, "spacing": 24, "offset": 0.4}, {"shape": "tree", "side": 0, "spacing": 12, "phase": 6, "offset": 1.2}]},
  "street_2_trees": {"extends": "_city", "name": "Улица с деревьями", "lanes": {"forward": 1, "backward": 1}, "sides": {"sidewalk": 4},
               "props": [{"shape": "lamp", "side": 0, "spacing": 24, "offset": 0.4}, {"shape": "tree", "side": 0, "spacing": 10, "phase": 5, "offset": 1.6},
                         {"shape": "flowerbed", "side": 0, "spacing": 20, "phase": 10, "offset": 1.6}]},
  "avenue_4": {"extends": "_city", "name": "Проспект, 4 полосы", "centre": "double", "lanes": {"forward": 2, "backward": 2}, "sides": {"sidewalk": 3.5},
               "props": [{"shape": "lamp", "side": 0, "spacing": 28, "offset": 0.4}, {"shape": "tree", "side": 0, "spacing": 12, "phase": 6, "offset": 2.2}]},
  "avenue_6": {"extends": "_city", "name": "Проспект, 6 полос", "centre": "none", "lanes": {"forward": 3, "backward": 3},
               "median": {"width": 2, "surface": "grass", "curb": true}, "sides": {"sidewalk": 4},
               "props": [{"shape": "lamp", "side": 2, "spacing": 30}, {"shape": "tree", "side": 0, "spacing": 12, "phase": 6, "offset": 2.5}]},
  "avenue_bus": {"extends": "_city", "name": "Проспект с автобусной полосой", "centre": "none", "lanes": {"forward": ["car", "bus"], "backward": ["car", "bus"]},
               "median": {"width": 1, "surface": "concrete", "curb": true}, "sides": {"sidewalk": 3.5},
               "props": [{"shape": "lamp", "side": 2, "spacing": 30}, {"shape": "bollard", "side": 0, "spacing": 4, "offset": 0.2}]},
  "boulevard": {"extends": "_city", "name": "Бульвар", "centre": "none", "lanes": {"forward": 2, "backward": 2},
               "median": {"width": 8, "surface": "grass", "curb": true}, "sides": {"sidewalk": 3},
               "props": [{"shape": "tree", "side": 2, "spacing": 9, "offset": -2}, {"shape": "tree", "side": 2, "spacing": 9, "offset": 2},
                         {"shape": "lamp", "side": 2, "spacing": 27, "phase": 4.5}, {"shape": "flowerbed", "side": 2, "spacing": 27, "phase": 13.5},
                         {"shape": "lamp", "side": 0, "spacing": 27, "offset": 0.4}]},
  "one_way_1": {"extends": "_city", "name": "Односторонняя, 1 полоса", "centre": "none", "lanes": {"forward": 1, "backward": 0}, "sides": {"sidewalk": 2},
               "props": [{"shape": "lamp", "side": 1, "spacing": 24, "offset": 0.4}]},
  "one_way_2": {"extends": "_city", "name": "Односторонняя, 2 полосы", "centre": "none", "lanes": {"forward": 2, "backward": 0},
               "props": [{"shape": "lamp", "side": 0, "spacing": 24, "offset": 0.4}]},
  "one_way_3": {"extends": "_city", "name": "Односторонняя, 3 полосы", "centre": "none", "lanes": {"forward": 3, "backward": 0}, "sides": {"sidewalk": 3},
               "props": [{"shape": "lamp", "side": 0, "spacing": 26, "offset": 0.4}]},
  "alley": {"extends": "_city", "name": "Переулок", "centre": "none", "laneLines": false, "speed": 20, "lanes": {"forward": 1, "backward": 1, "width": 2.75},
               "sides": {"sidewalk": 1.2}, "props": [{"shape": "pole", "side": 1, "spacing": 30, "offset": 0.2}]},
  "cobble_street": {"extends": "_city", "name": "Брусчатка", "surface": "cobble", "centre": "none", "laneLines": false, "speed": 30,
               "lanes": {"forward": 1, "backward": 1, "width": 3}, "sides": {"sidewalk": 2, "curb": "high"},
               "props": [{"shape": "lamp", "side": 0, "spacing": 18, "offset": 0.4}]},
  "pedestrian": {"extends": "_path", "name": "Пешеходная улица", "category": "city", "sides": {"sidewalk": 4, "sidewalkSurface": "paving"},
               "props": [{"shape": "lamp", "side": 0, "spacing": 20, "offset": 0.3}, {"shape": "tree", "side": 0, "spacing": 14, "phase": 7, "offset": 1.5},
                         {"shape": "flowerbed", "side": 0, "spacing": 14, "offset": 1.5}]},

  "highway_4": {"extends": "_highway", "name": "Шоссе 2×2", "lanes": {"forward": 2, "backward": 2},
               "props": [{"shape": "lamp", "side": 2, "spacing": 40}]},
  "highway_6": {"extends": "_highway", "name": "Шоссе 3×3", "lanes": {"forward": 3, "backward": 3},
               "props": [{"shape": "lamp", "side": 2, "spacing": 40}]},
  "highway_ramp": {"extends": "_highway", "name": "Съезд", "speed": 60, "lanes": {"forward": 1, "backward": 0, "width": 4},
               "median": {"width": 0, "barrier": "none"}, "sides": {"shoulder": 1.5}},
  "highway_ramp_2": {"extends": "_highway", "name": "Съезд, 2 полосы", "speed": 60, "lanes": {"forward": 2, "backward": 0},
               "median": {"width": 0, "barrier": "none"}, "sides": {"shoulder": 1.5}},

  "rural_2": {"extends": "_rural", "name": "Загородная, 2 полосы", "lanes": {"forward": 1, "backward": 1},
               "props": [{"shape": "pole", "side": 1, "spacing": 40, "offset": 2.5}]},
  "rural_2_guard": {"extends": "_rural", "name": "Загородная с отбойником", "lanes": {"forward": 1, "backward": 1},
               "sides": {"barrier": "guardrail"}},
  "rural_4": {"extends": "_rural", "name": "Загородная, 4 полосы", "centre": "double", "speed": 90, "lanes": {"forward": 2, "backward": 2, "width": 3.5},
               "sides": {"shoulder": 2, "shoulderSurface": "asphalt"}, "props": [{"shape": "pole", "side": 1, "spacing": 45, "offset": 3}]},
  "country_lane": {"extends": "_rural", "name": "Просёлок", "speed": 50, "centre": "none", "laneLines": false, "lanes": {"forward": 1, "backward": 1, "width": 2.75},
               "sides": {"shoulder": 1, "shoulderSurface": "grass"}},
  "gravel_road": {"extends": "_rural", "name": "Гравийная дорога", "surface": "gravel", "speed": 40, "centre": "none", "laneLines": false,
               "lanes": {"forward": 1, "backward": 1, "width": 3}, "sides": {"shoulder": 0.8, "shoulderSurface": "grass"}},
  "dirt_road": {"extends": "_rural", "name": "Грунтовка", "surface": "dirt", "speed": 30, "centre": "none", "laneLines": false,
               "lanes": {"forward": 1, "backward": 1, "width": 2.8}, "sides": {"shoulder": 0.6, "shoulderSurface": "grass"}},
  "rural_bridge": {"extends": "_rural", "name": "Мост загородный", "speed": 50, "centre": "none", "laneLines": false,
               "lanes": {"forward": 1, "backward": 1, "width": 3}, "sides": {"shoulder": 0.5, "shoulderSurface": "asphalt", "barrier": "guardrail"}},
  "farm_track": {"extends": "_rural", "name": "Полевая дорога", "surface": "dirt", "speed": 20, "centre": "none", "laneLines": false,
               "lanes": {"forward": 1, "backward": 0, "width": 3}, "sides": {"shoulder": 0.5, "shoulderSurface": "grass"}},

  "footpath": {"extends": "_path", "name": "Тротуар-дорожка", "sides": {"sidewalk": 1, "sidewalkSurface": "paving"}},
  "park_path": {"extends": "_path", "name": "Парковая дорожка", "sides": {"sidewalk": 1.25, "sidewalkSurface": "gravel"},
               "props": [{"shape": "lamp", "side": 1, "spacing": 22, "offset": 0.3}]},
  "trail": {"extends": "_path", "name": "Тропа", "sides": {"sidewalk": 0.6, "sidewalkSurface": "dirt"}}
})JSON";

template <class E, size_t N>
E enumOf(const Json& v, const char* const (&names)[N], E def, const std::string& where) {
    if (v.isNull()) return def;
    const std::string s = v.str();
    for (size_t i = 0; i < N; ++i) if (s == names[i]) return E(i);
    std::string all;
    for (size_t i = 0; i < N; ++i) all += (i ? ", " : "") + std::string(names[i]);
    throw std::runtime_error(where + ": unknown value '" + s + "' (" + all + ")");
}

const char* const kUse[] = {"car", "bus", "parking", "bike", "tram"};
const char* const kCurb[] = {"none", "low", "high", "sloped", "shoulder"};
const char* const kBarrier[] = {"none", "guardrail", "concrete", "fence", "wall", "hedge"};
const char* const kSurface[] = {"asphalt", "concrete", "cobble", "paving", "gravel", "dirt", "grass"};
const char* const kMarking[] = {"none", "dashed", "solid", "double", "yellow"};

// deep merge: b over a (objects merge, everything else replaces)
Json merged(const Json& a, const Json& b) {
    if (!a.isObject() || !b.isObject()) return b;
    Json out = a;
    for (auto& [k, v] : b.members()) {
        const Json* cur = out.find(k);
        out.set(k, cur && cur->isObject() && v.isObject() && k != "props" ? merged(*cur, v) : v);
    }
    return out;
}

struct Ctx { Fixed cell; };   // metres -> cells: m * cell

Fixed cells(const Json& j, const char* key, Fixed defMetres, const Ctx& c, const std::string& where) {
    const Json& v = j[key];
    if (v.isNull()) return defMetres * c.cell;
    if (!v.isNumber()) throw std::runtime_error(where + "." + key + ": a number (metres)");
    const Fixed m = v.fx();
    if (m < Fixed{}) throw std::runtime_error(where + "." + key + ": must not be negative");
    return m * c.cell;
}

std::vector<RoadLaneDef> lanesOf(const Json& lanes, const char* key, Fixed width, const Ctx& c, const std::string& where) {
    std::vector<RoadLaneDef> out;
    const Json& v = lanes[key];
    if (v.isNull()) return out;
    if (v.isNumber()) {
        const int64_t n = v.integer();
        if (n < 0 || n > 8) throw std::runtime_error(where + ".lanes." + key + ": 0..8 lanes");
        out.assign(size_t(n), RoadLaneDef{RoadLaneUse::Car, width});
        return out;
    }
    if (!v.isArray()) throw std::runtime_error(where + ".lanes." + key + ": a number of lanes or a list of lanes (centre outwards)");
    if (v.size() > 8) throw std::runtime_error(where + ".lanes." + key + ": at most 8 lanes");
    for (size_t i = 0; i < v.size(); ++i) {
        const Json& l = v[i];
        RoadLaneDef d;
        d.width = width;
        const std::string w = where + ".lanes." + key + "[" + std::to_string(i) + "]";
        if (l.isString()) d.use = enumOf(l, kUse, RoadLaneUse::Car, w);
        else if (l.isObject()) {
            d.use = enumOf(l["use"], kUse, RoadLaneUse::Car, w + ".use");
            if (l.has("width")) d.width = cells(l, "width", {}, c, w);
        } else throw std::runtime_error(w + ": \"car\" / \"bus\" / \"parking\" / \"bike\" / \"tram\" or {\"use\", \"width\"}");
        if (d.width <= Fixed{}) throw std::runtime_error(w + ".width: must be positive");
        out.push_back(d);
    }
    // parking lanes only at the kerb: the traffic drives lanes 0..n-1 and parks in the outermost one
    for (size_t i = 0; i + 1 < out.size(); ++i)
        if (out[i].use == RoadLaneUse::Parking) throw std::runtime_error(where + ".lanes." + key + ": a parking lane must be the outermost one");
    return out;
}

RoadSideDef sideOf(const Json& j, const Ctx& c, const std::string& where) {
    RoadSideDef s;
    s.curb = enumOf(j["curb"], kCurb, RoadCurb::None, where + ".curb");
    if (j.has("curbHeight")) s.curbHeight = j.getFx("curbHeight");
    s.shoulder = cells(j, "shoulder", {}, c, where);
    s.shoulderSurface = enumOf(j["shoulderSurface"], kSurface, RoadSurface::Gravel, where + ".shoulderSurface");
    s.sidewalk = cells(j, "sidewalk", {}, c, where);
    s.sidewalkSurface = enumOf(j["sidewalkSurface"], kSurface, RoadSurface::Paving, where + ".sidewalkSurface");
    s.barrier = enumOf(j["barrier"], kBarrier, RoadBarrier::None, where + ".barrier");
    if (j.has("barrierHeight")) s.barrierHeight = j.getFx("barrierHeight");
    return s;
}

RoadTypeDef parseType(const std::string& id, const Json& j, const Ctx& c) {
    const std::string where = "roadTypes '" + id + "'";
    RoadTypeDef t;
    t.id = id;
    t.name = j.getStr("name", id);
    t.category = j.getStr("category", "city");
    static const char* const cats[] = {"city", "rural", "highway", "path"};
    (void)enumOf(j["category"], cats, 0, where + ".category");
    const Json& lanes = j["lanes"];
    const Fixed width = cells(lanes, "width", Fixed::fromRatio(7, 2), c, where + ".lanes");
    if (width <= Fixed{}) throw std::runtime_error(where + ".lanes.width: must be positive");
    t.forward = lanesOf(lanes, "forward", width, c, where);
    t.backward = lanesOf(lanes, "backward", width, c, where);
    const Json& med = j["median"];
    if (med.isNumber()) t.median = med.fx() * c.cell;
    else if (med.isObject()) {
        t.median = cells(med, "width", {}, c, where + ".median");
        t.medianSurface = enumOf(med["surface"], kSurface, RoadSurface::Concrete, where + ".median.surface");
        t.medianBarrier = enumOf(med["barrier"], kBarrier, RoadBarrier::None, where + ".median.barrier");
        if (med.getBool("curb", false)) t.look.member("median").set("curb", Json::fromBool(true));
    }
    const Json both = j["sides"].isObject() ? j["sides"] : Json::object();
    t.left = sideOf(merged(both, j["left"].isObject() ? j["left"] : Json::object()), c, where + ".left");
    t.right = sideOf(merged(both, j["right"].isObject() ? j["right"] : Json::object()), c, where + ".right");
    t.surface = enumOf(j["surface"], kSurface, RoadSurface::Asphalt, where + ".surface");
    t.centre = enumOf(j["centre"], kMarking, RoadMarking::Dashed, where + ".centre");
    t.laneLines = j.getBool("laneLines", true);
    t.crossings = j.getBool("crossings", true);
    if (j.has("speed")) {
        const Fixed kmhv = j.getFx("speed");
        if (kmhv <= Fixed{}) throw std::runtime_error(where + ".speed: km/h, positive");
        t.speed = kmhv / Fixed::fromRatio(36, 10);
    }
    const Json& props = j["props"];
    if (!props.isNull() && !props.isArray()) throw std::runtime_error(where + ".props: a list");
    for (size_t i = 0; i < props.size(); ++i) {
        const Json& p = props[i];
        const std::string w = where + ".props[" + std::to_string(i) + "]";
        RoadPropDef d;
        d.shape = p.getStr("shape");
        d.model = p.getStr("model");
        d.type = p.getStr("type");
        static const char* const shapes[] = {"lamp", "tree", "bush", "flowerbed", "bollard", "pole", "bench", "sign"};
        if (d.shape.empty() && d.model.empty() && d.type.empty()) throw std::runtime_error(w + ": \"shape\", \"model\" or \"type\"");
        if (!d.shape.empty()) (void)enumOf(p["shape"], shapes, 0, w + ".shape");
        const int64_t side = p.getInt("side", 0);
        if (side != -1 && side != 0 && side != 1 && side != 2) throw std::runtime_error(w + ".side: -1 left, 1 right, 0 both, 2 median");
        d.side = int8_t(side);
        d.spacing = cells(p, "spacing", Fixed::fromInt(24), c, w);
        if (d.spacing < Fixed::fromRatio(1, 4)) throw std::runtime_error(w + ".spacing: too small");
        d.phase = cells(p, "phase", {}, c, w);
        if (p.has("offset")) d.offset = p.getFx("offset") * c.cell;   // may be negative (median: left of its centre)
        if (p.has("scale")) d.scale = p.getFx("scale");
        // trees, bushes and flowerbeds do not grow on a bridge deck unless asked
        d.onBridges = p.getBool("onBridges", !(d.shape == "tree" || d.shape == "bush" || d.shape == "flowerbed"));
        t.props.push_back(d);
    }
    if (j.has("look")) t.look = merged(t.look, j["look"]);
    if (t.forward.empty() && t.backward.empty() && t.left.sidewalk <= Fixed{} && t.right.sidewalk <= Fixed{})
        throw std::runtime_error(where + ": no lanes and no sidewalk: nothing to draw");
    return t;
}

// resolves "extends" chains of `raw` (id -> json) into full objects
Json resolved(const std::string& id, const std::map<std::string, Json>& raw, std::map<std::string, Json>& done, int depth) {
    if (auto it = done.find(id); it != done.end()) return it->second;
    auto it = raw.find(id);
    if (it == raw.end()) throw std::runtime_error("roadTypes: unknown type '" + id + "'");
    if (depth > 16) throw std::runtime_error("roadTypes '" + id + "': \"extends\" loops");
    Json j = it->second;
    const std::string base = j.getStr("extends");
    if (!base.empty()) {
        Json b = resolved(base, raw, done, depth + 1);
        b.erase("abstract");
        b.erase("name");
        j = merged(b, j);
    }
    j.erase("extends");
    done[id] = j;
    return j;
}

} // namespace

const char* roadSurfaceName(RoadSurface s) { return kSurface[size_t(s)]; }
const char* roadBarrierName(RoadBarrier b) { return kBarrier[size_t(b)]; }
const char* roadCurbName(RoadCurb c) { return kCurb[size_t(c)]; }

const Json& builtinRoadTypesJson() {
    static const Json j = Json::parse(kBuiltin);
    return j;
}

Fixed RoadTypeDef::carriageway(int dir) const {
    Fixed w = median / 2;
    for (auto& l : lanesOf(dir)) w += l.width;
    return w;
}
Fixed RoadTypeDef::halfWidth(int side) const {
    // right-hand: the forward lanes are on the right side of the centre line; the view / traffic mirror for left-hand
    const RoadSideDef& s = side > 0 ? right : left;
    return carriageway(side > 0 ? 1 : -1) + s.shoulder + s.sidewalk;
}
bool RoadTypeDef::drivable() const {
    for (auto* v : {&forward, &backward})
        for (auto& l : *v) if (l.use == RoadLaneUse::Car || l.use == RoadLaneUse::Bus) return true;
    return false;
}

Fixed roadCellMeters(const Json& rules) {
    const Fixed cm = rules.isObject() && rules.has("cellMeters") ? rules.getFx("cellMeters") : Fixed::fromInt(4);
    return cm > Fixed{} ? cm : Fixed::fromInt(4);
}

std::vector<RoadTypeDef> loadRoadTypes(const Json& rules) {
    if (!rules.isNull() && !rules.isObject()) throw std::runtime_error("roadTypes: an object of road type id -> definition");
    Ctx c;
    const Fixed cm = rules.isObject() && rules.has("cellMeters") ? rules.getFx("cellMeters") : Fixed::fromInt(4);
    if (cm <= Fixed{}) throw std::runtime_error("roadTypes.cellMeters: metres per map cell, positive");
    c.cell = Fixed::fromInt(1) / cm;
    std::map<std::string, Json> raw;
    std::vector<std::string> order;
    const bool lib = !rules.isObject() || rules.getBool("builtin", true);
    std::vector<std::string> builtinIds;
    if (lib)
        for (auto& [k, v] : builtinRoadTypesJson().members()) { raw[k] = v; order.push_back(k); builtinIds.push_back(k); }
    if (rules.isObject())
        for (auto& [k, v] : rules.members()) {
            if (k == "cellMeters" || k == "builtin") continue;
            if (!v.isObject()) throw std::runtime_error("roadTypes '" + k + "': an object");
            if (raw.count(k)) raw[k] = merged(raw[k], v);   // a game may retune a built-in type in place
            else { raw[k] = v; order.push_back(k); }
        }
    std::map<std::string, Json> done;
    std::vector<RoadTypeDef> out;
    for (auto& id : order) {
        Json j = resolved(id, raw, done, 0);
        if (j.getBool("abstract", false)) continue;
        RoadTypeDef t = parseType(id, j, c);
        t.builtin = std::find(builtinIds.begin(), builtinIds.end(), id) != builtinIds.end() && !(rules.isObject() && rules.has(id));
        out.push_back(std::move(t));
    }
    return out;
}

int findRoadType(const std::vector<RoadTypeDef>& types, const std::string& id) {
    for (size_t i = 0; i < types.size(); ++i) if (types[i].id == id) return int(i);
    return -1;
}

Json roadTypeToJson(const RoadTypeDef& t) {
    Json j = Json::object();
    j.set("id", Json::string(t.id));
    j.set("name", Json::string(t.name));
    j.set("category", Json::string(t.category));
    auto lanes = [&](const std::vector<RoadLaneDef>& v) {
        Json a = Json::array();
        for (auto& l : v) { Json o = Json::object(); o.set("use", Json::string(kUse[size_t(l.use)])); o.set("width", Json::number(l.width)); a.push(o); }
        return a;
    };
    j.set("forward", lanes(t.forward));
    j.set("backward", lanes(t.backward));
    j.set("median", Json::number(t.median));
    auto side = [&](const RoadSideDef& s) {
        Json o = Json::object();
        o.set("curb", Json::string(kCurb[size_t(s.curb)]));
        o.set("shoulder", Json::number(s.shoulder));
        o.set("sidewalk", Json::number(s.sidewalk));
        o.set("barrier", Json::string(kBarrier[size_t(s.barrier)]));
        return o;
    };
    j.set("left", side(t.left));
    j.set("right", side(t.right));
    j.set("surface", Json::string(kSurface[size_t(t.surface)]));
    j.set("width", Json::number(t.halfWidth(-1) + t.halfWidth(1)));
    j.set("speedKmh", Json::number(t.speed * Fixed::fromRatio(36, 10)));
    j.set("props", Json::number(int64_t(t.props.size())));
    j.set("builtin", Json::fromBool(t.builtin));
    return j;
}

} // namespace aiverse::roads
