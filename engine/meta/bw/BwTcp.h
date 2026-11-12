// TCP side of the Bunker Wars client (TcpConnector / MultiplayerManager / NetworkManager), poll
// driven on the engine's non-blocking sockets (no threads). Plain TCP like the Unity client: it has
// no TLS on these connections (battlePortSsl is never used by it). Native builds only: browsers
// cannot open raw TCP (see docs/meta-bunkerwars.md, "Web").
#pragma once
#ifndef __EMSCRIPTEN__
#include <memory>
#include <string>
#include <vector>

#include "BwMatch.h"
#include "engine/net/Socket.h"

namespace aiverse::meta::bw {

struct HostPort {
    std::string host;
    int port = 0;
};
// "host:port" (HostsTO strings, ApiClient::endpoints().game/chat); entries without a port are skipped.
std::vector<HostPort> parseHostPorts(const std::vector<std::string>& hosts);

class TcpLink {
public:
    enum class State { Idle, Connecting, Connected, Closed };
    bool connect(const HostPort& to, uint32_t timeoutMs, uint64_t now);   // false: cannot resolve / open
    void update(uint64_t now);           // connect progress, send queue, receive
    void send(const Bytes& b) { out_.insert(out_.end(), b.begin(), b.end()); }
    Bytes takeReceived() { return std::move(in_); }
    void close();
    State state() const { return state_; }
    const std::string& error() const { return error_; }

private:
    std::unique_ptr<net::TcpSocket> s_;
    State state_ = State::Idle;
    uint64_t deadline_ = 0;
    Bytes out_, in_;
    std::string error_;
};

// NetworkManager.CheckServer TCP part: every host of the group is probed at once with a 1 s
// connect timeout. Game hosts get a "Ping" frame with the meta token (TcpConnector("…", "Ping")
// + MsgConnect) and must answer "onPing"; chat hosts get the chat ping packet (type 2, one zero
// byte) and must answer anything. The first answer within 3 s wins; the others are closed.
class HostProbe {
public:
    enum class Kind { Game, Chat };
    HostProbe(Kind kind, std::vector<HostPort> hosts, std::string token, uint64_t now, uint32_t connectTimeoutMs = 1000,
              uint32_t answerTimeoutMs = 3000);
    void update(uint64_t now);
    bool done() const { return done_; }
    int winner() const { return winner_; }   // index into hosts, -1 = none answered
    const std::vector<HostPort>& hosts() const { return hosts_; }

private:
    struct Try { TcpLink link; FrameReader frames; ChatReader chat; bool sent = false; uint64_t connectedAt = 0; bool dead = false; };
    Kind kind_;
    std::vector<HostPort> hosts_;
    std::string token_;
    std::vector<Try> tries_;
    uint32_t answerMs_;
    bool done_ = false;
    int winner_ = -1;
};

// Matchmaker connection: MultiplayerManager.Connect/TryReconnect around a MatchSession.
//  - host group: hosts are tried in order, the next one after a failed connect (HostGroup.Request)
//  - on connect: the connect frame with the token (MsgConnect; nothing if the token is empty)
//  - drop: an "Int.onDisconnect" frame reaches the session; with reconnect() armed the connection
//    comes back after reconnectDelayMs (0.5 s like OnReconnect) on the same group and sends
//    Connect + MatchReconnect {roomId, lastTick}
//  - pingIntervalMs > 0: GamePing {"ms","tick"} while connected (MultiplayerManager.Ping)
struct MatchConnectionConfig {
    std::vector<HostPort> hosts;
    std::string token;                 // meta session token (or a battle ticket for a BW battle node)
    uint32_t connectTimeoutMs = 10000; // TcpConnector default
    uint32_t reconnectDelayMs = 500;
    uint32_t pingIntervalMs = 0;
    int maxReconnects = 5;
};
class MatchConnection {
public:
    explicit MatchConnection(MatchConnectionConfig cfg) : cfg_(std::move(cfg)) {}
    MatchSession& session() { return session_; }
    void start(uint64_t now);
    void update(uint64_t now);
    // Arms automatic reconnect + MatchReconnect (roomId from Match.onUpdate/onReady, last applied tick).
    void reconnect(int64_t roomId, int lastTick) { roomId_ = roomId; lastTick_ = lastTick; }
    void setTick(int tick) { tick_ = tick; }
    void stop();
    bool connected() const { return link_.state() == TcpLink::State::Connected; }
    int hostIndex() const { return host_; }
    int reconnects() const { return reconnects_; }

private:
    void open(uint64_t now);
    MatchConnectionConfig cfg_;
    MatchSession session_;
    TcpLink link_;
    int host_ = 0, reconnects_ = 0, tick_ = 0, lastTick_ = 0;
    int64_t roomId_ = 0;
    uint64_t retryAt_ = 0, lastPing_ = 0;
    bool started_ = false, stopped_ = false, everConnected_ = false, wasConnected_ = false;
};

// Chat connection: auth packet with the token, chunked server packets, reconnect after a drop.
class ChatConnection {
public:
    ChatConnection(std::vector<HostPort> hosts, std::string token) : hosts_(std::move(hosts)), token_(std::move(token)) {}
    void start(uint64_t now);
    void update(uint64_t now);
    void send(ChatType type, const std::string& json);
    std::vector<ChatPacket> takePackets() { return std::move(packets_); }
    bool connected() const { return link_.state() == TcpLink::State::Connected; }
    void stop() { link_.close(); stopped_ = true; }

private:
    std::vector<HostPort> hosts_;
    std::string token_;
    TcpLink link_;
    ChatReader reader_;
    std::vector<ChatPacket> packets_;
    int host_ = 0;
    uint64_t retryAt_ = 0;
    bool stopped_ = false, wasConnected_ = false;
};

} // namespace aiverse::meta::bw
#endif
