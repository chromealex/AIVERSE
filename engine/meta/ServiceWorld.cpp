#include "ServicePolicy.h"
// Persistent worlds (docs/city-server.md): what a player owns inside a world that keeps running without him.
// The world server is the only writer (authority): it takes the record when the player enters, stores it while he
// plays and when he leaves, and asks who has been away too long (their property goes back to the world).
// State: extras[player].world = {account, seen, online, op}. One receipt per player is kept: a newer save
// replaces the older one, so a server that saves every minute does not grow the state.
namespace aiverse::meta {
namespace {
constexpr size_t kMaxAccountBytes = 65536;
ServiceReply handle(const ServiceConfig &, Json &s, const Principal &who, const ServiceRequest &r, Time now) {
    using namespace detail;
    if (!who.authority)
        return fail(Code::Denied, "permission");
    const auto &a = r.arguments;
    auto p = who.player;
    Json &extra = object(s.member("extras"), p);
    if (!extra.has("world"))
        extra.set("world", Json::object());
    Json &w = object(extra, "world");
    if (w.has("op")) // the previous world receipt of this player is superseded
        s.member("receipts").erase(policy::pairKey(p, w.getStr("op")));
    w.set("op", str(r.operation));
    if (r.action == "absent") { // players offline longer than `seconds`
        keys(a, {"seconds"});
        auto seconds = integer(a["seconds"], 0, kMaxAmount);
        auto list = Json::array();
        for (auto &[player, other] : s["extras"].members()) {
            const Json &record = other["world"];
            if (record.isObject() && record.has("seen") && !record.getBool("online") && now - record.getInt("seen") > seconds)
                list.push(str(player));
        }
        auto out = Json::object();
        out.set("players", list);
        return {{}, out};
    }
    auto out = Json::object();
    if (r.action == "enter") {
        keys(a, {});
        out.set("known", Json::fromBool(w.has("account")));
        if (w.has("account"))
            out.set("account", w["account"]);
        out.set("away", number(w.has("seen") ? std::max<Time>(0, now - w.getInt("seen")) : 0));
        w.set("online", Json::fromBool(true));
    } else if (r.action == "save" || r.action == "leave") {
        keys(a, {"account"});
        require(a["account"].isObject() && a["account"].dump(-1).size() <= kMaxAccountBytes, "account");
        w.set("account", a["account"]);
        w.set("online", Json::fromBool(r.action == "save"));
    } else
        return fail(Code::Unsupported, r.action);
    w.set("seen", number(now));
    return {{}, out};
}
} // namespace
void modules::world(Modules &m) {
    m.features.insert("world");
    m.handlers["world"] = handle;
}
} // namespace aiverse::meta
