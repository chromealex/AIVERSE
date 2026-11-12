// Reusable meta-game policies. No GPU, platform, clock, network or simulation globals.
#pragma once
#include "engine/data/Json.h"
#include "Types.h"
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>
#include <cstdint>

namespace aiverse::meta {
struct Context {
    Time now = 0;
    std::string player;
    Bundle values; // level, wins, accountAge, etc.; owned by the caller
    std::set<std::string> tags; // country, platform, experiment, entitlement, etc.
};
struct Condition {
    Bundle minimum, maximum;
    std::set<std::string> requiredTags, excludedTags;
    std::vector<Condition> all, any, none;
    bool matches(const Context& context) const;
};
struct Schedule {
    Time start = 0, end = 0, period = 0, duration = 0;
    bool active(Time now) const;
    int64_t bucket(Time now) const; // -1 before start; 0 for non-recurring
};
struct Asset { Amount cap = kMaxAmount; Json metadata = Json::object(); };
struct Offer {
    Bundle cost, reward;
    Condition condition;
    Schedule schedule;
    int64_t limit = 0; // 0 = unlimited, otherwise per schedule bucket
    Time cooldown = 0;
    int priority = 0;
    std::string group, product; // product nonempty: verified real-money purchase only
    Json metadata = Json::object(); // localized labels, art, placements; policy ignores it
};
struct Track {
    std::string metric, premiumTag;
    Json metadata = Json::object();
    std::vector<Amount> thresholds;
    std::vector<Bundle> rewards;
    Schedule schedule;
    Condition condition;
};
struct Daily {
    Json metadata = Json::object();
    std::vector<Bundle> rewards;
    Time start = 0, period = 86400;
    bool resetOnMiss = true, loop = true;
};
struct Energy { Json metadata = Json::object(); std::string asset; Amount cap = 100, amount = 1; Time interval = 300; };
struct Config {
    int version = 1;
    std::map<std::string, Asset> assets;
    std::map<std::string, Offer> offers; // shops, crafting recipes, exchanges, gifts
    std::map<std::string, Track> tracks; // quests, achievements, passes, level rewards
    std::map<std::string, Daily> daily;
    std::map<std::string, Energy> energy;
    Json metadata = Json::object();
    Json save() const;
    static Config parse(const Json& json); // strict, throws readable errors; transactional load
};
struct Use { int64_t count = 0; Time last = 0; };
struct DailyState { int64_t bucket = -1; int64_t streak = 0; };
struct Mail { std::string id; Bundle reward; Time expires = 0; bool claimed = false; Json metadata = Json::object(); };
enum class OrderState { Pending, Fulfilled, Cancelled };
struct Order {
    std::string offer, product, useKey;
    Bundle reward;
    OrderState state = OrderState::Pending;
};
struct Profile {
    std::string player;
    int64_t revision = 0;
    Bundle balances, progress;
    std::map<std::string, std::string> receipts; // operation id -> exact payload, never silently prune
    std::map<std::string, Use> uses;
    std::map<std::string, DailyState> daily;
    std::map<std::string, Time> energy;
    std::set<std::string> claims;
    std::map<std::string, Mail> mail;
    std::map<std::string, Order> orders;
    Json attributes = Json::object();
    Json save() const;
    static Profile load(const Json& json, const Config& config);
};
// Commit one whole profile atomically (including receipts); expectedRevision is CAS.
// An online implementation runs policies on the server, never trusts a client Profile.
using Commit = std::function<bool(int64_t expectedRevision, const Json& next)>;
class Economy {
public:
    explicit Economy(Config config, Profile profile = {}, Commit commit = {});
    const Config& config() const { return config_; }
    const Profile& profile() const { return profile_; }
    Result transact(const std::string& operation, const Bundle& cost, const Bundle& reward);
    Result buy(const std::string& offer, const Context& context, const std::string& operation);
    // Call only after backend receipt validation; token must include provider + transaction id.
    Result fulfill(const std::string& offer, const Context& context, const std::string& verifiedToken);
    // Reserve before showing payment. These methods belong to the authoritative host.
    Result reserveOrder(const std::string& offer, const Context& context, const std::string& order);
    Result fulfillOrder(const std::string& order, const std::string& verifiedToken);
    Result cancelOrder(const std::string& order); // only after authoritative terminal nonpayment
    std::vector<std::string> visibleOffers(const Context& context) const;
    Result record(const std::string& metric, Amount delta, const std::string& operation);
    Result recordEvent(const std::string& metric, Amount delta, const Context& context, const std::string& operation);
    Result claim(const std::string& track, size_t tier, const Context& context, const std::string& operation);
    Result claimDaily(const std::string& id, Time now, const std::string& operation);
    Result regenerate(const std::string& id, Time now);
    Result deliver(const Mail& mail);
    Result claimMail(const std::string& id, Time now, const std::string& operation);
private:
    Config config_;
    Profile profile_;
    Commit commit_;
    Result apply(Profile& next, const Bundle& cost, const Bundle& reward) const;
    Result publish(Profile next);
    Result replay(const std::string& operation, const std::string& payload) const;
    Result purchase(const std::string&, const Context&, const std::string&, bool);
};
// Stable deterministic experiment assignment, independent of std::hash/platform.
size_t cohort(const std::string& player, const std::string& experiment, const std::vector<uint32_t>& weights);
struct Variant { Condition condition; int priority = 0; Json patch = Json::object(); };
Json resolveSettings(Json base, const Context& context, std::vector<Variant> variants);
} // namespace aiverse::meta
