// Top-down direct control (rules "topDown", RtsTopDown.h): hero adoption / respawn, sticky quantized input, movement
// with acceleration, wall sliding and unit push-out, facing, aim snap and auto-fire targeting, abilities aimed along
// the stick, a computer pilot for bots. Integer / Fixed maths only; every loop runs in player-slot or entity order.
#include "RtsTopDown.h"
#include "RtsInternal.h"
#include "../../engine/core/Archive.h"
#include "../../engine/core/FixedMath.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {
using namespace detail;
static_assert(kCmdTopDown == net::kCmdHeroInput && kCmdTopDownAbility == net::kCmdHeroAbility, "one hero command scheme");

namespace {
const Fixed kSkin = Fixed::fromRatio(1, 64);   // bodies fit corridors exactly as wide as they are

Fixed cosDegrees(Fixed deg) { return fxCosTurns(deg / 360); }

int typeIndex(const std::vector<std::string>& ids, const std::string& id, const std::string& where) {
    for (size_t i = 0; i < ids.size(); ++i) if (ids[i] == id) return int(i);
    throw std::runtime_error(where + ": unknown type '" + id + "'");
}
std::vector<uint16_t> typeList(const Json& j, const std::vector<std::string>& ids, const std::string& where) {
    std::vector<uint16_t> out;
    if (j.isString()) out.push_back(uint16_t(typeIndex(ids, j.str(), where)));
    else for (auto& it : j.items()) out.push_back(uint16_t(typeIndex(ids, it.str(), where)));
    return out;
}
Fixed nonNegative(const Json& j, const std::string& key, Fixed def, const std::string& where) {
    Fixed v = j.getFx(key, def);
    if (v.raw < 0) throw std::runtime_error(where + "." + key + ": must not be negative");
    return v;
}
} // namespace

// ================================================================= data

TopDownRules TopDown::parse(const Json& j, const std::vector<std::string>& ids) {
    TopDownRules r;
    if (!j.isObject()) return r;
    r.enabled = true;
    r.heroTypes = typeList(j["heroes"], ids, "topDown.heroes");
    if (r.heroTypes.empty()) throw std::runtime_error("topDown.heroes: list at least one unit type");
    r.respawn = nonNegative(j, "respawn", Fixed{}, "topDown");
    if (const Json& mv = j["move"]; mv.isObject()) {
        r.accel = nonNegative(mv, "accel", r.accel, "topDown.move");
        r.decel = nonNegative(mv, "decel", r.decel, "topDown.move");
        r.turnRate = nonNegative(mv, "turnRate", Fixed{}, "topDown.move") / 360;
        r.collideUnits = mv.getBool("collideUnits", true);
    }
    if (const Json& in = j["input"]; in.isObject()) {
        r.sectors = int(in.getInt("sectors", 0));
        r.magnitudeSteps = int(in.getInt("magnitudeSteps", 0));
        if (r.sectors < 0 || r.sectors > 3600) throw std::runtime_error("topDown.input.sectors: 0 (full resolution) or 1..3600");
        if (r.magnitudeSteps < 0 || r.magnitudeSteps > 1000) throw std::runtime_error("topDown.input.magnitudeSteps: 0 (full resolution) or 1..1000");
        Fixed dz = in.getFx("deadzone", Fixed::fromRatio(5, 100));
        if (dz.raw < 0 || dz >= Fixed::fromInt(1)) throw std::runtime_error("topDown.input.deadzone: 0 <= deadzone < 1");
        r.deadzone = int32_t((dz * 1000).toInt());
    }
    const Json& aim = j["aim"];
    {
        Fixed cone = aim.getFx("snapCone", Fixed::fromInt(15));
        if (cone.raw < 0 || cone > Fixed::fromInt(180)) throw std::runtime_error("topDown.aim.snapCone: 0..180 degrees");
        r.snapOn = cone.raw > 0;
        r.snapCos = cosDegrees(cone);
        r.aimFires = aim.getBool("fires", true);
        r.aimRange = nonNegative(aim, "range", Fixed{}, "topDown.aim");
        r.freeFire = aim.getBool("freeFire", false);
    }
    if (const Json& ab = j["abilities"]; ab.isObject()) {
        const Json& hold = ab["hold"];
        if (hold.isString()) r.holdIds.push_back(hold.str());
        else for (auto& it : hold.items()) r.holdIds.push_back(it.str());
        r.rootWhileCasting = ab.getBool("rootWhileCasting", false);
    }
    if (const Json& sq = j["squad"]; sq.isObject()) {
        auto& q = r.squad;
        q.enabled = true;
        q.types = typeList(sq["types"], ids, "topDown.squad.types");
        if (q.types.empty()) throw std::runtime_error("topDown.squad.types: list at least one unit type");
        q.size = int(sq.getInt("size", 0));
        if (q.size < 0 || q.size > 64) throw std::runtime_error("topDown.squad.size: 0 (every unit) or 1..64");
        q.radius = nonNegative(sq, "radius", q.radius, "topDown.squad");
        q.tolerance = nonNegative(sq, "tolerance", q.tolerance, "topDown.squad");
        q.rotate = sq.getBool("rotate", true);
        q.fireWhileMoving = sq.getBool("fireWhileMoving", false);
        q.repathSeconds = nonNegative(sq, "repath", q.repathSeconds, "topDown.squad");
    }
    if (const Json& es = j["escape"]; es.isObject() || es.boolean(false)) {
        auto& e = r.escape;
        e.enabled = es.isObject() ? es.getBool("enabled", true) : true;
        if (es.isObject()) {
            e.duration = nonNegative(es, "duration", e.duration, "topDown.escape");
            e.cooldown = nonNegative(es, "cooldown", e.cooldown, "topDown.escape");
            e.pause = es.getBool("pause", true);
        }
        if (e.enabled && e.duration.raw <= 0) throw std::runtime_error("topDown.escape.duration: must be positive");
    }
    const Json& af = j["autoFire"];
    {
        r.autoFire = af.isObject() ? af.getBool("enabled", true) : af.boolean(true);   // "autoFire": false switches it off
        if (af.isObject()) {
            r.fireWhileMoving = af.getBool("whileMoving", false);
            Fixed cone = af.getFx("cone", Fixed::fromInt(360));
            if (cone.raw <= 0 || cone > Fixed::fromInt(360)) throw std::runtime_error("topDown.autoFire.cone: 0 < cone <= 360 degrees");
            r.autoCos = cone >= Fixed::fromInt(360) ? Fixed::fromInt(-2) : cosDegrees(cone / 2);
            r.autoRange = nonNegative(af, "range", Fixed{}, "topDown.autoFire");
            r.lineOfFire = af.getBool("lineOfFire", true);
            r.preferArmed = af.getBool("preferArmed", false);
            const std::string mode = af.getStr("retarget", "nearest");
            if (mode == "nearest") r.retarget = RetargetMode::Nearest;
            else if (mode == "keep") r.retarget = RetargetMode::Keep;
            else throw std::runtime_error("topDown.autoFire.retarget: 'nearest' or 'keep' (got '" + mode + "')");
            r.retargetSeconds = nonNegative(af, "interval", Fixed{}, "topDown.autoFire");
            r.switchMargin = nonNegative(af, "switchMargin", Fixed{}, "topDown.autoFire");
            r.priority = typeList(af["priority"], ids, "topDown.autoFire.priority");
            r.movingSpeed = nonNegative(af, "movingSpeed", r.movingSpeed, "topDown.autoFire");
        }
    }
    r.extended = r.freeFire || !r.holdIds.empty() || r.rootWhileCasting || r.squad.enabled || r.escape.enabled;
    return r;
}

