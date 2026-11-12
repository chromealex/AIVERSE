// Bunker Wars / Mushroom Wars 2 computer player, ported as code (rules "ai": {"controller": "bw"}).
// One controller per bot slot, owned by RtsAI (RtsAI::update hands the slot over to it). Like every bot it only
// reads the simulation and submits Commands; all its maths is Fixed so it plays the same on every machine.
// Algorithms: AI6 (ranker + worker, Domination / King of the Hill modes, per-map custom factories), Bishop (task
// queue), Ashley; per-skill casters (AISkill*), debuts (recorded openings) and map bot triggers. Data keys:
// docs/game-format.md § Bunker Wars computer player.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../../engine/core/Random.h"
#include "RtsModule.h"

namespace aiverse { class Archive; class Json; }

namespace aiverse::game {

enum class BwAlgo : uint8_t { None, Ashley, Bishop, AI6 };
enum class BwRole : uint8_t { Other, House, Tower, Smithy };     // BW PointInfo.Type of a garrison structure
enum class BwMode : uint8_t { Standard, Domination, KingOfTheHill };
enum class BwCmp : uint8_t { Less, LessOrEqual, Equal, GreaterOrEqual, Greater, NotEqual };

// Piecewise linear curve (BW AnimationCurve of pointRankFactorAdjustments), keys sorted by time.
struct BwCurve { std::vector<std::pair<Fixed, Fixed>> keys; Fixed eval(Fixed t) const; };

struct BwDefeatCheck {   // AI6DefeatConditions.IDefeatCondition
    enum Kind : uint8_t { Points, Units, Morale, Time } kind = Points;
    BwCmp cmp = BwCmp::Less;
    int delta = 0;       // botPercentDelta / botPointsDelta / seconds
};
struct BwDefeat { Fixed timeToCheck = Fixed::fromInt(10); int chance = 0; std::vector<BwDefeatCheck> checks; };

// Bot casters (AISkillAssistant.SkillType): which heuristic aims a player ability.
enum class BwSkill : uint8_t {
    None, Generic, AverageMorale, BreakAllMagic, Downgrade, FireRing, FogOfWar, Freeze, Hypnosis, IgnoreDefence,
    IllusionWave, IncreaseDefence, IncreaseMorale, IncreaseProductionX2, IncreaseUnitsSpeed, Invisibility, Landing, Lock,
    Plague, Rage, Reverse, Sabotage, Shield, Spy, Teleport, WarChains, GoHome, Victim, Panic, Reaver, Invulnerability,
    Slowdown, BetrayalGarden2, EnhanceWeapon, ChickenCoop, BFG9000, Fortification, Count };
BwSkill bwSkillByName(const std::string& s);
const char* bwSkillName(BwSkill k);

// One difficulty (BW AIComplexityInfo + AI6ComplexityInfo + AI6SharedComplexityInfo + AIBishopComplexityInfo).
struct BwProfile {
    std::string name = "normal";
    int complexity = 2;                       // 1 easy .. 4 expert
    BwAlgo algorithm = BwAlgo::AI6;
    bool useSkills = true, canMoveInBlizzard = false;
    Fixed skillSlotAwait[4] = {Fixed::fromInt(70), Fixed::fromInt(40), Fixed::fromInt(20), Fixed::fromInt(10)};
    // AI6 shared
    Fixed minDelay = Fixed::fromInt(2), maxDelay = Fixed::fromInt(6);
    bool ignoreAverageActionTime = false;
    int attackLimitPerUpdate = 1, defenseLimitPerUpdate = 0;
    bool limitOneUpgrade = true, structuredAttacks = false;
    Fixed attackDistanceSafety = Fixed::fromInt(1);
    Fixed reserveUnitsForCounterAttackFactor{};
    Fixed increaseMoraleCoefficient = Fixed::fromInt(1), decreaseMoraleCooldownCoefficient = Fixed::fromInt(1);
    int minGroupSize = 16;
    // AI6
    std::vector<std::pair<int, Fixed>> attackTimeouts;          // point index -> seconds
    std::vector<std::pair<int, BwCurve>> pointRankFactors;      // point index -> factor over battle time
    Fixed counterAttackScanRadiusFactor = Fixed::fromInt(1), canDefendPointMinTimeFactor = Fixed::fromInt(1);
    Fixed towerRankFactor = Fixed::fromInt(1), smithyRankFactor = Fixed::fromInt(1), attackDistanceFactor = Fixed::fromInt(1);
    int minGroupSizeOverride = 0;
    Fixed upgradeTimeout{};
    bool allowConvert = true;
    std::vector<BwDefeat> defeat;
    // AI6CustomFactory: "map17005" (avoid a periodic freeze wave), "map11001" (test attack, then let the player develop)
    std::string custom;
    Fixed freezeWaveInterval{}; int freezeWaveAbility = -1;
    bool avoidFreezeWaveForAttacks = true, avoidFreezeWaveForDefenses = true;
    Fixed refreshTimeAfterFreezeWave = Fixed::fromInt(2);
    int testAttackTarget = 14; std::vector<int> playerSidePoints{8, 3, 2, 12, 6, 14, 5, 7};
    Fixed playerDevelopTimeout = Fixed::fromInt(30);
    // Bishop
    Fixed analyzePeriod = Fixed::fromRatio(1, 2);
    struct Act { Fixed delay = Fixed::fromRatio(1, 2); Fixed minPercent = Fixed::fromInt(25); int minCount = 5; int maxBuildings = 0; };
    Act attack, defense;
    int additionalAttackCount = 0;
    Fixed increaseUnitsCoefficient = Fixed::fromInt(2);
    int maxAttackTargets = 2;
    Fixed upgradePeriod = Fixed::fromInt(1);
    bool disablePointsConvertation = false;
    Fixed errorPercentPerSecond = Fixed::fromInt(10);
    Fixed firstActionDelay = Fixed::fromRatio(3, 2);
    uint8_t deniedConvertRoles = 0;          // bit (1 << BwRole)
    bool awaitForPlayerAction = false;

