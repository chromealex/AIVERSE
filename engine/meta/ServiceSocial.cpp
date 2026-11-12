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
    if (service == "friends") {
        if (!x.has("friends"))
            return missing();
        keys(a, {"target"});
        auto t = target();
        if (t == p)
            return denied();
        auto &fs = s.member("friends");
        auto &req = s.member("requests");
        if (action == "block") {
            object(s.member("blocks"), p).set(t, Json::fromBool(true));
            object(fs, p).erase(t);
            object(fs, t).erase(p);
            object(req, p).erase(t);
            object(req, t).erase(p);
            return ok();
        }
        if (action == "unblock") {
            object(s.member("blocks"), p).erase(t);
            return ok();
        }
        if (action == "remove") {
            object(fs, p).erase(t);
            object(fs, t).erase(p);
            return ok();
        }
        if (action == "reject") {
            object(req, p).erase(t);
            return ok();
        }
        if (blocked(s, p, t))
            return denied();
        auto max = x["friends"].getInt("maxFriends", 100);
        auto requests = x["friends"].getInt("maxRequests", 100);
        if (fs[p].has(t))
            return ok();
        if (action == "request") {
            if (req[t].size() >= size_t(requests))
                return limit();
            object(req, t).set(p, Json::fromBool(true));
            return ok();
        }
        if (action == "accept") {
            if (!req[p].has(t))
                return denied();
            if (fs[p].size() >= size_t(max) || fs[t].size() >= size_t(max))
                return limit();
            object(fs, p).set(t, Json::fromBool(true));
            object(fs, t).set(p, Json::fromBool(true));
            object(req, p).erase(t);
            object(req, t).erase(p);
            return ok();
        }
        return fail(Code::Unsupported, action);
    }
    if (service == "board") {
        keys(a, {"id", "score"});
        auto key = id(a["id"]);
        if (!config_.boards.count(key))
            return missing();
        if (!who.authority)
            return denied();
        if (action != "submit")
            return fail(Code::Unsupported, action);
        Leaderboard board(config_.boards.at(key));
        if (s["boards"].has(key)) {
            auto rr = board.restore(s["boards"][key]);
            if (!rr)
                return {rr, {}};
        }
        auto rr = board.submit(p, integer(a["score"], -kMaxAmount), now, op);
        if (rr)
            s.member("boards").set(key, board.save());
        return {rr, {}};
    }
    if (service == "chat") {
        keys(a, {"id", "text", "kind", "sequence", "target", "until"});
        auto key = id(a["id"]);
        if (!config_.chats.count(key))
            return missing();
        Chat chat(config_.chats.at(key), [&](const std::string &user, const std::string &verb) {
            return user == p && (verb != "delete" || who.authority);
        });
        if (s["chats"].has(key)) {
            auto rr = chat.restore(s["chats"][key]);
            if (!rr)
                return {rr, {}};
        }
        Result rr;
        if (action == "send") {
            require(a["text"].isString(), "text");
            rr = chat.send(p, a.has("kind") ? id(a["kind"]) : "text", a["text"].str(), now, op);
        } else if (action == "read")
            rr = chat.markRead(p, integer(a["sequence"]));
        else if (action == "report")
            rr = chat.report(p, integer(a["sequence"], 1));
        else if (action == "delete")
            rr = chat.remove(p, integer(a["sequence"], 1));
        else if (action == "mute") {
            if (!who.authority)
                return denied();
            chat.mute(target(), integer(a["until"]));
        } else if (action == "block" || action == "unblock")
            chat.block(p, target(), action == "block");
        else
            return fail(Code::Unsupported, action);
        if (rr)
            s.member("chats").set(key, chat.save());
        return {rr, {}};
    }
    return fail(Code::Unsupported, r.service);
}
void project([[maybe_unused]] const ServiceConfig &config_, const Json &state_, const std::string &p,
             [[maybe_unused]] Time now, Json &out) {
    out.set("friends", state_["friends"][p]);
    out.set("requests", state_["requests"][p]);
    auto boards = Json::object();
    for (auto &[key, cfg] : config_.boards) {
        Leaderboard board(cfg);
        if (state_["boards"].has(key))
            require(bool(board.restore(state_["boards"][key])), "invalid board state");
        auto rows = Json::array();
        for (auto &row : board.page(now).entries) {
            auto v = Json::object();
            v.set("player", str(row.player));
            v.set("score", number(row.score));
            v.set("rank", number(int64_t(row.rank)));
            rows.push(v);
        }
        boards.set(key, rows);
    }
    out.set("boards", boards);
    auto chats = Json::object();
    for (auto &[key, cfg] : config_.chats) {
        Chat chat(cfg, [&](const std::string &user, const std::string &verb) { return user == p && verb == "read"; });
        if (state_["chats"].has(key))
            require(bool(chat.restore(state_["chats"][key])), "invalid chat state");
        auto rows = Json::array();
        for (auto &m : chat.history(p, now)) {
            auto v = Json::object();
            v.set("sequence", number(m.sequence));
            v.set("sender", str(m.sender));
            v.set("text", str(m.text));
            rows.push(v);
        }
        chats.set(key, rows);
    }
    out.set("chats", chats);
}
} // namespace
void modules::social(Modules &m) {
    m.features.insert("social");
    m.handlers["friends"] = handle;
    m.handlers["board"] = handle;
    m.handlers["chat"] = handle;
    m.views.push_back(project);
}
} // namespace aiverse::meta
