// Platform social services: the signed-in player, achievements and stats, friends, rich presence,
// the platform overlay and game invites — one API over the platform plugin (Steam on desktop; the
// desktop mock "mock-social" for development and tests). Protocol "social" (docs/plugins.md).
//
//   "social": { "providers": { "desktop": ["steam", "mock-social"] } }    // optional
//   "plugins": { "steam": { "appId": 480, "achievements": { "first_win": "ACH_WIN_ONE_GAME" },
//                           "stats": { "wins": "NumWins" } } }
//
// Achievement / stat ids are the game's own; the plugin config maps them to the platform's API names
// (a missing entry means the same name). Unlocks and stats are stored by the platform (Steam keeps
// them offline and syncs later); calls before the platform is ready are queued by the plugin.
// Presentation / meta only: nothing here reaches the deterministic simulation.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace aiverse::social {

struct User { std::string id, name, language, country; bool ok = false; std::string error; };
struct Achievement { std::string id, name, description; bool unlocked = false; int64_t unlockTime = 0; float progress = 0; };
struct Friend {
    std::string id, name;
    std::string state;       // "offline", "online", "busy", "away", "snooze", "lookingToTrade", "lookingToPlay"
    bool playingThisGame = false;
    std::string status;      // the friend's rich presence "status" (this game only)
};

bool available();
std::string provider();          // "steam", "mock-social", ""

void user(std::function<void(const User&)> cb);
// Unlocks (idempotent). cb(ok, error) after the platform stored it.
void unlock(const std::string& achievement, std::function<void(bool, const std::string&)> cb = {});
// Progress popup ("3 / 10") for a stat-based achievement; unlocks at max.
void progress(const std::string& achievement, int current, int max);
void achievements(std::function<void(bool, const std::vector<Achievement>&, const std::string&)> cb);
// Stats: integer or float, set or added; stored with the next flush (the plugin batches).
void setStat(const std::string& stat, double value);
void addStat(const std::string& stat, double delta);
void stat(const std::string& stat, std::function<void(bool, double)> cb);
void friends(std::function<void(bool, const std::vector<Friend>&, const std::string&)> cb);
// Rich presence: key "status" is the text friends see; "connect" lets friends join (the join
// request arrives as event "join" {"connect"}); "" clears a key; clearPresence() clears all.
void setPresence(const std::string& key, const std::string& value);
void clearPresence();
// Opens the platform overlay: "friends", "achievements", "community", "players", "settings",
// "stats", "officialgamegroup", a "https://..." page, or "store" (the game's store page).
void overlay(const std::string& page);
// The platform's invite dialog (connect string as for presence), or invite one friend directly.
void invite(const std::string& connect, const std::string& friendId = "");

// Events from the platform: "join" {"connect", "friend"}, "overlay" {"active"},
// "achievement" {"id"} (stored), "presence" {"friend"} (a friend's presence changed).
int onEvent(std::function<void(const std::string& event, const std::string& dataJson)> cb);
void removeListener(int id);

void reset();   // tests: forget the provider, re-read game.json on next use

} // namespace aiverse::social
