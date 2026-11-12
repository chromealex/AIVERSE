#include "Social.h"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace aiverse::meta {
namespace {
inline int64_t readInt(const Json &j, int64_t low = 0, int64_t high = kMaxAmount) {
    if (!j.isNumber())
        throw std::runtime_error("meta leaderboard: expected integer");
    int64_t v;
    auto &s = j.numberLiteral();
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || v < low || v > high)
        throw std::runtime_error("meta leaderboard: invalid integer");
    return v;
}

} // namespace
BoardConfig BoardConfig::parse(const Json &j) {
    if (!j.isObject())
        throw std::runtime_error("meta board: expected object");
    const std::set<std::string> known{"ascending", "update",     "rank",   "minimum",
                                      "maximum",   "maxPlayers", "season", "metadata"};
    std::set<std::string> seen;
    for (auto &[k, v] : j.members()) {
        (void)v;
        if (!known.count(k) || !seen.insert(k).second)
            throw std::runtime_error("meta board: unknown/duplicate key " + k);
    }
    BoardConfig c;
    if (j.has("metadata")) {
        if (!j["metadata"].isObject())
            throw std::runtime_error("meta board: metadata must be object");
        c.metadata = j["metadata"];
    }
    if (j.has("ascending") && j["ascending"].type() != Json::Type::Bool)
        throw std::runtime_error("meta board: ascending must be bool");
    c.ascending = j.getBool("ascending");
    if (j.has("minimum"))
        c.minimum = readInt(j["minimum"], -kMaxAmount);
    if (j.has("maximum"))
        c.maximum = readInt(j["maximum"], -kMaxAmount);
    if (j.has("maxPlayers"))
        c.maxPlayers = size_t(readInt(j["maxPlayers"], 1, 10000000));
    if (c.minimum > c.maximum)
        throw std::runtime_error("meta board: minimum > maximum");
    if (j.has("update")) {
        auto s = j.getStr("update");
        if (s == "best")
            c.update = ScoreUpdate::Best;
        else if (s == "latest")
            c.update = ScoreUpdate::Latest;
        else if (s == "sum")
            c.update = ScoreUpdate::Sum;
        else
            throw std::runtime_error("meta board: unknown update");
    }
    if (j.has("rank")) {
        auto s = j.getStr("rank");
        if (s == "ordinal")
            c.rank = RankStyle::Ordinal;
        else if (s == "competition")
            c.rank = RankStyle::Competition;
        else if (s == "dense")
            c.rank = RankStyle::Dense;
        else
            throw std::runtime_error("meta board: unknown rank");
    }
    if (j.has("season")) {
        Json root = Json::object(), offers = Json::object(), o = Json::object();
        o.set("schedule", j["season"]);
        offers.set("s", o);
        root.set("offers", offers);
        c.season = Config::parse(root).offers.at("s").schedule;
    }
    return c;
}
} // namespace aiverse::meta
