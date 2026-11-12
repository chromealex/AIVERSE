#include "EconomyInternal.h"
#include <algorithm>
namespace aiverse::meta {
Result Economy::record(const std::string &metric, Amount delta, const std::string &op) {
    if (metric.empty() || metric.rfind("@meta/", 0) == 0 || delta < 0 || delta > kMaxAmount)
        return {Code::Invalid, "metric"};
    auto body = payload("record", metric, {{"delta", delta}});
    auto r = replay(op, body);
    if (r.code != Code::Ok)
        return r;
    auto next = profile_;
    auto &n = next.progress[metric];
    if (n > kMaxAmount - delta)
        return {Code::Limit, metric};
    n += delta;
    next.receipts[op] = body;
    return publish(std::move(next));
}
Result Economy::recordEvent(const std::string &metric, Amount delta, const Context &c, const std::string &op) {
    if (metric.empty() || metric.rfind("@meta/", 0) == 0 || delta < 0 || delta > kMaxAmount || !validTime(c.now))
        return {Code::Invalid, "event"};
    if (!profile_.player.empty() && profile_.player != c.player)
        return {Code::Denied, "player"};
    // Event timestamp belongs to the immutable event, not to the retry wall clock.
    auto body = payload("event", metric, {{"delta", delta}, {"time", c.now}});
    auto r = replay(op, body);
    if (r.code != Code::Ok)
        return r;
    auto next = profile_;
    std::vector<std::string> metrics{metric};
    for (auto &[id, t] : config_.tracks)
        if (t.metric == metric && t.schedule.period && t.schedule.active(c.now))
            metrics.push_back(key(id, t.schedule.bucket(c.now)));
    for (auto &m : metrics) {
        auto &n = next.progress[m];
        if (n > kMaxAmount - delta)
            return {Code::Limit, m};
        n += delta;
    }
    next.receipts[op] = body;
    return publish(std::move(next));
}
Result Economy::claim(const std::string &id, size_t tier, const Context &c, const std::string &op) {
    if (!profile_.player.empty() && profile_.player != c.player)
        return {Code::Denied, "player"};
    auto body = payload("claim", id + "/" + std::to_string(tier));
    auto r = replay(op, body);
    if (r.code != Code::Ok)
        return r;
    auto it = config_.tracks.find(id);
    if (it == config_.tracks.end())
        return {Code::Missing, id};
    const auto &t = it->second;
    if (tier >= t.thresholds.size() || tier >= t.rewards.size())
        return {Code::Invalid, "tier"};
    if (!t.schedule.active(c.now) || !t.condition.matches(c) || (!t.premiumTag.empty() && !c.tags.count(t.premiumTag)))
        return {Code::Unavailable, id};
    const auto metric = t.schedule.period ? key(id, t.schedule.bucket(c.now)) : t.metric;
    if (value(profile_.progress, metric) < t.thresholds[tier])
        return {Code::Insufficient, t.metric};
    auto claimKey = key(id, t.schedule.bucket(c.now)) + "/" + std::to_string(tier);
    if (profile_.claims.count(claimKey))
        return {Code::Duplicate, {}};
    auto next = profile_;
    r = apply(next, {}, t.rewards[tier]);
    if (!r)
        return r;
    next.claims.insert(claimKey);
    next.receipts[op] = body;
    return publish(std::move(next));
}
Result Economy::claimDaily(const std::string &id, Time now, const std::string &op) {
    auto body = payload("daily", id);
    auto r = replay(op, body);
    if (r.code != Code::Ok)
        return r;
    auto i = config_.daily.find(id);
    if (i == config_.daily.end())
        return {Code::Missing, id};
    const auto &d = i->second;
    if (!validTime(now) || now < d.start || d.period <= 0 || d.rewards.empty())
        return {Code::Unavailable, id};
    auto next = profile_;
    auto &state = next.daily[id];
    auto bucket = (now - d.start) / d.period;
    if (bucket <= state.bucket)
        return {Code::Unavailable, "already claimed or clock rollback"};
    if (d.resetOnMiss && state.bucket >= 0 && bucket > state.bucket + 1)
        state.streak = 0;
    if (!d.loop && uint64_t(state.streak) >= d.rewards.size())
        return {Code::Limit, id};
    size_t index = size_t(state.streak) % d.rewards.size();
    r = apply(next, {}, d.rewards[index]);
    if (!r)
        return r;
    if (state.streak == kMaxAmount)
        return {Code::Limit, "daily streak"};
    ++state.streak;
    state.bucket = bucket;
    next.receipts[op] = body;
    return publish(std::move(next));
}
Result Economy::regenerate(const std::string &id, Time now) {
    auto it = config_.energy.find(id);
    if (it == config_.energy.end())
        return {Code::Missing, id};
    const auto &e = it->second;
    if (!validTime(now) || e.interval <= 0 || e.amount <= 0 || e.cap < 0)
        return {Code::Invalid, id};
    auto next = profile_;
    auto i = next.energy.find(id);
    if (i == next.energy.end()) {
        next.energy[id] = now;
        return publish(std::move(next));
    }
    if (now < i->second)
        return {Code::Conflict, "clock rollback"};
    auto &balance = next.balances[e.asset];
    Amount room = std::max<Amount>(0, e.cap - balance);
    if (!room) {
        if (i->second == now)
            return {};
        i->second = now;
        return publish(std::move(next));
    }
    Amount ticks = (now - i->second) / e.interval;
    if (!ticks)
        return {};
    Amount needed = room / e.amount + (room % e.amount != 0);
    Amount grant = ticks >= needed ? room : ticks * e.amount;
    auto r = apply(next, {}, {{e.asset, grant}});
    if (!r)
        return r;
    i->second = ticks >= needed ? now : i->second + ticks * e.interval;
    return publish(std::move(next));
}
} // namespace aiverse::meta
