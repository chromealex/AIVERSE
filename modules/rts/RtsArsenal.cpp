// Arsenal (rules "arsenal", RtsArsenal.h): weapon slots and ammo of the top-down hero, free fire along the aim
// (projectile programs launched at a point, hit-scan for speed 0), crates on the map (ammo, money, health, gun shop),
// money for kills and the price of dying. Fixed maths only; every loop runs in player-slot or entity-index order.
#include "RtsArsenal.h"
#include "RtsInternal.h"
#include "../../engine/core/Archive.h"
#include "../../engine/core/FixedMath.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {
using namespace detail;

namespace {
Fixed nonNegative(const Json& j, const char* key, Fixed def, const std::string& where) {
    Fixed v = j.getFx(key, def);
    if (v.raw < 0) throw std::runtime_error(where + "." + key + ": must not be negative");
    return v;
}
int32_t ammoOf(const Json& j, const char* key, int32_t def, const std::string& where) {
    if (!j.has(key)) return def;
    const int64_t v = j.getInt(key, def);
    if (v < -1 || v > 1000000) throw std::runtime_error(where + "." + key + ": -1 (unlimited) or 0..1000000");
    return int32_t(v);
}
Vec3 dirOf(int32_t angle) { return TopDown::direction(angle); }
ArsenalCrate* findCrate(ArsenalStore& st, Entity e) {
    auto it = std::lower_bound(st.crates.begin(), st.crates.end(), e.index, [](const ArsenalCrate& c, uint32_t i) { return c.e.index < i; });
    return it != st.crates.end() && it->e == e ? &*it : nullptr;
}
} // namespace

// ================================================================= data

ArsenalRules Arsenal::parse(const Json& j, const std::vector<std::string>& ids, const std::vector<std::string>& resources) {
    ArsenalRules r;
    if (!j.isObject()) return r;
    r.enabled = true;
    auto typeIndex = [&](const std::string& id, const std::string& where) {
        for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return uint16_t(i);
        throw std::runtime_error(where + ": unknown type '" + id + "'");
    };
    const Json& ws = j["weapons"];
    if (!ws.isArray() || ws.size() == 0) throw std::runtime_error("arsenal.weapons: list at least one weapon slot");
    if (ws.size() > 32) throw std::runtime_error("arsenal.weapons: at most 32 slots");
    for (size_t i = 0; i < ws.size(); ++i) {
        const Json& v = ws[i];
        const std::string where = "arsenal.weapons[" + std::to_string(i) + "]";
        ArsenalWeapon a;
        a.weapon = v.isString() ? v.str() : v.getStr("weapon");
        if (a.weapon.empty()) throw std::runtime_error(where + ".weapon: the weapon id of the hero type");
        for (auto& o : r.weapons) if (o.weapon == a.weapon) throw std::runtime_error(where + ": weapon '" + a.weapon + "' listed twice");
        a.name = v.isObject() ? v.getStr("name", a.weapon) : a.weapon;
        if (v.isObject()) {
            a.start = ammoOf(v, "start", 0, where);
            a.max = ammoOf(v, "max", 999, where);
            if (a.max < 0) throw std::runtime_error(where + ".max: must not be negative");
            if (a.start > a.max) a.start = a.max;
            const int64_t pellets = v.getInt("pellets", 1);
            if (pellets < 1 || pellets > 16) throw std::runtime_error(where + ".pellets: 1..16");
            a.pellets = int(pellets);
            const Fixed spread = nonNegative(v, "spread", Fixed{}, where);
            if (spread > Fixed::fromInt(180)) throw std::runtime_error(where + ".spread: at most 180 degrees");
            a.spread = spread / 360;
        }
        r.weapons.push_back(std::move(a));
    }
    if (j.has("money")) {
        const std::string res = j.getStr("money");
        for (size_t i = 0; i < resources.size(); ++i) if (resources[i] == res) r.money = int16_t(i);
        if (r.money < 0) throw std::runtime_error("arsenal.money: unknown resource '" + res + "'");
    } else if (!resources.empty()) r.money = 0;
    r.loseOnDeath = j.getBool("loseOnDeath", true);
    r.hitsNeutral = j.getBool("hitsNeutral", true);
    r.payNeutral = j.getBool("payNeutral", true);
    r.deathFine = nonNegative(j, "deathFine", Fixed{}, "arsenal");
    r.pickupReach = nonNegative(j, "pickupReach", r.pickupReach, "arsenal");
    r.aimWindup = j.getBool("aimWindup", false);
    if (const Json& ps = j["pickups"]; ps.isObject()) for (auto& [id, v] : ps.members()) {
        const std::string where = "arsenal.pickups." + id;
        ArsenalPickupType p;
        p.type = typeIndex(id, where);
        if (v.has("weapon")) {
            const std::string wid = v.getStr("weapon");
            for (size_t i = 0; i < r.weapons.size(); ++i) if (r.weapons[i].weapon == wid) p.weapon = int16_t(i);
            if (p.weapon < 0) throw std::runtime_error(where + ".weapon: '" + wid + "' is not an arsenal slot");
            p.ammo = ammoOf(v, "ammo", 10, where);
            if (p.ammo <= 0) throw std::runtime_error(where + ".ammo: must be positive");
        }
        p.money = nonNegative(v, "money", Fixed{}, where);
        p.heal = nonNegative(v, "heal", Fixed{}, where);
        p.price = nonNegative(v, "price", Fixed{}, where);
        p.respawnSeconds = nonNegative(v, "respawn", p.respawnSeconds, where);
        if ((p.money.raw > 0 || p.price.raw > 0) && r.money < 0) throw std::runtime_error(where + ": money needs the 'money' resource");
        if (p.weapon < 0 && p.money.raw == 0 && p.heal.raw == 0) throw std::runtime_error(where + ": gives nothing (weapon, money or heal)");
        r.pickups.push_back(p);
    }
    std::sort(r.pickups.begin(), r.pickups.end(), [](const ArsenalPickupType& a, const ArsenalPickupType& b) { return a.type < b.type; });
    return r;
}

