// Messages of hosted battles (dedicated battle server mode, docs/battle-net.md). Additive to
// Protocol.h: older servers never send MatchPayload and ignore ResultReport / Away, so
// kProtocolVersion stays the same and lobby / RTS matches are unchanged.
#pragma once
#include <string>
#include <vector>
#include "Protocol.h"

namespace aiverse::net {

// One player as the orchestrator allocated them. loadout is game-defined bytes (Drone Fall:
// SpawnWanderData-like units / weapons / quest progress); the engine only carries it.
struct BattlePlayer {
    uint8_t slot = 0, team = 0;
    SlotKind kind = SlotKind::Human;
    std::string name;
    Bytes loadout;
};

// Sent to every player right before MatchStart (and again on a resumed MatchStart): everything a
// client needs to build the battle besides the map. Every client gets the same bytes, so the
// simulation can read loadouts deterministically (feed them through the game's setup, not commands).
struct MatchPayloadMsg {
    uint64_t matchId = 0;
    std::string compatKey;        // MatchTicket.h compatibilityKey()
    std::string mapId;
    Bytes gameData;               // match-wide game-defined bytes (mode, difficulty, modifiers ...)
    std::vector<BattlePlayer> players;
    uint32_t joinTimeoutMs = 0, maxDurationSec = 0;
};

// The game's deterministic outcome blob (per-player results, loot, stats) at a confirmed tick.
// Every client computes the same bytes; the server compares them before reporting to meta.
struct ResultReportMsg { uint32_t tick = 0; Bytes blob; };

constexpr size_t kMaxLoadoutBytes = 16384, kMaxGameDataBytes = 65536, kMaxReportBytes = 65536;

Bytes encode(const MatchPayloadMsg&);   bool decode(ByteReader&, MatchPayloadMsg&);
Bytes encode(const ResultReportMsg&);   bool decode(ByteReader&, ResultReportMsg&);
Bytes encodeAway(bool away);

} // namespace aiverse::net
