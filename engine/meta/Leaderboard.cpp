#include "Social.h"
#include <algorithm>
#include <charconv>
#include <stdexcept>

namespace aiverse::meta {
namespace {
int64_t readInt(const Json &j, int64_t low = 0, int64_t high = kMaxAmount) {
    if (!j.isNumber())
        throw std::runtime_error("meta leaderboard: expected integer");
    int64_t v;
    auto &s = j.numberLiteral();
    auto r = std::from_chars(s.data(), s.data() + s.size(), v);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || v < low || v > high)
        throw std::runtime_error("meta leaderboard: invalid integer");
    return v;
}
std::string body(const std::string &player, Amount score, int64_t season) {
    Json j = Json::array();
    j.push(Json::string(player));
    j.push(Json::number(score));
    j.push(Json::number(season));
    return j.dump(-1);
}
} // namespace
Leaderboard::Leaderboard(BoardConfig c) : config_(c) {
    if (c.minimum < -kMaxAmount || c.maximum > kMaxAmount || c.minimum > c.maximum || !c.maxPlayers ||
        c.maxPlayers > 10000000 || c.season.start < 0 || c.season.period < 0 || c.season.duration < 0 ||
        c.season.start > kMaxAmount || c.season.period > kMaxAmount || c.season.end < 0 || c.season.end > kMaxAmount ||
        (c.season.end && c.season.end <= c.season.start) ||
        (c.season.duration && (!c.season.period || c.season.duration > c.season.period)))
        throw std::runtime_error("meta: invalid board config");
}
Result Leaderboard::submit(const std::string &player, Amount score, Time now, const std::string &op) {
    if (player.empty() || op.empty() || op.size() > 512 || now < 0 || now > kMaxAmount)
        return {Code::Invalid, "submission"};
    auto season = config_.season.bucket(now);
    auto b = body(player, score, season);
    auto receipt = receipts_.find(op);
    if (receipt != receipts_.end())
        return receipt->second == b ? Result{Code::Duplicate, {}} : Result{Code::Conflict, "operation"};
    if (!config_.season.active(now))
        return {Code::Unavailable, "season"};
    if (score < config_.minimum || score > config_.maximum)
        return {Code::Invalid, "score"};
    const std::map<std::string, Amount> empty;
    auto si = seasons_.find(season);
    const auto &scores = si == seasons_.end() ? empty : si->second;
    auto it = scores.find(player);
    if (it == scores.end() && scores.size() >= config_.maxPlayers)
        return {Code::Limit, "players"};
    Amount result = score;
    if (it != scores.end()) {
        if (config_.update == ScoreUpdate::Sum)
            result = it->second + score; // operands bounded by kMaxAmount
        else if (config_.update == ScoreUpdate::Best)
            result = config_.ascending ? std::min(score, it->second) : std::max(score, it->second);
    }
    if (result < config_.minimum || result > config_.maximum || revision_ >= kMaxAmount)
        return {Code::Limit, "score/revision"};
    seasons_[season][player] = result;
    receipts_[op] = b;
    ++revision_;
    return {};
}
BoardPage Leaderboard::page(Time now, size_t offset, size_t limit, const std::set<std::string> &players) const {
    BoardPage p;
    p.revision = revision_;
    if (now < 0 || now > kMaxAmount)
        return p;
    auto it = seasons_.find(config_.season.bucket(now));
    if (it == seasons_.end())
        return p;
    for (auto &[id, n] : it->second)
        if (players.empty() || players.count(id))
            p.entries.push_back({id, n, 0});
    std::sort(p.entries.begin(), p.entries.end(), [&](auto &a, auto &b) {
        return a.score == b.score ? a.player < b.player : config_.ascending ? a.score < b.score : a.score > b.score;
    });
    size_t rank = 0;
    for (size_t i = 0; i < p.entries.size(); ++i) {
        if (config_.rank == RankStyle::Ordinal)
            rank = i + 1;
        else if (!i || p.entries[i].score != p.entries[i - 1].score)
            rank = config_.rank == RankStyle::Dense ? rank + 1 : i + 1;
        p.entries[i].rank = rank;
    }
    p.total = p.entries.size();
    offset = std::min(offset, p.total);
    limit = std::min(limit, p.total - offset);
    p.entries = std::vector<Score>(p.entries.begin() + offset, p.entries.begin() + offset + limit);
    return p;
}
BoardPage Leaderboard::around(Time now, const std::string &player, size_t radius,
                              const std::set<std::string> &players) const {
    auto all = page(now, 0, config_.maxPlayers, players);
    for (size_t i = 0; i < all.entries.size(); ++i)
        if (all.entries[i].player == player) {
            radius = std::min(radius, config_.maxPlayers);
            return page(now, i > radius ? i - radius : 0, radius * 2 + 1, players);
        }
    all.entries.clear();
    return all;
}
Json Leaderboard::save() const {
    Json j = Json::object();
    j.set("version", Json::number(int64_t(1)));
    j.set("revision", Json::number(revision_));
    Json ss = Json::array();
    for (auto &[n, scores] : seasons_) {
        Json s = Json::object(), v = Json::object();
        s.set("season", Json::number(n));
        for (auto &[id, score] : scores)
            v.set(id, Json::number(score));
        s.set("scores", v);
        ss.push(s);
    }
    j.set("seasons", ss);
    Json r = Json::object();
    for (auto &[k, v] : receipts_)
        r.set(k, Json::string(v));
    j.set("receipts", r);
    return j;
}
Result Leaderboard::restore(const Json &j) {
    try {
        if (!j.isObject() || readInt(j["version"]) != 1 || !j["seasons"].isArray() || !j["receipts"].isObject())
            return {Code::Invalid, "board snapshot"};
        auto rev = readInt(j["revision"]);
        decltype(seasons_) ss;
        decltype(receipts_) rs;
        for (auto &s : j["seasons"].items()) {
            auto season = readInt(s["season"]);
            if (ss.count(season) || !s["scores"].isObject() || s["scores"].size() > config_.maxPlayers)
                return {Code::Invalid, "season"};
            auto &scores = ss[season];
            for (auto &[id, v] : s["scores"].members()) {
                if (id.empty() || scores.count(id))
                    return {Code::Invalid, "player"};
                scores[id] = readInt(v, config_.minimum, config_.maximum);
            }
        }
        for (auto &[id, v] : j["receipts"].members()) {
            if (id.empty() || !v.isString() || rs.count(id))
                return {Code::Invalid, "receipt"};
            rs[id] = v.str();
        }
        seasons_ = std::move(ss);
        receipts_ = std::move(rs);
        revision_ = rev;
        return {};
    } catch (const std::exception &e) {
        return {Code::Invalid, e.what()};
    }
}
} // namespace aiverse::meta
