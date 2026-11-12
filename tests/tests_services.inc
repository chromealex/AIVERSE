// ---------------- analytics / privacy services (agent-sdk-plugins): engine/services/{Analytics,Privacy,ServicesLua}
#include "engine/services/Analytics.h"
#include "engine/services/MockPlugins.h"
#include "engine/services/Privacy.h"
#include "engine/services/ServicesLua.h"

namespace {
// Records "method args" of every call; stands in for an SDK plugin (gameanalytics, facebook, an ad network).
struct RecorderPlugin : plugins::NativePlugin {
    std::vector<std::string> calls;
    void call(const std::string& method, const std::string& args, plugins::Reply r) override {
        calls.push_back(method + " " + args);
        r.ok();
    }
    std::vector<std::string> take(const std::string& method) {
        std::vector<std::string> out;
        for (auto& c : calls) if (c.compare(0, method.size() + 1, method + " ") == 0) out.push_back(c.substr(method.size() + 1));
        return out;
    }
};
bool contains(const std::vector<std::string>& v, const std::string& s) { return std::find(v.begin(), v.end(), s) != v.end(); }
} // namespace

TEST(analytics_fans_out_to_every_provider) {
    auto ga = std::make_shared<RecorderPlugin>(), fb = std::make_shared<RecorderPlugin>();
    plugins::registerPlugin("gameanalytics", ga);
    plugins::registerPlugin("facebook", fb);
    plugins::configureGame(R"({
      "plugins": { "gameanalytics": { "gameKey": { "default": "k" } }, "facebook": {} },
      "analytics": { "providers": { "default": ["facebook", "no-such-sdk", "gameanalytics", "facebook"] } }
    })");
    analytics::reset();
    privacy::reset();
    CHECK(analytics::available() && analytics::consented());
    CHECK((analytics::providers() == std::vector<std::string>{"facebook", "gameanalytics"}));   // order kept, duplicates and missing dropped

    analytics::event("match_end", R"({"win": true, "kills": 3, "mode": "pve", "nested": {"a": 1}, "none": null, "ratio": 0.5})");
    analytics::design("match_end:pve:win", "{}", 93.5);
    analytics::progression(analytics::Progression::Complete, "pve:stage_3:boss:extra", R"({"kills": 7})", 120);
    analytics::progression(analytics::Progression::Start, "", "{}");   // no path: dropped
    analytics::progression(analytics::Progression::Fail, ":pve:", "{}");  // empty parts skipped
    analytics::resource(analytics::Flow::Sink, "coins", -50, "robot", "r2_level");
    analytics::purchase("USD", 990000, "gems", "gems_100", "shop", R"({"token": "t1", "productId": "gems_100"})");
    analytics::setUserProperty("dimension01", "pvp");
    plugins::pump();
    CHECK(ga->calls.size() == 8 && fb->calls.size() == 8);
    CHECK(ga->calls[0] == R"(init {"gameKey":"k"})");   // config resolved per platform, init first
    CHECK(ga->take("event")[0] == R"({"fields":{"win":1,"kills":3,"mode":"pve","ratio":0.5},"name":"match_end"})");
    CHECK(ga->take("design")[0] == R"({"fields":{},"id":"match_end:pve:win","value":93.5})");
    auto prog = ga->take("progression");
    CHECK(prog.size() == 2);
    CHECK(prog[0] == R"({"fields":{"kills":7},"status":"complete","path":["pve","stage_3","boss"],"score":120})");
    CHECK(prog[1] == R"({"fields":{},"status":"fail","path":["pve"]})");
    CHECK(ga->take("resource")[0] == R"({"fields":{},"flow":"sink","currency":"coins","amount":50,"itemType":"robot","itemId":"r2_level"})");
    CHECK(ga->take("purchase")[0] == R"({"fields":{},"currency":"USD","priceMicros":990000,"itemType":"gems","itemId":"gems_100","cartType":"shop","receipt":{"token":"t1","productId":"gems_100"}})");
    CHECK(fb->take("user")[0] == R"({"properties":{"dimension01":"pvp"}})");
    CHECK(analytics::cleanFields("[1,2]") == "{}" && analytics::cleanFields("not json") == "{}");

    // no consent: events stop at the service, identity / consent / flush still reach the SDKs
    analytics::setConsent(false, false);
    analytics::event("hidden");
    analytics::design("hidden");
    analytics::setUserId("u1");
    analytics::flush();
    plugins::pump();
    CHECK(ga->take("consent").back() == R"({"analytics":false,"tracking":false})");
    CHECK(ga->take("event").size() == 1 && ga->take("design").size() == 1);
    CHECK(fb->take("user").back() == R"({"id":"u1"})" && fb->take("flush").size() == 1);
    CHECK(!analytics::consented());

    // "consentRequired": nothing leaves before the player agreed
    plugins::configureGame(R"({ "plugins": { "gameanalytics": {} }, "analytics": { "consentRequired": true } })");
    analytics::reset();
    ga->calls.clear();
    analytics::event("early");
    plugins::pump();
    CHECK(ga->take("event").empty() && !analytics::consented());
    analytics::setConsent(true, true);
    analytics::event("late");
    plugins::pump();
    CHECK(ga->take("event").size() == 1 && ga->take("event")[0].find("\"late\"") != std::string::npos);

    plugins::unregisterPlugin("gameanalytics");
    plugins::unregisterPlugin("facebook");
    analytics::reset();
}

