// Game wizard: the spec, the catalog of templates the window offers, the choice of a template from the idea
// (planGame), the download of an art link, and the step list of GameMaker. The steps live in GameWizard*.cpp.
#include "GameWizardImpl.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include "ProjectFile.h"
#include "WizardAgent.h"
#include "Projects.h"

namespace fs = std::filesystem;

namespace aiverse::editor {

namespace wiz {

// Mechanics of every template, and the words of an idea that point to it. Stems: "гриб" matches "грибы", "грибной".
const std::vector<TemplateTraits>& templateTraits() {
    static const std::vector<TemplateTraits> t = {
        {"skirmish", "economy base army towers 3d", "warcraft варкрафт фэнтез fantasy рыцар knight орк orc крестьян peasant замок castle "
                                                    "золот gold лес wood база base классик classic средневек medieval", "classic"},
        {"mushrooms", "garrison casual mobile 3d", "гриб mushroom захват capture деревн village mushroom wars казуал casual простая simple "
                                                   "отправ send процент percent", "field"},
        {"shroomwars", "garrison heroes casual mobile 3d", "гриб mushroom герой hero кузниц forge мораль morale mushroom wars 2", "field"},
        {"sporelanes", "garrison lanes casual mobile 3d", "спор spore лини lane колонн column дорожк", "lanes"},
        {"spellbunkers", "garrison lanes spells casual mobile 3d", "заклинан spell магия magic маг mage бункер bunker книг book", "lanes"},
        {"borderrush", "garrison territory casual mobile portrait 3d", "state.io state страна country клетк cell граница border "
                                                                       "территор territory гипер hyper вертикал portrait", ""},
        {"laneclash", "lanes waves casual mobile portrait 3d", "лайн lane волн wave спавн spawn башн tower clash royale ядро core "
                                                               "вертикал portrait", ""},
        {"lastcolony", "economy base waves survival 3d", "зомби zombie орд horde выжив surviv колони colony billions волн wave "
                                                         "оборон defen стен wall", "classic"},
        {"ironfront", "economy base army modern 3d", "red alert танк tank мсв mcv руда ore совет soviet союзн allied современ modern "
                                                     "войн war армия army", "classic"},
        {"metaltide", "economy base army scifi flow 3d", "total annihilation робот robot металл metal энерги energy поток flow "
                                                         "мех mech", "metal"},
        {"starfall", "economy base army scifi races 3d", "starcraft старкрафт космос space раса race пришел alien зерг zerg протос "
                                                         "sci-fi scifi фантаст", "classic"},
        {"fourages", "economy base army ages history 3d", "age of empires эпох age истори history цивилиз civiliz древн ancient", "classic"},
        {"clanlands", "economy territory vikings 3d", "northgard викинг viking клан clan регион region зима winter", "continents"},
        {"flagsectors", "territory army flags 3d", "флаг flag сектор sector фабрик factory z ", "classic"},
        {"holdtheline", "army squads points ww2 3d", "company of heroes отряд squad точк point втор ww2 солдат soldier окоп", "classic"},
        {"hearthhaul", "economy roads settlers 3d", "knights and merchants settlers поселен settler дорог road товар goods торгов trade "
                                                    "экономик economy", "classic"},
        {"underlord", "dungeon economy 3d", "dungeon keeper подзем dungeon копа dig комнат room монстр monster зло evil", ""},
        {"planets", "planets space 3d", "планет planet орбит orbit луна moon космос space", ""},
        {"ambersands", "economy base army desert 3d", "dune дюна пустын desert спайс spice песок sand червь worm", "classic"},
        {"frostcrown", "economy base army winter 3d", "зим winter снег snow лёд лед ice север north", "classic"},
        {"tiles2d", "economy base army 2d", "2d пиксел pixel тайл tile ретро retro dune спрайт sprite", ""},
        {"km2d", "economy base army 2d", "2d спрайт sprite рисован drawn холм hill", ""},
        {"wildfire", "fields sample 3d", "огонь fire пожар wildfire лес forest", ""},
        {"scripted", "economy base army scripted 3d", "скрипт script lua", "classic"},
    };
    return t;
}

std::string lower(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        const unsigned char c = (unsigned char)s[i];
        if (c < 128) { o += char(std::tolower(c)); continue; }
        if ((c == 0xD0) && i + 1 < s.size()) {             // А-П (D0 90..9F) -> а-п (D0 B0..BF), Р-Я (D0 A0..AF) -> р-я (D1 80..8F), Ё
            const unsigned char d = (unsigned char)s[i + 1];
            if (d >= 0x90 && d <= 0x9F) { o += char(0xD0); o += char(d + 0x20); ++i; continue; }
            if (d >= 0xA0 && d <= 0xAF) { o += char(0xD1); o += char(d - 0x20); ++i; continue; }
            if (d == 0x81) { o += char(0xD1); o += char(0x91); ++i; continue; }
        }
        o += char(c);
    }
    return o;
}

std::vector<std::string> words(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    const std::string l = lower(s);
    for (size_t i = 0; i < l.size(); ++i) {
        const unsigned char c = (unsigned char)l[i];
        if (c >= 128 || std::isalnum(c)) cur += char(c);
        else if (!cur.empty()) { out.push_back(cur); cur.clear(); }
    }
    if (!cur.empty()) out.push_back(cur);
    return out;
}

uint32_t hashText(const std::string& s) {
    uint32_t h = 2166136261u;
    for (unsigned char c : s) h = (h ^ c) * 16777619u;
    return h;
}

std::string luaLiteral(const Json& j, int indent) {
    const std::string pad(size_t(indent + 2), ' '), endPad(size_t(indent), ' ');
    switch (j.type()) {
    case Json::Type::Null: return "nil";
    case Json::Type::Bool: return j.boolean() ? "true" : "false";
    case Json::Type::Number: return std::to_string(j.integer());   // the kit's configs are integers (simulation scripts)
    case Json::Type::String: {
        std::string o = "\"";
        for (char ch : j.str()) {
            if (ch == '"' || ch == '\\') { o += '\\'; o += ch; }
            else if (ch == '\n') o += "\\n";
            else o += ch;
        }
        return o + "\"";
    }
    case Json::Type::Array: {
        if (j.size() == 0) return "{}";
        bool simple = true;
        for (auto& v : j.items()) if (v.isObject() || v.isArray()) simple = false;
        std::string o = "{";
        for (size_t i = 0; i < j.size(); ++i) {
            o += simple ? (i ? ", " : "") : "\n" + pad;
            o += luaLiteral(j[i], indent + 2);
            if (!simple) o += ",";
        }
        return o + (simple ? "}" : "\n" + endPad + "}");
    }
    case Json::Type::Object: {
        if (j.size() == 0) return "{}";
        std::string o = "{";
        bool first = true;
        for (auto& [k, v] : j.members()) {
            bool ident = !k.empty() && !std::isdigit((unsigned char)k[0]);
            for (char ch : k) if (!(std::isalnum((unsigned char)ch) || ch == '_')) ident = false;
            o += (first ? " " : ", ");
            o += ident ? k : "[" + luaLiteral(Json::string(k)) + "]";
            o += " = " + luaLiteral(v, indent + 2);
            first = false;
        }
        return o + " }";
    }
    }
    return "nil";
}

Json readJsonFile(const std::string& path) {
    try { return Json::parseFile(path); } catch (...) { return Json(); }
}

std::string readText(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::stringstream ss; ss << f.rdbuf();
    return ss.str();
}

bool writeText(const std::string& path, const std::string& text, std::string* err) {
    std::error_code ec;
    fs::create_directories(fs::path(path).parent_path(), ec);
    std::ofstream f(path, std::ios::binary);
    if (!f) { if (err) *err = "не удалось записать " + path; return false; }
    f << text;
    return bool(f);
}

std::string slugOf(const std::string& title) {
    std::string o;
    for (unsigned char c : title) {
        if (std::isalnum(c)) o += char(std::tolower(c));
        else if ((c == ' ' || c == '-' || c == '_') && !o.empty() && o.back() != '_') o += '_';
    }
    while (!o.empty() && o.back() == '_') o.pop_back();
    if (o.empty() || std::isdigit((unsigned char)o[0])) o = "game" + o;
    return o;
}

const TypeInfo* RulesInfo::find(const std::string& id) const {
    for (auto& t : types) if (t.id == id) return &t;
    return nullptr;
}

RulesInfo RulesInfo::read(const Json& rules) {
    RulesInfo r;
    for (auto& [id, v] : rules["resources"].members()) { (void)v; r.resources.push_back(id); }
    for (auto& [id, t] : rules["types"].members()) {
        if (!t.isObject()) continue;
        TypeInfo ti;
        ti.id = id;
        ti.name = t.getStr("name", id);
        ti.structure = t.has("structure");
        ti.mobile = t.has("move");
        ti.combat = t.has("weapons") || t.has("weapon");
        ti.worker = t.has("gather");
        ti.node = t.has("resourceNode");
        ti.garrison = t.has("garrison");
        ti.entersGarrison = t.has("entersGarrison");
        ti.key = t.getBool("key");
        ti.waveSpawn = t.has("waveSpawn");
        ti.air = t["move"].getStr("layer") == "air";
        ti.supply = int(t["supply"].integer(0));
        if (t.has("sprite")) r.sprites2d = true;
        for (auto& p : t["produces"].items()) ti.produces.push_back(p.str());
        for (auto& p : t["builds"].items()) ti.builds.push_back(p.str());
        for (auto& [res, g] : t["gather"].members()) { (void)g; ti.gathers.push_back(res); }
        ti.nodeResource = t["resourceNode"].getStr("resource");
        for (auto& p : t["requires"].items()) if (p.isString()) ti.needs.push_back(p.str());
        for (auto& p : t["factions"].items()) if (p.isString()) ti.factions.push_back(p.str());
        ti.deploysTo = t.getStr("deploysTo");
        ti.buildMode = t.getStr("buildMode");
        ti.powerField = t.has("powerField");
        for (auto& [res, v] : t["cost"].members()) {
            if (res == "time") continue;
            if (res == "supply") { ti.supplyCost = int(v.integer(0)); continue; }
            if (!rules["resources"].has(res)) continue;   // other cost keys (growth, ...) aren't resources
            ti.cost.push_back({res, v.integer(0)});
        }
        r.types.push_back(std::move(ti));
    }
    return r;
}

} // namespace wiz

