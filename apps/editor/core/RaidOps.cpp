#include "RaidOps.h"
#include "SimTools.h"
#include "modules/rts/RtsAI.h"
#include "modules/rts/RtsModule.h"
#include <stdexcept>

namespace aiverse::editor {

namespace {
Json str(const std::string& s) { return Json::string(s); }
void needProject(const Workspace& ws) { if (ws.project.dir.empty()) throw std::runtime_error("no game is open: call open_game first"); }
double sec(Fixed f) { return double(f.raw) / 65536.0; }
Json mapView(const MapDoc& doc) {
    Json m = Json::object();
    if (const Json& r = doc.data.extra["raid"]; !r.isNull()) m.set("raid", r);
    return m;
}
const char* const kPhase[] = {"briefing", "running", "over"};
const char* const kPointPhase[] = {"locked", "sleep", "inbound", "open", "departing"};
const char* const kStatus[] = {"waiting", "in_raid", "extracted", "dead", "timeout", "abandoned"};
} // namespace

Json raidInfo(const Workspace& ws) {
    needProject(ws);
    const game::Rules& rules = ws.project.rules;
    Json r = Json::object();
    r.set("enabled", Json::fromBool(rules.raid.enabled));
    if (!rules.raid.enabled) { r.set("hint", str("no rules.json \"raid\" block: raid_set {\"raid\": {...}} adds one (docs/raid.md)")); return r; }
    const game::RaidRules R = game::raidRulesForMap(rules, mapView(ws.map));   // throws on bad map data
    r.set("mode", str(R.pvp ? "pvp" : "pve"));
    r.set("duration", Json::number(sec(R.duration), 2));
    r.set("startDelay", Json::number(sec(R.startDelay), 2));
    Json raiders = Json::array();
    for (auto t : R.raiderTypes) raiders.push(str(rules.types[t].id));
    r.set("raiders", raiders);
    Json pts = Json::array();
    for (auto& p : R.points) {
        Json j = Json::object();
        j.set("id", str(p.id));
        Json pos = Json::array(); pos.push(Json::number(sec(p.x), 2)); pos.push(Json::number(sec(p.z), 2));
        j.set("pos", pos);
        j.set("radius", Json::number(sec(p.radius), 2));
        j.set("channel", Json::number(sec(p.channel), 2));
        j.set("scope", str(p.scope == game::RaidScope::Point ? "point" : "player"));
        if (p.open.raw) { j.set("open", Json::number(sec(p.open), 2)); j.set("sleep", Json::number(sec(p.sleep), 2)); }
        if (p.opensAt.raw) j.set("opensAt", Json::number(sec(p.opensAt), 2));
        if (p.call) j.set("call", Json::fromBool(true));
        if (p.follow) j.set("follow", Json::fromBool(true));
        Json req = Json::array(); for (auto& q : p.requires_) req.push(str(q));
        j.set("requires", req);
        j.set("origin", str(rules.raid.findPoint(p.id) >= 0 ? "rules" : "map"));
        pts.push(j);
    }
    r.set("points", pts);
    Json objs = Json::array();
    static const char* kk[] = {"collect", "reach", "kill", "interact", "script"};
    for (auto& o : R.objectives) {
        Json j = Json::object();
        j.set("id", str(o.id)); j.set("text", str(o.text)); j.set("kind", str(kk[int(o.kind)]));
        j.set("count", Json::number(int64_t(o.count)));
        j.set("required", Json::fromBool(o.required)); j.set("shared", Json::fromBool(o.shared));
        objs.push(j);
    }
    r.set("objectives", objs);
    Json loot = Json::array(); for (auto& t : R.loot) loot.push(str(t.id));
    r.set("lootTables", loot);
    Json inter = Json::array();
    for (size_t t = 0; t < R.interact.size(); ++t) if (R.interact[t].on) inter.push(str(rules.types[t].id));
    r.set("interactables", inter);
    // counts on the open map
    int raidersOnMap = 0, objects = 0;
    for (auto& e : ws.map.data.entities) {
        const int t = rules.findType(e.type);
        if (t < 0) continue;
        if (size_t(t) < R.interact.size() && R.interact[size_t(t)].on) ++objects;
        bool rd = std::find(R.raiderTypes.begin(), R.raiderTypes.end(), uint16_t(t)) != R.raiderTypes.end() || (rules.types[size_t(t)].tags & R.raiderTags);
        if (R.raiderTypes.empty() && !R.raiderTags) rd = rules.types[size_t(t)].mech.inventorySlots > 0;
        raidersOnMap += rd;
    }
    r.set("raidersOnMap", Json::number(int64_t(raidersOnMap)));
    r.set("interactablesOnMap", Json::number(int64_t(objects)));
    Json warn = Json::array();
    if (!raidersOnMap) warn.push(str("no raider on the map: nobody takes part in the raid"));
    if (R.points.empty()) warn.push(str("no extraction point: raiders can only time out (raid_point adds one)"));
    r.set("warnings", warn);
    return r;
}

Json raidRun(const Workspace& ws, uint64_t seed, double seconds) {
    needProject(ws);
    SimConfig cfg;
    cfg.seed = seed;
    Simulation sim(cfg, std::make_unique<game::RtsModule>());
    const Json simMap = mapForSim(ws.map.toJson(), ws.project.dir);
    sim.setup(ws.project.simRules(simMap), simMap);
    auto& mod = *dynamic_cast<game::RtsModule*>(&sim.module());
    game::Raid* raid = mod.raid();
    if (!raid || !raid->active()) throw std::runtime_error("the game has no rules \"raid\" block (docs/raid.md)");
    std::vector<game::RtsAI> bots;
    for (auto& pl : sim.players()) bots.emplace_back(pl.slot, seed);
    const uint32_t limit = uint32_t(seconds * double(sim.tickRate()));
    Json timeline = Json::array();
    auto t = [&] { return Json::number(double(sim.tick()) / sim.tickRate(), 2); };
    while (sim.tick() < limit && sim.result() == 0) {
        for (auto& b : bots) b.update(sim);
        sim.step();
        for (auto& ev : sim.events()) {
            if (ev.type < game::kEvRaidPhase || ev.type > game::kEvRaidPlayer || timeline.size() >= 400) continue;
            Json e = Json::object();
            e.set("t", t());
            e.set("player", Json::number(int64_t(ev.player)));
            switch (ev.type) {
            case game::kEvRaidPhase: e.set("event", str("phase")); e.set("phase", str(kPhase[std::min<int64_t>(2, ev.value)])); break;
            case game::kEvRaidPoint: e.set("event", str("point")); e.set("point", str(raid->rules().points[size_t(ev.value >> 8)].id)); e.set("phase", str(kPointPhase[std::min<int64_t>(4, ev.value & 0xFF)])); break;
            case game::kEvRaidExtracted: e.set("event", str("extracted")); if (ev.value >= 0) e.set("point", str(raid->rules().points[size_t(ev.value)].id)); break;
            case game::kEvRaidObjective: e.set("event", str("objective")); e.set("objective", str(raid->rules().objectives[size_t(ev.value & 0xFFFF)].id)); break;
            case game::kEvRaidPlayer: e.set("event", str("player")); e.set("status", str(kStatus[std::min<int64_t>(5, ev.value)])); break;
            case game::kEvRaidInteract: if ((ev.value >> 16) != 2) continue; e.set("event", str("interacted")); e.set("object", str(mod.rules().types[size_t(ev.value & 0xFFFF)].id)); break;
            default: continue;   // channels / loot: too many for a summary
            }
            timeline.push(e);
        }
    }
    game::RaidStore* st = raid->store(sim);
    Json r = Json::object();
    r.set("phase", str(kPhase[std::min<int>(2, st->phase)]));
    const int res = sim.result();
    r.set("winnerTeam", Json::number(int64_t(res > 0 ? res : 0)));
    r.set("seconds", t());
    Json receipts = Json::array();
    for (size_t i = 0; i < st->players.size(); ++i)
        receipts.push(Json::parse(st->receipts[i].empty() ? raid->receiptJson(sim, st->players[i]) : st->receipts[i]));
    r.set("receipts", receipts);
    r.set("timeline", timeline);
    if (auto* se = mod.scripts(); se && se->errorCount() > 0) {
        Json errs = Json::array();
        for (auto& l : se->log()) if (l.error && errs.size() < 20) errs.push(str(l.text));
        r.set("scriptErrors", errs);
    }
    r.set("checksum", str([&] { char b[20]; std::snprintf(b, sizeof b, "%016llx", (unsigned long long)sim.checksum()); return std::string(b); }()));
    return r;
}

void registerRaidOps(Workspace& ws) {
    Workspace* w = &ws;
    auto add = [&ws](const char* name, const char* desc, const char* schema, Workspace::OpFn fn) {
        ws.registerOp({name, desc, Json::parse(schema), std::move(fn), false});
    };
    add("raid_info",
        "Extraction raid of the open game (rules.json \"raid\" + the map's \"raid\" overrides, docs/raid.md): mode, clock, raider types, "
        "extraction points (pos, radius, channel, open/sleep cycle, call/follow helicopters, requirements), objectives, loot tables, "
        "interactable types, how many raiders / interactables the map has, and warnings.",
        "{}", [w](const Json&) { return raidInfo(*w); });
    add("raid_set",
        "Replace or merge the raid block (docs/raid.md): target 'rules' (default) writes rules.json \"raid\", target 'map' the open map's "
        "\"raid\" (mode, duration, startDelay, points, objectives by id). merge=true merges top-level keys; delete=true removes the block. "
        "Validated before saving (readable errors, nothing written on failure).",
        R"({"properties": {"raid": {"type": "object"}, "target": {"type": "string", "enum": ["rules", "map"]}, "merge": {"type": "boolean"},
            "delete": {"type": "boolean"}}})",
        [w](const Json& p) {
            needProject(*w);
            const bool toMap = p.getStr("target", "rules") == "map", del = p.getBool("delete");
            const Json& in = p["raid"];
            if (!del && !in.isObject()) throw std::runtime_error("pass 'raid' (object) or delete=true");
            auto merged = [&](const Json* old) {
                Json e = old && p.getBool("merge") ? *old : Json::object();
                for (auto& [k, v] : in.members()) e.set(k, v);
                return e;
            };
            if (toMap) {
                w->map.beginEdit();
                if (del) w->map.data.extra.erase("raid");
                else { Json e = merged(w->map.data.extra.find("raid")); w->map.data.extra.set("raid", e); }
                game::raidRulesForMap(w->project.rules, mapView(w->map));   // throws: the snapshot is still there to undo
                w->map.touched();
                w->note({Change::Map, 0, 0, -1, -1, -1, "Рейд карты"});
            } else {
                Json rules = w->project.rulesJson;
                if (del) rules.erase("raid");
                else { Json e = merged(rules.find("raid")); rules.set("raid", e); }
                game::loadRules(rules);
                w->project.rulesJson = rules;
                std::string err;
                if (!w->project.saveRules(&err)) throw std::runtime_error(err);
                w->project.reloadRules();
                w->note({Change::Rules, 0, 0, -1, -1, -1, "Рейд"});
            }
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("summary", str(std::string(del ? "Рейд удалён" : "Рейд сохранён") + (toMap ? " (карта)" : " (rules.json)")));
            return r;
        });
    add("raid_point",
        "Add / replace (by id) or delete an extraction point on the open map (its \"raid\".points; undoable). point = {id, pos [x, z], "
        "radius, channel, opensAt, open, sleep, arrival, departure, requires, scope player|point, contest block|ignore|reset, "
        "leave reset|pause, interruptOnDamage, capacity, call, follow, team, fn}; unspecified keys use rules raid.extraction.",
        R"({"properties": {"id": {"type": "string"}, "point": {"type": "object"}, "delete": {"type": "boolean"}}, "required": ["id"]})",
        [w](const Json& p) {
            needProject(*w);
            if (!w->project.rules.raid.enabled) throw std::runtime_error("the game has no rules \"raid\" block (raid_set first)");
            const std::string id = p.getStr("id");
            if (id.empty()) throw std::runtime_error("id expected");
            w->map.beginEdit();
            Json& raid = w->map.data.extra.member("raid");
            Json& pts = raid.member("points");
            if (!pts.isArray()) pts = Json::array();
            size_t at = pts.size();
            for (size_t i = 0; i < pts.size(); ++i) if (pts[i].getStr("id") == id) at = i;
            if (p.getBool("delete")) { if (at < pts.size()) pts.eraseAt(at); }
            else {
                Json e = p["point"].isObject() ? p["point"] : Json::object();
                e.set("id", str(id));
                if (at < pts.size()) pts.at(at) = e; else pts.push(e);
            }
            game::raidRulesForMap(w->project.rules, mapView(w->map));   // validate
            w->map.touched();
            w->note({Change::Map, 0, 0, -1, -1, -1, "Точка эвакуации: " + id});
            Json r = Json::object();
            r.set("ok", Json::fromBool(true));
            r.set("summary", str((p.getBool("delete") ? "Точка удалена: " : "Точка сохранена: ") + id));
            return r;
        });
    add("raid_run",
        "Run a bot raid on the open map (headless, deterministic): raiders loot, complete required objectives and extract. Returns the "
        "final phase, winner team, each player's receipt (outcome, secured / lost items, value, objectives, reward bundle, operation id) "
        "and a timeline of point phases, interactions, objectives and extractions.",
        R"({"properties": {"seed": {"type": "integer"}, "seconds": {"type": "number", "description": "time limit, default 900"}}})",
        [w](const Json& p) { return raidRun(*w, uint64_t(std::max<int64_t>(0, p.getInt("seed", 1))), p["seconds"].toDouble(900)); });
}

} // namespace aiverse::editor
