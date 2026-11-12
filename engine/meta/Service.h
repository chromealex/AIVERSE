#pragma once
#include "Social.h"
#include <memory>

namespace aiverse::meta {
// Trusted host supplies identity, time and privilege; never deserialize these from client arguments.
struct Principal { std::string player; bool authority = false; };
struct ServiceConfig {
    Config economy;
    Json extensions = Json::object();
    std::map<std::string, BoardConfig> boards;
    std::map<std::string, ChatConfig> chats;
    static ServiceConfig parse(const Json&);
};
// Single-owner aggregate. Cross-profile operations publish one atomic snapshot through Commit.
// Intended for small services/reference backends; production sharding must preserve transactions.
// Trusted host RNG returns a uniform integer in [1,total]; omitted disables loot.
using RewardDraw = std::function<int64_t(int64_t)>;
class Service {
public:
    explicit Service(ServiceConfig config, Json state = {}, Commit commit = {}, RewardDraw draw = {});
    ServiceReply execute(const Principal&, const ServiceRequest&, Time now);
    Json view(const std::string& player, Time now) const;
    const Json& save() const { return state_; }
private:
    ServiceReply dispatch(Json&, const Principal&, const ServiceRequest&, Time);
    ServiceConfig config_;
    Json state_;
    Commit commit_;
    RewardDraw draw_;
};
} // namespace aiverse::meta
