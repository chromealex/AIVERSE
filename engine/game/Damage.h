// Damage to a unit: damage table, the game's mitigation, statuses (invulnerable, hit caps, illusions, damage taken,
// shields), lifesteal, the game's final hooks, then the hit itself (hp, last attacker, hit event, camps fight back).
// Header-only: the game passes its hooks (H below), so there is no per-hit indirection.
#pragma once
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "../../engine/sim/CommonComponents.h"
#include "../../engine/sim/Simulation.h"
#include "Components.h"
#include "GameData.h"
#include <algorithm>

namespace aiverse::game {

// H supplies (all called in this order, each may be a no-op):
//   bool before(target, source, Fixed& dmg)                  - false: nothing happens (native modules)
//   Fixed mitigate(target, raw, wd, dmgType)                  - bonuses, shields, armor model
//   Fixed taken(target, source, Fixed fin)                    - extra multipliers (cover, armor arcs)
//   uint32_t statusFlags(target)                              - union of active status flags
//   Fixed stat(e, Stat)                                       - effective stat
//   Fixed finish(target, source, sourceOwner, dmgType, fin)   - perks, script hooks
//   void hit(target, source, fin)                             - after hp changed, before the event (morale ...)
//   void afterHit(target, source, wd, killed)                 - after the event (crews, infection ...)
template <class H>
void applyDamage(Simulation& sim, const Rules& rules, Entity target, Fixed dmg, uint16_t dmgType, Entity source,
                 uint8_t sourceOwner, const WeaponDef* wd, H&& h) {
    World& w = sim.world();
    auto* u = w.tryGet<Unit>(target);
    if (!u || u->hp.raw <= 0) return;
    const UnitType& t = rules.types[u->type];
    if (t.invulnerable) return;
    if (!h.before(target, source, dmg)) return;
    const Fixed raw = dmgType == kDamagePure ? dmg : dmg * rules.damageMult(dmgType, t.armorType);
    Fixed fin = h.mitigate(target, raw, wd, dmgType);
    fin = h.taken(target, source, fin);
    if (rules.advancedEffects) {
        const uint32_t fl = h.statusFlags(target);
        if (fl & kStInvulnerable) return;
        if (wd && rules.anyHitCap)
            if (auto* st = w.tryGet<Statuses>(target)) {
                bool absorbed = false;
                for (auto& in : st->list) {
                    const Fixed cap = rules.statuses[in.def].hitCap;
                    if (cap.raw <= 0 || in.until <= sim.tick()) continue;
                    const uint32_t end = sim.tick() + std::max<uint32_t>(1, uint32_t((cap * int64_t(sim.tickRate())).toInt()));
                    if (in.until > end) in.until = end;
                    absorbed = true;
                }
                if (absorbed) return;
            }
        if (fl & kStFake) { u->hp = Fixed{}; u->flags |= kUnitSilent; return; }  // illusions pop on any hit
        fin = fin * h.stat(target, Stat::DamageTaken);
        if (auto* st = w.tryGet<Statuses>(target))
            for (auto& in : st->list) {
                if (in.shield.raw <= 0 || fin.raw <= 0) continue;
                Fixed absorb = fxMin(in.shield, fin);
                in.shield -= absorb; fin -= absorb;
                if (in.shield.raw <= 0 && rules.statuses[in.def].shield.raw > 0) in.until = sim.tick(); // shield broken
            }
        if (w.alive(source) && w.has<Unit>(source)) {
            Fixed ls = h.stat(source, Stat::Lifesteal);
            if (ls.raw > 0) { Unit& su = w.get<Unit>(source); su.hp = fxMin(h.stat(source, Stat::MaxHp), su.hp + fin * ls); }
        }
    }
    fin = h.finish(target, source, sourceOwner, dmgType, fin);
    u->hp -= fin;
    if (sourceOwner != 0) u->lastAttacker = sourceOwner;
    if (source.valid()) u->lastHitBy = source;
    h.hit(target, source, fin);
    SimEvent event{kEvHit, sourceOwner, target, source, fin.raw, w.get<Transform>(target).pos};
    if constexpr (requires { h.critical; }) {
        if (h.critical && fin.raw > 0) event.flags |= kHitCritical;
    }
    event.subjectType = u->type;
    if (auto* owner = w.tryGet<Owner>(target)) event.subjectOwner = owner->player;
    event.subjectKey = target.packed();
    sim.emit(event);
    h.afterHit(target, source, wd, u->hp.raw <= 0);
    // creeps fight back as a camp
    if (auto* g = w.tryGet<Guard>(target); g && w.alive(source) && w.has<Unit>(source)) {
        if (auto* c = w.tryGet<Combat>(target); c && !w.alive(c->target)) { c->target = source; c->autoTarget = true; }
    }
}

} // namespace aiverse::game
