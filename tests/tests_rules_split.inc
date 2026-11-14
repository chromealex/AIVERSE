// rules.json split over files ("include") and inherited entries ("extends" / "abstract"):
// engine/data/JsonCompose.h, docs/game-format.md § Splitting rules.json
#include "engine/data/JsonCompose.h"

namespace rules_split {
namespace fs = std::filesystem;

struct Dir {
    fs::path root;
    explicit Dir(const char* tag)
        : root(fs::temp_directory_path() / (std::string("aiverse_rules_") + tag + "_" +
                                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()))) {
        fs::create_directories(root);
    }
    ~Dir() { std::error_code ec; fs::remove_all(root, ec); }
    void put(const std::string& rel, const std::string& text) const {
        fs::create_directories((root / rel).parent_path());
        std::ofstream(root / rel, std::ios::binary) << text;
    }
    std::string get(const std::string& rel) const {
        std::ifstream f(root / rel, std::ios::binary);
        return std::string(std::istreambuf_iterator<char>(f), {});
    }
    std::string path(const std::string& rel) const { return (root / rel).generic_string(); }
};

std::string errorOf(const std::function<void()>& fn) {
    try { fn(); } catch (const std::exception& e) { return e.what(); }
    return "";
}

// the skirmish rules with its "types" moved to rules/types.json (half) and rules/more/units.json (half)
void splitSkirmish(const Dir& d) {
    Json r = Json::parseFile(kSkirmish + "/rules.json");
    Json a = Json::object(), b = Json::object();
    size_t i = 0;
    for (auto& [k, v] : r["types"].members()) ((i++ % 2) ? b : a).set(k, v);
    r.erase("types");
    Json inc = Json::array();
    inc.push(Json::string("rules/types.json"));
    r.set("include", inc);
    Json ta = Json::object(); ta.set("types", a);
    Json more = Json::array(); more.push(Json::string("more/units.json"));
    ta.set("include", more);
    Json tb = Json::object(); tb.set("types", b);
    d.put("rules.json", r.dump());
    d.put("rules/types.json", ta.dump());
    d.put("rules/more/units.json", tb.dump());
}

uint64_t botChecksum(const Json& rules, const Json& map, uint32_t ticks) {
    SimConfig cfg; cfg.seed = 7;
    Simulation sim(cfg, std::make_unique<game::RtsModule>());
    sim.setup(rules, map);
    std::vector<game::RtsAI> bots;
    for (auto& p : sim.players()) bots.emplace_back(p.slot, 7);
    for (uint32_t t = 0; t < ticks && sim.result() == 0; ++t) {
        for (auto& b : bots) b.update(sim);
        sim.step();
    }
    return sim.checksum();
}
} // namespace rules_split