#if AIVERSE_WITH_MOCK_PLUGINS
TEST(privacy_att_answer_reaches_ads_and_analytics) {
    auto adsRec = std::make_shared<RecorderPlugin>();
    plugins::configureGame(R"({
      "plugins": { "mock-att": { "prompt": "manual" }, "mock-ads": {}, "mock-analytics": {} },
      "ads": { "providers": ["mock-ads"] }, "analytics": { "providers": ["mock-analytics"] }
    })");
    plugins::registerPlugin("mock-ads", adsRec);   // the ad network sees what consent it gets
    plugins::unregisterPlugin("mock-att");
    services::registerMockPlugin("mock-att");      // fresh prompt state ("notDetermined")
    services::registerMockPlugin("mock-analytics");
    privacy::reset();
    analytics::reset();
    services::mockAnalyticsLog(true);
    CHECK(ads::provider() == "mock-ads");
    CHECK(privacy::provider() == "mock-att");
    plugins::pump();
    CHECK(privacy::tracking() == privacy::Tracking::NotDetermined && !privacy::trackingAllowed());

    setEnvVar("AIVERSE_MOCK_ATT", "denied");
    std::vector<privacy::Tracking> answers;
    privacy::requestTracking([&](privacy::Tracking t) { answers.push_back(t); });
    plugins::pump();
    CHECK(answers.size() == 1 && answers[0] == privacy::Tracking::Denied);
    CHECK(adsRec->take("consent").back() == R"({"personalized":false,"ageRestricted":false})");
    auto log = services::mockAnalyticsLog(true);
    CHECK(contains(log, R"(consent {"analytics":true,"tracking":false})"));

    // the prompt is shown once; a later request just reports the answer
    setEnvVar("AIVERSE_MOCK_ATT", "authorized");
    privacy::requestTracking([&](privacy::Tracking t) { answers.push_back(t); });
    plugins::pump();
    CHECK(answers.size() == 2 && answers[1] == privacy::Tracking::Denied);

    // the game's own GDPR dialog: no analytics, child-directed ads
    privacy::setConsent({false, true, true});
    analytics::event("after_no");
    plugins::pump();
    CHECK(adsRec->take("consent").back() == R"({"personalized":false,"ageRestricted":true})");
    log = services::mockAnalyticsLog(true);
    CHECK(contains(log, R"(consent {"analytics":false,"tracking":false})"));
    for (auto& l : log) CHECK(l.find("after_no") == std::string::npos);

    // "prompt": "ready": the plugin asks by itself on the first interactive frame; the answer is applied
    plugins::configureGame(R"({
      "plugins": { "mock-att": { "prompt": "ready" }, "mock-ads": {}, "mock-analytics": {} },
      "ads": { "providers": ["mock-ads"] }, "analytics": { "providers": ["mock-analytics"] }
    })");
    plugins::unregisterPlugin("mock-att");
    services::registerMockPlugin("mock-att");
    privacy::reset();
    analytics::reset();
    analytics::available();                         // services load (and listen) before the frame
    plugins::pump();
    plugins::detail::broadcast("lifecycle", R"({"event":"ready"})");
    plugins::pump();
    plugins::pump();
    CHECK(privacy::tracking() == privacy::Tracking::Authorized && privacy::trackingAllowed());
    CHECK(adsRec->take("consent").back() == R"({"personalized":true,"ageRestricted":false})");
    log = services::mockAnalyticsLog(true);
    CHECK(contains(log, R"(consent {"analytics":true,"tracking":true})"));

    setEnvVar("AIVERSE_MOCK_ATT", "authorized");
    plugins::unregisterPlugin("mock-ads");
    services::registerMockPlugin("mock-ads");
    privacy::reset();
    analytics::reset();
}

