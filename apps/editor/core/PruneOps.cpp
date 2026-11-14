// rules_prune: keys of rules.json whose value is exactly what the loader uses when the key is missing.
// Like rules_lint it runs the real loader (Simulation::setup) with a JsonAccessLog: a key is a candidate
// only when every lookup of it was a typed read with a default (getFx / getInt / getStr / getBool) and
// that default equals the value. Conservative on purpose: keys of iterated objects, presentation blocks,
// level overrides, design-generated types and entries taking part in "extends" are never touched, and
// apply checks that a bot match on the open map gives the same checksums before saving.
#include "SchemaOps.h"
#include "engine/data/JsonCompose.h"
#include <map>
#include <set>
#include <stdexcept>
#include "engine/sim/Simulation.h"
#include "modules/rts/RtsAI.h"
#include "modules/rts/RtsModule.h"

namespace aiverse::editor {
using namespace aiverse::game;

namespace {
// Read by the view / UI too (a different default there would change the look): never pruned.
const std::set<std::string> kKeep = {"view", "sprite", "sprites", "name", "description", "icon", "portrait", "hotkey", "tooltip",
                                     "label", "color", "colour", "sound", "sounds", "voice", "music", "ui", "hud", "card", "help",
                                     "title", "detail", "detailLook", "style", "terrain", "fogOfWar", "overlays", "levels", "lights",
                                     "camera", "look", "id", "extends", "abstract", "include"};

struct Candidate { std::vector<std::string> path; Json value; };

bool matches(const Json& v, const std::string& def) {
    if (def.empty()) return false;
    const std::string d = def.substr(1);
    switch (def[0]) {
    case 'n': return v.isNumber() && std::to_string(v.fx().raw) == d;
    case 'i': return v.isNumber() && v.numberLiteral().find_first_not_of("-0123456789") == std::string::npos && std::to_string(v.integer()) == d;
    case 's': return v.isString() && v.str() == d;
    case 'b': return v.type() == Json::Type::Bool && v.boolean() == (d == "1");
    default: return false;
    }
}

struct Log {
    std::map<std::pair<const Json*, std::string>, int> lookups;
    std::map<std::pair<const Json*, std::string>, std::vector<std::string>> defaults;
    std::set<const Json*> iterated;
};

Log record(const Json& rules, const Json& map) {
    JsonAccessLog log;
    Json::setAccessLog(&log);
    try {
        Simulation sim(SimConfig{}, std::make_unique<RtsModule>());
        sim.setup(rules, map);
    } catch (...) {
        Json::setAccessLog(nullptr);
        throw;
    }
    Json::setAccessLog(nullptr);
    Log l;
    for (auto& [o, k] : log.keys) ++l.lookups[{o, k}];
    for (auto& d : log.defaults) l.defaults[{d.object, d.key}].push_back(d.def);
    l.iterated.insert(log.iterated.begin(), log.iterated.end());
    return l;
}

void walk(const Log& l, const Json& j, std::vector<std::string>& path, std::vector<Candidate>& out) {
    if (j.isArray()) {
        for (size_t i = 0; i < j.items().size(); ++i) {
            const Json& x = j.items()[i];
            path.push_back(x.isObject() && x["id"].isString() ? "#" + x["id"].str() : "[" + std::to_string(i) + "]");
            walk(l, x, path, out);
            path.pop_back();
        }
        return;
    }
    if (!j.isObject()) return;
    const bool iterated = l.iterated.count(&j) > 0;
    for (auto& [k, v] : j.members()) {
        if (kKeep.count(k)) continue;
        path.push_back(k);
        auto look = l.lookups.find({&j, k});
        auto defs = l.defaults.find({&j, k});
        bool prune = !iterated && look != l.lookups.end() && defs != l.defaults.end() && int(defs->second.size()) == look->second;
        if (prune) for (auto& d : defs->second) prune = prune && matches(v, d);
        if (prune) out.push_back({path, v});
        else walk(l, v, path, out);
        path.pop_back();
    }
}

// Entries named in "extends", inheriting, or abstract: their keys may be overrides, leave them as written.
std::set<std::pair<std::string, std::string>> inheritance(const Json& raw) {
    std::set<std::pair<std::string, std::string>> out;
    for (auto& [section, v] : raw.members()) {
        auto note = [&](const std::string& name, const Json& e) {
            if (!e.isObject()) return;
            if (e.has("extends") || e.has("abstract")) out.insert({section, name});
            const Json& ext = e["extends"];
            if (ext.isString()) out.insert({section, (v.isArray() ? "#" : "") + ext.str()});
            for (auto& b : ext.items()) out.insert({section, (v.isArray() ? "#" : "") + b.str()});
        };
        if (v.isObject()) for (auto& [m, e] : v.members()) note(m, e);
        else if (v.isArray()) for (auto& e : v.items()) if (e["id"].isString()) note("#" + e["id"].str(), e);
    }
    return out;
}

Json* step(Json& j, const std::string& c) {
    if (j.isObject()) return j.find(c);
    if (!j.isArray()) return nullptr;
    if (c.size() > 1 && c[0] == '#') {
        for (auto& x : j.itemsMut()) if (x.isObject() && x.getStr("id") == c.substr(1)) return &x;
        return nullptr;
    }
    if (c.size() > 2 && c[0] == '[') {
        const size_t i = size_t(std::stoul(c.substr(1)));
        return i < j.size() ? &j.at(i) : nullptr;
    }
    return nullptr;
}

std::string dotted(const std::vector<std::string>& p) {
    std::string s;
    for (auto& c : p) {
        if (c[0] == '[') s += c;
        else if (c[0] == '#') s += "[id=" + c.substr(1) + "]";
        else s += (s.empty() ? "" : ".") + c;
    }
    return s;
}

// The candidates that can be removed from the raw (composed, unresolved) document, removed from `raw`.
std::vector<Candidate> prune(Json& raw, const Json& map) {
    Json loaded = needsExtends(raw) ? resolveExtends(raw) : raw;
    const bool designs = loaded.has("designs");
    if (designs) loaded = expandDesigns(loaded);   // the loader reads the expanded copy
    const Log l = record(loaded, map);
    std::vector<Candidate> found;
    std::vector<std::string> path;
    walk(l, loaded, path, found);
    const auto inherit = inheritance(raw);
    std::vector<Candidate> done;
    for (auto& c : found) {
        if (c.path.size() >= 2 && inherit.count({c.path[0], c.path[1]})) continue;
        if (designs && (c.path[0] == "types" || c.path[0] == "designs")) continue;
        Json* parent = &raw;
        for (size_t i = 0; parent && i + 1 < c.path.size(); ++i) parent = step(*parent, c.path[i]);
        if (!parent || !parent->isObject()) continue;
        const Json* now = parent->find(c.path.back());
        if (!now || !(*now == c.value)) continue;
        parent->erase(c.path.back());
        done.push_back(c);
    }
    return done;
}

std::vector<uint64_t> botHashes(const Json& rules, const Json& map) {
    SimConfig cfg; cfg.seed = 11;
    Simulation sim(cfg, std::make_unique<RtsModule>());
    sim.setup(rules, map);
    std::vector<RtsAI> bots;
    for (auto& p : sim.players()) bots.emplace_back(p.slot, 11);
    std::vector<uint64_t> h;
    for (uint32_t t = 0; t < 1200 && sim.result() == 0; ++t) {
        for (auto& b : bots) b.update(sim);
        sim.step();
        if (sim.tick() % 100 == 0) h.push_back(sim.checksum());
    }
    h.push_back(sim.checksum());
    return h;
}
} // namespace

void registerPruneOps(Workspace& ws) {
    ws.registerOp({"rules_prune",
        "Find keys in rules.json (and its included files) set to exactly the value the engine uses when the key is missing, "
        "so they can be dropped: a smaller rules file, the same game. Runs the real loader; skips presentation blocks, level "
        "overrides and entries in \"extends\" chains. apply: true removes them, checks a bot match on the open map gives the same "
        "checksums, and saves (each part back to its file).",
        Json::parse(R"({"properties": {"apply": {"type": "boolean"}, "limit": {"type": "integer"}}})"),
        [&ws](const Json& p) {
            if (ws.project.dir.empty()) throw std::runtime_error("no game is open (open_game)");
            if (ws.project.isFps()) throw std::runtime_error("rules_prune reads the RTS loader; FPS rules are not supported");
            const Json map = ws.map.toJson();
            Json pruned = ws.project.rulesJson;
            const size_t before = pruned.dump(-1).size();
            const std::vector<Candidate> keys = prune(pruned, map);
            Json r = Json::object(), list = Json::array();
            const size_t limit = size_t(p.getInt("limit", 200));
            for (auto& c : keys) {
                if (list.size() >= limit) break;
                Json k = Json::object();
                k.set("path", Json::string(dotted(c.path)));
                k.set("value", c.value);
                list.push(k);
            }
            r.set("count", Json::number(int64_t(keys.size())));
            r.set("bytesSaved", Json::number(int64_t(before - pruned.dump(-1).size())));
            r.set("keys", list);
            if (!p.getBool("apply") || keys.empty()) return r;
            const Json resolvedOld = needsExtends(ws.project.rulesJson) ? resolveExtends(ws.project.rulesJson) : ws.project.rulesJson;
            const Json resolvedNew = needsExtends(pruned) ? resolveExtends(pruned) : pruned;
            if (botHashes(resolvedOld, map) != botHashes(resolvedNew, map))
                throw std::runtime_error("not applied: a bot match on the open map differs without these keys (report it: a loader default is not what it reads)");
            const Json backup = ws.project.rulesJson;
            ws.project.rulesJson = pruned;
            if (!ws.project.reloadRules()) {
                ws.project.rulesJson = backup;
                ws.project.reloadRules();
                throw std::runtime_error("not applied: " + ws.project.rulesError);
            }
            ws.note({Change::Rules, 0, 0, -1, -1, -1, "Правила: убраны значения по умолчанию"});
            std::string err;
            if (!ws.project.saveRules(&err)) throw std::runtime_error(err);
            r.set("applied", Json::fromBool(true));
            return r;
        }});
}

} // namespace aiverse::editor
