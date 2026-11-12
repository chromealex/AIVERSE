// In-match progression at run time (data: RtsProgressionData.h, reference: docs/progression.md).
//
// State: the ProgressionStore world resource (present only in games with a rules "progression" block, so other games
// keep their checksums and saves; rollback snapshots copy it like any other resource). It holds the library states
// (PerkState: decks, xp, levels, offers, perk occurrences, its own PCG32 stream; PickupState: xp orbs; SummonState:
// circles, allies, charges) and the table of real units that belong to a deck slot (entity -> player, slot, bearer).
//
// The "progression" system runs right before "death": units at 0 hp are still there, so kills (killer = the last
// attacker's owner) turn into orbs at the victim's position, and allies that died are reported to the summon policy.
// Perk effects are never written into units: RtsModule::stat / dealDamage / sysWeapons ask modStat, damage and the
// shot hooks, which evaluate the library pipeline (Set -> Add -> Multiply, slot before player scope) on the fly.
// Choices arrive as kCmdProgression commands (player UI, bot, Lua), validated against the offer revision.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "RtsProgressionData.h"
#include "RtsData.h"
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"

namespace aiverse { class Simulation; class Hasher; class Archive; struct Command; }

namespace aiverse::game {

class RtsModule;
struct Projectile;

// kCmdProgression param = op | index << 8 | revision << 16
enum ProgressionOp : int64_t { kProgChoose = 1, kProgReroll = 2, kProgClose = 3, kProgOpen = 4 };
inline int64_t progressionParam(int64_t op, int index, uint32_t revision) { return op | int64_t(index & 0xFF) << 8 | int64_t(revision) << 16; }
// kEvProgOffer value >> 40
enum class ProgOfferEv : uint8_t { Opened = 0, Rerolled = 1, Closed = 2, Empty = 3 };
// kEvProgOrb value >> 40
enum class ProgOrbEv : uint8_t { Spawned = 0, Collected = 1, Expired = 2, Merged = 3 };

struct ProgUnit {
    uint64_t entity = 0;          // packed handle (sorted)
    uint8_t player = 0;
    int8_t slot = 0;              // deck slot (0 = hero)
    uint8_t bearer = 0;           // kProgBearerHero / kProgBearerAlly
};
struct ProgressionStore {
    lib::genre::PerkState perks;
    lib::genre::PickupState pickups;
    lib::genre::SummonState summons;
    std::vector<ProgUnit> units;              // sorted by entity
    std::vector<uint8_t> magnet;              // per player slot: one-shot magnet pending
    std::vector<int64_t> kills;               // per player slot
    std::vector<lib::genre::OrbSpawn> orbQueue;   // orbs to drop on the next step (Lua spawn_orb)
    const ProgUnit* find(Entity e) const;
};

class Progression {
public:
    bool active() const { return R_ != nullptr; }
    const ProgressionRules& rules() const { return *R_; }
    void setup(Simulation& sim, RtsModule& m, const Json& map);   // after the map's units exist (decks, heroes)
    void tick(Simulation& sim);                                   // the "progression" system (before "death")
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    bool command(Simulation& sim, const Command& cmd);           // kCmdProgression
    ProgressionStore* store(Simulation& sim) const;
    // Computer player: the offer choice a bot of `player` sends now (false: nothing to choose)
    bool botCommand(Simulation& sim, uint8_t player, Command& out) const;
    // Computer player (steering a top-down hero, agent-df-hero-net): where progression wants the hero now. A banked
    // level with offers at circles: the nearest own-team summon circle (or stay while the offer is open there);
    // else the nearest collectable xp orb within `orbRange`. false: nothing (objectives / extraction decide).
    bool botGoal(Simulation& sim, uint8_t player, Vec3& goal, Fixed& stand, Fixed orbRange = Fixed::fromInt(14)) const;

    // ---- hooks of the unit pipelines (RtsModule): no effect on units outside the table
    void modStat(Simulation& sim, Entity e, Stat s, Fixed& value) const;              // RtsModule::stat
    Fixed damage(Simulation& sim, Entity target, Entity source, Fixed amount) const;  // incoming multiplier, crits
    int16_t hitPayload(Simulation& sim, Entity source) const;                         // perk on-hit payload (rolls the chance)
    void beforeShot(Simulation& sim, Projectile& p) const;                            // bullet variant, splash, on-hit
    void afterShot(Simulation& sim, Projectile& p) const;                             // ricochet / fly-through counts
    const projectile::ProjectileFlight* variantFlight(int weapon) const;               // Projectile::weapon <= -2
    static int variantIndex(int weapon) { return weapon <= -2 ? -2 - weapon : -1; }

    // ---- queries / actions (Lua, tools, tests)
    const lib::genre::PlayerProgress* progress(Simulation& sim, uint8_t player) const;
    Entity heroOf(Simulation& sim, uint8_t player) const;
    int64_t statOf(Simulation& sim, Entity e, int64_t stat, int64_t base) const;      // library pipeline for a unit
    bool grantXp(Simulation& sim, uint8_t player, int64_t amount, bool pickup);
    bool grantPerk(Simulation& sim, uint8_t player, int64_t perk, int slot);
    bool choose(Simulation& sim, uint8_t player, uint32_t revision, int index);   // also the command path
    bool reroll(Simulation& sim, uint8_t player, uint32_t revision);
    bool open(Simulation& sim, uint8_t player);
    bool close(Simulation& sim, uint8_t player);
    const std::vector<lib::genre::SummonPointDef>& summonPoints() const { return points_; }   // rules + map circles
    const lib::genre::SummonConfig& summonConfig() const { return summon_; }                 // in ticks
    Entity spawnSlot(Simulation& sim, uint8_t player, int slot, Vec3 at, bool nearHero);   // a deck slot's unit as an ally

private:
    void registerUnit(Simulation& sim, ProgressionStore& st, Entity e, uint8_t player, int slot, uint8_t bearer);
    void adoptHeroes(Simulation& sim, ProgressionStore& st);
    void collectDeaths(Simulation& sim, ProgressionStore& st, std::vector<int64_t>& alliesDied);
    void stepPickups(Simulation& sim, ProgressionStore& st);
    void stepSummons(Simulation& sim, ProgressionStore& st, const std::vector<int64_t>& alliesDied);
    void stepUnits(Simulation& sim, ProgressionStore& st);
    void openReady(Simulation& sim, ProgressionStore& st);
public:
    bool atCircle(Simulation& sim, uint8_t player) const;   // the hero stands in a summon circle of its team
private:
    void dispatch(Simulation& sim, uint8_t player, const std::vector<lib::genre::PerkEvent>& ev);
    lib::genre::PerkHooks hooks(Simulation& sim) const;
    int64_t slotOfUnit(const lib::genre::PlayerProgress& p, int64_t unit) const;
    lib::genre::BattleDeck deckFor(const Json& map, uint8_t player) const;
    void maxHpBefore(Simulation& sim, uint8_t player, std::vector<std::pair<Entity, Fixed>>& out) const;
    void maxHpAfter(Simulation& sim, const std::vector<std::pair<Entity, Fixed>>& before) const;
    const ProgressionRules* R_ = nullptr;
    RtsModule* m_ = nullptr;
    // runtime configs in ticks (from the rules' seconds at the simulation's tick rate)
    lib::genre::PickupConfig pickup_;
    lib::genre::SummonConfig summon_;
    std::vector<lib::genre::SummonPointDef> points_;
    uint32_t allyLifetime_ = 0;
};

} // namespace aiverse::game
