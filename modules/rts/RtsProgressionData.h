// In-match progression on real units (rules "progression", docs/progression.md): the match-progression library
// package (libraries/include/aiverselib/genre/{Perks,XpPickups,Summons,BattleDeck}.h) connected to the RTS
// simulation. Hero xp from kills and xp orbs (ground pickups with a magnet), a level curve, seeded level-up offers
// chosen by a player command or by the bot, perk stat effects on the unit pipelines (damage, incoming damage, move
// speed, fire interval, range, max health, splash, crits, regen), bullet modifiers (split / ricochet / fly-through
// through the shared projectile programs), on-hit payloads (burn, stun, ...), summon circles that restore or
// recruit allies, and the battle deck from the match start payload.
//
// The block keeps the library's JSON format (stats, perks, units, deck, xp, offers, summonPoints) plus engine keys;
// the engine keys of `units[]` and `perks[]` entries are removed before the library reader sees them.
// Runtime: RtsProgression.{h,cpp} (store, system, hooks, bot), Lua `progression`: RtsProgressionApi.cpp.
// A game without the block loads, plays, saves and hashes exactly as before.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include "../../engine/core/Fixed.h"
#include "../../engine/data/Json.h"
#include "../../engine/sim/ProjectileData.h"
#include "libraries/include/aiverselib/genre/Perks.h"
#include "libraries/include/aiverselib/genre/XpPickups.h"
#include "libraries/include/aiverselib/genre/Summons.h"

namespace aiverse::game {

struct Rules;
struct EffectParser;

// Bearer ids of real units (library ModEffect::bearer / PerkDef::bearer): perks may target only the hero or allies.
constexpr int64_t kProgBearerHero = 1, kProgBearerAlly = 2;

// Engine keys of one perk.
struct ProgPerkExt {
    std::vector<projectile::ProjectileModifier> bullets;   // "bullet": {"modifiers": [...]} split / ricochet / flyThrough / damage / speed / radius
    int16_t onHit = -1;                                    // "onHit": effects payload applied to targets the bearer hits
    Fixed onHitChance = Fixed::fromInt(1);
};

// A projectile weapon of a progression unit with a set of bullet perks: the weapon's own program followed by each
// perk's modifiers in perk id order (Projectile::weapon = -2 - index). Counts of ricochet / fly-through entries are
// scaled at the shot by the number of times the perk was taken (bounded by the template count).
struct ProgFlightVariant {
    uint16_t type = 0;
    uint8_t weapon = 0;
    uint32_t mask = 0;                         // bit k = bulletPerks[k]
    projectile::ProjectileFlight flight;
    std::vector<int8_t> perkOf;                // per modifier: index into bulletPerks, -1 = the weapon's own
    std::vector<uint16_t> baseCount;           // per modifier: count of one selection
};

struct ProgressionRules {
    bool enabled = false;
    lib::genre::PerkDefinitions defs;
    lib::genre::PerkConfig perk;
    lib::genre::PickupConfig pickup;
    lib::genre::SummonConfig summon;
    std::vector<lib::genre::SummonPointDef> points;
    std::map<int64_t, uint16_t> unitType;      // deck unit id -> unit type ("units[].type")
    std::vector<int64_t> typeUnit;             // unit type -> deck unit id (0: none)
    std::vector<uint8_t> players;              // "players": slots taking part (empty: every player slot)
    std::vector<int64_t> killXp;               // per unit type: xp of a kill ("killXp")
    bool orbs = true;                          // kills drop xp orbs (false: xp straight to the killer)
    bool orbsForKiller = false;                // "orbOwner": "killer" (else anyone may collect)
    bool autoOpen = true;                      // offers open as soon as a level is banked
    bool offerAtCircles = false;               // "offerAt": "summonPoints": offers open only while the hero stands in a circle (Drone Fall stations)
    Fixed allyFollow = Fixed::fromInt(5);      // "squad.follow": idle allies walk back when farther from the hero
    uint32_t allyLifetime = 0;                 // "squad.lifetime" (ticks at load: seconds * tick rate later)
    Fixed allyLifetimeSeconds{};
    int64_t critDamageMilli = 2000;            // base crit multiplier (critDamage stat base)
    std::vector<int64_t> bulletPerks;          // perks with bullet modifiers (ascending id), at most 6
    std::map<int64_t, ProgPerkExt> ext;        // engine keys by perk id
    std::vector<ProgFlightVariant> variants;   // Projectile::weapon = -2 - index
    std::map<uint64_t, int> variantOf;         // (type << 40 | weapon << 32 | mask) -> index
    Json decks;                                // "decks": payloads by player (optional)
    Json defaultDeck;                          // "defaultDeck": the payload every player gets without one
    uint64_t defsHash = 0, configHash = 0;     // perkDefinitionsHash / progressionConfigHash (state checksums)
    int variantFor(uint16_t type, int weapon, uint32_t mask) const {
        auto it = variantOf.find(uint64_t(type) << 40 | uint64_t(uint8_t(weapon)) << 32 | mask);
        return it == variantOf.end() ? -1 : it->second;
    }
    int64_t unitOfType(uint16_t type) const { return type < typeUnit.size() ? typeUnit[type] : 0; }
};

// rules "progression" (called by loadRules after the effect engine is set up: on-hit payloads use it).
void loadProgressionRules(Rules& r, const Json& j, EffectParser& ep);

} // namespace aiverse::game
