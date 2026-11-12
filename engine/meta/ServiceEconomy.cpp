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
    if (service == "economy") {
        Economy e(config_.economy, Profile::load(s["profiles"][p], config_.economy));
        auto ctx = context(s, p, now, config_);
        Result result;
        if (action == "buy" || action == "mail" || action == "reserve") {
            keys(a, {"id"});
            auto key = id(a["id"]);
            if (action == "buy")
                result = e.buy(key, ctx, op);
            if (action == "mail")
                result = e.claimMail(key, now, op);
            if (action == "reserve")
                result = e.reserveOrder(key, ctx, op);
        } else if (action == "grant") {
            if (!who.authority)
                return denied();
            keys(a, {"reward"});
            result = e.transact(op, {}, bundle(a["reward"]));
        } else if (action == "deliver") {
            if (!who.authority)
                return denied();
            keys(a, {"id", "reward", "expires"});
            Mail mail;
            mail.id = id(a["id"]);
            mail.reward = bundle(a["reward"]);
            mail.expires = integer(a["expires"]);
            result = e.deliver(mail);
        } else if (action == "fulfill" || action == "cancel") {
            if (!who.authority)
                return denied();
            keys(a, {"order", "token"});
            require(a["order"].isString() && !a["order"].str().empty() && a["order"].str().size() <= 512, "order ID");
            auto order = a["order"].str();
            if (action == "cancel")
                result = e.cancelOrder(order);
            else {
                auto token = id(a["token"]);
                auto event = "payment:" + token;
                auto owner = pairKey(p, order);
                if (s["events"].has(event) && s["events"].getStr(event) != owner)
                    return fail(Code::Conflict, "payment token used");
                result = e.fulfillOrder(order, token);
                if (result)
                    s.member("events").set(event, str(owner));
            }
        } else
            return fail(Code::Unsupported, action);
        if (result)
            s.member("profiles").set(p, e.profile().save());
        Json data = Json::object();
        if (action == "reserve")
            data.set("order", str(op));
        return {result, data};
    }
    return fail(Code::Unsupported, r.service);
}
} // namespace
void modules::economy(Modules &m) {
    m.features.insert("economy");
    m.handlers["economy"] = handle;
}
} // namespace aiverse::meta
