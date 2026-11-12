#pragma once
#include "Service.h"
namespace aiverse::meta {
namespace live {
struct Call;
}
ServiceReply dispatchLiveOpsSelected(const ServiceConfig &, Json &, const Principal &, const ServiceRequest &, Time,
                                     const Context &, const RewardDraw &, Json (*)(live::Call &));
// Optional host-side policies sharing the Service aggregate transaction.
void validateLiveOps(const Json &definitions, const Config &economy);
ServiceReply dispatchLiveOps(const ServiceConfig &, Json &state, const Principal &, const ServiceRequest &, Time,
                             const Context &, const RewardDraw &);
Json viewLiveOps(const ServiceConfig &, const Json &state, const std::string &player, Time);
} // namespace aiverse::meta
