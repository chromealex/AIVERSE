#include "LiveOpsInternal.h"
#include "Modules.h"
namespace aiverse::meta::live {
Json notifications(Call &c) {
    const auto &a = c.args();
    auto action = c.req.action;
    auto current = c.self[c.id];
    if (!current.isObject())
        current = Json::object();

    keys(a, {"id"});
    if (action == "schedule") {
        c.authority();
        current = row({{"due", number(add(c.now, c.def.getInt("delay")))}, {"read", number(0)}});
    } else if (action == "ack") {
        need(current.has("due") && c.now >= current.getInt("due"), Code::Unavailable, "notification not due");
        auto repeat = c.def.getInt("repeat");
        current.set("read", number(repeat ? (c.now - current.getInt("due")) / repeat + 1 : 1));
    } else if (action == "cancel") {
        c.authority();
        c.self.erase(c.id);
        return Json::object();
    } else
        throw Stop(Code::Unsupported, action);
    c.self.set(c.id, current);
    return current;
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
Json notificationView(const ServiceConfig &cfg, const Json &state, const std::string &player, Time now) {
    using namespace live;
    auto out = state["live"]["players"][player];
    if (!out.isObject())
        out = Json::object();
    auto notices = Json::array();
    std::map<std::string, int64_t> groups;
    for (auto &[id, n] : out["notifications"].members()) {
        const auto &d = cfg.extensions["liveops"]["notifications"][id];
        if (!d.isObject() || now < n.getInt("due"))
            continue;
        if (d.getInt("lifetime") && now >= n.getInt("due") + d.getInt("lifetime"))
            continue;
        auto occurrence = d.getInt("repeat") ? (now - n.getInt("due")) / d.getInt("repeat") + 1 : 1;
        if (occurrence <= n.getInt("read"))
            continue;
        notices.push(row({{"id", str(id)},
                          {"group", d["group"]},
                          {"priority", d["priority"]},
                          {"count", number(occurrence - n.getInt("read"))},
                          {"metadata", d["metadata"]}}));
        groups[d.getStr("group")] = add(groups[d.getStr("group")], occurrence - n.getInt("read"));
    }
    std::stable_sort(notices.itemsMut().begin(), notices.itemsMut().end(),
                     [](const Json &a, const Json &b) { return a.getInt("priority") > b.getInt("priority"); });
    out.set("badges", encode(groups));
    out.set("notices", notices);
    return out;
}
void modules::notifications(Modules &m) {
    m.features.insert("notifications");
    m.live["notifications"] = live::notifications;
    m.views.push_back([](const ServiceConfig &c, const Json &s, const std::string &p, Time t, Json &out) {
        out.set("liveops", notificationView(c, s, p, t));
    });
}
} // namespace aiverse::meta