topdown::Input TopDown::requantize(const TopDownRules& r, topdown::Input in) {
    in.moveMag = topdown::quantizeMag(in.moveMag, r.magnitudeSteps, r.deadzone);
    in.moveAngle = in.moveMag > 0 ? topdown::quantizeAngle(in.moveAngle, r.sectors) : 0;
    in.aimAngle = in.aim ? topdown::quantizeAngle(in.aimAngle, 0) : 0;
    return in;
}

Vec3 TopDown::direction(int32_t angle) {
    const Fixed t = Fixed::fromRaw(int64_t(angle & 0xFFFF));   // Q16 turns: one binary step = one raw unit
    return {fxSinTurns(t), Fixed{}, fxCosTurns(t)};
}

int32_t TopDown::angleOf(Vec3 d) {
    if (d.x.raw == 0 && d.z.raw == 0) return 0;
    return int32_t((fxAtan2Turns(d.x, d.z).raw + topdown::kAngleSteps) & 0xFFFF);
}

// ================================================================= setup / state

TopDownStore* TopDown::store(Simulation& sim) const { return active() ? sim.world().resource<TopDownStore>() : nullptr; }

void TopDown::setup(Simulation& sim, RtsModule& m, const Json& rulesJson) {
    m_ = &m;
    std::vector<std::string> ids;
    for (auto& t : m.rules().types) ids.push_back(t.id);
    rules_ = parse(rulesJson["topDown"], ids);
    if (!active()) return;
    if (m.state(sim).surface) throw std::runtime_error("topDown: planet maps are not supported (flat maps only)");
    rules_.retargetTicks = uint32_t((rules_.retargetSeconds * int64_t(sim.tickRate())).toInt());
    rules_.squad.repathTicks = uint32_t(std::max<int64_t>(1, (rules_.squad.repathSeconds * int64_t(sim.tickRate())).toInt()));
    for (auto& id : rules_.holdIds) {   // hold abilities must exist on some hero type
        bool found = false;
        for (uint16_t ht : rules_.heroTypes) for (auto& a : m.rules().types[ht].abilities) found |= a.id == id;
        if (!found) throw std::runtime_error("topDown.abilities.hold: no hero type has an ability '" + id + "'");
    }
    for (uint16_t ht : rules_.heroTypes)   // windups run in the sim: their state is hashed / saved
        for (auto& a : m.rules().types[ht].abilities) {
            rules_.extended |= a.castTime.raw > 0;
            for (auto& l : a.levels) rules_.extended |= l.castTime.raw > 0;
        }
    auto& st = sim.world().setResource<TopDownStore>();
    st.pilots.assign(sim.players().size() + 1, {});
    for (auto& p : st.pilots) p.heroType = rules_.heroTypes[0];
    respawnAndAdopt(sim, st);
}

void TopDown::adopt(Simulation& sim, TopDownPilot& p, Entity e) {
    World& w = sim.world();
    Unit& u = w.get<Unit>(e);
    p.hero = e;
    p.heroType = u.type;
    p.home = w.get<Transform>(e).pos;
    p.vel = {};
    p.target = kNullEntity;
    p.retargetAt = 0;
    u.flags |= kUnitPossessed;
    if (auto* o = w.tryGet<Orders>(e)) o->queue.clear();
    if (w.has<Mobile>(e)) m_->stopMoving(sim, e);
    if (auto* c = w.tryGet<Combat>(e)) { c->target = kNullEntity; c->autoTarget = false; }
}

void TopDown::respawnAndAdopt(Simulation& sim, TopDownStore& st) {
    World& w = sim.world();
    const uint32_t tick = sim.tick();
    for (size_t pl = 1; pl < st.pilots.size(); ++pl) {
        TopDownPilot& p = st.pilots[pl];
        if (p.hero.valid()) {
            const Unit* u = w.alive(p.hero) ? w.tryGet<Unit>(p.hero) : nullptr;
            if (u && u->hp.raw > 0) continue;
            p.hero = kNullEntity; p.target = kNullEntity; p.vel = {};
            if (rules_.respawn.raw > 0) p.respawnAt = tick + uint32_t(std::max<int64_t>(1, (rules_.respawn * int64_t(sim.tickRate())).toInt()));
            continue;
        }
        auto* pi = sim.player(uint8_t(pl));
        if (!pi || !pi->alive) continue;
        if (p.respawnAt) {
            if (tick < p.respawnAt) continue;
            p.respawnAt = 0;
            Entity e = m_->spawn(sim, p.heroType, uint8_t(pl), p.home);
            if (w.alive(e)) { adopt(sim, p, e); continue; }
        }
        if (tick % 20 != 0 && tick != 0) continue;   // a trained / placed hero is picked up within a second
        Entity found;
        w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
            if (found.valid() || o.player != pl || u.hp.raw <= 0 || (u.flags & (kUnitOffMap | kUnitUnderConstruction | kUnitPossessed))) return;
            if (std::find(rules_.heroTypes.begin(), rules_.heroTypes.end(), u.type) != rules_.heroTypes.end()) found = e;
        });
        if (found.valid()) adopt(sim, p, found);
    }
}

Entity TopDown::heroOf(Simulation& sim, uint8_t player) const {
    auto* st = store(sim);
    if (!st || player >= st->pilots.size()) return kNullEntity;
    Entity e = st->pilots[player].hero;
    return sim.world().alive(e) ? e : kNullEntity;
}

bool TopDown::isHero(Simulation& sim, Entity e) const {
    auto* st = store(sim);
    if (!st) return false;
    for (auto& p : st->pilots) if (p.hero == e) return true;
    return false;
}

bool TopDown::heroVelocity(Simulation& sim, Entity e, Vec3& vel) const {
    auto* st = store(sim);
    if (!st) return false;
    for (auto& p : st->pilots) if (p.hero == e) { vel = p.vel; return true; }
    return false;
}

void TopDown::hash(Simulation& sim, Hasher& h) const {
    auto* st = store(sim);
    if (!st) return;
    for (auto& p : st->pilots) {
        h.u64(p.hero.packed()); h.u32(p.heroType); h.v3(p.home); h.u32(p.respawnAt);
        h.u64(uint64_t(topdown::pack(p.input))); h.u32(p.inputTick);
        h.v3(p.vel); h.u64(p.target.packed()); h.u32(p.retargetAt);
        if (!rules_.extended) continue;   // games without squads / escape / holds / windups keep their checksums
        h.i32(p.held); h.i32(p.heldAngle); h.u32(p.heldDir); h.i32(p.casting); h.u32(p.castAt);
        h.u64(p.castTarget.packed()); h.v3(p.castPoint); h.u32(p.escapeReadyAt);
        h.u32(uint32_t(p.squad.size()));
        for (Entity e : p.squad) h.u64(e.packed());
    }
}

void TopDown::io(Simulation& sim, Archive& a) {
    auto* st = store(sim);
    if (!st) return;
    uint32_t n = uint32_t(st->pilots.size());
    aiverse::io(a, n);
    if (!a.writing() && n != st->pilots.size()) { a.fail(); return; }
    for (auto& p : st->pilots) {
        int64_t in = topdown::pack(p.input);
        a(p.hero, p.heroType, p.home, p.respawnAt, in, p.inputTick, p.vel, p.target, p.retargetAt);
        if (!a.writing()) p.input = topdown::unpack(in);
        if (!rules_.extended) continue;   // the save layout of plain top-down games is unchanged
        a(p.held, p.heldAngle, p.heldDir, p.casting, p.castAt, p.castTarget, p.castPoint, p.escapeReadyAt);
        uint32_t ns = uint32_t(p.squad.size());
        aiverse::io(a, ns);
        if (!a.writing()) { if (ns > 4096) { a.fail(); return; } p.squad.assign(ns, kNullEntity); }
        for (Entity& e : p.squad) a(e);
    }
}

