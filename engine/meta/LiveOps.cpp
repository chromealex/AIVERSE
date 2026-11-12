#include "LiveOpsInternal.h"
namespace aiverse::meta::live {
Call::Call(const ServiceConfig &c, Json &s, const Principal &w, const ServiceRequest &r, Time t, const Context &ctx,
           const RewardDraw &rng)
    : cfg(c), state(s), who(w), req(r), now(t), context(ctx), draw(rng), id(detail::id(r.arguments["id"])),
      operation(key(w.player, r.operation)), def(c.extensions["liveops"][r.service][id]),
      self(object(object(object(object(s, "live"), "players"), w.player), r.service)),
      shared(object(object(object(s, "live"), "shared"), r.service)) {
    need(def.isObject(), Code::Missing, "liveops definition: " + r.service + "/" + id);
}
Json &Call::player(const std::string &p) {
    need(state["profiles"].has(p), Code::Missing, "player");
    return object(object(object(state, "live"), "players"), p);
}
void Call::pay(const Bundle &cost, const Bundle &reward, const std::string &suffix, const std::string &playerId) {
    auto p = playerId.empty() ? who.player : playerId;
    Economy e(cfg.economy, Profile::load(state["profiles"][p], cfg.economy));
    auto result = e.transact(operation + ":" + suffix, cost, reward);
    need(bool(result), result.code, result.detail);
    state.member("profiles").set(p, e.profile().save());
}
void Call::mail(const std::string &p, const Bundle &reward, const std::string &suffix) {
    Economy e(cfg.economy, Profile::load(state["profiles"][p], cfg.economy));
    Mail m;
    m.id = key(req.service + ":" + id, suffix);
    m.reward = reward;
    auto result = e.deliver(m);
    need(bool(result), result.code, result.detail);
    state.member("profiles").set(p, e.profile().save());
}
int64_t Call::balance(const std::string &asset, const std::string &p) const {
    return state["profiles"][p.empty() ? who.player : p]["balances"].getInt(asset);
}
int64_t Call::metric(const std::string &metric, const std::string &p) const {
    return state["profiles"][p.empty() ? who.player : p]["progress"].getInt(metric);
}
void Call::once(const std::string &providerEvent) {
    auto &used = object(object(state, "live"), "tokens");
    need(!used.has(providerEvent), Code::Conflict, "provider event already consumed");
    used.set(providerEvent, str(who.player));
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
ServiceReply dispatchLiveOpsSelected(const ServiceConfig &cfg, Json &state, const Principal &who,
                                     const ServiceRequest &req, Time now, const Context &ctx, const RewardDraw &draw,
                                     Json (*handler)(live::Call &)) {
    try {
        // Create both children before taking references to either child of Json's vector-backed object.
        auto &root = detail::object(state, "live");
        detail::object(root, "players");
        detail::object(root, "shared");
        detail::object(root, "tokens");
        live::Call c(cfg, state, who, req, now, ctx, draw);
        Json data = handler(c);
        return {{}, std::move(data)};
    } catch (const live::Stop &e) {
        return detail::fail(e.code, e.what());
    }
}
} // namespace aiverse::meta
