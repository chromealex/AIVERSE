#include "LiveOps.h"
#include "ServiceInternal.h"
namespace aiverse::meta {
using namespace detail;
ServiceConfig ServiceConfig::parse(const Json &j) {
    keys(j, {"version", "economy", "boards", "chats", "extensions"});
    if (j.has("version"))
        require(integer(j["version"], 1, 1) == 1, "version");
    ServiceConfig c;
    c.economy = Config::parse(j["economy"]);
    for (auto key : {"boards", "chats"})
        if (j.has(key))
            require(j[key].isObject(), "named definitions");
    for (auto &[k, v] : j["boards"].members()) {
        id(str(k));
        require(!c.boards.count(k), "duplicate board");
        c.boards[k] = BoardConfig::parse(v);
    }
    for (auto &[k, v] : j["chats"].members()) {
        id(str(k));
        require(!c.chats.count(k), "duplicate chat");
        c.chats[k] = ChatConfig::parse(v);
    }
    c.extensions = j.has("extensions") ? canonical(j["extensions"]) : Json::object();
    auto &x = c.extensions;
    keys(x, {"initial", "friends", "clans", "items", "entitlements", "tournaments", "liveops"});
    if (x.has("initial"))
        checkBundle(x["initial"], c.economy);
    if (x.has("friends")) {
        keys(x["friends"], {"maxFriends", "maxRequests"});
        for (auto &[k, v] : x["friends"].members()) {
            (void)k;
            integer(v, 1, 10000);
        }
    }
    if (x.has("clans")) {
        auto &g = x["clans"];
        keys(g, {"maxMembers", "inviteSeconds", "createCost", "goals"});
        if (g.has("maxMembers"))
            integer(g["maxMembers"], 1, 10000);
        if (g.has("inviteSeconds"))
            integer(g["inviteSeconds"], 1);
        if (g.has("createCost"))
            checkBundle(g["createCost"], c.economy);
        if (g.has("goals")) {
            require(g["goals"].isObject(), "goals");
            for (auto &[k, v] : g["goals"].members()) {
                id(str(k));
                keys(v, {"asset", "amount", "reward"});
                require(c.economy.assets.count(id(v["asset"])), "goal asset");
                integer(v["amount"], 1);
                checkBundle(v["reward"], c.economy);
            }
        }
    }
    for (auto key : {"items", "entitlements", "tournaments"})
        if (x.has(key))
            require(x[key].isObject(), key);
    for (auto &[k, v] : x["items"].members()) {
        id(str(k));
        keys(v, {"asset", "slots", "upgradeCosts", "salvage", "mergeCount", "mergeTo", "metadata"});
        if (v.has("asset"))
            require(c.economy.assets.count(id(v["asset"])), "item redemption asset");
        require(v["slots"].isArray(), "item slots");
        std::set<std::string> slots;
        for (auto &s : v["slots"].items())
            require(slots.insert(id(s)).second, "duplicate slot");
        require(v["upgradeCosts"].isArray(), "upgradeCosts");
        for (auto &b : v["upgradeCosts"].items())
            checkBundle(b, c.economy);
        checkBundle(v["salvage"], c.economy);
        if (v.has("mergeCount")) {
            integer(v["mergeCount"], 2, 100);
            require(x["items"].has(id(v["mergeTo"])), "merge target");
        } else
            require(!v.has("mergeTo"), "mergeCount missing");
        if (v.has("metadata"))
            require(v["metadata"].isObject(), "metadata");
    }
    for (auto &[k, v] : x["entitlements"].members()) {
        id(str(k));
        keys(v, {"tag", "metadata"});
        id(v["tag"]);
        if (v.has("metadata"))
            require(v["metadata"].isObject(), "metadata");
    }
    for (auto &[k, v] : x["tournaments"].members()) {
        id(str(k));
        keys(v, {"start", "end", "groupSize", "promote", "demote", "maxLeague", "rewards", "board"});
        auto start = integer(v["start"]);
        require(integer(v["end"], 1) > start, "tournament interval");
        auto size = integer(v["groupSize"], 2, 10000);
        require(integer(v["promote"]) + integer(v["demote"]) <= size, "promotion overlap");
        integer(v["maxLeague"], 0, 100);
        require(v["rewards"].isArray() && v["rewards"].size() <= size_t(size), "rewards");
        for (auto &b : v["rewards"].items())
            checkBundle(b, c.economy);
        auto board = BoardConfig::parse(v["board"]);
        require(board.maxPlayers >= size_t(size), "board capacity below group size");
        require(!board.season.period && !board.season.start && !board.season.end, "tournament owns season");
    }
    if (x.has("liveops"))
        validateLiveOps(x["liveops"], c.economy);
    return c;
}
} // namespace aiverse::meta
