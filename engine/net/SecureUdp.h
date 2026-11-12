// Secure battle transport: encrypted, authenticated UDP for matches over the Internet (mobile PvP/PvE).
//
//   client                                  battle server
//   Request  [ticket sealed part, padded] -> opens the ticket with the server key, checks expiry,
//                                            compatibility and that it hosts the match (admit)
//            <- Challenge [session id, random cookie]   encrypted with the s2c key
//   Response [cookie]                     -> encrypted with the c2s key: proves the client holds the
//                                            ticket's keys and receives at its source address
//            <- Accept, then Data both ways (XChaCha20-Poly1305, 64-bit sequence nonces, replay window)
//
// - NAT friendly: the client always initiates; keepalives every 250 ms keep the mapping open; the
//   session id travels in the clear so an authenticated packet from a new address (NAT rebinding,
//   Wi-Fi -> LTE handover) moves the session there instead of dropping the player.
// - No amplification: the request is padded to 1000 bytes; challenge, pong and rejects are smaller.
// - Reliability: the same ReliableChannel / LatestChannel as plain UDP, with redundancy for small
//   messages so 10-20% loss costs about one tick, not an RTO (SecureLinkConfig::redundancyMs).
// - Unconnected ping/pong for server selection before matchmaking (sendBattlePing / readBattlePong).
// Native builds only (browsers have no UDP: web clients use wss through a proxy; see docs/battle-net.md).
#pragma once
#ifndef __EMSCRIPTEN__
#include <functional>
#include <memory>
#include <vector>
#include "Conn.h"
#include "Datagram.h"
#include "MatchTicket.h"

namespace aiverse::net {

struct SecureLinkConfig {
    uint32_t connectTimeoutMs = 10000;
    uint32_t retryMs = 200;            // handshake resend interval
    uint32_t silenceTimeoutMs = 10000; // no authenticated packet for this long: closed
    uint32_t redundancyMs = 30;        // 0 = plain RTO-based resends
};

struct SecureStats {
    uint64_t handshakes = 0, rejectedTickets = 0, badPackets = 0, replays = 0, migrations = 0, pings = 0;
    uint64_t packetsIn = 0, packetsOut = 0, bytesIn = 0, bytesOut = 0;
};

// Client connection with a ticket from matchmaking. The first form opens a real UDP socket and
// resolves ticket.server ("host:port"); nullptr when that fails.
std::unique_ptr<Conn> connectSecure(const ClientTicket& ticket, SecureLinkConfig cfg = {});
std::unique_ptr<Conn> connectSecure(std::unique_ptr<DatagramPort> port, const Address& server, const ClientTicket& ticket,
                                    SecureLinkConfig cfg = {});

struct SecureServerConfig {
    Key serverKey{};
    // Accept this ticket (the server hosts claims.matchId with that compatibility hash and slot)?
    std::function<bool(const TicketClaims&)> admit;
    std::function<uint64_t()> clock = unixMs;     // ticket expiry clock (tests use a fake one)
    SecureLinkConfig link;
    uint32_t maxSessions = 4096;
};

struct SecureAccepted {
    std::unique_ptr<Conn> conn;
    TicketClaims claims;            // trusted: from the sealed ticket, never from the peer
};

class SecureListener {
public:
    explicit SecureListener(SecureServerConfig cfg);
    ~SecureListener();
    bool open(uint16_t port, bool loopbackOnly = false);   // real UDP socket
    void attach(std::unique_ptr<DatagramPort> port);       // any datagram path (SimNet in tests)
    uint16_t port() const;
    // Pump the socket; returns connections whose handshake completed since the last call. A player
    // who connects again with the same ticket replaces (closes) their previous connection.
    std::vector<SecureAccepted> poll(uint64_t now);
    void setLoad(uint8_t percent) { load_ = percent; }      // reported in pongs (server selection)
    const SecureStats& stats() const;
    size_t sessions() const;

    struct Impl;
private:
    std::unique_ptr<Impl> impl_;
    uint8_t load_ = 0;
};

// Server selection: send an unconnected ping (32 bytes); readBattlePong() parses an answer and
// returns the RTT from the echoed timestamp, the server's load and true. Works on any DatagramPort.
void sendBattlePing(DatagramPort& port, const Address& server, uint64_t nowMs);
bool readBattlePong(const uint8_t* data, size_t n, uint64_t nowMs, uint32_t& rttMs, uint8_t& loadPercent);

} // namespace aiverse::net
#endif
