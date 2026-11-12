#include "EconomyInternal.h"
#include <algorithm>
#include <limits>
#include <stdexcept>

namespace aiverse::meta {
bool Condition::matches(const Context &c) const {
    for (auto &[k, v] : minimum) {
        auto i = c.values.find(k);
        if (i == c.values.end() || i->second < v)
            return false;
    }
    for (auto &[k, v] : maximum) {
        auto i = c.values.find(k);
        if (i == c.values.end() || i->second > v)
            return false;
    }
    for (auto &t : requiredTags)
        if (!c.tags.count(t))
            return false;
    for (auto &t : excludedTags)
        if (c.tags.count(t))
            return false;
    for (auto &x : all)
        if (!x.matches(c))
            return false;
    for (auto &x : none)
        if (x.matches(c))
            return false;
    return any.empty() || std::any_of(any.begin(), any.end(), [&](auto &x) { return x.matches(c); });
}
bool Schedule::active(Time now) const {
    return validTime(now) && now >= start && (!end || now < end) &&
           (!period || !duration || (now - start) % period < duration);
}
int64_t Schedule::bucket(Time now) const {
    return now < start ? -1 : period ? (now - start) / period : 0;
}
Economy::Economy(Config config, Profile profile, Commit commit)
    : config_(std::move(config)), profile_(std::move(profile)), commit_(std::move(commit)) {
    config_ = Config::parse(config_.save()); // Typed and JSON inputs share one validator.
    profile_ = Profile::load(profile_.save(), config_);
    if (profile_.revision < 0)
        throw std::runtime_error("meta: negative revision");
    Profile empty;
    if (!apply(empty, {}, profile_.balances))
        throw std::runtime_error("meta: invalid profile balances");
}
Result Economy::replay(const std::string &op, const std::string &body) const {
    if (op.empty() || op.size() > 512)
        return {Code::Invalid, "operation id"};
    auto i = profile_.receipts.find(op);
    if (i == profile_.receipts.end())
        return {};
    return i->second == body ? Result{Code::Duplicate, {}}
                             : Result{Code::Conflict, "operation reused with different payload"};
}
Result Economy::apply(Profile &next, const Bundle &cost, const Bundle &reward) const {
    for (const auto *b : {&cost, &reward})
        for (auto &[id, n] : *b)
            if (!config_.assets.count(id) || n < 0 || n > kMaxAmount)
                return {Code::Invalid, "asset or amount: " + id};
    Bundle result = next.balances;
    for (auto &[id, n] : cost) {
        if (value(result, id) < n)
            return {Code::Insufficient, id};
        result[id] -= n;
    }
    for (auto &[id, n] : reward) {
        Amount cap = config_.assets.at(id).cap;
        if (n > cap || value(result, id) > cap - n)
            return {Code::Limit, id};
        result[id] += n;
    }
    next.balances = std::move(result);
    return {};
}
Result Economy::publish(Profile next) {
    if (profile_.revision == std::numeric_limits<int64_t>::max())
        return {Code::Limit, "revision"};
    next.revision = profile_.revision + 1;
    if (commit_ && !commit_(profile_.revision, next.save()))
        return {Code::Conflict, "storage commit"};
    profile_ = std::move(next);
    return {};
}
Result Economy::transact(const std::string &op, const Bundle &cost, const Bundle &reward) {
    auto body = payload("transaction", "", cost, reward);
    auto r = replay(op, body);
    if (r.code != Code::Ok)
        return r;
    auto next = profile_;
    r = apply(next, cost, reward);
    if (!r)
        return r;
    next.receipts[op] = body;
    return publish(std::move(next));
}
std::vector<std::string> Economy::visibleOffers(const Context &c) const {
    std::vector<std::string> ids;
    for (auto &[id, o] : config_.offers) {
        if (!o.schedule.active(c.now) || !o.condition.matches(c))
            continue;
        auto i = profile_.uses.find(key(id, o.schedule.bucket(c.now)));
        if (i != profile_.uses.end() &&
            ((o.limit && i->second.count >= o.limit) || c.now < i->second.last || c.now - i->second.last < o.cooldown))
            continue;
        ids.push_back(id);
    }
    std::stable_sort(ids.begin(), ids.end(),
                     [&](auto &a, auto &b) { return config_.offers.at(a).priority > config_.offers.at(b).priority; });
    return ids;
}
Result Economy::buy(const std::string &id, const Context &c, const std::string &op) {
    return purchase(id, c, op, false);
}
Result Economy::fulfill(const std::string &id, const Context &c, const std::string &token) {
    return purchase(id, c, "iap:" + token, true);
}
Result Economy::purchase(const std::string &id, const Context &c, const std::string &op, bool paid) {
    auto body = payload(paid ? "iap" : "buy", id);
    auto r = replay(op, body);
    if (r.code != Code::Ok)
        return r;
    auto i = config_.offers.find(id);
    if (i == config_.offers.end())
        return {Code::Missing, id};
    const auto &o = i->second;
    if (paid != !o.product.empty() || (paid && op.size() == 4))
        return {Code::Denied, "purchase authority"};
    if (!profile_.player.empty() && profile_.player != c.player)
        return {Code::Denied, "player"};
    auto visible = visibleOffers(c);
    if (!paid && std::find(visible.begin(), visible.end(), id) == visible.end())
        return {Code::Unavailable, id};
    auto next = profile_;
    r = apply(next, o.cost, o.reward);
    if (!r)
        return r;
    auto &use = next.uses[key(id, o.schedule.bucket(c.now))];
    if (use.count == kMaxAmount)
        return {Code::Limit, "purchase count"};
    ++use.count;
    use.last = c.now;
    next.receipts[op] = body;
    return publish(std::move(next));
}
Result Economy::reserveOrder(const std::string &id, const Context &c, const std::string &order) {
    if (order.empty() || order.size() > 512)
        return {Code::Invalid, "order id"};
    if (!profile_.player.empty() && profile_.player != c.player)
        return {Code::Denied, "player"};
    auto found = profile_.orders.find(order);
    if (found != profile_.orders.end())
        return found->second.offer == id && found->second.state != OrderState::Cancelled
                   ? Result{Code::Duplicate, {}}
                   : Result{Code::Conflict, "order reused or cancelled"};
    auto it = config_.offers.find(id);
    if (it == config_.offers.end())
        return {Code::Missing, id};
    const auto &o = it->second;
    if (o.product.empty())
        return {Code::Invalid, "not a real-money offer"};
    auto visible = visibleOffers(c);
    if (std::find(visible.begin(), visible.end(), id) == visible.end())
        return {Code::Unavailable, id};
    auto next = profile_;
    const auto useKey = key(id, o.schedule.bucket(c.now));
    auto &use = next.uses[useKey];
    if (use.count == kMaxAmount)
        return {Code::Limit, "orders"};
    ++use.count;
    use.last = c.now;
    next.orders[order] = {id, o.product, useKey, o.reward, OrderState::Pending};
    return publish(std::move(next));
}
Result Economy::fulfillOrder(const std::string &id, const std::string &token) {
    if (token.empty() || token.size() > 508)
        return {Code::Invalid, "verified token"};
    auto body = payload("order", id);
    auto op = "iap:" + token;
    auto r = replay(op, body);
    if (r.code != Code::Ok)
        return r;
    auto it = profile_.orders.find(id);
    if (it == profile_.orders.end())
        return {Code::Missing, id};
    if (it->second.state == OrderState::Fulfilled)
        return {Code::Conflict, "order already paid with another token"};
    if (it->second.state != OrderState::Pending)
        return {Code::Conflict, "cancelled order"};
    auto next = profile_;
    r = apply(next, {}, it->second.reward);
    if (!r)
        return r;
    next.orders[id].state = OrderState::Fulfilled;
    next.receipts[op] = body;
    return publish(std::move(next));
}
Result Economy::cancelOrder(const std::string &id) {
    auto it = profile_.orders.find(id);
    if (it == profile_.orders.end())
        return {Code::Missing, id};
    if (it->second.state == OrderState::Cancelled)
        return {Code::Duplicate, {}};
    if (it->second.state == OrderState::Fulfilled)
        return {Code::Conflict, "paid order"};
    auto next = profile_;
    auto &count = next.uses[it->second.useKey].count;
    if (count <= 0)
        return {Code::Conflict, "reservation counter"};
    --count;
    next.orders[id].state = OrderState::Cancelled;
    return publish(std::move(next));
}
Result Economy::deliver(const Mail &mail) {
    if (mail.id.empty() || mail.claimed || !validTime(mail.expires))
        return {Code::Invalid, "mail"};
    auto i = profile_.mail.find(mail.id);
    if (i != profile_.mail.end())
        return i->second.reward == mail.reward && i->second.expires == mail.expires &&
                       i->second.metadata == mail.metadata
                   ? Result{Code::Duplicate, {}}
                   : Result{Code::Conflict, "mail id"};
    Profile check;
    auto r = apply(check, {}, mail.reward);
    if (!r)
        return r;
    auto next = profile_;
    next.mail[mail.id] = mail;
    return publish(std::move(next));
}
Result Economy::claimMail(const std::string &id, Time now, const std::string &op) {
    auto body = payload("mail", id);
    auto r = replay(op, body);
    if (r.code != Code::Ok)
        return r;
    auto i = profile_.mail.find(id);
    if (i == profile_.mail.end())
        return {Code::Missing, id};
    if (i->second.claimed)
        return {Code::Duplicate, {}};
    if (!validTime(now) || (i->second.expires && now >= i->second.expires))
        return {Code::Unavailable, id};
    auto next = profile_;
    r = apply(next, {}, i->second.reward);
    if (!r)
        return r;
    next.mail[id].claimed = true;
    next.receipts[op] = body;
    return publish(std::move(next));
}
size_t cohort(const std::string &player, const std::string &experiment, const std::vector<uint32_t> &weights) {
    uint64_t total = 0;
    for (auto w : weights)
        total += w;
    if (!total)
        return weights.size();
    uint64_t h = 14695981039346656037ULL;
    auto s = payload(player, experiment);
    for (unsigned char c : s) {
        h ^= c;
        h *= 1099511628211ULL;
    }
    auto n = h % total;
    for (size_t i = 0; i < weights.size(); ++i) {
        if (n < weights[i])
            return i;
        n -= weights[i];
    }
    return weights.size();
}
namespace {
void merge(Json &dst, const Json &src) {
    if (!src.isObject()) {
        dst = src;
        return;
    }
    if (!dst.isObject())
        dst = Json::object();
    for (auto &[k, v] : src.members()) {
        if (v.isNull())
            dst.erase(k);
        else
            merge(dst.member(k), v);
    }
}
} // namespace
Json resolveSettings(Json base, const Context &c, std::vector<Variant> variants) {
    std::stable_sort(variants.begin(), variants.end(), [](auto &a, auto &b) { return a.priority < b.priority; });
    for (auto &v : variants)
        if (v.condition.matches(c))
            merge(base, v.patch);
    return base;
}
} // namespace aiverse::meta
