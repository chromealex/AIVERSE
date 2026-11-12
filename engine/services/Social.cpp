#include "Social.h"

#include "ServiceProvider.h"

#include <cstdio>
#include <map>

namespace aiverse::social {
namespace {
namespace json = plugins::json;

struct State {
    bool loaded = false;
    std::string provider;
    int pluginListener = 0;
    int nextId = 1;
    std::map<int, std::function<void(const std::string&, const std::string&)>> listeners;
};
State& S() { static State s; return s; }

void load() {
    State& s = S();
    if (s.loaded) return;
    s.loaded = true;
    auto list = services::availableProviders("social", {"steam", "mock-social"});
    if (list.empty()) return;
    s.provider = list[0];
    s.pluginListener = plugins::addListener(s.provider, [](const std::string& ev, const std::string& data) {
        if (ev.rfind("app.", 0) == 0 || ev == "purchase") return;   // store / platform events of the same plugin
        auto copy = S().listeners;   // a handler may remove itself
        for (auto& [id, cb] : copy) cb(ev, data);
    });
}

json::Value obj(std::initializer_list<std::pair<const char*, json::Value>> kv) {
    json::Value v = json::Value::object();
    for (auto& [k, x] : kv) v.set(k, x);
    return v;
}

void send(const char* method, const json::Value& args, plugins::Callback cb = {}) {
    load();
    if (S().provider.empty()) {
        if (cb) cb({false, "null", "no social platform in this build"});
        return;
    }
    plugins::call(S().provider, method, json::dump(args), std::move(cb));
}

std::string num(double v) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.17g", v);
    return buf;
}
} // namespace

bool available() { load(); return !S().provider.empty(); }
std::string provider() { load(); return S().provider; }

void user(std::function<void(const User&)> cb) {
    send("user", json::Value::object(), [cb](const plugins::Result& r) {
        User u;
        u.ok = r.ok;
        u.error = r.error;
        if (r.ok) {
            json::Value v = json::parseOr(r.data);
            u.id = v["id"].str(); u.name = v["name"].str(); u.language = v["language"].str(); u.country = v["country"].str();
        }
        if (cb) cb(u);
    });
}

void unlock(const std::string& id, std::function<void(bool, const std::string&)> cb) {
    send("unlock", obj({{"id", json::Value::string(id)}}), [cb](const plugins::Result& r) { if (cb) cb(r.ok, r.error); });
}

void progress(const std::string& id, int current, int max) {
    send("progress", obj({{"id", json::Value::string(id)}, {"current", json::Value::number(int64_t(current))},
                          {"max", json::Value::number(int64_t(max))}}));
}

void achievements(std::function<void(bool, const std::vector<Achievement>&, const std::string&)> cb) {
    send("achievements", json::Value::object(), [cb](const plugins::Result& r) {
        std::vector<Achievement> out;
        if (r.ok)
            for (auto& a : json::parseOr(r.data, json::Value::array()).arr)
                out.push_back({a["id"].str(), a["name"].str(), a["description"].str(), a["unlocked"].boolean(),
                               a["unlockTime"].i64(), float(a["progress"].num())});
        if (cb) cb(r.ok, out, r.error);
    });
}

void setStat(const std::string& id, double value) {
    send("stat", obj({{"id", json::Value::string(id)}, {"set", json::Value::number(num(value))}}));
}

void addStat(const std::string& id, double delta) {
    send("stat", obj({{"id", json::Value::string(id)}, {"add", json::Value::number(num(delta))}}));
}

void stat(const std::string& id, std::function<void(bool, double)> cb) {
    send("stat", obj({{"id", json::Value::string(id)}}), [cb](const plugins::Result& r) {
        if (cb) cb(r.ok, r.ok ? json::parseOr(r.data)["value"].num() : 0.0);
    });
}

void friends(std::function<void(bool, const std::vector<Friend>&, const std::string&)> cb) {
    send("friends", json::Value::object(), [cb](const plugins::Result& r) {
        std::vector<Friend> out;
        if (r.ok)
            for (auto& f : json::parseOr(r.data, json::Value::array()).arr)
                out.push_back({f["id"].str(), f["name"].str(), f["state"].str("offline"), f["playingThisGame"].boolean(), f["status"].str()});
        if (cb) cb(r.ok, out, r.error);
    });
}

void setPresence(const std::string& key, const std::string& value) {
    send("presence", obj({{"key", json::Value::string(key)}, {"value", json::Value::string(value)}}));
}

void clearPresence() { send("presence", obj({{"clear", json::Value::boolean_(true)}})); }

void overlay(const std::string& page) { send("overlay", obj({{"page", json::Value::string(page)}})); }

void invite(const std::string& connect, const std::string& friendId) {
    json::Value a = obj({{"connect", json::Value::string(connect)}});
    if (!friendId.empty()) a.set("friend", json::Value::string(friendId));
    send("invite", a);
}

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

} // namespace aiverse::social
