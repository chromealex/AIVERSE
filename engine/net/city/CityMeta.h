// City accounts kept by the meta service (engine/meta, module "world"): the production AccountStore.
#pragma once
#include "CityNet.h"
#include "engine/meta/Service.h"

namespace aiverse::net::city {
class MetaAccounts final : public AccountStore {
public:
    // The service must have been built with meta::modules::world. `clock`: trusted unix seconds.
    MetaAccounts(meta::Service& service, std::function<int64_t()> clock = {});
    Json enter(const std::string& player) override;
    void save(const std::string& player, const Json& account, bool leaving) override;
    std::vector<std::pair<std::string, Json>> known() override;
    std::vector<std::string> absent(int64_t seconds);   // offline longer than this (a real week: 604800)
    uint64_t failed() const { return failed_; }
private:
    meta::ServiceReply call(const std::string& player, const std::string& service, const std::string& action, Json args);
    meta::Service& service_;
    std::function<int64_t()> clock_;
    uint64_t next_ = 0, failed_ = 0;
};
}
