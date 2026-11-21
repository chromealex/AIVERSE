#include "WaterOps.h"
#include "engine/world/NereidFlowAuthoring.h"
#include "apps/editor/AuthoringWater.h"
#include <algorithm>
#include <cmath>
#include <map>
#include <stdexcept>

namespace aiverse::editor {

using namespace game::water;

namespace waterops {

ShoreTypes types(const Workspace& ws) { return loadShoreTypes(ws.project.rulesJson["shoreTypes"]); }

namespace {

// points along the map's spline roads (cells): what counts as "town" next to the water, and bridges over it
std::vector<std::pair<float, float>> roadSamples(const Json& rn) {
    std::vector<std::pair<float, float>> out;
    if (!rn.isObject()) return out;
    std::map<int64_t, std::pair<float, float>> nodes;
    for (auto& n : rn["nodes"].items()) nodes[n.getInt("id")] = {float(n["x"].toDouble()), float(n["z"].toDouble())};
    for (auto& e : rn["edges"].items()) {
        auto a = nodes.find(e.getInt("a")), b = nodes.find(e.getInt("b"));
        if (a == nodes.end() || b == nodes.end()) continue;
        const auto A = a->second, B = b->second;
        std::pair<float, float> c1 = A, c2 = B;
        if (e["c1"].isArray() && e["c1"].size() >= 2) c1 = {float(e["c1"][size_t(0)].toDouble()), float(e["c1"][size_t(1)].toDouble())};
        if (e["c2"].isArray() && e["c2"].size() >= 2) c2 = {float(e["c2"][size_t(0)].toDouble()), float(e["c2"][size_t(1)].toDouble())};
        const float len = std::hypot(B.first - A.first, B.second - A.second);
        const int n = std::max(2, int(len * 2));
        for (int k = 0; k <= n; ++k) {
            const float t = float(k) / float(n), u = 1 - t;
            out.push_back({u * u * u * A.first + 3 * u * u * t * c1.first + 3 * u * t * t * c2.first + t * t * t * B.first,
                           u * u * u * A.second + 3 * u * u * t * c1.second + 3 * u * t * t * c2.second + t * t * t * B.second});
        }
    }
    return out;
}

} // namespace

Json bake(Workspace& ws, Network n) {
    MapData& d = ws.map.data;
    const ShoreTypes T = types(ws);
    const int W = d.w, H = d.h;
    Json* tiles = d.extra.find("tiles");
    Json* rows = tiles && tiles->isObject() ? tiles->find("rows") : nullptr;
    if (rows && (!rows->isArray() || int(rows->size()) != H)) rows = nullptr;
    auto tileAt = [&](size_t i) -> char {
        if (!rows) return 0;
        const std::string& r = rows->at(i / size_t(W)).str();
        return i % size_t(W) < r.size() ? r[i % size_t(W)] : 0;
    };
    auto setTile = [&](size_t i, char c) {
        if (!rows || !c) return;
        std::string r = rows->at(i / size_t(W)).str();
        if (i % size_t(W) < r.size()) { r[i % size_t(W)] = c; rows->at(i / size_t(W)) = Json::string(r); }
    };
    // 1. undo the previous bake
    for (auto& c : n.baked["cells"].items()) {
        if (!c.isArray() || c.size() < 2) continue;
        const size_t i = size_t(c[size_t(0)].integer());
        if (i >= d.cells.size()) continue;
        const std::string old = c[size_t(1)].str(".");
        if (!old.empty()) d.cells[i] = old[0];
        if (c.size() > 2 && !c[size_t(2)].str().empty()) setTile(i, c[size_t(2)].str()[0]);
    }
    // 2. water, auto shores, cliffs
    Geometry g = buildGeometry(n, W, H, T.cellMeters);
    const auto wet = waterCells(n, g, W, H, T.cellMeters);
    markOpen(g, wet, W, H);
    const auto roadPts = roadSamples(d.extra["roadNetwork"]);
    std::vector<uint8_t> built(size_t(W) * size_t(H), 0);
    for (size_t i = 0; i < built.size(); ++i) if (d.cells[i] == '#' || d.cells[i] == '+') built[i] = 1;
    for (auto [x, z] : roadPts) { const int cx = int(std::floor(x)), cz = int(std::floor(z)); if (cx >= 0 && cz >= 0 && cx < W && cz < H) built[size_t(cz) * size_t(W) + size_t(cx)] = 1; }
    for (size_t i = 0; i < built.size(); ++i) if (wet[i]) built[i] = 0;   // bridges over the water are not town
    classify(n, g, T, [&](int x, int z) { return built[size_t(z) * size_t(W) + size_t(x)] != 0; }, W, H);
    const auto cliff = cliffCells(n, g, T, W, H);
    // 3. write the cells, remember what was there
    const std::string wTile = n.tiles.count("water") ? n.tiles["water"] : "", cTile = n.tiles.count("cliff") ? n.tiles["cliff"] : "";
    Json record = Json::array();
    int water = 0, cliffs = 0;
    for (size_t i = 0; i < d.cells.size(); ++i) {
        char want = 0, wantTile = 0;
        if (wet[i]) { want = '~'; wantTile = wTile.empty() ? 0 : wTile[0]; ++water; }
        else if (cliff[i]) { want = '#'; wantTile = cTile.empty() ? 0 : cTile[0]; ++cliffs; }
        if (!want) continue;
        const char oldTile = tileAt(i);
        if (d.cells[i] == want && (!wantTile || oldTile == wantTile)) continue;
        Json c = Json::array();
        c.push(Json::number(int64_t(i)));
        c.push(Json::string(std::string(1, d.cells[i])));
        if (wantTile && oldTile) c.push(Json::string(std::string(1, oldTile)));
        record.push(c);
        d.cells[i] = want;
        if (wantTile) setTile(i, wantTile);
    }
    Json baked = Json::object();
    baked.set("cells", record);
    n.baked = baked;
    if (n.empty()) d.extra.erase("waterNetwork");
    else d.extra.set("waterNetwork", networkToJson(n));
    // 4. land in one piece? (spline roads over the water count as bridges)
    std::vector<uint8_t> land(d.cells.size(), 0);
    for (size_t i = 0; i < land.size(); ++i) land[i] = d.cells[i] != '~' && d.cells[i] != '#';
    for (auto [x, z] : roadPts)   // a road is wider than a cell: 3 x 3, so diagonal steps of the samples stay joined
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                const int cx = int(std::floor(x)) + dx, cz = int(std::floor(z)) + dz;
                if (cx >= 0 && cz >= 0 && cx < W && cz < H) land[size_t(cz) * size_t(W) + size_t(cx)] = 1;
            }
    std::vector<int> comp(land.size(), -1);
    std::vector<std::pair<int, size_t>> parts;   // size, first cell
    for (size_t s = 0; s < land.size(); ++s) {
        if (!land[s] || comp[s] >= 0) continue;
        const int id = int(parts.size());
        std::vector<size_t> st{s};
        comp[s] = id;
        int size = 0;
        while (!st.empty()) {
            const size_t i = st.back(); st.pop_back(); ++size;
            const int x = int(i % size_t(W)), z = int(i / size_t(W));
            const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
            for (auto& o : nb) {
                const int nx = x + o[0], nz = z + o[1];
                if (nx < 0 || nz < 0 || nx >= W || nz >= H) continue;
                const size_t j = size_t(nz) * size_t(W) + size_t(nx);
                if (land[j] && comp[j] < 0) { comp[j] = id; st.push_back(j); }
            }
        }
        parts.push_back({size, s});
    }
    Json r = Json::object();
    r.set("waterCells", Json::number(int64_t(water)));
    r.set("cliffCells", Json::number(int64_t(cliffs)));
    r.set("changedCells", Json::number(int64_t(record.size())));
    int big = 0;
    Json islands = Json::array();
    for (auto& [size, first] : parts) {
        if (size < 8) continue;
        ++big;
        if (big > 1) { Json q = Json::array(); q.push(Json::number(int64_t(first % size_t(W)))); q.push(Json::number(int64_t(first / size_t(W)))); q.push(Json::number(int64_t(size))); islands.push(q); }
    }
    r.set("landParts", Json::number(int64_t(big)));
    if (big > 1) {
        r.set("separatedLand", islands);   // [x, z, cells] of every part after the first: needs a bridge
        r.set("warning", Json::string(std::to_string(big) + " land parts without a bridge between them (spline road bridges join them)"));
    }
    Json bodies = Json::array();
    for (auto& B : n.bodies) {
        Json b = Json::object();
        b.set("id", Json::number(int64_t(B.id)));
        b.set("kind", Json::string(kindName(B.kind)));
        Json banks = Json::array();
        for (auto& bank : g.banks) {
            if (n.bodies[size_t(bank.body)].id != B.id) continue;
            Json k = Json::object();
            k.set("side", Json::number(int64_t(bank.side)));
            k.set("length", Json::number(double(bank.length), 1));
            {   // metres where the land side is water too (a mouth): no shore there
                double open = 0;
                for (size_t i = 1; i < bank.pts.size(); ++i) if (bank.pts[i].open && bank.pts[i - 1].open) open += double(bank.pts[i].s - bank.pts[i - 1].s);
                if (open > 0.5) k.set("openMetres", Json::number(open, 1));
            }
            std::map<std::string, double> by;
            Json segs = Json::array();
            for (auto& s : B.shores) {
                if (s.side != bank.side) continue;
                by[s.type] += double(s.to - s.from);
                Json q = Json::object();
                q.set("from", Json::number(double(s.from), 1)); q.set("to", Json::number(double(s.to), 1));
                q.set("type", Json::string(s.type));
                if (!s.generated) q.set("manual", Json::fromBool(true));
                segs.push(q);
            }
            Json m = Json::object();
            for (auto& [t, l] : by) m.set(t, Json::number(l, 1));
            k.set("metres", m);
            k.set("segments", segs);
            banks.push(k);
        }
        b.set("banks", banks);
        bodies.push(b);
    }
    r.set("bodies", bodies);
    Json issues = Json::array();
    for (auto& i : g.issues) issues.push(Json::string(i));
    if (issues.size()) r.set("issues", issues);
    return r;
}

} // namespace waterops