// ================================================================= commands

bool TopDown::command(Simulation& sim, const Command& cmd) {
    if (cmd.type != kCmdTopDown && cmd.type != kCmdTopDownAbility) return false;
    auto* st = store(sim);
    if (!st || cmd.player == 0 || cmd.player >= st->pilots.size()) return true;
    TopDownPilot& p = st->pilots[cmd.player];
    if (cmd.type == kCmdTopDown) {
        p.input = requantize(topdown::unpack(cmd.param));
        p.inputTick = sim.tick();
        return true;
    }
    // kCmdTopDownAbility: topdown::Ability (old clients: param = ability index = a tap); target / point optional
    const topdown::Ability ab = topdown::unpackAbility(cmd.param);
    if (ab.index < 0) return true;
    World& w = sim.world();
    const bool pointGiven = ab.hasPoint || !(cmd.point == Vec3{});
    if (ab.index == topdown::kEscapeAbility) {   // the destination the player picked in the escape preview
        if (!rules_.escape.enabled || !w.alive(p.hero) || (ab.phase != topdown::Phase::Tap && ab.phase != topdown::Phase::Release)) return true;
        Vec3 to = cmd.point;
        if (!pointGiven && ab.hasAngle) {
            const Fixed budget = escapeBudget(sim, cmd.player);
            to = w.get<Transform>(p.hero).pos + direction(ab.angle) * (ab.distance > 0 ? budget * int64_t(ab.distance) / topdown::kMagMax : budget);
        } else if (!pointGiven) return true;
        doEscape(sim, p, cmd.player, flat(to));
        return true;
    }
    if (ab.index >= 8 || !w.alive(p.hero)) return true;
    TopDownCast c;
    c.index = ab.index;
    c.target = w.alive(cmd.target) && w.has<Unit>(cmd.target) ? cmd.target : kNullEntity;
    c.point = cmd.point; c.havePoint = pointGiven;
    c.haveDir = ab.hasAngle; c.angle = ab.angle; c.distance = ab.distance;
    if (!rules_.extended) {   // plain games: every button event that casts is a tap (no hold state to keep)
        if (ab.phase == topdown::Phase::Tap || ab.phase == topdown::Phase::Release) requestCast(sim, p, c);
        return true;
    }
    switch (ab.phase) {
    case topdown::Phase::Tap: requestCast(sim, p, c); break;
    case topdown::Phase::Press:
        p.held = int8_t(ab.index); p.heldDir = ab.hasAngle; p.heldAngle = ab.angle;
        if (holds(sim, p.hero, ab.index)) requestCast(sim, p, c);   // hold: the first shot at once, then repeats
        break;
    case topdown::Phase::Release: {
        const bool wasHeld = p.held == ab.index;
        if (wasHeld) { p.held = -1; p.heldDir = false; p.heldAngle = 0; }
        if (!holds(sim, p.hero, ab.index)) requestCast(sim, p, c);   // aimed buttons cast on release, along their stick
        break;
    }
    case topdown::Phase::Cancel:
        if (p.held == ab.index) { p.held = -1; p.heldDir = false; p.heldAngle = 0; }
        break;
    }
    return true;
}

bool TopDown::holds(Simulation& sim, Entity hero, int index) const {
    if (rules_.holdIds.empty() || index < 0 || !sim.world().alive(hero)) return false;
    const UnitType& t = m_->rules().types[sim.world().get<Unit>(hero).type];
    if (size_t(index) >= t.abilities.size()) return false;
    return std::find(rules_.holdIds.begin(), rules_.holdIds.end(), t.abilities[size_t(index)].id) != rules_.holdIds.end();
}

// Resolves where / at whom an ability goes (button stick, given point / target, aim, current or nearest target),
// then starts its windup (castTime) or casts at once. false: nothing valid to cast at.
bool TopDown::requestCast(Simulation& sim, TopDownPilot& p, const TopDownCast& req) {
    World& w = sim.world();
    const Entity e = p.hero;
    if (!w.alive(e) || p.casting >= 0) return false;   // one windup at a time
    const int idx = req.index;
    if (idx < 0 || idx >= 8 || !m_->canCast(sim, e, idx)) return false;
    const UnitType& t = m_->rules().types[w.get<Unit>(e).type];
    if (size_t(idx) >= t.abilities.size()) return false;
    const AbilityDef& a = m_->rules().advancedEffects ? m_->abilityFor(sim, e, idx) : t.abilities[size_t(idx)];
    const Vec3 pos = w.get<Transform>(e).pos;
    TopDownCast c = req;
    if (a.target == TargetKind::None || a.target == TargetKind::Self) { c.target = e; c.point = pos; }
    else {
        const uint8_t owner = w.get<Owner>(e).player;
        const Vec3 aimDir = c.haveDir ? direction(c.angle) : p.input.aim ? direction(p.input.aimAngle) : w.get<Unit>(e).facing;
        if (a.target == TargetKind::Unit) {
            Entity target = c.target;
            auto nearest = [&](Vec3 coneDir, Fixed coneCos) {   // auto-aim: nearest valid target inside the ability's range
                Entity best; Fixed bestD{};
                m_->state(sim).hash.query(pos, a.range + Fixed::fromInt(4), [&](Entity cand) {
                    Fixed d;
                    if (!validTarget(sim, e, owner, cand, a.range, coneDir, coneCos, &d)) return;
                    if (!best.valid() || d < bestD || (d == bestD && cand.index < best.index)) { best = cand; bestD = d; }
                });
                return best;
            };
            if (!target.valid() && c.haveDir) target = nearest(aimDir, rules_.snapOn ? rules_.snapCos : Fixed::fromInt(1));
            if (!target.valid() && validTarget(sim, e, owner, p.target, a.range, {}, Fixed::fromInt(-2), nullptr)) target = p.target;
            if (!target.valid()) target = nearest({}, Fixed::fromInt(-2));
            if (!target.valid() || m_->contactDistance(sim, e, target) > a.range + kHalf) return false;
            c.target = target; c.point = w.get<Transform>(target).pos;
        } else {
            Vec3 point = c.target.valid() ? w.get<Transform>(c.target).pos : c.point;
            if (!c.target.valid() && (c.haveDir || !c.havePoint)) {   // along the stick (or the aim / facing)
                const Fixed reach = c.haveDir && c.distance > 0 ? a.range * int64_t(c.distance) / topdown::kMagMax : a.range;
                point = pos + aimDir * reach;
            }
            Vec3 off = flat(point - pos);
            Fixed len = off.length();
            if (len > a.range && len.raw > 0) point = pos + off * (a.range / len);   // skill shots stop at their range
            c.target = kNullEntity; c.point = flat(point);
        }
    }
    if (a.castTime.raw > 0) {   // windup: the stick keeps steering (unless rootWhileCasting); stun / silence cancel it
        p.casting = int8_t(idx);
        p.castAt = sim.tick() + uint32_t(std::max<int64_t>(1, (a.castTime * int64_t(sim.tickRate())).toInt()));
        p.castTarget = c.target == e ? kNullEntity : c.target;
        p.castPoint = c.point;
        sim.emit({kEvCastStart, w.get<Owner>(e).player, e, c.target, int64_t(idx), c.point});
        return true;
    }
    castNow(sim, p, c);
    return true;
}