ArsenalStore* Arsenal::store(Simulation& sim) const { return active() ? sim.world().resource<ArsenalStore>() : nullptr; }

const ArsenalHand* Arsenal::hand(Simulation& sim, uint8_t player) const {
    auto* st = store(sim);
    return st && player < st->hands.size() ? &st->hands[player] : nullptr;
}

bool Arsenal::isCrate(Simulation& sim, Entity e) const {
    auto* st = store(sim);
    return st && findCrate(*st, e);
}

void Arsenal::setup(Simulation& sim, RtsModule& m, const Json& rulesJson) {
    m_ = &m;
    std::vector<std::string> ids, res;
    for (auto& t : m.rules().types) ids.push_back(t.id);
    for (auto& r : m.rules().resources) res.push_back(r.id);
    rules_ = parse(rulesJson["arsenal"], ids, res);
    if (!active()) return;
    if (!m.topDown() || !m.topDown()->active()) throw std::runtime_error("arsenal: needs the 'topDown' block (the hero carries the weapons)");
    const auto& types = m.rules().types;
    for (auto& a : rules_.weapons) {
        a.index.assign(types.size(), -1);
        for (size_t t = 0; t < types.size(); ++t)
            for (size_t i = 0; i < types[t].weapons.size() && i < 127; ++i)
                if (types[t].weapons[i].id == a.weapon) { a.index[t] = int16_t(i); break; }
        for (uint16_t h : m.topDown()->rules().heroTypes)
            if (a.index[h] < 0) throw std::runtime_error("arsenal.weapons: hero type '" + types[h].id + "' has no weapon '" + a.weapon + "'");
    }
    for (auto& p : rules_.pickups) p.respawnTicks = uint32_t((p.respawnSeconds * int64_t(sim.tickRate())).toInt());
    auto& st = sim.world().setResource<ArsenalStore>();
    st.hands.assign(sim.players().size() + 1, {});
    adoptCrates(sim, st);
}

