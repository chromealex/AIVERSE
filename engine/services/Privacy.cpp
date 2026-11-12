#include "Privacy.h"

#include "Ads.h"
#include "Analytics.h"
#include "engine/platform/PluginJson.h"
#include "engine/platform/Plugins.h"

#include <vector>

namespace aiverse::privacy {
namespace {
namespace json = plugins::json;

struct State {
    bool loaded = false;
    bool applying = false;
    std::string provider;
    Tracking tracking = Tracking::Unavailable;
    Consent consent;
    bool consentSet = false;
    int listener = 0;
};
State& S() { static State s; return s; }

void load() {
    State& s = S();
    if (s.loaded) return;
    s.loaded = true;
    json::Value cfg = json::parseOr(plugins::gameSection("privacy"), json::Value::object());
    std::vector<std::string> order;
    const json::Value& prov = cfg["providers"];
    if (prov.isArray()) for (auto& v : prov.arr) order.push_back(v.str());
    else if (!prov.isNull()) order.push_back(prov.str());
    if (order.empty()) order = {"att", "mock-att"};
    for (auto& n : order) if (plugins::available(n)) { s.provider = n; break; }
    if (s.provider.empty()) return;
    s.tracking = Tracking::NotDetermined; // until the plugin tells otherwise
    s.listener = plugins::addListener(s.provider, [](const std::string& ev, const std::string& data) {
        if (ev != "status") return;
        S().tracking = parseTracking(json::parseOr(data)["status"].str());
        apply();
    });
    plugins::call(s.provider, "status", "{}", [](const plugins::Result& r) {
        if (!r.ok) return;
        Tracking t = parseTracking(json::parseOr(r.data)["status"].str());
        if (t != S().tracking) { S().tracking = t; apply(); }
    });
}

void answer(const std::function<void(Tracking)>& cb, const plugins::Result& r) {
    if (r.ok) S().tracking = parseTracking(json::parseOr(r.data)["status"].str());
    apply();
    if (cb) cb(S().tracking);
}
} // namespace

const char* name(Tracking t) {
    switch (t) {
    case Tracking::NotDetermined: return "notDetermined";
    case Tracking::Restricted: return "restricted";
    case Tracking::Denied: return "denied";
    case Tracking::Authorized: return "authorized";
    default: return "unavailable";
    }
}

Tracking parseTracking(const std::string& s) {
    if (s == "notDetermined") return Tracking::NotDetermined;
    if (s == "restricted") return Tracking::Restricted;
    if (s == "denied") return Tracking::Denied;
    if (s == "authorized") return Tracking::Authorized;
    return Tracking::Unavailable;
}

std::string provider() { load(); return S().provider; }
Tracking tracking() { load(); return S().tracking; }
bool trackingAllowed() { Tracking t = tracking(); return t == Tracking::Authorized || t == Tracking::Unavailable; }
Consent consent() { return S().consent; }
bool consentSet() { return S().consentSet; }
void attach() { load(); }

void requestTracking(std::function<void(Tracking)> cb) {
    load();
    if (S().provider.empty()) { if (cb) cb(Tracking::Unavailable); return; }
    plugins::call(S().provider, "request", "{}", [cb](const plugins::Result& r) { answer(cb, r); });
}

void refreshTracking(std::function<void(Tracking)> cb) {
    load();
    if (S().provider.empty()) { if (cb) cb(Tracking::Unavailable); return; }
    plugins::call(S().provider, "status", "{}", [cb](const plugins::Result& r) { answer(cb, r); });
}

void setConsent(const Consent& c) {
    S().consent = c;
    S().consentSet = true;
    apply();
}

void apply() {
    State& s = S();
    if (s.applying) return; // analytics / ads loading call attach() → load(), never back into apply
    s.applying = true;
    load();
    const bool tracking = trackingAllowed();
    ads::setConsent(s.consent.personalizedAds && tracking, s.consent.ageRestricted);
    analytics::setConsent(s.consentSet ? s.consent.analytics : analytics::consented(), tracking);
    s.applying = false;
}

void reset() {
    if (S().listener) plugins::removeListener(S().listener);
    S() = State{};
}

} // namespace aiverse::privacy
