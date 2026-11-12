// Match tickets: how a player proves to a battle server that the matchmaker put them into a match.
//
// The matchmaker (meta backend) and the battle servers share a 32-byte server key. For every player
// of an allocated match the matchmaker issues a ticket:
//   - a sealed part (XChaCha20-Poly1305 with the server key) that only battle servers can read:
//     player id, slot, match id, compatibility hash, expiry and two fresh session keys;
//   - the same session keys and public fields in the clear for the client (sent to it over the
//     backend's TLS channel, never logged).
// The client presents the sealed part in its connect request and then proves it holds the keys by
// encrypting the handshake (SecureUdp.h). A copied sealed part alone is useless to an attacker.
// Same idea as netcode.io connect tokens; formats are our own.
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include "Conn.h"
#include "Crypto.h"

namespace aiverse::net {

constexpr uint32_t kTicketVersion = 1;

struct TicketClaims {
    uint64_t matchId = 0;
    std::string playerId;         // stable account id from the backend (<= 64 bytes)
    uint8_t slot = 0;             // player slot in the match (1..N)
    uint64_t compatHash = 0;      // compatibilityHash() of the client build + map (see below)
    uint64_t expiresAtMs = 0;     // unix ms; also bounds reconnects with this ticket
    Key c2s{}, s2c{};             // session keys (client->server, server->client)
};

// What the game client receives from matchmaking. encode()/decode() give a printable string that
// can travel inside the backend's JSON/protobuf reply. Treat it as a secret (it holds the keys).
struct ClientTicket {
    std::string server;           // "host:port" of the battle server
    TicketClaims claims;          // public copy (keys included: they are the client's secret)
    Bytes sealed;                 // opaque to the client
    std::string encode() const;
    static std::optional<ClientTicket> decode(const std::string& text);
};

// Unix wall clock in ms (tickets need a clock both sides agree on; steady nowMs() does not).
uint64_t unixMs();

// Issue a ticket. Session keys that are zero in `claims` are generated here.
ClientTicket issueTicket(const Key& serverKey, TicketClaims claims, const std::string& serverAddress);
// Server side: open a sealed ticket. Fails if forged, altered, for another key, or expired at nowUnixMs.
std::optional<TicketClaims> openTicket(const Key& serverKey, const Bytes& sealed, uint64_t nowUnixMs);

// Compatibility key in Drone Fall's format "<appVersion>|protocol:<n>|map:<mapId>": players meet only
// on identical builds/rules. The server rejects tickets whose hash differs from the allocated match.
std::string compatibilityKey(const std::string& appVersion, uint32_t protocol, const std::string& mapId);
uint64_t compatibilityHash(const std::string& key);

} // namespace aiverse::net
