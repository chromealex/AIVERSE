#include "Consent.h"

#include "Privacy.h"
#include "ServiceProvider.h"

namespace aiverse::consent {
namespace {
namespace json = plugins::json;

struct State { bool loaded = false; std::string provider; };
State& S() { static State s; return s; }

void load() {
    if (S().loaded) return;
    S().loaded = true;
    auto list = services::availableProviders("consent", {"ump", "mock-consent"});
    if (!list.empty()) S().provider = list[0];
}

Result parse(const plugins::Result& r) {
    Result out;
    if (!r.ok) { out.error = r.error; return out; }
    json::Value v = json::parseOr(r.data);
    std::string s = v["status"].str();
    out.status = s == "notRequired" ? Status::NotRequired : s == "required" ? Status::Required : s == "obtained" ? Status::Obtained : Status::Unknown;
    out.canRequestAds = v["canRequestAds"].boolean(true);
    out.personalized = v["personalized"].boolean(out.status == Status::NotRequired);
    out.privacyOptionsRequired = v["optionsRequired"].boolean();
    return out;
}

// The CMP answer is the GDPR consent of the privacy service: ads and analytics get it before the callback runs.
// No answer (error / unknown status) leaves what the game set.
void forward(const Result& r) {
    if (!r.error.empty() || r.status == Status::Unknown) return;
    privacy::Consent c = privacy::consent();
    c.personalizedAds = r.personalized;
    privacy::setConsent(c);
}

void send(const char* method, const json::Value& args, std::function<void(const Result&)> cb) {
    load();
    if (S().provider.empty()) {
        Result none;
        none.status = Status::NotRequired;   // no CMP in this build: nothing to ask
        none.personalized = true;
        if (cb) cb(none);
        return;
    }
    plugins::call(S().provider, method, json::dump(args), [cb](const plugins::Result& r) {
        Result res = parse(r);
        forward(res);
        if (cb) cb(res);
    });
}
} // namespace

bool available() { load(); return !S().provider.empty(); }
std::string provider() { load(); return S().provider; }

void request(std::function<void(const Result&)> cb, bool debugEEA) {
    json::Value a = json::Value::object();
    if (debugEEA) a.set("debugGeography", json::Value::string("eea"));
    send("request", a, std::move(cb));
}

void showOptions(std::function<void(const Result&)> cb) { send("options", json::Value::object(), std::move(cb)); }

const char* name(Status s) {
    switch (s) {
    case Status::NotRequired: return "notRequired";
    case Status::Required: return "required";
    case Status::Obtained: return "obtained";
    default: return "unknown";
    }
}

void reset() { S() = State{}; }

} // namespace aiverse::consent
