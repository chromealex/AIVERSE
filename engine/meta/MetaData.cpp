#include "Meta.h"
#include <charconv>
#include <limits>
#include <stdexcept>

namespace aiverse::meta {
namespace {
[[noreturn]] void bad(const std::string &p, const std::string &msg) {
    throw std::runtime_error("meta " + p + ": " + msg);
}
void object(const Json &j, const std::string &p) {
    if (!j.isObject())
        bad(p, "expected object");
}
void keys(const Json &j, const std::string &p, std::initializer_list<const char *> known) {
    object(j, p);
    std::set<std::string> seen;
    for (auto &[k, v] : j.members()) {
        (void)v;
        if (!seen.insert(k).second)
            bad(p, "duplicate key '" + k + "'");
        bool found = false;
        for (auto x : known)
            if (k == x)
                found = true;
        if (!found)
            bad(p, "unknown key '" + k + "' (custom data belongs in metadata)");
    }
}
int64_t number(const Json &j, const std::string &p, int64_t lo = 0, int64_t hi = kMaxAmount) {
    if (!j.isNumber())
        bad(p, "expected integer");
    int64_t n = 0;
    auto &s = j.numberLiteral();
    auto r = std::from_chars(s.data(), s.data() + s.size(), n);
    if (r.ec != std::errc{} || r.ptr != s.data() + s.size() || n < lo || n > hi)
        bad(p, "integer out of range");
    return n;
}
int64_t num(const Json &j, const std::string &k, const std::string &p, int64_t def = 0, int64_t lo = 0,
            int64_t hi = kMaxAmount) {
    return j.has(k) ? number(j[k], p + "." + k, lo, hi) : def;
}
std::string string(const Json &j, const std::string &p, bool empty = false) {
    if (!j.isString() || (!empty && j.str().empty()))
        bad(p, "expected nonempty string");
    return j.str();
}
bool boolean(const Json &j, const std::string &k, const std::string &p, bool def) {
    if (!j.has(k))
        return def;
    if (j[k].type() != Json::Type::Bool)
        bad(p + "." + k, "expected boolean");
    return j[k].boolean();
}
Bundle bundle(const Json &j, const std::string &p, bool optional = true) {
    Bundle b;
    if (j.isNull() && optional)
        return b;
    object(j, p);
    for (auto &[k, v] : j.members()) {
        if (k.empty() || b.count(k))
            bad(p, "empty or duplicate id");
        b[k] = number(v, p + "." + k);
    }
    return b;
}
void assetRefs(const Bundle &b, const Config &c, const std::string &p) {
    for (auto &[id, n] : b) {
        auto i = c.assets.find(id);
        if (i == c.assets.end())
            bad(p, "unknown asset '" + id + "'");
        if (n > i->second.cap)
            bad(p, "amount exceeds asset cap '" + id + "'");
    }
}
std::set<std::string> strings(const Json &j, const std::string &p) {
    std::set<std::string> r;
    if (j.isNull())
        return r;
    if (!j.isArray())
        bad(p, "expected array");
    for (auto &v : j.items())
        if (!r.insert(string(v, p)).second)
            bad(p, "duplicate string");
    return r;
}
Condition condition(const Json &j, const std::string &p, int depth = 0) {
    Condition c;
    if (j.isNull())
        return c;
    if (depth > 16)
        bad(p, "condition nesting exceeds 16");
    keys(j, p, {"minimum", "maximum", "requiredTags", "excludedTags", "all", "any", "none"});
    c.minimum = bundle(j["minimum"], p + ".minimum");
    c.maximum = bundle(j["maximum"], p + ".maximum");
    c.requiredTags = strings(j["requiredTags"], p);
    c.excludedTags = strings(j["excludedTags"], p);
    for (auto pair : {std::pair{"all", &c.all}, std::pair{"any", &c.any}, std::pair{"none", &c.none}}) {
        if (!j.has(pair.first))
            continue;
        if (!j[pair.first].isArray())
            bad(p, "condition group must be array");
        for (auto &x : j[pair.first].items())
            pair.second->push_back(condition(x, p + "." + pair.first, depth + 1));
    }
    return c;
}
Schedule schedule(const Json &j, const std::string &p) {
    Schedule s;
    if (j.isNull())
        return s;
    keys(j, p, {"start", "end", "period", "duration"});
    s.start = num(j, "start", p);
    s.end = num(j, "end", p);
    s.period = num(j, "period", p);
    s.duration = num(j, "duration", p);
    if ((s.end && s.end <= s.start) || (s.duration && (!s.period || s.duration > s.period)))
        bad(p, "invalid schedule interval");
    return s;
}
Json encode(const Bundle &b) {
    Json j = Json::object();
    for (auto &[k, n] : b)
        j.set(k, Json::number(n));
    return j;
}
Json encode(const std::set<std::string> &s) {
    Json j = Json::array();
    for (auto &k : s)
        j.push(Json::string(k));
    return j;
}
void section(const Json &j, const std::string &k) {
    if (j.has(k))
        object(j[k], k);
}
void checkMetadata(const Json &j, const std::string &p) {
    if (j.has("metadata"))
        object(j["metadata"], p + ".metadata");
}
} // namespace
Config Config::parse(const Json &j) {
    keys(j, "config", {"version", "assets", "offers", "tracks", "daily", "energy", "metadata"});
    Config c;
    c.version = int(num(j, "version", "config", 1, 1, 1));
    for (auto k : {"assets", "offers", "tracks", "daily", "energy"})
        section(j, k);
    checkMetadata(j, "config");
    if (j.has("metadata"))
        c.metadata = j["metadata"];
    for (auto &[id, v] : j["assets"].members()) {
        auto p = "assets." + id;
        if (id.empty() || c.assets.count(id))
            bad(p, "duplicate or empty id");
        keys(v, p, {"cap", "metadata"});
        checkMetadata(v, p);
        Asset a;
        a.cap = num(v, "cap", p, kMaxAmount);
        if (v.has("metadata"))
            a.metadata = v["metadata"];
        c.assets[id] = a;
    }
    for (auto &[id, v] : j["offers"].members()) {
        auto p = "offers." + id;
        if (id.empty() || c.offers.count(id))
            bad(p, "duplicate or empty id");
        keys(v, p,
             {"cost", "reward", "condition", "schedule", "limit", "cooldown", "priority", "group", "product",
              "metadata"});
        checkMetadata(v, p);
        Offer o;
        o.cost = bundle(v["cost"], p + ".cost");
        o.reward = bundle(v["reward"], p + ".reward");
        assetRefs(o.cost, c, p);
        assetRefs(o.reward, c, p);
        o.condition = condition(v["condition"], p + ".condition");
        o.schedule = schedule(v["schedule"], p + ".schedule");
        o.limit = num(v, "limit", p);
        o.cooldown = num(v, "cooldown", p);
        o.priority = int(num(v, "priority", p, 0, -1000000, 1000000));
        if (v.has("group"))
            o.group = string(v["group"], p + ".group", true);
        if (v.has("product"))
            o.product = string(v["product"], p + ".product");
        if (!o.product.empty() && !o.cost.empty())
            bad(p, "real-money offers cannot also charge virtual assets");
        if (v.has("metadata"))
            o.metadata = v["metadata"];
        c.offers[id] = o;
    }
    for (auto &[id, v] : j["tracks"].members()) {
        auto p = "tracks." + id;
        if (id.empty() || c.tracks.count(id))
            bad(p, "duplicate or empty id");
        keys(v, p, {"metric", "premiumTag", "thresholds", "rewards", "schedule", "condition", "metadata"});
        Track t;
        checkMetadata(v, p);
        if (v.has("metadata"))
            t.metadata = v["metadata"];
        t.metric = string(v["metric"], p + ".metric");
        if (t.metric.rfind("@meta/", 0) == 0)
            bad(p, "reserved metric prefix");
        if (v.has("premiumTag"))
            t.premiumTag = string(v["premiumTag"], p + ".premiumTag");
        t.condition = condition(v["condition"], p + ".condition");
        t.schedule = schedule(v["schedule"], p + ".schedule");
        if (!v["thresholds"].isArray() || !v["rewards"].isArray() || !v["thresholds"].size() ||
            v["thresholds"].size() != v["rewards"].size())
            bad(p, "thresholds/rewards must be nonempty arrays of equal size");
        for (auto &x : v["thresholds"].items()) {
            auto n = number(x, p);
            if (!t.thresholds.empty() && n <= t.thresholds.back())
                bad(p, "thresholds must increase");
            t.thresholds.push_back(n);
        }
        for (auto &x : v["rewards"].items()) {
            auto b = bundle(x, p, false);
            assetRefs(b, c, p);
            t.rewards.push_back(b);
        }
        c.tracks[id] = t;
    }
    for (auto &[id, v] : j["daily"].members()) {
        auto p = "daily." + id;
        if (id.empty() || c.daily.count(id))
            bad(p, "duplicate or empty id");
        keys(v, p, {"rewards", "start", "period", "resetOnMiss", "loop", "metadata"});
        Daily d;
        checkMetadata(v, p);
        if (v.has("metadata"))
            d.metadata = v["metadata"];
        d.start = num(v, "start", p);
        d.period = num(v, "period", p, 86400, 1);
        d.resetOnMiss = boolean(v, "resetOnMiss", p, true);
        d.loop = boolean(v, "loop", p, true);
        if (!v["rewards"].isArray() || !v["rewards"].size())
            bad(p, "rewards must be nonempty array");
        for (auto &x : v["rewards"].items()) {
            auto b = bundle(x, p, false);
            assetRefs(b, c, p);
            d.rewards.push_back(b);
        }
        c.daily[id] = d;
    }
    for (auto &[id, v] : j["energy"].members()) {
        auto p = "energy." + id;
        if (id.empty() || c.energy.count(id))
            bad(p, "duplicate or empty id");
        keys(v, p, {"asset", "cap", "amount", "interval", "metadata"});
        Energy e;
        checkMetadata(v, p);
        if (v.has("metadata"))
            e.metadata = v["metadata"];
        e.asset = string(v["asset"], p + ".asset");
        e.cap = num(v, "cap", p, 100);
        e.amount = num(v, "amount", p, 1, 1);
        e.interval = num(v, "interval", p, 300, 1);
        assetRefs({{e.asset, e.cap}}, c, p);
        c.energy[id] = e;
    }
    return c;
}
namespace {
Json encodeCondition(const Condition &c) {
    Json j = Json::object();
    j.set("minimum", encode(c.minimum));
    j.set("maximum", encode(c.maximum));
    j.set("requiredTags", encode(c.requiredTags));
    j.set("excludedTags", encode(c.excludedTags));
    for (auto pair : {std::pair{"all", &c.all}, std::pair{"any", &c.any}, std::pair{"none", &c.none}}) {
        Json xs = Json::array();
        for (auto &x : *pair.second)
            xs.push(encodeCondition(x));
        j.set(pair.first, xs);
    }
    return j;
}
Json encodeSchedule(const Schedule &s) {
    return encode(Bundle{{"start", s.start}, {"end", s.end}, {"period", s.period}, {"duration", s.duration}});
}
} // namespace
Json Config::save() const {
    Json j = Json::object();
    j.set("version", Json::number(int64_t(version)));
    j.set("metadata", metadata);
    Json as = Json::object();
    for (auto &[id, a] : assets) {
        Json x = Json::object();
        x.set("cap", Json::number(a.cap));
        x.set("metadata", a.metadata);
        as.set(id, x);
    }
    j.set("assets", as);
    Json os = Json::object();
    for (auto &[id, o] : offers) {
        Json x = Json::object();
        x.set("cost", encode(o.cost));
        x.set("reward", encode(o.reward));
        x.set("condition", encodeCondition(o.condition));
        x.set("schedule", encodeSchedule(o.schedule));
        x.set("limit", Json::number(o.limit));
        x.set("cooldown", Json::number(o.cooldown));
        x.set("priority", Json::number(int64_t(o.priority)));
        x.set("group", Json::string(o.group));
        if (!o.product.empty())
            x.set("product", Json::string(o.product));
        x.set("metadata", o.metadata);
        os.set(id, x);
    }
    j.set("offers", os);
    Json ts = Json::object();
    for (auto &[id, t] : tracks) {
        Json x = Json::object();
        x.set("metadata", t.metadata);
        x.set("metric", Json::string(t.metric));
        if (!t.premiumTag.empty())
            x.set("premiumTag", Json::string(t.premiumTag));
        x.set("condition", encodeCondition(t.condition));
        x.set("schedule", encodeSchedule(t.schedule));
        Json thresholds = Json::array(), rewards = Json::array();
        for (auto n : t.thresholds)
            thresholds.push(Json::number(n));
        for (auto &b : t.rewards)
            rewards.push(encode(b));
        x.set("thresholds", thresholds);
        x.set("rewards", rewards);
        ts.set(id, x);
    }
    j.set("tracks", ts);
    Json ds = Json::object();
    for (auto &[id, d] : daily) {
        Json x = Json::object(), rewards = Json::array();
        x.set("metadata", d.metadata);
        for (auto &b : d.rewards)
            rewards.push(encode(b));
        x.set("rewards", rewards);
        x.set("start", Json::number(d.start));
        x.set("period", Json::number(d.period));
        x.set("resetOnMiss", Json::fromBool(d.resetOnMiss));
        x.set("loop", Json::fromBool(d.loop));
        ds.set(id, x);
    }
    j.set("daily", ds);
    Json es = Json::object();
    for (auto &[id, e] : energy) {
        Json x = encode(Bundle{{"cap", e.cap}, {"amount", e.amount}, {"interval", e.interval}});
        x.set("metadata", e.metadata);
        x.set("asset", Json::string(e.asset));
        es.set(id, x);
    }
    j.set("energy", es);
    return j;
}
Json Profile::save() const {
    Json j = Json::object();
    j.set("version", Json::number(int64_t(1)));
    j.set("player", Json::string(player));
    j.set("revision", Json::number(revision));
    j.set("balances", encode(balances));
    j.set("progress", encode(progress));
    j.set("attributes", attributes);
    j.set("claims", encode(claims));
    Json rs = Json::object();
    for (auto &[k, v] : receipts)
        rs.set(k, Json::string(v));
    j.set("receipts", rs);
    Json us = Json::object();
    for (auto &[k, v] : uses)
        us.set(k, encode(Bundle{{"count", v.count}, {"last", v.last}}));
    j.set("uses", us);
    Json ds = Json::object();
    for (auto &[k, v] : daily)
        ds.set(k, encode(Bundle{{"bucket", v.bucket}, {"streak", v.streak}}));
    j.set("daily", ds);
    j.set("energy", encode(energy));
    Json ms = Json::object();
    for (auto &[k, v] : mail) {
        Json m = Json::object();
        m.set("reward", encode(v.reward));
        m.set("expires", Json::number(v.expires));
        m.set("claimed", Json::fromBool(v.claimed));
        m.set("metadata", v.metadata);
        ms.set(k, m);
    }
    j.set("mail", ms);
    Json os = Json::object();
    for (auto &[id, o] : orders) {
        Json x = Json::object();
        x.set("offer", Json::string(o.offer));
        x.set("product", Json::string(o.product));
        x.set("useKey", Json::string(o.useKey));
        x.set("reward", encode(o.reward));
        x.set("state", Json::number(int64_t(o.state)));
        os.set(id, x);
    }
    j.set("orders", os);
    return j;
}
Profile Profile::load(const Json &j, const Config &c) {
    keys(j, "profile",
         {"version", "player", "revision", "balances", "progress", "attributes", "claims", "receipts", "uses", "daily",
          "energy", "mail", "orders"});
    if (num(j, "version", "profile", 0, 1, 1) != 1)
        bad("profile", "version required");
    Profile p;
    p.player = string(j["player"], "profile.player", true);
    p.revision = num(j, "revision", "profile", 0, 0, std::numeric_limits<int64_t>::max());
    p.balances = bundle(j["balances"], "profile.balances");
    assetRefs(p.balances, c, "profile.balances");
    p.progress = bundle(j["progress"], "profile.progress");
    if (j.has("attributes")) {
        object(j["attributes"], "attributes");
        p.attributes = j["attributes"];
    }
    p.claims = strings(j["claims"], "claims");
    p.energy = bundle(j["energy"], "energy");
    for (auto k : {"receipts", "uses", "daily", "mail", "orders"})
        section(j, k);
    for (auto &[k, v] : j["receipts"].members()) {
        if (p.receipts.count(k))
            bad(k, "duplicate receipt");
        p.receipts[k] = string(v, "receipt");
    }
    for (auto &[k, v] : j["uses"].members()) {
        keys(v, k, {"count", "last"});
        if (p.uses.count(k))
            bad(k, "duplicate use");
        p.uses[k] = {num(v, "count", k), num(v, "last", k)};
    }
    for (auto &[k, v] : j["daily"].members()) {
        keys(v, k, {"bucket", "streak"});
        if (p.daily.count(k))
            bad(k, "duplicate daily state");
        p.daily[k] = {num(v, "bucket", k, -1, -1), num(v, "streak", k)};
    }
    for (auto &[k, v] : j["mail"].members()) {
        keys(v, k, {"reward", "expires", "claimed", "metadata"});
        if (k.empty() || p.mail.count(k))
            bad(k, "invalid mail id");
        Mail m;
        m.id = k;
        m.reward = bundle(v["reward"], k);
        assetRefs(m.reward, c, k);
        m.expires = num(v, "expires", k);
        m.claimed = boolean(v, "claimed", k, false);
        checkMetadata(v, k);
        if (v.has("metadata"))
            m.metadata = v["metadata"];
        p.mail[k] = m;
    }
    for (auto &[id, v] : j["orders"].members()) {
        keys(v, id, {"offer", "product", "useKey", "reward", "state"});
        if (id.empty() || p.orders.count(id))
            bad(id, "duplicate/empty order id");
        Order o;
        o.offer = string(v["offer"], id);
        o.product = string(v["product"], id);
        o.useKey = string(v["useKey"], id);
        o.reward = bundle(v["reward"], id, false);
        assetRefs(o.reward, c, id);
        o.state = OrderState(num(v, "state", id, 0, 0, 2));
        if (!p.uses.count(o.useKey))
            bad(id, "missing order reservation counter");
        p.orders[id] = o;
    }
    return p;
}
} // namespace aiverse::meta