using namespace wiz;

// ---------------------------------------------------------------------------------------------------- spec

namespace {
Json strings(const std::vector<std::string>& v) { Json a = Json::array(); for (auto& s : v) a.push(Json::string(s)); return a; }
std::vector<std::string> stringsOf(const Json& j, std::vector<std::string> def) {
    if (!j.isArray()) return def;
    std::vector<std::string> o;
    for (auto& s : j.items()) if (s.isString() && !s.str().empty()) o.push_back(s.str());
    return o;
}
} // namespace

Json WizardSpec::toJson() const {
    Json j = Json::object();
    auto S = [&](const char* k, const std::string& v) { j.set(k, Json::string(v)); };
    auto B = [&](const char* k, bool v) { j.set(k, Json::fromBool(v)); };
    auto I = [&](const char* k, int v) { j.set(k, Json::number(int64_t(v))); };
    S("title", title); S("location", location); S("studio", studio); S("version", version);
    S("artDir", artDir); S("artUrl", artUrl); S("idea", idea); S("metaIdea", metaIdea);
    S("genre", genre); j.set("features", strings(features)); S("orientation", orientation);
    j.set("languages", strings(languages)); j.set("platforms", strings(platforms)); I("seed", seed);
    S("tutorial", tutorial); B("tutorialForced", tutorialForced); B("tutorialSkippable", tutorialSkippable); B("tutorialDim", tutorialDim);
    I("levels", levels); S("mapPreset", mapPreset); S("mapSize", mapSize); I("bossEvery", bossEvery);
    S("difficultyCurve", difficultyCurve); B("keepTemplateMaps", keepTemplateMaps); B("verifyLevels", verifyLevels);
    I("startResourcesPercent", startResourcesPercent);
    j.set("currencies", strings(currencies));
    B("upgrades", upgrades); B("shop", shop); B("daily", daily); B("hints", hints); B("profileScreen", profileScreen);
    I("xpPerLevel", xpPerLevel);
    B("ads", ads); I("interstitialEvery", interstitialEvery); B("purchases", purchases); j.set("stores", strings(stores));
    B("paletteFromArt", paletteFromArt); B("brandingFromArt", brandingFromArt);
    B("agent", agent); S("agentClient", agentClient);
    if (ai.isObject() && ai.size()) j.set("ai", ai);
    return j;
}

