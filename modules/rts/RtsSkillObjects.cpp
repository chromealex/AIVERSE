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
    w.each<Area>([&](Entity e, Area& a) { if (a.goal.valid() || a.arrived || a.deaths) { h.u64(e.packed()); h.u64(a.goal.packed()); h.u32(a.arrived); h.i32(a.deaths); } });
}

} // namespace aiverse::game
