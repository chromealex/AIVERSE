// Notifications: remote push (Firebase Cloud Messaging on Android, APNs on iOS, web push) and
// local notifications scheduled by the game ("energy is full", "your chest is ready", the comeback
// reminders BW's tutorial meta schedules). Protocol "push" (docs/plugins.md), plugin "push" (Android,
// iOS) and the desktop mock "mock-push".
//
//   "plugins": { "push": { "channel": {"id": "main", "name": "Game"}, "askOnStart": false } }
//   "push": { "providers": ["push"] }    // optional
//
// The remote token goes to the game's backend (it sends the pushes); local notifications need no
// server. Categories / tags let a game cancel a whole group (BW: ClearPushNotificationByCategory/Tag).
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace aiverse::push {

struct Local {
    std::string id;          // unique: scheduling the same id again replaces it
    std::string title, body;
    double delay = 0;        // seconds from now
    std::string category;    // group for cancelCategory (BW push categories)
    std::string tag;         // second grouping (BW tags)
    std::string data;        // JSON object handed back in "opened"
    int64_t repeat = 0;      // seconds between repeats (0 = once)
};

bool available();
std::string provider();       // "push", "mock-push", ""

// Asks the player (iOS / Android 13+ system dialog; at once where no permission is needed).
void requestPermission(std::function<void(bool granted)> cb = {});
// The remote token for the backend ("" when there is none yet); also arrives as event "token".
void token(std::function<void(const std::string&)> cb);
void schedule(const Local& n, std::function<void(bool, const std::string&)> cb = {});
void cancel(const std::string& id);
void cancelCategory(const std::string& category);
void cancelTag(const std::string& tag);
void cancelAll();
void setBadge(int count);

// Events: "token" {"token"}, "received" {"title", "body", "data", "foreground": true} (a push while
// the game is open), "opened" {"id"?, "data"} (the player started the game from a notification;
// also delivered once at startup when that is how the game was launched).
int onEvent(std::function<void(const std::string& event, const std::string& dataJson)> cb);
void removeListener(int id);
void reset();   // tests

} // namespace aiverse::push
