// Steam → the engine's "social" protocol (docs/plugins.md, engine/services/Social.h): the signed-in
// player, achievements (ISteamUserStats: unlock, progress popup, list with unlock times), stats
// (int or float, batched StoreStats), friends with their state / game / rich presence, own rich
// presence, the overlay (dialogs, web pages, the store page), invites (dialog with a connect string
// or one friend) and join requests ("join" event from GameRichPresenceJoinRequested_t, and the
// "+connect" launch command line when the game was started by accepting an invite).
//
// game.json: "steam": { "achievements": { "first_win": "ACH_WIN_ONE_GAME" }, "stats": { "wins": "NumWins" },
//                       "floatStats": ["playtime"] }    // ids missing from the maps are used as they are
// Since Steamworks 1.61 the client loads the current stats itself (no RequestCurrentStats).
// Included by SteamPlugin.cpp only (one translation unit: the plugin is one AIVERSE_PLUGIN).
#pragma once

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <set>
#include <string>

namespace steam_social {

namespace json = aiverse::plugins::json;
using aiverse::plugins::Reply;

inline std::string cs(const char* p) { return p ? p : ""; }   // the SDK may hand back null

class Social {
public:
    explicit Social(json::Value config) : config_(std::move(config)) {
        for (auto& v : config_["floatStats"].arr) floatStats_.insert(v.str());
        // started by accepting an invite while the game was not running: "+connect <string>"
        char cmd[1024] = {};
        if (SteamApps()->GetLaunchCommandLine(cmd, sizeof cmd) > 0) launchConnect_ = cmd;
    }

    bool handles(const std::string& m) const {
        static const char* kMethods[] = {"user", "unlock", "progress", "achievements", "stat", "friends", "presence", "overlay", "invite"};
        return std::any_of(std::begin(kMethods), std::end(kMethods), [&](const char* x) { return m == x; });
    }

    void call(const std::string& method, const json::Value& a, Reply reply) {
        if (method == "user") return user(reply);
        if (method == "unlock") return unlock(a, reply);
        if (method == "progress") {
            SteamUserStats()->IndicateAchievementProgress(achievement(a["id"].str()).c_str(), uint32(a["current"].i64()), uint32(a["max"].i64()));
            reply.ok();
            return;
        }
        if (method == "achievements") return achievements(reply);
        if (method == "stat") return stat(a, reply);
        if (method == "friends") return friends(reply);
        if (method == "presence") {
            if (a["clear"].boolean()) SteamFriends()->ClearRichPresence();
            else {
                std::string v = a["value"].str();
                if (!SteamFriends()->SetRichPresence(a["key"].str().c_str(), v.empty() ? nullptr : v.c_str())) {
                    reply.fail("steam: SetRichPresence failed (key/value too long or too many keys)");
                    return;
                }
            }
            reply.ok();
            return;
        }
        if (method == "overlay") return overlay(a["page"].str(), reply);
        if (method == "invite") {
            std::string connect = a["connect"].str(), who = a["friend"].str();
            if (!who.empty()) {
                CSteamID id(uint64(std::strtoull(who.c_str(), nullptr, 10)));
                if (!SteamFriends()->InviteUserToGame(id, connect.c_str())) { reply.fail("steam: InviteUserToGame failed"); return; }
            } else {
                SteamFriends()->ActivateGameOverlayInviteDialogConnectString(connect.c_str());
            }
            reply.ok();
            return;
        }
        reply.fail("steam: unknown method " + method);
    }

    void update() {
        if (!launchConnect_.empty()) {   // after init: the game has listeners now
            std::string c = launchConnect_;
            launchConnect_.clear();
            size_t at = c.find("+connect ");
            if (at != std::string::npos) emitJoin(c.substr(at + 9), "");
        }
        if (dirty_ && std::chrono::steady_clock::now() - lastStore_ > std::chrono::seconds(1)) {
            dirty_ = false;
            lastStore_ = std::chrono::steady_clock::now();
            SteamUserStats()->StoreStats();
        }
    }

private:
    std::string mapped(const char* section, const std::string& id) const {
        const json::Value* v = config_[section].find(id);
        return v ? v->str() : id;
    }
    std::string achievement(const std::string& id) const { return mapped("achievements", id); }
    std::string gameId(const std::string& apiName) const {
        for (auto& [k, v] : config_["achievements"].obj) if (v.str() == apiName) return k;
        return apiName;
    }

    void user(Reply reply) {
        json::Value u = json::Value::object();
        u.set("id", json::Value::string(std::to_string(static_cast<unsigned long long>(SteamUser()->GetSteamID().ConvertToUint64()))))
         .set("name", json::Value::string(cs(SteamFriends()->GetPersonaName())))
         .set("language", json::Value::string(cs(SteamApps()->GetCurrentGameLanguage())))
         .set("country", json::Value::string(cs(SteamUtils()->GetIPCountry())));
        reply.ok(json::dump(u));
    }

    void unlock(const json::Value& a, Reply reply) {
        std::string name = achievement(a["id"].str());
        bool had = false;
        SteamUserStats()->GetAchievement(name.c_str(), &had);
        if (!SteamUserStats()->SetAchievement(name.c_str())) {
            reply.fail("steam: no achievement \"" + name + "\" (Steamworks: Stats & Achievements, or game.json steam.achievements)");
            return;
        }
        if (!had) SteamUserStats()->StoreStats();   // the unlock popup shows after StoreStats
        reply.ok();
    }

