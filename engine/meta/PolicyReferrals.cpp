#include "LiveOpsInternal.h"
#include "Modules.h"
namespace aiverse::meta::live {
Json referrals(Call &c) {
    auto action = c.req.action;
    const auto &a = c.args();
    auto current = c.self[c.id];
    if (!current.isObject())
        current = Json::object();
    auto global = c.shared[c.id];
    if (!global.isObject())
        global = Json::object();

    keys(a, {"id", "code", "inviter", "milestone"});
    if (action == "code") {
        auto code = id(a["code"]);
        auto d = c.def["codes"][code];
        need(d.isObject() && active(d, c.now), Code::Unavailable, "promotion unavailable");
        need(!current["codes"].has(code), Code::Unavailable, "promotion claimed");
        need(global["codes"].getInt(code) < d.getInt("maxUses"), Code::Limit, "promotion limit");
        c.pay({}, bundle(d["reward"]));
        object(current, "codes").set(code, Json::fromBool(true));
        object(global, "codes").set(code, number(global["codes"].getInt(code) + 1));
    } else if (action == "bind") {
        auto inviter = id(a["inviter"]);
        need(inviter != c.who.player && c.state["profiles"].has(inviter), Code::Denied, "inviter");
        need(!current.has("inviter"), Code::Conflict, "referral already bound");
        need(c.context.values.count("accountAge") && c.context.values.at("accountAge") <= c.def.getInt("maxAccountAge"),
             Code::Unavailable, "referral age");
        auto walk = inviter;
        std::set<std::string> visited{c.who.player};
        while (!walk.empty()) {
            need(visited.insert(walk).second, Code::Conflict, "referral cycle");
            walk = c.state["live"]["players"][walk]["referrals"][c.id].getStr("inviter");
        }
        auto count = global["invites"].getInt(inviter);
        need(count < c.def.getInt("maxInvitees"), Code::Limit, "inviter limit");
        current.set("inviter", str(inviter));
        object(global, "invites").set(inviter, number(count + 1));
    } else if (action == "claim") {
        auto milestone = id(a["milestone"]);
        auto d = c.def["milestones"][milestone];
        need(d.isObject() && current.has("inviter"), Code::Missing, "referral milestone");
        need(!current["claims"].has(milestone), Code::Unavailable, "referral claimed");
        need(c.metric(d.getStr("metric")) >= d.getInt("target"), Code::Insufficient, "referral progress");
        c.pay({}, bundle(d["inviterReward"]), "inviter", current.getStr("inviter"));
        c.pay({}, bundle(d["inviteeReward"]), "invitee");
        object(current, "claims").set(milestone, Json::fromBool(true));
    } else
        throw Stop(Code::Unsupported, action);
    c.self.set(c.id, current);
    c.shared.set(c.id, global);
    return current;
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
void modules::referrals(Modules &m) {
    m.features.insert("referrals");
    m.live["referrals"] = live::referrals;
}
} // namespace aiverse::meta
