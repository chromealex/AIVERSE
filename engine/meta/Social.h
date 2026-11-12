#pragma once
#include "Meta.h"
#include "Protocol.h"

namespace aiverse::meta {
enum class ScoreUpdate { Best, Latest, Sum };
enum class RankStyle { Ordinal, Competition, Dense };
struct BoardConfig {
    Json metadata = Json::object();
    bool ascending = false;
    ScoreUpdate update = ScoreUpdate::Best;
    RankStyle rank = RankStyle::Competition;
    Amount minimum = 0, maximum = kMaxAmount;
    size_t maxPlayers = 100000;
    Schedule season;
    static BoardConfig parse(const Json&);
};
struct Score { std::string player; Amount score = 0; size_t rank = 0; };
struct BoardPage { int64_t revision = 0; size_t total = 0; std::vector<Score> entries; };
// One instance per board+scope (global/region/clan). Friends are a query filter.
// Local authoritative implementation, not a network service or anti-cheat mechanism.
class Leaderboard {
public:
    explicit Leaderboard(BoardConfig config);
    Result submit(const std::string& player,Amount score,Time now,const std::string& operation);
    BoardPage page(Time now,size_t offset=0,size_t limit=50,const std::set<std::string>& players={}) const;
    BoardPage around(Time now,const std::string& player,size_t radius=5,const std::set<std::string>& players={}) const;
    Json save() const;
    Result restore(const Json&);
private:
    BoardConfig config_;
    int64_t revision_ = 0;
    std::map<int64_t, std::map<std::string,Amount>> seasons_;
    std::map<std::string,std::string> receipts_;
};
struct ChatConfig {
    Json metadata = Json::object();
    size_t maxBytes = 2048, maxHistory = 200, maxMessages = 100000;
    Time cooldown = 1, retention = 604800;
    std::set<std::string> kinds{"text"};
    static ChatConfig parse(const Json&);
};
struct Message {
    int64_t sequence = 0;
    std::string sender, kind, text;
    Time sent = 0;
    bool deleted = false;
};
using AuthorizeChat = std::function<bool(const std::string& player,const std::string& action)>;
using ModerateChat = std::function<bool(const std::string& kind,const std::string& text)>;
// One instance per channel; authorize is required (read/send/delete/report).
// All authenticated identities/time must come from the host, not request JSON.
class Chat {
public:
    Chat(ChatConfig config, AuthorizeChat authorize, ModerateChat moderate = {});
    Result send(const std::string& player,const std::string& kind,const std::string& text,Time now,const std::string& operation);
    Result remove(const std::string& actor,int64_t sequence);
    Result report(const std::string& actor,int64_t sequence);
    void block(const std::string& viewer,const std::string& sender,bool blocked=true);
    void mute(const std::string& player,Time until); // trusted moderation command only
    Result markRead(const std::string& viewer,int64_t sequence);
    size_t unread(const std::string& viewer,Time now) const;
    std::vector<Message> history(const std::string& viewer,Time now,int64_t before=0,size_t limit=50) const;
    const std::map<int64_t,std::set<std::string>>& reports() const { return reports_; }
    Json save() const;
    Result restore(const Json&);
private:
    bool visible(const std::string& viewer,const Message& message,Time now) const;
    ChatConfig config_;
    AuthorizeChat authorize_;
    ModerateChat moderate_;
    int64_t next_ = 1;
    Time lastTime_ = 0;
    std::vector<Message> messages_;
    std::map<std::string,std::string> receipts_;
    std::map<std::string,Time> lastSend_, muted_;
    std::map<std::string,int64_t> read_;
    std::map<std::string,std::set<std::string>> blocked_;
    std::map<int64_t,std::set<std::string>> reports_;
};
} // namespace aiverse::meta
