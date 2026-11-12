// aiverse::meta::Backend over the Bunker Wars meta API: UI screens keep using the engine meta
// request model (meta::Client, ServiceRequest{service, action, operation, arguments}) while the
// player's state lives on the BW servers. Arguments are the BW transfer object of the endpoint
// (field names as in the Unity client's *TO classes); replies carry the endpoint's "data".
//
//   bw::ApiClient api(transport, {"https://meta.example.com"});
//   bw::BwBackend backend(api);
//   meta::Client client(backend);
//   client.begin({"heroes", "buy", "op-1", args});     // POST v/hero/buyHero {"heroClientId": 7}
//   client.begin(meta::requests::view());              // v/profile/get -> engine view projection
//   for (;;) { client.pump(); ... }                     // pump also drives the ApiClient
//
// The route catalog (service.action -> path) is in BwRoutes.cpp and docs/meta-bunkerwars.md.
#pragma once
#include <string>
#include <vector>

#include "BwApi.h"
#include "engine/meta/Protocol.h"

namespace aiverse::meta::bw {

enum RouteFlags : unsigned {
    kRead = 0,
    kMutates = 1u << 0,   // changes server state: never retried automatically
    kChunk = 1u << 1,     // controller has @ChunkedMethod: chunk protocol + request-id dedupe
    kCache = 1u << 2,     // E-Tag cacheable read
    kStatic = 1u << 3,    // v/db/* static data
    kNoAuth = 1u << 4,    // callable without a session (entryPoint, auth)
};
struct Route {
    const char* service;
    const char* action;
    const char* path;
    unsigned flags;
};
const std::vector<Route>& routes();
// nullptr if unknown. service "statics" accepts any action as v/db/<action>.
const Route* findRoute(const std::string& service, const std::string& action);

// BW error code -> engine meta Code (NOT_ENOUGH_MONEY -> Insufficient, NOT_FOUND -> Missing, ...).
Code mapCode(int bwCode);

// BW ProfileResponseTO (v/profile/get data) -> engine view projection:
// {"source":"bunkerwars","player","revision","profile":{"player","balances","attributes"},
//  "offers","tags","liveops","heroes","skills","inventory","cargoes","clan","challenges","quests",
//  "forge","premium","stat","raw"}. Unknown fields stay reachable under "raw".
Json projectView(const Json& profile, int64_t revision);

class BwBackend final : public Backend {
public:
    explicit BwBackend(ApiClient& api, int64_t profileMask = 0 /* ProfileRequestMask.Full */);
    void request(ServiceRequest request, Callback callback) override;
    void pump() override { api_.pump(); }
    const Json& lastView() const { return view_; }   // last successful meta/view projection

private:
    ApiClient& api_;
    int64_t mask_;
    int64_t revision_ = 0;
    Json view_;
};

} // namespace aiverse::meta::bw