WizardSpec WizardSpec::fromJson(const Json& j) {
    WizardSpec s;
    auto S = [&](const char* k, std::string& v) { if (j[k].isString()) v = j[k].str(); };
    auto B = [&](const char* k, bool& v) { if (j[k].type() == Json::Type::Bool) v = j[k].boolean(); };
    auto I = [&](const char* k, int& v) { if (j[k].isNumber()) v = int(j[k].integer()); };
    S("title", s.title); S("location", s.location); S("studio", s.studio); S("version", s.version);
    S("artDir", s.artDir); S("artUrl", s.artUrl); S("idea", s.idea); S("metaIdea", s.metaIdea);
    S("genre", s.genre); s.features = stringsOf(j["features"], s.features); S("orientation", s.orientation);
    s.languages = stringsOf(j["languages"], s.languages); s.platforms = stringsOf(j["platforms"], s.platforms); I("seed", s.seed);
    S("tutorial", s.tutorial); B("tutorialForced", s.tutorialForced); B("tutorialSkippable", s.tutorialSkippable); B("tutorialDim", s.tutorialDim);
    I("levels", s.levels); S("mapPreset", s.mapPreset); S("mapSize", s.mapSize); I("bossEvery", s.bossEvery);
    S("difficultyCurve", s.difficultyCurve); B("keepTemplateMaps", s.keepTemplateMaps); B("verifyLevels", s.verifyLevels);
    I("startResourcesPercent", s.startResourcesPercent);
    s.currencies = stringsOf(j["currencies"], s.currencies);
    B("upgrades", s.upgrades); B("shop", s.shop); B("daily", s.daily); B("hints", s.hints); B("profileScreen", s.profileScreen);
    I("xpPerLevel", s.xpPerLevel);
    B("ads", s.ads); I("interstitialEvery", s.interstitialEvery); B("purchases", s.purchases); s.stores = stringsOf(j["stores"], s.stores);
    B("paletteFromArt", s.paletteFromArt); B("brandingFromArt", s.brandingFromArt);
    B("agent", s.agent); S("agentClient", s.agentClient);
    if (j["ai"].isObject()) s.ai = j["ai"];
    s.levels = std::clamp(s.levels, 0, 40);
    s.bossEvery = std::clamp(s.bossEvery, 0, 40);
    s.startResourcesPercent = std::clamp(s.startResourcesPercent, 10, 1000);
    s.xpPerLevel = std::clamp(s.xpPerLevel, 10, 100000);
    if (s.languages.empty()) s.languages = {"en"};
    return s;
}

