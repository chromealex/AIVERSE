// The game simulation as its systems see it (heroes, vehicles, traffic, arsenal, raids, encounters, progression,
// commerce, careers, fields, environment, prompts, sensors, field operations): rules, match state, the shared
// queries and actions on units, and the other systems. The game module (GameCore) implements it.
#pragma once
#include <cstdint>
#include <optional>
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "../../engine/sim/Simulation.h"
#include "Components.h"
#include "GameData.h"
#include "GameState.h"
#include "ScriptHooks.h"

namespace aiverse::game {

class Arsenal; class Career; class Commerce; class Driving; class Encounters; class Environment; class FieldOps;
class GridFields; class Progression; class Prompts; class Raid; class Sensors; class TopDown; class Traffic;

class GameHost {
public:
    virtual ~GameHost() = default;
    GameState& state(Simulation& sim) { return *sim.world().resource<GameState>(); }
    virtual const Rules& rules() const = 0;
    virtual ScriptHooks* scripts() const = 0;   // null: no scripts

    // systems (null before setup; check active())
    virtual Arsenal* arsenal() const = 0;
    virtual Career* career() const = 0;
    virtual Commerce* commerce() const = 0;
    virtual Driving* driving() const = 0;
    virtual Encounters* encounters() const = 0;
    virtual Environment* environment() const = 0;
    virtual FieldOps* fieldOps() const = 0;
    virtual GridFields* gridFields() const = 0;
    virtual Progression* progression() const = 0;
    virtual Prompts* prompts() const = 0;
    virtual Raid* raid() const = 0;
    virtual Sensors* sensors() const = 0;
    virtual TopDown* topDown() const = 0;
    virtual Traffic* traffic() const = 0;

    // queries
    virtual bool hostile(Simulation& sim, uint8_t a, uint8_t b) const = 0;
    virtual uint8_t teamOf(Simulation& sim, uint8_t player) const = 0;
    virtual Fixed stat(Simulation& sim, Entity e, Stat s) const = 0;
    // Native effects use the authoritative stat pipeline, not presentation
    // estimates. No target-specific armor, bonuses, elevation or hit RNG.
    std::optional<Fixed> currentWeaponDamage(Simulation& sim, Entity caster, size_t slot = 0) const {
        const World& w = sim.world();
        if (!w.alive(caster)) return std::nullopt;
        const auto* unit = w.tryGetConst<Unit>(caster);
        if (!unit || !w.tryGetConst<Owner>(caster) || unit->type >= rules().types.size())
            return std::nullopt;
        const auto& weapons = rules().types[unit->type].weapons;
        if (slot >= weapons.size()) return std::nullopt;
        return fxMax(Fixed{}, weapons[slot].damage * stat(sim, caster, Stat::Damage));
    }
    virtual uint32_t statusFlags(Simulation& sim, Entity e) const = 0;
    virtual Fixed contactDistance(Simulation& sim, Entity a, Entity b) = 0;
    virtual bool isVisible(Simulation& sim, uint8_t player, Vec3 p) const = 0;
    virtual bool canSee(Simulation& sim, Entity e, uint8_t viewer) const = 0;
    virtual bool onScreen(Simulation& sim, Vec3 p) const = 0;
    virtual Vec3 lift(Simulation& sim, Vec3 p, Fixed h) const = 0;
    virtual bool canCast(Simulation& sim, Entity caster, int index) const = 0;
    virtual const AbilityDef& abilityFor(Simulation& sim, Entity e, int index) const = 0;
    virtual Vec3 freeSpotNear(Simulation& sim, Entity structure, uint8_t layer, const Vec3* toward = nullptr) = 0;

    // actions
    virtual Entity spawn(Simulation& sim, uint16_t type, uint8_t owner, Vec3 pos, bool underConstruction = false) = 0;
    virtual void issue(Simulation& sim, Entity e, const Order& o, bool queue) = 0;
    virtual void moveTo(Simulation& sim, Entity e, Vec3 goal, Fixed radius) = 0;
    virtual void stopMoving(Simulation& sim, Entity e) = 0;
    virtual void addResource(Simulation& sim, uint8_t player, uint16_t res, Fixed amount) = 0;
    virtual void dealDamage(Simulation& sim, Entity target, Fixed dmg, uint16_t dmgType, Entity source, uint8_t sourceOwner,
                            const WeaponDef* wd = nullptr) = 0;
    virtual void addStatus(Simulation& sim, Entity target, int def, Fixed duration, const EffectCtx& ctx) = 0;
    virtual void applyPayload(Simulation& sim, int payload, const EffectCtx& ctx) = 0;
    virtual void castAbility(Simulation& sim, Entity caster, int index, Entity target, Vec3 point) = 0;
    virtual void initProjectileProgram(Simulation& sim, Projectile& p, Vec3 start) const = 0;
};

// The game host of a simulation whose module is one.
inline GameHost& hostOf(Simulation& sim) { return dynamic_cast<GameHost&>(sim.module()); }

} // namespace aiverse::game
