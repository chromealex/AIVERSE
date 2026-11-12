#include "LiveOpsInternal.h"
#include "Modules.h"
#include "libraries/include/aiverselib/systems/Loot.h"
namespace aiverse::meta::live {
namespace {
void merge(Bundle &to, const Bundle &from) {
    for (auto &[k, v] : from)
        to[k] = add(to[k], v);
}
void loot(Call &c, const std::string &table, Bundle &reward, Json &results, int depth = 0) {
    need(depth < 8, Code::Limit, "loot nesting exceeds 8");
    const auto &d = c.cfg.extensions["liveops"]["loot"][table];
    auto stats = c.self[table];
    if (!stats.isObject())
        stats = Json::object();
    for (int64_t roll = 0; roll < d.getInt("rolls"); ++roll) {
        need(results.size() < 1024, Code::Limit, "loot expansion exceeds 1024 draws");
        std::vector<lib::systems::LootRow> rows;
        for (size_t i = 0; i < d["entries"].size(); ++i) {
            const auto &e = d["entries"][i];
            rows.push_back({int64_t(i + 1), e.getInt("weight"), e.getBool("rare")});
        }
        lib::systems::LootState pity{stats.getInt("misses")};
        auto selected = lib::systems::rollLoot(rows, {d.getInt("pity")}, pity, {}, c.draw);
        need(bool(selected), Code::Invalid, "random draw");
        auto entry = d["entries"][size_t(*selected - 1)];
        need(pity.misses <= kMaxAmount, Code::Limit, "pity counter");
        stats.set("misses", number(pity.misses));
        results.push(row({{"table", str(table)}, {"entry", entry["id"]}}));
        if (entry.has("uniqueAsset") &&
            (c.balance(entry.getStr("uniqueAsset")) + reward[entry.getStr("uniqueAsset")] > 0))
            merge(reward, bundle(entry["duplicateReward"]));
        else if (entry.has("table"))
            loot(c, entry.getStr("table"), reward, results, depth + 1);
        else
            merge(reward, bundle(entry["reward"]));
    }
    c.self.set(table, stats);
}
} // namespace
Json loot(Call &c) {
    const auto &a = c.args();
    auto action = c.req.action;
    auto current = c.self[c.id];
    if (!current.isObject())
        current = Json::object();

    keys(a, {"id"});
    need(action == "open", Code::Unsupported, action);
    need(bool(c.draw), Code::Unavailable, "host RNG required");
    Bundle reward;
    auto results = Json::array();
    loot(c, c.id, reward, results);
    c.pay(bundle(c.def["cost"]), reward);
    return row({{"draws", results}, {"reward", encode(reward)}});
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
void modules::loot(Modules &m) {
    m.features.insert("loot");
    m.live["loot"] = live::loot;
}
} // namespace aiverse::meta