    void achievements(Reply reply) {
        json::Value list = json::Value::array();
        auto add = [&](const std::string& api) {
            bool done = false;
            uint32 when = 0;
            if (!SteamUserStats()->GetAchievementAndUnlockTime(api.c_str(), &done, &when)) return;
            json::Value v = json::Value::object();
            v.set("id", json::Value::string(gameId(api)))
             .set("name", json::Value::string(cs(SteamUserStats()->GetAchievementDisplayAttribute(api.c_str(), "name"))))
             .set("description", json::Value::string(cs(SteamUserStats()->GetAchievementDisplayAttribute(api.c_str(), "desc"))))
             .set("unlocked", json::Value::boolean_(done)).set("unlockTime", json::Value::number(int64_t(when)));
            list.push(std::move(v));
        };
        uint32 n = SteamUserStats()->GetNumAchievements();
        for (uint32 i = 0; i < n; ++i)
            if (const char* api = SteamUserStats()->GetAchievementName(i)) add(api);
        reply.ok(json::dump(list));
    }

    void stat(const json::Value& a, Reply reply) {
        std::string id = a["id"].str(), name = mapped("stats", id);
        bool isFloat = floatStats_.count(id) > 0;
        double value = 0;
        if (isFloat) { float f = 0; if (!SteamUserStats()->GetStat(name.c_str(), &f)) { reply.fail("steam: no stat \"" + name + "\""); return; } value = f; }
        else { int32 i = 0; if (!SteamUserStats()->GetStat(name.c_str(), &i)) { reply.fail("steam: no stat \"" + name + "\""); return; } value = i; }
        if (a.find("set") || a.find("add")) {
            value = a.find("set") ? a["set"].num() : value + a["add"].num();
            bool ok = isFloat ? SteamUserStats()->SetStat(name.c_str(), float(value)) : SteamUserStats()->SetStat(name.c_str(), int32(value));
            if (!ok) { reply.fail("steam: SetStat failed for \"" + name + "\" (type, or a max / increment-only rule)"); return; }
            dirty_ = true;
        }
        char buf[64];
        std::snprintf(buf, sizeof buf, "%.17g", value);
        json::Value v = json::Value::object();
        v.set("value", json::Value::number(buf));
        reply.ok(json::dump(v));
    }

    void friends(Reply reply) {
        static const char* kStates[] = {"offline", "online", "busy", "away", "snooze", "lookingToTrade", "lookingToPlay", "invisible"};
        json::Value list = json::Value::array();
        AppId_t me = SteamUtils()->GetAppID();
        int n = SteamFriends()->GetFriendCount(k_EFriendFlagImmediate);
        for (int i = 0; i < n; ++i) {
            CSteamID id = SteamFriends()->GetFriendByIndex(i, k_EFriendFlagImmediate);
            FriendGameInfo_t game{};
            bool inGame = SteamFriends()->GetFriendGamePlayed(id, &game) && game.m_gameID.AppID() == me;
            int st = int(SteamFriends()->GetFriendPersonaState(id));
            json::Value f = json::Value::object();
            f.set("id", json::Value::string(std::to_string(static_cast<unsigned long long>(id.ConvertToUint64()))))
             .set("name", json::Value::string(cs(SteamFriends()->GetFriendPersonaName(id))))
             .set("state", json::Value::string(st >= 0 && st < 8 ? kStates[st] : "offline"))
             .set("playingThisGame", json::Value::boolean_(inGame))
             .set("status", json::Value::string(inGame ? cs(SteamFriends()->GetFriendRichPresence(id, "status")) : std::string()));
            list.push(std::move(f));
        }
        reply.ok(json::dump(list));
    }

    void overlay(const std::string& page, Reply reply) {
        if (page.rfind("http://", 0) == 0 || page.rfind("https://", 0) == 0) SteamFriends()->ActivateGameOverlayToWebPage(page.c_str());
        else if (page == "store") SteamFriends()->ActivateGameOverlayToStore(SteamUtils()->GetAppID(), k_EOverlayToStoreFlag_None);
        else SteamFriends()->ActivateGameOverlay(page.empty() ? "friends" : page.c_str());
        reply.ok();
    }

    void emitJoin(const std::string& connect, const std::string& who) {
        json::Value e = json::Value::object();
        e.set("connect", json::Value::string(connect)).set("friend", json::Value::string(who));
        aiverse::plugins::emit("steam", "join", json::dump(e));
    }

    STEAM_CALLBACK(Social, onJoin, GameRichPresenceJoinRequested_t) {
        emitJoin(pParam->m_rgchConnect, pParam->m_steamIDFriend.IsValid()
                     ? std::to_string(static_cast<unsigned long long>(pParam->m_steamIDFriend.ConvertToUint64())) : std::string());
    }
    STEAM_CALLBACK(Social, onStored, UserAchievementStored_t) {
        if (pParam->m_nMaxProgress != 0) return;   // a progress popup, not an unlock
        json::Value e = json::Value::object();
        e.set("id", json::Value::string(gameId(pParam->m_rgchAchievementName)));
        aiverse::plugins::emit("steam", "achievement", json::dump(e));
    }
    STEAM_CALLBACK(Social, onPresence, FriendRichPresenceUpdate_t) {
        json::Value e = json::Value::object();
        e.set("friend", json::Value::string(std::to_string(static_cast<unsigned long long>(pParam->m_steamIDFriend.ConvertToUint64()))));
        aiverse::plugins::emit("steam", "presence", json::dump(e));
    }

    json::Value config_;
    std::set<std::string> floatStats_;
    std::string launchConnect_;
    bool dirty_ = false;
    std::chrono::steady_clock::time_point lastStore_{};
};

} // namespace steam_social