void registerWaterOps(Workspace& ws) {
    Workspace* w = &ws;
    auto add = [&ws](const char* name, const char* desc, const char* schema, Workspace::OpFn fn) {
        ws.registerOp({name, desc, Json::parse(schema), std::move(fn), false});
    };
    auto need = [w] { if (w->project.dir.empty()) throw std::runtime_error("no game is open: call open_game first"); };
    auto read = [w] { return parseNetwork(w->map.data.extra["waterNetwork"], waterops::types(*w)); };
    auto commit = [w](Network n, const std::string& text) {
        w->map.beginEdit();
        Json r = waterops::bake(*w, std::move(n));
        w->map.touched();
        w->note({Change::Cells, 0, 0, w->map.data.w - 1, w->map.data.h - 1, -1, text});
        return r;
    };
    add("nereid_water_state","Native NEREID surfaces and resolved authoring currents.","{}",[w,need](const Json&){need();return world::nereidFlowState(authoredWaterDocument(w->map));});
    add("nereid_flow_set","Set NEREID manual velocity or automatic slope current; editable grade in degrees. Undoable map authoring, does not mutate legacy waterNetwork.",
        R"({"type":"object","properties":{"id":{"type":"integer"},"mode":{"type":"string","enum":["automatic","manual"]},"flow":{"type":"array"},"slopeDegrees":{"type":"number"},"downhillHeadingDegrees":{"type":"number"},"responseSeconds":{"type":"number"},"maxSpeed":{"type":"number"},"origin":{"type":"array"}},"required":["id","mode"]})",
        [w,need](const Json& input){need();if(w->project.isFps())throw std::runtime_error("NEREID MapDoc operation requires a flat map");editAuthoredWaterFlow(w->map,input);w->note({Change::Cells,0,0,0,0,-1,"NEREID flow"});return world::nereidFlowState(authoredWaterDocument(w->map));});
    add("water_types",
        "Shore types of water from splines (docs/water.md): the built-in library (embankment, embankment_stone, cliff, "
        "beach, bank) plus rules \"shoreTypes\" (a game retunes a type by id, gives its own textures per surface and "
        "prop models). Each: id, name, kind (embankment | cliff | beach | natural) and its sizes in metres.",
        "{}", [w, need](const Json&) {
            need();
            const ShoreTypes T = waterops::types(*w);
            Json a = Json::array();
            for (auto& t : T.list) a.push(shoreTypeToJson(t));
            Json r = Json::object();
            r.set("cellMeters", Json::number(double(T.cellMeters), 2));
            r.set("types", a);
            return r;
        });
    add("water_network",
        "The map's water (\"waterNetwork\"): bodies (strait / river: centre line [x, z, width m]; sea: map side and "
        "shore line [x, z]) with their shore segments per bank (metres along, shore type, hand-drawn or auto).",
        "{}", [w, need](const Json&) {
            need();
            const Json& j = w->map.data.extra["waterNetwork"];
            return j.isObject() ? j : Json::object().set("bodies", Json::array());
        });
    add("water_set",
        "Adds or replaces one water body and bakes the map (cells under the water become '~', under cliffs '#'; shores "
        "\"auto\" are recomputed). body: {id?, kind: strait | river | sea, points: [[x, z, widthMetres], ...] (strait, "
        "river: centre line in cells, at least 2 points, smoothed) or side: north|south|east|west + shore: [[x, z], ...] "
        "(sea), depth?, shores?: \"auto\" | [{side, from, to, type}]}. tiles?: {water: 'w', cliff: 'x'} - chars the bake "
        "also writes into the map's tiles layer. Strait: embankments on both banks; river: embankment by the town, "
        "natural bank elsewhere; sea: embankment by the town, cliffs at headlands and map ends, beaches between.",
        R"({"type":"object","properties":{"body":{"type":"object"},"tiles":{"type":"object"}},"required":["body"]})",
        [w, need, read, commit](const Json& p) {
            need();
            const ShoreTypes T = waterops::types(*w);
            Network n = read();
            Json body = p["body"];
            if (!body.has("id")) { int id = 1; for (auto& b : n.bodies) id = std::max(id, b.id + 1); body.set("id", Json::number(int64_t(id))); }
            Json one = Json::object();
            Json arr = Json::array(); arr.push(body); one.set("bodies", arr);
            Network parsed = parseNetwork(one, T);   // validates with readable errors
            Body B = parsed.bodies[0];
            const int at = n.index(B.id);
            if (at >= 0) {
                // a changed shape: hand-drawn segments are kept, auto ones recomputed
                if (!body.has("shores")) { B.shores = n.bodies[size_t(at)].shores; B.autoShores = n.bodies[size_t(at)].autoShores; }
                n.bodies[size_t(at)] = B;
            } else n.bodies.push_back(B);
            if (p["tiles"].isObject()) for (auto& [k, v] : p["tiles"].members()) if (v.isString()) n.tiles[k] = v.str();
            Json r = commit(std::move(n), "Вода: " + std::string(kindName(B.kind)) + " " + std::to_string(B.id));
            r.set("id", Json::number(int64_t(B.id)));
            return r;
        });
    add("water_delete", "Removes a water body by id and bakes the map again (its cells get back what was there).",
        R"({"type":"object","properties":{"id":{"type":"integer"}},"required":["id"]})",
        [need, read, commit](const Json& p) {
            need();
            Network n = read();
            const int at = n.index(int(p.getInt("id")));
            if (at < 0) throw std::runtime_error("no water body " + std::to_string(p.getInt("id")) + " (water_network lists them)");
            n.bodies.erase(n.bodies.begin() + at);
            return commit(std::move(n), "Вода: удалить " + std::to_string(p.getInt("id")));
        });
    add("water_shores",
        "Shore segments of one water body. Either a hand-drawn segment {id, side (-1 left / 1 right bank of a strait / "
        "river, 1 for the sea), from, to (metres along the bank), type (water_types)}: kept over auto; or {id, reset: "
        "true}: drop the hand-drawn ones (all auto again). Bakes the map.",
        R"({"type":"object","properties":{"id":{"type":"integer"},"side":{"type":"integer"},"from":{"type":"number"},"to":{"type":"number"},"type":{"type":"string"},"reset":{"type":"boolean"}},"required":["id"]})",
        [w, need, read, commit](const Json& p) {
            need();
            const ShoreTypes T = waterops::types(*w);
            Network n = read();
            const int at = n.index(int(p.getInt("id")));
            if (at < 0) throw std::runtime_error("no water body " + std::to_string(p.getInt("id")) + " (water_network lists them)");
            Body& B = n.bodies[size_t(at)];
            B.autoShores = true;
            if (p.getBool("reset")) B.shores.clear();
            else {
                Segment s;
                s.side = p.getInt("side", 1) < 0 ? -1 : 1;
                s.from = float(p["from"].toDouble()); s.to = float(p["to"].toDouble());
                s.type = p.getStr("type");
                if (T.find(s.type) < 0) throw std::runtime_error("unknown shore type '" + s.type + "' (water_types lists them)");
                if (s.to <= s.from) throw std::runtime_error("to must be greater than from");
                // the new segment replaces what hand-drawn segments it overlaps
                std::vector<Segment> keep;
                for (auto& o : B.shores) {
                    if (o.generated || o.side != s.side || o.to <= s.from || o.from >= s.to) { keep.push_back(o); continue; }
                    if (o.from < s.from) { Segment a = o; a.to = s.from; keep.push_back(a); }
                    if (o.to > s.to) { Segment b = o; b.from = s.to; keep.push_back(b); }
                }
                keep.push_back(s);
                B.shores = keep;
            }
            return commit(std::move(n), "Вода: берега " + std::to_string(p.getInt("id")));
        });
    add("water_bake", "Bakes the map's water again (after the roads, the rules or the map changed): cells, auto shores, "
        "and a check that the land is one piece (spline road bridges join parts).",
        "{}", [need, read, commit](const Json&) {
            need();
            return commit(read(), "Вода: пересчитать");
        });
}

} // namespace aiverse::editor
