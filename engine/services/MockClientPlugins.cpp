// Desktop stand-ins for the client platform services (agent-bw-client): "mock-social" (Steam-like
// achievements, stats, friends, presence, overlay), "mock-push" (local notifications, a fake token),
// "mock-links" (deep links) and "mock-consent" (a CMP form). Registered like the store / ads mocks (MockPlugins.cpp): listed in
// game.json "plugins" or AIVERSE_MOCK_PLUGINS=1; compiled out of packaged builds.
//
//   AIVERSE_MOCK_LINK=<url>              mock-links: the launch link
//   AIVERSE_MOCK_PUSH_OPENED=<json>      mock-push: started from a notification with this data
//   AIVERSE_MOCK_SOCIAL_LOG=<file>       mock-social: every call appended as "method {json}"
//   AIVERSE_MOCK_CONSENT=granted|denied|outside   mock-consent: the player's answer / not in a consent region
#include "MockPlugins.h"

#include "engine/platform/PluginJson.h"
#include "engine/platform/Plugins.h"

#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <memory>
#include <string>

namespace aiverse::services {
namespace {
namespace json = plugins::json;

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v ? v : "";
}

class MockSocial : public plugins::NativePlugin {
public:
    void call(const std::string& method, const std::string& args, plugins::Reply reply) override {
        json::Value a = json::parseOr(args);
        if (std::string log = env("AIVERSE_MOCK_SOCIAL_LOG"); !log.empty())
            if (FILE* f = std::fopen(log.c_str(), "a")) { std::fprintf(f, "%s %s\n", method.c_str(), args.c_str()); std::fclose(f); }
        if (method == "init") { config_ = a; reply.ok(); return; }
        if (method == "lifecycle") { reply.ok(); return; }
        if (method == "user") {
            json::Value u = json::Value::object();
            u.set("id", json::Value::string("76561197960287930")).set("name", json::Value::string("Player"))
             .set("language", json::Value::string("english")).set("country", json::Value::string("US"));
            reply.ok(json::dump(u));
            return;
        }
        if (method == "unlock") {
            std::string id = a["id"].str();
            if (id.empty()) { reply.fail("mock-social: no achievement id"); return; }
            if (!unlocked_.count(id)) {
                unlocked_[id] = int64_t(std::time(nullptr));
                json::Value e = json::Value::object();
                e.set("id", json::Value::string(id));
                plugins::emit("mock-social", "achievement", json::dump(e));
            }
            reply.ok();
            return;
        }
        if (method == "progress") {
            if (a["current"].i64() >= a["max"].i64() && a["max"].i64() > 0) unlocked_.emplace(a["id"].str(), int64_t(std::time(nullptr)));
            progress_[a["id"].str()] = a["max"].i64() > 0 ? double(a["current"].i64()) / double(a["max"].i64()) : 0;
            reply.ok();
            return;
        }
        if (method == "achievements") {
            json::Value list = json::Value::array();
            for (auto& [k, v] : config_["achievements"].obj) add(list, k);
            for (auto& [id, t] : unlocked_) if (!config_["achievements"].find(id)) add(list, id);
            reply.ok(json::dump(list));
            return;
        }
        if (method == "stat") {
            std::string id = a["id"].str();
            if (a.find("set")) stats_[id] = a["set"].num();
            else if (a.find("add")) stats_[id] += a["add"].num();
            json::Value v = json::Value::object();
            char buf[64];
            std::snprintf(buf, sizeof buf, "%.17g", stats_[id]);
            v.set("value", json::Value::number(buf));
            reply.ok(json::dump(v));
            return;
        }
        if (method == "friends") {
            json::Value list = json::Value::array();
            const char* names[] = {"Ally", "Rival", "Sleepy"};
            const char* states[] = {"online", "online", "offline"};
            for (int i = 0; i < 3; i++) {
                json::Value f = json::Value::object();
                f.set("id", json::Value::string(std::to_string(76561197960287931LL + i))).set("name", json::Value::string(names[i]))
                 .set("state", json::Value::string(states[i])).set("playingThisGame", json::Value::boolean_(i == 0))
                 .set("status", json::Value::string(i == 0 ? "In a match" : ""));
                list.push(std::move(f));
            }
            reply.ok(json::dump(list));
            return;
        }
        if (method == "presence") {
            if (a["clear"].boolean()) presence_.clear();
            else if (a["value"].str().empty()) presence_.erase(a["key"].str());
            else presence_[a["key"].str()] = a["value"].str();
            reply.ok();
            return;
        }
        if (method == "overlay") {
            plugins::emit("mock-social", "app.pause", "null");   // like the Steam overlay
            plugins::emit("mock-social", "app.resume", "null");
            reply.ok();
            return;
        }
        if (method == "invite") {
            if (!presence_.count("connect") && a["connect"].str().empty()) { reply.fail("mock-social: nothing to join"); return; }
            reply.ok();
            return;
        }
        if (method == "join") {   // test hook: a friend accepted an invite
            json::Value e = json::Value::object();
            e.set("connect", json::Value::string(a["connect"].str())).set("friend", json::Value::string("76561197960287931"));
            plugins::emit("mock-social", "join", json::dump(e));
            reply.ok();
            return;
        }
        reply.fail("mock-social: unknown method " + method);
    }

private:
    void add(json::Value& list, const std::string& id) {
        json::Value v = json::Value::object();
        auto it = unlocked_.find(id);
        v.set("id", json::Value::string(id)).set("name", json::Value::string(id)).set("description", json::Value::string(""))
         .set("unlocked", json::Value::boolean_(it != unlocked_.end()))
         .set("unlockTime", json::Value::number(it != unlocked_.end() ? it->second : int64_t(0)));
        list.push(std::move(v));
    }
    json::Value config_ = json::Value::object();
    std::map<std::string, int64_t> unlocked_;
    std::map<std::string, double> stats_, progress_;
    std::map<std::string, std::string> presence_;
};

class MockPush : public plugins::NativePlugin {
public:
    void call(const std::string& method, const std::string& args, plugins::Reply reply) override {
        json::Value a = json::parseOr(args);
        if (method == "init") {
            reply.ok();
            json::Value t = json::Value::object();
            t.set("token", json::Value::string("mock-push-token"));
            plugins::emit("mock-push", "token", json::dump(t));
            if (std::string opened = env("AIVERSE_MOCK_PUSH_OPENED"); !opened.empty()) {
                json::Value e = json::Value::object();
                e.set("data", json::parseOr(opened));
                plugins::emit("mock-push", "opened", json::dump(e));
            }
            return;
        }
        if (method == "lifecycle" || method == "badge") { reply.ok(); return; }
        if (method == "permission") { reply.ok(R"({"granted":true})"); return; }
        if (method == "token") { reply.ok(R"({"token":"mock-push-token"})"); return; }
        if (method == "schedule") {
            if (a["id"].str().empty()) { reply.fail("mock-push: no id"); return; }
            scheduled_[a["id"].str()] = a;
            reply.ok();
            return;
        }
        if (method == "cancel") {
            for (auto it = scheduled_.begin(); it != scheduled_.end();) {
                const json::Value& n = it->second;
                bool hit = a["all"].boolean() || (a.find("id") && n["id"].str() == a["id"].str()) ||
                           (a.find("category") && n["category"].str() == a["category"].str()) ||
                           (a.find("tag") && n["tag"].str() == a["tag"].str());
                it = hit ? scheduled_.erase(it) : std::next(it);
            }
            reply.ok();
            return;
        }
        if (method == "pending") {   // test hook: what is scheduled
            json::Value list = json::Value::array();
            for (auto& [id, n] : scheduled_) list.push(n);
            reply.ok(json::dump(list));
            return;
        }
        if (method == "fire") {      // test hook: deliver a scheduled one as if the player tapped it
            auto it = scheduled_.find(a["id"].str());
            if (it == scheduled_.end()) { reply.fail("mock-push: not scheduled"); return; }
            json::Value e = json::Value::object();
            e.set("id", json::Value::string(it->first)).set("data", it->second["data"]);
            plugins::emit("mock-push", "opened", json::dump(e));
            scheduled_.erase(it);
            reply.ok();
            return;
        }
        reply.fail("mock-push: unknown method " + method);
    }

private:
    std::map<std::string, json::Value> scheduled_;
};

class MockLinks : public plugins::NativePlugin {
public:
    void call(const std::string& method, const std::string& args, plugins::Reply reply) override {
        json::Value a = json::parseOr(args);
        if (method == "init" || method == "lifecycle") { reply.ok(); return; }
        if (method == "initial") {
            json::Value v = json::Value::object();
            v.set("url", json::Value::string(env("AIVERSE_MOCK_LINK")));
            reply.ok(json::dump(v));
            return;
        }
        if (method == "open") {      // test hook / desktop: a link arrives while running
            json::Value v = json::Value::object();
            v.set("url", json::Value::string(a["url"].str()));
            plugins::emit("mock-links", "link", json::dump(v));
            reply.ok();
            return;
        }
        reply.fail("mock-links: unknown method " + method);
    }
};
class MockConsent : public plugins::NativePlugin {
public:
    void call(const std::string& method, const std::string&, plugins::Reply reply) override {
        if (method == "init" || method == "lifecycle") { reply.ok(); return; }
        if (method != "request" && method != "options") { reply.fail("mock-consent: unknown method " + method); return; }
        std::string answer = env("AIVERSE_MOCK_CONSENT");
        json::Value r = json::Value::object();
        bool outside = answer == "outside", granted = answer.empty() || answer == "granted";
        r.set("status", json::Value::string(outside ? "notRequired" : "obtained")).set("canRequestAds", json::Value::boolean_(true))
         .set("personalized", json::Value::boolean_(outside || granted)).set("optionsRequired", json::Value::boolean_(!outside));
        if (!outside) { plugins::emit("mock-consent", "app.pause", "null"); plugins::emit("mock-consent", "app.resume", "null"); }
        reply.ok(json::dump(r));
    }
};
} // namespace

bool registerClientMockPlugin(const std::string& name) {
    if (name == "mock-social") plugins::registerPlugin(name, std::make_shared<MockSocial>());
    else if (name == "mock-push") plugins::registerPlugin(name, std::make_shared<MockPush>());
    else if (name == "mock-links") plugins::registerPlugin(name, std::make_shared<MockLinks>());
    else if (name == "mock-consent") plugins::registerPlugin(name, std::make_shared<MockConsent>());
    else return false;
    return true;
}

} // namespace aiverse::services