void Arsenal::hash(Simulation& sim, Hasher& h) const {
    auto* st = store(sim);
    if (!st) return;
    h.u32(uint32_t(st->hands.size()));
    for (auto& a : st->hands) {
        h.u64(a.hero.packed()); h.i32(a.current); h.u32(a.readyAt);
        h.u32(uint32_t(a.ammo.size())); for (int32_t n : a.ammo) h.i32(n);
    }
    h.u32(uint32_t(st->crates.size()));
    for (auto& c : st->crates) { h.u64(c.e.packed()); h.u32(c.kind); h.u32(c.respawnAt); }
}

void Arsenal::io(Simulation& sim, Archive& a) {
    auto* st = store(sim);
    if (!st) return;
    uint32_t n = uint32_t(st->hands.size());
    aiverse::io(a, n);
    if (!a.writing()) { if (n > 256) { a.fail(); return; } st->hands.assign(n, {}); }
    for (auto& hd : st->hands) {
        uint32_t k = uint32_t(hd.ammo.size());
        a(hd.hero, hd.current, hd.readyAt, k);
        if (!a.writing()) { if (k > 32) { a.fail(); return; } hd.ammo.assign(k, 0); }
        for (auto& x : hd.ammo) a(x);
    }
    n = uint32_t(st->crates.size());
    aiverse::io(a, n);
    if (!a.writing()) { if (n > 1000000) { a.fail(); return; } st->crates.assign(n, {}); }
    for (auto& c : st->crates) a(c.e, c.kind, c.respawnAt);
}

// ================================================================= loadout and weapon choice

bool Arsenal::selectable(const ArsenalHand& h, size_t slot) const {
    return slot < h.ammo.size() && h.ammo[slot] != 0;
}

void Arsenal::fallBack(ArsenalHand& h) {
    // the weapon in hand ran dry: the next lower slot with ammo, else the next higher one
    for (int i = h.current - 1; i >= 0; --i) if (selectable(h, size_t(i))) { h.current = int16_t(i); return; }
    for (size_t i = size_t(h.current) + 1; i < h.ammo.size(); ++i) if (selectable(h, i)) { h.current = int16_t(i); return; }
}

void Arsenal::loadout(Simulation& sim, ArsenalHand& h, Entity hero, uint8_t player, bool died) {
    const bool first = h.ammo.empty();
    if (first || (died && rules_.loseOnDeath)) {
        h.ammo.assign(rules_.weapons.size(), 0);
        for (size_t i = 0; i < rules_.weapons.size(); ++i) h.ammo[i] = rules_.weapons[i].start;
        h.current = 0;
        for (size_t i = rules_.weapons.size(); i-- > 0;) if (selectable(h, i)) { h.current = int16_t(i); break; }   // best weapon held
    }
    if (died && rules_.deathFine.raw > 0 && rules_.money >= 0) {
        auto& eco = m_->state(sim).players[player];
        const Fixed fine = fxMin(rules_.deathFine, fxMax(Fixed{}, eco.stock[size_t(rules_.money)]));
        if (fine.raw > 0) m_->addResource(sim, player, uint16_t(rules_.money), -fine);
    }
    h.hero = hero;
    h.readyAt = sim.tick();
}

bool Arsenal::command(Simulation& sim, const Command& cmd) {
    if (cmd.type != kCmdTopDownWeapon) return false;
    auto* st = store(sim);
    if (!st || cmd.player == 0 || cmd.player >= st->hands.size()) return true;
    ArsenalHand& h = st->hands[cmd.player];
    const int n = int(h.ammo.size());
    if (n == 0) return true;
    if (cmd.param == kWeaponNext || cmd.param == kWeaponPrev) {
        const int step = cmd.param == kWeaponNext ? 1 : n - 1;
        for (int k = 1, i = (h.current + step) % n; k < n; ++k, i = (i + step) % n)
            if (selectable(h, size_t(i))) { h.current = int16_t(i); break; }
    } else if (cmd.param >= kWeaponSlot && cmd.param < kWeaponSlot + n && selectable(h, size_t(cmd.param - kWeaponSlot))) {
        h.current = int16_t(cmd.param - kWeaponSlot);
    }
    return true;
}