    int minGroup() const { return minGroupSizeOverride > 0 ? minGroupSizeOverride : minGroupSize; }
    Fixed attackTimeout(int point) const;
    Fixed rankFactor(int point, Fixed time) const;
};

// Recorded opening (DebutPlayer): commands at fixed ticks while the bot's points look like the recording.
struct BwDebut {
    int complexity = 0, slot = 0;              // 0: any
    BwMode mode = BwMode::Standard; bool anyMode = true;
    struct Act { uint32_t tick = 0; uint8_t kind = 0; int from = -1, to = -1, count = 0; uint16_t type = 0; int ability = -1; Vec3 point{}; };  // kind 0 send, 1 upgrade, 2 skill
    struct State { uint32_t tick = 0; int point = 0; uint16_t type = 0; };   // expected type of an own point
    std::vector<Act> actions;
    std::vector<State> states;
};

// Map bot triggers (Database/AI TriggerInfo): groups of elements, each fires its actions once when its conditions hold.
struct BwTrigger {
    struct Cond { uint8_t kind = 0; std::string name; BwCmp cmp = BwCmp::GreaterOrEqual; Fixed value{}; int point = -1, from = -1, to = -1, minCount = -1; uint8_t who = 0; Fixed progress = Fixed::fromInt(-1); };
    // kind: 0 instant, 1 timer, 2 value, 3 pointCaptured, 4 unitsMove, 5 time. who: 0 any, 1 me, 2 allies (and me), 3 enemies
    struct Action { uint8_t kind = 0; std::string name; Fixed value{}; std::vector<int> a, b; int count = -1, from = -1, to = -1; bool flag = false, flag2 = false; uint8_t mask = 0; };
    // kind: 0 setTimer, 1 setValue, 2 botAttack, 3 botIgnore, 4 botDisable, 5 botReset, 6 botMoveUnits, 7 groupSetState, 8 botEnable(attack/relocation/defense/upgrade)
    struct Element { bool all = true; std::vector<Cond> conds; std::vector<Action> actions; };
    struct Group { std::string name; bool enabled = true; std::vector<Element> elements; };
    std::vector<Group> groups;
};

struct BwAIConfig {
    std::map<std::string, BwProfile> profiles;            // by name (easy, normal, hard, expert + custom)
    std::string defaultProfile = "normal";
    std::map<int, std::string> slotProfile;              // slot -> profile name
    std::vector<int> botSlots;                           // slots played by bots (ActionDetector ignores them)
    BwMode mode = BwMode::Standard; bool modeAuto = true;
    uint64_t roleTags[4] = {};                           // explicit role tags (0: inferred from the type)
    uint64_t dominationTags = 0, kothTags = 0;
    std::vector<std::pair<int, BwSkill>> skills;         // player ability -> caster
    Fixed pxPerCell = Fixed::fromInt(40);                // BW pixel distances (200 px rules) in cells
    std::string blizzardVar = "blizzard", kothScoreVar = "koth_{player}", kothMaxVar = "koth_max";
    std::vector<BwDebut> debuts;
    BwTrigger triggers;
    const BwProfile& profileFor(int slot) const;
};
// rules ai.bw (+ map "ai" block): throws std::runtime_error("ai.bw...: ...") with a readable message.
std::shared_ptr<const BwAIConfig> loadBwAIConfig(const Json& rulesAi, const Json& map, const Rules& R);

// ---- one think's view of the battle (points, players, groups in transit); built from the simulation
struct BwPoint {
    Entity e; uint16_t type = 0; uint8_t owner = 0; BwRole role = BwRole::Other; int level = 1;
    int value = 0, max = 0, pendingOut = 0;
    Vec3 pos{};
    Fixed def = Fixed::fromInt(1), defNext = Fixed::fromInt(1);   // full defense coefficient (now / at next morale level)
    Fixed prod{};                                                // units per second
    bool upgrading = false, dom = false, koth = false, tower = false;
    Fixed towerRange{}, towerCooldown = Fixed::fromInt(1), towerKills = Fixed::fromInt(1);
    int nextLevel = -1; int convertTo[4] = {-1, -1, -1, -1};     // upgrade types (rules type index)
    int upgradeCost = 0; int convertCost[4] = {};
    int produced = 0, kills = 0;                                 // (Downgrade heuristics; not tracked by the engine yet)
};
struct BwPlayer { bool alive = false, neutral = true; uint8_t team = 0; Fixed damage = Fixed::fromInt(1), speed = Fixed::fromInt(1);
                  int units = 0, maxUnits = 0, smithy = 0; Fixed morale{}; int moraleLevel = 0; uint16_t unitType = 0; };
struct BwGroup {
    uint32_t id = 0; uint8_t owner = 0; int from = -1, to = -1;
    int count = 0, pending = 0;                                  // in flight / still inside the source
    Fixed minDist{}, maxDist{}, routeLen{};
    bool fake = false;
    std::vector<Vec3> units;                                     // positions (heatmap)
    std::vector<Entity> ents;
    bool closed() const { return pending == 0; }
    int need() const { return count + pending; }
};
struct BwWorld {
    std::vector<BwPoint> pts;
    std::vector<BwPlayer> players;                               // by slot (0 = neutral)
    std::vector<BwGroup> groups;
    Fixed time{}, releaseRate = Fixed::fromInt(40), mapW = Fixed::fromInt(64), mapH = Fixed::fromInt(64);
    uint32_t tick = 0;
    int pointOf(Entity e) const;
};

// Routes between points: lengths and tower coverage, rebuilt when the lanes change (not saved: derived).
struct BwRoutes {
    uint32_t signature = 0; std::vector<Entity> nodes; bool lanes = false;
    std::vector<Fixed> len;                                     // n*n, < 0: no route
    std::vector<std::vector<Vec3>> poly;                        // n*n (built on demand)
    std::vector<int32_t> cover;                                 // n*n*n coverage of tower k on route i-j (raw/64, -1 unknown)
    std::vector<int> roadsInRange;                              // per tower point, -1 unknown
};

class BwAI {
public:
    BwAI(uint8_t player, uint64_t seed) : player_(player), rng_(seed ^ 0xb3a1u, player) {}
    void update(Simulation& sim, RtsModule& m);
    void stateIo(Archive& a);

