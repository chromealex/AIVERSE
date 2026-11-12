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
    // kCmdTopDownAbility: param = ability index of the hero's type; target / point optional (aimed along the stick)
    World& w = sim.world();
    const Entity e = p.hero;
    if (!w.alive(e) || cmd.param < 0 || cmd.param >= 8) return true;
    const int idx = int(cmd.param);
    if (!m_->canCast(sim, e, idx)) return true;
    const UnitType& t = m_->rules().types[w.get<Unit>(e).type];
    const AbilityDef& a = m_->rules().advancedEffects ? m_->abilityFor(sim, e, idx) : t.abilities[size_t(idx)];
    const Vec3 pos = w.get<Transform>(e).pos;
    if (a.target == TargetKind::None || a.target == TargetKind::Self) { m_->castAbility(sim, e, idx, e, pos); return true; }
    const uint8_t owner = w.get<Owner>(e).player;
    Entity target = w.alive(cmd.target) && w.has<Unit>(cmd.target) ? cmd.target : kNullEntity;
    if (a.target == TargetKind::Unit) {
        if (!target.valid() && validTarget(sim, e, owner, p.target, a.range, {}, Fixed::fromInt(-2), nullptr)) target = p.target;
        if (!target.valid()) {   // auto-aim: nearest valid target inside the ability's range
            Fixed best{};
            m_->state(sim).hash.query(pos, a.range + Fixed::fromInt(4), [&](Entity c) {
                Fixed d;
                if (!validTarget(sim, e, owner, c, a.range, {}, Fixed::fromInt(-2), &d)) return;
                if (!target.valid() || d < best || (d == best && c.index < target.index)) { target = c; best = d; }
            });
        }
        if (!target.valid() || m_->contactDistance(sim, e, target) > a.range + kHalf) return true;
        m_->castAbility(sim, e, idx, target, w.get<Transform>(target).pos);
        return true;
    }
    Vec3 point = target.valid() ? w.get<Transform>(target).pos : cmd.point;
    if (!target.valid() && cmd.point == Vec3{}) {   // no point: along the aim (or the facing), at full range
        Vec3 dir = p.input.aim ? direction(p.input.aimAngle) : w.get<Unit>(e).facing;
        point = pos + dir * a.range;
    }
    Vec3 off = flat(point - pos);
    Fixed len = off.length();
    if (len > a.range && len.raw > 0) point = pos + off * (a.range / len);   // skill shots stop at their range
    m_->castAbility(sim, e, idx, kNullEntity, flat(point));
    return true;
}

// ================================================================= per tick

void TopDown::tick(Simulation& sim) {
    auto* st = store(sim);
    if (!st) return;
    respawnAndAdopt(sim, *st);
    World& w = sim.world();
    for (size_t pl = 1; pl < st->pilots.size(); ++pl) {
        TopDownPilot& p = st->pilots[pl];
        if (!w.alive(p.hero) || w.get<Unit>(p.hero).hp.raw <= 0) continue;
        if (m_->driving() && m_->driving()->isDriving(sim, p.hero)) continue;   // at the wheel: RtsDriving.cpp reads the stick
        moveHero(sim, p);
        aimHero(sim, p, uint8_t(pl));
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
    if (!rooted && p.input.moveMag > 0) wish = direction(p.input.moveAngle) * (m_->stat(sim, e, Stat::Speed) * p.input.moveMag / topdown::kMagMax);
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
    const UnitType& t = m_->rules().types[w.get<Unit>(e).type];
    GameState& gs = m_->state(sim);
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
            MapGrid& g = gs.grid;
            if (g.lineWalkable(g.cellX(pos.x), g.cellY(pos.z), g.cellX(fp.x), g.cellY(fp.z), t.pathLayer)) dir = flat(fp - pos);
            else {
                std::vector<Vec3> path;
                int gx = g.cellX(fp.x), gy = g.cellY(fp.z);   // structures stand on blocked cells: walk to a free one next to it
                Vec3 goal = fp;
                if (!g.passable(gx, gy, t.pathLayer) && g.nearestPassable(gx, gy, t.pathLayer)) goal = g.cellCenter(gx, gy);
                if (g.findPath(pos, goal, t.pathLayer, path, 4000, MapGrid::clearanceFor(t.radius)))
                    for (auto& wp : path) if (dist2D(wp, pos) > kHalf) { dir = flat(wp - pos); break; }
            }
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

} // namespace aiverse::game
