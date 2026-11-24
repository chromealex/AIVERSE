// The match state of the game simulation (a World resource): players' economies, the map grid and its derived
// caches, vision, regions, waves, triggers, lanes, routes, match modes. Components: Components.h; rules: GameData.h.
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "Colliders.h"
#include "Components.h"
#include "GameData.h"
#include "MapGrid.h"
#include "Mech.h"
#include "ModesData.h"
#include "Routes.h"
#include "Stats.h"
#include "Triggers.h"

namespace aiverse::game {

// ---------------------------------------------------------------- per-player state

struct PlayerEconomy {
    std::vector<Fixed> stock, cap, income;
    std::vector<Fixed> capProvided, capUsed;  // Capacity resources
    uint64_t disabledTags = 0;                 // from capacity deficits
    Fixed productionMult = Fixed::fromInt(1);
    Fixed supplyUsed{}, supplyCap{};
    std::vector<int> typeCount;                // completed
    std::vector<int> typeCountAll;             // incl. constructing and queued (unique)
    std::vector<uint8_t> researched, researching;
    std::vector<TypeMods> mods;                // per type
    Fixed gatherMult = Fixed::fromInt(1);      // upkeep
    int regions = 0;
    std::string faction;
    std::vector<uint8_t> typeDisabled;         // per type: switched off by triggers
    std::vector<StatusInst> statuses;          // player statuses (silence, freeze regen, passives, wind)
    uint32_t statusFlags = 0;
    // fidelity mechanics (Originals.cpp)
    std::vector<Fixed> gatherMultRes;          // upkeep per resource (empty: gatherMult for all)
    int heroesMade = 0;                        // heroes trained so far (free first hero)
    struct DeadHero { uint16_t type; int level; Fixed xp; uint8_t skill[8]; };
    std::vector<DeadHero> deadHeroes;          // revive keeps the level and skills
    std::vector<uint8_t> fieldCells;           // derived: cells covered by own power fields (needsField)
    Vec3 point2{};                             // second point of the last pointPair player ability (Fidelity.cpp)
};

struct WaveDef {
    uint32_t tick = 0;
    uint8_t player = 0;
    std::vector<std::pair<uint16_t, int>> spawn;
    Vec3 at{};
    Fixed spread = Fixed::fromInt(3);
    uint32_t every = 0;
    Fixed growth = Fixed::fromInt(1);
    int repeats = 0;
    int done = 0;
    std::vector<Vec3> route;   // agent-mech: waypoints (map areas / points) the wave walks before its objective
};

struct RoomZone { uint16_t type = 0; uint8_t owner = 0; std::vector<int32_t> cells; Vec3 center{}; };
struct PlayerAbilityState {
    uint32_t readyAt = 0; int charges = 0; Fixed rearm{};
    bool owned = true;                 // in the loadout (or stolen)
    uint8_t level = 1;
    uint32_t stolenUntil = 0;          // > 0: a stolen skill, lost at this tick
    uint32_t pendingAt = 0;            // activation delay: fires at this tick
    Vec3 pendingPoint{}; Entity pendingTarget = kNullEntity;
    uint32_t nextAuto = 0;
    int8_t slot = -1;                  // loadout slot (BW skill slot; per-slot silence)
};

// Who / where an effect payload is applied (see Effects.cpp).
struct EffectCtx {
    Entity caster = kNullEntity;
    uint8_t owner = 0;
    uint16_t sourceType = 0xFFFF;
    Entity target = kNullEntity;
    Vec3 point{}, origin{};
    Fixed scale = Fixed::fromInt(1);   // chain / splash falloff
    int depth = 0;
    uint32_t group = 0;
    Entity budgetEnt = kNullEntity;    // the area / status holder whose `budget` counter effects use
    int16_t budgetDef = -1;            // status def (-1: an area)
    Fixed areaRadius{};                // radius of the area the payload runs in
};

struct GameState {
    MechState mech;                           // agent-mech global state (Mech.h)
    bool mechActive = false;                  // agent-mech: a mech component exists (stance given in any game); derived
    std::vector<PlayerEconomy> players;   // index = slot (0 = neutral)
    MapGrid grid;
    RoadGraph roads;
    SpatialHash hash;
    StaticColliders colliders;               // map entities of "collider" types (Colliders.h); derived from the map
    bool roadsOnly = false;
    std::vector<std::vector<uint8_t>> vision; // per player per cell: 0 unexplored, 1 explored, 2 visible
    std::vector<std::vector<int32_t>> visibleCells; // per player: cells at 2 (derived, so fog reset is O(visible))
    uint8_t fogMode = 0;                            // FogMode of this match (rules, map override)
    std::vector<int64_t> visionBase, visionOcc;     // line of sight: ground / occluder heights (1/64 units, derived)
    uint64_t visionToken = 0;                       // vision cache that last updated this state (Vision.cpp; not saved / hashed)
    // regions
    int regionCount = 0;
    std::vector<uint8_t> regionOwner;
    std::vector<std::vector<uint16_t>> regionAdj;
    std::vector<std::string> regionNames;
    std::vector<Vec3> regionCenter;
    // victory
    VictoryRules victory;
    std::vector<Fixed> tickets;          // per team (index = team id)
    std::vector<uint8_t> wavePlayers;    // slots controlled by scripted waves (excluded from defeat)
    std::vector<WaveDef> waves;
    bool fieldsDirty = true;             // presentation: resource fields changed
    // lane battler
    uint32_t nextWaveTick = 0;
    std::vector<Fixed> score;            // per team
    std::vector<int> autoBuilt;          // per player
    int winnerTeam = -1;                 // set by timed modes / triggers
    TriggerState trig;
    // dungeon / territory
    std::vector<int> territoryCells;     // per player slot: owned cells (HUD share = territoryCells[p] / territoryTotal)
    int territoryTotal = 0;              // claimable cells (ground or diggable)
    bool territoryDirty = true;          // presentation: territory / dig / room layers changed
    std::vector<RoomZone> roomZones;     // connected painted areas
    std::vector<std::vector<int>> roomCells; // [player][room type] cells owned
    bool roomsDirty = true;
    std::vector<std::vector<Entity>> hand;   // per player: units picked up (order = pick order)
    std::vector<Entity> possessed;           // per player
    std::vector<std::vector<PlayerAbilityState>> playerAbilities; // [player][ability]
    uint32_t nextGroupId = 1;                // ids of groups of units sent together
    // settlers economy
    std::vector<int32_t> roadComp;           // per cell: road network component (-1 none)
    bool roadsDirty = true;
    // lanes (Mushroom Wars 2): every lane from a to b (offset lanes around the main spline), for the view
    std::vector<Lane> lanes;
    bool lanesDirty = true;
    uint32_t laneSignature = 0;              // garrisons/obstacles the lanes were built for
    std::vector<uint32_t> laneRoundRobin;    // per garrison entity index: next lane slot
    RouteState routes;                       // authored routes, row groups, multiscreen (Routes.cpp)
    // planets (map `world` block): bodies, cube-sphere layout of the grid rows (Surface.h); null on flat maps
    std::shared_ptr<const Surface> surface;
    std::shared_ptr<const Surface> authoredSurface; // immutable map recipe, shared by rollback snapshots
    uint64_t planetTerrainRevision = 0;
    // fidelity mechanics: eaters (sandworms) that submerged and come back later
    struct Resurface { uint16_t type; uint8_t owner; uint32_t at; };
    std::vector<Resurface> resurface;
    ModesState modes;                        // match modes, timed rules, levels, match stats (Modes.cpp)
};

} // namespace aiverse::game
