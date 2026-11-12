// Bunker Wars (Mushroom Wars 2 server family) wire formats, byte-compatible with the Java servers
// (BunkerWars-Server) and their C# port (BunkerWars-Server-CS):
//  - matchmaker / battle TCP frames: [u16 BE len][UTF-8 "func" or "func,idx"][u16 BE len][UTF-8 msg]
//  - chat TCP: client [i16 type][i16 len][JSON], server [u16 type][i32 chunk][u16 msgId][u8 more][i32 total][chunk]
//  - battle packets: MessagePack (msgpack-java 0.6, old "raw" spec) NetworkPackage, hex in JSON
//  - battle tickets: v1.<base64url(json{G,U,Exp[,R,N]})>.<base64url(HMAC-SHA256)>
// Host/meta code: no simulation state here.
#pragma once
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "engine/meta/MsgPack.h"

namespace aiverse::meta::bw {

using Bytes = std::vector<uint8_t>;

// ---- matchmaker / battle frames (Java GameMessageDecoder / GameMessageEncoder)
struct Frame {
    std::string func;   // "Match", "Match.onReady", ...
    int idx = 0;        // channel index, written as "func,idx" only when > 0
    std::string msg;    // usually JSON
};
constexpr size_t kMaxFrameString = 32768;
Bytes encodeFrame(const Frame& f);   // empty when func or msg has >= 32768 bytes (the client refuses those too)

class FrameReader {
public:
    void feed(const uint8_t* p, size_t n) { buf_.insert(buf_.end(), p, p + n); }
    void feed(const Bytes& b) { feed(b.data(), b.size()); }
    // Next complete frame. Frames Java dropped ("func," without index) are skipped silently.
    bool next(Frame& out);
    size_t buffered() const { return buf_.size() - pos_; }

private:
    Bytes buf_;
    size_t pos_ = 0;
};

// ---- chat
constexpr uint16_t kChatVersion = 1;   // first client packet: [1][len]{"token": "..."}
enum class ChatType : uint16_t {
    OnMsg = 0, OnMember, OnHistory, GetHistory, OnMemberList, SetState, RegisterOnStateList, OnStateList, GetRoomsList, OnRoomsList,
    OnNotifyBase = 0x10, OnNotifyFriendRequest, OnNotifyFriendAccept, OnNotifyFriendDecline, OnNotifyGameRequest, OnNotifyConsoleAction,
    OnNotifySpectatorInvite, OnNotifyClanInvited, OnNotifyClanAccept, OnNotifyClanDeclined, OnNotifySeasonRewardAvailable,
    DoNotifyRead = 0x20, DoNotifyAccept, DoNotifyDecline, DoNotifyRemove,
    OnQuest = 0x30, OnCustomerSupport, GetCustomerSupportInfo, OnHeroBpTasksProgressChanged,
    ClanUserJoined = 0x40, ClanUserLeft, ClanPointsChanged, ClanDataChanged, ClanUserReceivedNewInvite, ClanUserDeclinedInvite,
    ClanRequestReceived, ClanTaskProgressChanged, ClanNewGiftAvailable, ClanSuperPackAvailable, ClanSuperpackGiftAcquire,
    ClanUserRoleChanged, ClanSeasonEnded, ClanLeadershipTransferred,
    OnVoiceChat = 100, UpdateProfile = 255,
};
Bytes encodeChatAuth(const std::string& token);
Bytes encodeChatPacket(uint16_t type, const std::string& json);   // client -> server
Bytes encodeChatServerPacket(uint16_t type, uint16_t msgId, const std::string& json, size_t maxChunk = 8192); // tests / mocks
struct ChatPacket { uint16_t type = 0; std::string json; };
class ChatReader {   // server -> client, reassembles chunked messages by message id
public:
    void feed(const uint8_t* p, size_t n) { buf_.insert(buf_.end(), p, p + n); }
    void feed(const Bytes& b) { feed(b.data(), b.size()); }
    bool next(ChatPacket& out);
    bool broken() const { return broken_; }

private:
    Bytes buf_;
    size_t pos_ = 0;
    bool broken_ = false;
    std::map<uint16_t, ChatPacket> partial_;
};

// ---- battle packets (com.mw2.server.api.game.rpc.NetworkPackage)
enum class RpcMethod { OnPlayerConnected_RPC, OnPlayerDisconnected_RPC, OnPlayerConnectionPoor_RPC, OnPlayerConnectionGood_RPC, OnPlayerDefeatTimeout_RPC };
const char* rpcMethodName(RpcMethod m);
struct RpcHeader { int64_t tick = 0; int playerIndex = 0, playerOrder = 0; };
// NetworkPackage = [header [tick, playerIndex, playerOrder], networkGroup, networkId, methodName, parameters]
bool readPackageHeader(const Bytes& packet, RpcHeader& header, std::string* method = nullptr, std::string* error = nullptr);
Bytes packageForPlayer(RpcMethod method, int playerIndex, int64_t tick);   // server-injected event, as MpUtil.pack

// ---- battle tickets (BunkerWars-Server-CS BattleTicketCodec)
struct BattleTicket {
    int64_t gameId = 0, userId = 0;
    int64_t expiresAt = 0;   // unix seconds
    int role = 0;            // 0 player, 1 spectator
    std::string name;        // spectators only
};
std::string issueTicket(const BattleTicket& t, const std::string& secret);
// nullopt: malformed, bad signature or expired at nowSec. The secret must be >= 32 bytes.
std::optional<BattleTicket> validateTicket(const std::string& ticket, const std::string& secret, int64_t nowSec);
inline bool looksLikeTicket(const std::string& s) { return s.rfind("v1.", 0) == 0; }

// ---- JSON helpers tolerant of Java/Jackson numbers ("1.0E10", "12.0")
int64_t jsonInt(const Json& j, int64_t def = 0);
inline int64_t jsonInt(const Json& o, const char* key, int64_t def = 0) { return jsonInt(o[key], def); }

} // namespace aiverse::meta::bw