void TopDown::castNow(Simulation& sim, TopDownPilot& p, const TopDownCast& c) {
    m_->castAbility(sim, p.hero, c.index, c.target, c.point);
}

void TopDown::tickCasts(Simulation& sim, TopDownPilot& p) {
    World& w = sim.world();
    const Entity e = p.hero;
    if (p.casting >= 0) {
        const int idx = p.casting;
        if (!w.alive(e) || w.get<Unit>(e).hp.raw <= 0 || !m_->canCast(sim, e, idx)) { p.casting = -1; return; }   // stunned / silenced
        if (sim.tick() < p.castAt) return;
        p.casting = -1;
        const UnitType& t = m_->rules().types[w.get<Unit>(e).type];
        const AbilityDef& a = m_->rules().advancedEffects ? m_->abilityFor(sim, e, idx) : t.abilities[size_t(idx)];
        Entity target = p.castTarget;
        Vec3 point = p.castPoint;
        if (a.target == TargetKind::None || a.target == TargetKind::Self) { target = e; point = w.get<Transform>(e).pos; }
        else if (a.target == TargetKind::Unit) {
            if (!w.alive(target) || !w.has<Unit>(target) || w.get<Unit>(target).hp.raw <= 0) return;   // the target died during the windup
            if (m_->contactDistance(sim, e, target) > a.range + Fixed::fromInt(1)) return;
            point = w.get<Transform>(target).pos;
        }
        p.castTarget = kNullEntity; p.castPoint = {};
        TopDownCast c; c.index = idx; c.target = target; c.point = point;
        castNow(sim, p, c);
        return;
    }
    if (p.held >= 0 && holds(sim, e, p.held) && m_->canCast(sim, e, p.held)) {   // hold buttons repeat while held
        TopDownCast c; c.index = p.held; c.haveDir = p.heldDir; c.angle = p.heldAngle;
        requestCast(sim, p, c);
    }
}

// ================================================================= escape

uint32_t TopDown::escapeReadyAt(Simulation& sim, uint8_t player) const {
    auto* st = store(sim);
    return st && player < st->pilots.size() ? st->pilots[player].escapeReadyAt : 0;
}

Fixed TopDown::escapeBudget(Simulation& sim, uint8_t player) const {
    if (!rules_.escape.enabled) return {};
    const Entity h = heroOf(sim, player);
    if (!h.valid()) return {};
    return m_->stat(sim, h, Stat::Speed) * rules_.escape.duration;
}

bool TopDown::escapeAllowed(Simulation& sim, uint8_t player, Vec3 to, std::string* why) const {
    auto fail = [&](const char* w) { if (why) *why = w; return false; };
    if (!rules_.escape.enabled) return fail("escape is off");
    const Entity h = heroOf(sim, player);
    if (!h.valid() || sim.world().get<Unit>(h).hp.raw <= 0) return fail("no hero");
    if (sim.tick() < escapeReadyAt(sim, player)) return fail("cooling down");
    World& w = sim.world();
    const UnitType& t = m_->rules().types[w.get<Unit>(h).type];
    MapGrid& g = m_->state(sim).grid;
    const Vec3 from = w.get<Transform>(h).pos;
    const Fixed budget = escapeBudget(sim, player);
    if (dist2D(from, to) > budget + kHalf) return fail("too far");
    const int cx = g.cellX(to.x), cy = g.cellY(to.z);
    if (!g.inside(cx, cy) || !g.passable(cx, cy, t.pathLayer)) return fail("blocked");
    if ((t.layer & kLayerAir) && !g.airSolid) return true;
    // the preview walks: the way there must exist and fit the budget (walls are kept, Drone Fall)
    if (walkable(sim, from, to, Fixed{}, t.pathLayer, nullptr)) return true;
    std::vector<Vec3> path;
    if (!g.findPath(from, to, t.pathLayer, path, 4000, MapGrid::clearanceFor(t.radius))) return fail("unreachable");
    Fixed len{};
    Vec3 at = from;
    for (auto& wp : path) { len += dist2D(at, wp); at = wp; }
    if (len > budget + Fixed::fromInt(1)) return fail("too far to walk");
    return true;
}

void TopDown::doEscape(Simulation& sim, TopDownPilot& p, uint8_t player, Vec3 to) {
    if (!escapeAllowed(sim, player, to)) return;
    World& w = sim.world();
    MapGrid& g = m_->state(sim).grid;
    const Entity h = p.hero;
    auto place = [&](Entity e, Vec3 at) {
        Transform& tr = w.get<Transform>(e);
        sim.emit({kEvGroupTeleport, player, e, h, 2, flat(tr.pos)});   // value 2: departure (the effect at the old spot)
        at = flat(at);
        if (g.hf) at.y = g.surfaceHeight(at.x, at.z);
        else at.y = tr.pos.y;
        tr.pos = at; tr.prevPos = at;
        if (w.has<Mobile>(e)) m_->stopMoving(sim, e);
        if (auto* cb = w.tryGet<Combat>(e)) cb->target = kNullEntity;
        sim.emit({kEvGroupTeleport, player, e, h, 0, at});
    };
    place(h, to);
    p.vel = {}; p.target = kNullEntity; p.casting = -1;
    const int n = int(p.squad.size());
    const int32_t facing = rules_.squad.rotate ? angleOf(w.get<Unit>(h).facing) : 0;
    for (int i = 0; i < n; ++i) {
        const Entity m = p.squad[size_t(i)];
        if (!w.alive(m)) continue;
        const Fixed r = m_->rules().types[w.get<Unit>(m).type].radius;
        place(m, squadSlot(sim, to, facing, i, n, r));
    }
    p.escapeReadyAt = sim.tick() + uint32_t(std::max<int64_t>(1, (rules_.escape.cooldown * int64_t(sim.tickRate())).toInt()));
}

// ================================================================= squad

std::vector<Entity> TopDown::squadOf(Simulation& sim, uint8_t player) const {
    auto* st = store(sim);
    std::vector<Entity> out;
    if (!st || player >= st->pilots.size()) return out;
    for (Entity e : st->pilots[player].squad) if (sim.world().alive(e)) out.push_back(e);
    return out;
}

bool TopDown::isFollower(Simulation& sim, Entity e) const {
    if (!rules_.squad.enabled || !sim.world().alive(e)) return false;
    auto* u = sim.world().tryGet<Unit>(e);
    return u && (u->flags & kUnitFollower);
}

bool TopDown::walkable(Simulation& sim, Vec3 from, Vec3 to, Fixed radius, uint8_t layer, Vec3* stop) const {
    const MapGrid& g = m_->state(sim).grid;
    const Vec3 d = flat(to - from);
    const Fixed len = d.length();
    if (stop) *stop = to;
    if (len.raw <= 0) return true;
    const int n = std::min(512, 1 + int((len * 4).toInt()));   // quarter-cell steps
    Vec3 last = from;
    for (int i = 1; i <= n; ++i) {
        const Vec3 q = from + d * Fixed::fromRatio(i, n);
        const int cx = g.cellX(q.x), cy = g.cellY(q.z);
        if (!g.inside(cx, cy) || !g.passable(cx, cy, layer)) {
            if (stop) {   // back off the body radius from the last free point (never behind the start)
                const Vec3 back = flat(last - from);
                const Fixed bl = back.length();
                *stop = bl > radius && bl.raw > 0 ? from + back * ((bl - radius) / bl) : from;
            }
            return false;
        }
        last = q;
    }
    return true;
}

