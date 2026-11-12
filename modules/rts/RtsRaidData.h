// Extraction raids (rules "raid", map "raid"): the match loop of extraction shooters (Drone Fall, Tarkov-likes).
// Raiders enter the map, open containers and interactables (hold to use), carry loot in their item bags
// (rules "items" + type "inventory", RtsMechData.h), complete objectives and leave through extraction points
// (zones with open / sleep cycles, called helicopters with an arrival phase, channel timers, contest and
// interrupt rules). The match ends when every raider is resolved (extracted, dead, timed out, abandoned)
// or the clock runs out; each player gets a receipt (a deterministic JSON payload with a stable operation id)
// that the meta layer consumes once (engine/meta Economy::transact(receipt.id, ...)).
// Runtime: RtsRaid.{h,cpp} (store, system, bot), Lua `raid` table: RtsRaidApi.cpp. Reference: docs/raid.md.
// A game without a "raid" block loads, plays and hashes exactly as before.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/Fixed.h"
#include "../../engine/data/Json.h"

namespace aiverse::game {

struct Rules;

enum class RaidScope : uint8_t { Player, Point };          // channel progress per raider / one shared fill per point
enum class RaidContest : uint8_t { Block, Ignore, Reset };  // hostile raider inside: pause, nothing, reset progress
enum class RaidLeave : uint8_t { Reset, Pause };            // channelling raider steps out
enum class RaidLoss : uint8_t { Drop, Destroy };            // a dead raider's carried items: on the ground / gone
enum class RaidObjKind : uint8_t { Collect, Reach, Kill, Interact, Script };

struct RaidLootEntry {
    int16_t item = -1;          // rules "items" index, or
    int16_t res = -1;           // a resource: amount goes to the raider's carried resources
    int countMin = 1, countMax = 1;
    Fixed amount{};             // resource amount (per count)
    uint32_t weight = 1;        // 0: never rolled (placeholder); "none": true entries roll nothing
};
struct RaidLootTable {
    std::string id;
    int rollsMin = 1, rollsMax = 1;
    std::vector<RaidLootEntry> entries;
    uint32_t total = 0;         // sum of weights
};

// Hold-to-use object (unit type "raid": {"interact": {...}}): containers (loot), rescue targets, switches.
struct RaidInteractDef {
    bool on = false;
    Fixed hold = Fixed::fromInt(2);      // seconds of standing in range
    Fixed range = Fixed::fromInt(2);     // contact distance
    int16_t loot = -1;                   // loot table rolled on completion (items to the ground next to it)
    std::string objective;               // objective id progressed by one on completion
    std::string fn;                      // Lua global called on completion: fn({player, unit, target, x, z})
    int uses = 1;                        // completions before it is spent (0: unlimited)
    Fixed cooldown{};                    // seconds between uses
    bool consume = true;                 // removed (silently) when spent
    bool single = true;                  // two players in range: nobody progresses (Drone Fall crates)
    bool interruptOnDamage = false;      // the user takes damage: progress resets
    RaidLeave leave = RaidLeave::Reset;
};

struct RaidPointDef {
    std::string id;
    Fixed x{}, z{};
    Fixed radius = Fixed::fromInt(3);
    Fixed channel = Fixed::fromInt(5);   // seconds inside to extract (0: at once)
    Fixed opensAt{};                     // match seconds before it can open
    Fixed open{}, sleep{};               // open / sleep cycle (open 0: stays open once it opened)
    Fixed arrival{};                     // inbound phase before it opens (helicopter flying in)
    Fixed departure = Fixed::fromRatio(5, 2);   // departing phase after a window closes / its last use
    std::vector<std::string> requires_;  // objective ids the raider (or its team) must have completed
    RaidScope scope = RaidScope::Player;
    RaidContest contest = RaidContest::Block;
    RaidLeave leave = RaidLeave::Reset;
    bool interruptOnDamage = true;
    int capacity = 0;                    // extractions per open window (0: unlimited)
    bool call = false;                   // closed until a raider calls it (raid command "call")
    bool follow = false;                 // called: opens around the caller and follows its raider (heli offer)
    int team = 0;                        // >0: only this team may use it
    std::string fn;                      // Lua veto: fn(player, point_id) returning false refuses
};

struct RaidObjectiveDef {
    std::string id, text;
    RaidObjKind kind = RaidObjKind::Script;
    int16_t item = -1;                   // collect: item carried (bag) or secured
    uint16_t type = 0xFFFF;              // kill: victim type (0xFFFF any enemy); interact: the interactable type
    Fixed x{}, z{}, radius = Fixed::fromInt(3);   // reach
    int count = 1;
    bool shared = false;                 // progress is the team's (every teammate completes it)
    bool required = false;               // receipt "completed" needs it (and extraction lists it in requires)
    std::vector<std::pair<std::string, int64_t>> reward;   // receipt reward bundle when completed (meta asset ids)
};

struct RaidRules {
    bool enabled = false;
    bool pvp = false;
    Fixed duration = Fixed::fromInt(600);   // raid seconds (0: no limit)
    Fixed startDelay{};                     // briefing before the clock runs (deploy)
    std::vector<uint16_t> raiderTypes;      // the bodies that count as a player's raider
    uint64_t raiderTags = 0;                //   ... or these tags (neither: units with an inventory)
    RaidLoss loss = RaidLoss::Drop;
    Fixed pickupRadius = Fixed::fromRatio(3, 2);   // raiders pick up ground items this close (0: only kCmdPickItem)
    bool removeAll = true;                  // extraction removes every unit of the player (else only raiders)
    bool endOnFirstExtract = false;
    std::vector<int64_t> itemValue;         // per rules item: receipt value (default: sum of its cost)
    std::vector<std::string> itemAsset;     // per rules item: meta asset id in the reward bundle (default: item id)
    std::vector<RaidLootTable> loot;
    std::vector<RaidInteractDef> interact;  // per unit type (empty when none)
    bool anyInteract = false;
    RaidPointDef pointDefaults;
    std::vector<RaidPointDef> points;       // rules points; the map adds / overrides by id
    std::vector<RaidObjectiveDef> objectives;
    int findLoot(const std::string& id) const;
    int findObjective(const std::string& id) const;
    int findPoint(const std::string& id) const;
};

// Called by loadRules after types / items are parsed.
void loadRaidRules(Rules& r, const Json& j);
// Map "raid" block: duration / startDelay / pvp overrides, points and objectives added or replaced by id.
// Throws readable errors (the editor validates maps with it).
RaidRules raidRulesForMap(const Rules& r, const Json& map);

} // namespace aiverse::game
