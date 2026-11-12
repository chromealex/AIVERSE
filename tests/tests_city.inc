// City planner (map generator preset "city", modules/rts/gen/GenCity.cpp): determinism, a connected street network,
// bridges over the river, buildings on free lots, the road graph, and a simulation that starts on the result.
namespace {
const char* kCityRules = R"J({
  "victory": "noUnits",
  "resources": {"gold": {"start": 0}},
  "topDown": {"heroes": ["guy"], "autoFire": false},
  "driving": {"vehicles": {"sedan": {"length": 1.1, "width": 0.5}}},
  "types": {
    "guy":    {"health": {"max": 100}, "move": {"speed": 2, "radius": 0.1}, "sight": 8},
    "sedan":  {"health": {"max": 300}, "move": {"speed": 0, "radius": 0.5}, "sight": 1},
    "walker": {"tags": ["pedestrian"], "health": {"max": 40}, "move": {"speed": 0.4, "radius": 0.1}, "sight": 3},
    "oak":    {"tags": ["tree"], "health": {"max": 1000}, "move": {"speed": 0, "radius": 0.1}, "sight": 0},
    "lamp":   {"tags": ["prop", "lamp"], "health": {"max": 1000}, "move": {"speed": 0, "radius": 0.05}, "sight": 0},
    "bin":    {"tags": ["prop"], "health": {"max": 1000}, "move": {"speed": 0, "radius": 0.05}, "sight": 0},
    "house":  {"tags": ["building", "residential"], "structure": {"footprint": [3, 2]}, "health": {"max": 5000}, "sight": 1},
    "shop":   {"tags": ["building", "commercial"], "structure": {"footprint": [4, 3]}, "health": {"max": 5000}, "sight": 1},
    "works":  {"tags": ["building", "industrial"], "structure": {"footprint": [5, 4]}, "health": {"max": 5000}, "sight": 1},
    "tower":  {"tags": ["building", "commercial"], "structure": {"footprint": [2, 2]}, "health": {"max": 5000}, "sight": 1}
  }
})J";

Json cityMap(uint64_t seed, const char* params = R"({"size": [96, 72], "river": true, "roundabouts": 1})", uint64_t* hash = nullptr) {
    Json rj = Json::parse(kCityRules);
    game::Rules rules = game::loadRules(rj);
    gen::GenRequest req;
    req.preset = "city"; req.seed = seed; req.players = 2; req.params = Json::parse(params);
    Json map = Json::parse(R"({"name": "c", "players": []})");
    std::string err;
    if (!gen::generateMap(req, rules, rj, map, &err)) { std::printf("  city error: %s\n", err.c_str()); return Json::object(); }
    if (hash) *hash = gen::mapHash(map);
    return map;
}
} // namespace

TEST(city_plan_is_deterministic_and_seeded) {
    uint64_t a = 0, b = 0, c2 = 0;
    cityMap(7, R"({"size": [96, 72]})", &a);
    cityMap(7, R"({"size": [96, 72]})", &b);
    cityMap(8, R"({"size": [96, 72]})", &c2);
    CHECK(a != 0 && a == b && a != c2);
    Json rj = Json::parse(kCityRules);
    bool listed = false;
    const Json presets = gen::presetList(rj);
    for (auto& p : presets.items()) listed = listed || p.getStr("id") == "city";
    CHECK(listed);
    // sizes outside 32..512 are refused with a readable error
    game::Rules rules = game::loadRules(rj);
    gen::GenRequest req; req.preset = "city"; req.params = Json::parse(R"({"size": [20, 20]})");
    Json map = Json::object();
    std::string err;
    CHECK(!gen::generateMap(req, rules, rj, map, &err) && err.find("32..512") != std::string::npos);
}

