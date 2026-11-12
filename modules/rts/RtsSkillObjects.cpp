// Skill objects: things a skill turns units into or sends across the map, as generic delivery keys
// (docs/game-format.md "Skill objects"). Bunker Wars uses them for rolling stones (spline walkers),
// jumping heads (line walkers), mines left by units (standing objects), souls flying into a structure,
// magnets pulling groups into a structure and death traps that collect the dead.
//
//  - `origins` (filter) + `consume`: one area / projectile per chosen unit, starting where it stands; a lane area
//    continues the unit's own lane from its position and direction; consumed units die silently.
//  - `goal`: the structure (or point) a moving area / projectile heads for; areas stop there and apply `onArrive`.
//  - `onExpire`: lifetime ran out before arriving or being used up (mine self-destruction).
//  - `onDeath`: every unit dying inside an area (souls collector), target = the followed object.
//  - `pull`: units fly straight into the goal structure, velocity += accel * dt, clamped (magnet, worm).
#include "RtsInternal.h"
#include <algorithm>

namespace aiverse::game {
using namespace detail;

namespace {
uint32_t ticksOf(Fixed s, uint32_t rate) { return s.raw <= 0 ? 0 : uint32_t((s * int64_t(rate)).toInt()); }
} // namespace

Entity RtsModule::resolveGoal(Simulation& sim, uint8_t goal, const EffectCtx& ctx, Entity source, Vec3 from) {
    World& w = sim.world();
    auto isStructure = [&](Entity s) { return w.alive(s) && w.has<Unit>(s) && w.get<Unit>(s).hp.raw > 0 && rules_.types[w.get<Unit>(s).type].structure; };
    // nearest structure passing `ok` (ECS order, ties to the lower entity)
    auto nearest = [&](Vec3 p, auto ok) {
        Entity best; Fixed bd{};
        w.each<Unit, Owner, Transform>([&](Entity c, Unit& u, Owner& o, Transform& t) {
            if (u.hp.raw <= 0 || !rules_.types[u.type].structure || !ok(o.player)) return;
            Fixed d = dist2D(t.pos, p);
            if (!best.valid() || d < bd) { best = c; bd = d; }
        });
        return best;
    };
    switch (goal) {
    case kGoalTarget:
        if (isStructure(ctx.target)) return ctx.target;
        return nearest(ctx.point, [](uint8_t) { return true; });
    case kGoalGroupTarget: case kGoalGroupSource: {
        if (w.alive(source)) {
            if (auto* gt = w.tryGet<GroupTag>(source)) {
                Entity s = goal == kGoalGroupTarget ? gt->to : gt->from;
                if (isStructure(s)) return s;
            }
            if (goal == kGoalGroupTarget)
                if (auto* ord = w.tryGet<Orders>(source); ord && !ord->queue.empty() && ord->queue.front().type == OrderType::Enter && isStructure(ord->queue.front().target))
                    return ord->queue.front().target;
        }
        return isStructure(ctx.target) ? ctx.target : kNullEntity;
    }
    case kGoalOwnStructure: return nearest(from, [&](uint8_t p) { return p == ctx.owner; });
    case kGoalEnemyStructure: return nearest(from, [&](uint8_t p) { return p != 0 && hostile(sim, ctx.owner, p); });
    case kGoalCaster: return w.alive(ctx.caster) ? ctx.caster : kNullEntity;
    default: return kNullEntity;
    }
}

// Area / projectile effects with skill-object keys; false: not ours (the plain path in applyEffect runs).
bool RtsModule::spawnSkillObjects(Simulation& sim, const EffectDef& e, int list, int index, const EffectCtx& ctx) {
    const DeliveryDef& d = e.delivery;
    if (d.origins < 0) return false;
    World& w = sim.world();
    const EffectDef& oe = rules_.payloads[size_t(d.origins)][0];
    std::vector<Entity> from = effectTargets(sim, oe, ctx);
    for (Entity s : from) {
        if (!w.alive(s) || !w.has<Unit>(s)) continue;
        const Vec3 at = flat(w.get<Transform>(s).pos);
        if (e.kind == EffectKind::Projectile) spawnProjectile(sim, e, list, index, ctx, at, s);
        else spawnArea(sim, e, list, index, ctx, at, s);
        if (d.consume) { Unit& u = w.get<Unit>(s); u.hp = Fixed{}; u.flags |= kUnitSilent; }
    }
    return true;
}

void RtsModule::spawnArea(Simulation& sim, const EffectDef& e, int list, int index, const EffectCtx& ctx, Vec3 at, Entity source) {
    World& w = sim.world();
    GameState& gs = state(sim);
    const uint32_t tick = sim.tick();
    const DeliveryDef& dl = e.delivery;
    const Vec3 casterPos = w.alive(ctx.caster) && w.has<Transform>(ctx.caster) ? w.get<Transform>(ctx.caster).pos : ctx.origin;
    Entity a = w.create();
    w.add<Transform>(a, Transform{at, at, Fixed{}});
    w.add<Owner>(a, Owner{ctx.owner});
    Area ar;
    ar.caster = ctx.caster; ar.sourceType = ctx.sourceType; ar.effList = int16_t(list); ar.effIndex = int16_t(index);
    ar.radius = fxMax(e.radius, kHalf);
    ar.scale = ctx.scale;
    ar.fireAt = tick + ticksOf(dl.delay, sim.tickRate());
    ar.nextAt = ar.fireAt;
    Fixed life = e.kind == EffectKind::Reveal ? fxMax(e.duration, dl.lifetime) : dl.lifetime;
    if (dl.lifetimeMax > dl.lifetime) life = dl.lifetime + (dl.lifetimeMax - dl.lifetime) * sim.rng().unit();
    // contact areas and travelling objects (goal / lane end) live until used up or arrived
    const bool travels = dl.goal != kGoalNone || (dl.motion == AreaMotion::Lane && dl.onArrive >= 0);
    ar.endAt = life.raw > 0 ? ar.fireAt + ticksOf(life, sim.tickRate()) : (dl.trigger == AreaTrigger::Contact || travels ? UINT32_MAX : ar.fireAt);
    ar.dest = flat(ctx.point) + dl.teleportOffset;
    Vec3 d = flat(ctx.point - (source.valid() ? at : casterPos));
    if (dl.goal != kGoalNone) {
        ar.goal = resolveGoal(sim, dl.goal, ctx, source, at);
        if (ar.goal.valid()) ar.dest = flat(w.get<Transform>(ar.goal).pos);
        else if (dl.goal != kGoalPoint) ar.dest = at;   // nothing to head for: arrives at once
        d = flat(ar.dest - at);
    }
    if (d.length().raw > 0) ar.dir = d / d.length();
    if (dl.motion == AreaMotion::Follow) ar.follow = w.alive(source) ? source : w.alive(ctx.target) ? ctx.target : ctx.caster;
    if (dl.motion == AreaMotion::Lane) {
        auto* slw = w.alive(source) ? w.tryGet<LaneWalker>(source) : nullptr;
        if (slw && slw->lane >= 0 && size_t(slw->lane) < gs.lanes.size()) {
            // a unit turned into a walker keeps rolling along its own lane from where it was
            ar.lane = slw->lane; ar.laneReverse = slw->reverse; ar.lanePos = slw->pos;
        } else {
            // along the lane of the targeted group, from its start
            Entity m = ctx.target;
            if (!w.alive(m) || !w.has<LaneWalker>(m)) { auto mem = groupMembers(sim, ctx.group); m = mem.empty() ? kNullEntity : mem[0]; }
            if (auto* lw = w.alive(m) ? w.tryGet<LaneWalker>(m) : nullptr; lw && lw->lane >= 0 && size_t(lw->lane) < gs.lanes.size()) {
                ar.lane = lw->lane; ar.laneReverse = lw->reverse; ar.lanePos = 0;
                const auto& pts = gs.lanes[size_t(lw->lane)].points;
                Vec3 p0 = pts[lw->reverse ? pts.size() - 1 : 0];
                w.get<Transform>(a).pos = p0; w.get<Transform>(a).prevPos = p0;
            }
        }
        if (ar.lane >= 0 && !ar.goal.valid() && (dl.onArrive >= 0 || dl.goal != kGoalNone)) {
            const Lane& ln = gs.lanes[size_t(ar.lane)];
            ar.goal = ar.laneReverse ? ln.a : ln.b;   // the structure at the end it rolls to
        }
    }
    w.add<Area>(a, ar);
    sim.emit({kEvAreaStart, ctx.owner, a, ctx.caster, list, at});
}

// Same launch as the plain projectile effect (flight programs included); `start` is the caster, the effect point
// (`start: "point"`) or the origin unit; with no caster and no origin it comes from the sky.
void RtsModule::spawnProjectile(Simulation& sim, const EffectDef& e, int list, int index, const EffectCtx& ctx, Vec3 start, Entity source) {
    World& w = sim.world();
    const DeliveryDef& dl = e.delivery;
    Entity goal = dl.goal != kGoalNone ? resolveGoal(sim, dl.goal, ctx, source, start) : kNullEntity;
    Vec3 aim = goal.valid() ? flat(w.get<Transform>(goal).pos) : flat(ctx.point);
    if (dl.scatter.raw > 0) aim = aim + Vec3{(sim.rng().unit() * 2 - kOne) * dl.scatter, Fixed{}, (sim.rng().unit() * 2 - kOne) * dl.scatter};
    const bool sky = !source.valid() && !dl.fromPoint && !w.alive(ctx.caster);
    if (sky) start = aim + Vec3{-Fixed::fromInt(3), Fixed{}, -Fixed::fromInt(3)};
    const Fixed launchHeight = sky ? Fixed::fromInt(6) : Fixed::fromRatio(6, 10);
    if (dl.flight.enabled) start = lift(sim, start, launchHeight);
    else start.y = launchHeight;   // legacy delivery coordinates (checksum streams)
    Entity p = w.create();
    Projectile pr;
    pr.target = goal.valid() ? goal : (dl.flight.enabled ? dl.flight.homing : dl.homing) && w.alive(ctx.target) ? ctx.target : kNullEntity;
    pr.source = ctx.caster; pr.aim = aim; pr.speed = fxMax(dl.speed, Fixed::fromRatio(1, 10));
    pr.owner = ctx.owner; pr.layers = 0xFF; pr.effList = int16_t(list); pr.effIndex = int16_t(index);
    pr.sourceType = ctx.sourceType; pr.start = start; pr.total = (aim - start).length(); pr.scale = ctx.scale;
    w.add<Transform>(p, Transform{start, start, Fixed{}});
    initProjectileProgram(sim, pr, start);
    w.add<Projectile>(p, pr);
    w.add<Owner>(p, Owner{ctx.owner});
    sim.emit({kEvProjectileLaunch, ctx.owner, p, ctx.caster, list, start});
}

bool RtsModule::areaArrival(Simulation& sim, Entity a, Fixed step) {
    World& w = sim.world();
    Area& ar = w.get<Area>(a);
    Transform& tr = w.get<Transform>(a);
    if (w.alive(ar.goal) && w.has<Transform>(ar.goal)) ar.dest = flat(w.get<Transform>(ar.goal).pos);   // goals may move (flying towers)
    Vec3 d = flat(ar.dest - tr.pos);
    Fixed len = d.length();
    if (len <= step) { tr.pos = ar.dest; return true; }
    ar.dir = d / len;
    tr.pos = tr.pos + ar.dir * step;
    return false;
}

void RtsModule::areaFinished(Simulation& sim, Entity a, bool usedUp) {
    World& w = sim.world();
    const Area ar = w.get<Area>(a);
    const DeliveryDef& dl = rules_.payloads[size_t(ar.effList)][size_t(ar.effIndex)].delivery;
    int16_t pl = ar.arrived ? dl.onArrive : usedUp ? int16_t(-1) : dl.onExpire;
    if (pl < 0) return;
    EffectCtx ctx;
    ctx.caster = ar.caster; ctx.owner = w.get<Owner>(a).player; ctx.sourceType = ar.sourceType; ctx.scale = ar.scale;
    ctx.point = w.get<Transform>(a).pos; ctx.origin = ctx.point;
    ctx.target = w.alive(ar.goal) ? ar.goal : kNullEntity;
    applyPayload(sim, pl, ctx);
}

// ================================================================= magnets

void RtsModule::effectPull(Simulation& sim, const EffectDef& e, const EffectCtx& ctx, const std::vector<Entity>& targets) {
    World& w = sim.world();
    Entity dest = resolveGoal(sim, e.delivery.goal != kGoalNone ? e.delivery.goal : uint8_t(kGoalTarget), ctx, kNullEntity, ctx.point);
    if (!w.alive(dest)) return;
    const Vec3 dp = flat(w.get<Transform>(dest).pos);
    for (Entity h : targets) {
        if (h == dest || !w.alive(h) || !w.has<Mobile>(h) || rules_.types[w.get<Unit>(h).type].structure) continue;
        if (auto* p = w.tryGet<Pulled>(h); p && !p->done) continue;   // one special move at a time
        auto* ord = w.tryGet<Orders>(h);
        if (!ord) continue;
        const Vec3 pos = flat(w.get<Transform>(h).pos);
        Pulled pl;
        pl.target = dest; pl.start = pos; pl.dist = dist2D(pos, dp);
        pl.vel = e.delivery.speed; pl.accel = e.delivery.accel; pl.minVel = e.delivery.minSpeed; pl.maxVel = e.delivery.maxSpeed;
        if (ord->queue.empty() || ord->queue.front().type != OrderType::Enter) {
            ord->queue.clear();
            Order o; o.type = OrderType::Enter;
            ord->queue.push_back(o);
        }
        Order& o = ord->queue.front();
        o.target = dest; o.point = dp; o.state = 1;
        if (auto* gt = w.tryGet<GroupTag>(h)) gt->to = dest;
        stopMoving(sim, h);
        if (w.has<Pulled>(h)) w.get<Pulled>(h) = pl;
        else w.add<Pulled>(h, pl);
    }
}

bool RtsModule::runPull(Simulation& sim, Entity e, Order& o) {
    World& w = sim.world();
    Pulled& p = w.get<Pulled>(e);
    if (p.done) return true;
    if (!w.alive(p.target) || p.target != o.target) { p.done = true; return true; }
    const Fixed dt = sim.dt();
    p.vel += p.accel * dt;
    if (p.minVel.raw > 0) p.vel = fxMax(p.vel, p.minVel);
    if (p.maxVel.raw > 0) p.vel = fxMin(p.vel, p.maxVel);
    p.way += p.vel * dt;
    const Vec3 tp = flat(w.get<Transform>(p.target).pos);
    Transform& tr = w.get<Transform>(e);
    Vec3 d = tp - p.start;
    if (p.way >= p.dist || p.dist.raw <= 0) { tr.pos = Vec3{tp.x, tr.pos.y, tp.z}; p.done = true; return true; }
    Vec3 np = p.start + d * (p.way / p.dist);
    tr.pos = Vec3{np.x, tr.pos.y, np.z};
    if (d.length().raw > 0) w.get<Unit>(e).facing = flat(d) / flat(d).length();
    return false;
}

void RtsModule::tickSkillObjects(Simulation& sim) {
    World& w = sim.world();
    std::vector<Entity> done;
    w.each<Pulled>([&](Entity e, Pulled& p) { if (p.done) done.push_back(e); });
    for (Entity e : done) w.remove<Pulled>(e);
    tickTransfers(sim);
}

// ================================================================= death traps (souls)

void RtsModule::noteDeathInAreas(Simulation& sim, Entity dead, Vec3 pos) {
    World& w = sim.world();
    const Unit& u = w.get<Unit>(dead);
    if (rules_.types[u.type].structure) return;
    const uint8_t owner = w.get<Owner>(dead).player;
    w.each<Area, Transform>([&](Entity a, Area& ar, Transform& t) {
        if (ar.effList < 0 || sim.tick() < ar.fireAt) return;
        const EffectDef& e = rules_.payloads[size_t(ar.effList)][size_t(ar.effIndex)];
        if (e.delivery.onDeath < 0 || dist2D(t.pos, pos) > ar.radius) return;
        const uint8_t ao = w.get<Owner>(a).player;
        const bool enemy = hostile(sim, ao, owner), own = owner == ao;
        const TargetFilter& f = e.filter;
        if (!((enemy && f.enemies) || (own && (f.allies || f.self)) || (!enemy && !own && owner != 0 && f.allies) || (owner == 0 && f.neutral))) return;
        areaDeaths_.push_back({a, pos});
    });
}

void RtsModule::flushAreaDeaths(Simulation& sim) {
    if (areaDeaths_.empty()) return;
    World& w = sim.world();
    auto deaths = std::move(areaDeaths_);
    areaDeaths_.clear();
    for (auto& d : deaths) {
        if (!w.alive(d.area) || !w.has<Area>(d.area)) continue;
        Area& ar = w.get<Area>(d.area);
        const EffectDef& e = rules_.payloads[size_t(ar.effList)][size_t(ar.effIndex)];
        if (e.delivery.maxTriggers > 0 && ar.deaths >= e.delivery.maxTriggers) continue;
        ar.deaths++;
        EffectCtx ctx;
        ctx.caster = ar.caster; ctx.owner = w.get<Owner>(d.area).player; ctx.sourceType = ar.sourceType; ctx.scale = ar.scale;
        ctx.point = d.pos; ctx.origin = d.pos;
        ctx.target = w.alive(ar.follow) ? ar.follow : w.alive(ar.goal) ? ar.goal : kNullEntity;
        sim.emit({kEvAreaTrigger, ctx.owner, d.area, kNullEntity, ar.effList, d.pos});
        applyPayload(sim, e.delivery.onDeath, ctx);
        if (w.alive(d.area) && e.delivery.maxTriggers > 0 && w.get<Area>(d.area).deaths >= e.delivery.maxTriggers) w.get<Area>(d.area).endAt = sim.tick();
    }
}

void RtsModule::hashSkillObjects(Simulation& sim, Hasher& h) {
    World& w = sim.world();
    w.each<Pulled>([&](Entity e, Pulled& p) { h.u64(e.packed()); h.u64(p.target.packed()); h.fx(p.way); h.fx(p.vel); h.u32(p.done); });
    w.each<Area>([&](Entity e, Area& a) { if (a.goal.valid() || a.arrived || a.deaths || a.spent) { h.u64(e.packed()); h.u64(a.goal.packed()); h.u32(a.arrived); h.i32(a.deaths); h.i32(a.spent); } });
    w.each<Transfer>([&](Entity e, Transfer& t) { h.u64(e.packed()); h.fx(t.collected); h.fx(t.toGrab); h.fx(t.grabLeft); h.fx(t.wait); h.fx(t.fillLeft); h.fx(t.acc); });
    w.each<Statuses>([&](Entity e, Statuses& st) { for (auto& in : st.list) if (in.spent || in.group) { h.u64(e.packed()); h.u32(in.def); h.i32(in.spent); h.u32(in.group); } });
}

} // namespace aiverse::game

