#include "RouteOps.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace aiverse::editor {

namespace {
Json str(const std::string& s) { return Json::string(s); }
void needProject(const Workspace& ws) { if (ws.project.dir.empty()) throw std::runtime_error("no game is open: call open_game first"); }
bool isGarrison(const game::Rules& R, const DocEntity& e) {
    const int t = R.findType(e.type);
    return t >= 0 && R.types[size_t(t)].garrisonMax > 0;
}
// entity reference: id string, "#<index>" or an integer index; entities without an id get one
int entityRef(Workspace& w, const Json& v, bool assignId, std::string& id) {
    MapData& d = w.map.data;
    int i = -1;
    if (v.isNumber()) i = int(v.integer(-1));
    else if (v.isString() && !v.str().empty() && v.str()[0] == '#') i = std::atoi(v.str().c_str() + 1);
    else if (v.isString()) i = w.map.findEntity(v.str());
    if (i < 0 || size_t(i) >= d.entities.size()) throw std::runtime_error("unknown entity " + v.dump(-1) + " (entity id or index)");
    if (d.entities[size_t(i)].id.empty() && assignId) d.entities[size_t(i)].id = w.map.uniqueId("point");
    id = d.entities[size_t(i)].id.empty() ? "#" + std::to_string(i) : d.entities[size_t(i)].id;
    return i;
}
std::vector<game::RouteDef> readRoutes(const MapDoc& doc) {
    std::vector<game::RouteDef> out;
    const Json& rs = doc.data.extra["routes"];
    for (size_t i = 0; i < rs.size(); ++i) out.push_back(game::parseRoute(rs[i], int(i)));
    return out;
}
void writeRoutes(MapDoc& doc, const std::vector<game::RouteDef>& rs) {
    if (rs.empty()) { doc.data.extra.erase("routes"); return; }
    Json a = Json::array();
    for (auto& r : rs) a.push(game::routeToJson(r));
    doc.data.extra.set("routes", a);
}
int findRoute(const std::vector<game::RouteDef>& rs, const std::string& a, const std::string& b) {
    for (size_t i = 0; i < rs.size(); ++i)
        if ((rs[i].from == a && rs[i].to == b) || (rs[i].from == b && rs[i].to == a)) return int(i);
    return -1;
}
Vec2 posOf(const DocEntity& e) { return {Fixed::fromRaw(int64_t(std::llround(double(e.x) * 65536.0))), Fixed::fromRaw(int64_t(std::llround(double(e.z) * 65536.0)))}; }
Json routeSummary(const game::RouteDef& r, const game::Rules& R, int index) {
    Json j = game::routeToJson(r);
    j.set("index", Json::number(int64_t(index)));
    const auto lines = game::buildRouteLines(r, R.routes);
    j.set("length", Json::number(game::polylineLength(lines[0])));
    j.set("aiLength", Json::number(game::routeAiLength(r)));
    j.set("lineCount", Json::number(int64_t(lines.size())));
    return j;
}
} // namespace

std::vector<RoutePreviewLine> routePreviewLines(const game::Rules& rules, const MapDoc& doc) {
    std::vector<RoutePreviewLine> out;
    const Json& rs = doc.data.extra["routes"];
    for (size_t i = 0; i < rs.size(); ++i) {
        try {
            const auto lines = game::buildRouteLines(game::parseRoute(rs[i], int(i)), rules.routes);
            for (size_t k = 0; k < lines.size(); ++k) {
                RoutePreviewLine l; l.route = int(i); l.line = int(k);
                for (Vec2 p : lines[k]) l.pts.push_back({float(p.x.toDouble()), float(p.y.toDouble())});
                out.push_back(std::move(l));
            }
        } catch (const std::exception&) {}
    }
    return out;
}