// ================================================================= crates

void Arsenal::adoptCrates(Simulation& sim, ArsenalStore& st) {
    World& w = sim.world();
    st.crates.erase(std::remove_if(st.crates.begin(), st.crates.end(), [&](const ArsenalCrate& c) { return !w.alive(c.e); }), st.crates.end());
    if (rules_.pickups.empty()) return;
    std::vector<ArsenalCrate> fresh;
    w.each<Unit, Transform>([&](Entity e, Unit& u, Transform&) {
        auto it = std::lower_bound(rules_.pickups.begin(), rules_.pickups.end(), u.type, [](const ArsenalPickupType& p, uint16_t t) { return p.type < t; });
        if (it == rules_.pickups.end() || it->type != u.type || findCrate(st, e)) return;
        fresh.push_back({e, uint16_t(it - rules_.pickups.begin()), 0});
    });
    if (fresh.empty()) return;
    st.crates.insert(st.crates.end(), fresh.begin(), fresh.end());
    std::sort(st.crates.begin(), st.crates.end(), [](const ArsenalCrate& a, const ArsenalCrate& b) { return a.e.index < b.e.index; });
}

bool Arsenal::take(Simulation& sim, ArsenalHand& h, Entity hero, uint8_t player, const ArsenalPickupType& p) {
    World& w = sim.world();
    Unit& hu = w.get<Unit>(hero);
    const Fixed maxHp = m_->stat(sim, hero, Stat::MaxHp);
    bool useful = p.money.raw > 0;
    if (p.heal.raw > 0 && hu.hp < maxHp) useful = true;
    if (p.weapon >= 0) {
        const int32_t have = h.ammo[size_t(p.weapon)];
        if (have >= 0 && have < rules_.weapons[size_t(p.weapon)].max) useful = true;
    }
    if (!useful) return false;   // full: the crate stays for later
    if (p.price.raw > 0) {
        auto& eco = m_->state(sim).players[player];
        if (eco.stock[size_t(rules_.money)] < p.price) {
            if (sim.tick() % std::max<uint32_t>(1, sim.tickRate()) == 0) sim.emit({kEvNotEnough, player, hero, kNullEntity, rules_.money, w.get<Transform>(hero).pos});
            return false;
        }
        m_->addResource(sim, player, uint16_t(rules_.money), -p.price);
    }
    if (p.money.raw > 0) m_->addResource(sim, player, uint16_t(rules_.money), p.money);
    if (p.heal.raw > 0) hu.hp = fxMin(maxHp, hu.hp + p.heal);
    if (p.weapon >= 0) {
        int32_t& have = h.ammo[size_t(p.weapon)];
        const bool fresh = have == 0;
        if (have >= 0) have = std::min(rules_.weapons[size_t(p.weapon)].max, have + p.ammo);
        if (fresh) h.current = p.weapon;   // a weapon you did not have goes into your hand (GTA2)
    }
    return true;
}

