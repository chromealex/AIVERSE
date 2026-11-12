#include "LiveOpsInternal.h"
#include "Modules.h"
namespace aiverse::meta::live {
Json wars(Call &c) {
    const auto &a = c.args();
    auto action = c.req.action;
    auto global = c.shared[c.id];
    if (!global.isObject())
        global = Json::object();

    keys(a, {"id", "amount"});
    auto entries = global["entries"];
    if (!entries.isObject())
        entries = Json::object();
    if (action == "enroll") {
        need(active(c.def, c.now) && !global.getBool("paired"), Code::Unavailable, "war enrollment closed");
        auto clan = c.state["membership"].getStr(c.who.player);
        auto members = c.state["clans"][clan]["members"];
        need(!clan.empty() && members.getStr(c.who.player) == "owner", Code::Denied, "clan owner");
        need(!entries.has(clan), Code::Conflict, "clan enrolled");
        int64_t strength = 0;
        for (auto &[p, role] : members.members()) {
            (void)role;
            need(!c.state["live"]["players"][p]["wars"].has(c.id), Code::Conflict, "member already enrolled");
            strength += c.state["extras"][p].getInt("league");
        }
        entries.set(clan, row({{"members", members},
                               {"strength", number(strength)},
                               {"score", number(0)},
                               {"contributions", Json::object()}}));
        for (auto &[p, role] : members.members()) {
            (void)role;
            object(c.player(p), "wars").set(c.id, row({{"clan", str(clan)}}));
        }
    } else if (action == "pair") {
        c.authority();
        need(active(c.def, c.now) && !global.getBool("paired"), Code::Unavailable, "war already paired or closed");
        need(entries.size() >= 2, Code::Insufficient, "war clans");
        std::vector<std::pair<std::string, Json>> sorted(entries.members().begin(), entries.members().end());
        std::sort(sorted.begin(), sorted.end(), [](auto &a, auto &b) {
            return a.second.getInt("strength") != b.second.getInt("strength")
                       ? a.second.getInt("strength") < b.second.getInt("strength")
                       : a.first < b.first;
        });
        auto pairs = Json::array();
        for (size_t i = 0; i < sorted.size(); i += 2) {
            auto pair = Json::array();
            pair.push(str(sorted[i].first));
            if (i + 1 < sorted.size())
                pair.push(str(sorted[i + 1].first));
            pairs.push(pair);
        }
        global.set("pairs", pairs);
        global.set("paired", Json::fromBool(true));
    } else if (action == "score") {
        c.authority();
        need(active(c.def, c.now) && global.getBool("paired") && !global.getBool("settled"), Code::Unavailable,
             "war not active");
        auto clan = c.self[c.id].getStr("clan");
        need(entries[clan]["members"].has(c.who.player), Code::Denied, "war roster");
        auto entry = entries[clan];
        auto n = integer(a["amount"], 1);
        entry.set("score", number(add(entry.getInt("score"), n)));
        object(entry, "contributions").set(c.who.player, number(add(entry["contributions"].getInt(c.who.player), n)));
        entries.set(clan, entry);
    } else if (action == "settle") {
        c.authority();
        need(c.def.getInt("end") && c.now >= c.def.getInt("end") && global.getBool("paired") &&
                 !global.getBool("settled"),
             Code::Unavailable, "war settlement");
        for (auto &pair : global["pairs"].items())
            for (size_t i = 0; i < pair.size(); ++i) {
                auto clan = pair[i].str();
                auto entry = entries[clan];
                int outcome = pair.size() == 1
                                  ? 0
                                  : (entry.getInt("score") > entries[pair[1 - i].str()].getInt("score")   ? 1
                                     : entry.getInt("score") < entries[pair[1 - i].str()].getInt("score") ? -1
                                                                                                          : 0);
                for (auto &[p, role] : entry["members"].members()) {
                    (void)role;
                    auto result = row({{"clan", str(clan)}, {"score", entry["score"]}, {"outcome", number(outcome)}});
                    object(c.player(p), "wars").set(c.id, result);
                    if (entry["contributions"].getInt(p) >= c.def.getInt("minimumContribution"))
                        c.mail(p,
                               bundle(c.def[outcome > 0   ? "winReward"
                                            : outcome < 0 ? "loseReward"
                                                          : "drawReward"]),
                               clan);
                }
            }
        global.set("settled", Json::fromBool(true));
    } else
        throw Stop(Code::Unsupported, action);
    global.set("entries", entries);
    c.shared.set(c.id, global);
    return row({{"paired", Json::fromBool(global.getBool("paired"))},
                {"settled", Json::fromBool(global.getBool("settled"))},
                {"pairs", global["pairs"]}});
}
} // namespace aiverse::meta::live
namespace aiverse::meta {
void modules::wars(Modules &m) {
    m.features.insert("wars");
    m.live["wars"] = live::wars;
}
} // namespace aiverse::meta