TEST(services_lua_tables) {
    plugins::configureGame(R"({ "plugins": { "mock-analytics": {}, "mock-att": {} } })");
    plugins::unregisterPlugin("mock-att");
    services::registerMockPlugin("mock-att");
    services::registerMockPlugin("mock-analytics");
    privacy::reset();
    analytics::reset();
    services::mockAnalyticsLog(true);
    setEnvVar("AIVERSE_MOCK_DEFERRED_LINK", "dronefall://promo/42");
    lua_State* L = luaL_newstate();
    luaL_requiref(L, "_G", luaopen_base, 1);
    lua_pop(L, 1);
    auto alive = std::make_shared<bool>(true);
    services::openServicesLua(L, alive);
    const char* code = R"(
      assert(analytics.available() and analytics.providers()[1] == "mock-analytics")
      analytics.event("robot_unlocked", { robot_id = "r2", first = true, level = 3, skip = { 1 } })
      analytics.design("match_end:pve:win", 93.5, { kills = 7 })
      analytics.design("tutorial:step1")
      analytics.progression("complete", "pve:stage_3", { kills = 7 }, 120)
      analytics.progression("start", "pve", 5)
      analytics.resource("sink", "coins", 50, "robot", "r2_level")
      analytics.purchase{ currency = "USD", priceMicros = 990000, itemType = "gems", itemId = "gems_100", receipt = { token = "t" } }
      analytics.user("player-1")
      analytics.property("cohort", 3)
      analytics.deferredLink(function(url) link = url end)
      assert(privacy.provider() == "mock-att")
      privacy.requestTracking(function(s) status = s end)
      local c = privacy.consent({ personalizedAds = false })
      assert(c.analytics == true and c.personalizedAds == false and c.set == true)
    )";
    const bool ran = luaL_loadstring(L, code) == LUA_OK && lua_pcall(L, 0, 0, 0) == LUA_OK;
    if (!ran) std::fprintf(stderr, "lua: %s\n", lua_tostring(L, -1));
    CHECK(ran);
    plugins::pump();
    lua_getglobal(L, "status");
    CHECK(lua_isstring(L, -1) && std::string(lua_tostring(L, -1)) == "authorized");
    lua_getglobal(L, "link");
    CHECK(lua_isstring(L, -1) && std::string(lua_tostring(L, -1)) == "dronefall://promo/42");
    lua_pop(L, 2);
    const auto log = services::mockAnalyticsLog(true);
    // Lua table order is unspecified: check the fields one by one (the nested table is dropped)
    CHECK(std::any_of(log.begin(), log.end(), [](const std::string& l) {
        return l.rfind("event ", 0) == 0 && l.find("\"name\":\"robot_unlocked\"") != std::string::npos && l.find("\"first\":1") != std::string::npos &&
               l.find("\"level\":3") != std::string::npos && l.find("\"robot_id\":\"r2\"") != std::string::npos && l.find("skip") == std::string::npos;
    }));
    CHECK(contains(log, R"(design {"fields":{"kills":7},"id":"match_end:pve:win","value":93.5})"));
    CHECK(contains(log, R"(design {"fields":{},"id":"tutorial:step1"})"));
    CHECK(contains(log, R"(progression {"fields":{"kills":7},"status":"complete","path":["pve","stage_3"],"score":120})"));
    CHECK(contains(log, R"(progression {"fields":{},"status":"start","path":["pve"],"score":5})"));
    CHECK(contains(log, R"(resource {"fields":{},"flow":"sink","currency":"coins","amount":50,"itemType":"robot","itemId":"r2_level"})"));
    CHECK(contains(log, R"(purchase {"fields":{},"currency":"USD","priceMicros":990000,"itemType":"gems","itemId":"gems_100","cartType":"","receipt":{"token":"t"}})"));
    CHECK(contains(log, R"(user {"id":"player-1"})") && contains(log, R"(user {"properties":{"cohort":"3"}})"));
    CHECK(contains(log, R"(consent {"analytics":true,"tracking":true})"));

    // a callback whose state is gone is dropped, never called into a closed lua_State
    privacy::requestTracking([](privacy::Tracking) {});
    lua_close(L);
    *alive = false;
    plugins::pump();
    setEnvVar("AIVERSE_MOCK_DEFERRED_LINK", "");
    privacy::reset();
    analytics::reset();
}
#endif
