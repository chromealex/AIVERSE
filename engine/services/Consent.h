// GDPR / CCPA consent through a consent management platform (Google User Messaging Platform:
// plugin "ump"; desktop mock "mock-consent"). Protocol "consent" (docs/plugins.md).
//
//   consent::request([](const consent::Result& r) { ads::setConsent(r.personalized); if (r.canRequestAds) ads::preload(...); });
//
// request() refreshes the consent information and shows the CMP form when the region requires it
// (EEA, UK, Switzerland, US states with privacy laws); the answer is remembered by the platform, so
// later launches return at once. Games with a privacy settings button show it when
// privacyOptionsRequired and call showOptions(). Where the "privacy" service exists
// (engine/services/Privacy.h, branch agent-sdk-plugins) pass the answer to privacy::setConsent.
#pragma once
#include <functional>
#include <string>

namespace aiverse::consent {

enum class Status { Unknown, NotRequired, Required, Obtained };

struct Result {
    Status status = Status::Unknown;
    bool canRequestAds = true;          // ads may be loaded (consent obtained or not required)
    bool personalized = false;          // TCF purposes 1, 3, 4 granted (or no consent required)
    bool privacyOptionsRequired = false;
    std::string error;
};

bool available();
std::string provider();     // "ump", "mock-consent", ""
// debugEEA: test as if in the EEA (with the device registered as a test device in the plugin config).
void request(std::function<void(const Result&)> cb, bool debugEEA = false);
void showOptions(std::function<void(const Result&)> cb = {});
const char* name(Status s);  // "unknown", "notRequired", "required", "obtained"
void reset();                // tests

} // namespace aiverse::consent