Vec3 TopDown::squadSlot(Simulation& sim, Vec3 centre, int32_t facingAngle, int i, int n, Fixed memberRadius) const {
    if (n <= 0) return centre;
    // ring around the hero, member 0 straight behind it (Drone Fall GetCirclePosition over the alive members)
    const int32_t angle = int32_t((int64_t(facingAngle) + 32768 + int64_t(i) * topdown::kAngleSteps / n) & 0xFFFF);
    const Vec3 slot = flat(centre) + direction(angle) * rules_.squad.radius;
    const uint8_t layer = rules_.squad.types.empty() ? uint8_t(kLayerGround) : m_->rules().types[rules_.squad.types[0]].pathLayer;
    Vec3 stop = slot;
    walkable(sim, flat(centre), slot, memberRadius, layer, &stop);   // a wall in between: the slot moves in front of it
    return stop;
}

void TopDown::adoptSquad(Simulation& sim, TopDownStore& st) {
    World& w = sim.world();
    for (size_t pl = 1; pl < st.pilots.size(); ++pl) {
        TopDownPilot& p = st.pilots[pl];
        // alive members stay in order at the front; the dead drop out (their slots close up)
        p.squad.erase(std::remove_if(p.squad.begin(), p.squad.end(), [&](Entity e) {
            const Unit* u = w.alive(e) ? w.tryGet<Unit>(e) : nullptr;
            return !u || u->hp.raw <= 0 || w.get<Owner>(e).player != pl;
        }), p.squad.end());
        if (rules_.squad.size > 0 && int(p.squad.size()) >= rules_.squad.size) continue;
        if (sim.tick() % 20 != 0 && sim.tick() != 0) continue;   // new / trained members join within a second
        w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
            if (o.player != pl || u.hp.raw <= 0 || e == p.hero) return;
            if (u.flags & (kUnitOffMap | kUnitUnderConstruction | kUnitPossessed | kUnitFollower)) return;
            if (rules_.squad.size > 0 && int(p.squad.size()) >= rules_.squad.size) return;
            if (std::find(rules_.squad.types.begin(), rules_.squad.types.end(), u.type) == rules_.squad.types.end()) return;
            if (m_->rules().types[u.type].structure || !w.has<Mobile>(e)) return;
            u.flags |= kUnitFollower | kUnitHold;   // hold: fights from its slot, never chases
            if (auto* o2 = w.tryGet<Orders>(e)) o2->queue.clear();
            p.squad.push_back(e);
        });
    }
}

void TopDown::tickSquad(Simulation& sim, TopDownPilot& p, uint8_t) {
    World& w = sim.world();
    if (p.squad.empty() || !w.alive(p.hero)) return;
    const Vec3 centre = w.get<Transform>(p.hero).pos;
    const int32_t facing = rules_.squad.rotate ? angleOf(w.get<Unit>(p.hero).facing) : 0;
    const bool heroMoving = p.vel.length() > rules_.movingSpeed;
    const int n = int(p.squad.size());
    for (int i = 0; i < n; ++i) {
        const Entity e = p.squad[size_t(i)];
        if (!w.alive(e)) continue;
        if (heroMoving && !rules_.squad.fireWhileMoving)   // Drone Fall SupressFireOnMove: the group holds fire while it moves
            if (auto* c = w.tryGet<Combat>(e)) c->target = kNullEntity;
        auto* m = w.tryGet<Mobile>(e);
        if (!m) continue;
        const Fixed r = m_->rules().types[w.get<Unit>(e).type].radius;
        const Vec3 slot = squadSlot(sim, centre, facing, i, n, r);
        const Fixed d = dist2D(w.get<Transform>(e).pos, slot);
        if (d <= rules_.squad.tolerance) { if (m->moving) m_->stopMoving(sim, e); continue; }
        const bool drifted = dist2D(m->goal, slot) > rules_.squad.tolerance;
        if (!m->moving || (drifted && (sim.tick() + uint32_t(i)) % rules_.squad.repathTicks == 0))
            m_->moveTo(sim, e, slot, rules_.squad.tolerance / 2);
    }
}

// ================================================================= free fire

// Aiming (or fire held) with nothing to shoot at: the weapons fire along the aim. A weapon with a flight program
// flies it (the program stops on bodies / walls); otherwise the shot travels to the first hostile body on the line
// or the first wall, whichever comes first, within the weapon range.
void TopDown::freeFire(Simulation& sim, TopDownPilot& p, uint8_t owner) {
    World& w = sim.world();
    const Entity e = p.hero;
    auto* c = w.tryGet<Combat>(e);
    if (!c) return;
    if (auto* b = w.tryGet<Buffs>(e); b && b->stunUntil > sim.tick()) return;
    if (m_->rules().advancedEffects && (m_->statusFlags(sim, e) & (kStStun | kStDisarm))) return;
    const Unit& u = w.get<Unit>(e);
    const UnitType& t = m_->rules().types[u.type];
    const Vec3 pos = w.get<Transform>(e).pos;
    const Vec3 dir = p.input.aim ? direction(p.input.aimAngle) : u.facing;
    const MapGrid& g = m_->state(sim).grid;
    const Fixed mult = m_->stat(sim, e, Stat::Damage);
    const Fixed bonus = m_->stat(sim, e, Stat::Range);
    for (size_t i = 0; i < t.weapons.size() && i < 4; ++i) {
        const WeaponDef& wd = t.weapons[i];
        if (c->cooldown[i].raw > 0) continue;
        const Fixed range = wd.range + bonus;
        // march the line in quarter cells: the first wall ends it; the first hostile body on it is hit
        Entity hit;
        Vec3 end = pos + dir * range;
        const int n = std::min(1024, 1 + int((range * 4).toInt()));
        const bool air = (t.layer & kLayerAir) && !g.airSolid;
        for (int k = 1; k <= n && !hit.valid(); ++k) {
            const Vec3 q = pos + dir * (range * int64_t(k) / int64_t(n));
            const int cx = g.cellX(q.x), cy = g.cellY(q.z);
            if (!g.inside(cx, cy) || (!air && !g.passable(cx, cy, kLayerGround) && !g.passable(cx, cy, kLayerNaval))) {
                end = pos + dir * (range * int64_t(k - 1) / int64_t(n));
                break;
            }
            Fixed bestD{};
            m_->state(sim).hash.query(q, Fixed::fromInt(2), [&](Entity cand) {
                if (cand == e || !w.alive(cand)) return;
                const Unit* cu = w.tryGet<Unit>(cand);
                const Owner* co = w.tryGet<Owner>(cand);
                if (!cu || !co || cu->hp.raw <= 0 || (cu->flags & kUnitOffMap)) return;
                const UnitType& ct = m_->rules().types[cu->type];
                if (ct.invulnerable || !m_->hostile(sim, owner, co->player) || !layerHit(wd.targets, ct.layer)) return;
                const Fixed d = dist2D(w.get<Transform>(cand).pos, q);
                if (d > ct.radius + Fixed::fromRatio(1, 4)) return;
                if (!hit.valid() || d < bestD || (d == bestD && cand.index < hit.index)) { hit = cand; bestD = d; }
            });
            if (hit.valid()) end = flat(w.get<Transform>(hit).pos);
        }
        c->cooldown[i] = wd.cooldown;
        sim.emit({kEvAttack, owner, e, hit, int64_t(i), pos});
        const Fixed dmg = wd.damage * mult;
        if (wd.projectileSpeed.raw <= 0 && !wd.flight.enabled) {   // instant weapons hit at once
            if (hit.valid()) m_->dealDamage(sim, hit, dmg, wd.damageType, e, owner, &wd);
            continue;
        }
        Projectile pr;
        pr.source = e; pr.speed = wd.projectileSpeed.raw > 0 ? wd.projectileSpeed : Fixed::fromInt(20);
        pr.damage = dmg; pr.splash = wd.splash; pr.damageType = wd.damageType; pr.owner = owner; pr.layers = wd.targets;
        pr.sourceType = u.type; pr.weapon = int8_t(i);
        pr.target = wd.flight.enabled ? kNullEntity : hit;   // programs find their own victims
        pr.aim = m_->lift(sim, wd.flight.enabled ? pos + dir * range : end, Fixed::fromRatio(4, 10));
        const Vec3 start = m_->lift(sim, pos, Fixed::fromRatio(6, 10));
        Entity pe = w.create();
        w.add<Transform>(pe, Transform{start, start, Fixed{}});
        m_->initProjectileProgram(sim, pr, start);
        w.add<Projectile>(pe, pr);
        w.add<Owner>(pe, Owner{owner});
    }
}

