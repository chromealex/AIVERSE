// Commands, events and the ECS components of the game simulation (units, orders, combat, production, abilities,
// statuses, needs, lanes ...). Rules data: GameData.h. The simulation that runs on them: GameCore.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/Archive.h"
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "GameData.h"
#include "engine/mechanics/AttackTiming.h"

namespace aiverse::game {

// ---------------------------------------------------------------- commands
enum CommandType : uint16_t {
    kCmdMove = 1,     // units, point
    kCmdAttack,       // units, target entity OR point (attack-move)
    kCmdStop,
    kCmdHold,
    kCmdPatrol,       // units, point
    kCmdGather,       // units, target node / dropoff, or point on a resource field
    kCmdBuild,        // units (builders), param = type, point = centre
    kCmdTrain,        // units (producers), param = type
    kCmdCancelTrain,  // units (producers or constructions), param = queue slot (-1 = last)
    kCmdRally,        // units (producers), point or target
    kCmdSmart,        // right click
    kCmdSend,         // garrison structures, target structure, param = percent (< 0: exactly -param units)
    kCmdResearch,     // units (researchers), param = tech
    kCmdPlace,        // producer with a finished structure (sidebar), point
    kCmdSell,         // structures
    kCmdDeploy,       // units with deploysTo
    kCmdAbility,      // units, param = ability index, target or point
    kCmdReinforce,    // squads
    kCmdClaim,        // point inside a region (Northgard)
    kCmdAutoBuild,    // no units; param = type or -1 (random from rules.autoBuild pool); spot chosen by the sim
    kCmdUpgrade,      // structures, param = target type from `upgrades`
    kCmdSurrender,    // no units: the issuing player is defeated (kEvPlayerDefeated with value kDefeatSurrendered)
    // dungeon management / territory (rect commands: point = any point in the first cell, param = rectParam(w, h, extra))
    kCmdDig,          // no units; mark (extra 0) / unmark (extra 1) diggable cells in a rect
    kCmdPaintRoom,    // no units; extra = room index (-1 = erase, refunds rules.sellRefund)
    kCmdPickUp,       // units: own units into the player's hand
    kCmdDrop,         // point: drop units from the hand (units = which ones, empty = the last picked; param = count, <=0: 1)
    kCmdSlap,         // units: own units
    kCmdPossess,      // units[0] = unit to possess; empty units = release
    kCmdDirect,       // possessed unit: point = direction (x, z), param bit 0 = fire, target = optional attack target
    kCmdPlayerAbility,// no units; param = index into rules.playerAbilities, point
    kCmdBuildRoad,    // no units; rect (extra 0 build, 1 remove), cost per cell from rules.roadNetwork
    kCmdConnectRoad,  // units: own structures; the sim lays the shortest road from each to the own road network
    kCmdTransit,      // planets: orbit units, param = target body index; timed transfer to that body's orbit shell
    // agent-mech (RtsMech*.cpp)
    kCmdStance,       // units; param = StanceMode (0 aggressive, 1 defensive, 2 hold, 3 passive)
    kCmdRetreat,      // units: run to the nearest own retreatPoint (rules "retreat")
    kCmdLoad,         // units = passengers, target = transport (or units = transports, target = a passenger)
    kCmdUnload,       // units = transports; point: move there first (param 1), else unload where they stand
    kCmdGuard,        // units follow `target` and fight what attacks near it
    kCmdFollow,       // units follow `target`
    kCmdFormation,    // units; param = shape (0 line, 1 box, 2 wedge, 3 loose, -1 break up)
    kCmdBuildLine,    // builders; point = start, param = type | end x << 16 | end z << 32 (cells): walls in a line
    kCmdMerge,        // units: pair up into their mergeInto result (archons)
    kCmdSplit,        // units with splitInto
    kCmdTrade,        // units[0] = own market; param = resource | (1 << 8 when selling)
    kCmdTradeRoute,   // traders; target = destination market (own or allied), home = nearest own market
    kCmdTribute,      // param = resource | to player << 8 | amount << 16
    kCmdDiplomacy,    // param = other player | 1 << 8 ally (0: enemy)
    kCmdBuyItem,      // units[0] = buyer (inventory), target = shop, param = item index
    kCmdUseItem,      // units[0]; param = slot; target / point for the effect (self by default)
    kCmdDropItem,     // units[0]; param = slot
    kCmdPickItem,     // units[0]; point = where the ground item lies
    kCmdLearn,        // heroes (rules heroes.skills): param = ability index to learn / raise with a skill point
    kCmdBounty,       // point or target; param = resource | reward << 8 (autonomous heroes go for it)
    kCmdRotate,       // conveyor pieces: param 1..4 = east, south, west, north; 0 = turn clockwise
    kCmdLock,         // gates / doors: param 1 lock (closed for everyone), 2 unlock, 0 toggle
    kCmdEndTurn,      // turn-based mode: the active player ends the turn
    kCmdScript,       // rules "commands" (Script.h): param = command index, args = encoded arguments, units = selection
    kCmdRaid,         // extraction raids (Raid.h): param = RaidOp | point index << 8 (call: -1 = nearest)
    // in-match progression (rules "progression", Progression.h)
    kCmdProgression,  // no units; param = ProgressionOp | card index << 8 | offer revision << 16 (choose / reroll / close / open)
    kCmdTopDownEnter,   // no units (rules "driving", Driving.h): the hero gets into the nearest free vehicle / out of his car
    kCmdCommerce,       // no units (rules "commerce", Commerce.h): param = commercePack(op, premises, value)
    kCmdTopDownWeapon,  // no units (rules "arsenal", Arsenal.h): param kWeaponNext / kWeaponPrev, or kWeaponSlot + slot
    kCmdCareer,         // no units (rules "careers", Career.h): param = careerPack(op, career); open careers only
    kCmdPrompt,         // no units (rules "prompts", Prompts.h): param = option picked (0-based), -1 = the interact key
    // field operations (FieldOps.h)
    kCmdRepair,         // builders; target = damaged own / allied unit (rules "repair" units: mobile ones too)
    kCmdReclaim,        // builders; target = resource node (gather) or a unit to take apart (rules "reclaim")
    kCmdSelfDestruct,   // units: arm the countdown (rules "selfDestruct"); again: disarm
    // top-down heroes (rules "topDown", TopDown.h). Engine-reserved hero ids (engine/net/HeroInput.h: net::kCmdHeroInput / kCmdHeroAbility), so hosted battles,
    // the battle load test and every top-down game share one command scheme.
    kCmdTopDown = 0xF100, // no units; param = topdown::pack(Input): sticky move vector / aim / fire of the player's hero
    kCmdTopDownAbility,   // no units; param = topdown::abilityParam(Ability) (old clients: the ability index = tap);
                          // target / point optional (default: along the button's stick, else the aim); index 255 = escape
};

// Rect parameter of kCmdDig / kCmdPaintRoom: width and height in cells (1..64) and a signed 24-bit extra value.
inline int64_t rectParam(int w, int h, int extra = 0) {
    return (int64_t(h & 0xFFFF) << 40) | (int64_t(w & 0xFFFF) << 24) | int64_t(extra & 0xFFFFFF);
}
inline void unpackRect(int64_t p, int& w, int& h, int& extra) {
    w = int((p >> 24) & 0xFFFF); h = int((p >> 40) & 0xFFFF);
    extra = int(p & 0xFFFFFF);
    if (extra & 0x800000) extra -= 0x1000000;
}

// ---------------------------------------------------------------- events (presentation)
enum DefeatReason : int64_t { kDefeatEliminated = 0, kDefeatSurrendered = 1, kDefeatMode = 2 /*match modes (Modes.cpp)*/ }; // kEvPlayerDefeated value

enum GameEvent : uint16_t {
    kEvSpawned = 1, kEvDied, kEvAttack, kEvHit, kEvTrainStarted, kEvTrainDone, kEvBuildPlaced, kEvBuildDone,
    kEvGathered, kEvCaptured, kEvPlayerDefeated, kEvNotEnough, kEvResearched, kEvLevelUp, kEvAbility, kEvMiss,
    kEvReadyToPlace, kEvSold, kEvRegionClaimed, kEvWave, kEvLowPower, kEvUpgraded,
    kEvMessage,       // trigger message: value = index into GameState::trig.messages, player 0 = everyone
    kEvDug,           // value = cell index
    kEvTerritory,     // a cell changed owner: value = cell index, player = new owner
    kEvRoomPainted,   // value = room type (-1 erased), player
    kEvRecruited,     // a = creature, b = portal
    kEvCreatureLeft,  // unhappy creature left the dungeon
    kEvPickedUp, kEvDropped, kEvSlapped,
    kEvPossessed,     // a = unit (kNullEntity: released)
    kEvPayday,        // value = 1 paid, 0 unpaid
    kEvPlayerAbility, // value = ability index
    kEvProduced,      // a = workplace, value = resource of the first output
    kEvDelivered,     // a = carrier, b = destination, value = resource
    kEvRoadBuilt,     // value = cell index (removed: player 0)
    kEvLanesChanged,  // lane geometry regenerated (GameState::lanes)
    // abilities and effects (statuses: value = status index in Rules::statuses)
    kEvStatusStart,   // a = holder (kNullEntity: player status, player = holder slot), b = source
    kEvStatusEnd,     // a = holder; value = status
    kEvAreaStart,     // a = area entity (Transform + Area), value = payload
    kEvAreaTrigger,   // a = area, pos
    kEvAreaEnd,       // a = area
    kEvProjectileLaunch, // a = projectile, b = caster
    kEvImpact,        // payload delivered: a = target (may be null), b = caster, value = payload, pos
    kEvChainJump,     // a = from, b = to
    kEvCastStart,     // windup started: a = caster, value = ability index
    kEvChannelStart, kEvChannelEnd, // a = caster
    kEvAlert,         // player = alerted player, a = first unit of the group, b = target structure
    kEvGarrisonChanged, // a = structure, value = units delta (effects)
    kEvTransitStart,  // planets: a = unit, value = target body, pos = departure point
    kEvTransitEnd,    // planets: a = unit, value = body arrived at, pos = arrival point
    kEvTriggerFired,    // tools/debug: value = trigger index (Rules triggers first, then map), player = triggering slot, a = unit
    kEvLoaded,          // agent-mech: a = passenger, b = transport, value = passengers aboard
    kEvUnloaded,        // a = passenger, b = transport, value = passengers left
    kEvCyclePhase,      // agent-mech: value = cycle index << 8 | phase index (day/night, seasons)
    kEvTraded,          // player, value = resource | 1 << 8 when sold (market); trader income: a = trader
    kEvDiplomacy,       // player = who, value = other | 1 << 8 when allied
    kEvItem,            // a = unit, value = item index; b: shop (bought) / null (used, picked, dropped)
    kEvMerged,          // a = result
    kEvObjective,       // campaigns: value = objective index | state << 16 (0 added, 1 done, 2 failed), player
    kEvDialog,          // value = index into MechState::dialogs, a = speaking unit (if any), player 0 = everyone
    kEvCamera,          // pos = where to look, value = duration in ms | 1 << 31 when the camera is locked, player
    kEvMissionEnd,      // value 1 victory / 0 defeat, player
    kEvLearned,       // a = hero, value = ability index | level << 8
    kEvCrewKilled,    // a = vehicle (now empty), b = shooter
    kEvCrewed,        // a = vehicle, player = new owner
    kEvInfected,      // a = victim, value = spawned type
    kEvSubmerged,     // a = eater (sandworm) leaving; kEvSpawned marks its return
    kEvTrapFired,     // a = trap, b = victim, pos = victim
    kEvGroupTeleport, // a = unit moved, b = caster (may be null), pos = arrival, value = 1 when it is a return trip;
                      // top-down escape: value 2 = departure (pos = where it left), then value 0 at the arrival
    kEvPaved,         // a = slab (removed), value = cells paved, pos = slab centre
    kEvMorale,          // a = unit, value = 0 steady / 1 wavering / 2 routing (stage 4)
    kEvTurn,            // turn-based mode: player = whose turn starts, value = turn number
    kEvFortified,       // dungeon walls: value = cell, player = new wall owner (0: the wall fell back to plain rock), a = worker
    // extraction raids (Raid.h, docs/raid.md)
    kEvRaidPhase,       // value = RaidPhase (1 running, 2 over)
    kEvRaidPoint,       // extraction point phase: value = RaidPointPhase | point << 8, pos = centre, player = caller
    kEvRaidChannel,     // player, value = point | RaidChannelEv << 16 (0 start, 1 interrupted), pos = centre
    kEvRaidExtracted,   // player, a = raider, value = point (-1: forced)
    kEvRaidLoot,        // player, value = item | count << 16 (| 1 << 40: resource index instead of an item), pos
    kEvRaidInteract,    // player, a = raider, b = object, value = object type | RaidChannelEv << 16 (0 start, 1 interrupted, 2 done)
    kEvRaidObjective,   // player, value = objective | 1 << 16 (completed)
    kEvRaidPlayer,      // player, value = RaidStatus (1 in raid, 2 extracted, 3 dead, 4 timed out, 5 abandoned)
    // in-match progression (Progression.h, docs/progression.md)
    kEvProgXp,          // player, a = hero, value = xp gained (after the pickupXp stat), pos = hero
    kEvProgLevel,       // player: a level is banked; value = levels banked | level reached << 32
    kEvProgOffer,       // player, value = offer revision | cards << 32 | ProgOfferEv << 40 (0 opened, 1 rerolled, 2 closed, 3 empty)
    kEvProgPerk,        // player, a = unit of the slot (hero for player perks), value = perk id | (slot + 1) << 32 (0: player scope) | 1 << 48 when it replaced a lower tier
    kEvProgRecruit,     // player, a = new ally, value = deck slot, pos
    kEvProgOrb,         // player = collector (0: none), value = xp | ProgOrbEv << 40 (0 spawned, 1 collected, 2 expired, 3 merged), pos
    kEvProgSummon,      // player, a = ally (confirmed / died / expired), value = point id | SummonEventKind << 32, pos = circle centre
    // boss encounters (rules "encounters", Encounters.h, docs/encounters.md)
    kEvBossEngaged,     // a = boss, b = its target, value = boss type, player = boss owner
    kEvBossPhase,       // a = boss, b = target, value = phase index (0 = first)
    kEvBossEnraged,     // a = boss, b = target, value = boss type
    kEvBossAdds,        // a = boss, value = unit type | count << 16, pos = boss
    kEvBossDefeated,    // a = boss, value = boss type, player = owner slot of the last damage source
    kEvBossWindow,      // schedule: value = window index | state << 16 (0 opened, 1 boss spawned, 2 expired), player = participant, a = boss
    kEvTelegraphStart,  // a = caster, b = aimed unit, value = attack index, pos = area origin, player = caster owner
    kEvTelegraphStrike, // a = caster, b = unit struck, value = attack index | damage (whole units) << 16, pos = unit struck
    kEvTelegraphEnd,    // a = caster, value = attack index | 1 << 16 when cancelled (caster died in the windup)
    kEvEnv,             // map environment (Environment.h): value = EnvEventKind << 16 | data, a / pos per kind (weather, lightning, wind)
    kEvModePhase,       // match modes (Modes.cpp): value = ModePhaseEvent, player = side concerned (0: everyone), a = point
    kEvShotEnd,         // a free-fire shot (rules "arsenal") stopped at a wall / its range, or went off (splash): a = shooter, value = weapon index of its type, pos
    kEvAssault,         // a unit assaulted a defended garrison: player = attacker, a = structure, b = the unit, value = defenders lost
    kEvNative,          // emitted by native modules for the view / scripts: value, a, b, pos are the module's (docs/native-modules.md)
    kEvFieldOp,         // field operations (FieldOps.h): value = FieldOpEvent | data << 8, a = unit, b = projectile (intercepted)
    kEvAttackWindup,    // attack clip starts; a = attacker, b = target, value = weapon index
    kEvAttackCancelled, // windup interrupted without consuming cooldown
    kEvAttackRecoveryEnd, // backswing completed; value = weapon index
};
// kEvModePhase values
enum ModePhaseEvent : int64_t { kModeSdCounter = 1, kModeSdPreview, kModeSdStart, kModeSdEnd, kModeTimeStep, kModeTimeUp,
                                kModeKohAlarm, kModeDomHeld, kModeZoneLost, kModeZoneChanged };

// ---------------------------------------------------------------- components
enum UnitFlags : uint16_t { kUnitHold = 1, kUnitUnderConstruction = 2, kUnitDisabled = 4 /*power deficit*/, kUnitSilent = 8 /*removed without death effects*/,
                          kUnitInHand = 16 /*picked up: off the map*/, kUnitPossessed = 32 /*directly controlled*/,
                          kUnitInTransit = 64 /*between bodies (set together with kUnitInHand: off every surface)*/,
                          kUnitInCargo = 128 /*inside a transport: off the map (agent-mech)*/,
                          kUnitInfected = 256 /*killed by an infecting unit: rises as the infect type (originals)*/,
                          kUnitFollower = 512 /*top-down squad member: follows its hero, not RTS orders / AI (TopDown.cpp)*/ };
constexpr uint16_t kUnitOffMap = kUnitInHand | kUnitInCargo;
// Mask for clearing unit flags. Clearing used to go through uint8_t masks, which also dropped kUnitInfected; that
// stays exactly as it was (checksums), while kUnitFollower (and later high flags) survive.
constexpr uint16_t kKeepFlags(int notMask) { return uint16_t(notMask & 0xFF) | uint16_t(kUnitFollower); }

struct Unit {
    uint16_t type = 0;
    Fixed hp{};
    uint16_t flags = 0;         // UnitFlags (hashed as u32)
    uint8_t lastAttacker = 0;   // owner slot of the last damage source (xp / bounty)
    Vec3 facing{Fixed::fromInt(1), Fixed{}, Fixed{}};
    Fixed energy{};
    Entity lastHitBy = kNullEntity;   // last damage source entity (unit_died.killer; not hashed)
};

enum class OrderType : uint8_t { Move, AttackMove, Attack, Patrol, Gather, Return, Build, Repair, Enter, Cast,
                                 Dig,        // point2 = cell to dig, point = cell to stand on
                                 ClaimCell,  // point = cell to claim (claimers)
                                 Need,       // param = NeedKind | room << 8, point = room cell (or target = structure)
                                 Haul,       // carrier job, see Carrier
                                 Board,      // agent-mech: walk to the transport `target` and get in
                                 Unload,     // agent-mech (transports): go to `point` (state 0) then unload
                                 Follow,     // agent-mech: stay near `target`; param 1 = guard it
                                 Rearm,      // go to the dock `target` and reload ammo
                                 Trade,      // trader loop between its markets (Trader component)
                                 PickItem,   // walk to `point` and pick up the ground item there
                                 Merge,      // walk to `target` and merge with it
                                 Reclaim };  // take the unit `target` apart for its cost (FieldOps.cpp)

struct Order {
    OrderType type = OrderType::Move;
    Entity target = kNullEntity;
    Vec3 point{};
    Vec3 point2{};
    int32_t param = 0;
    uint8_t state = 0;
    Fixed timer{};
    bool flow = false;           // move along a shared flow field (rules movement.flowField)
};

struct Mobile {
    std::vector<Vec3> path;
    uint16_t pathPos = 0;
    Vec3 goal{};
    bool moving = false;
    Fixed goalRadius{};
    Fixed stuck{};
    std::vector<uint16_t> roadPath;
    Vec3 vel{};                  // last velocity (local avoidance)
    Vec3 yieldHome{};            // stepped aside for a passing unit; walks back here
    uint32_t yieldUntil = 0;
    bool yielded = false;
    // flow field following (FlowField.cpp): the field is keyed by (goal cell, radius, path layer, clearance)
    bool flow = false;
    uint8_t flowRadius = 0;
    uint8_t flowStuck = 0;       // stuck events while on the field: re-sample first, A* after repeated stalls
    int32_t flowGoalCell = -1;   // goal cell of the field this unit follows / followed
    Fixed flowArea{};            // goal area radius of the order (contagion reaches out to twice that)
    uint16_t flowNear = 0;       // ticks spent near the settled crowd without settling (lets it settle farther out)
    int32_t flowArrived = -1;    // goal cell this unit arrived at (stopped there): followers of the same goal that
                                 // touch it arrive too ("arrival contagion"), so a crowd settles instead of jamming
};

struct Orders { std::vector<Order> queue; };

struct Combat {
    Fixed cooldown[4] = {};
    Entity target = kNullEntity;
    uint8_t acquireTimer = 0;
    bool autoTarget = false;
    Entity chargeTarget = kNullEntity;   // charging shots: aiming at this target since chargeStart
    uint32_t chargeStart = 0;
    // melee surround: the attack slot (ring around the target) this unit is heading for
    Entity slotTarget = kNullEntity;
    int8_t slot = -1;
    uint32_t slotTick = 0;
    Vec3 slotPos{};
    lib::mechanics::AttackTimingState attackTiming[4]{};
};

struct Cargo { int16_t res = -1; Fixed amount{}; Entity lastNode = kNullEntity; Vec3 lastPos{}; bool field = false; };

struct ProdItem { int32_t item = 0; Fixed progress{}; Fixed paid{}; bool upgrade = false; Fixed timeMult = Fixed::fromInt(1); }; // item >= 0: type; < 0: tech ~item; timeMult: hero revive

struct Production {
    std::vector<ProdItem> queue;
    Vec3 rally{};
    bool hasRally = false;
    Entity rallyTarget = kNullEntity;
    uint16_t ready = kNoType;    // finished structure waiting to be placed (sidebar)
    int charges = 0;             // larva
    Fixed chargeTimer{};
};

struct Construction { Fixed progress{}; Fixed paid{}; Entity builder = kNullEntity; bool selfBuild = false; };
struct ResourceStock { int16_t res = -1; Fixed amount{}; bool infinite = false; };
struct Extractor { uint16_t nodeType = 0; Fixed remaining{}; };
struct Footprint { int x = 0, y = 0, w = 0, h = 0; };
// Map entity components.Transform authored in Studio: absolute height, X/Z tilt
// (degrees, Unity order Z, X, then the unit facing about Y) and per-axis scale.
// Presentation of the placed object; pathing, footprint and colliders keep the
// unit type's ground placement. Facing (Y) lives in Unit::facing. Persisted via
// Simulation::registerPersistentComponent only on maps that use it.
struct AuthoredPose {
    bool hasY = false;
    Fixed y{};
    Fixed rotX{}, rotZ{};
    Vec3 scale{Fixed::fromInt(1), Fixed::fromInt(1), Fixed::fromInt(1)};
    // Hidden friend: found by ADL only, so it never hides aiverse::io for other types in this namespace.
    friend void io(Archive& a, AuthoredPose& x) {
        using aiverse::io;
        io(a, x.hasY); io(a, x.y); io(a, x.rotX); io(a, x.rotZ); io(a, x.scale);
    }
};

struct Garrison {
    int count = 0;
    Fixed growth{};
    Fixed damageAcc{};
    int pendingOut = 0;
    int lost = 0;                    // units lost to assaults and effects (resurrection)
    uint32_t pendingGroup = 0;       // group id of the units being sent
    Entity pendingTarget = kNullEntity;
};

struct Projectile {
    Entity target = kNullEntity, source = kNullEntity;
    Vec3 aim{};
    Fixed speed{}, damage{}, splash{};
    uint16_t damageType = 0;
    uint8_t owner = 0, layers = 0;
    // ability projectiles: deliver a payload on impact (effect = Rules::payloads[effList][effIndex])
    int16_t effList = -1, effIndex = -1, onHit = -1;
    Fixed onHitChance{};
    uint16_t sourceType = 0xFFFF;
    Vec3 start{};
    Fixed travel{}, total{};  // legacy arc progress; fitted flight: peak and duration (zero duration = ballistic continuation)
    Fixed scale = Fixed::fromInt(1);
    ProjectileProgram program;
    int8_t weapon = -1;       // weapon index of sourceType (bonus damage, armor class, crew kills)
};

struct Hero { int level = 1; Fixed xp{}; uint8_t skill[8] = {}; uint8_t points = 0; };   // skill / points: rules heroes.skills
struct Shield { Fixed value{}; uint32_t lastHit = 0; };       // StarCraft shields (health.shields)
struct Eater { int kills = 0; };                              // neutral eats (sandworm)
struct Weathering { Fixed bare{}; };                          // share of the footprint off the owner's foundation (Dune II)
struct TrapState { int charges = 0; uint32_t readyAt = 0; };  // traps (Fidelity.cpp)
struct TeleportBack { Vec3 home{}; uint32_t at = 0; Entity caster = kNullEntity; }; // group teleport with a return trip
// Interplanetary transfer (kCmdTransit): leaves the shell of `from` at depart, arrives on the shell of `to`.
// from/to world positions are the shell points at departure (body positions at that tick), for the view.
struct Transit { int16_t from = 0, to = 0; uint32_t depart = 0, arrive = 0; Vec3 fromWorld{}, toWorld{}; Vec3 arrival{}; };
struct Squad { Fixed reinforceTimer{}; bool reinforcing = false; };

struct Buff { Stat stat = Stat::Damage; Fixed add{}; Fixed mul = Fixed::fromInt(1); uint32_t until = 0; uint16_t sourceType = kNoType; uint16_t stackingGroup = 0; };
struct Buffs { std::vector<Buff> list; uint32_t stunUntil = 0; };

// Grouped modifiers retain distinct strengths until their own expiration. Equal
// modifiers from the same type are interchangeable; refreshing extends their lifetime.
inline void upsertTimedBuff(Buffs& buffs, const Buff& incoming) {
    for (auto& current : buffs.list) {
        if (current.stat != incoming.stat || current.sourceType != incoming.sourceType ||
            current.stackingGroup != incoming.stackingGroup) continue;
        if (!incoming.stackingGroup) { current = incoming; return; }
        if (current.add == incoming.add && current.mul == incoming.mul) {
            if (incoming.until > current.until) current.until = incoming.until;
            return;
        }
    }
    buffs.list.push_back(incoming);
}
struct AbilityState { uint32_t readyAt[8] = {}; uint8_t charges[8] = {}; uint32_t chargeAt[8] = {}; bool init = false; uint32_t busyUntil = 0; };
// A running status effect (unit, structure or player).
struct StatusInst {
    uint16_t def = 0;
    uint8_t owner = 0;              // who applied it
    Entity source = kNullEntity;
    uint16_t sourceType = 0xFFFF;
    uint32_t until = 0, nextTick = 0;
    uint16_t stacks = 1;
    Fixed shield{}, acc{};
    Entity link = kNullEntity;      // Link / Redirect partner
    uint16_t revertType = 0xFFFF;   // Transform / Level: type to go back to
    uint8_t revertOwner = 0;        // Capture: owner to give it back to
    bool revertsOwner = false;
    uint8_t capturedBy = 0;
    int spent = 0;                  // targets used of an effect `budget`; skill objects: commandable / transfer used
    uint32_t group = 0;             // commandable: the group sent from the structure
};
struct Statuses { std::vector<StatusInst> list; uint32_t flags = 0; };   // flags: union of active StatusFlags
// Ground effect: delayed strike, persistent field, moving hazard, mine, portal, reveal, soul trap.
struct Area {
    Entity caster = kNullEntity;
    uint16_t sourceType = 0xFFFF;
    int16_t effList = -1, effIndex = -1;  // the Area effect in Rules::payloads
    uint32_t fireAt = 0, nextAt = 0, endAt = 0;
    Fixed radius{};
    int triggers = 0;
    std::vector<uint64_t> touched;        // contact areas: entities already hit
    Vec3 dir{}, dest{};
    int lane = -1; uint16_t lanePos = 0; bool laneReverse = false;
    Entity follow = kNullEntity;
    Fixed scale = Fixed::fromInt(1);
    Entity goal = kNullEntity;            // skill objects: the structure it heads for (onArrive target)
    bool arrived = false;
    int deaths = 0;                       // onDeath payloads applied (capped by maxTriggers)
    int spent = 0;                        // targets used of an effect `budget`
};
// A unit pulled straight into a structure by a magnet (`pull` effect, SkillObjects.cpp).
// A garrison teleport in progress (BW PointsTeleport): grab from `from`, travel, fill `to`.
struct Transfer {
    Entity from = kNullEntity, to = kNullEntity;
    uint8_t owner = 0;
    Fixed toGrab{}, collected{}, acc{}, grabLeft{}, grabVel{}, wait{}, fillLeft{}, fillVel{};
};
struct Pulled {
    Entity target = kNullEntity;
    Vec3 start{};
    Fixed dist{}, way{}, vel{}, accel{}, minVel{}, maxVel{};
    bool done = false;
};
struct Channel { int16_t payload = -1; Entity target = kNullEntity; Vec3 point{}, startPos{}; uint32_t nextAt = 0, endAt = 0; int ability = 0; Fixed interval{}; };
struct GroupTag { uint32_t id = 0; Entity from = kNullEntity, to = kNullEntity; };
struct Capture { Fixed progress{}; uint8_t team = 0; };
struct Guard { Vec3 home{}; Fixed leash{}; bool returning = false; uint32_t nextWander = 0; };
struct Summon { uint32_t expire = 0; uint16_t summonerType = 0xFFFF; Entity summoner = kNullEntity; };

enum NeedKind : int32_t { kNeedHunger = 0, kNeedSleep = 1, kNeedWork = 2 };
struct Needs {
    Fixed hunger{}, sleep{};          // 0..1+: active at >= 1
    Fixed happiness = Fixed::fromInt(1);
    Fixed payTimer{}, unhappyTime{};
    bool leaving = false;
};
struct Possessed { Vec3 dir{}; Entity target = kNullEntity; bool fire = false; uint32_t lastTick = 0; };
struct PortalState { Fixed timer{}; };
struct AutoTrainState { Fixed timer{}; };
struct ClaimState { Fixed acc{}; };
struct Advancer { uint8_t unused = 0; };     // keeps pushing to the nearest front when idle (autoTrain units)

// Settlers economy
struct Inventory { std::vector<Fixed> have, incoming, reserved; };   // per resource
struct Workshop { Fixed progress{}; bool running = false; };
struct Carrier { Entity from = kNullEntity, to = kNullEntity; int16_t res = -1; Fixed amount{}, loaded{}; };

// Mushroom Wars 2 lanes
struct Lane { Entity a = kNullEntity, b = kNullEntity; int offset = 0; std::vector<Vec3> points; }; // offset: <0 left, >0 right
// Per-entity Lua data (game.data(h)): canonical serialization of the entity's table, written when the
// table changed (flush), hashed through `hash`, saved with the world, gone with the entity.
struct ScriptData {
    std::string blob;
    uint64_t hash = 0;     // FNV-1a of blob (hashState feeds entity + hash)
    uint64_t serial = 0;   // VM write stamp (not saved, not hashed): the VM's cached table is current when equal
};

// Base stat overrides set by game.set_stat_base(h, stat, value): replace the unit type's base value
// before techs / levels / buffs apply (Stats.h). Saved and hashed; only games with scripts read it.
struct ScriptStats {
    struct Entry { uint8_t stat = 0; Fixed base{}; };
    std::vector<Entry> list;
};

struct LaneWalker { int lane = -1; uint16_t pos = 0; bool reverse = false; };

} // namespace aiverse::game
