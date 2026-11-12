#pragma once
#include "Service.h"
namespace aiverse::meta {
namespace live {
struct Call;
}
struct Modules {
    using Handler = ServiceReply (*)(const ServiceConfig &, Json &, const Principal &, const ServiceRequest &, Time);
    using View = void (*)(const ServiceConfig &, const Json &, const std::string &, Time, Json &);
    std::map<std::string, Handler> handlers;
    std::map<std::string, Json (*)(live::Call &)> live;
    std::vector<View> views;
    std::set<std::string> features;
};
namespace modules {
void economy(Modules &);
void social(Modules &);
void clans(Modules &);
void items(Modules &);
void backend(Modules &);
void tournaments(Modules &);
void progression(Modules &);
void loot(Modules &);
void albums(Modules &);
void research(Modules &);
void offline(Modules &);
void jobs(Modules &);
void quests(Modules &);
void events(Modules &);
void notifications(Modules &);
void referrals(Modules &);
void ads(Modules &);
void chains(Modules &);
void wars(Modules &);
void asyncPvp(Modules &);
void ratings(Modules &);
void matchmaking(Modules &);
Modules all(); // Compatibility only; links every policy.
} // namespace modules
} // namespace aiverse::meta