TEST(rules_include_merges_files) {
    using namespace rules_split;
    Dir d("include");
    splitSkirmish(d);
    JsonOrigin o;
    const Json composed = composeJson(d.path("rules.json"), fileReader(), &o);
    const Json whole = Json::parseFile(kSkirmish + "/rules.json");
    CHECK(o.files.size() == 3 && o.files[1].find("rules/types.json") != std::string::npos && o.files[2].find("more/units.json") != std::string::npos);
    CHECK(!composed.has("include") && composed["types"].size() == whole["types"].size());
    for (auto& [k, v] : whole["types"].members()) CHECK(composed["types"][k] == v);
    // the same game: same rules, same match
    const Json map = Json::parseFile(kSkirmish + "/maps/valley.json");
    CHECK(botChecksum(composed, map, 900) == botChecksum(whole, map, 900));
    // every file is part of the data a match depends on
    CHECK(composedFiles(d.path("rules.json"), fileReader()).size() == 3);
    const uint64_t h0 = hashGameData(d.path("rules.json"), kSkirmish + "/maps/valley.json");
    d.put("rules/more/units.json", d.get("rules/more/units.json") + "\n");
    CHECK(hashGameData(d.path("rules.json"), kSkirmish + "/maps/valley.json") != h0);
    // a key defined twice names both files
    Dir e("clash");
    e.put("rules.json", R"({"include": ["a.json"], "types": {"x": {}}, "supplyCap": 10})");
    e.put("a.json", R"({"types": {"x": {}}})");
    std::string err = errorOf([&] { composeJson(e.path("rules.json"), fileReader()); });
    CHECK(err.find("types.x") != std::string::npos && err.find("a.json") != std::string::npos && err.find("rules.json") != std::string::npos);
    e.put("a.json", R"({"supplyCap": 20})");
    CHECK(errorOf([&] { composeJson(e.path("rules.json"), fileReader()); }).find("supplyCap") != std::string::npos);
    e.put("a.json", R"({"list": [{"id": "p"}], "include": "b.json"})");
    e.put("b.json", R"({"list": [{"id": "q"}, {"id": "p"}]})");
    CHECK(errorOf([&] { composeJson(e.path("rules.json"), fileReader()); }).find("list.#p") != std::string::npos);
    e.put("b.json", R"({"list": [{"id": "q"}], "include": "a.json"})");   // a -> b -> a
    CHECK(errorOf([&] { composeJson(e.path("rules.json"), fileReader()); }).find("itself") != std::string::npos);
    e.put("a.json", R"({"include": "../outside.json"})");
    CHECK(errorOf([&] { composeJson(e.path("rules.json"), fileReader()); }).find("outside") != std::string::npos);
    e.put("a.json", R"({"include": "missing.json"})");
    CHECK(errorOf([&] { composeJson(e.path("rules.json"), fileReader()); }).find("not found") != std::string::npos);
    // arrays append in reading order
    e.put("a.json", R"({"list": [{"id": "p"}], "include": "b.json"})");
    e.put("b.json", R"({"list": [{"id": "q"}]})");
    e.put("rules.json", R"({"include": ["a.json"], "list": [{"id": "o"}]})");
    const Json l = composeJson(e.path("rules.json"), fileReader())["list"];
    CHECK(l.size() == 3 && l[0].getStr("id") == "o" && l[1].getStr("id") == "p" && l[2].getStr("id") == "q");
}

TEST(rules_extends_inherits_entries) {
    using namespace rules_split;
    Json doc = Json::parse(R"({
        "types": {
            "soldier": {"abstract": true, "health": {"max": 100, "armor": 1}, "move": {"speed": 2}, "tags": ["inf"]},
            "elite":   {"abstract": true, "extends": "soldier", "health": {"armor": 3}},
            "footman": {"extends": "soldier", "name": "Footman"},
            "knight":  {"extends": ["elite"], "move": {"speed": 3}, "tags": ["cav"]}
        },
        "abilities": [
            {"id": "bolt", "abstract": true, "cooldown": 5, "effects": [{"type": "damage", "amount": 10}]},
            {"id": "bolt2", "extends": "bolt", "cooldown": 4}
        ],
        "victory": {"mode": "x"}
    })");
    CHECK(needsExtends(doc));
    const Json r = resolveExtends(doc);
    CHECK(!needsExtends(r));
    CHECK(!r["types"].has("soldier") && !r["types"].has("elite") && r["types"].size() == 2);
    const Json& f = r["types"]["footman"];
    CHECK(f.getStr("name") == "Footman" && f["health"].getInt("max") == 100 && f["move"].getInt("speed") == 2 && !f.has("extends") && !f.has("abstract"));
    const Json& k = r["types"]["knight"];   // deep merge: health.max from soldier, armor from elite, own speed; arrays replace
    CHECK(k["health"].getInt("max") == 100 && k["health"].getInt("armor") == 3 && k["move"].getInt("speed") == 3);
    CHECK(k["tags"].size() == 1 && k["tags"][0].str() == "cav");
    CHECK(r["abilities"].size() == 1 && r["abilities"][0].getStr("id") == "bolt2" && r["abilities"][0].getInt("cooldown") == 4 &&
          r["abilities"][0]["effects"][0].getInt("amount") == 10);
    CHECK(r["victory"] == doc["victory"]);
    // readable errors
    std::string err = errorOf([] { resolveExtends(Json::parse(R"({"types": {"a": {"extends": "nope"}}})")); });
    CHECK(err.find("types 'a'") != std::string::npos && err.find("nope") != std::string::npos);
    err = errorOf([] { resolveExtends(Json::parse(R"({"types": {"a": {"extends": "b"}, "b": {"extends": "a"}}})")); });
    CHECK(err.find("cycle") != std::string::npos);
    // the loader and the simulation apply it: skirmish with a shared abstract base is the same game
    Json whole = Json::parseFile(kSkirmish + "/rules.json");
    Json based = whole;
    Json& types = *based.find("types");
    Json base = whole["types"]["footman"];
    base.set("abstract", Json::fromBool(true));
    Json foot = Json::object();
    foot.set("extends", Json::string("_infantry"));
    foot.set("name", whole["types"]["footman"]["name"]);
    types.set("footman", foot);
    types.set("_infantry", base);
    const game::Rules a = game::loadRules(whole), b = game::loadRules(based);
    CHECK(a.types.size() == b.types.size() && b.findType("_infantry") < 0 && b.findType("footman") >= 0);
    CHECK(b.types[b.findType("footman")].maxHp == a.types[a.findType("footman")].maxHp);
    const Json map = Json::parseFile(kSkirmish + "/maps/valley.json");
    CHECK(botChecksum(based, map, 900) == botChecksum(whole, map, 900));
}

