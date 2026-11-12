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
    if (service == "clan") {
        if (!x.has("clans"))
            return missing();
        auto cfg = x["clans"];
        auto &clans = s.member("clans");
        auto &membership = s.member("membership");
        if (action == "create") {
            keys(a, {"id"});
            auto cid = id(a["id"]);
            if (clans.has(cid) || membership.has(p))
                return fail(Code::Conflict, "clan exists or already joined");
            auto rr = transact(s, config_, p, cfg.has("createCost") ? bundle(cfg["createCost"]) : Bundle{}, {}, op);
            if (!rr)
                return {rr, {}};
            auto g = Json::object();
            auto members = Json::object();
            members.set(p, str("owner"));
            g.set("members", members);
            g.set("invites", Json::object());
            g.set("bank", Json::object());
            g.set("claimed", Json::object());
            clans.set(cid, g);
            membership.set(p, str(cid));
            return ok();
        }
        keys(a, {"id", "target", "role", "asset", "amount", "goal"});
        auto cid = id(a["id"]);
        if (!clans.has(cid))
            return missing();
        auto &g = clans.member(cid);
        auto role = g["members"].getStr(p);
        bool officer = role == "owner" || role == "officer";
        if (action == "join") {
            if (membership.has(p) || !g["invites"].has(p) || g["invites"].getInt(p) <= now)
                return denied();
            if (g["members"].size() >= size_t(cfg.getInt("maxMembers", 50)))
                return limit();
            g.member("members").set(p, str("member"));
            g.member("invites").erase(p);
            membership.set(p, str(cid));
            return ok();
        }
        if (role.empty())
            return denied();
        if (action == "invite") {
            auto t = target();
            if (!officer || blocked(s, p, t) || membership.has(t))
                return denied();
            if (g["invites"].size() >= size_t(cfg.getInt("maxMembers", 50)) * 2) {
                for (auto it = g.member("invites").membersMut().begin();
                     it != g.member("invites").membersMut().end();) {
                    if (it->second.integer() <= now)
                        it = g.member("invites").membersMut().erase(it);
                    else
                        ++it;
                }
            }
            if (g["invites"].size() >= size_t(cfg.getInt("maxMembers", 50)) * 2)
                return limit();
            auto duration = cfg.getInt("inviteSeconds", 86400);
            if (duration > kMaxAmount - now)
                return limit();
            g.member("invites").set(t, number(now + duration));
            return ok();
        }
        if (action == "role" || action == "transfer") {
            auto t = target();
            if (role != "owner" || !g["members"].has(t) || t == p)
                return denied();
            if (action == "transfer") {
                g.member("members").set(t, str("owner"));
                g.member("members").set(p, str("officer"));
            } else {
                auto v = id(a["role"]);
                if (v != "member" && v != "officer")
                    return denied();
                g.member("members").set(t, str(v));
            }
            return ok();
        }
        if (action == "leave" || action == "kick") {
            auto t = action == "leave" ? p : target();
            auto tr = g["members"].getStr(t);
            if (tr.empty() || (action == "kick" && (!officer || tr == "owner" || (tr == "officer" && role != "owner"))))
                return denied();
            if (tr == "owner" && g["members"].size() > 1)
                return fail(Code::Conflict, "transfer ownership first");
            g.member("members").erase(t);
            membership.erase(t);
            if (g["members"].size() == 0)
                clans.erase(cid);
            return ok();
        }
        if (action == "donate") {
            auto asset = id(a["asset"]);
            auto n = integer(a["amount"], 1);
            if (!config_.economy.assets.count(asset))
                return missing();
            auto total = g["bank"].getInt(asset);
            if (n > kMaxAmount - total)
                return limit();
            auto rr = transact(s, config_, p, {{asset, n}}, {}, op);
            if (!rr)
                return {rr, {}};
            g.member("bank").set(asset, number(total + n));
            return ok();
        }
        if (action == "goal") {
            auto goal = id(a["goal"]);
            if (!cfg["goals"].has(goal))
                return missing();
            auto def = cfg["goals"][goal];
            auto claim = pairKey(p, goal);
            if (g["claimed"].has(claim))
                return {{Code::Duplicate, {}}, {}};
            if (g["bank"].getInt(def.getStr("asset")) < def.getInt("amount"))
                return fail(Code::Insufficient, "clan goal");
            auto rr = transact(s, config_, p, {}, bundle(def["reward"]), op);
            if (!rr)
                return {rr, {}};
            g.member("claimed").set(claim, Json::fromBool(true));
            return ok();
        }
        return fail(Code::Unsupported, action);
    }
    return fail(Code::Unsupported, r.service);
}
void project([[maybe_unused]] const ServiceConfig &config_, const Json &state_, const std::string &p,
             [[maybe_unused]] Time now, Json &out) {
    out.set("clanId", state_["membership"][p]);
    auto cid = state_["membership"].getStr(p);
    if (!cid.empty()) {
        auto clan = state_["clans"][cid];
        clan.erase("claimed");
        out.set("clan", clan);
    }
    auto invites = Json::array();
    for (auto &[key, g] : state_["clans"].members())
        if (g["invites"].getInt(p) > now)
            invites.push(str(key));
    out.set("invites", invites);
}
} // namespace
void modules::clans(Modules &m) {
    m.features.insert("clans");
    m.handlers["clan"] = handle;
    m.views.push_back(project);
}
} // namespace aiverse::meta
