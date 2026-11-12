#include "LiveOps.h"
#include "Modules.h"
namespace aiverse::meta {
Modules modules::all() {
    Modules m;
    modules::economy(m);
    modules::social(m);
    modules::clans(m);
    modules::items(m);
    modules::backend(m);
    modules::tournaments(m);
    modules::progression(m);
    modules::loot(m);
    modules::albums(m);
    modules::research(m);
    modules::offline(m);
    modules::jobs(m);
    modules::quests(m);
    modules::events(m);
    modules::notifications(m);
    modules::referrals(m);
    modules::ads(m);
    modules::chains(m);
    modules::wars(m);
    modules::asyncPvp(m);
    modules::ratings(m);
    modules::matchmaking(m);
    return m;
}
Service::Service(ServiceConfig c, Json s, Commit commit, RewardDraw draw)
    : Service(modules::all(), std::move(c), std::move(s), std::move(commit), std::move(draw)) {}
ServiceReply dispatchLiveOps(const ServiceConfig &c, Json &s, const Principal &p, const ServiceRequest &r, Time t,
                             const Context &ctx, const RewardDraw &draw) {
    auto m = modules::all();
    auto found = m.live.find(r.service);
    if (found == m.live.end())
        return {{Code::Unsupported, r.service}, {}};
    return dispatchLiveOpsSelected(c, s, p, r, t, ctx, draw, found->second);
}
Json viewLiveOps(const ServiceConfig &c, const Json &s, const std::string &p, Time t) {
    Json out = Json::object();
    Modules m;
    modules::notifications(m);
    for (auto project : m.views)
        project(c, s, p, t, out);
    return out["liveops"];
}
} // namespace aiverse::meta
