#include "RtsBattle.h"
#include "engine/data/JsonCompose.h"
#include <algorithm>
#include <fstream>
#include <iterator>
#include <stdexcept>
#include "modules/rts/RtsModule.h"
#include "modules/rts/RtsScript.h"
#include "modules/rts/RtsTiles.h"
#include "modules/rts/RtsTopDown.h"

namespace aiverse::battle {

namespace {
using game::RtsModule;

void merge(Json& into, const Json& patch) {   // objects merge recursively, anything else replaces
    if (!patch.isObject() || !into.isObject()) { into = patch; return; }
    for (auto& [k, v] : patch.members()) {
        Json* cur = into.find(k);
        if (cur && cur->isObject() && v.isObject()) merge(*cur, v);
        else into.set(k, v);
    }
}

class RtsBattleGame final : public BattleGame {
public:
    explicit RtsBattleGame(const RtsBattleOptions& o) : opt_(o) {
        const std::string dir = o.project;
        auto read = [dir](const std::string& p, std::string& out) {
            std::ifstream f(dir + "/" + p, std::ios::binary);
            if (!f) return false;
            out.assign(std::istreambuf_iterator<char>(f), {});
            return true;
        };
        const Json manifest = Json::parseFile(dir + "/game.json");
        Json map = o.mapJson.empty() ? Json::parseFile(dir + "/" + (o.map.empty() ? manifest["maps"][size_t(0)].getStr("file") : o.map))
                                     : Json::parse(o.mapJson);
        std::string err;
        if (!game::applyTilesToMap(map, read, &err)) throw std::runtime_error("map: " + err);
        Json rules = readComposedJson(dir + "/" + manifest.getStr("rules", "rules.json"), fileReader());
        if (!o.rulesPatch.empty()) merge(rules, Json::parse(o.rulesPatch));
        if (!game::embedScripts(rules, map, read, &err)) throw std::runtime_error("scripts: " + err);
        if (!rules["topDown"].isObject()) throw std::runtime_error(dir + ": rules have no \"topDown\" block (no heroes to steer)");
        map_ = map.dump(-1);
        rules_ = rules.dump(-1);
        id_ = manifest.getStr("id", "rts");
    }
    std::string id() const override { return "rts-" + id_; }
    // The project's map; its player count must equal the allocation's players per match.
    std::string mapJson(uint32_t, uint32_t) const override { return map_; }
    std::unique_ptr<Simulation> create(const SimConfig& cfg, const net::MatchPayloadMsg*, const std::string& map) override {
        auto sim = std::make_unique<Simulation>(cfg, std::make_unique<RtsModule>());
        sim->setup(Json::parse(rules_), Json::parse(map));
        return sim;
    }
    std::vector<uint8_t> resultBlob(Simulation& sim) override { return mod(sim).outcomeBlob(sim); }
    void bot(Simulation& sim, uint8_t slot, BotThumbs& t, uint64_t now, std::vector<Command>& out) override {
        game::TopDown* td = mod(sim).topDown();
        if (!td || !td->active()) return;
        if (now >= t.nextInput) {   // the computer pilot (RtsAI's hero steering) sends kCmdTopDown on change only
            t.nextInput = now + opt_.steerEveryMs;
            Command c;
            if (td->botCommand(sim, slot, c)) out.push_back(c);
        }
        if (opt_.abilityEveryMs && now >= t.nextAbility) {
            t.nextAbility = now + opt_.abilityEveryMs / 2 + t.rng.below(opt_.abilityEveryMs);
            const Entity h = td->heroOf(sim, slot);
            if (!h.valid()) return;
            const auto& abilities = mod(sim).rules().types[sim.world().get<game::Unit>(h).type].abilities;
            if (abilities.empty()) return;
            topdown::Ability a;
            a.index = int(t.rng.below(uint32_t(std::min<size_t>(abilities.size(), 8))));
            out.push_back(net::heroAbilityCommand(slot, a));   // a tap: along the aim / at the nearest target
        }
    }
    void heroPositions(Simulation& sim, std::map<uint64_t, std::pair<float, float>>& out) override {
        game::TopDown* td = mod(sim).topDown();
        if (!td || !td->active()) return;
        World& w = sim.world();
        auto put = [&](uint64_t id, Entity e) {
            const Vec3 p = w.get<Transform>(e).pos;
            out[id] = {float(p.x.toDouble()), float(p.z.toDouble())};
        };
        for (auto& p : sim.players()) {
            if (Entity h = td->heroOf(sim, p.slot); h.valid()) put(p.slot, h);
            for (Entity m : td->squadOf(sim, p.slot)) put(uint64_t(1) << 63 | m.packed(), m);
        }
    }
    void corrupt(Simulation& sim, uint8_t slot) override {
        game::TopDown* td = mod(sim).topDown();
        Entity h = td ? td->heroOf(sim, slot) : kNullEntity;
        if (h.valid()) sim.world().get<game::Unit>(h).hp -= Fixed::fromInt(1);
    }

private:
    static RtsModule& mod(Simulation& sim) { return *static_cast<RtsModule*>(&sim.module()); }
    RtsBattleOptions opt_;
    std::string map_, rules_, id_;
};
} // namespace

std::unique_ptr<BattleGame> rtsBattleGame(const RtsBattleOptions& options) { return std::make_unique<RtsBattleGame>(options); }

} // namespace aiverse::battle
