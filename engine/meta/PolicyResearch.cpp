#include "LiveOpsInternal.h"
#include "Modules.h"
namespace aiverse::meta::live {
Json research(Call &c) {
    const auto &a = c.args();
    auto action = c.req.action;
    auto current = c.self[c.id];
    if (!current.isObject())
        current = Json::object();

    keys(a, {"id", "node"});
    auto unlocked = current["nodes"];
    if (!unlocked.isObject())
        unlocked = Json::object();
    if (action == "unlock") {
        auto node = id(a["node"]);
        auto d = c.def["nodes"][node];
        need(d.isObject(), Code::Missing, "research node");
        need(!unlocked.has(node), Code::Unavailable, "already unlocked");
        for (auto &dep : d["requires"].items())
            need(unlocked.has(dep.str()), Code::Unavailable, "research dependency");
        if (d.has("group"))
            for (auto &[other, v] : unlocked.members()) {
                (void)v;
                need(c.def["nodes"][other].getStr("group") != d.getStr("group"), Code::Conflict,
                     "exclusive research branch");
            }
        c.pay(bundle(d["cost"]), bundle(d["reward"]));
        unlocked.set(node, Json::fromBool(true));
        current.set("nodes", unlocked);
    } else {
        need(action == "prestige", Code::Unsupported, action);
        need(unlocked.size() >= size_t(c.def.getInt("prestigeCount")), Code::Insufficient, "prestige prerequisites");
        c.pay(bundle(c.def["prestigeCost"]), bundle(c.def["prestigeReward"]));
        current.set("nodes", Json::object());
        current.set("prestige", number(add(current.getInt("prestige"), 1)));
    }
    c.self.set(c.id, current);
    return current;
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
void modules::research(Modules &m) {
    m.features.insert("research");
    m.live["research"] = live::research;
}
} // namespace aiverse::meta