    struct Ctx;   // per-update context (RtsBwAIInternal.h)
    // The bot's state below is plain data shared by the RtsBwAI*.cpp files (saved by stateIo).
    uint8_t player_;
    Random rng_;
    bool init_ = false;
    uint32_t lastTick_ = 0;
    // AIBase
    Fixed timeout_{};
    bool disabled_ = false, useAllUnitsForAttack_ = false, attackTesting_ = false;
    bool enableAttack_ = true, enableRelocation_ = true, enableDefense_ = true, enableUpgrade_ = true;
    std::vector<int> objSources_, objTargets_, ignore_;
    int objCount_ = -1;
    bool hasObjSources_ = false, hasObjTargets_ = false, hasIgnore_ = false;
    std::vector<int> reserved_;                                   // AI6MapState.reservedUnits per point
    struct Pending { Entity e; int units = 0; uint32_t until = 0; bool upgrade = false; };
    std::vector<Pending> pending_;                               // own commands not executed yet
    // AI6
    bool attackStrategy_ = false;
    struct Deferred { int batch = 0; Entity from, to; int units = 0; Fixed left{}; bool dead = false; };
    std::vector<Deferred> deferred_;
    std::vector<int> actionCounts_; Fixed actionClock_{}, avgActionTime_{}, lastAvgActionTime_{}; uint64_t cmdCursor_ = 0;
    Fixed defeatClock_{};
    uint8_t customState_ = 0; std::vector<uint8_t> customFlags_;
    // skills
    struct Caster { int ability = -1, slot = 0; BwSkill kind = BwSkill::None; Fixed timer{}, heat{}; Entity a, b; int n = 0; };
    std::vector<Caster> casters_;
    struct MoveTask { Entity from; uint8_t owner = 0; Fixed timer{}, duration{}; Fixed percent{}; };
    std::vector<MoveTask> moveTasks_;
    uint32_t lastGroupId_ = 0;                                     // OnGroupStarted: groups above this id are new
    // AI6MapState (refreshed when the bot thinks; skills read it in between)
    struct PS {
        int total = 0, attackers = 0, defenders = 0, firstEnemy = -1;
        std::vector<int> byPlayer, byTeam;
        Fixed available{}, attackOnWay{}, defenseOnWay{}, attack{}, defense{};
        Fixed minTimeAttack{}, minTimeDefense{}, lastTimeAttack{}, lastTimeDefense{};
        Fixed protection() const { return defense + defenseOnWay; }
        Fixed fullDefense() const { return protection() - attackOnWay; }
    };
    std::vector<PS> ms_;
    int ownPoints_ = 0;
    // debut / triggers
    int debut_ = -1; bool debutPlaying_ = false; uint32_t debutCursor_ = 0;
    std::vector<uint8_t> trigGroupOn_, trigArrived_;
    std::map<std::string, std::pair<Fixed, uint8_t>> trigTimers_; // name -> (left, 1 running / 2 arrived)
    std::map<std::string, Fixed> trigValues_;
    std::vector<uint8_t> lastOwners_, prevOwners_;
    // Ashley
    Fixed ashleyTimer_{};
    std::vector<std::pair<Entity, Fixed>> activeTargets_;          // (target, battle time it frees up)
    std::vector<std::pair<Entity, int>> ashleyNeed_;               // needUnitsToUpgrade
    // Bishop
    Fixed bScan_{}, bAttack_{}, bDefense_{}, bUpgrade_{}, bAwait_ = Fixed::fromInt(-1), bError_{};
    bool bReady_ = false;
    struct TaskSrc { Entity from; int count = 0; Fixed timer{}; };
    struct Task { uint8_t type = 0; Entity target; std::vector<TaskSrc> src; };   // 0 attack, 1 defense
    std::vector<Task> tasks_;
    int bAttacks_ = 0;                                            // myAttacksCount
    struct BPoint {                                               // AIBishop.PointData (scan result, not saved: rescanned)
        int point = -1; Fixed nearest{}, incomeDamage{}, incomeDefense{}, attackValue{}, potByMe{}, potDefense{}, potDamage{}, real{};
        Fixed danger{}, timeToAttack{}, timeToDefence{}, waveDuration{}, myAttack{}, potDamageT{}, potDefenseT{}, minTimeEnemies{}, minTimeAllies{};
        Fixed avgLenEnemies{}; int reserved = 0, incomeDefenseCount = 0;
        Fixed protection{}; Fixed fullDefense() const { return incomeDefense + protection - incomeDamage; }
    };
    std::vector<BPoint> bScanData_;
    std::vector<std::vector<int>> bClusters_;
    std::vector<int> bMine_;
    Fixed bMaxRoute_{};
    // derived (not saved)
    BwRoutes routes_;
    std::shared_ptr<const BwAIConfig> cfg_;
};

} // namespace aiverse::game
