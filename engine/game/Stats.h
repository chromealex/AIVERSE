// Effective unit stats: base value of the unit type, script override, technology modifiers of the owner, hero
// levels, buffs, statuses (unit and player), then the game's own modifiers, then the final hooks (perks) and the
// pull toward 1 of "lerp" statuses. Header-only and inlined: stats are read very often.
#pragma once
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "../../engine/sim/CommonComponents.h"
#include "../../engine/sim/Simulation.h"
#include "Components.h"
#include "GameData.h"
#include <vector>

namespace aiverse::game {

// Per unit type technology modifiers of a player (index = Stat).
struct TypeMods { Fixed add[size_t(Stat::Count)]; Fixed mul[size_t(Stat::Count)]; };

// Base value of stat s of unit type t (before any modifier).
inline Fixed baseStat(const UnitType& t, Stat s) {
    const Fixed one = Fixed::fromInt(1);
    switch (s) {
        case Stat::MaxHp: return t.maxHp;
        case Stat::Armor: return t.armor;
        case Stat::Damage: return one;          // multiplier on weapon damage
        case Stat::Range: return Fixed{};       // bonus added to weapon range
        case Stat::Speed: return t.speed;
        case Stat::Sight: return t.sight;
        case Stat::GatherRate: return one;
        case Stat::AttackSpeed: return one;
        case Stat::ProductionSpeed: return one;
        case Stat::EnergyRegen: case Stat::Defense: case Stat::CooldownRate: case Stat::UpgradeCost: case Stat::DamageTaken: case Stat::Scale: return one;
        case Stat::GarrisonCap: return Fixed::fromInt(t.garrisonMax);
        case Stat::Lifesteal: return Fixed{};
        case Stat::Armor2: return t.armor2;
        default: return Fixed{};
    }
}

// What the game supplies besides the unit itself.
struct StatSources {
    bool scriptBase = false;                                   // ScriptStats on units may replace the base value
    const TypeMods* mods = nullptr;                            // the owner's modifiers of this unit type (null: none)
    const std::vector<StatusInst>* playerStatuses = nullptr;   // the owner's statuses (advanced effects only)
};

// extra(add, mul): the game's own modifiers (applied in its order); finish(v): final hooks on the result (perks).
template <class Extra, class Finish>
Fixed effectiveStat(Simulation& sim, const Rules& rules, Entity e, Stat s, const StatSources& src, Extra&& extra, Finish&& finish) {
    World& w = sim.world();
    const Fixed kOne = Fixed::fromInt(1);
    const Unit& u = w.get<Unit>(e);
    const UnitType& t = rules.types[u.type];
    Fixed base = baseStat(t, s);
    if (src.scriptBase)
        if (auto* ss = w.tryGet<ScriptStats>(e))
            for (auto& x : ss->list) if (x.stat == uint8_t(s)) base = x.base;
    Fixed add{}, mul = kOne, lerp{};
    if (src.mods) { add += src.mods->add[size_t(s)]; mul *= src.mods->mul[size_t(s)]; }
    if (auto* h = w.tryGet<Hero>(e)) for (auto& pl : t.perLevel) if (pl.stat == s) { add += pl.add * (h->level - 1); }
    if (auto* b = w.tryGet<Buffs>(e)) {
        for (auto& bf : b->list) if (bf.stat == s && bf.until > sim.tick()) { add += bf.add; mul *= bf.mul; }
    }
    if (rules.advancedEffects) {
        Fixed smul = kOne, floor{};
        if (auto* st = w.tryGet<Statuses>(e))
            for (auto& in : st->list)
                for (auto& m : rules.statuses[in.def].mods)
                    if (m.stat == s) floor = fxMax(floor, m.floor);
        if (auto* st = w.tryGet<Statuses>(e))
            for (auto& in : st->list)
                for (auto& m : rules.statuses[in.def].mods)
                    if (m.stat == s) { add += m.add * in.stacks; lerp += m.lerp * in.stacks; for (int k = 0; k < in.stacks; ++k) (floor.raw > 0 ? smul : mul) *= m.mul; }
        if (floor.raw > 0) mul *= fxMax(smul, floor);
        if (src.playerStatuses)
            for (auto& in : *src.playerStatuses)
                for (auto& m : rules.statuses[in.def].mods)
                    if (m.stat == s) { add += m.add * in.stacks; for (int k = 0; k < in.stacks; ++k) mul *= m.mul; }
    }
    extra(add, mul);
    // Damage/Range/GatherRate/AttackSpeed "add" are fractional bonuses on top of 1 / absolute range
    Fixed v = (base + add) * mul;
    finish(v);
    if (lerp.raw > 0) v = v + (kOne - v) * fxMin(kOne, lerp);   // statuses that pull the stat toward 1
    if (s == Stat::Speed && t.speed.raw > 0) {
        if (rules.hasMinUnitSpeed) v = fxMax(v, rules.minUnitSpeed);
        if (rules.hasMaxUnitSpeed) v = fxMin(v, rules.maxUnitSpeed);
    }
    return v;
}

} // namespace aiverse::game
