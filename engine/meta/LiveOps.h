#pragma once
#include "Service.h"
namespace aiverse::meta {
// Optional host-side policies sharing the Service aggregate transaction.
void validateLiveOps(const Json& definitions,const Config& economy);
ServiceReply dispatchLiveOps(const ServiceConfig&,Json& state,const Principal&,const ServiceRequest&,Time,const Context&,const RewardDraw&);
Json viewLiveOps(const ServiceConfig&,const Json& state,const std::string& player,Time);
}
