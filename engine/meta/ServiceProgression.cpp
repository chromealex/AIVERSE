#include "ServicePolicy.h"
namespace aiverse::meta {
namespace {
ServiceReply handle(const ServiceConfig &config, Json &s, const Principal &who, const ServiceRequest &r, Time now) {
    using namespace detail;
    auto p = who.player;
    auto op = policy::pairKey(p, r.operation);
    const auto &a = r.arguments;
    Economy e(config.economy, Profile::load(s["profiles"][p], config.economy));
    auto ctx = policy::context(s, p, now, config);
    Result result;
    if (r.action == "daily" || r.action == "energy") {
        keys(a, {"id"});
        auto key = id(a["id"]);
        result = r.action == "daily" ? e.claimDaily(key, now, op) : e.regenerate(key, now);
    } else if (r.action == "claim") {
        keys(a, {"id", "tier"});
        result = e.claim(id(a["id"]), size_t(integer(a["tier"])), ctx, op);
    } else if (r.action == "event") {
        if (!who.authority)
            return fail(Code::Denied, "permission");
        keys(a, {"metric", "amount"});
        result = e.recordEvent(id(a["metric"]), integer(a["amount"]), ctx, op);
    } else
        return fail(Code::Unsupported, r.action);
    if (result)
        s.member("profiles").set(p, e.profile().save());
    return {result, Json::object()};
}
} // namespace
void modules::progression(Modules &m) {
    m.features.insert("progression");
    for (auto action : {"daily", "energy", "claim", "event"})
        m.handlers[std::string("economy/") + action] = handle;
}
} // namespace aiverse::meta
