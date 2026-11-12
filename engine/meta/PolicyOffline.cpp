#include "LiveOpsInternal.h"
#include "Modules.h"
namespace aiverse::meta::live {
namespace {
void accrue(Call &c, Json &current) {
    if (!current.has("anchor")) {
        current.set("anchor", number(c.now));
        current.set("tier", number(0));
        current.set("pending", Json::object());
        return;
    }
    auto elapsed = std::min(c.now - current.getInt("anchor"), c.def.getInt("maxAway"));
    need(elapsed >= 0, Code::Conflict, "offline clock");
    auto total = add(elapsed, current.getInt("remainder"));
    auto ticks = total / c.def.getInt("interval");
    auto pending = bundle(current["pending"]);
    const auto &tier = c.def["tiers"][size_t(current.getInt("tier"))];
    need(tier.isObject(), Code::Conflict, "removed offline tier");
    for (auto &[asset, n] : bundle(tier))
        pending[asset] = add(pending[asset], mul(n, ticks));
    current.set("pending", encode(pending));
    current.set("remainder", number(total % c.def.getInt("interval")));
    current.set("anchor", number(c.now));
}
} // namespace
Json offline(Call &c) {
    const auto &a = c.args();
    auto action = c.req.action;
    auto current = c.self[c.id];
    if (!current.isObject())
        current = Json::object();

    keys(a, {"id", "tier"});
    need(action == "claim" || action == "tier" || action == "checkpoint", Code::Unsupported, action);
    if (action != "claim")
        c.authority();
    accrue(c, current);
    if (action == "tier") {
        auto tier = integer(a["tier"], 0, int64_t(c.def["tiers"].size()) - 1);
        current.set("tier", number(tier));
        current.set("remainder", number(0));
    }
    if (action == "claim") {
        c.pay({}, bundle(current["pending"]));
        current.set("pending", Json::object());
    }
    c.self.set(c.id, current);
    return current;
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
void modules::offline(Modules &m) {
    m.features.insert("offline");
    m.live["offline"] = live::offline;
}
} // namespace aiverse::meta
