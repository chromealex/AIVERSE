#include "Analytics.h"

#include "Privacy.h"
#include "engine/platform/PluginJson.h"
#include "engine/platform/Plugins.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace aiverse::analytics {
namespace {
namespace json = plugins::json;

struct State {
    bool loaded = false;
    std::vector<std::string> providers;
    bool consent = true;
};
State& S() { static State s; return s; }

void load() {
    State& s = S();
    if (s.loaded) return;
    s.loaded = true;
    json::Value cfg = json::parseOr(plugins::gameSection("analytics"), json::Value::object());
    std::vector<std::string> order;
    const json::Value& prov = cfg["providers"];
    if (prov.isArray()) for (auto& v : prov.arr) order.push_back(v.str());
    else if (!prov.isNull()) order.push_back(prov.str());
    if (order.empty()) order = {"gameanalytics", "facebook", "appmetrica", "mock-analytics"};
    for (auto& n : order)
        if (plugins::available(n) && std::find(s.providers.begin(), s.providers.end(), n) == s.providers.end()) s.providers.push_back(n);
    s.consent = !cfg["consentRequired"].boolean(false);
    privacy::attach(); // an ATT answer that arrived on its own (prompt at launch) reaches the providers
}

json::Value number(double v) {
    if (!std::isfinite(v)) return json::Value::number(int64_t(0));
    char buf[40];
    std::snprintf(buf, sizeof buf, "%.15g", v);
    return json::Value::number(std::string(buf));
}

json::Value cleanValue(const std::string& fieldsJson) {
    json::Value in = json::parseOr(fieldsJson, json::Value::object());
    json::Value out = json::Value::object();
    if (!in.isObject()) return out;
    for (auto& m : in.obj) {
        const json::Value& v = m.second;
        if (m.first.empty()) continue;
        if (v.type == json::Value::Type::String || v.type == json::Value::Type::Number) out.set(m.first, v);
        else if (v.type == json::Value::Type::Bool) out.set(m.first, json::Value::number(int64_t(v.b ? 1 : 0)));
    }
    return out;
}

// Sends to every provider; event-like methods are dropped without consent.
void send(const std::string& method, const json::Value& args, bool needsConsent = true) {
    load();
    if (needsConsent && !S().consent) return;
    const std::string text = json::dump(args);
    for (auto& p : S().providers) plugins::call(p, method, text);
}

json::Value withFields(const std::string& fieldsJson) {
    json::Value a = json::Value::object();
    a.set("fields", cleanValue(fieldsJson));
    return a;
}
} // namespace

bool available() { load(); return !S().providers.empty(); }
std::vector<std::string> providers() { load(); return S().providers; }
bool consented() { load(); return S().consent; }
std::string cleanFields(const std::string& fieldsJson) { return json::dump(cleanValue(fieldsJson)); }

void event(const std::string& name, const std::string& fieldsJson) {
    if (name.empty()) return;
    json::Value a = withFields(fieldsJson);
    a.set("name", json::Value::string(name));
    send("event", a);
}

void design(const std::string& id, const std::string& fieldsJson, std::optional<double> value) {
    if (id.empty()) return;
    json::Value a = withFields(fieldsJson);
    a.set("id", json::Value::string(id));
    if (value) a.set("value", number(*value));
    send("design", a);
}

void progression(Progression status, const std::string& path, const std::string& fieldsJson, std::optional<int64_t> score) {
    json::Value parts = json::Value::array();
    size_t from = 0;
    while (parts.arr.size() < 3) {
        size_t at = path.find(':', from);
        std::string part = path.substr(from, at == std::string::npos ? std::string::npos : at - from);
        if (!part.empty()) parts.push(json::Value::string(part));
        if (at == std::string::npos) break;
        from = at + 1;
    }
    if (parts.arr.empty()) return; // every SDK needs at least the first level of the path
    json::Value a = withFields(fieldsJson);
    a.set("status", json::Value::string(status == Progression::Start ? "start" : status == Progression::Complete ? "complete" : "fail"));
    a.set("path", std::move(parts));
    if (score) a.set("score", json::Value::number(*score));
    send("progression", a);
}

void resource(Flow flow, const std::string& currency, double amount, const std::string& itemType,
              const std::string& itemId, const std::string& fieldsJson) {
    json::Value a = withFields(fieldsJson);
    a.set("flow", json::Value::string(flow == Flow::Source ? "source" : "sink"))
     .set("currency", json::Value::string(currency)).set("amount", number(std::fabs(amount)))
     .set("itemType", json::Value::string(itemType)).set("itemId", json::Value::string(itemId));
    send("resource", a);
}

void purchase(const std::string& currency, int64_t priceMicros, const std::string& itemType, const std::string& itemId,
              const std::string& cartType, const std::string& receiptJson, const std::string& fieldsJson) {
    json::Value a = withFields(fieldsJson);
    a.set("currency", json::Value::string(currency)).set("priceMicros", json::Value::number(priceMicros))
     .set("itemType", json::Value::string(itemType)).set("itemId", json::Value::string(itemId))
     .set("cartType", json::Value::string(cartType)).set("receipt", json::parseOr(receiptJson, json::Value::object()));
    send("purchase", a);
}

void setUserId(const std::string& id) {
    json::Value a = json::Value::object();
    a.set("id", json::Value::string(id));
    send("user", a, false);
}

void setUserProperty(const std::string& key, const std::string& value) {
    if (key.empty()) return;
    json::Value props = json::Value::object();
    props.set(key, json::Value::string(value));
    json::Value a = json::Value::object();
    a.set("properties", std::move(props));
    send("user", a, false);
}

void setConsent(bool analytics, bool tracking) {
    load();
    S().consent = analytics;
    json::Value a = json::Value::object();
    a.set("analytics", json::Value::boolean_(analytics)).set("tracking", json::Value::boolean_(tracking));
    send("consent", a, false);
}

void flush() { send("flush", json::Value::object(), false); }

void deferredLink(std::function<void(const std::string&)> cb) {
    load();
    for (auto& p : S().providers) {
        if (p != "facebook" && p != "mock-analytics") continue;
        plugins::call(p, "deferredLink", "{}", [cb](const plugins::Result& r) {
            if (cb) cb(r.ok ? json::parseOr(r.data)["url"].str() : "");
        });
        return;
    }
    if (cb) cb("");
}

void reset() { S() = State{}; }

} // namespace aiverse::analytics
