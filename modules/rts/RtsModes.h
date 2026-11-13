// Match modes (agent-bw-modes): rule-level victory modes and their clocks, ported 1:1 (fixed point) from
// Bunker Wars / Mushroom Wars 2 (ModsModule: Conquest / Domination / KingOfTheHill / TimeMod, BattleRoyalModule,
// SuddenDeathModule, CustomRules time slots, MoraleInfo levels, PlayerStatistic counters) and expressed as
// generic data: rules "modes" (map "modes" overrides, rules "matchModes" presets picked by map "matchMode"),
// rules "timedRules", resource "levels", rules "matchStats". See docs/game-format.md § Match modes.
// Data structs live in Rules (RtsData.h), the per-match state in GameState::modes (RtsModule.h).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/Fixed.h"
#include "../../engine/core/ECS.h"

namespace aiverse::game {

// a rectangle on the map (cells): [x, x + w) x [z, z + h)
struct ModeRect { Fixed x{}, z{}, w{}, h{}; };

// one element of a battle-royal zone chain: stays `stay` seconds, then morphs into `next` over `move` seconds
struct ZoneStep { ModeRect rect; Fixed stay{}, move{}; int next = -1; };

// one time slot of a timed rule (CustomRulesInfo.TimeSlots.Item): any valid slot enables the rule
struct TimeSlot {
    bool checkTime = false, inverse = false;
    Fixed from{}, to{};                       // seconds of real match time
    bool iterate = false;
    Fixed every{}, duration{};                // valid while t % every <= duration (t > 0)
    bool levelBehind = false; int behind = 0; // only when the player's level is exactly `behind` below the best level
    int everyAbilities = 0;                   // > 0: only while the player's ability uses are a positive multiple of it
};
// a player status switched on for every player while its slots say so (CustomRules -> player statuses)
struct TimedRule { int16_t status = -1; std::vector<TimeSlot> slots; int16_t levelRes = -1; };

// resource "levels" (MoraleInfo): level i holds from `at`; mods apply to the player's objects; decay drains
struct LevelDef {
    Fixed at{};
    std::vector<uint64_t> modTags;            // parallel to mods: 0 = every type
    std::vector<uint8_t> modStat;             // Stat
    std::vector<Fixed> modAdd, modMul;
    Fixed decayEvery{}, decayAmount{}, decayAfter{};
};
struct ResourceLevels { int16_t res = -1; std::vector<LevelDef> levels; };

enum class DominationRule : uint8_t { Auto, HoldAny, HoldAll };
enum class TimeWin : uint8_t { MaxPoints, Compare };

// Battle core (rules "battleCore", RtsCore.cpp): Bunker Wars / Mushroom Wars 2 morale points, energy per lost unit,
// production slowing with the army size, neutral growth, point values per difficulty.
struct CoreRules {
    bool any = false;
    // morale points (Player.AddMoralePoints / SubtractMoralePoints, BaseModule.Catch / AttackResult, TowerModule)
    int16_t moraleRes = -1;
    Fixed unitPoints = Fixed::fromInt(10);      // per unit lost in an assault: defender +, attacker -
    Fixed towerPoints = Fixed::fromInt(10);     // per unit killed by a tower: tower owner + (no cooldown reset), victim -
    Fixed gainMult = Fixed::fromInt(1), lossMult = Fixed::fromInt(1);
    Fixed moraleMax{};                          // 0: the resource storage (capped) or none
    std::vector<Fixed> catchNeutral, catchEnemy, lost, upgrade;   // per type ("moralePoints")
    // energy per own unit lost (Player.OnUnitLost): perUnit x (levels - level index - 1), all reasons but defending
    int16_t energyRes = -1;
    Fixed energyPerUnit{};
    int energyLevels = 0;                       // 0: the number of morale levels
    // production slows with the army (Player.GetIncreaseRate): rate by army / cap share
    bool production = false;
    Fixed limitMult = Fixed::fromInt(1);
    std::vector<std::pair<Fixed, Fixed>> tiers; // (share reached, rate), ascending
    bool neutralGrowth = false;                 // neutral garrisons grow up to their neutral maximum (HouseModule)
};

struct ModesRules {
    bool any = false;            // a "modes" block or a matchModes preset exists: system, state, hash and save run
    bool stats = false;          // match statistics counters (rules "matchStats", on with any mode)
    // which points count: structures with Garrison (true) or every structure that counts for victory
    bool garrisonPoints = true;
    // Domination
    bool domination = false;
    Fixed domHold = Fixed::fromRatio(3, 2);
    DominationRule domRule = DominationRule::Auto;
    uint16_t domType = 0xFFFF;   // structure type a domination point becomes when the mode is on (0xFFFF: keep)
    int domGarrison = -1;        // start garrison of domination points (-1: keep)
    // King of the hill
    bool koh = false;
    Fixed kohScore = Fixed::fromInt(300), kohTeamScore = Fixed::fromInt(300), kohRate = Fixed::fromInt(1);
    Fixed kohAlarm = Fixed::fromInt(100);   // event when a side's score drops below this
    uint16_t kohType = 0xFFFF;
    int kohGarrison = -1;
    // Time limit (TimeMod)
    bool timeLimit = false;
    Fixed timeSeconds = Fixed::fromInt(120);
    TimeWin timeWin = TimeWin::MaxPoints;
    Fixed timeCoef = Fixed::fromInt(1);
    std::vector<std::pair<Fixed, Fixed>> timeSpeed;   // (remaining seconds, speed), in order
    // Sudden death
    bool suddenDeath = false;
    Fixed sdAt{}, sdPreview = Fixed::fromInt(3), sdPreviewSpeed = Fixed::fromRatio(1, 2);
    Fixed sdEnd{};               // > 0: seconds after the preview when the side with most points wins (0: never)
    Fixed sdCounter{};           // seconds before the start / the end when the counter shows (presentation flag)
    int16_t sdStatus = -1;       // player status every player gets when the preview ends (factor: income / cooldowns)
    std::vector<std::vector<TimedRule>> sdRuleSets;   // one is picked at random at match start (custom configs)
    // Battle-royal zones
    bool zones = false;
    std::vector<ZoneStep> zoneStart, zoneSteps;
    uint16_t zoneNeutralType = 0xFFFF;   // a point outside its owner's zone turns neutral (and into this type)
    Fixed zoneSdDelay{};                 // sudden death and its rule sets start this much later on zone maps
    bool conquestTeam = false;           // conquest: a team is out only when none of its players has anything left (BW COOP)
    std::vector<Fixed> playerGain, playerDecay;   // per slot: morale gain / decay-timer multipliers (map players moraleGain / moraleDecay)
    // CustomRules
    std::vector<TimedRule> timedRules;
    // resource levels (morale stars)
    std::vector<ResourceLevels> levels;
};

// ---------------------------------------------------------------- state (GameState::modes)
enum MatchStat : uint8_t {
    kMsKills, kMsLost, kMsProduced, kMsCaptured, kMsCapturedNeutral, kMsStructuresLost, kMsUpgrades, kMsAbilities,
    kMsMaxUnits, kMsMaxPoints, kMsPoints, kMsLevelUps, kMsLevelDowns, kMsMaxLevel, kMsModeScore, kMsCount };

struct ModePoint { Entity e; uint8_t kind = 0; /*1 domination, 2 king of the hill*/ uint8_t lastOwner = 0; Fixed hold{}; };
struct ZoneState { int start = -1, cur = -1; ModeRect rect; bool on = false; };

struct ModesState {
    Fixed realTime{};             // seconds of real (unscaled) match time: zones, sudden death, timed rules
    Fixed speed = Fixed::fromInt(1);   // current game speed (time limit steps x sudden death preview)
    Fixed timeSpeed = Fixed::fromInt(1), sdSpeed = Fixed::fromInt(1);
    Fixed timeLeft{};             // time limit: seconds left (sim time)
    int timeIndex = -1;
    uint8_t sdPhase = 0;          // 0 before, 1 preview, 2 sudden death, 3 decided
    bool sdCounter = false;
    int sdRuleSet = -1;
    std::vector<ModePoint> points;
    std::vector<Fixed> kohScore;  // per team
    std::vector<uint8_t> kohAlarmed;
    std::vector<ZoneState> zone;  // per player slot
    std::vector<uint8_t> ruleOn;  // [rule * players + slot]: timed rule status applied
    std::vector<std::vector<int16_t>> level;   // [levels entry][slot]: current level index
    std::vector<std::vector<Fixed>> decayTimer, lastStock;
    std::vector<std::vector<uint32_t>> lastGain;
    std::vector<std::vector<int64_t>> stats;   // [slot][MatchStat]
    std::vector<std::vector<int32_t>> abilityUses;   // [slot][player ability]
    std::vector<int8_t> outcome;  // per slot: 0 playing, 1 won, 2 lost, 3 draw
    std::vector<int32_t> neutralMax;   // battle core: by entity index, neutral garrison maximum (-1: the type's)
    // agent-bw-ai: per garrison entity index (BW HouseModule.producedUnits / TowerModule.killCount; reset on capture)
    std::vector<int64_t> pointProduced, pointKills;
};

} // namespace aiverse::game
