#include "Links.h"

#include "ServiceProvider.h"

#include <cctype>
#include <chrono>
#include <map>

namespace aiverse::links {
namespace {
namespace json = plugins::json;
using Clock = std::chrono::steady_clock;

struct State {
    bool loaded = false;
    std::vector<std::string> providers;
    std::vector<int> pluginListeners;
    int nextId = 1;
    std::map<int, std::function<void(const Link&)>> listeners;
    std::string lastUrl;
    Clock::time_point lastAt{};
    bool initialAsked = false, initialDone = false;
    int initialPending = 0;
    Link initial;
    std::vector<std::function<void(const Link&)>> initialWaiters;
};
State& S() { static State s; return s; }

void deliver(const Link& l) {
    State& s = S();
    if (l.url.empty()) return;
    auto now = Clock::now();
    if (l.url == s.lastUrl && now - s.lastAt < std::chrono::seconds(5)) return;   // OS + SDK both report it
    s.lastUrl = l.url;
    s.lastAt = now;
    auto copy = s.listeners;
    for (auto& [id, cb] : copy) cb(l);
}

Link fromJson(const std::string& source, const json::Value& v) {
    Link l;
    l.url = v["url"].str();
    l.source = source;
    l.deferred = v["deferred"].boolean();
    if (const json::Value* d = v.find("data")) l.dataJson = json::dump(*d);
    return l;
}

void load() {
    State& s = S();
    if (s.loaded) return;
    s.loaded = true;
    s.providers = services::availableProviders("links", {"deeplinks", "appsflyer", "mock-links"});
    for (const std::string& p : s.providers)
        s.pluginListeners.push_back(plugins::addListener(p, [p](const std::string& ev, const std::string& data) {
            if (ev == "link") deliver(fromJson(p, json::parseOr(data)));
        }));
}

std::string decode(const std::string& s) {
    std::string out;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '%' && i + 2 < s.size() && std::isxdigit(static_cast<unsigned char>(s[i + 1])) &&
            std::isxdigit(static_cast<unsigned char>(s[i + 2]))) {
            out += char(std::stoi(s.substr(i + 1, 2), nullptr, 16));
            i += 2;
        } else out += s[i] == '+' ? ' ' : s[i];
    }
    return out;
}
} // namespace

bool available() { load(); return !S().providers.empty(); }
std::vector<std::string> providers() { load(); return S().providers; }

void initial(std::function<void(const Link&)> cb) {
    load();
    State& s = S();
    if (s.initialDone) { if (cb) cb(s.initial); return; }
    if (cb) s.initialWaiters.push_back(std::move(cb));
    if (s.initialAsked) return;
    s.initialAsked = true;
    s.initialPending = int(s.providers.size());
    auto finish = [] {
        State& st = S();
        st.initialDone = true;
        auto waiters = std::move(st.initialWaiters);
        st.initialWaiters.clear();
        for (auto& w : waiters) w(st.initial);
        if (!st.initial.url.empty()) deliver(st.initial);
    };
    if (s.initialPending == 0) { finish(); return; }
    for (const std::string& p : s.providers)
        plugins::call(p, "initial", "{}", [p, finish](const plugins::Result& r) {
            State& st = S();
            if (r.ok && st.initial.url.empty()) {
                Link l = fromJson(p, json::parseOr(r.data));
                if (!l.url.empty()) st.initial = l;
            }
            if (--st.initialPending == 0) finish();
        });
}

int onLink(std::function<void(const Link&)> cb) {
    load();
    int id = S().nextId++;
    S().listeners[id] = std::move(cb);
    return id;
}

void removeListener(int id) { S().listeners.erase(id); }

Parsed parse(const std::string& url) {
    Parsed p;
    std::string rest = url;
    size_t hash = rest.find('#');
    if (hash != std::string::npos) rest = rest.substr(0, hash);
    size_t colon = rest.find(':');
    if (colon != std::string::npos && rest.find('/') > colon) {
        p.scheme = rest.substr(0, colon);
        rest = rest.substr(colon + 1);
    }
    if (rest.rfind("//", 0) == 0) {
        rest = rest.substr(2);
        size_t end = rest.find_first_of("/?");
        p.host = rest.substr(0, end);
        rest = end == std::string::npos ? std::string() : rest.substr(end);
    }
    size_t q = rest.find('?');
    p.path = decode(rest.substr(0, q));
    while (!p.path.empty() && p.path.front() == '/') p.path.erase(0, 1);
    if (q != std::string::npos) {
        std::string qs = rest.substr(q + 1);
        size_t i = 0;
        while (i <= qs.size()) {
            size_t amp = qs.find('&', i);
            std::string part = qs.substr(i, amp == std::string::npos ? std::string::npos : amp - i);
            if (!part.empty()) {
                size_t eq = part.find('=');
                p.query.emplace_back(decode(part.substr(0, eq)), eq == std::string::npos ? std::string() : decode(part.substr(eq + 1)));
            }
            if (amp == std::string::npos) break;
            i = amp + 1;
        }
    }
    return p;
}

std::string query(const Parsed& p, const std::string& key, const std::string& def) {
    for (auto& [k, v] : p.query) if (k == key) return v;
    return def;
}

void reset() {
    State& s = S();
    for (int id : s.pluginListeners) plugins::removeListener(id);
    s = State{};
}

} // namespace aiverse::links