TEST(city_streets_connect_bridges_cross_the_river_and_buildings_fit) {
    for (uint64_t seed : {1ull, 5ull, 23ull}) {
        Json map = cityMap(seed);
        const int W = int(map["size"][size_t(0)].integer()), H = int(map["size"][size_t(1)].integer());
        CHECK(W == 96 && H == 72);
        const Json& city = map["city"];
        // road cells from the plan's rectangles
        std::vector<char> road(size_t(W * H), 0);
        for (auto& r : city["roads"].items()) {
            const bool ns = r.getStr("axis") == "ns";
            for (int t = int(r.getInt("from")); t < int(r.getInt("to")); ++t)
                for (int k = int(r.getInt("pos")); k < int(r.getInt("pos") + r.getInt("width")); ++k) road[size_t(ns ? t * W + k : k * W + t)] = 1;
        }
        // every road cell is passable ground, even over the river (bridges), and they form one network
        std::vector<std::string> rows;
        for (auto& r : map["terrain"]["rows"].items()) rows.push_back(r.str());
        CHECK(int(rows.size()) == H && int(rows[0].size()) == W);
        int roads = 0, water = 0, start = -1;
        for (int i = 0; i < W * H; ++i) {
            const char ch = rows[size_t(i / W)][size_t(i % W)];
            if (ch == '~') ++water;
            if (!road[size_t(i)]) continue;
            if (ch == '#') road[size_t(i)] = 0;             // roundabout islands sit inside the junction
            else { ++roads; if (start < 0) start = i; CHECK(ch == '.'); }
        }
        std::vector<int> stack{start};
        std::vector<char> seen(size_t(W * H), 0);
        seen[size_t(start)] = 1;
        int reached = 0;
        while (!stack.empty()) {
            const int i = stack.back(); stack.pop_back(); ++reached;
            const int x = i % W, z = i / W;
            const int nb[4][2] = {{x + 1, z}, {x - 1, z}, {x, z + 1}, {x, z - 1}};
            for (auto& q : nb) {
                if (q[0] < 0 || q[1] < 0 || q[0] >= W || q[1] >= H) continue;
                const int k = q[1] * W + q[0];
                if (road[size_t(k)] && !seen[size_t(k)]) { seen[size_t(k)] = 1; stack.push_back(k); }
            }
        }
        CHECK(reached == roads && roads > 600);
        CHECK(water > 100 && city["bridges"].size() >= 2 && city.has("river"));
        CHECK(map["generated"].getInt("roundabouts") >= 1 && city["junctions"].size() >= 9);
        bool tee = false, round = false;
        for (auto& j : city["junctions"].items()) { tee = tee || j.getStr("type") == "t"; round = round || j.getStr("type") == "roundabout"; }
        CHECK(tee && round);
        // buildings: inside the map, never on a road or on water, never overlapping; one hero; cars only on roads or car parks
        Json rj = Json::parse(kCityRules);
        game::Rules rules = game::loadRules(rj);
        std::vector<char> used(size_t(W * H), 0);
        int buildings = 0, heroes = 0, cars = 0;
        bool clean = true;
        for (auto& e : map["entities"].items()) {
            const game::UnitType& t = rules.types[size_t(rules.findType(e.getStr("type")))];
            const double px = e["pos"][size_t(0)].toDouble(), pz = e["pos"][size_t(1)].toDouble();
            clean = clean && px > 0 && pz > 0 && px < W && pz < H;
            if (t.id == "guy") ++heroes;
            if (t.id == "sedan") ++cars;
            if (!t.structure) continue;
            ++buildings;
            const int x0 = int(px - t.footprintW / 2.0 + 0.01), z0 = int(pz - t.footprintH / 2.0 + 0.01);
            for (int z = z0; z < z0 + t.footprintH; ++z)
                for (int x = x0; x < x0 + t.footprintW; ++x) {
                    clean = clean && x >= 0 && z >= 0 && x < W && z < H && !road[size_t(z * W + x)] && rows[size_t(z)][size_t(x)] == '.' && !used[size_t(z * W + x)];
                    if (x >= 0 && z >= 0 && x < W && z < H) used[size_t(z * W + x)] = 1;
                }
        }
        CHECK(clean && buildings >= 20 && heroes == 1 && cars >= 5);
        CHECK(map["generated"].getInt("buildings") == buildings && !map["generated"].has("warnings"));
    }
}

TEST(city_tiles_follow_the_tileset_bases_and_the_simulation_starts) {
    Json map = cityMap(3, R"({"size": [64, 48], "river": false, "tileset": "tilesets/city.json", "chars": {"road": "r"},
                              "tileBases": {"ns": 0, "ew": 16, "x15": 32, "walk": 100}})");
    const Json& tiles = map["tiles"];
    CHECK(tiles.getStr("tileset") == "tilesets/city.json" && tiles["rows"].size() == 48);
    CHECK(tiles["rows"][size_t(0)].str().find('r') != std::string::npos);
    bool nsOk = true, any = false;
    for (auto& f : tiles["fixed"].items()) {
        const int t = int(f[size_t(2)].integer());
        any = true;
        nsOk = nsOk && ((t >= 0 && t < 48) || (t >= 100 && t < 104));   // only the bases that were given
    }
    CHECK(any && nsOk && !map["city"].has("river") && map["city"]["bridges"].size() == 0);
    // the generated map is an ordinary map: the simulation loads it, the hero is adopted, cars are vehicles
    map.erase("tiles");
    auto sim = makeSimJson(kCityRules, map.dump(0), 5);
    CHECK(mod(*sim).topDown()->heroOf(*sim, 1).valid());
    CHECK(mod(*sim).driving()->store(*sim) && !mod(*sim).driving()->store(*sim)->cars.empty());
    run(*sim, 2.0);   // the match runs on it
    CHECK(sim->world().alive(mod(*sim).topDown()->heroOf(*sim, 1)));
    // a rules set without buildings still gives streets, with a warning
    Json bare = Json::parse(R"({"types": {"u": {"health": {"max": 10}, "move": {"speed": 1}}}})");
    game::Rules rules = game::loadRules(bare);
    gen::GenRequest req; req.preset = "city"; req.seed = 2;
    Json m2 = Json::object();
    std::string err;
    CHECK(gen::generateMap(req, rules, bare, m2, &err) && m2["generated"]["warnings"].size() == 3 && m2["city"]["roads"].size() > 8);
}