void registerRouteOps(Workspace& ws) {
    Workspace* w = &ws;
    auto add = [&ws](const char* name, const char* desc, const char* schema, Workspace::OpFn fn) {
        ws.registerOp({name, desc, Json::parse(schema), std::move(fn), false});
    };
    add("route_list",
        "Authored routes of the open map (map \"routes\", Bunker Wars RoutesModule; docs/game-format.md § Routes): from / to "
        "entity ids, control points, per-point offsets, raw lines, blockers, line-0 length and BW AI length, plus the map "
        "\"screen\" block. With routes, units are only sent between garrisons that have a route.",
        "{}", [w](const Json&) {
            needProject(*w);
            const auto rs = readRoutes(w->map);
            Json arr = Json::array();
            for (size_t i = 0; i < rs.size(); ++i) arr.push(routeSummary(rs[i], w->project.rules, int(i)));
            Json r = Json::object();
            r.set("routes", arr);
            if (w->map.data.extra.has("screen")) r.set("screen", w->map.data.extra["screen"]);
            return r;
        });
    add("route_set",
        "Add or replace the route between two garrison entities (ids or indices; entities without an id get one). 'points' "
        "[[x, z], ...] control points from the first garrison to the second (at least 2; a point may be {\"at\": [x, z], "
        "\"offsets\": [8 numbers], \"zone\": int, \"slowdown\": n}); optional 'lines' (raw nodes per line), 'blockers' "
        "(entity ids that close the route while alive), 'flying'. Without points: a straight BW route (in / out points).",
        R"({"properties": {"from": {}, "to": {}, "points": {"type": "array"}, "lines": {"type": "array"}, "blockers": {"type": "array"},
            "flying": {"type": "boolean"}}, "required": ["from", "to"]})",
        [w](const Json& p) {
            needProject(*w);
            w->map.beginEdit();
            std::string a, b;
            const int ia = entityRef(*w, p["from"], true, a), ib = entityRef(*w, p["to"], true, b);
            const auto& R = w->project.rules;
            if (!isGarrison(R, w->map.data.entities[size_t(ia)]) || !isGarrison(R, w->map.data.entities[size_t(ib)]))
                throw std::runtime_error("routes join garrison structures (types with \"garrison\")");
            Json rj = Json::object();
            rj.set("from", str(a)); rj.set("to", str(b));
            if (p["points"].size() >= 2) rj.set("points", p["points"]);
            else {
                game::RouteDef s = game::flyingRoute(posOf(w->map.data.entities[size_t(ia)]), posOf(w->map.data.entities[size_t(ib)]), R.routes);
                s.flying = false;
                s.points.erase(s.points.begin() + 2);   // BW CreateRouteInfo without the middle point
                rj.set("points", game::routeToJson(s)["points"]);
            }
            for (const char* k : {"lines", "blockers", "flying"}) if (p.has(k)) rj.set(k, p[k]);
            game::RouteDef r = game::parseRoute(rj, 0);
            for (auto& bl : r.blockers) { std::string id; entityRef(*w, Json::string(bl), false, id); }
            auto rs = readRoutes(w->map);
            const int at = findRoute(rs, a, b);
            if (at >= 0) rs[size_t(at)] = r; else rs.push_back(r);
            writeRoutes(w->map, rs);
            w->map.touched();
            w->note({Change::Map, 0, 0, -1, -1, -1, "Маршрут " + a + " - " + b});
            Json out = routeSummary(r, R, at >= 0 ? at : int(rs.size() - 1));
            out.set("summary", str(std::string(at >= 0 ? "Маршрут заменён: " : "Маршрут добавлен: ") + a + " - " + b));
            return out;
        });
    add("route_delete",
        "Delete authored routes: 'from' + 'to' (either direction), or 'index', or 'all': true.",
        R"({"properties": {"from": {}, "to": {}, "index": {"type": "integer"}, "all": {"type": "boolean"}}})",
        [w](const Json& p) {
            needProject(*w);
            auto rs = readRoutes(w->map);
            size_t before = rs.size();
            w->map.beginEdit();
            if (p.getBool("all")) rs.clear();
            else if (p.has("index")) { const int64_t i = p.getInt("index", -1); if (i < 0 || size_t(i) >= rs.size()) throw std::runtime_error("index out of range"); rs.erase(rs.begin() + i); }
            else {
                std::string a, b;
                entityRef(*w, p["from"], false, a); entityRef(*w, p["to"], false, b);
                const int at = findRoute(rs, a, b);
                if (at < 0) throw std::runtime_error("no route between " + a + " and " + b);
                rs.erase(rs.begin() + at);
            }
            writeRoutes(w->map, rs);
            w->map.touched();
            w->note({Change::Map, 0, 0, -1, -1, -1, "Маршруты удалены"});
            Json r = Json::object();
            r.set("deleted", Json::number(int64_t(before - rs.size())));
            r.set("summary", str("Удалено маршрутов: " + std::to_string(before - rs.size())));
            return r;
        });
    add("route_generate",
        "Author straight routes like the Bunker Wars map editor (MapRoutesModule.Setup / CreateRouteInfo): for every pair of "
        "garrison entities (or those within 'maxDistance' cells) that has no route yet, a route with in / out points "
        "lanes.routes.inOut from each end. 'replace': true regenerates existing ones too. Edit the points afterwards with route_set.",
        R"({"properties": {"maxDistance": {"type": "number"}, "replace": {"type": "boolean"}}})",
        [w](const Json& p) {
            needProject(*w);
            w->map.beginEdit();
            const auto& R = w->project.rules;
            auto rs = readRoutes(w->map);
            if (p.getBool("replace")) rs.clear();
            const double maxD = p["maxDistance"].toDouble(0);
            auto& ents = w->map.data.entities;
            int added = 0;
            for (size_t i = 0; i < ents.size(); ++i)
                for (size_t j = i + 1; j < ents.size(); ++j) {
                    if (!isGarrison(R, ents[i]) || !isGarrison(R, ents[j])) continue;
                    if (maxD > 0 && std::hypot(double(ents[i].x - ents[j].x), double(ents[i].z - ents[j].z)) > maxD) continue;
                    std::string a, b;
                    entityRef(*w, Json::number(int64_t(i)), true, a); entityRef(*w, Json::number(int64_t(j)), true, b);
                    if (findRoute(rs, a, b) >= 0) continue;
                    game::RouteDef s = game::flyingRoute(posOf(ents[i]), posOf(ents[j]), R.routes);
                    s.flying = false;
                    s.points.erase(s.points.begin() + 2);
                    s.from = a; s.to = b;
                    rs.push_back(s);
                    ++added;
                }
            writeRoutes(w->map, rs);
            w->map.touched();
            w->note({Change::Map, 0, 0, -1, -1, -1, "Маршруты созданы"});
            Json r = Json::object();
            r.set("added", Json::number(int64_t(added)));
            r.set("routes", Json::number(int64_t(rs.size())));
            r.set("summary", str("Создано маршрутов: " + std::to_string(added)));
            return r;
        });
    add("screen_set",
        "Set or delete the map \"screen\" (Bunker Wars MultiscreenModule): 'size' [w, h] cells (BW 2720 x 1520 px = 54.4 x 30.4), "
        "'center' [x, z] (default map centre), 'bounds' [left, right, top, bottom] how far scripts may move the centre, "
        "'smooth' camera follow rate; 'delete': true removes it. Garrisons off the screen do not grow.",
        R"({"properties": {"size": {"type": "array"}, "center": {"type": "array"}, "bounds": {"type": "array"}, "smooth": {"type": "number"},
            "delete": {"type": "boolean"}}})",
        [w](const Json& p) {
            needProject(*w);
            w->map.beginEdit();
            if (p.getBool("delete")) w->map.data.extra.erase("screen");
            else {
                Json s = w->map.data.extra.has("screen") ? w->map.data.extra["screen"] : Json::object();
                for (const char* k : {"size", "center", "bounds", "smooth"}) if (p.has(k)) s.set(k, p[k]);
                game::parseScreen(s, w->map.data.w, w->map.data.h);   // validate
                w->map.data.extra.set("screen", s);
            }
            w->map.touched();
            w->note({Change::Map, 0, 0, -1, -1, -1, "Экран карты"});
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("summary", str(p.getBool("delete") ? "Экран карты удалён" : "Экран карты сохранён"));
            return r;
        });
}

} // namespace aiverse::editor