// ================================================================= per tick

void TopDown::tick(Simulation& sim) {
    auto* st = store(sim);
    if (!st) return;
    respawnAndAdopt(sim, *st);
    if (rules_.squad.enabled) adoptSquad(sim, *st);
    World& w = sim.world();
    for (size_t pl = 1; pl < st->pilots.size(); ++pl) {
        TopDownPilot& p = st->pilots[pl];
        if (!w.alive(p.hero) || w.get<Unit>(p.hero).hp.raw <= 0) { p.casting = -1; p.held = -1; continue; }
        if (rules_.extended) tickCasts(sim, p);
        moveHero(sim, p);
        aimHero(sim, p, uint8_t(pl));
        if (rules_.squad.enabled) tickSquad(sim, p, uint8_t(pl));
    }
}

void TopDown::moveHero(Simulation& sim, TopDownPilot& p) {
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    MapGrid& g = gs.grid;
    const Entity e = p.hero;
    Unit& u = w.get<Unit>(e);
    Transform& tr = w.get<Transform>(e);
    const UnitType& t = m_->rules().types[u.type];
    const Fixed dt = sim.dt();
    auto* b = w.tryGet<Buffs>(e);
    const uint32_t fl = m_->rules().advancedEffects ? m_->statusFlags(sim, e) : 0;
    const bool stunned = (b && b->stunUntil > sim.tick()) || (fl & kStStun);
    const bool rooted = stunned || (fl & kStRoot);

    // velocity toward the stick: accelerate up, brake (decel) when slowing down or reversing
    Vec3 wish{};
    const bool castRoot = rules_.rootWhileCasting && p.casting >= 0;
    if (!rooted && !castRoot && p.input.moveMag > 0) wish = direction(p.input.moveAngle) * (m_->stat(sim, e, Stat::Speed) * p.input.moveMag / topdown::kMagMax);
    {
        const Vec3 dv = wish - p.vel;
        const Fixed dl = dv.length();
        const bool speedUp = wish.lengthSq() >= p.vel.lengthSq() && wish.dot(p.vel).raw >= 0;
        const Fixed rate = speedUp ? rules_.accel : rules_.decel;
        if (rate.raw <= 0 || dl <= rate * dt) p.vel = wish;
        else p.vel += dv * (rate * dt / dl);
    }

    // movement over the grid: the body box must stay on passable cells (cliffs: canStep between centre cells);
    // a blocked axis is dropped (slide along walls). A hero inside a blocked cell may walk out over passable centres.
    const bool free3d = (t.layer & kLayerAir) && !g.airSolid;
    const Fixed r = fxMax(t.radius - kSkin, Fixed{});
    auto boxFree = [&](Vec3 q) {
        if (free3d) return q.x.raw >= 0 && q.z.raw >= 0 && q.x < Fixed::fromInt(g.w) && q.z < Fixed::fromInt(g.h);
        const int x0 = g.cellX(q.x - r), x1 = g.cellX(q.x + r), y0 = g.cellY(q.z - r), y1 = g.cellY(q.z + r);
        for (int y = y0; y <= y1; ++y)
            for (int x = x0; x <= x1; ++x) if (!g.passable(x, y, t.pathLayer)) return false;
        return true;
    };
    auto stepOk = [&](Vec3 from, Vec3 to) {
        if (free3d) return boxFree(to);
        const int cx0 = g.cellX(from.x), cy0 = g.cellY(from.z), cx = g.cellX(to.x), cy = g.cellY(to.z);
        if (!g.passable(cx, cy, t.pathLayer)) return false;
        if (cx != cx0 || cy != cy0) {
            if (cx != cx0 && cy != cy0) {
                if (!g.canStep(cx0, cy0, cx, cy0, t.pathLayer) || !g.canStep(cx, cy0, cx, cy, t.pathLayer)) return false;
            } else if (g.passable(cx0, cy0, t.pathLayer) && !g.canStep(cx0, cy0, cx, cy, t.pathLayer)) return false;
        }
        return boxFree(to) || !boxFree(from);
    };
    Vec3 pos = tr.pos;
    const Vec3 d = flat(p.vel * dt);
    const Fixed dlen = d.length();
    if (dlen.raw > 0) {
        const int n = std::min(16, 1 + int((dlen * 4).toInt()));   // sub-steps of at most a quarter cell
        const Vec3 s = d / Fixed::fromInt(n);
        for (int i = 0; i < n; ++i) {
            const Vec3 nx = pos + Vec3{s.x, Fixed{}, Fixed{}}, nz = pos + Vec3{Fixed{}, Fixed{}, s.z};
            if (stepOk(pos, pos + s)) pos = pos + s;
            else if (s.x.raw != 0 && stepOk(pos, nx)) { pos = nx; p.vel.z = Fixed{}; }
            else if (s.z.raw != 0 && stepOk(pos, nz)) { pos = nz; p.vel.x = Fixed{}; }
            else { p.vel = {}; break; }
        }
    }

    // bodies: push out of overlapping units of the same layer (structures block through the grid)
    if (rules_.collideUnits) {
        struct Near { Entity e; Vec3 p; Fixed r; };
        std::vector<Near> near;
        const bool air = t.layer & kLayerAir;
        gs.hash.query(pos, t.radius + Fixed::fromInt(2), [&](Entity o) {
            if (o == e) return;
            auto* ou = w.tryGet<Unit>(o);
            if (!ou || ou->hp.raw <= 0 || (ou->flags & kUnitOffMap)) return;
            const UnitType& ot = m_->rules().types[ou->type];
            if (ot.structure || bool(ot.layer & kLayerAir) != air) return;
            near.push_back({o, w.get<Transform>(o).pos, ot.radius});
        });
        std::sort(near.begin(), near.end(), [](const Near& a, const Near& c) { return a.e.index < c.e.index; });
        for (auto& o : near) {
            Vec3 delta = flat(pos - o.p);
            Fixed dist = delta.length();
            const Fixed minD = t.radius + o.r;
            if (dist >= minD) continue;
            Vec3 nrm = dist.raw > 0 ? delta / dist : u.facing;
            Vec3 np = pos + nrm * (minD - dist);
            if (stepOk(pos, np)) pos = np;
            const Fixed into = -p.vel.dot(nrm);
            if (into.raw > 0) p.vel += nrm * into;   // no velocity left pushing into the body
        }
    }
    tr.pos = pos;
    if (auto* mb = w.tryGet<Mobile>(e)) { mb->moving = false; mb->vel = p.vel; }

    // facing: the aim, else the way it moves; turnRate limits the turn per tick
    Vec3 want{};
    if (p.input.aim) want = direction(p.input.aimAngle);
    else if (p.vel.length() > rules_.movingSpeed) want = p.vel;
    if (stunned || (want.x.raw == 0 && want.z.raw == 0)) return;
    const int32_t goal = angleOf(want);
    if (rules_.turnRate.raw <= 0) { u.facing = direction(goal); return; }
    const int32_t cur = angleOf(u.facing);
    int32_t diff = ((goal - cur) % 65536 + 65536 + 32768) % 65536 - 32768;
    const int32_t maxStep = int32_t(std::max<int64_t>(1, (rules_.turnRate * dt).raw));   // turns raw == binary steps
    diff = std::clamp(diff, -maxStep, maxStep);
    u.facing = direction(cur + diff);
}

