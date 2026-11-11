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
// Native networking adapter. Transport completion is queued by the host on the owning thread.
// Host implements HTTPS/auth and retries with the same operation ID.
class RemoteBackend final : public Backend {
public:
    using Transport = std::function<void(const ServiceRequest&, Callback)>;
    explicit RemoteBackend(Transport send):send_(std::move(send)) {}
    void request(ServiceRequest, Callback) override;
    void pump() override;
private:
    Transport send_;
    struct Pending { Callback callback; ServiceReply reply; };
    std::shared_ptr<std::vector<Pending>> pending_ = std::make_shared<std::vector<Pending>>();
};
} // namespace aiverse::meta
