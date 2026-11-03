// Game wizard, around the matches: the kit files, levels (generated maps of rising difficulty, every N-th a boss),
// the meta config the menus read (ui/wizard.json, profile, shop, screens), purchases and ads, the language files,
// a check of everything with bots, and the report (WIZARD.md).
#include "GameWizardImpl.h"

#include <algorithm>
#include <filesystem>
#include <memory>
#include "SimTools.h"
#include "engine/pack/FileCopy.h"
#include "engine/sim/Simulation.h"
#include "modules/rts/RtsAI.h"
#include "modules/rts/RtsModule.h"
#include "modules/rts/RtsScript.h"

namespace fs = std::filesystem;

namespace rts::editor::wiz {

namespace {
Json num(int64_t v) { return Json::number(v); }
Json gift(const std::string& a, int64_t x, const std::string& b = "", int64_t y = 0) {
    Json g = Json::object();
    if (!a.empty() && x) g.set(a, num(x));
    if (!b.empty() && y) g.set(b, num(y));
    return g;
}
bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
std::string soft(const GameMaker::Ctx& c) { return c.spec.currencies.empty() ? "gold" : c.spec.currencies[0]; }
std::string hard(const GameMaker::Ctx& c) { return c.spec.currencies.size() > 1 ? c.spec.currencies[1] : ""; }
} // namespace

void copyKit(GameMaker::Ctx& c) {
    std::error_code ec;
    if (!fs::is_directory(c.kitDir, ec)) throw std::runtime_error("нет набора мастера: " + c.kitDir);
    int n = 0;
    for (auto it = fs::recursive_directory_iterator(c.kitDir, ec); !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const fs::path rel = fs::relative(it->path(), c.kitDir, ec);
        const std::string r = rel.generic_string();
        if (r == "README.md" || r.rfind("lang/", 0) == 0) continue;   // languages are merged (writeLanguages)
        if (r.rfind("ui/screens/", 0) == 0 && c.spec.tutorial == "none" && false) continue;
        const fs::path to = fs::path(c.dir) / rel;
        fs::create_directories(to.parent_path(), ec);
        rts::pack::copyFileReplaceOrThrow(it->path(), to);
        ++n;
    }
    // menus: ui/main.lua loads the wizard's meta first (an existing main.lua keeps its code after it)
    const std::string mainLua = c.path("ui/main.lua");
    const std::string old = readText(mainLua);
    if (old.find("wizard_meta") == std::string::npos)
        writeText(mainLua, "-- Game wizard: the meta game (ui/wizard_meta.lua, config ui/wizard.json)\nrequire('wizard_meta')\n" + old);
    c.did("набор мастера: " + std::to_string(n) + " файлов (templates/_wizard)");
}

void makeLevels(GameMaker::Ctx& c) {
    Json maps = Json::array();
    const Json oldMaps = c.manifest["maps"];
    auto addMap = [&](const std::string& file, const std::string& title) {
        Json e = Json::object();
        e.set("file", Json::string(file));
        e.set("title", Json::string(title));
        maps.push(e);
        return int(maps.size());
    };
    if (c.tutorialMap) c.tutorialMap = addMap("maps/wiz_tutorial.json", c.tr("Обучение", "Tutorial"));
    if (c.tutorialMap) c.text("map.wiz_tutorial.title", "Обучение", "Tutorial");
    std::string preset = c.plan.getStr("levelPreset");
    if (preset.find(' ') != std::string::npos) preset.clear();   // "copies of the base map"
    if (c.info.planets) preset.clear();
    const uint32_t seed0 = c.spec.seed ? uint32_t(c.spec.seed) : hashText(c.spec.title + "|" + c.spec.idea);
    // size: the base map's, scaled
    Json size;
    if (c.baseMap["size"].isArray() && c.spec.mapSize != "auto" && c.spec.mapSize != "medium") {
        const int k = c.spec.mapSize == "small" ? 75 : 140;
        size = Json::array();
        for (size_t i = 0; i < 2 && i < c.baseMap["size"].size(); ++i)
            size.push(num(std::clamp<int64_t>(c.baseMap["size"][i].integer(64) * k / 100, 16, 1024)));
    }
    const int players = std::max<int>(2, int(c.baseMap["players"].size()));
    const std::string s = soft(c), h = hard(c);
    for (int i = 1; i <= c.spec.levels; ++i) {
        const bool boss = c.spec.bossEvery > 0 && i % c.spec.bossEvery == 0;
        int diff = 1;
        if (c.spec.difficultyCurve == "easy") diff = i <= 3 ? 0 : i <= 7 ? 1 : 2;
        else if (c.spec.difficultyCurve == "hard") diff = i <= 1 ? 0 : i <= 3 ? 1 : 2;
        else diff = i <= 2 ? 0 : i <= 5 ? 1 : 2;
        if (boss) diff = 2;
        Json m = c.baseMap;
        const std::string file = "maps/wiz_level_" + std::to_string(i) + ".json";
        m.set("name", Json::string(c.tr("Уровень ", "Level ") + std::to_string(i)));
        if (!preset.empty()) {
            Json g = Json::object();
            g.set("preset", Json::string(preset));
            g.set("seed", num(int64_t((seed0 + uint32_t(i) * 7919u) % 1000000u)));
            g.set("players", num(players));
            g.set("template", Json::string(c.baseMapFile));
            if (size.isArray()) { Json p = Json::object(); p.set("size", size); g.set("params", p); }
            m.set("generate", g);
        }
        std::string err;
        if (!writeText(c.path(file), m.dump() + "\n", &err)) throw std::runtime_error(err);
        const std::string ru = std::string(boss ? "Босс: " : "") + "Уровень " + std::to_string(i);
        const std::string en = std::string(boss ? "Boss: " : "") + "Level " + std::to_string(i);
        const int idx = addMap(file, c.tr(ru, en));
        c.text("map.wiz_level_" + std::to_string(i) + ".title", ru, en);
        Json lv = Json::object();
        lv.set("map", num(idx));
        lv.set("difficulty", num(diff));
        lv.set("boss", Json::fromBool(boss));
        lv.set("reward", gift(s, 40 + 20 * i, boss ? h : "", boss ? 3 : 0));
        lv.set("firstReward", gift(s, 80 + 40 * i, h, boss ? 10 : (i % 2 ? 1 : 0)));
        Json st = Json::array(); st.push(num(240 + 30 * i)); st.push(num(480 + 60 * i));
        lv.set("stars", st);
        c.levels.push_back(lv);
    }
    if (c.spec.keepTemplateMaps || c.spec.levels == 0)
        for (auto& e : oldMaps.items()) maps.push(e);
    c.manifest.set("maps", maps);
    c.saveManifest();
    c.did("уровни: " + std::to_string(c.spec.levels) + (preset.empty() ? " (копии карты шаблона)" : " (генератор «" + preset + "»)") +
          (c.spec.bossEvery ? ", босс каждый " + std::to_string(c.spec.bossEvery) + "-й" : ""));
}

void makeMeta(GameMaker::Ctx& c) {
    const std::string s = soft(c), h = hard(c);
    // profile defaults
    Json prof = readJsonFile(c.path("ui/profile.json"));
    if (!prof.isObject()) prof = Json::object();
    auto def = [&](const char* k, Json v) { if (!prof.has(k)) prof.set(k, std::move(v)); };
    def("name", Json::string(""));
    def("level", num(1));
    def("xp", num(0));
    for (size_t i = 0; i < c.spec.currencies.size(); ++i) def(c.spec.currencies[i].c_str(), num(i == 0 ? 100 : 5));
    { Json st = Json::object(); st.set("matches", num(0)); st.set("wins", num(0)); def("stats", st); }
    { Json t = Json::object(); t.set("done", Json::fromBool(c.tutorialMap == 0)); def("tutorial", t); }
    { Json t = Json::object(); t.set("hints", Json::fromBool(c.spec.hints)); def("settings", t); }
    def("upgrades", Json::object());
    def("stars", Json::object());
    def("difficulty", num(1));
    writeText(c.path("ui/profile.json"), prof.dump() + "\n");
    // the config of ui/wizard_meta.lua
    Json w = Json::object();
    Json cur = Json::array();
    for (auto& id : c.spec.currencies) { Json e = Json::object(); e.set("id", Json::string(id)); cur.push(e); }
    w.set("currencies", cur);
    if (c.tutorialMap) {
        Json t = Json::object();
        t.set("map", num(c.tutorialMap));
        t.set("forced", Json::fromBool(c.spec.tutorialForced));
        t.set("reward", gift(s, 150, h, 5));
        w.set("tutorial", t);
    }
    Json lv = Json::array();
    for (auto& l : c.levels) lv.push(l);
    w.set("levels", lv);
    w.set("winReward", gift(s, 50));
    w.set("lossReward", gift(s, 15));
    w.set("xpWin", num(50));
    w.set("xpLoss", num(15));
    w.set("xpPerLevel", num(c.spec.xpPerLevel));
    w.set("hints", Json::fromBool(c.spec.hints));
    w.set("shop", Json::fromBool(c.spec.shop));
    // upgrades: what the match can feel (scripts/wizard/meta_bonus.lua)
    Json ups = Json::array(), simUps = Json::array();
    if (c.spec.upgrades) {
        bool combat = false, mobile = false;
        for (auto& t : c.info.types) { combat |= t.combat; mobile |= t.mobile; }
        auto up = [&](const std::string& id, const std::string& ru, const std::string& en, const std::string& dru, const std::string& den,
                      Json simEntry, int cost) {
            Json u = Json::object();
            u.set("id", Json::string(id));
            u.set("name", Json::string("@wiz.up." + id));
            u.set("description", Json::string("@wiz.up." + id + ".d"));
            u.set("cost", gift(s, cost));
            u.set("growth", num(cost * 3 / 4));
            u.set("max", num(5));
            ups.push(u);
            c.text("wiz.up." + id, ru, en);
            c.text("wiz.up." + id + ".d", dru, den);
            simEntry.set("id", Json::string(id));
            simUps.push(simEntry);
        };
        Json e = Json::object();
        e.set("stat", Json::string("maxHp")); e.set("percent", num(10));
        up("hp", "Крепость", "Toughness", "+10% здоровья всем вашим войскам и зданиям за уровень", "+10% health for all your units and buildings per level", e, 100);
        if (combat) {
            Json d = Json::object(); d.set("stat", Json::string("damage")); d.set("percent", num(8));
            up("dmg", "Сила", "Strength", "+8% урона за уровень", "+8% damage per level", d, 120);
        }
        if (mobile) {
            Json sp = Json::object(); sp.set("stat", Json::string("speed")); sp.set("percent", num(5));
            up("speed", "Марш", "March", "+5% скорости войск за уровень", "+5% unit speed per level", sp, 90);
        }
        if (!c.info.resources.empty()) {
            const std::string r = c.info.resources[0];
            Json rs = Json::object(); rs.set("resource", Json::string(r)); rs.set("amount", num(50));
            up("start", "Запасы", "Supplies", "+50 к стартовому ресурсу за уровень", "+50 starting resource per level", rs, 80);
        }
        Json mc = Json::object();
        mc.set("player", num(1));
        mc.set("upgrades", simUps);
        writeText(c.path("scripts/wizard/meta_config.lua"),
                  "-- Meta upgrades in the match (game wizard; the menu's ui/wizard.json \"upgrades\" share the ids). Runtime: scripts/wizard/meta_bonus.lua\n"
                  "return " + luaLiteral(mc) + "\n");
        Json sc = c.rules["scripts"].isArray() ? c.rules["scripts"] : Json::array();
        bool have = false;
        for (auto& x : sc.items()) have |= x.str() == "scripts/wizard/meta_bonus.lua";
        if (!have) sc.push(Json::string("scripts/wizard/meta_bonus.lua"));
        c.rules.set("scripts", sc);
        c.saveRules();
    }
    w.set("upgrades", ups);
    if (c.spec.daily) {
        Json d = Json::object();
        d.set("enabled", Json::fromBool(true));
        Json rw = Json::array();
        const int64_t days[7] = {50, 75, 100, 125, 150, 200, 300};
        for (int i = 0; i < 7; ++i) rw.push(gift(s, days[i], (i == 2 || i == 6) ? h : "", i == 6 ? 5 : 1));
        d.set("rewards", rw);
        w.set("daily", d);
    }
    Json ads = Json::object();
    ads.set("interstitialEvery", num(c.spec.ads ? c.spec.interstitialEvery : 0));
    ads.set("rewardedDouble", Json::fromBool(c.spec.ads));
    w.set("ads", ads);
    writeText(c.path("ui/wizard.json"), w.dump() + "\n");
    // shop
    Json shop = Json::object();
    Json items = Json::array(), products = Json::array(), rewarded = Json::array();
    auto item = [&](Json& list, const std::string& id, const std::string& ru, const std::string& en, Json price, Json give, bool once = false) {
        Json it = Json::object();
        it.set("id", Json::string(id));
        it.set("name", Json::string("@wiz.shop." + id));
        if (price.isObject()) it.set("price", price);
        it.set("give", give);
        if (once) it.set("once", Json::fromBool(true));
        list.push(it);
        c.text("wiz.shop." + id, ru, en);
        return &list.at(list.size() - 1);
    };
    if (!h.empty()) {
        item(items, "soft_small", "Мешок ресурсов", "A bag of riches", gift(h, 10), gift(s, 500));
        item(items, "soft_big", "Сундук ресурсов", "A chest of riches", gift(h, 25), gift(s, 1500));
    }
    if (c.spec.purchases && !h.empty()) {
        item(products, "hard_small", "Горсть", "A handful", Json(), gift(h, 50));
        item(products, "hard_big", "Гора", "A mountain", Json(), gift(h, 300));
        if (c.spec.ads) { Json g = Json::object(); g.set("no_ads", Json::fromBool(true)); item(products, "no_ads", "Без рекламы", "No ads", Json(), g, true); }
    }
    if (c.spec.ads) {
        Json* r = item(rewarded, "free_soft", "Бесплатно за рекламу", "Free for an ad", Json(), gift(s, 100));
        r->set("cooldown", num(600));
    }
    shop.set("items", items);
    if (products.size()) shop.set("products", products);
    if (rewarded.size()) shop.set("rewarded", rewarded);
    writeText(c.path("ui/shop.json"), shop.dump() + "\n");
    // screens
    Json sj = Json::object();
    sj.set("start", Json::string("home"));
    sj.set("afterMatch", Json::string("result"));
    Json scr = Json::object();
    for (const char* p : {"daily", "dialog"}) { Json x = Json::object(); x.set("popup", Json::fromBool(true)); scr.set(p, x); }
    sj.set("screens", scr);
    writeText(c.path("ui/screens.json"), sj.dump() + "\n");
    // currency names
    for (auto& id : c.spec.currencies) {
        if (id == "gold") c.text("currency.gold", "Золото", "Gold");
        else if (id == "gems") c.text("currency.gems", "Кристаллы", "Gems");
        else if (id == "coins") c.text("currency.coins", "Монеты", "Coins");
        else if (id == "stars") c.text("currency.stars", "Звёзды", "Stars");
        else { std::string n = id; if (!n.empty()) n[0] = char(std::toupper((unsigned char)n[0])); c.text("currency." + id, n, n); }
    }
    const std::string first = c.idea.substr(0, c.idea.find_first_of(".!\n"));
    if (!c.spec.idea.empty() && first.size() > 3 && first.size() < 90) c.text("wiz.tagline", first, first);
    c.did("мета: главное меню, уровни, " + std::string(c.spec.upgrades ? "улучшения, " : "") + (c.spec.shop ? "магазин, " : "") +
          (c.spec.daily ? "награда дня, " : "") + "профиль, настройки, итоги матча");
}

void makeMonetization(GameMaker::Ctx& c) {
    if (!c.spec.ads && !c.spec.purchases) { c.did("монетизация: нет"); return; }
    const auto& P = c.spec.platforms;
    Json plugins = c.manifest["plugins"].isObject() ? c.manifest["plugins"] : Json::object();
    std::vector<std::string> stores = c.spec.stores;
    if (stores.empty()) {
        if (contains(P, "android")) stores.push_back("googleplay");
        if (contains(P, "ios")) stores.push_back("appstore");
        if (contains(P, "web")) stores.push_back("yandex-games");
    }
    if (c.spec.purchases) {
        Json prov = Json::object();
        auto pick = [&](const char* platform, std::initializer_list<const char*> ids) {
            Json a = Json::array();
            for (const char* id : ids) if (contains(stores, id)) a.push(Json::string(id));
            if (a.size()) prov.set(platform, a);
        };
        pick("android", {"googleplay", "rustore"});
        pick("ios", {"appstore"});
        pick("web", {"yandex-games"});
        pick("desktop", {"steam"});
        Json d = Json::array(); d.push(Json::string("mock-store")); if (!prov.has("desktop")) prov.set("desktop", d);
        for (auto& s : stores) if (!plugins.has(s)) plugins.set(s, Json::object());
        plugins.set("mock-store", Json::object());
        Json store = Json::object();
        store.set("providers", prov);
        Json prods = Json::array();
        for (auto [id, type] : {std::pair{"hard_small", "consumable"}, {"hard_big", "consumable"}, {"no_ads", "nonConsumable"}}) {
            if (std::string(id) == "no_ads" && !c.spec.ads) continue;
            Json p = Json::object(); p.set("id", Json::string(id)); p.set("type", Json::string(type)); prods.push(p);
        }
        store.set("products", prods);
        c.manifest.set("store", store);
        if (contains(stores, "rustore")) c.warn("RuStore: впишите consoleAppId в game.json plugins.rustore");
        if (contains(stores, "steam")) c.warn("Steam: впишите appId и items в game.json plugins.steam");
    }
    if (c.spec.ads) {
        Json prov = Json::object();
        Json a = Json::array(); a.push(Json::string("yandex-ads"));
        if (contains(P, "android")) prov.set("android", a);
        if (contains(P, "ios")) prov.set("ios", a);
        if (contains(P, "web")) { Json y = Json::array(); y.push(Json::string("yandex-games")); prov.set("web", y); plugins.set("yandex-games", Json::object()); }
        Json m = Json::array(); m.push(Json::string("mock-ads")); prov.set("desktop", m);
        Json ya = Json::object();   // Yandex demo units: work out of the box in test builds; replace with yours
        ya.set("interstitial", Json::string("demo-interstitial-yandex"));
        ya.set("rewarded", Json::string("demo-rewarded-yandex"));
        ya.set("banner", Json::string("demo-banner-yandex"));
        plugins.set("yandex-ads", ya);
        plugins.set("mock-ads", Json::object());
        Json ads = Json::object();
        ads.set("providers", prov);
        c.manifest.set("ads", ads);
        c.warn("реклама: стоят тестовые блоки Яндекса (demo-*) — замените на свои в game.json plugins.yandex-ads");
    }
    c.manifest.set("plugins", plugins);
    c.saveManifest();
    c.did(std::string("монетизация: ") + (c.spec.purchases ? "покупки " : "") + (c.spec.ads ? "реклама" : ""));
}

void writeLanguages(GameMaker::Ctx& c) {
    for (auto& L : c.spec.languages) {
        const std::string file = c.path("lang/" + L + ".json");
        Json j = readJsonFile(file);
        if (!j.isObject()) {
            j = Json::object();
            Json en = readJsonFile(c.path("lang/en.json"));   // a new language starts from English
            if (en.isObject()) j = en;
            j.set("@language", Json::string(L));
        }
        Json kit = readJsonFile(c.kitDir + "/lang/" + L + ".json");
        if (!kit.isObject()) kit = readJsonFile(c.kitDir + "/lang/en.json");
        for (auto& [k, v] : kit.members()) j.set(k, v);
        for (auto& [k, by] : c.texts) {
            auto it = by.find(L);
            if (it == by.end()) it = by.find("en");
            if (it != by.end()) j.set(k, Json::string(it->second));
        }
        j.set("game.name", Json::string(c.spec.title));
        std::string err;
        if (!writeText(file, j.dump() + "\n", &err)) c.warn(err);
    }
    c.manifest.set("name", Json::string(c.spec.title));
    c.saveManifest();
    c.did("языки: " + [&] { std::string s; for (auto& l : c.spec.languages) s += (s.empty() ? "" : ", ") + l; return s; }());
}

// ---------------------------------------------------------------------------------------------------- check

namespace {
struct RunResult { bool ok = false; int winner = 0; double seconds = 0; std::string error; int scriptErrors = 0; std::string firstScriptError; int64_t tutStep = 0; };

RunResult runMap(Workspace& ws, const std::string& mapFile, uint32_t maxSeconds, bool autoTutorial, uint64_t seed) {
    RunResult r;
    try {
        Json map = Json::parseFile(ws.project.dir + "/" + mapFile);
        if (autoTutorial) { Json iv = Json::object(); iv.set("wiz_tutorial_auto", Json::number(int64_t(1))); map.set("initialVars", iv); }
        const Json simMap = mapForSim(map, ws.project.dir);
        SimConfig cfg;
        cfg.seed = seed;
        Simulation sim(cfg, std::make_unique<game::RtsModule>());
        sim.setup(ws.project.simRules(simMap), simMap);
        auto& mod = *dynamic_cast<game::RtsModule*>(&sim.module());
        game::AIConfig ai;
        ai.thinkTicks = 10;
        std::vector<game::RtsAI> bots;
        for (auto& pl : sim.players()) bots.emplace_back(pl.slot, cfg.seed, ai);
        const uint32_t limit = maxSeconds * sim.tickRate();
        while (sim.tick() < limit && sim.result() == 0) {
            for (auto& b : bots) b.update(sim);
            sim.step();
        }
        r.winner = sim.result();
        r.seconds = double(sim.tick()) / sim.tickRate();
        if (auto* se = mod.scripts())
            for (auto& e : se->log()) if (e.error) { if (!r.scriptErrors++) r.firstScriptError = e.text; }
        auto& vars = mod.state(sim).trig.vars;
        auto it = vars.find("wiz_tutorial_step");
        if (it != vars.end()) r.tutStep = it->second.toInt();
        r.ok = true;
    } catch (const std::exception& e) {
        r.error = e.what();
    }
    return r;
}
} // namespace

void verifyGame(Workspace& ws, GameMaker::Ctx& c) {
    Json ui = ws.call("ui_validate", Json::object(), "wizard");
    if (ui.has("error")) c.warn("ui_validate: " + ui.getStr("error"));
    else if (ui["errors"].size()) c.warn("экраны: " + std::to_string(ui["errors"].size()) + " ошибок (ui_validate), первая: " + ui["errors"][0].dump(0));
    if (!c.spec.verifyLevels) return;
    Json checks = Json::array();
    const Json& maps = c.manifest["maps"];
    for (size_t i = 0; i < maps.size(); ++i) {
        const std::string file = maps[i].getStr("file");
        const bool tut = int(i) + 1 == c.tutorialMap;
        if (!tut && file.rfind("maps/wiz_level_", 0) != 0) continue;
        if (!tut && checks.size() > 3 && file != "maps/wiz_level_" + std::to_string(c.spec.levels) + ".json" &&
            (c.spec.bossEvery <= 0 || file.find("_" + std::to_string(c.spec.bossEvery) + ".json") == std::string::npos))
            continue;   // a sample: the first levels, the first boss and the last one (the rest are the same generator)
        RunResult r = runMap(ws, file, tut ? 900 : 300, tut, 7 + i);
        Json e = Json::object();
        e.set("map", Json::string(file));
        e.set("ok", Json::fromBool(r.ok));
        e.set("seconds", Json::number(r.seconds, 1));
        e.set("winner", Json::number(int64_t(r.winner)));
        if (!r.error.empty()) { e.set("error", Json::string(r.error)); c.warn(file + ": не загружается — " + r.error); }
        if (r.scriptErrors) { e.set("scriptErrors", Json::number(int64_t(r.scriptErrors))); c.warn(file + ": ошибки скриптов: " + r.firstScriptError); }
        if (tut) {
            const int total = int(c.tutorialSteps.size());
            e.set("tutorialSteps", Json::number(int64_t(total)));
            e.set("tutorialReached", Json::number(r.tutStep));
            if (r.ok && r.winner != 1) {
                std::string at;
                if (r.tutStep >= 1 && r.tutStep <= total) at = " («" + c.tutorialSteps[size_t(r.tutStep - 1)].getStr("id") + "»)";
                c.warn("обучение: автопроверка дошла до шага " + std::to_string(r.tutStep) + " из " + std::to_string(total) + at +
                       " — бот мог не найти место или условие этого шага; пройдите обучение сами (Game → карта «Обучение») "
                       "и при необходимости поправьте scripts/wizard/tutorial_steps.lua");
            }
            else if (r.ok) c.did("проверка: обучение проходится (" + std::to_string(total) + " шагов, " + std::to_string(int(r.seconds)) + " с)");
        }
        checks.push(e);
    }
    c.plan.set("checks", checks);
    int good = 0;
    for (auto& e : checks.items()) good += e.getBool("ok") && !e.has("scriptErrors");
    c.did("проверка ботами: " + std::to_string(good) + " из " + std::to_string(checks.size()) + " карт без ошибок");
}

void writeReport(GameMaker::Ctx& c) {
    std::string md = "# " + c.spec.title + "\n\nСделано мастером «Сделать игру» (RTSEngine). Шаблон: **" + c.plan.getStr("templateName") +
                     "** (`" + c.templ + "`) — " + c.plan.getStr("reason") + ".\n\n## Что сделано\n\n";
    for (auto& d : c.done.items()) md += "- " + d.str() + "\n";
    if (c.assigned.size()) {
        md += "\n## Контент\n\n| Файл | Куда | Почему |\n|---|---|---|\n";
        for (auto& a : c.assigned.items()) md += "| " + a.getStr("file") + " | " + a.getStr("to") + " | " + a.getStr("why") + " |\n";
    }
    if (c.warnings.size()) {
        md += "\n## Проверьте\n\n";
        for (auto& w : c.warnings.items()) md += "- " + w.str() + "\n";
    }
    md += "\n## Где что лежит\n\n"
          "- Обучение: `maps/wiz_tutorial.json`, шаги `scripts/wizard/tutorial_steps.lua`, тексты `lang/*.json` (`wiz.tut.*`).\n"
          "- Подсказки в бою: `scripts/wizard/hints_config.lua` (`wiz.hint.*`).\n"
          "- Уровни: `maps/wiz_level_*.json` (генератор карт, `generate`), награды и звёзды — `ui/wizard.json` → `levels`.\n"
          "- Мета: `ui/wizard.json` (валюты, награды, улучшения, награда дня, реклама), `ui/shop.json`, `ui/profile.json`, экраны `ui/screens/`.\n"
          "- Улучшения в матче: `scripts/wizard/meta_config.lua` + `scripts/wizard/meta_bonus.lua`.\n"
          "- Покупки и реклама: `game.json` → `plugins`, `store`, `ads` (docs/plugins.md).\n\n"
          "## Дальше\n\n"
          "1. Нажмите «Играть» в окне Game — начнётся обучение, потом уровни.\n"
          "2. Замените модели и звуки, которые мастер не смог назначить (таблица выше), в Инспекторе.\n"
          "3. Подключите агента (окно Agent) и дайте ему задачу, например:\n\n"
          "> Это игра «" + c.spec.title + "», сделанная мастером из шаблона " + c.templ + ". Прочитай WIZARD.md и docs/game-wizard.md. "
          "Идея игры: " + (c.spec.idea.empty() ? std::string("(не задана)") : c.spec.idea.substr(0, 400)) +
          ". Доведи механики и баланс до идеи, проверь обучение (match_run на maps/wiz_tutorial.json) и экраны (ui_preview).\n";
    writeText(c.path("WIZARD.md"), md);
}

} // namespace rts::editor::wiz
