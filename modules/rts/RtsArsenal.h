// Arsenal (agent-gta2): the top-down hero's weapons, ammo and money, GTA2 style. rules "arsenal" (docs/arsenal.md).
//
// The hero carries a row of weapon slots (fists, pistol, machine gun, ...), each with its own ammo; one is in hand.
// Slots name weapons of the hero's unit type, so damage, range, cooldown, projectile speed, splash and the projectile
// program ("flight": radius, walls, fly-through, ricochet) stay ordinary weapon data. Holding fire (the fire bit, or
// the aim when topDown.aim.fires) shoots the weapon in hand along the aim, else along the facing; there is no target
// lock (a soft snap toward a hostile inside topDown.aim.snapCone). Bullets are projectile programs launched at a
// point, so they stop at walls and hit the first body in their way; "hitsNeutral" lets them hit passive neutral
// objects too (parked cars). Projectile speed 0 is a hit-scan along the aim (fists). An empty weapon switches to the
// next lower slot that has ammo.
//
// Pickups are units of the types listed in arsenal.pickups (crates on the map, placed by the map or spawned later):
// walking (or driving) over one gives ammo, money or health, or sells a weapon for money (price). A taken crate is
// off the map (kUnitInCargo) until it respawns. Money is a stock resource ("money"); unit bounties pay into it, and
// "payNeutral" also pays the bounty of passive neutral victims (cars). A dead hero loses his weapons
// ("loseOnDeath") and pays "deathFine".
//
// State lives in the ArsenalStore world resource: hashed, saved after the vehicles, copied by rollback snapshots.
// Games without the block keep their checksums and save layout.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "../../engine/data/Json.h"

namespace aiverse { class Simulation; class Hasher; class Archive; struct Command; }

namespace aiverse::game {

class RtsModule;

// kCmdTopDownWeapon param: kWeaponNext / kWeaponPrev cycle through the slots that have ammo, kWeaponSlot + i selects
// slot i (ignored when it is empty).
constexpr int64_t kWeaponNext = 1, kWeaponPrev = -1, kWeaponSlot = 100;
// Projectile::layers bit: the projectile also hits passive neutral units (owner 0), not only hostile ones.
constexpr uint8_t kLayerHitsNeutral = 0x80;

struct ArsenalWeapon {
    std::string weapon;                 // weapon id on the hero types
    std::string name;                   // HUD
    int32_t start = 0;                  // ammo at the start and after a respawn (-1: unlimited)
    int32_t max = 999;                  // ammo cap (unlimited weapons ignore it)
    int pellets = 1;                    // projectiles per shot (shotgun), fanned over `spread`
    Fixed spread{};                     // turns (data: degrees)
    std::vector<int16_t> index;         // weapon index per unit type (-1: the type has no such weapon)
};

struct ArsenalPickupType {
    uint16_t type = UINT16_MAX;
    int16_t weapon = -1;                // arsenal slot it fills
    int32_t ammo = 0;
    Fixed money{}, heal{}, price{};     // heal: hit points; price: paid from the money resource (gun shop)
    Fixed respawnSeconds = Fixed::fromInt(30);   // 0: never comes back
    uint32_t respawnTicks = 0;
};

struct ArsenalRules {
    bool enabled = false;
    std::vector<ArsenalWeapon> weapons;            // slot order (cycling, number keys)
    std::vector<ArsenalPickupType> pickups;        // sorted by type
    int16_t money = -1;                            // resource index of the money (-1: no money)
    bool loseOnDeath = true, hitsNeutral = true, payNeutral = true;
    Fixed deathFine{};
    Fixed pickupReach = Fixed::fromRatio(1, 4);    // cells added to the body radii
    Fixed aimWindup{};                             // seconds a firearm is raised before its first shot (0: fires at once)
};

struct ArsenalHand {
    Entity hero = kNullEntity;                     // the hero this loadout belongs to (a new hero: respawn)
    int16_t current = 0;
    uint32_t readyAt = 0;                          // next shot tick
    std::vector<int32_t> ammo;                     // per slot (-1: unlimited)
};

struct ArsenalCrate {
    Entity e = kNullEntity;
    uint16_t kind = 0;                             // index into ArsenalRules::pickups
    uint32_t respawnAt = 0;                        // taken: back at this tick (0: on the map; UINT32_MAX: gone for good)
};

struct ArsenalStore {
    std::vector<ArsenalHand> hands;                // by player slot (0 unused)
    std::vector<ArsenalCrate> crates;              // ascending entity index
};

class Arsenal {
public:
    bool active() const { return rules_.enabled; }
    const ArsenalRules& rules() const { return rules_; }
    // rules "arsenal" with names resolved against the unit types / resources (throws readable errors)
    static ArsenalRules parse(const Json& j, const std::vector<std::string>& typeIds, const std::vector<std::string>& resourceIds);
    void setup(Simulation& sim, RtsModule& m, const Json& rulesJson);
    bool command(Simulation& sim, const Command& cmd);   // true: kCmdTopDownWeapon handled
    void tick(Simulation& sim);                          // after the vehicles: crates, loadouts, firing
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    ArsenalStore* store(Simulation& sim) const;
    const ArsenalHand* hand(Simulation& sim, uint8_t player) const;
    bool isCrate(Simulation& sim, Entity e) const;
    // kill rewards for passive neutral victims ("payNeutral"): the bounty of the type, paid to `killer`
    void neutralKill(Simulation& sim, Entity victim, uint8_t killer);

private:
    void adoptCrates(Simulation& sim, ArsenalStore& st);
    void loadout(Simulation& sim, ArsenalHand& h, Entity hero, uint8_t player, bool died);
    void crates(Simulation& sim, ArsenalStore& st);
    bool take(Simulation& sim, ArsenalHand& h, Entity hero, uint8_t player, const ArsenalPickupType& p);
    void fire(Simulation& sim, ArsenalHand& h, Entity hero, uint8_t player);
    void hitscan(Simulation& sim, Entity hero, uint8_t player, size_t weapon, Vec3 from, Vec3 dir, Fixed damage);
    Vec3 snap(Simulation& sim, Entity hero, uint8_t player, Vec3 dir, Fixed range) const;
    bool selectable(const ArsenalHand& h, size_t slot) const;
    void fallBack(ArsenalHand& h);

    RtsModule* m_ = nullptr;
    ArsenalRules rules_;
};

} // namespace aiverse::game
