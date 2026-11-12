#include "LiveOpsInternal.h"
#include "Modules.h"
namespace aiverse::meta::live {
Json albums(Call &c) {
    const auto &a = c.args();
    auto action = c.req.action;
    auto current = c.self[c.id];
    if (!current.isObject())
        current = Json::object();

    keys(a, {"id", "trade", "target", "give", "want"});
    need(active(c.def, c.now), Code::Unavailable, "album season");
    if (action == "claim") {
        need(!current.getBool("claimed"), Code::Unavailable, "album claimed");
        auto needed = bundle(c.def["required"]);
        for (auto &[asset, n] : needed)
            need(c.balance(asset) >= n, Code::Insufficient, asset);
        c.pay(c.def.getBool("consume") ? needed : Bundle{}, bundle(c.def["reward"]));
        current.set("claimed", Json::fromBool(true));
        c.self.set(c.id, current);
        return current;
    }
    auto trade = id(a["trade"]);
    auto trades = c.shared[c.id];
    if (!trades.isObject())
        trades = Json::object();
    if (action == "offer") {
        auto target = id(a["target"]);
        need(target != c.who.player && c.state["profiles"].has(target), Code::Denied, "trade target");
        need(!trades.has(trade), Code::Conflict, "trade id");
        checkBundle(a["give"], c.cfg.economy);
        checkBundle(a["want"], c.cfg.economy);
        for (auto *b : {&a["give"], &a["want"]})
            for (auto &[asset, n] : b->members()) {
                (void)n;
                need(c.def["required"].has(asset), Code::Denied, "not an album asset");
            }
        trades.set(trade, row({{"owner", str(c.who.player)},
                               {"target", str(target)},
                               {"give", a["give"]},
                               {"want", a["want"]},
                               {"status", str("pending")}}));
    } else {
        need(trades.has(trade), Code::Missing, "trade");
        auto t = trades[trade];
        need(t.getStr("status") == "pending", Code::Unavailable, "trade closed");
        if (action == "cancel") {
            need(t.getStr("owner") == c.who.player, Code::Denied, "trade owner");
            t.set("status", str("cancelled"));
        } else {
            need(action == "accept" && t.getStr("target") == c.who.player, Code::Denied, "trade recipient");
            c.pay(bundle(t["give"]), bundle(t["want"]), "seller", t.getStr("owner"));
            c.pay(bundle(t["want"]), bundle(t["give"]), "buyer");
            t.set("status", str("completed"));
        }
        trades.set(trade, t);
    }
    c.shared.set(c.id, trades);
    return trades[trade];
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
void modules::albums(Modules &m) {
    m.features.insert("albums");
    m.live["albums"] = live::albums;
}
} // namespace aiverse::meta