// ---------------------------------------------------------------------------------------------------- catalog

Json wizardCatalog(const std::string& engineDir) {
    Json r = Json::object();
    Json temps = Json::array();
    for (auto& t : templateTraits()) {
        const fs::path dir = fs::path(engineDir) / "templates" / t.id;
        if (!fs::exists(dir / "game.json")) continue;
        Json m = readJsonFile((dir / "game.json").string());
        Json e = Json::object();
        e.set("id", Json::string(t.id));
        e.set("name", Json::string(m.getStr("name", t.id)));
        e.set("description", Json::string(m.getStr("description", "")));
        e.set("orientation", Json::string(m.getStr("orientation", "landscape")));
        Json tags = Json::array();
        for (auto& w : words(t.tags)) tags.push(Json::string(w));
        e.set("features", tags);
        e.set("preset", Json::string(t.preset));
        temps.push(e);
    }
    r.set("templates", temps);
    Json feats = Json::array();
    const std::pair<const char*, const char*> fl[] = {
        {"economy", "добыча ресурсов рабочими"}, {"base", "строительство базы"}, {"army", "производство армии"},
        {"garrison", "здания-гарнизоны, отправка войск (Mushroom Wars)"}, {"lanes", "линии и колонны"}, {"waves", "волны врагов"},
        {"territory", "захват территорий"}, {"heroes", "герои"}, {"spells", "заклинания"}, {"survival", "выживание"},
        {"planets", "планеты и орбиты"}, {"2d", "2D-спрайты"}, {"casual", "простое управление, короткие матчи"},
        {"portrait", "вертикальный экран"}, {"scifi", "фантастика"}, {"modern", "современная война"}};
    for (auto& [id, d] : fl) { Json f = Json::object(); f.set("id", Json::string(id)); f.set("description", Json::string(d)); feats.push(f); }
    r.set("features", feats);
    r.set("tutorialModes", strings({"both", "hard", "soft", "none"}));
    r.set("mapPresets", strings({"auto", "none", "classic", "continents", "islands", "highlands", "canyons", "metal", "caves", "lanes", "field"}));
    r.set("mapSizes", strings({"auto", "small", "medium", "large"}));
    r.set("stores", strings({"googleplay", "rustore", "appstore", "yandex-games", "steam"}));
    r.set("platforms", strings({"android", "ios", "web", "desktop"}));
    r.set("languages", strings({"ru", "en", "de", "fr", "es", "pt", "it", "tr", "uk", "kk", "zh", "ja", "ko"}));
    r.set("defaults", WizardSpec().toJson());
    return r;
}

// ---------------------------------------------------------------------------------------------------- plan

