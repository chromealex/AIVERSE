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
    if (service == "tournament") {
        keys(a, {"id", "score"});
        auto key = id(a["id"]);
        if (!x["tournaments"].has(key))
            return missing();
        auto cfg = x["tournaments"][key];
        auto &tournaments = s.member("tournaments");
        auto &t = object(tournaments, key);
        bool active = now >= cfg.getInt("start") && now < cfg.getInt("end");
        object(t, "entrants");
        object(t, "groups");
        auto &entrants = t.member("entrants");
        auto &groups = t.member("groups");
        if (action == "join") {
            if (!active || t.getBool("closed"))
                return fail(Code::Unavailable, "tournament closed");
            if (entrants.has(p))
                return ok();
            auto league = std::min(s["extras"][p].getInt("league"), cfg.getInt("maxLeague"));
            size_t count = 0;
            for (auto &[u, v] : entrants.members()) {
                (void)u;
                if (v.getInt("league") == league)
                    ++count;
            }
            auto group = std::to_string(league) + ":" + std::to_string(count / size_t(cfg.getInt("groupSize")));
            auto entry = Json::object();
            entry.set("league", number(league));
            entry.set("group", str(group));
            entrants.set(p, entry);
            Leaderboard board(BoardConfig::parse(cfg["board"]));
            if (groups.has(group)) {
                auto rr = board.restore(groups[group]);
                if (!rr)
                    return {rr, {}};
            }
            groups.set(group, board.save());
            return ok();
        }
        if (action == "submit") {
            if (!who.authority)
                return denied();
            if (!active || t.getBool("closed") || !entrants.has(p))
                return fail(Code::Unavailable, "not enrolled or closed");
            auto group = entrants[p].getStr("group");
            Leaderboard board(BoardConfig::parse(cfg["board"]));
            auto rr = board.restore(groups[group]);
            if (!rr)
                return {rr, {}};
            rr = board.submit(p, integer(a["score"], -kMaxAmount), now, op);
            if (rr)
                groups.set(group, board.save());
            return {rr, {}};
        }
        if (action == "settle") {
            if (!who.authority)
                return denied();
            if (now < cfg.getInt("end"))
                return fail(Code::Unavailable, "tournament active");
            if (t.getBool("closed"))
                return {{Code::Duplicate, {}}, {}};
            auto results = Json::object();
            for (auto &[gid, data] : groups.members()) {
                (void)gid;
                Leaderboard board(BoardConfig::parse(cfg["board"]));
                auto rr = board.restore(data);
                if (!rr)
                    return {rr, {}};
                auto page = board.page(cfg.getInt("end") - 1, 0, 10000);
                for (size_t i = 0; i < page.entries.size(); ++i) {
                    auto &row = page.entries[i];
                    auto league = entrants[row.player].getInt("league");
                    if (i < size_t(cfg.getInt("promote")))
                        league = std::min(league + 1, cfg.getInt("maxLeague"));
                    else if (i >= page.entries.size() - std::min(page.entries.size(), size_t(cfg.getInt("demote"))))
                        league = std::max(int64_t(0), league - 1);
                    object(s.member("extras"), row.player).set("league", number(league));
                    auto result = Json::object();
                    result.set("rank", number(int64_t(row.rank)));
                    result.set("league", number(league));
                    result.set("score", number(row.score));
                    results.set(row.player, result);
                    if (row.rank && row.rank <= cfg["rewards"].size()) {
                        Economy e(config_.economy, Profile::load(s["profiles"][row.player], config_.economy));
                        Mail m;
                        m.id = "tournament:" + key;
                        m.reward = bundle(cfg["rewards"][row.rank - 1]);
                        rr = e.deliver(m);
                        if (!rr)
                            return {rr, {}};
                        s.member("profiles").set(row.player, e.profile().save());
                    }
                }
            }
            t.set("results", results);
            t.set("closed", Json::fromBool(true));
            return ok(results);
        }
        return fail(Code::Unsupported, action);
    }
    return fail(Code::Unsupported, r.service);
}
void project([[maybe_unused]] const ServiceConfig &config_, const Json &state_, const std::string &p,
             [[maybe_unused]] Time now, Json &out) {
    auto tournaments = Json::object();
    for (auto &[key, v] : state_["tournaments"].members()) {
        auto t = Json::object();
        t.set("entry", v["entrants"][p]);
        t.set("result", v["results"][p]);
        t.set("closed", Json::fromBool(v.getBool("closed")));
        tournaments.set(key, t);
    }
    out.set("tournaments", tournaments);
}
} // namespace
void modules::tournaments(Modules &m) {
    m.features.insert("tournaments");
    m.handlers["tournament"] = handle;
    m.views.push_back(project);
}
} // namespace aiverse::meta
