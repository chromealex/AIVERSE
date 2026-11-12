#include "LiveOpsInternal.h"
#include "Modules.h"
namespace aiverse::meta::live {
Json ads(Call &c) {
    auto action = c.req.action;
    const auto &a = c.args();
    auto current = c.self[c.id];
    if (!current.isObject())
        current = Json::object();
    auto global = c.shared[c.id];
    if (!global.isObject())
        global = Json::object();

    keys(a, {"id", "ticket", "token"});
    auto bucket = c.now / c.def.getInt("period");
    if (action == "begin") {
        need(!current.has("last") || c.now - current.getInt("last") >= c.def.getInt("cooldown"), Code::Limit,
             "ad cooldown");
        auto used = current.getInt("bucket", -1) == bucket ? current.getInt("used") : 0;
        need(used < c.def.getInt("limit"), Code::Limit, "ad limit");
        auto ticket = c.operation;
        object(current, "tickets")
            .set(ticket, row({{"expires", number(add(c.now, c.def.getInt("timeout")))},
                              {"reward", c.def["reward"]},
                              {"done", Json::fromBool(false)}}));
        current.set("bucket", number(bucket));
        current.set("used", number(used + 1));
        current.set("last", number(c.now));
        c.self.set(c.id, current);
        return row({{"ticket", str(ticket)}});
    }
    need(action == "complete", Code::Unsupported, action);
    c.authority();
    need(a["ticket"].isString(), Code::Invalid, "ad ticket");
    auto ticket = a["ticket"].str();
    auto t = current["tickets"][ticket];
    need(t.isObject() && !t.getBool("done") && c.now < t.getInt("expires"), Code::Unavailable,
         "ad ticket expired or complete");
    c.once("ad:" + id(a["token"]));
    c.pay({}, bundle(t["reward"]));
    t.set("done", Json::fromBool(true));
    object(current, "tickets").set(ticket, t);
    c.self.set(c.id, current);
    return t;
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
void modules::ads(Modules &m) {
    m.features.insert("ads");
    m.live["ads"] = live::ads;
}
} // namespace aiverse::meta
