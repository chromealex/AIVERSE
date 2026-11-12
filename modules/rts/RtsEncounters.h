// Boss encounters in the RTS simulation: rules "encounters" (docs/encounters.md, docs/game-format.md § Boss encounters).
//
// Glue between the `boss-encounters` library package (libraries/include/aiverselib/genre: Telegraphs, StatusEffects,
// BossEncounter, BossSchedule, MobBehaviour) and real units: the library makes the decisions (which attack, which
// phase, when adds come, where a mob goes, who a telegraph strikes on which tick), this file owns the bodies.
//  - attacks: telegraphed ground areas (circle / cone / line / ring, windup -> active -> recovery). Bosses, mobs, unit
//    abilities ("ability": id) and Lua (encounters.cast) start them; hits deal damage through RtsModule::dealDamage and
//    apply the game's own rules "statuses" (slow / root / stun / burn are ordinary statuses: mods, flags, tick damage).
//  - bosses: unit types with phases (hp / time), patterns by range band, enrage, add waves. A boss unit of a monster
//    player gets a brain; it walks to its target with ordinary move orders and stands still while an attack winds up.
//  - mobs: wander / hunt / flee / return / swarm pressure per unit type; decisions become ordinary RTS orders (attack,
//    attack-move along a shared flow field when far, move), so pathing, crowds and weapons are the engine's.
//  - schedule: encounter windows that spawn a (personal) boss near each participant's hero, rotation, breathers.
// Monster players are the map players with "monsters": true: the computer player leaves them alone (RtsAI.cpp) and
// this file drives their boss / mob units. Telegraph attacks themselves work for every unit.
//
// State: the EncounterStore world resource, present only in games with the block (rollback snapshots copy it, hashState
// hashes it, RtsSave saves it), so other games keep their checksums and saves. Integer / Fixed maths only; every loop
// runs in entity order or sorted vectors; randomness via sim.rng() in that order.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "aiverselib/genre/BossEncounter.h"
#include "aiverselib/genre/BossSchedule.h"
#include "aiverselib/genre/MobBehaviour.h"
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "../../engine/data/Json.h"

namespace aiverse { class Simulation; class Hasher; class Archive; }

