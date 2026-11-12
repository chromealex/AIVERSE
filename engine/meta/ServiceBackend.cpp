#include "ServicePolicy.h"
namespace aiverse::meta {
using namespace detail;
using namespace policy;
namespace {
ServiceReply handle(const ServiceConfig &config_, Json &s, const Principal &who, const ServiceRequest &r,
                    [[maybe_unused]] Time now) {
    [[maybe_unused]] const auto &a = r.arguments;
    require(a.isObject(), "arguments");
    auto p = who.player;
    auto op = pairKey(p, r.operation);
    auto action = r.action;
    auto service = r.service;
    [[maybe_unused]] auto ok = [](Json data = Json::object()) -> ServiceReply { return {{}, std::move(data)}; };
    [[maybe_unused]] auto missing = []() { return fail(Code::Missing, "definition or object"); };
    [[maybe_unused]] auto denied = []() { return fail(Code::Denied, "permission"); };
    [[maybe_unused]] auto limit = []() { return fail(Code::Limit, "capacity or limit"); };
    [[maybe_unused]] auto target = [&]() {
        auto t = id(a["target"]);
        require(s["profiles"].has(t), "unknown player");
        return t;
    };
    [[maybe_unused]] auto &x = config_.extensions;
    if (service == "entitlement") {
        if (!who.authority)
            return denied();
        keys(a, {"id", "event", "sequence", "start", "end"});
        auto key = id(a["id"]);
        auto event = id(a["event"]);
        if (!x["entitlements"].has(key))
            return missing();
        auto seq = integer(a["sequence"], 1);
        auto &rights = object(object(s.member("extras"), p), "rights");
        auto eventKey = "entitlement:" + event;
        auto owner = pairKey(p, key);
        if (s["events"].has(eventKey))
            return fail(Code::Conflict, "provider event already processed");
        if (seq <= rights[key].getInt("sequence"))
            return fail(Code::Conflict, "stale provider sequence");
        if (action != "grant" && action != "revoke")
            return fail(Code::Unsupported, action);
        auto start = integer(a["start"]), end = integer(a["end"]);
        if (end && end <= start)
            return fail(Code::Invalid, "entitlement interval");
        auto right = Json::object();
        right.set("sequence", number(seq));
        right.set("start", number(start));
        right.set("end", number(end));
        right.set("revoked", Json::fromBool(action == "revoke"));
        rights.set(key, right);
        s.member("events").set(eventKey, str(owner));
        return ok();
    }
    return fail(Code::Unsupported, r.service);
}
} // namespace
void modules::backend(Modules &m) {
    m.features.insert("backend");
    m.handlers["entitlement"] = handle;
}
} // namespace aiverse::meta