Fixed TopDown::weaponRange(Simulation& sim, Entity hero) const {
    const UnitType& t = m_->rules().types[sim.world().get<Unit>(hero).type];
    return maxWeaponRange(t) + m_->stat(sim, hero, Stat::Range);
}

bool TopDown::lineOfFire(Simulation& sim, Vec3 from, Entity target) const {
    World& w = sim.world();
    const MapGrid& g = m_->state(sim).grid;
    const Vec3 to = w.get<Transform>(target).pos;
    const Footprint* fp = w.tryGet<Footprint>(target);
    int x = g.cellX(from.x), y = g.cellY(from.z);
    const int x1 = g.cellX(to.x), y1 = g.cellY(to.z);
    const int dx = std::abs(x1 - x), dy = std::abs(y1 - y), sx = x < x1 ? 1 : -1, sy = y < y1 ? 1 : -1;
    int err = dx - dy;
    // walls: cells neither ground nor water units can enter (rock, structures); the target's own cells don't count
    auto opaque = [&](int cx, int cy) {
        if (fp && cx >= fp->x && cx < fp->x + fp->w && cy >= fp->y && cy < fp->y + fp->h) return false;
        return !g.passable(cx, cy, kLayerGround) && !g.passable(cx, cy, kLayerNaval);
    };
    while (x != x1 || y != y1) {
        const int e2 = 2 * err;
        if (e2 > -dy) { err -= dy; x += sx; }
        if (e2 < dx) { err += dx; y += sy; }
        if ((x != x1 || y != y1) && opaque(x, y)) return false;
    }
    return true;
}

bool TopDown::validTarget(Simulation& sim, Entity hero, uint8_t owner, Entity c, Fixed range, Vec3 coneDir, Fixed coneCos, Fixed* dist) const {
    World& w = sim.world();
    if (c == hero || !w.alive(c)) return false;
    const Unit* cu = w.tryGet<Unit>(c);
    const Owner* co = w.tryGet<Owner>(c);
    if (!cu || !co || cu->hp.raw <= 0 || (cu->flags & kUnitOffMap)) return false;
    const Rules& R = m_->rules();
    const UnitType& ct = R.types[cu->type];
    if (ct.invulnerable || !m_->hostile(sim, owner, co->player)) return false;
    uint8_t layers = 0;
    for (auto& wd : R.types[w.get<Unit>(hero).type].weapons) layers |= wd.targets;
    if (layers && !layerHit(layers, ct.layer)) return false;
    const Vec3 cp = w.get<Transform>(c).pos;
    if (R.advancedEffects) { uint32_t f = m_->statusFlags(sim, c); if ((f & kStInvisible) && !(f & kStRevealed)) return false; }
    if (!m_->isVisible(sim, owner, cp)) return false;
    const Fixed d = m_->contactDistance(sim, hero, c);
    if (d > range) return false;
    const Vec3 hp = w.get<Transform>(hero).pos;
    if (coneCos >= Fixed::fromInt(-1)) {
        Vec3 off = flat(cp - hp);
        Fixed l = off.length();
        if (l.raw > 0 && off.dot(coneDir) < coneCos * l) return false;
    }
    if (rules_.lineOfFire && !lineOfFire(sim, hp, c)) return false;
    if (dist) *dist = d;
    return true;
}

void TopDown::aimHero(Simulation& sim, TopDownPilot& p, uint8_t owner) {
    World& w = sim.world();
    const Entity e = p.hero;
    auto* c = w.tryGet<Combat>(e);
    if (!c) return;
    c->autoTarget = false;
    const Unit& u = w.get<Unit>(e);
    const Vec3 pos = w.get<Transform>(e).pos;
    const Fixed range = weaponRange(sim, e);
    const Rules& R = m_->rules();
    // best target in range / cone: priority types first, armed before harmless (preferArmed), then the nearest
    auto search = [&](Fixed r, Vec3 coneDir, Fixed coneCos, Fixed& bestD) {
        Entity best;
        int bestRank = 0;
        m_->state(sim).hash.query(pos, r + Fixed::fromInt(6), [&](Entity cand) {
            Fixed d;
            if (!validTarget(sim, e, owner, cand, r, coneDir, coneCos, &d)) return;
            const uint16_t ct = w.get<Unit>(cand).type;
            int rank = int(std::find(rules_.priority.begin(), rules_.priority.end(), ct) - rules_.priority.begin()) * 2;
            if (rules_.preferArmed && R.types[ct].weapons.empty()) rank += 1;
            if (!best.valid() || rank < bestRank || (rank == bestRank && (d < bestD || (d == bestD && cand.index < best.index)))) {
                best = cand; bestD = d; bestRank = rank;
            }
        });
        return best;
    };
    const bool moving = p.vel.length() > rules_.movingSpeed;
    if (p.input.aim) {   // manual aim: snap to the nearest target in the cone around the stick
        Entity want;
        if (rules_.aimFires || p.input.fire) {
            Fixed bd{};
            const Fixed r = rules_.aimRange.raw > 0 ? rules_.aimRange : range;
            want = search(r, direction(p.input.aimAngle), rules_.snapOn ? rules_.snapCos : Fixed::fromInt(1), bd);
        }
        p.target = want;
        p.retargetAt = 0;
    } else if ((rules_.autoFire && (rules_.fireWhileMoving || !moving)) || p.input.fire) {
        const Fixed r = rules_.autoRange.raw > 0 ? rules_.autoRange : range;
        Fixed curD{};
        const bool curOk = validTarget(sim, e, owner, p.target, r, u.facing, rules_.autoCos, &curD);
        const bool hold = curOk && (rules_.retarget == RetargetMode::Keep || sim.tick() < p.retargetAt);
        if (!hold) {
            Fixed bd{};
            Entity best = search(r, u.facing, rules_.autoCos, bd);
            // nearest: switch only to a target that is closer by more than switchMargin (same priority class)
            const bool stay = curOk && best.valid() && best != p.target && bd + rules_.switchMargin >= curD &&
                              std::find(rules_.priority.begin(), rules_.priority.end(), w.get<Unit>(best).type) ==
                                  std::find(rules_.priority.begin(), rules_.priority.end(), w.get<Unit>(p.target).type);
            if (!stay) p.target = best;
            p.retargetAt = sim.tick() + rules_.retargetTicks;
        }
    } else {
        p.target = kNullEntity;
    }
    c->target = p.target;
    // free fire: aiming (with aimFires) or fire held, nothing to shoot at -> the weapons fire along the aim
    if (rules_.freeFire && !p.target.valid() && ((p.input.aim && rules_.aimFires) || p.input.fire)) freeFire(sim, p, owner);
}

