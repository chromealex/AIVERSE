// Water from splines (docs/water.md): shore types, bodies, banks, auto shores, baking into the map's cells.
#include "engine/view/game/GameWater.h"
#ifdef AIVERSE_TESTS_EDITOR_CORE
#include "apps/editor/core/WaterOps.h"
#endif

namespace {
namespace wtr = aiverse::game::water;

float shoreMetres(const wtr::Body& b, const wtr::ShoreTypes& T, int side, wtr::ShoreKind k) {
    float m = 0;
    for (auto& s : b.shores) if (s.side == side && T.find(s.type) >= 0 && T.list[size_t(T.find(s.type))].kind == k) m += s.to - s.from;
    return m;
}
wtr::Network seaNetwork(const wtr::ShoreTypes& T) {
    return wtr::parseNetwork(Json::parse(R"({"bodies": [{"id": 1, "kind": "sea", "side": "south",
        "shore": [[0, 96], [60, 95], [100, 92], [130, 84], [150, 92], [200, 97], [256, 96]]}]})"), T);
}
} // namespace

TEST(water_shore_types_builtin_and_overrides) {
    const wtr::ShoreTypes B = wtr::loadShoreTypes(Json());
    CHECK(B.find("embankment") >= 0 && B.find("embankment_stone") >= 0 && B.find("cliff") >= 0 && B.find("beach") >= 0 && B.find("bank") >= 0);
    CHECK(B.list[size_t(B.find("cliff"))].kind == wtr::ShoreKind::Cliff);
    const wtr::ShoreTypes T = wtr::loadShoreTypes(Json::parse(R"({"cellMeters": 2,
        "embankment": {"promenade": {"width": 7}, "textures": {"stone": "textures/quay.png"}},
        "granite_quay": {"extends": "embankment", "edge": {"parapet": "stone"}},
        "dunes": {"kind": "beach", "width": [20, 40], "props": [{"model": "models/hut.glb", "spacing": 50}]}})"));
    CHECK(T.cellMeters == 2.0f);
    CHECK(T.list[size_t(T.find("embankment"))].promenade == 7.0f && T.list[size_t(T.find("embankment"))].textures.at("stone") == "textures/quay.png");
    CHECK(T.list[size_t(T.find("granite_quay"))].parapet == "stone" && T.list[size_t(T.find("granite_quay"))].promenade == 7.0f);
    CHECK(T.list[size_t(T.find("dunes"))].widthMax == 40.0f && T.list[size_t(T.find("dunes"))].props[0].model == "models/hut.glb");
    std::string err;
    try { wtr::loadShoreTypes(Json::parse(R"({"lava": {"kind": "volcano"}})")); } catch (const std::exception& e) { err = e.what(); }
    CHECK(err.find("shore type 'lava': unknown kind 'volcano'") != std::string::npos);
    err.clear();
    try { wtr::parseNetwork(Json::parse(R"({"bodies": [{"id": 3, "kind": "river", "points": [[0,0,20],[9,9,20]], "shores": [{"from": 0, "to": 10, "type": "x"}]}]})"), B); }
    catch (const std::exception& e) { err = e.what(); }
    CHECK(err == "water body 3: unknown shore type 'x' (water_types lists them)");
    // round trip
    const wtr::Network n = seaNetwork(B);
    const wtr::Network m = wtr::parseNetwork(wtr::networkToJson(n), B);
    CHECK(m.bodies.size() == 1 && m.bodies[0].kind == wtr::Kind::Sea && m.bodies[0].points.size() == 7 && m.bodies[0].side == "south");
}

