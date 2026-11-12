#include "LiveOpsInternal.h"
#include "Modules.h"
namespace aiverse::meta::live {
Json chains(Call &c) {
    auto action = c.req.action;
    const auto &a = c.args();
    auto current = c.self[c.id];
    if (!current.isObject())
        current = Json::object();
    auto global = c.shared[c.id];
    if (!global.isObject())
        global = Json::object();

    keys(a, {"id"});
    if (!current.has("node"))
        current.set("node", c.def["start"]);
    auto node = current.getStr("node");
    need(!node.empty(), Code::Unavailable, "chain completed");
    auto step = c.def["nodes"][node];
    need(step.isObject(), Code::Conflict, "removed chain node");
    if (action == "show") {
        auto bucket = c.now / c.def.getInt("period");
        auto shows = current.getInt("bucket", -1) == bucket ? current.getInt("shows") : 0;
        need(shows < c.def.getInt("maxShows"), Code::Limit, "offer impression limit");
        need(!current.has("last") || c.now - current.getInt("last") >= c.def.getInt("cooldown"), Code::Unavailable,
             "offer cooldown");
        Economy e(c.cfg.economy, Profile::load(c.state["profiles"][c.who.player], c.cfg.economy));
        auto visible = e.visibleOffers(c.context);
        need(std::find(visible.begin(), visible.end(), step.getStr("offer")) != visible.end(), Code::Unavailable,
             "offer conditions");
        current.set("bucket", number(bucket));
        current.set("shows", number(shows + 1));
        current.set("last", number(c.now));
        current.set("shown", Json::fromBool(true));
    } else {
        need(current.getBool("shown"), Code::Unavailable, "offer not shown");
        need(action == "buy" || action == "dismiss", Code::Unsupported, action);
        if (action == "buy") {
            Economy e(c.cfg.economy, Profile::load(c.state["profiles"][c.who.player], c.cfg.economy));
            auto result = e.buy(step.getStr("offer"), c.context, c.operation);
            need(bool(result), result.code, result.detail);
            c.state.member("profiles").set(c.who.player, e.profile().save());
        }
        current.set("node", str(step.getStr(action == "buy" ? "accept" : "reject")));
        current.set("shown", Json::fromBool(false));
    }
    c.self.set(c.id, current);
    return row({{"state", current}, {"offer", step["offer"]}});
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
void modules::chains(Modules &m) {
    m.features.insert("chains");
    m.live["chains"] = live::chains;
}
} // namespace aiverse::meta