namespace {

struct Pick { std::string id, reason; int score = 0; };

Pick chooseTemplate(const std::string& engineDir, const WizardSpec& s, const std::string& idea, Json* scoresOut) {
    if (s.genre != "auto" && !s.genre.empty()) {
        if (!fs::exists(fs::path(engineDir) / "templates" / s.genre / "game.json")) throw std::runtime_error("шаблон '" + s.genre + "' не найден");
        return {s.genre, "выбран в мастере", 0};
    }
    const auto ws = words(idea);
    std::set<std::string> want(s.features.begin(), s.features.end());
    if (s.orientation == "portrait") want.insert("portrait");
    std::vector<Pick> picks;
    const uint32_t seed = s.seed ? uint32_t(s.seed) : hashText(s.title + "|" + idea);
    for (auto& t : templateTraits()) {
        if (!fs::exists(fs::path(engineDir) / "templates" / t.id / "game.json")) continue;
        Pick p{t.id, "", 0};
        std::vector<std::string> hits;
        for (auto& kw : words(t.keywords)) {
            if (kw.size() < 2) continue;
            for (auto& w : ws) if (w.rfind(kw, 0) == 0) { p.score += 3; hits.push_back(w); break; }
        }
        const auto tags = words(t.tags);
        std::set<std::string> tagSet(tags.begin(), tags.end());
        std::vector<std::string> feats;
        for (auto& f : want) {
            if (tagSet.count(f)) { p.score += 4; feats.push_back(f); }
            else p.score -= 2;
        }
        if (tagSet.count("sample") || std::string(t.id) == "scripted") p.score -= 3;
        if (!hits.empty()) p.reason = "в идее: " + hits[0] + (hits.size() > 1 ? ", " + hits[1] : "");
        if (!feats.empty()) p.reason += std::string(p.reason.empty() ? "" : "; ") + "механики: " + feats[0] + (feats.size() > 1 ? ", " + feats[1] : "");
        picks.push_back(p);
    }
    if (picks.empty()) throw std::runtime_error("в templates/ нет шаблонов игр");
    std::stable_sort(picks.begin(), picks.end(), [&](const Pick& a, const Pick& b) {
        if (a.score != b.score) return a.score > b.score;
        return (hashText(a.id) ^ seed) < (hashText(b.id) ^ seed);   // ties: the seed decides, the same answers give the same game
    });
    if (scoresOut) {
        *scoresOut = Json::array();
        for (size_t i = 0; i < picks.size() && i < 6; ++i) {
            Json e = Json::object();
            e.set("id", Json::string(picks[i].id)); e.set("score", Json::number(int64_t(picks[i].score)));
            scoresOut->push(e);
        }
    }
    if (picks[0].score <= 0) {   // nothing in the idea: a well-rounded game, picked by the seed among the safest
        const std::vector<std::string> safe = s.orientation == "portrait" ? std::vector<std::string>{"borderrush", "laneclash"}
                                                                           : std::vector<std::string>{"skirmish", "mushrooms", "ironfront", "starfall"};
        std::vector<std::string> have;
        for (auto& id : safe) if (fs::exists(fs::path(engineDir) / "templates" / id / "game.json")) have.push_back(id);
        if (!have.empty()) return {have[seed % have.size()], "идея не задана: мастер выбрал сам", 0};
    }
    if (picks[0].reason.empty()) picks[0].reason = "лучше всего подходит к ответам";
    return picks[0];
}

std::string ideaFromFolder(const std::string& dir) {
    std::string out;
    if (dir.empty()) return out;
    std::error_code ec;
    for (auto it = fs::recursive_directory_iterator(dir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        if (!it->is_regular_file(ec)) continue;
        const std::string ext = lower(it->path().extension().string());
        if ((ext == ".md" || ext == ".txt") && it->file_size(ec) < 512 * 1024) {
            out += readText(it->path().string()) + "\n";
            if (out.size() > 200000) break;
        }
    }
    return out;
}

} // namespace

Json planGame(const std::string& engineDir, const WizardSpec& s) {
    const std::string idea = s.idea + "\n" + ideaFromFolder(s.artDir);
    Json scores;
    Pick p = chooseTemplate(engineDir, s, idea, &scores);
    Json r = Json::object();
    r.set("template", Json::string(p.id));
    r.set("reason", Json::string(p.reason));
    r.set("candidates", scores);
    const fs::path tdir = fs::path(engineDir) / "templates" / p.id;
    Json m = readJsonFile((tdir / "game.json").string());
    r.set("templateName", Json::string(m.getStr("name", p.id)));
    r.set("orientation", Json::string(s.orientation == "auto" ? m.getStr("orientation", "landscape") : s.orientation));
    Json rules = readComposedJson((tdir / m.getStr("rules", "rules.json")).string(), fileReader());
    RulesInfo info = RulesInfo::read(rules);
    bool garrison = false, economy = false;
    for (auto& t : info.types) { garrison |= t.entersGarrison; economy |= t.worker; }
    r.set("tutorialArchetype", Json::string(s.tutorial == "none" || s.tutorial == "soft" ? "none" : economy ? "economy" : garrison ? "garrison"
                                            : rules["autoBuild"].isObject() ? "autobuild" : "generic"));
    std::string preset = s.mapPreset;
    if (preset == "auto") {
        for (auto& t : templateTraits()) if (p.id == t.id) preset = t.preset;
        if (preset == "auto") preset = "";
    }
    if (preset == "none") preset = "";
    r.set("levelPreset", Json::string(preset.empty() ? "copies of the base map" : preset));
    r.set("levels", Json::number(int64_t(s.levels)));
    Json meta = Json::array();
    for (auto [on, name] : {std::pair{s.upgrades, "upgrades"}, {s.shop, "shop"}, {s.daily, "daily"}, {s.hints, "hints"},
                            {s.ads, "ads"}, {s.purchases, "purchases"}})
        if (on) meta.push(Json::string(name));
    r.set("meta", meta);
    r.set("ideaWords", Json::number(int64_t(words(idea).size())));
    r.set("summary", Json::string("Шаблон: " + m.getStr("name", p.id) + " (" + p.id + ") — " + p.reason));
    return r;
}

// ---------------------------------------------------------------------------------------------------- download

namespace {
std::string quoteArg(const std::string& s) {
#if defined(_WIN32)
    return "\"" + s + "\"";
#else
    std::string o = "'";
    for (char c : s) { if (c == '\'') o += "'\\''"; else o += c; }
    return o + "'";
#endif
}
int run(const std::string& cmd, std::string* out) {
#if defined(_WIN32)
    FILE* p = _popen(("\"" + cmd + " 2>&1\"").c_str(), "r");
#else
    FILE* p = popen((cmd + " 2>&1").c_str(), "r");
#endif
    if (!p) return -1;
    char buf[512];
    while (std::fgets(buf, sizeof buf, p)) if (out && out->size() < 8000) *out += buf;
#if defined(_WIN32)
    return _pclose(p);
#else
    int rc = pclose(p);
    return WIFEXITED(rc) ? WEXITSTATUS(rc) : rc;
#endif
}
} // namespace

std::string fetchWizardArt(const std::string& url, const std::string& dir, std::string* err) {
    auto fail = [&](const std::string& m) { if (err) *err = m; return std::string(); };
    if (url.rfind("http://", 0) != 0 && url.rfind("https://", 0) != 0) return fail("ссылка должна начинаться с http:// или https://");
    for (char c : url) if (c == '\n' || c == '\r' || c == '"' || c == '`') return fail("в ссылке недопустимые символы");
    std::error_code ec;
    fs::create_directories(dir, ec);
    if (ec) return fail("не удалось создать " + dir);
    // the file name from the link (without the query); Google Drive / Dropbox share links: direct download forms
    std::string u = url;
    if (u.find("dropbox.com") != std::string::npos && u.find("dl=0") != std::string::npos) u.replace(u.find("dl=0"), 4, "dl=1");
    const size_t gd = u.find("drive.google.com/file/d/");
    if (gd != std::string::npos) {
        const size_t a = gd + 24, b = u.find('/', a);
        u = "https://drive.google.com/uc?export=download&id=" + u.substr(a, b == std::string::npos ? std::string::npos : b - a);
    }
    std::string name = u.substr(0, u.find('?'));
    name = name.substr(name.find_last_of('/') + 1);
    if (name.empty() || name.find('.') == std::string::npos) name = "download";
    for (char& c : name) if (!(std::isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_')) c = '_';
    const fs::path file = fs::path(dir) / name;
    std::string out;
    const int rc = run("curl -L --fail --silent --show-error --max-time 600 -o " + quoteArg(file.string()) + " " + quoteArg(u), &out);
    if (rc != 0 || !fs::exists(file, ec)) return fail("не удалось скачать: " + (out.empty() ? "curl код " + std::to_string(rc) : out));
    // what came: an archive, a page, a file
    std::string head = readText(file.string()).substr(0, 512);
    auto starts = [&](const char* m) { return head.rfind(m, 0) == 0; };
    const std::string lname = lower(name);
    const bool zip = starts("PK\x03\x04");
    const bool gz = head.size() > 2 && (unsigned char)head[0] == 0x1f && (unsigned char)head[1] == 0x8b;
    if (zip || gz || (lname.size() > 4 && (lname.rfind(".zip") == lname.size() - 4 || lname.find(".tar") != std::string::npos))) {
        const fs::path to = fs::path(dir) / "content";
        fs::create_directories(to, ec);
        std::string o2;
        int r2 = zip ? run("unzip -q -o " + quoteArg(file.string()) + " -d " + quoteArg(to.string()), &o2) : -1;
        if (r2 != 0) { o2.clear(); r2 = run("tar -xf " + quoteArg(file.string()) + " -C " + quoteArg(to.string()), &o2); }
        if (r2 != 0) return fail("не удалось распаковать архив: " + o2);
        fs::remove(file, ec);
        return to.string();
    }
    const std::string lh = lower(head);
    if (lh.find("<!doctype html") != std::string::npos || lh.find("<html") != std::string::npos) {
        fs::remove(file, ec);
        return fail("по ссылке веб-страница, а не файл: дайте прямую ссылку на картинку или архив (.zip) — или папку на диске");
    }
    if (name == "download") {   // no extension in the link: guess from the bytes
        std::string ext = starts("\x89PNG") ? ".png" : starts("\xff\xd8") ? ".jpg" : starts("OggS") ? ".ogg" : starts("RIFF") ? ".wav"
                        : starts("ID3") ? ".mp3" : starts("glTF") ? ".glb" : "";
        if (!ext.empty()) { fs::rename(file, fs::path(dir) / ("art" + ext), ec); }
    }
    return dir;
}

// ---------------------------------------------------------------------------------------------------- the maker

std::string GameMaker::Ctx::typeName(const std::string& id, const std::string& lang) const {
    if (auto it = texts.find("type." + id); it != texts.end()) {   // the AI's name for this game
        if (auto l = it->second.find(lang); l != it->second.end()) return l->second;
        if (auto l = it->second.find("en"); l != it->second.end()) return l->second;
    }
    Json l = readJsonFile(dir + "/lang/" + lang + ".json");
    const std::string k = "type." + id;
    if (l[k].isString() && !l[k].str().empty()) return l[k].str();
    const auto* t = info.find(id);
    return t ? t->name : id;
}

void GameMaker::Ctx::saveManifest() {
    std::string err;
    if (!writeText(dir + "/game.json", manifest.dump() + "\n", &err)) throw std::runtime_error(err);
}

void GameMaker::Ctx::saveRules() {
    std::string err;
    if (rulesOrigin.files.empty()) rulesOrigin.files.push_back(dir + "/" + manifest.getStr("rules", "rules.json"));
    for (auto& [file, part] : splitComposed(rules, rulesOrigin))
        if (!writeText(file, part.dump() + "\n", &err)) throw std::runtime_error(err);
}

GameMaker::GameMaker(Workspace& ws, WizardSpec spec) : ws_(ws), spec_(std::move(spec)), ctx_(std::make_shared<Ctx>()) {
    Ctx& c = *ctx_;
    c.engineDir = ws_.engineDir;
    c.kitDir = (fs::path(ws_.engineDir) / "templates" / "_wizard").string();
    c.spec = spec_;
    if (c.spec.location.empty()) c.spec.location = defaultProjectsDir();
    if (!c.spec.languages.empty()) c.mainLang = c.spec.languages[0];

    steps_.push_back({"Подбираю жанр и шаблон", [this] {
        Ctx& c = *ctx_;
        if (!c.spec.artUrl.empty() && c.spec.artDir.empty()) {   // the window downloads on a thread before; MCP / tests here
            std::string err;
            c.spec.artDir = fetchWizardArt(c.spec.artUrl, (fs::path(c.spec.location) / ".wizard-download").string(), &err);
            if (c.spec.artDir.empty()) throw std::runtime_error(err);
        }
        c.sourceDir = c.spec.artDir;
        c.plan = planGame(c.engineDir, c.spec);
        c.templ = c.plan.getStr("template");
        c.did("шаблон: " + c.plan.getStr("summary"));
    }});
    steps_.push_back({"Создаю проект", [this] {
        Ctx& c = *ctx_;
        std::string err;
        c.dir = createProject(c.engineDir, c.templ, c.spec.title, c.spec.location, "", &err);
        if (c.dir.empty()) throw std::runtime_error(err);
        dir_ = c.dir;
        file_ = (fs::path(c.dir) / projectFileName(c.spec.title)).string();
        c.manifest = readJsonFile(c.dir + "/game.json");
        c.rules = composeJson(c.dir + "/" + c.manifest.getStr("rules", "rules.json"), fileReader(), &c.rulesOrigin);
        c.info = RulesInfo::read(c.rules);
        if (c.manifest["maps"].size() > 0) {
            c.baseMapFile = c.manifest["maps"][0].getStr("file");
            c.baseMap = readJsonFile(c.path(c.baseMapFile));
        }
        if (c.baseMap.isNull()) c.baseMap = Json::object();
        // identity
        const std::string slug = slugOf(c.spec.title.empty() ? "game" : c.spec.title);
        std::string studio = slugOf(c.spec.studio.empty() ? "mystudio" : c.spec.studio);
        c.manifest.set("id", Json::string("com." + studio + "." + slug));
        c.manifest.set("version", Json::string(c.spec.version.empty() ? "0.1.0" : c.spec.version));
        c.manifest.set("build", Json::number(int64_t(1)));
        if (!c.spec.idea.empty()) c.manifest.set("description", Json::string(c.spec.idea.substr(0, 300)));
        c.manifest.set("orientation", Json::string(c.plan.getStr("orientation", "landscape")));
        c.manifest.set("languages", strings(c.spec.languages));
        c.manifest.set("sourceLanguage", Json::string(c.mainLang));
        c.manifest.erase("template");
        c.manifest.set("template", Json::string(c.templ));
        c.saveManifest();
        c.did("проект: " + c.dir);
    }});
    steps_.push_back({"Читаю арт, музыку и контент", [this] { scanSources(*ctx_); }});
    steps_.push_back({"Переношу арт, модели, звуки и шрифты", [this] { importContent(*ctx_); applyAudio(*ctx_); }});
    steps_.push_back({"Правила и экономика", [this] {
        Ctx& c = *ctx_;
        if (c.spec.startResourcesPercent != 100) {
            for (auto& [id, r] : c.rules.member("resources").membersMut()) {
                (void)id;
                if (r.isObject() && r.has("start")) r.set("start", Json::number(r["start"].integer() * c.spec.startResourcesPercent / 100));
            }
            c.did("стартовые ресурсы: " + std::to_string(c.spec.startResourcesPercent) + "%");
        }
        c.saveRules();
    }});
    steps_.push_back({"Имена из идеи (ИИ)", [this] { applyAiNames(*ctx_); }});
    steps_.push_back({"Копирую набор мастера (обучение, мета, подсказки)", [this] { copyKit(*ctx_); }});
    steps_.push_back({"Обучение: карта и шаги", [this] { makeTutorial(*ctx_); }});
    steps_.push_back({"Подсказки в бою", [this] { makeHints(*ctx_); }});
    steps_.push_back({"Уровни и сложность", [this] { makeLevels(*ctx_); }});
    steps_.push_back({"Мета: меню, профиль, улучшения, магазин", [this] { makeMeta(*ctx_); }});
    steps_.push_back({"Тексты игры из идеи (ИИ)", [this] { applyAiTexts(*ctx_); }});
    steps_.push_back({"Монетизация", [this] { makeMonetization(*ctx_); }});
    steps_.push_back({"Тексты и языки", [this] { writeLanguages(*ctx_); }});
    steps_.push_back({"Открываю проект в редакторе", [this] {
        Ctx& c = *ctx_;
        std::string err;
        if (!ws_.openGame(file_.empty() ? c.dir : file_, &err)) throw std::runtime_error("проект создан, но не открылся: " + err);
    }});
    steps_.push_back({"Иконка, заставка и цвета из арта", [this] { applyBranding(ws_, *ctx_); applyPalette(*ctx_); }});
    steps_.push_back({"Проверка: обучение и уровни ботами", [this] { verifyGame(ws_, *ctx_); }});
    steps_.push_back({"Отчёт и файл проекта", [this] {
        Ctx& c = *ctx_;
        writeReport(c);
        // the task for the agent step: the description verbatim + how to keep the wizard's parts consistent
        writeText(c.path("WIZARD_TASK.md"), wizardAgentTask(c.spec, c.dir, c.templ, c.idea));
        report_.set("task", Json::string(c.path("WIZARD_TASK.md")));
        // .aiverse: what the wizard was asked (unknown keys are kept by the editor), so "make again" can start from it
        Json pf = readJsonFile(file_);
        if (!pf.isObject()) pf = Json::object();
        Json w = Json::object();
        w.set("spec", c.spec.toJson());
        w.set("template", Json::string(c.templ));
        w.set("made", Json::string([] { char b[32]; std::time_t t = std::time(nullptr); std::strftime(b, sizeof b, "%Y-%m-%d", std::gmtime(&t)); return std::string(b); }()));
        pf.set("wizard", w);
        std::string err;
        writeText(file_, pf.dump() + "\n", &err);
        ws_.openGame(file_, &err);   // reload: the report and the last files are in the project now
    }});
}

std::string GameMaker::current() const { return next_ < steps_.size() ? steps_[next_].label : "Готово"; }

bool GameMaker::step() {
    if (done()) return false;
    Step& s = steps_[next_];
    try {
        s.fn();
    } catch (const std::exception& e) {
        failed_ = true;
        report_.set("error", Json::string(s.label + ": " + e.what()));
    }
    ++next_;
    Ctx& c = *ctx_;
    report_.set("ok", Json::fromBool(!failed_));
    report_.set("dir", Json::string(dir_));
    report_.set("file", Json::string(file_));
    report_.set("template", Json::string(c.templ));
    report_.set("plan", c.plan);
    report_.set("done", c.done);
    report_.set("warnings", c.warnings);
    report_.set("assets", c.assigned);
    if (done()) {
        report_.set("summary", Json::string(failed_ ? "Не получилось: " + report_.getStr("error")
                                                    : "Игра готова: " + file_ + " (шаблон " + c.templ + ")"));
    }
    return !done();
}

Json GameMaker::runAll() {
    while (step()) {}
    return report_;
}

} // namespace aiverse::editor