// ================================================================= computer pilot

bool TopDown::botCommand(Simulation& sim, uint8_t player, Command& out) {
    auto* st = store(sim);
    if (!st || player == 0 || player >= st->pilots.size()) return false;
    TopDownPilot& p = st->pilots[player];
    World& w = sim.world();
    if (!w.alive(p.hero)) return false;
    const Entity e = p.hero;
    const Vec3 pos = w.get<Transform>(e).pos;
    // nearest hostile thing (heroes and armed units count as nearer: they shoot back)
    Entity foe;
    Fixed foeD{};
    w.each<Unit, Owner, Transform>([&](Entity c, Unit& cu, Owner& co, Transform& ct) {
        if (cu.hp.raw <= 0 || (cu.flags & kUnitOffMap) || co.player == 0 || !m_->hostile(sim, player, co.player)) return;
        const UnitType& ty = m_->rules().types[cu.type];
        if (ty.invulnerable) return;
        Fixed d = dist2D(ct.pos, pos) + (ty.weapons.empty() ? Fixed::fromInt(4) : Fixed{});
        if (!foe.valid() || d < foeD) { foe = c; foeD = d; }
    });
    topdown::Input in;
    if (foe.valid()) {
        const Fixed range = weaponRange(sim, e);
        const Fixed d = m_->contactDistance(sim, e, foe);
        const Vec3 fp = w.get<Transform>(foe).pos;
        const bool armed = !m_->rules().types[w.get<Unit>(foe).type].weapons.empty();
        Vec3 dir{};
        if (d > range * Fixed::fromRatio(85, 100) || (rules_.lineOfFire && !lineOfFire(sim, pos, foe))) {
            dir = walkDir(sim, e, fp);
        } else if (armed && d < range * Fixed::fromRatio(35, 100)) {
            dir = flat(pos - fp);   // too close to something that shoots back: give ground
        }
        if (dir.x.raw != 0 || dir.z.raw != 0) { in.moveAngle = angleOf(dir); in.moveMag = int32_t(topdown::kMagMax); }
    }
    in = requantize(in);
    if (in == p.input) return false;
    out = Command{};
    out.type = kCmdTopDown;
    out.player = player;
    out.param = topdown::pack(in);
    return true;
}

Vec3 TopDown::walkDir(Simulation& sim, Entity e, Vec3 to) const {
    World& w = sim.world();
    const Vec3 pos = w.get<Transform>(e).pos;
    const UnitType& t = m_->rules().types[w.get<Unit>(e).type];
    MapGrid& g = m_->state(sim).grid;
    if (g.lineWalkable(g.cellX(pos.x), g.cellY(pos.z), g.cellX(to.x), g.cellY(to.z), t.pathLayer)) return flat(to - pos);
    std::vector<Vec3> path;
    int gx = g.cellX(to.x), gy = g.cellY(to.z);   // structures stand on blocked cells: walk to a free one next to it
    Vec3 goal = to;
    if (!g.passable(gx, gy, t.pathLayer) && g.nearestPassable(gx, gy, t.pathLayer)) goal = g.cellCenter(gx, gy);
    if (g.findPath(pos, goal, t.pathLayer, path, 4000, MapGrid::clearanceFor(t.radius)))
        for (auto& wp : path) if (dist2D(wp, pos) > kHalf) return flat(wp - pos);
    return {};
}

bool TopDown::steerCommand(Simulation& sim, uint8_t player, Vec3 goal, Fixed stand, Command& out, Fixed kite) {
    auto* st = store(sim);
    if (!st || player == 0 || player >= st->pilots.size()) return false;
    TopDownPilot& p = st->pilots[player];
    World& w = sim.world();
    if (!w.alive(p.hero)) return false;
    const Entity e = p.hero;
    const Vec3 pos = w.get<Transform>(e).pos;
    topdown::Input in;
    Vec3 dir{};
    if (dist2D(pos, flat(goal)) > fxMax(stand, Fixed::fromRatio(1, 4))) dir = walkDir(sim, e, flat(goal));
    // kite: dangerous foes about to reach the hero push the way out (away from all of them, nearer ones weigh more,
    // still drifting toward the goal); a wall ahead turns the escape by 45 / 90 / 135 degrees to the open side.
    // Dangerous = a single hit of at least 5% of the hero's health (Drone Fall guardians' telegraphed clubs), or a
    // swarm (6+ armed foes in reach). A foe is "about to reach" inside its weapon range + 1.5 (kite > 0: inside
    // `kite`). Otherwise the hero stands and shoots (Drone Fall heroes only fire standing still) or walks on.
    const Fixed maxHp = m_->stat(sim, e, Stat::MaxHp);
    Vec3 away{}, swarmAway{};
    int threats = 0, swarm = 0;
    m_->state(sim).hash.query(pos, Fixed::fromInt(12), [&](Entity c) {
        const Unit* cu = w.tryGet<Unit>(c);
        const Owner* co = w.tryGet<Owner>(c);
        if (!cu || !co || cu->hp.raw <= 0 || (cu->flags & kUnitOffMap) || co->player == 0 || !m_->hostile(sim, player, co->player)) return;
        const UnitType& ct = m_->rules().types[cu->type];
        if (ct.weapons.empty()) return;
        Fixed reach{}, hit{};
        for (auto& wd : ct.weapons) { reach = fxMax(reach, wd.range); hit = fxMax(hit, wd.damage); }
        const Fixed keep = kite.raw > 0 ? kite : reach + Fixed::fromRatio(5, 2);
        const Fixed d = m_->contactDistance(sim, e, c);
        if (d >= keep) return;
        Vec3 off = flat(pos - w.get<Transform>(c).pos);
        const Fixed l = off.length();
        if (l.raw <= 0) return;
        const Vec3 push = off / l * (keep - d + Fixed::fromRatio(1, 4));   // order-independent sums (fixed point, exact)
        swarmAway += push; ++swarm;
        if (kite.raw > 0 || hit * 20 >= maxHp) { away += push; ++threats; }
    });
    if (!threats && swarm >= 6) { away = swarmAway; threats = swarm; }
    if (threats) {
        const Fixed al = away.length();
        const Fixed dl = dir.length();
        Vec3 flee = al.raw > 0 ? away / al * Fixed::fromInt(2) : Vec3{};
        if (dl.raw > 0) flee += dir / dl;
        if (flee.x.raw != 0 || flee.z.raw != 0) {
            const UnitType& t = m_->rules().types[w.get<Unit>(e).type];
            const int32_t base = angleOf(flee);
            const Fixed probe = fxMax(Fixed::fromInt(2), t.radius * 3);
            const int32_t turns[7] = {0, 8192, -8192, 16384, -16384, 24576, -24576};
            dir = flee;   // nothing open: flee anyway (movement slides along walls)
            const MapGrid& g = m_->state(sim).grid;
            if (!((t.layer & kLayerAir) && !g.airSolid))
            for (int32_t tr : turns) {
                const int32_t ang = (base + tr) & 0xFFFF;
                if (walkable(sim, pos, pos + direction(ang) * probe, Fixed{}, t.pathLayer, nullptr)) { dir = direction(ang); break; }
            }
        }
    }
    if (dir.x.raw != 0 || dir.z.raw != 0) { in.moveAngle = angleOf(dir); in.moveMag = int32_t(topdown::kMagMax); }
    in = requantize(in);
    if (in == p.input) return false;
    out = Command{};
    out.type = kCmdTopDown;
    out.player = player;
    out.param = topdown::pack(in);
    return true;
}

} // namespace aiverse::game