TEST(water_strait_has_embankments_on_both_banks) {
    const wtr::ShoreTypes T = wtr::loadShoreTypes(Json());
    wtr::Network n = wtr::parseNetwork(Json::parse(R"({"bodies": [{"id": 1, "kind": "strait",
        "points": [[-2, 60, 60], [50, 66, 70], [100, 58, 60], [130, 62, 60]]}]})"), T);
    const wtr::Geometry g = wtr::buildGeometry(n, 128, 128, T.cellMeters);
    CHECK(g.banks.size() == 2);
    wtr::classify(n, g, T, [](int, int) { return false; }, 128, 128);
    for (int side : {-1, 1}) {
        const wtr::Bank* b = wtr::findBank(g, 0, side);
        CHECK(b && b->length > 400);
        if (!b) continue;
        CHECK(std::fabs(shoreMetres(n.bodies[0], T, side, wtr::ShoreKind::Embankment) - b->length) < 1.0f);
    }
    // water between the banks, land outside
    const auto wet = wtr::waterCells(n, g, 128, 128, T.cellMeters);
    CHECK(wet[size_t(62) * 128 + 50] == 1 && wet[size_t(20) * 128 + 50] == 0 && wet[size_t(100) * 128 + 50] == 0);
    // about 70 m / 4 m = 17.5 cells wide at x = 50 (plus a touched cell each side)
    int across = 0;
    for (int z = 0; z < 128; ++z) across += wet[size_t(z) * 128 + 50];
    CHECK(across >= 17 && across <= 21);
}

TEST(water_river_embankment_only_by_the_town) {
    const wtr::ShoreTypes T = wtr::loadShoreTypes(Json());
    wtr::Network n = wtr::parseNetwork(Json::parse(R"({"bodies": [{"id": 2, "kind": "river", "points": [[64, -1, 30], [60, 60, 36], [66, 129, 30]]}]})"), T);
    const wtr::Geometry g = wtr::buildGeometry(n, 128, 128, T.cellMeters);
    // a town on the east bank in the north half
    wtr::classify(n, g, T, [](int x, int z) { return x >= 66 && x < 90 && z < 60; }, 128, 128);
    const float east = shoreMetres(n.bodies[0], T, 1, wtr::ShoreKind::Embankment) + shoreMetres(n.bodies[0], T, -1, wtr::ShoreKind::Embankment);
    const float natural = shoreMetres(n.bodies[0], T, 1, wtr::ShoreKind::Natural) + shoreMetres(n.bodies[0], T, -1, wtr::ShoreKind::Natural);
    CHECK(east > 150 && east < 330);
    CHECK(natural > 600);
    CHECK(shoreMetres(n.bodies[0], T, 1, wtr::ShoreKind::Cliff) + shoreMetres(n.bodies[0], T, -1, wtr::ShoreKind::Cliff) == 0);
}

TEST(water_sea_shore_mixes_embankment_cliffs_and_beaches) {
    const wtr::ShoreTypes T = wtr::loadShoreTypes(Json());
    auto run = [&] {
        wtr::Network n = seaNetwork(T);
        const wtr::Geometry g = wtr::buildGeometry(n, 256, 128, T.cellMeters);
        wtr::classify(n, g, T, [](int x, int z) { return x >= 20 && x < 70 && z >= 70 && z < 92; }, 256, 128);   // the port town
        return std::make_pair(n, g);
    };
    auto [n, g] = run();
    CHECK(g.banks.size() == 1 && g.banks[0].length > 900);
    const wtr::Body& B = n.bodies[0];
    const float e = shoreMetres(B, T, 1, wtr::ShoreKind::Embankment), c = shoreMetres(B, T, 1, wtr::ShoreKind::Cliff), b = shoreMetres(B, T, 1, wtr::ShoreKind::Beach);
    CHECK(e > 100 && c > 60 && b > 100);
    CHECK(std::fabs(e + c + b - g.banks[0].length) < 1.0f);          // the whole shore is covered
    for (auto& s : B.shores) CHECK(s.to - s.from >= 29.9f);          // no scraps
    for (size_t i = 1; i < B.shores.size(); ++i) CHECK(B.shores[i].type != B.shores[i - 1].type && std::fabs(B.shores[i].from - B.shores[i - 1].to) < 0.01f);
    // the town's stretch is the embankment
    CHECK(wtr::shoreAt(n, T, 0, 1, 160) == T.find("embankment"));
    // the headland at x = 130 is cliff, the map ends are cliffs
    float headS = 0, best = 1e9f;
    for (auto& p : g.banks[0].pts) if (std::fabs(p.x - 130) < best) { best = std::fabs(p.x - 130); headS = p.s; }
    CHECK(T.list[size_t(wtr::shoreAt(n, T, 0, 1, headS))].kind == wtr::ShoreKind::Cliff);
    CHECK(T.list[size_t(wtr::shoreAt(n, T, 0, 1, g.banks[0].length - 5))].kind == wtr::ShoreKind::Cliff);
    // deterministic
    auto [n2, g2] = run();
    CHECK(wtr::networkToJson(n).dump(0) == wtr::networkToJson(n2).dump(0));
    // cliffs block land, never water
    const auto wet = wtr::waterCells(n, g, 256, 128, T.cellMeters);
    const auto cl = wtr::cliffCells(n, g, T, 256, 128);
    int blocked = 0, both = 0;
    for (size_t i = 0; i < cl.size(); ++i) { blocked += cl[i]; both += cl[i] && wet[i]; }
    CHECK(blocked > 50);
    (void)both;
    CHECK(wet[size_t(120) * 256 + 10] == 1 && wet[size_t(50) * 256 + 10] == 0);
}