// ================================================================= stage 2: BW behaviours as generic keys

namespace aiverse::game {
using namespace detail;

namespace {
// sin / cos of an angle in degrees, Taylor series in fixed point after range reduction (deterministic)
Fixed sinDeg(Fixed deg) {
    int64_t r = deg.raw % (int64_t(360) << 16);
    if (r < 0) r += int64_t(360) << 16;
    Fixed d = Fixed::fromRaw(r);
    bool neg = false;
    if (d > Fixed::fromInt(180)) { d = d - Fixed::fromInt(180); neg = true; }
    if (d > Fixed::fromInt(90)) d = Fixed::fromInt(180) - d;
    Fixed x = d * Fixed::fromRaw(1144);   // pi / 180
    Fixed x2 = x * x, term = x, sum = x;
    for (int k = 1; k <= 6; ++k) { term = -term * x2 / int64_t((2 * k) * (2 * k + 1)); sum += term; }
    return neg ? -sum : sum;
}
Fixed cosDeg(Fixed deg) { return sinDeg(deg + Fixed::fromInt(90)); }
} // namespace

int* RtsModule::budgetCounter(Simulation& sim, const EffectCtx& ctx) {
    World& w = sim.world();
    if (!w.alive(ctx.budgetEnt)) return nullptr;
    if (ctx.budgetDef < 0) return w.has<Area>(ctx.budgetEnt) ? &w.get<Area>(ctx.budgetEnt).spent : nullptr;
    if (auto* st = w.tryGet<Statuses>(ctx.budgetEnt))
        for (auto& in : st->list) if (in.def == uint16_t(ctx.budgetDef)) return &in.spent;
    return nullptr;
}

bool RtsModule::routeFilterOk(Simulation& sim, uint8_t route, Entity unit, const EffectCtx& ctx) {
    World& w = sim.world();
    auto* gt = w.tryGet<GroupTag>(unit);
    if (!gt || !w.alive(ctx.target)) return false;
    if (route == 1) {   // walking to the target (BW Amulet: units whose route ends at the point)
        auto* ord = w.tryGet<Orders>(unit);
        return ord && !ord->queue.empty() && ord->queue.front().type == OrderType::Enter && ord->queue.front().target == ctx.target;
    }
    return gt->from == ctx.target;   // left the target (BW KillLeavingUnits / OnUnitCreated)
}

void RtsModule::areaOrbit(Simulation& sim, Entity a) {
    World& w = sim.world();
    Area& ar = w.get<Area>(a);
    const DeliveryDef& dl = rules_.payloads[size_t(ar.effList)][size_t(ar.effIndex)].delivery;
    const Fixed age = Fixed::fromInt(int64_t(sim.tick() - ar.fireAt)) / int64_t(sim.tickRate());
    const Fixed k = dl.orbitExpand.raw > 0 ? fxMin(kOne, age / dl.orbitExpand) : kOne;   // BW expandEllipseDuration
    const Fixed ang = dl.orbitSpeed * age;
    Transform& tr = w.get<Transform>(a);
    tr.pos = Vec3{ar.dest.x + cosDeg(ang) * dl.orbitA * k, tr.pos.y, ar.dest.z + sinDeg(ang) * dl.orbitB * k};
}

void RtsModule::staggerCast(Simulation& sim, int list, int index, const EffectCtx& ctx, Entity target, Fixed delay) {
    World& w = sim.world();
    if (!w.alive(target)) return;
    const Vec3 at = flat(w.get<Transform>(target).pos);
    Entity a = w.create();
    w.add<Transform>(a, Transform{at, at, Fixed{}});
    w.add<Owner>(a, Owner{ctx.owner});
    Area ar;
    ar.caster = ctx.caster; ar.sourceType = ctx.sourceType; ar.effList = int16_t(list); ar.effIndex = int16_t(index);
    ar.radius = kHalf; ar.scale = ctx.scale;
    ar.fireAt = sim.tick() + uint32_t((delay * int64_t(sim.tickRate())).toInt());
    ar.nextAt = ar.fireAt; ar.endAt = ar.fireAt;   // fires once: the cast payload on `target`
    ar.goal = target;
    w.add<Area>(a, ar);
    sim.emit({kEvAreaStart, ctx.owner, a, ctx.caster, list, at});
}

void RtsModule::staggerCastAt(Simulation& sim, int list, int index, const EffectCtx& ctx, Vec3 at, Fixed delay) {
    World& w = sim.world();
    at = flat(at);
    Entity a = w.create();
    w.add<Transform>(a, Transform{at, at, Fixed{}});
    w.add<Owner>(a, Owner{ctx.owner});
    Area ar;
    ar.caster = ctx.caster; ar.sourceType = ctx.sourceType; ar.effList = int16_t(list); ar.effIndex = int16_t(index);
    ar.radius = kHalf; ar.scale = ctx.scale;
    ar.fireAt = sim.tick() + uint32_t((delay * int64_t(sim.tickRate())).toInt());
    ar.nextAt = ar.fireAt; ar.endAt = ar.fireAt;   // fires once: the cast payload at its point
    w.add<Area>(a, ar);
    sim.emit({kEvAreaStart, ctx.owner, a, ctx.caster, list, at});
}

bool RtsModule::silencedFor(Simulation& sim, uint8_t player, int index) const {
    auto* gs = sim.world().resource<GameState>();
    if (player >= gs->players.size()) return false;
    const int slot = player < gs->playerAbilities.size() && size_t(index) < gs->playerAbilities[player].size() ? gs->playerAbilities[player][size_t(index)].slot : -1;
    for (auto& in : gs->players[player].statuses) {
        const StatusDef& d = rules_.statuses[in.def];
        if (!(d.flags & kStSilence) || in.until <= sim.tick()) continue;
        if (!d.slots || (slot >= 0 && (d.slots >> slot) & 1u)) return true;   // BW PlayersSilence slotIndexes
    }
    return false;
}

void RtsModule::endCaptureStatuses(Simulation& sim, Entity s) {
    World& w = sim.world();
    auto* st = w.tryGet<Statuses>(s);
    if (!st) return;
    const size_t n = st->list.size();
    st->list.erase(std::remove_if(st->list.begin(), st->list.end(), [&](const StatusInst& in) { return rules_.statuses[in.def].endOnCapture; }), st->list.end());
    if (st->list.size() != n) { st->flags = 0; for (auto& in : st->list) st->flags |= rules_.statuses[in.def].flags; }
}

void RtsModule::onUnitReleased(Simulation& sim, Entity from, Entity unit) {
    World& w = sim.world();
    auto* st = w.tryGet<Statuses>(from);
    if (!st || !w.alive(unit)) return;
    const uint32_t group = w.has<GroupTag>(unit) ? w.get<GroupTag>(unit).id : 0;
    std::vector<std::pair<int16_t, StatusInst>> spawns;
    uint8_t captor = 0;
    for (auto& in : st->list) {
        const StatusDef& d = rules_.statuses[in.def];
        if (d.onSpawn >= 0) spawns.push_back({d.onSpawn, in});
        if (d.commandable && d.commandCapture && in.spent && in.group && in.group == group) captor = in.owner;
    }
    for (auto& [pl, in] : spawns) {   // BW OnUnitCreated (chicken coop, enhance weapon, kill leaving units)
        EffectCtx ctx;
        ctx.caster = from; ctx.owner = in.owner; ctx.sourceType = in.sourceType; ctx.target = unit;
        ctx.point = w.get<Transform>(unit).pos; ctx.origin = ctx.point; ctx.group = group;
        ctx.budgetEnt = from; ctx.budgetDef = int16_t(in.def);
        applyPayload(sim, pl, ctx);
    }
    if (captor && w.alive(unit) && w.get<Owner>(unit).player != captor) {   // BW Hypnosis captureHypnotisedUnits
        const uint8_t old = w.get<Owner>(unit).player;
        w.get<Owner>(unit).player = captor;
        sim.emit({kEvCaptured, captor, unit, from, old, w.get<Transform>(unit).pos});
    }
}

// BW Hypnosis: the caster drags units out of the hypnotised enemy point once while the skill lives.
bool RtsModule::hypnoticSend(Simulation& sim, const Command& cmd) {
    World& w = sim.world();
    GameState& gs = state(sim);
    if (cmd.units.size() != 1) return false;
    Entity e = cmd.units[0];
    if (!w.alive(e) || !w.has<Garrison>(e) || w.get<Owner>(e).player == cmd.player) return false;
    auto* st = w.tryGet<Statuses>(e);
    if (!st) return false;
    StatusInst* hit = nullptr;
    for (auto& in : st->list)
        if (rules_.statuses[in.def].commandable && in.owner == cmd.player && !in.spent && in.until > sim.tick()) { hit = &in; break; }
    if (!hit) return false;
    const StatusDef& d = rules_.statuses[hit->def];
    const uint8_t holder = w.get<Owner>(e).player;
    auto* hp = sim.player(holder);
    if (!hp || !hp->alive) return false;
    Command c2 = cmd;
    c2.player = holder;   // the units stay the owner's (BW group.sender = caster, owner = point owner)
    if (d.commandPercent.raw > 0) c2.param = int32_t((d.commandPercent * 100).toInt());
    const uint32_t gid = gs.nextGroupId;
    const int before = w.get<Garrison>(e).count;
    hit->spent = 1;
    const uint16_t def = hit->def;
    applyCommand(sim, c2);
    for (auto& in : w.get<Statuses>(e).list)
        if (in.def == def && in.owner == cmd.player) {
            if (gs.nextGroupId != gid || w.get<Garrison>(e).count != before) {
                in.group = gid;
                if (d.commandCapture) in.until = std::max(in.until, sim.tick() + 60 * sim.tickRate());   // until the group is out
                else in.until = sim.tick();   // BW: deactivated when the group moves
            } else in.spent = 0;   // nothing left: still usable
        }
    return true;
}

// BW PointsTeleport.Use: the next send from a point with the skill teleports instead of walking.
bool RtsModule::skillSend(Simulation& sim, Entity from, Entity to, uint8_t player, int) {
    World& w = sim.world();
    auto* st = w.tryGet<Statuses>(from);
    if (!st) return false;
    for (auto& in : st->list) {
        const StatusDef& d = rules_.statuses[in.def];
        if (!d.transfer || in.spent || in.owner != player || in.until <= sim.tick()) continue;
        if (w.get<Owner>(to).player != player) return false;   // BW targetFilter: own points
        Garrison& g = w.get<Garrison>(from);
        const int floorUnits = g.count;
        const int want = std::min(std::max(1, int((d.transferPercent * floorUnits).floorInt())), floorUnits);
        if (want <= 0) return false;
        in.spent = 1;
        Transfer t;
        t.from = from; t.to = to; t.owner = player; t.toGrab = Fixed::fromInt(want);
        t.grabLeft = d.transferGrab; t.fillLeft = d.transferFill;
        const Fixed len = routeLength(sim, from, to);
        t.wait = (len.raw > 0 ? len : dist2D(w.get<Transform>(from).pos, w.get<Transform>(to).pos)) / d.transferSpeed;
        if (t.grabLeft.raw > 0) t.grabVel = t.toGrab / t.grabLeft;
        else { const int got = std::min(g.count, want); g.count -= got; t.collected = Fixed::fromInt(got); t.toGrab = Fixed{}; }
        if (t.fillLeft.raw > 0) t.fillVel = (t.collected + t.toGrab) / t.fillLeft;
        in.until = std::max(in.until, sim.tick() + uint32_t(((t.grabLeft + t.wait + t.fillLeft + kOne) * int64_t(sim.tickRate())).toInt()));
        Entity j = w.create();
        w.add<Transfer>(j, t);
        sim.emit({kEvAreaStart, player, j, from, -1, w.get<Transform>(from).pos});
        return true;
    }
    return false;
}

void RtsModule::tickTransfers(Simulation& sim) {
    World& w = sim.world();
    const Fixed dt = sim.dt();
    std::vector<Entity> list;
    w.each<Transfer>([&](Entity e, Transfer&) { list.push_back(e); });
    for (Entity j : list) {
        Transfer& t = w.get<Transfer>(j);
        bool done = !w.alive(t.to) || !w.has<Garrison>(t.to) || w.get<Owner>(t.to).player != t.owner;   // BW TestPoint(destination)
        if (!done && t.grabLeft.raw > 0) {   // grab
            if (!w.alive(t.from) || !w.has<Garrison>(t.from) || w.get<Owner>(t.from).player != t.owner) t.grabLeft = Fixed{};
            else {
                t.grabLeft -= dt;
                t.acc += fxMin(dt * t.grabVel, t.toGrab);
                if (t.acc >= kOne) {
                    Garrison& g = w.get<Garrison>(t.from);
                    const int take = std::min(g.count, int(t.acc.floorInt()));
                    t.acc -= Fixed::fromInt(t.acc.floorInt());
                    g.count -= take;
                    t.collected += Fixed::fromInt(take);
                    t.toGrab -= Fixed::fromInt(take);
                }
                if (t.grabLeft.raw <= 0 && t.fillLeft.raw > 0) t.fillVel = t.collected / t.fillLeft;
            }
        } else if (!done && t.wait.raw > 0) {
            t.wait -= dt;   // the dig wave travels the route
        } else if (!done) {
            Garrison& g = w.get<Garrison>(t.to);
            Fixed add = t.fillLeft.raw > 0 ? fxMin(t.fillVel * dt, t.collected) : t.collected;
            t.fillLeft -= dt;
            t.collected -= add;
            t.acc += add;   // whole units arrive; the fraction waits
            const int n = int(t.acc.floorInt());
            g.count += n;
            t.acc -= Fixed::fromInt(n);
            if (t.collected.raw <= 0 || (t.fillLeft.raw <= 0 && t.collected.raw <= 0)) {
                g.count += int((t.acc + Fixed::fromRatio(1, 2)).floorInt());
                done = true;
            }
        }
        if (done) w.destroyLater(j);
    }
}

Fixed RtsModule::levelOfStock(int16_t res, Fixed stock) const {
    for (auto& rl : rules_.modes.levels) {
        if (rl.res != res || rl.levels.empty()) continue;
        const auto& L = rl.levels;
        for (size_t i = 0; i < L.size(); ++i) {   // BW MoraleInfo.GetLevelIndexFromPoints: level + progress
            if (stock < L[i].at) {
                if (i == 0) return L[0].at.raw > 0 ? stock / L[0].at - kOne : Fixed{};
                return Fixed::fromInt(int64_t(i - 1)) + kOne - (L[i].at - stock) / (L[i].at - L[i - 1].at);
            }
        }
        return Fixed::fromInt(int64_t(L.size() - 1));
    }
    return Fixed{};
}

Fixed RtsModule::stockOfLevel(int16_t res, Fixed level) const {
    for (auto& rl : rules_.modes.levels) {
        if (rl.res != res || rl.levels.empty()) continue;
        const auto& L = rl.levels;
        const int idx = std::max(0, int(level.floorInt()));   // BW MoraleInfo.GetMoralePointsFromLevel
        if (size_t(idx) >= L.size() - 1) return L.back().at;
        const Fixed p = level - Fixed::fromInt(idx);
        return L[size_t(idx)].at + (L[size_t(idx) + 1].at - L[size_t(idx)].at) * fxMax(Fixed{}, p);
    }
    return Fixed{};
}


bool RtsModule::everyDue(Simulation& sim, const EffectDef& e, const EffectCtx& ctx) {
    World& w = sim.world();
    const uint32_t period = std::max<uint32_t>(1, uint32_t((e.every * int64_t(sim.tickRate())).toInt()));
    uint32_t age = sim.tick();
    if (ctx.budgetDef < 0 && w.alive(ctx.budgetEnt) && w.has<Area>(ctx.budgetEnt)) age = sim.tick() - w.get<Area>(ctx.budgetEnt).fireAt;
    return age % period == 0;
}

// BW PointsPanic.MoveUnits: percent of the garrison leaves as sends of its owner, split over the maxPoints nearest
// (route length) structures that pass `runTo` relative to the owner; ceil(units / count) per destination
void RtsModule::garrisonSend(Simulation& sim, const EffectDef& e, const EffectCtx& ctx, const std::vector<Entity>& targets) {
    World& w = sim.world();
    const TargetFilter& rf = rules_.payloads[size_t(e.runTo)][0].filter;
    for (Entity h : targets) {
        if (!w.alive(h) || !w.has<Garrison>(h)) continue;
        const uint8_t o = w.get<Owner>(h).player;
        auto* pi = sim.player(o);
        if (!pi || !pi->alive) continue;
        std::vector<std::pair<Fixed, Entity>> dests;
        w.each<Garrison, Owner, Transform>([&](Entity d, Garrison&, Owner& od, Transform& t) {
            if (d == h) return;
            const uint8_t p = od.player;
            const bool self = p == o, enemy = !self && hostile(sim, o, p), ally = !self && !enemy && p != 0;
            if (!((self && rf.allies) || (enemy && rf.enemies) || (ally && rf.allies) || (p == 0 && rf.neutral))) return;
            if (!canSend(sim, h, d)) return;
            Fixed len = routeLength(sim, h, d);
            dests.push_back({len.raw >= 0 ? len : dist2D(t.pos, w.get<Transform>(h).pos), d});
        });
        std::stable_sort(dests.begin(), dests.end(), [](auto& a, auto& b) { return a.first < b.first; });
        const int count = std::min<int>(e.maxPoints, int(dests.size()));
        if (count <= 0) continue;
        const Fixed units = Fixed::fromInt(w.get<Garrison>(h).count) * e.percent + rollAmount(sim, e) * ctx.scale;
        const int per = int(((units + Fixed::fromRaw(65535)) / int64_t(count)).floorInt());   // CeilToInt(units / count)
        for (int k = 0; k < count && per > 0; ++k) {
            Command c;
            c.player = o; c.type = kCmdSend; c.units = {h}; c.target = dests[size_t(k)].second; c.param = -per;
            applyCommand(sim, c);
        }
    }
}

void RtsModule::closeGroups(Simulation& sim, const std::vector<Entity>& structures) {
    World& w = sim.world();
    GameState& gs = state(sim);
    for (Entity h : structures) {
        auto& gr = gs.routes.groups;
        gr.erase(std::remove_if(gr.begin(), gr.end(), [&](const SendGroup& g) { return g.from == h; }), gr.end());
        if (auto* g = w.tryGet<Garrison>(h)) { g->count += g->pendingOut; g->pendingOut = 0; }   // engine sends without groups
    }
}
namespace detail {
void changeUnitType(RtsModule& m, Simulation& sim, Entity e, uint16_t type);   // RtsEffects.cpp
} // namespace detail

// BW PointsSwap.Swap: the target exchanges owner, garrison and building (type / level) with a random structure of
// `with` (tags, relation to the caster; the target itself may be drawn); `effects` run on both afterwards
void RtsModule::swapRandom(Simulation& sim, const EffectDef& e, const EffectCtx& ctx, Entity h) {
    World& w = sim.world();
    const TargetFilter& rf = rules_.payloads[size_t(e.runTo)][0].filter;
    std::vector<Entity> pool;
    w.each<Garrison, Unit, Owner>([&](Entity d, Garrison&, Unit& u, Owner& od) {
        if (u.hp.raw <= 0) return;
        if (rf.tags && !(rules_.types[u.type].tags & rf.tags)) return;
        const uint8_t p = od.player;
        const bool self = p == ctx.owner, enemy = !self && p != 0 && hostile(sim, ctx.owner, p), ally = !self && !enemy && p != 0;
        if (!((self && (rf.self || rf.allies)) || (enemy && rf.enemies) || (ally && rf.allies) || (p == 0 && rf.neutral))) return;
        pool.push_back(d);
    });
    if (pool.empty()) return;
    Entity other = pool[size_t(sim.rng().range(0, int(pool.size()) - 1))];
    if (other != h) {
        Owner& o1 = w.get<Owner>(h); Owner& o2 = w.get<Owner>(other);
        Garrison& g1 = w.get<Garrison>(h); Garrison& g2 = w.get<Garrison>(other);
        if (e.swapType) {
            const uint16_t t1 = w.get<Unit>(h).type, t2 = w.get<Unit>(other).type;
            if (t1 != t2) { detail::changeUnitType(*this, sim, h, t2); detail::changeUnitType(*this, sim, other, t1); }
        }
        std::swap(o1.player, o2.player);
        std::swap(g1.count, g2.count);
        g1.growth = g2.growth = Fixed{};
        const Vec3 p1 = w.get<Transform>(h).pos, p2 = w.get<Transform>(other).pos;
        if (o1.player != o2.player) {
            sim.emit({kEvCaptured, o1.player, h, other, o2.player, p1});
            sim.emit({kEvCaptured, o2.player, other, h, o1.player, p2});
        }
    }
    if (e.payload < 0) return;
    for (Entity s : {h, other}) {
        EffectCtx c2 = ctx;
        c2.target = s; c2.point = w.get<Transform>(s).pos;
        applyPayload(sim, e.payload, c2);
        if (other == h) break;
    }
}
void RtsModule::payDeathGain(Simulation& sim, Entity unit, uint8_t owner) {
    auto* st = sim.world().tryGet<Statuses>(unit);
    if (!st || owner == 0) return;
    for (auto& in : st->list)
        for (auto& a : rules_.statuses[in.def].deathGain) addResource(sim, owner, a.res, a.value);
}
} // namespace aiverse::game
