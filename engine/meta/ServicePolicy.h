#pragma once
#include "Modules.h"
#include "ServiceInternal.h"
#include <algorithm>
namespace aiverse::meta::policy {
using namespace detail;
inline Context context(const Json &state, const std::string &player, Time now, const ServiceConfig &c) {
    Context ctx;
    ctx.player = player;
    ctx.now = now;
    auto p = Profile::load(state["profiles"][player], c.economy);
    ctx.values = p.progress;
    if (state["extras"][player].has("created"))
        ctx.values["accountAge"] = std::max(int64_t(0), now - state["extras"][player].getInt("created"));
    for (auto &[key, v] : state["extras"][player]["rights"].members()) {
        if (!v.getBool("revoked") && v.getInt("start") <= now && (!v.getInt("end") || now < v.getInt("end"))) {
            auto tag = c.extensions["entitlements"][key].getStr("tag");
            if (!tag.empty())
                ctx.tags.insert(tag);
        }
    }
    return ctx;
}
inline Result transact(Json &state, const ServiceConfig &c, const std::string &p, const Bundle &cost,
                       const Bundle &reward, const std::string &op) {
    Economy e(c.economy, Profile::load(state["profiles"][p], c.economy));
    auto r = e.transact(op, cost, reward);
    if (r)
        state.member("profiles").set(p, e.profile().save());
    return r;
}
inline std::string pairKey(const std::string &a, const std::string &b) {
    auto j = Json::array();
    j.push(str(a));
    j.push(str(b));
    return j.dump(-1);
}
inline bool blocked(const Json &s, const std::string &a, const std::string &b) {
    return s["blocks"][a].has(b) || s["blocks"][b].has(a);
}
} // namespace aiverse::meta::policy
