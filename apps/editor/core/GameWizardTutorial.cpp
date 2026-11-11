// Game wizard, tutorials. The hard tutorial is a map of its own (a copy of the template's first map) where
// scripts/wizard/tutorial.lua leads the player through steps written here from the game's own rules: what the workers
// gather, what they build, who trains whom, which buildings take troops. Three shapes cover the templates:
//   economy   workers + resources + a producer (Warcraft, Red Alert, StarCraft, AoE ...): gather, build, train, attack
//   garrison  buildings that hold troops (Mushroom Wars, State.io): send to a neutral one, take more, take the enemy's
//   generic   anything else: move the army, attack
// The soft tutorial is hints in ordinary matches (scripts/view/wizard/hints.lua) from scripts/wizard/hints_config.lua.
#include "GameWizardImpl.h"

#include <algorithm>
#include <filesystem>
#include <functional>

namespace fs = std::filesystem;

namespace aiverse::editor::wiz {

namespace {

Json strs(std::initializer_list<std::string> v) { Json a = Json::array(); for (auto& s : v) a.push(Json::string(s)); return a; }
Json strv(const std::vector<std::string>& v) { Json a = Json::array(); for (auto& s : v) a.push(Json::string(s)); return a; }
Json num(int64_t v) { return Json::number(v); }

std::map<std::string, int> countEntities(const Json& map, int owner) {
    std::map<std::string, int> n;
    for (auto& e : map["entities"].items())
        if (int(e["owner"].integer(0)) == owner) n[e.getStr("type")]++;
    return n;
}

Json costOf(const TypeInfo& t, int times) {
    Json g = Json::object();
    for (auto& [res, v] : t.cost) g.set(res, num(v * times));
    return g;
}

std::string resName(GameMaker::Ctx& c, const std::string& id, const std::string& lang) {
    if (auto it = c.texts.find("resource." + id); it != c.texts.end()) {
        if (auto l = it->second.find(lang); l != it->second.end()) return l->second;
    }
    Json l = readJsonFile(c.dir + "/lang/" + lang + ".json");
    if (l["resource." + id].isString()) return l["resource." + id].str();
    const Json& r = c.rules["resources"][id];
    return r.isObject() ? r.getStr("name", id) : id;
}

struct StepText { std::string ru, en; };

void addStep(GameMaker::Ctx& c, Json step, const StepText& t) {
    const std::string key = "wiz.tut." + step.getStr("id");
    step.set("text", Json::string(key));
    c.text(key, t.ru, t.en);
    c.tutorialSteps.push(step);
}

} // namespace

void makeTutorial(GameMaker::Ctx& c) {
    const std::string mode = c.spec.tutorial;
    if (mode == "none" || mode == "soft") { c.did("обучение: без отдельной карты (" + mode + ")"); return; }
    if (c.baseMap.size() == 0) { c.warn("обучение: у шаблона нет карты"); return; }
    const RulesInfo& I = c.info;
    // what player 1 starts with: its map objects + its faction's start kit
    auto mine = countEntities(c.baseMap, 1);
    const auto neutral = countEntities(c.baseMap, 0);
    const std::string faction = c.baseMap["players"].size() ? c.baseMap["players"][0].getStr("faction") : std::string();
    if (!faction.empty())
        for (auto& k : c.rules["factions"][faction]["start"].items()) mine[k.getStr("type")] += int(k["count"].integer(1));
    auto has = [&](const std::map<std::string, int>& m, const std::string& id) { auto it = m.find(id); return it != m.end() && it->second > 0; };
    auto N = [&](const std::string& id, const std::string& lang) { return c.typeName(id, lang); };
    std::vector<std::string> enemyStructures, combat;
    for (auto& t : I.types) {
        if (t.structure && !t.node) enemyStructures.push_back(t.id);
        if (t.combat && t.mobile && !t.worker && has(mine, t.id)) combat.push_back(t.id);
    }
    if (combat.empty()) for (auto& t : I.types) if (t.combat && t.mobile && !t.worker && t.forFaction(faction)) combat.push_back(t.id);
    const std::string title = c.spec.title;
    addStep(c, [&] { Json s = Json::object(); s.set("id", Json::string("intro"));
                     Json d = Json::object(); d.set("next", Json::fromBool(true)); s.set("done", d); return s; }(),
            {"Добро пожаловать в «" + title + "»! Пара минут — и вы знаете главное. Нажмите «Далее».",
             "Welcome to " + title + "! A couple of minutes and you know the basics. Press Next."});

    // ---------------------------------------------------------------- economy: who builds (a worker or a sidebar structure)
    const TypeInfo* W = nullptr;          // worker of the kit
    const TypeInfo* deployer = nullptr;   // a kit unit that deploys into a builder (Red Alert MCV)
    const TypeInfo* yard = nullptr;       // a structure that produces structures (sidebar building)
    for (auto& t : I.types) if (t.worker && t.mobile && has(mine, t.id) && t.forFaction(faction)) { W = &t; break; }
    if (!W)   // a builder that gathers nothing (Total Annihilation's commander, a constructor)
        for (auto& t : I.types) if (t.mobile && !t.builds.empty() && has(mine, t.id) && t.forFaction(faction)) { W = &t; break; }
    auto buildsStructures = [&](const TypeInfo& t) {
        for (auto& p : t.produces) { const auto* x = I.find(p); if (x && x->structure) return true; }
        return false;
    };
    for (auto& [id, n] : mine) { (void)n; const auto* t = I.find(id); if (t && t->structure && buildsStructures(*t)) { yard = t; break; } }
    if (!yard)
        for (auto& [id, n] : mine) {
            (void)n;
            const auto* t = I.find(id);
            const auto* to = t && !t->deploysTo.empty() ? I.find(t->deploysTo) : nullptr;
            if (to && buildsStructures(*to)) { deployer = t; yard = to; break; }
        }
    bool garrison = false;
    for (auto& t : I.types) garrison |= t.entersGarrison;
    // Knights and Merchants: buildings need roads and carried goods, a step script can't lead that yet
    if (c.rules.has("roadNetwork")) { W = nullptr; yard = nullptr; }
    if (W || yard) {
        c.archetype = "economy";
        if (deployer) {
            Json s = Json::object();
            s.set("id", Json::string("deploy"));
            Json f = Json::object(); f.set("type", Json::string(deployer->id)); s.set("focus", f);
            s.set("allow", strs({"deploy", "move"}));
            Json d = Json::object(); d.set("command", Json::string("deploy")); s.set("done", d);
            Json a = Json::object(); a.set("type", Json::string("deploy")); a.set("units", Json::string(deployer->id)); a.set("bare", Json::fromBool(true));
            s.set("auto", a);
            addStep(c, s, {"Выделите «" + N(deployer->id, "ru") + "» и разверните его: так появится «" + N(yard->id, "ru") + "», из которого строится база.",
                           "Select the " + N(deployer->id, "en") + " and deploy it: it becomes the " + N(yard->id, "en") + " that builds your base."});
            mine[yard->id]++;
        }
        // what to gather: a resource with a node on the map
        std::string res, node;
        if (W)
            for (auto& r : W->gathers) {
                for (auto& t : I.types) if (t.node && t.nodeResource == r && has(neutral, t.id)) { res = r; node = t.id; break; }
                if (!res.empty()) break;
            }
        if (!res.empty()) {
            Json s = Json::object();
            s.set("id", Json::string("gather"));
            Json f = Json::object(); f.set("type", Json::string(W->id)); s.set("focus", f);
            s.set("allow", strs({"gather", "smart", "move"}));
            Json d = Json::object(); d.set("event", Json::string("gathered")); d.set("owner", Json::string("me")); d.set("count", num(1)); s.set("done", d);
            Json a = Json::object(); a.set("type", Json::string("gather")); a.set("units", Json::string(W->id));
            Json tg = Json::object(); tg.set("owner", Json::string("neutral")); tg.set("type", Json::string(node)); a.set("target", tg);
            s.set("auto", a);
            addStep(c, s, {"Выделите работника («" + N(W->id, "ru") + "») и укажите ему «" + N(node, "ru") + "»: он начнёт добывать «" +
                               resName(c, res, "ru") + "». Дождитесь первой доставки.",
                           "Select a worker (" + N(W->id, "en") + ") and point at the " + N(node, "en") + ": it will gather " +
                               resName(c, res, "en") + ". Wait for the first delivery."});
        }
        // what can be built, and how
        std::vector<std::string> buildable;
        if (W) buildable = W->builds;
        else for (auto& p : yard->produces) { const auto* x = I.find(p); if (x && x->structure) buildable.push_back(p); }
        auto canBuild = [&](const std::string& id) {
            const auto* t = I.find(id);
            return t && t->forFaction(faction) && std::find(buildable.begin(), buildable.end(), id) != buildable.end();
        };
        auto buildStep = [&](const TypeInfo& s, const std::string& id, const std::string& ru, const std::string& en, int k) {
            Json st = Json::object();
            st.set("id", Json::string(id));
            Json f = Json::object(); f.set("type", Json::string(W ? W->id : yard->id)); st.set("focus", f);
            st.set("allow", W ? strs({"build", "smart", "move", "gather"}) : strs({"train", "place", "cancel"}));
            st.set("param", Json::string(s.id));
            st.set("give", costOf(s, 1));
            Json d = Json::object(); d.set("event", Json::string("built")); d.set("type", Json::string(s.id)); d.set("count", num(1)); st.set("done", d);
            Json a = Json::object();
            a.set("type", Json::string(W ? "build" : "train"));
            a.set("units", Json::string(W ? W->id : yard->id));
            a.set("param", Json::string(s.id));
            Json near = Json::array(); near.push(num(k % 2 ? -6 : 6)); near.push(num(k < 2 ? 6 : -6)); a.set("near", near);
            if (!W) { a.set("bare", Json::fromBool(true)); a.set("place", Json::fromBool(true)); }
            st.set("auto", a);
            const std::string howRu = W ? "Выделите работника, выберите «" + N(s.id, "ru") + "» в меню стройки и укажите место рядом с базой."
                                        : "Выделите «" + N(yard->id, "ru") + "», закажите «" + N(s.id, "ru") + "», а когда будет готово — поставьте рядом с базой.";
            const std::string howEn = W ? "Select a worker, pick the " + N(s.id, "en") + " in the build menu and place it near the base."
                                        : "Select the " + N(yard->id, "en") + ", order the " + N(s.id, "en") + " and place it near the base when it is ready.";
            addStep(c, st, {ru + " " + howRu + " Ресурсы мы выдали.", en + " " + howEn + " We gave you the resources."});
            mine[s.id]++;
        };
        int builtN = 0;
        // warp-style builders: structures stand only in a power field (StarCraft pylons)
        if (W && W->buildMode == "warp")
            for (auto& b : buildable) {
                const auto* s = I.find(b);
                if (!s || !s->powerField || !canBuild(b) || has(mine, b)) continue;
                buildStep(*s, "power", "Здания ставятся только в поле «" + N(s->id, "ru") + "». Постройте его.",
                          "Buildings stand only in the field of a " + N(s->id, "en") + ". Build one.", builtN++);
                break;
            }
        // a producer of combat units: fewest missing requirements first
        auto producerOf = [&](const TypeInfo& s) -> const TypeInfo* {
            for (auto& u : s.produces) { const auto* t = I.find(u); if (t && t->combat && t->mobile && !t->worker && !t->air && t->forFaction(faction) && t->needs.empty()) return t; }
            for (auto& u : s.produces) { const auto* t = I.find(u); if (t && t->combat && t->mobile && !t->worker && t->forFaction(faction) && t->needs.empty()) return t; }
            return nullptr;
        };
        const TypeInfo *Pr = nullptr, *C = nullptr;
        for (auto& [id, n] : mine) { (void)n; const auto* s = I.find(id); if (s && s->structure && (C = producerOf(*s))) { Pr = s; break; } }
        std::vector<const TypeInfo*> chain;   // to build, in order
        if (!Pr) {
            size_t best = 99;
            for (auto& b : buildable) {
                const auto* s = I.find(b);
                const TypeInfo* u = s && canBuild(b) ? producerOf(*s) : nullptr;
                if (!u) continue;
                std::vector<const TypeInfo*> need;
                std::function<bool(const TypeInfo&, int)> addReq = [&](const TypeInfo& t, int depth) {
                    for (auto& r : t.needs) {
                        if (has(mine, r)) continue;
                        const auto* rt = I.find(r);
                        if (!rt || !canBuild(r) || depth > 3 || !addReq(*rt, depth + 1)) return false;
                        if (std::find(need.begin(), need.end(), rt) == need.end()) need.push_back(rt);
                    }
                    return true;
                };
                if (!addReq(*s, 0)) continue;
                if (need.size() < best) { best = need.size(); Pr = s; C = u; chain = need; chain.push_back(s); }
            }
        }
        if (Pr && C) {
            // room for two units? (supply)
            int cap = 0, used = 0;
            for (auto& [id, n] : mine) { const auto* t = I.find(id); if (t) { cap += t->supply * n; used += t->supplyCost * n; } }
            if (cap > 0 && C->supplyCost > 0 && cap - used < 2 * C->supplyCost) {
                const TypeInfo* best = nullptr;   // the cheapest house, not a town hall
                int64_t bestCost = 0;
                for (auto& b : buildable) {
                    const auto* s = I.find(b);
                    if (!s || s->supply <= 0 || !canBuild(b)) continue;
                    int64_t sum = 0;
                    for (auto& [r, v] : s->cost) sum += v;
                    if (!s->produces.empty()) sum += 1000000;
                    if (!best || sum < bestCost) { best = s; bestCost = sum; }
                }
                if (best) buildStep(*best, "supply", "Войскам нужно место: постройте «" + N(best->id, "ru") + "».",
                                    "Your army needs room: build a " + N(best->id, "en") + ".", builtN++);
            }
            for (size_t i = 0; i < chain.size(); ++i) {
                const bool last = i + 1 == chain.size();
                buildStep(*chain[i], last ? "build" : "build_" + chain[i]->id,
                          last ? "Постройте «" + N(chain[i]->id, "ru") + "» — там обучают войска." : "Для казарм нужно «" + N(chain[i]->id, "ru") + "». Постройте его.",
                          last ? "Build a " + N(chain[i]->id, "en") + ": it trains troops." : "Troops need a " + N(chain[i]->id, "en") + " first. Build it.",
                          builtN++);
            }
            Json s = Json::object();
            s.set("id", Json::string("train"));
            Json f = Json::object(); f.set("type", Json::string(Pr->id)); s.set("focus", f);
            s.set("allow", strs({"train", "rally", "gather", "smart", "place"}));
            s.set("param", Json::string(C->id));
            s.set("give", costOf(*C, 2));
            Json d = Json::object(); d.set("event", Json::string("trained")); d.set("type", Json::string(C->id)); d.set("owner", Json::string("me")); d.set("count", num(2)); s.set("done", d);
            Json a = Json::object(); a.set("type", Json::string("train")); a.set("units", Json::string(Pr->id)); a.set("param", Json::string(C->id)); a.set("bare", Json::fromBool(true));
            s.set("auto", a);
            addStep(c, s, {"Выделите «" + N(Pr->id, "ru") + "» и обучите двух «" + N(C->id, "ru") + "».",
                           "Select the " + N(Pr->id, "en") + " and train two " + N(C->id, "en") + "."});
            combat.insert(combat.begin(), C->id);
        }
    } else if (garrison) {
        // ---------------------------------------------------------------- garrison
        c.archetype = "garrison";
        std::vector<std::string> holds;
        for (auto& t : I.types) if (t.garrison) holds.push_back(t.id);
        bool neutralHolds = false;
        for (auto& h : holds) neutralHolds |= has(neutral, h);
        Json focus = Json::object(); focus.set("type", strv(holds)); focus.set("owner", Json::string("me")); focus.set("kind", Json::string("structure"));
        Json allow = strs({"send", "smart", "move", "attack", "upgrade"});
        if (neutralHolds) {
            for (int k = 0; k < 2; ++k) {
                Json s = Json::object();
                s.set("id", Json::string(k == 0 ? "capture" : "capture_more"));
                s.set("focus", focus);
                s.set("allow", allow);
                Json d = Json::object(); d.set("event", Json::string("captured")); d.set("owner", Json::string("me")); d.set("count", num(k == 0 ? 1 : 2)); s.set("done", d);
                Json a = Json::object(); a.set("type", Json::string("send")); a.set("units", strv(holds)); a.set("param", num(100));
                Json tg = Json::object(); tg.set("owner", Json::string("neutral")); tg.set("kind", Json::string("structure")); a.set("target", tg);
                s.set("auto", a);
                if (k == 0) addStep(c, s, {"Это ваше здание: в нём растут войска. Проведите от него к нейтральному зданию (или выделите и нажмите на цель) — отряд пойдёт захватывать.",
                                           "This is your building: troops grow inside. Drag from it to a neutral building (or select it and tap the target) to send a squad."});
                else addStep(c, s, {"Отлично! Больше зданий — быстрее растёт армия. Захватите ещё два.",
                                    "Great! More buildings, faster growth. Take two more."});
            }
        }
        Json s = Json::object();
        s.set("id", Json::string("attack"));
        Json f = Json::object(); f.set("owner", Json::string("enemy")); f.set("kind", Json::string("structure")); s.set("focus", f);
        s.set("allow", allow);
        s.set("free", Json::fromBool(true));
        Json d = Json::object(); d.set("event", Json::string("captured")); d.set("owner", Json::string("me")); d.set("from", Json::string("enemy")); d.set("count", num(1)); s.set("done", d);
        Json a = Json::object(); a.set("type", Json::string("send")); a.set("units", strv(holds)); a.set("param", num(100));
        Json tg = Json::object(); tg.set("owner", Json::string("enemy")); tg.set("kind", Json::string("structure")); a.set("target", tg);
        s.set("auto", a);
        addStep(c, s, {"Враг тоже растёт. Соберите силы и захватите его здание — отправляйте войска из нескольких зданий сразу.",
                       "The enemy grows too. Gather your forces and take one of its buildings: send troops from several buildings at once."});
    } else if (c.rules["autoBuild"].isObject()) {
        // ---------------------------------------------------------------- auto-build (lane battlers: a BUILD button, waves go by themselves)
        c.archetype = "autobuild";
        const Json& ab = c.rules["autoBuild"];
        Json give = Json::object();
        for (auto& [res, v] : ab["cost"].members()) give.set(res, num(v.integer(0) * 4));
        for (int k = 0; k < 2; ++k) {
            Json s = Json::object();
            s.set("id", Json::string(k == 0 ? "autobuild" : "autobuild_more"));
            Json f = Json::object(); f.set("owner", Json::string("me")); f.set("kind", Json::string("structure")); s.set("focus", f);
            s.set("allow", strs({"auto_build", "upgrade"}));
            if (k == 0) s.set("give", give);
            Json d = Json::object(); d.set("command", Json::string("auto_build")); d.set("count", num(k == 0 ? 1 : 2)); s.set("done", d);
            Json a = Json::object(); a.set("type", Json::string("auto_build")); a.set("bare", Json::fromBool(true)); s.set("auto", a);
            if (k == 0) addStep(c, s, {"Нажмите «Строить»: рядом с базой появится здание, и оно само будет выпускать войска на линию с каждой волной.",
                                       "Press Build: a building appears next to your base and sends troops down the lane with every wave."});
            else addStep(c, s, {"Больше зданий — больше войск в каждой волне. Постройте ещё два.", "More buildings, bigger waves. Build two more."});
        }
        // an upgrade, when the buildings have one
        for (auto& [id, t] : c.rules["types"].members()) {
            if (!t["upgrades"].isArray() || t["upgrades"].size() == 0) continue;
            bool pooled = false;
            for (auto& x : ab["pool"].items()) pooled |= x.str() == id;
            if (!pooled) continue;
            const std::string to = t["upgrades"][0].str();
            const auto* ti = I.find(to);
            Json s = Json::object();
            s.set("id", Json::string("upgrade"));
            Json f = Json::object(); f.set("owner", Json::string("me")); f.set("kind", Json::string("structure"));
            Json types = Json::array();
            for (auto& [id2, t2] : c.rules["types"].members()) if (t2["upgrades"].isArray() && t2["upgrades"].size()) types.push(Json::string(id2));
            f.set("type", types); s.set("focus", f);
            s.set("allow", strs({"upgrade", "auto_build"}));
            if (ti) s.set("give", costOf(*ti, 1));
            Json d = Json::object(); d.set("event", Json::string("upgraded")); d.set("owner", Json::string("me")); d.set("count", num(1)); s.set("done", d);
            Json a = Json::object(); a.set("type", Json::string("upgrade")); a.set("units", types); a.set("param", Json::string(to)); a.set("bare", Json::fromBool(true));
            s.set("auto", a);
            addStep(c, s, {"Выделите здание и улучшите его: улучшенное выпускает больше войск.", "Select a building and upgrade it: upgraded ones send more troops."});
            break;
        }
        Json s = Json::object();
        s.set("id", Json::string("hold"));
        Json f = Json::object(); f.set("owner", Json::string("enemy")); f.set("kind", Json::string("structure")); s.set("focus", f);
        s.set("allow", strs({"auto_build", "upgrade", "player_ability", "ability"}));
        s.set("free", Json::fromBool(true));
        Json d = Json::object(); d.set("event", Json::string("unit_died")); d.set("owner", Json::string("enemy")); d.set("count", num(6)); s.set("done", d);
        addStep(c, s, {"Враг тоже строит. Стройте и улучшайте здания — ваши волны сами пойдут на его ядро. Уничтожьте 6 вражеских бойцов.",
                       "The enemy builds too. Keep building and upgrading: your waves push to its core by themselves. Destroy 6 enemy troops."});
    } else {
        c.archetype = "generic";
        Json s = Json::object();
        s.set("id", Json::string("move"));
        Json f = Json::object(); f.set("owner", Json::string("me")); f.set("kind", Json::string("unit")); s.set("focus", f);
        s.set("allow", strs({"move", "attack", "smart", "patrol", "stance", "formation"}));
        Json d = Json::object(); d.set("command", strs({"move", "attack", "patrol"})); s.set("done", d);
        Json a = Json::object(); a.set("type", Json::string("move")); a.set("units", strv(combat)); Json near = Json::array(); near.push(num(4)); near.push(num(0)); a.set("near", near);
        s.set("auto", a);
        addStep(c, s, {"Выделите свои войска (рамкой или нажатием) и укажите точку на карте — они пойдут туда.",
                       "Select your troops (drag a box or tap) and point at the map: they move there."});
    }
    // ---------------------------------------------------------------- the first fight (economy / generic)
    if (c.archetype == "economy" || c.archetype == "generic") {
        Json s = Json::object();
        s.set("id", Json::string("attack"));
        // the target: an enemy building that doesn't shoot back (a house, not a tower or a town centre with arrows)
        std::vector<std::string> soft;
        for (auto& t : I.types) if (t.structure && !t.node && !t.combat && !t.garrison) soft.push_back(t.id);
        Json f = Json::object(); f.set("owner", Json::string("enemy")); f.set("kind", Json::string("structure"));
        if (!soft.empty()) f.set("type", strv(soft));
        s.set("focus", f);
        s.set("allow", strs({"attack", "move", "smart", "patrol", "stance", "formation", "train", "gather", "build"}));
        s.set("weaken", num(80));         // the enemy keeps waiting (its units defend themselves): a first fight to win
        s.set("weaken_units", num(50));
        Json d = Json::object(); d.set("event", Json::string("unit_died")); d.set("owner", Json::string("enemy"));
        if (!enemyStructures.empty()) { d.set("type", strv(enemyStructures)); d.set("anyIfNone", Json::fromBool(true)); }
        d.set("count", num(1)); s.set("done", d);
        Json a = Json::object(); a.set("type", Json::string("attack")); a.set("units", strv(combat));
        Json tg = Json::object(); tg.set("owner", Json::string("enemy")); tg.set("kind", Json::string("structure"));
        if (!soft.empty()) tg.set("type", strv(soft));
        a.set("target", tg);
        s.set("auto", a);
        addStep(c, s, {"Армия готова! Выделите воинов и атакуйте отмеченное вражеское здание.",
                       "Your army is ready! Select your soldiers and attack the marked enemy building."});
    }

    // ---------------------------------------------------------------- files
    Json cfg = Json::object();
    cfg.set("player", num(1));
    cfg.set("mode", Json::string("hard"));
    cfg.set("skippable", Json::fromBool(c.spec.tutorialSkippable));
    cfg.set("dim", Json::fromBool(c.spec.tutorialDim));
    cfg.set("always", strs({"stop", "cancel", "hold", "rally"}));
    cfg.set("steps", c.tutorialSteps);
    std::string err;
    if (!writeText(c.path("scripts/wizard/tutorial_steps.lua"),
                   "-- Tutorial steps (game wizard: written from this game's rules; edit freely). Runtime and keys: scripts/wizard/tutorial.lua\n"
                   "-- Texts: lang/<language>.json wiz.tut.<id>. archetype: " + c.archetype + "\nreturn " + luaLiteral(cfg) + "\n", &err))
        throw std::runtime_error(err);
    // the map: the template's first map + the tutorial scripts
    Json m = c.baseMap;
    m.erase("generate");
    m.set("name", Json::string(c.tr("Обучение", "Tutorial")));
    Json sc = m["scripts"].isArray() ? m["scripts"] : Json::array();
    sc.push(Json::string("scripts/wizard/tutorial.lua"));
    m.set("scripts", sc);
    Json vs = m["viewScripts"].isArray() ? m["viewScripts"] : Json::array();
    vs.push(Json::string("scripts/view/wizard/tutorial_view.lua"));
    m.set("viewScripts", vs);
    if (!writeText(c.path("maps/wiz_tutorial.json"), m.dump() + "\n", &err)) throw std::runtime_error(err);
    // hidden commands of the panel
    Json& cmds = c.rules.member("commands");
    for (const char* id : {"wiz_tut_next", "wiz_tut_skip", "wiz_tut_off"}) {
        Json d = Json::object();
        d.set("label", Json::string(id));
        d.set("card", Json::fromBool(false));
        d.set("fn", Json::string("wiz_tutorial_command"));
        cmds.set(id, d);
    }
    c.saveRules();
    c.tutorialMap = 1;
    c.did("обучение: " + std::to_string(c.tutorialSteps.size()) + " шагов (" + c.archetype + "), карта maps/wiz_tutorial.json");
}

void makeHints(GameMaker::Ctx& c) {
    if (!c.spec.hints || !(c.spec.tutorial == "both" || c.spec.tutorial == "soft")) return;
    const RulesInfo& I = c.info;
    Json hints = Json::array();
    auto add = [&](Json h, const std::string& ru, const std::string& en) {
        const std::string key = "wiz.hint." + h.getStr("id");
        h.set("text", Json::string(key));
        c.text(key, ru, en);
        hints.push(h);
    };
    auto H = [](const char* id, const char* when) { Json h = Json::object(); h.set("id", Json::string(id)); h.set("when", Json::string(when)); return h; };
    { Json h = H("camera", "start"); h.set("delay", num(3));
      add(h, "Камера: тяните карту (на телефоне — пальцем), колесо мыши или щипок — приблизить и отдалить.",
             "Camera: drag the map (a finger on phones), the mouse wheel or a pinch zooms."); }
    std::vector<std::string> workers, combat, supply;
    bool garrison = false;
    for (auto& t : I.types) {
        if (t.worker && t.mobile) workers.push_back(t.id);
        if (t.combat && t.mobile) combat.push_back(t.id);
        if (t.supply > 0) supply.push_back(t.id);
        garrison |= t.entersGarrison;
    }
    if (!workers.empty()) {
        Json h = H("workers", "idle"); h.set("type", strv(workers)); h.set("after", num(15));
        add(h, "Работники стоят без дела — отправьте их добывать: ресурсы решают всё.", "Workers are idle: send them to gather. Resources decide everything.");
        Json n = H("resources", "event"); n.set("event", Json::string("not_enough")); n.set("owner", Json::string("me"));
        add(n, "Не хватает ресурсов. Больше работников — быстрее добыча.", "Not enough resources. More workers gather faster.");
    }
    if (!supply.empty()) {
        Json h = H("supply", "supply");
        add(h, "Лимит войск исчерпан: постройте «" + c.typeName(supply[0], "ru") + "», чтобы обучать больше.",
               "Unit limit reached: build a " + c.typeName(supply[0], "en") + " to train more.");
    }
    if (!combat.empty()) {
        Json h = H("army", "event"); h.set("event", Json::string("trained")); h.set("owner", Json::string("me")); h.set("type", strv(combat));
        add(h, "Выделяйте войска рамкой, двойной клик — все такого типа. Укажите врага, чтобы атаковать.",
               "Drag a box to select troops, double click selects every unit of a type. Point at an enemy to attack.");
    }
    if (garrison) {
        Json h = H("send", "start"); h.set("delay", num(6));
        add(h, "Проведите от своего здания к другому — уйдёт часть войск. Чем больше зданий, тем быстрее растёт армия.",
               "Drag from your building to another to send troops. More buildings grow a bigger army.");
        Json g = H("captured", "event"); g.set("event", Json::string("captured")); g.set("owner", Json::string("me"));
        add(g, "Здание ваше! Не оставляйте его пустым — враг заберёт.", "The building is yours! Don't leave it empty, the enemy will take it.");
    }
    { Json h = H("under_attack", "event"); h.set("event", Json::string("alert")); h.set("owner", Json::string("me"));
      add(h, "Нас атакуют! Мини-карта показывает, где.", "We are under attack! The minimap shows where."); }
    Json cfg = Json::object();
    cfg.set("duration", num(7));
    cfg.set("hints", hints);
    std::string err;
    if (!writeText(c.path("scripts/wizard/hints_config.lua"),
                   "-- Soft tutorial hints (game wizard: written from this game's rules; edit freely). Runtime: scripts/view/wizard/hints.lua\n"
                   "return " + luaLiteral(cfg) + "\n", &err))
        throw std::runtime_error(err);
    Json vs = c.rules["viewScripts"].isArray() ? c.rules["viewScripts"] : Json::array();
    vs.push(Json::string("scripts/view/wizard/hints.lua"));
    c.rules.set("viewScripts", vs);
    c.saveRules();
    c.did("подсказки: " + std::to_string(hints.size()));
}

} // namespace aiverse::editor::wiz