TEST(rules_split_editor_saves_back) {
    using namespace rules_split;
    Dir d("editor");
    for (auto& e : fs::recursive_directory_iterator(kSkirmish)) {   // a copy of the template with split rules
        const fs::path rel = fs::relative(e.path(), kSkirmish);
        if (e.is_directory()) fs::create_directories(d.root / rel);
        else fs::copy_file(e.path(), d.root / rel, fs::copy_options::overwrite_existing);
    }
    splitSkirmish(d);
    const std::string rootBefore = d.get("rules.json"), typesBefore = d.get("rules/types.json");
    editor::Workspace ws;
    ws.engineDir = AIVERSE_SOURCE_DIR;
    std::string err;
    CHECK(ws.openGame(d.root.string(), &err));
    CHECK(ws.project.rulesFiles().size() == 3 && ws.project.rules.findType("peasant") >= 0);
    // a type from rules/more/units.json: only that file is rewritten
    const std::string inMore = Json::parse(d.get("rules/more/units.json"))["types"].members().front().first;
    Json p = Json::object();
    p.set("path", Json::string("types." + inMore + ".sight"));
    p.set("value", Json::number(int64_t(11)));
    Json r = ws.call("rules_set", p);
    CHECK(!r.has("error"));
    CHECK(Json::parse(d.get("rules/more/units.json"))["types"][inMore].getInt("sight") == 11);
    CHECK(d.get("rules.json") == rootBefore && d.get("rules/types.json") == typesBefore);
    // a new type goes next to the last type (the file holding the previous entry of "types")
    p.set("path", Json::string("types.scout"));
    p.set("value", ws.project.rulesJson["types"]["peasant"]);
    CHECK(!ws.call("rules_set", p).has("error"));
    const std::string lastType = ws.project.rulesJson["types"].members()[ws.project.rulesJson["types"].size() - 2].first;
    const bool lastInMore = Json::parse(d.get("rules/more/units.json"))["types"].has(lastType);
    CHECK(Json::parse(d.get(lastInMore ? "rules/more/units.json" : "rules/types.json"))["types"].has("scout"));
    CHECK(!Json::parse(d.get("rules.json")).has("types") && Json::parse(d.get("rules.json")).has("include"));
    // rules_get shows the files; a game file write to a rules part is refused
    Json g = ws.call("rules_get", Json::parse(R"({"path": "resources"})"));
    CHECK(g["files"].size() == 3);
    Json w = ws.call("game_file_write", Json::parse(R"({"path": "rules/types.json", "text": "{}"})"));
    CHECK(w.has("error") && w.getStr("error").find("rules") != std::string::npos);
    // reopened: the same rules
    editor::Workspace ws2;
    ws2.engineDir = AIVERSE_SOURCE_DIR;
    CHECK(ws2.openGame(d.root.string(), &err) && ws2.project.rules.findType("scout") >= 0 &&
          ws2.project.rules.types.size() == ws.project.rules.types.size());
}
