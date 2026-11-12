#include "Push.h"

#include "ServiceProvider.h"

#include <cstdio>
#include <map>

namespace aiverse::push {
namespace {
namespace json = plugins::json;

struct State {
    bool loaded = false;
    std::string provider;
    int pluginListener = 0;
    int nextId = 1;
    std::string token;
    std::map<int, std::function<void(const std::string&, const std::string&)>> listeners;
};
State& S() { static State s; return s; }

void load() {
    State& s = S();
    if (s.loaded) return;
    s.loaded = true;
    auto list = services::availableProviders("push", {"push", "mock-push"});
    if (list.empty()) return;
    s.provider = list[0];
    s.pluginListener = plugins::addListener(s.provider, [](const std::string& ev, const std::string& data) {
        if (ev.rfind("app.", 0) == 0) return;
        if (ev == "token") S().token = json::parseOr(data)["token"].str();
        auto copy = S().listeners;
        for (auto& [id, cb] : copy) cb(ev, data);
    });
}

void send(const char* method, const json::Value& args, plugins::Callback cb = {}) {
    load();
    if (S().provider.empty()) { if (cb) cb({false, "null", "no notifications in this build"}); return; }
    plugins::call(S().provider, method, json::dump(args), std::move(cb));
}

json::Value one(const char* key, const std::string& v) {
    json::Value a = json::Value::object();
    a.set(key, json::Value::string(v));
    return a;
}
} // namespace

bool available() { load(); return !S().provider.empty(); }
std::string provider() { load(); return S().provider; }

void requestPermission(std::function<void(bool)> cb) {
    send("permission", json::Value::object(), [cb](const plugins::Result& r) {
        if (cb) cb(r.ok && json::parseOr(r.data)["granted"].boolean());
    });
}

void token(std::function<void(const std::string&)> cb) {
    if (!S().token.empty()) { if (cb) cb(S().token); return; }
    send("token", json::Value::object(), [cb](const plugins::Result& r) {
        std::string t = r.ok ? json::parseOr(r.data)["token"].str() : std::string();
        if (!t.empty()) S().token = t;
        if (cb) cb(t);
    });
}

void schedule(const Local& n, std::function<void(bool, const std::string&)> cb) {
    if (n.id.empty()) { if (cb) cb(false, "push: a local notification needs an id"); return; }
    json::Value a = json::Value::object();
    char delay[32];
    std::snprintf(delay, sizeof delay, "%.3f", n.delay < 0 ? 0.0 : n.delay);
    a.set("id", json::Value::string(n.id)).set("title", json::Value::string(n.title)).set("body", json::Value::string(n.body))
     .set("delay", json::Value::number(delay)).set("category", json::Value::string(n.category)).set("tag", json::Value::string(n.tag))
     .set("repeat", json::Value::number(n.repeat));
    a.set("data", json::parseOr(n.data.empty() ? "{}" : n.data));
    send("schedule", a, [cb](const plugins::Result& r) { if (cb) cb(r.ok, r.error); });
}

void cancel(const std::string& id) { send("cancel", one("id", id)); }
void cancelCategory(const std::string& c) { send("cancel", one("category", c)); }
void cancelTag(const std::string& t) { send("cancel", one("tag", t)); }
void cancelAll() { json::Value a = json::Value::object(); a.set("all", json::Value::boolean_(true)); send("cancel", a); }
void setBadge(int count) { json::Value a = json::Value::object(); a.set("count", json::Value::number(int64_t(count))); send("badge", a); }

int onEvent(std::function<void(const std::string&, const std::string&)> cb) {
    load();
    int id = S().nextId++;
    S().listeners[id] = std::move(cb);
    return id;
}

void removeListener(int id) { S().listeners.erase(id); }

void reset() {
    State& s = S();
    if (s.pluginListener) plugins::removeListener(s.pluginListener);
    s = State{};
}

} // namespace aiverse::push