void Arsenal::crates(Simulation& sim, ArsenalStore& st) {
    World& w = sim.world();
    const uint32_t now = sim.tick();
    const auto& types = m_->rules().types;
    // collectors: heroes on foot, heroes at the wheel (the car body collects)
    struct Taker { uint8_t player; Entity hero; Vec3 pos; Fixed radius; };
    std::vector<Taker> takers;
    TopDown* td = m_->topDown();
    for (size_t pl = 1; pl < st.hands.size(); ++pl) {
        const Entity hero = td->heroOf(sim, uint8_t(pl));
        if (!w.alive(hero) || w.get<Unit>(hero).hp.raw <= 0) continue;
        Entity body = hero;
        if (m_->driving() && m_->driving()->active())
            if (const DrivingCar* c = m_->driving()->carOf(sim, hero)) body = c->e;
        if (!w.alive(body)) continue;
        Fixed radius = types[w.get<Unit>(body).type].radius;
        if (body != hero)   // a car collects with its whole length
            for (auto& v : m_->driving()->rules().vehicles) if (v.type == w.get<Unit>(body).type) radius = fxMax(radius, v.length / 2);
        takers.push_back({uint8_t(pl), hero, w.get<Transform>(body).pos, radius});
    }
    for (auto& c : st.crates) {
        if (!w.alive(c.e)) continue;
        Unit& cu = w.get<Unit>(c.e);
        if (c.respawnAt != 0) {
            if (c.respawnAt == UINT32_MAX || now < c.respawnAt) continue;
            c.respawnAt = 0;
            cu.flags = uint16_t(cu.flags & ~kUnitInCargo);
            sim.emit({kEvSpawned, w.get<Owner>(c.e).player, c.e, kNullEntity, int64_t(cu.type), w.get<Transform>(c.e).pos});
        }
        const ArsenalPickupType& p = rules_.pickups[c.kind];
        const Vec3 cp = w.get<Transform>(c.e).pos;
        for (auto& t : takers) {
            if (dist2D(t.pos, cp) > t.radius + types[cu.type].radius + rules_.pickupReach) continue;
            if (!take(sim, st.hands[t.player], t.hero, t.player, p)) continue;
            cu.flags |= kUnitInCargo;   // off the map until it comes back
            c.respawnAt = p.respawnTicks > 0 ? now + p.respawnTicks : UINT32_MAX;
            sim.emit({kEvItem, t.player, t.hero, c.e, int64_t(c.kind), cp});
            break;
        }
    }
}

// ================================================================= firing

Vec3 Arsenal::snap(Simulation& sim, Entity hero, uint8_t player, Vec3 dir, Fixed range) const {
    const TopDownRules& tr = m_->topDown()->rules();
    if (!tr.snapOn || tr.snapCos < Fixed::fromInt(-1)) return dir;
    World& w = sim.world();
    const Vec3 pos = w.get<Transform>(hero).pos;
    Entity best; Fixed bestD;
    m_->state(sim).hash.query(pos, range + Fixed::fromInt(2), [&](Entity c) {
        if (c == hero || !w.alive(c)) return;
        const Unit* cu = w.tryGet<Unit>(c);
        const Owner* co = w.tryGet<Owner>(c);
        if (!cu || !co || cu->hp.raw <= 0 || (cu->flags & kUnitOffMap)) return;
        if (m_->rules().types[cu->type].invulnerable || !m_->hostile(sim, player, co->player)) return;
        const Vec3 cp = w.get<Transform>(c).pos;
        if (!m_->isVisible(sim, player, cp)) return;
        Vec3 off = flat(cp - pos);
        const Fixed l = off.length();
        if (l.raw <= 0 || l > range || off.dot(dir) < tr.snapCos * l) return;
        if (!best.valid() || l < bestD || (l == bestD && c.index < best.index)) { best = c; bestD = l; }
    });
    if (!best.valid()) return dir;
    return fxNormalize(flat(w.get<Transform>(best).pos - pos));
}