#ifdef AIVERSE_TESTS_EDITOR_CORE
TEST(water_editor_bake_and_restore) {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / ("aiverse_water_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    fs::create_directories(root);
    fs::copy(fs::path(AIVERSE_TEMPLATES_DIR) / "skirmish", root / "skirmish", fs::copy_options::recursive);
    editor::Workspace ws;
    ws.engineDir = AIVERSE_SOURCE_DIR;
    std::string err;
    CHECK(ws.openGame((root / "skirmish").string(), &err));
    const std::vector<char> before = ws.map.data.cells;
    const int W = ws.map.data.w, H = ws.map.data.h;
    // a river across the whole map splits the land in two
    Json body = Json::parse(R"({"kind": "river", "points": [[0, 0, 24], [0, 0, 24]]})");
    body.member("points").at(0) = Json::parse("[" + std::to_string(W / 2) + ", -1, 24]");
    body.member("points").at(1) = Json::parse("[" + std::to_string(W / 2) + ", " + std::to_string(H + 1) + ", 24]");
    Json q = Json::object(); q.set("body", body);
    const Json r = ws.call("water_set", q);
    CHECK(r["waterCells"].integer() >= H * 6);
    CHECK(r["landParts"].integer() == 2 && r.has("warning"));
    CHECK(ws.map.data.cell(W / 2, H / 2) == '~');
    CHECK(ws.map.data.extra["waterNetwork"]["bodies"].size() == 1);
    const Json shores = ws.map.data.extra["waterNetwork"]["bodies"][size_t(0)]["shores"];
    CHECK(shores.isArray() && shores.size() >= 2);
    // a hand-drawn segment is kept over auto
    Json s = Json::parse(R"({"id": 1, "side": 1, "from": 10, "to": 60, "type": "embankment_stone"})");
    ws.call("water_shores", s);
    bool manual = false;
    for (auto& g : ws.map.data.extra["waterNetwork"]["bodies"][size_t(0)]["shores"].items())
        manual |= g.getStr("type") == "embankment_stone" && !g.getBool("auto") && g["from"].toDouble() == 10.0;
    CHECK(manual);
    ws.call("water_bake", Json::object());
    CHECK(ws.map.data.cell(W / 2, H / 2) == '~');
    // deleting the body gives the cells back exactly
    Json del = Json::object(); del.set("id", Json::number(int64_t(1)));
    ws.call("water_delete", del);
    CHECK(ws.map.data.cells == before);
    CHECK(!ws.map.data.extra.has("waterNetwork"));
    const Json bad = ws.call("water_shores", Json::parse(R"({"id": 9, "reset": true})"));
    CHECK(bad.getStr("error").find("no water body 9") != std::string::npos);
    CHECK(!r.has("error"));
    fs::remove_all(root);
}
#endif
