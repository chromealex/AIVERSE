#include "LiveOpsInternal.h"
#include "Modules.h"
namespace aiverse::meta::live {
Json events(Call &c) {
    const auto &a = c.args();
    auto action = c.req.action;
    auto current = c.self[c.id];
    if (!current.isObject())
        current = Json::object();

    keys(a, {"id", "phase"});
    auto bucket = (c.now - c.def.getInt("start")) / c.def.getInt("period");
    auto base = c.def.getInt("start") + bucket * c.def.getInt("period");
    if (action == "join") {
        need(active(c.def, c.now), Code::Unavailable, "event unavailable");
        need(c.def.getBool("allowLate") || c.now == base, Code::Unavailable, "late enrollment disabled");
        if (!current.has("bucket") || current.getInt("bucket") != bucket)
            current = row({{"bucket", number(bucket)},
                           {"origin", number(c.def.getBool("personal") ? c.now : base)},
                           {"joined", number(c.now)},
                           {"claimed", Json::object()}});
    } else {
        need(action == "claim" && current.has("bucket"), Code::Unavailable, "join current event");
        auto phaseId = id(a["phase"]);
        Time phaseStart = current.getInt("origin");
        Json phase;
        for (auto &p : c.def["phases"].items()) {
            if (p.getStr("id") == phaseId) {
                phase = p;
                break;
            }
            phaseStart = add(phaseStart, p.getInt("duration"));
        }
        need(phase.isObject(), Code::Missing, "event phase");
        auto phaseEnd = add(phaseStart, phase.getInt("duration"));
        need(c.now >= phaseEnd && c.now - phaseEnd < c.def.getInt("claimSeconds"), Code::Unavailable,
             "phase not complete or claim expired");
        need(c.def.getBool("catchUp") || current.getInt("joined") <= phaseStart, Code::Unavailable, "missed phase");
        need(!current["claimed"].has(phaseId), Code::Unavailable, "phase claimed");
        c.pay(bundle(phase["cost"]), bundle(phase["reward"]));
        object(current, "claimed").set(phaseId, Json::fromBool(true));
    }
    c.self.set(c.id, current);
    return current;
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
void modules::events(Modules &m) {
    m.features.insert("events");
    m.live["events"] = live::events;
}
} // namespace aiverse::meta