void Arsenal::hitscan(Simulation& sim, Entity hero, uint8_t player, size_t weapon, Vec3 from, Vec3 dir, Fixed damage) {
    World& w = sim.world();
    const MapGrid& g = m_->state(sim).grid;
    const UnitType& ht = m_->rules().types[w.get<Unit>(hero).type];
    const WeaponDef& wd = ht.weapons[weapon];
    // walls: the first cell along the ray no ground unit can enter
    Fixed reach = wd.range;
    const Fixed step = Fixed::fromRatio(1, 8);
    for (Fixed d = step; d <= wd.range; d += step) {
        const Vec3 q = from + dir * d;
        if (!g.passable(g.cellX(q.x), g.cellY(q.z), kLayerGround)) { reach = d; break; }
    }
    Entity best; Fixed bestAlong;
    m_->state(sim).hash.query(from + dir * (reach / 2), reach / 2 + Fixed::fromInt(2), [&](Entity c) {
        if (c == hero || !w.alive(c)) return;
        const Unit* cu = w.tryGet<Unit>(c);
        const Owner* co = w.tryGet<Owner>(c);
        if (!cu || !co || cu->hp.raw <= 0 || (cu->flags & kUnitOffMap)) return;
        const UnitType& ct = m_->rules().types[cu->type];
        if (ct.invulnerable || !layerHit(wd.targets, ct.layer)) return;
        if (!m_->hostile(sim, player, co->player) && !(rules_.hitsNeutral && co->player == 0)) return;
        const Vec3 off = flat(w.get<Transform>(c).pos - from);
        const Fixed along = off.dot(dir);
        if (along.raw < 0 || along > reach + ct.radius) return;
        const Fixed side2 = off.dot(off) - along * along;
        if (side2 > ct.radius * ct.radius) return;
        if (!best.valid() || along < bestAlong || (along == bestAlong && c.index < best.index)) { best = c; bestAlong = along; }
    });
    if (best.valid()) m_->dealDamage(sim, best, damage, wd.damageType, hero, player, &wd);
}

void Arsenal::fire(Simulation& sim, ArsenalHand& h, Entity hero, uint8_t player) {
    World& w = sim.world();
    TopDown* td = m_->topDown();
    const TopDownPilot& pilot = td->store(sim)->pilots[player];
    bool trigger = pilot.input.fire || (pilot.input.aim && td->rules().aimFires);
    if (rules_.aimWindup) {   // rule arsenal.aimWindup: a quarter second with the weapon raised before the first shot
    // Saved first-shot windup: a tap is queued, sustained fire keeps its normal cadence.
    if(size_t(h.current)<rules_.weapons.size()&&rules_.weapons[size_t(h.current)].weapon!="fists"){
        auto& vars=m_->state(sim).trig.vars;const auto key="kingston_aim_"+std::to_string(hero.packed())+"_";
        const bool aiming=pilot.input.aim||pilot.input.fire;
        if(aiming&&(vars[key+"active"].raw<=0||vars[key+"slot"].toInt()!=h.current)){
            vars[key+"until"]=Fixed::fromInt(sim.tick()+std::max<uint32_t>(1,(sim.tickRate()+3)/4));
            vars[key+"slot"]=Fixed::fromInt(h.current);
        }
        if(trigger&&vars[key+"trigger"].raw<=0)vars[key+"pending"]=Fixed::fromInt(1);
        vars[key+"trigger"]=Fixed::fromInt(trigger);vars[key+"active"]=Fixed::fromInt(aiming);
        trigger=trigger||vars[key+"pending"].raw>0;
        if(trigger&&int64_t(sim.tick())<vars[key+"until"].toInt())return;
        if(trigger&&sim.tick()>=h.readyAt)vars[key+"pending"]={};
    }
    }
    if (!trigger || sim.tick() < h.readyAt) return;
    if (size_t(h.current) >= h.ammo.size() || h.ammo[size_t(h.current)] == 0) { fallBack(h); return; }
    Unit& u = w.get<Unit>(hero);
    if (u.flags & (kUnitDisabled | kUnitInHand)) return;
    if (auto* b = w.tryGet<Buffs>(hero); b && b->stunUntil > sim.tick()) return;
    const ArsenalWeapon& aw = rules_.weapons[size_t(h.current)];
    const int16_t wi = aw.index[u.type];
    if (wi < 0) return;
    const UnitType& t = m_->rules().types[u.type];
    const WeaponDef& wd = t.weapons[size_t(wi)];
    const Vec3 pos = w.get<Transform>(hero).pos;
    Vec3 dir = pilot.input.aim ? dirOf(pilot.input.aimAngle) : u.facing;
    dir = fxNormalize(flat(dir));
    if (dir.lengthSq().raw == 0) dir = {Fixed{}, Fixed{}, kOne};
    const Fixed range = wd.range + m_->stat(sim, hero, Stat::Range);
    dir = snap(sim, hero, player, dir, range);
    u.facing = dir;
    const Fixed atkSpeed = fxMax(m_->stat(sim, hero, Stat::AttackSpeed), Fixed::fromRatio(1, 10));
    const int64_t cd = ((wd.cooldown / atkSpeed) * int64_t(sim.tickRate())).toInt();
    h.readyAt = sim.tick() + uint32_t(std::max<int64_t>(1, cd));
    if (h.ammo[size_t(h.current)] > 0) --h.ammo[size_t(h.current)];
    const Fixed damage = wd.damage * m_->stat(sim, hero, Stat::Damage);
    sim.emit({kEvAttack, player, hero, kNullEntity, int64_t(wi), pos});
    const int32_t base = TopDown::angleOf(dir);
    for (int k = 0; k < aw.pellets; ++k) {
        int32_t angle = base;
        if (aw.pellets > 1) angle += int32_t((aw.spread * (2 * k - (aw.pellets - 1)) / (2 * (aw.pellets - 1))).raw);   // even fan
        const Vec3 d = dirOf(angle);
        if (wd.projectileSpeed.raw <= 0) { hitscan(sim, hero, player, size_t(wi), pos, d, damage); continue; }
        Projectile pr;
        pr.source = hero; pr.owner = player; pr.speed = wd.projectileSpeed;
        pr.damage = damage; pr.splash = wd.splash; pr.damageType = wd.damageType;
        pr.layers = uint8_t(wd.targets | (rules_.hitsNeutral ? kLayerHitsNeutral : 0));
        pr.sourceType = u.type; pr.weapon = int8_t(wi);
        const Vec3 start = m_->lift(sim, pos, Fixed::fromRatio(6, 10));
        pr.aim = wd.flight.enabled ? start + d * range : m_->lift(sim, pos + d * range, Fixed::fromRatio(4, 10));   // no program: lands at full range (grenade)
        m_->initProjectileProgram(sim, pr, start);
        Entity p = w.create();
        w.add<Transform>(p, Transform{start, start, Fixed{}});
        w.add<Projectile>(p, pr);
        w.add<Owner>(p, Owner{player});
    }
    if (h.ammo[size_t(h.current)] == 0) fallBack(h);
}