namespace aiverse::game {

class RtsModule;
struct Rules;
namespace lg = aiverse::lib::genre;

// What an encounter status looks like (status markers); derived from the rules status: stun / root flags, a speed
// multiplier below 1, positive tick damage.
enum class EncStatusKind : uint8_t { Other = 0, Slow, Root, Stun, Burn };

struct EncStatusHit { int16_t status = -1; Fixed duration{}; };
// One attack. Library TelegraphDef id = index + 1.
struct EncAttack {
    std::string id;
    std::vector<EncStatusHit> statuses;   // rules statuses applied to every unit struck
    uint16_t damageType = 0xFFFF;         // kDamagePure unless "damageType"
    std::string ability;                  // a unit ability with this id starts the attack ("" none)
    Fixed reach{};                        // how close a mob must be to start it (maxRange, else the area's extent)
};
// A boss unit type. Library BossDef id = type + 1 (add-wave units likewise: type + 1).
struct EncBossDef {
    uint16_t type = 0;
    Fixed engage = Fixed::fromInt(12), lose = Fixed::fromInt(24), approach = Fixed::fromInt(2);
    std::vector<int16_t> immune;          // rules statuses this boss ignores from encounter attacks (sorted)
};
struct EncMobDef {
    uint16_t type = 0;
    lg::MobBehaviourConfig cfg;
    int16_t attack = -1;                  // telegraphed attack used in melee reach (-1: weapons only)
    uint32_t think = 4;                   // ticks between decisions (staggered by entity)
    Fixed flowDistance = Fixed::fromInt(10);   // farther than this: attack-move along a flow field (rules movement.flowField)
};
struct EncounterRules {
    bool enabled = false;
    std::vector<EncAttack> attacks;
    lg::TelegraphSet telegraphs;
    std::vector<EncBossDef> bosses;
    lg::BossSet bossSet;
    std::vector<EncMobDef> mobs;
    lg::ScheduleDef schedule;
    Fixed spawnMin = Fixed::fromInt(8), spawnMax = Fixed::fromInt(12);   // scheduled boss: ring around the participant
    std::vector<int16_t> bossOfType, mobOfType;   // per unit type: index into bosses / mobs or -1
    bool speedMods = false;                        // a phase / enrage changes speed (stat hook)
    int findAttack(const std::string& id) const { for (size_t i = 0; i < attacks.size(); ++i) if (attacks[i].id == id) return int(i); return -1; }
};

// ---- state (world resource)
struct EncBossState {
    uint64_t entity = 0;                  // packed handle (sorted)
    lg::BossState brain;
    uint64_t target = 0;                  // packed (0: none)
    uint8_t participant = 0;              // personal boss of this player (schedule); 0: none
    int64_t window = 0;                   // schedule window id (0: placed by the map / spawned otherwise)
    bool engaged = false, defeated = false;
    int64_t damageMilli = 1000, speedMilli = 1000;   // current phase / enrage multipliers
    Vec3 goal{};                          // last move goal given
};
struct EncAdd { uint64_t entity = 0, boss = 0; uint32_t phase = 0, wave = 0; };
struct EncMobState {
    uint64_t entity = 0;                  // packed handle (sorted)
    lg::MobState brain;
    uint64_t priority = 0;                // preferred target (a personal boss's participant hero)
    Vec3 home{}, goal{};
    uint8_t order = 0;                    // 0 none, 1 move, 2 attack-move, 3 attack
    uint32_t readyAt = 0;                 // own telegraph attack cooldown
};
struct EncCast {
    lg::TelegraphInstance t;
    uint64_t caster = 0;
    uint8_t owner = 0;
    uint16_t attack = 0;                  // index into EncounterRules::attacks
    int64_t damageMilli = 1000;
};
struct EncounterStore {
    std::vector<EncBossState> bosses;     // sorted by entity
    std::vector<EncAdd> adds;             // sorted by entity
    std::vector<EncMobState> mobs;        // sorted by entity
    std::vector<EncCast> casts;           // ascending instance id
    int64_t nextCast = 1;
    lg::ScheduleState schedule;
    EncBossState* boss(uint64_t e);
    EncMobState* mob(uint64_t e);
};

class Encounters {
public:
    bool active() const { return R_.enabled; }
    const EncounterRules& rules() const { return R_; }
    // parses rules "encounters" (throws readable errors); reads the map's monster players
    void setup(Simulation& sim, RtsModule& m, const Json& rulesJson, const Json& map);
    void tick(Simulation& sim);                          // the "encounters" system (after projectiles, before death)
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    EncounterStore* store(Simulation& sim) const;
    bool monsterPlayer(uint8_t slot) const;
    // stat hook: phase / enrage speed multiplier of a boss (1 for everything else)
    bool hasSpeed() const { return R_.speedMods; }
    Fixed speedMul(Simulation& sim, Entity e) const;
    // RtsModule::castAbility: a unit ability whose id an attack names starts that attack toward `point`
    void onAbility(Simulation& sim, Entity caster, int index, Entity target, Vec3 point);
    // starts attack `attack` of `caster` toward `aim` (Lua encounters.cast, abilities, bosses, mobs); false when invalid
    bool cast(Simulation& sim, Entity caster, int attack, Vec3 aim, Entity target, int64_t damageMilli = 1000);
    // presentation / Lua
    lg::TelegraphView view(const EncCast& c, uint32_t tick) const;
    EncStatusKind statusKind(int status) const { return status >= 0 && size_t(status) < statusKinds_.size() ? statusKinds_[size_t(status)] : EncStatusKind::Other; }
    bool wavesAllowed(Simulation& sim, uint8_t player) const;   // schedule gate for ordinary waves (Lua)

    static EncounterRules parse(const Json& j, const Rules& rules, uint32_t tickRate);

private:
    void discover(Simulation& sim, EncounterStore& st);
    void tickSchedule(Simulation& sim, EncounterStore& st);
    void tickBoss(Simulation& sim, EncounterStore& st, EncBossState& b);
    void tickMob(Simulation& sim, EncounterStore& st, EncMobState& m, const EncMobDef& d);
    void tickCasts(Simulation& sim, EncounterStore& st);
    void spawnAdds(Simulation& sim, EncounterStore& st, EncBossState& b, const lg::BossEvent& ev);
    Entity anchorOf(Simulation& sim, uint8_t player) const;   // participant's hero / first unit
    Entity nearestHostile(Simulation& sim, Entity self, Fixed range) const;
    bool castBusy(const EncounterStore& st, uint64_t caster, uint32_t tick, bool* locked, uint64_t* target) const;
    void order(Simulation& sim, Entity e, EncMobState& ms, uint8_t kind, Vec3 goal, Entity target, bool flow);
    EncounterRules R_;
    RtsModule* m_ = nullptr;
    std::vector<uint8_t> monsters_;                     // monster player slots (sorted)
    std::vector<EncStatusKind> statusKinds_;            // per rules status
};

} // namespace aiverse::game
