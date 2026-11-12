#include "LiveOps.h"
#include "Modules.h"
#include "ServicePolicy.h"
#include <algorithm>
namespace aiverse::meta {
using namespace detail;
using namespace policy;
namespace {
Json initialState() {
    auto j = Json::object();
    j.set("version", number(1));
    j.set("revision", number(0));
    j.set("time", number(0));
    for (auto k : {"profiles", "extras", "friends", "requests", "blocks", "clans", "membership", "tournaments",
                   "boards", "chats", "receipts", "events", "live"})
        j.set(k, Json::object());
    return j;
}

} // namespace
Service::Service(Modules modules, ServiceConfig config, Json state, Commit commit, RewardDraw draw)
    : modules_(std::make_shared<const Modules>(std::move(modules))), config_(std::move(config)),
      state_(std::move(state)), commit_(std::move(commit)), draw_(std::move(draw)) {
    // Validate typed callers with the same parser and fail closed on incompatible persisted state.
    Json cfg = Json::object();
    cfg.set("economy", config_.economy.save());
    cfg.set("extensions", config_.extensions);
    (void)ServiceConfig::parse(cfg);
    auto selected = [&](const char *feature) { return modules_->features.count(feature) != 0; };
    require(selected("economy"), "meta selection requires economy");
    require((config_.boards.empty() && config_.chats.empty()) || selected("social"),
            "social configuration without library");
    require((config_.economy.tracks.empty() && config_.economy.daily.empty() && config_.economy.energy.empty()) ||
                selected("progression"),
            "progression configuration without library");
    for (auto entry : {std::pair{"clans", "clans"},
                       {"items", "items"},
                       {"friends", "social"},
                       {"tournaments", "tournaments"},
                       {"entitlements", "backend"}})
        require(config_.extensions[entry.first].size() == 0 || selected(entry.second),
                std::string(entry.first) + " configuration without library");
    for (const auto &[name, definitions] : config_.extensions["liveops"].members())
        require(definitions.size() == 0 || modules_->live.count(name),
                "liveops configuration without library: " + name);

    if (state_.isNull())
        state_ = initialState();
    if (!state_.has("live"))
        state_.set("live", Json::object());
    keys(state_, {"version", "revision", "time", "profiles", "extras", "friends", "requests", "blocks", "clans",
                  "membership", "tournaments", "boards", "chats", "receipts", "events", "live"});
    integer(state_["version"], 1, 1);
    integer(state_["revision"]);
    integer(state_["time"]);
    for (auto k : {"profiles", "extras", "friends", "requests", "blocks", "clans", "membership", "tournaments",
                   "boards", "chats", "receipts", "events", "live"})
        require(state_[k].isObject(), std::string("state ") + k);
    for (auto &[p, v] : state_["profiles"].members())
        require(Profile::load(v, config_.economy).player == p, "profile identity");
}
ServiceReply Service::execute(const Principal &who, const ServiceRequest &request, Time now) {
    try {
        id(str(who.player));
        require(now >= 0 && now <= kMaxAmount, "time");
        if (request.service == "meta" && request.action == "view")
            return {{}, view(who.player, now)};
        require(!request.operation.empty() && request.operation.size() <= 128, "operation id");
        auto fingerprint = Json::array();
        fingerprint.push(str(request.service));
        fingerprint.push(str(request.action));
        fingerprint.push(canonical(request.arguments));
        fingerprint.push(Json::fromBool(who.authority));
        auto receipt = pairKey(who.player, request.operation);
        auto body = fingerprint.dump(-1);
        if (state_["receipts"].has(receipt)) {
            auto &old = state_["receipts"][receipt];
            if (old.getStr("body") != body)
                return fail(Code::Conflict, "operation payload changed");
            return {{Code::Duplicate, {}}, old["data"]};
        }
        if (now < state_.getInt("time"))
            return fail(Code::Conflict, "clock rollback");
        if (state_.getInt("revision") == kMaxAmount)
            return fail(Code::Limit, "revision");
        Json next = state_;
        auto reply = dispatch(next, who, request, now);
        if (!reply.result)
            return reply;
        Json entry = Json::object();
        entry.set("body", str(body));
        entry.set("data", reply.data);
        next.member("receipts").set(receipt, entry);
        next.set("revision", number(state_.getInt("revision") + 1));
        next.set("time", number(now));
        if (commit_ && !commit_(state_.getInt("revision"), next))
            return fail(Code::Conflict, "storage CAS");
        state_ = std::move(next);
        return reply;
    } catch (const std::exception &e) {
        return fail(Code::Invalid, e.what());
    }
}
ServiceReply Service::dispatch(Json &s, const Principal &who, const ServiceRequest &r, Time now) {
    const auto &a = r.arguments;
    require(a.isObject(), "arguments");
    auto p = who.player;
    auto action = r.action;
    auto service = r.service;
    auto ok = [](Json data = Json::object()) -> ServiceReply { return {{}, std::move(data)}; };
    auto missing = []() { return fail(Code::Missing, "definition or object"); };
    auto &x = config_.extensions;
    if (service == "profile" && action == "create") {
        keys(a, {});
        if (s["profiles"].has(p))
            return {{Code::Duplicate, {}}, Json::object()};
        Profile profile;
        profile.player = p;
        Economy e(config_.economy, profile);
        auto rr = e.transact("initial", {}, x.has("initial") ? bundle(x["initial"]) : Bundle{});
        if (!rr)
            return {rr, {}};
        s.member("profiles").set(p, e.profile().save());
        object(s.member("extras"), p).set("created", number(now));
        return ok();
    }
    if (!s["profiles"].has(p))
        return missing();
    auto route = modules_->handlers.find(service + "/" + action);
    if (route == modules_->handlers.end())
        route = modules_->handlers.find(service);
    if (route != modules_->handlers.end())
        return route->second(config_, s, who, r, now);
    auto live = modules_->live.find(service);
    if (live != modules_->live.end())
        return dispatchLiveOpsSelected(config_, s, who, r, now, context(s, p, now, config_), draw_, live->second);
    return fail(Code::Unsupported, service);
}
Json Service::view(const std::string &p, Time now) const {
    require(now >= 0 && now <= kMaxAmount, "view time");
    require(state_["profiles"].has(p), "unknown player");
    auto out = Json::object();
    out.set("revision", state_["revision"]);
    out.set("player", str(p));
    auto profile = state_["profiles"][p];
    profile.erase("receipts");
    profile.erase("orders");
    out.set("profile", profile);
    out.set("extras", state_["extras"][p]);
    Economy e(config_.economy, Profile::load(state_["profiles"][p], config_.economy));
    auto offers = Json::array();
    auto ctx = context(state_, p, now, config_);
    for (auto &key : e.visibleOffers(ctx))
        offers.push(str(key));
    out.set("offers", offers);
    auto tags = Json::array();
    for (auto &tag : ctx.tags)
        tags.push(str(tag));
    out.set("tags", tags);
    auto live = state_["live"]["players"][p];
    if (!live.isObject())
        live = Json::object();
    out.set("liveops", live);
    for (auto project : modules_->views)
        project(config_, state_, p, now, out);
    return out;
}
} // namespace aiverse::meta