// ================================================================= per tick

void Arsenal::tick(Simulation& sim) {
    auto* st = store(sim);
    if (!st) return;
    World& w = sim.world();
    if (sim.tick() % std::max<uint32_t>(1, sim.tickRate()) == 0) adoptCrates(sim, *st);   // crates spawned by scripts
    TopDown* td = m_->topDown();
    for (size_t pl = 1; pl < st->hands.size(); ++pl) {
        ArsenalHand& h = st->hands[pl];
        const Entity hero = td->heroOf(sim, uint8_t(pl));
        if (!w.alive(hero) || w.get<Unit>(hero).hp.raw <= 0) continue;
        if (hero != h.hero || h.ammo.empty()) loadout(sim, h, hero, uint8_t(pl), h.hero.valid());   // a new hero: respawned
    }
    crates(sim, *st);
    for (size_t pl = 1; pl < st->hands.size(); ++pl) {
        ArsenalHand& h = st->hands[pl];
        const Entity hero = h.hero;
        if (!w.alive(hero) || w.get<Unit>(hero).hp.raw <= 0 || hero != td->heroOf(sim, uint8_t(pl))) continue;
        if (auto* c = w.tryGet<Combat>(hero)) { c->target = kNullEntity; c->autoTarget = false; }   // the hand fires, not the unit's weapon system
        if (m_->driving() && m_->driving()->isDriving(sim, hero)) continue;   // at the wheel the fire button is the handbrake
        fire(sim, h, hero, uint8_t(pl));
    }
}

void Arsenal::neutralKill(Simulation& sim, Entity victim, uint8_t killer) {
    if (!active() || !rules_.payNeutral || killer == 0 || killer == kHostileNeutral) return;
    World& w = sim.world();
    const Unit& vu = w.get<Unit>(victim);
    for (auto& b : m_->rules().types[vu.type].bounty) m_->addResource(sim, killer, b.res, b.value);
}

} // namespace aiverse::game
