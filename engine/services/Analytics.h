// Analytics, one API for every SDK: GameAnalytics, Facebook App Events, AppMetrica and the desktop
// mock. Unlike store / ads (one provider wins), every analytics provider built into the app gets every
// call: a game typically sends design / progression data to GameAnalytics and install / purchase
// attribution to Facebook at the same time. game.json:
//
//   "plugins":   { "gameanalytics": { "gameKey": {...}, "secretKey": {...} }, "facebook": { "appId": "..." } },
//   "analytics": { "providers": { "ios": ["gameanalytics", "facebook"], "android": [...] },   // optional
//                  "consentRequired": false }   // true: nothing is sent before setConsent(analytics = true)
//
// Without "providers" every built plugin of: gameanalytics, facebook, appmetrica, mock-analytics.
// A provider maps each call to the closest native API and ignores what it has no equivalent for
// (docs/plugins.md § analytics). Analytics never runs inside the simulation: call it from the client
// (UI Lua `analytics.*`, C++ host code), never from sim scripts — replays would send events again.
//
// Fields are a flat JSON object ("{\"kills\": 3, \"mode\": \"pve\"}"): strings and numbers pass,
// booleans become 1 / 0 (GameAnalytics accepts no booleans), nested values and nulls are dropped.
#pragma once
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace aiverse::analytics {

enum class Progression : uint8_t { Start, Complete, Fail };
enum class Flow : uint8_t { Source, Sink };   // resource gained / spent

bool available();                        // at least one provider is built in
std::vector<std::string> providers();    // the providers calls go to, in order

// A named custom event: Facebook logEvent, AppMetrica reportEvent, GameAnalytics design event.
void event(const std::string& name, const std::string& fieldsJson = "{}");
// GameAnalytics design event ("segment:segment:..." up to 5 parts), optionally with a value.
void design(const std::string& id, const std::string& fieldsJson = "{}", std::optional<double> value = std::nullopt);
// Level progression: path "world:level:phase" (1..3 parts), optional score.
void progression(Progression status, const std::string& path, const std::string& fieldsJson = "{}",
                 std::optional<int64_t> score = std::nullopt);
// Virtual currency flow: currency / itemType must be listed in the provider config (GameAnalytics).
void resource(Flow flow, const std::string& currency, double amount, const std::string& itemType,
              const std::string& itemId, const std::string& fieldsJson = "{}");
// A real-money purchase (store::Product::priceMicros / currency). receiptJson lets the provider validate
// it: {"token", "productId"} for Google Play, {"transactionId"} for the App Store; "{}" if none.
void purchase(const std::string& currency, int64_t priceMicros, const std::string& itemType, const std::string& itemId,
              const std::string& cartType = "", const std::string& receiptJson = "{}", const std::string& fieldsJson = "{}");

// User identity / segmentation: GameAnalytics custom dimensions ("dimension01".."dimension03") and
// global custom fields (any other key), Facebook / AppMetrica user id.
void setUserId(const std::string& id);
void setUserProperty(const std::string& key, const std::string& value);

// GDPR consent (analytics) and the ad-tracking answer (iOS ATT, privacy::requestTracking). Without
// analytics consent nothing is sent and providers stop their own automatic events. Usually called
// through privacy::setConsent / privacy::apply rather than directly.
void setConsent(bool analytics, bool tracking);
bool consented();

// Sends buffered events now (before the app may be killed: match end, going to background).
void flush();

// The deep link of the ad that brought this install (Facebook deferred app link), "" if none / no
// provider supports it. Ask once, on the first launch.
void deferredLink(std::function<void(const std::string& url)> cb);

// Re-reads game.json (tests, a launcher switching games); the next call loads the providers again.
void reset();

// Normalized fields object (see above) — exposed for tests and the Lua binding.
std::string cleanFields(const std::string& fieldsJson);

} // namespace aiverse::analytics
