// Privacy: the iOS App Tracking Transparency prompt and the player's consent, applied to the ads and
// analytics services in one place.
//
//   "plugins": { "att": { "usage": "Your data is used to show you relevant ads.", "prompt": "ready", "delay": 2 } }
//
// The "att" plugin (iOS) asks for tracking authorization — on privacy::requestTracking(), or by itself
// `delay` seconds after the first interactive frame with "prompt": "ready" — and writes
// NSUserTrackingUsageDescription into Info.plist. Other platforms have no prompt: tracking() is
// Unavailable there, which counts as allowed (Android ad id rules are the consent below).
//
// The effective settings every provider gets:
//   ads        personalized = consent.personalizedAds && trackingAllowed(), ageRestricted
//   analytics  analytics consent (once the game set it), tracking = trackingAllowed()
// A game with its own GDPR / CMP dialog calls setConsent() with the answer (and keeps it in its save).
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace aiverse::privacy {

enum class Tracking : uint8_t { Unavailable, NotDetermined, Restricted, Denied, Authorized };
const char* name(Tracking t);          // "unavailable", "notDetermined", "restricted", "denied", "authorized"
Tracking parseTracking(const std::string& s);

struct Consent {
    bool analytics = true;        // analytics events may be sent
    bool personalizedAds = true;  // GDPR consent to personalised ads
    bool ageRestricted = false;   // child-directed / age-restricted audience
};

std::string provider();          // "att", "mock-att" or "" (no tracking prompt on this platform)
Tracking tracking();             // last known answer
bool trackingAllowed();          // Authorized, or Unavailable (no prompt exists here)
// Shows the system prompt if the player was not asked yet; cb gets the answer (at once when there is
// nothing to ask). The answer is applied to ads and analytics before cb runs.
void requestTracking(std::function<void(Tracking)> cb = {});
// Reads the current status without prompting (the player may change it in Settings).
void refreshTracking(std::function<void(Tracking)> cb = {});

void setConsent(const Consent& c);
Consent consent();
bool consentSet();               // setConsent() was called this session

// Pushes the effective settings to ads::setConsent and analytics::setConsent.
void apply();
// Listens to the provider's own answers (prompt at launch). The ads / analytics services call it
// when they load; idempotent.
void attach();
void reset();                    // tests: forget state, re-read game.json on next use

} // namespace aiverse::privacy
