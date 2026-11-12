// Point modules of the environment (RtsEnv.h): linked caves (BW CaveGroup), graveyards fed by souls
// (BW GraveyardModule / UnitSoul), burning buildings after an assault (BW PointFire), destructible walls
// (BW DestructibleObjectInfo), environment sends (worm armies, Mouro, off-map senders, graveyards).
#include "RtsEnv.h"
#include "RtsInternal.h"
#include <algorithm>

namespace aiverse::game {

using namespace detail;

// ================================================================= hooks

bool Environment::sendAllowed(Simulation& sim, Entity from, Entity to) const {
    EnvState* st = state(sim);
    if (!st) return true;
    World& w = sim.world();
    if (def_.graveyardType != 0xFFFF && w.alive(to) && w.get<Unit>(to).type == def_.graveyardType) return false;
    const int ti = pointIndex(sim, to);
    if (ti >= 0 && std::find(def_.noSendTargets.begin(), def_.noSendTargets.end(), ti) != def_.noSendTargets.end()) return false;
    const int fi = pointIndex(sim, from);
    if (fi >= 0 && ti >= 0)
        for (auto& g : def_.caves)
            if (std::find(g.begin(), g.end(), fi) != g.end() && std::find(g.begin(), g.end(), ti) != g.end()) return false;
    return true;
}

bool Environment::damageOverride(Simulation& sim, Entity target, Fixed& dmg) {
    EnvState* st = state(sim);
    if (!st) return false;
    for (size_t i = 0; i < st->walls.size(); ++i) {
        EnvWall& wl = st->walls[i];
        if (wl.e != target) continue;
        dmg = Fixed{};   // walls count hits, not damage (BW OnProjectileAttack: hp - 1)
        if (wl.state != 0 || def_.walls[i].ignoreTowers) return true;
        --wl.hits;
        sim.emit({kEvEnv, 0, target, kNullEntity, kEnvWall << 16, sim.world().get<Transform>(target).pos});
        if (wl.hits <= 0) {
            wl.state = 1;
            sim.world().get<Owner>(target).player = 0;   // nobody shoots the rubble
            sim.emit({kEvEnv, 0, target, kNullEntity, kEnvWall << 16 | 1, sim.world().get<Transform>(target).pos});
        }
        return true;
    }
    for (auto& f : st->flyers)
        if (f.body == target) { dmg = Fixed{}; --f.health; return true; }
    return false;
}

void Environment::onDeath(Simulation& sim, Entity e, uint8_t owner, Vec3 pos, bool silent) {
    EnvState* st = state(sim);
    if (!st || !def_.souls.enabled || silent) return;
    World& w = sim.world();
    const UnitType& t = m_->rules().types[w.get<Unit>(e).type];
    if (t.structure) return;
    const bool noSoul = std::find(st->noSoul.begin(), st->noSoul.end(), e.packed()) != st->noSoul.end();
    if (noSoul) return;
    // BW RegisterSpaceAreaSoul: every non-neutral death feeds the graveyard; without one the soul is only drawn
    Entity gy;
    for (Entity g : st->graveyards) if (w.alive(g)) { gy = g; break; }
    if (gy.valid() && owner != 0 && owner != kHostileNeutral) spawnSoul(sim, *st, pos, owner, gy);
    else sim.emit({kEvEnv, owner, kNullEntity, kNullEntity, kEnvSoul << 16, pos});
}

void Environment::onAssault(Simulation& sim, Entity s, uint8_t attacker, uint8_t defender, int attackerLost, int defenderLost) {
    EnvState* st = state(sim);
    if (!st) return;
    World& w = sim.world();
    const int pi = pointIndex(sim, s);
    if (pi >= 0 && defender < st->lastAction.size()) {   // BW AddLastActionBuilding (defender)
        auto& la = st->lastAction[defender];
        la.erase(std::remove(la.begin(), la.end(), pi), la.end());
        la.push_back(pi);
    }
    const Vec3 sp = w.get<Transform>(s).pos;
    if (def_.fire.enabled) {   // BW AttackResult: the building burns for fireDuration, flames by units left
        const int count = w.has<Garrison>(s) ? w.get<Garrison>(s).count : 0;
        EnvFire* f = nullptr;
        for (auto& x : st->fires) if (x.e == s) f = &x;
        if (!f) { st->fires.push_back({s, Fixed{}, Fixed::fromInt(count + defenderLost), Fixed::fromInt(1)}); f = &st->fires.back(); }
        else if (f->timer.raw <= 0) f->start = Fixed::fromInt(count + defenderLost);
        f->start = fxMax(f->start, Fixed::fromInt(count));
        f->percent = Fixed::fromInt(std::max(count, 1)) / fxMax(f->start, Fixed::fromInt(1));
        f->timer = def_.fire.duration;
    }
    if (!def_.souls.enabled) return;
    Entity gy;
    for (Entity g : st->graveyards) if (w.alive(g) && g != s) { gy = g; break; }
    Fixed acc[2];
    Fixed* a = pi >= 0 ? &st->soulAcc[size_t(pi) * 2] : acc;
    a[0] += Fixed::fromInt(attackerLost) / def_.souls.attackerDivider;
    a[1] += Fixed::fromInt(defenderLost) / def_.souls.defenderDivider;
    const Fixed rad = m_->rules().types[w.get<Unit>(s).type].radius;
    auto spot = [&]() { return Vec3{sp.x + (sim.rng().unit() * 2 - Fixed::fromInt(1)) * rad, Fixed{}, sp.z + (sim.rng().unit() * 2 - Fixed::fromInt(1)) * rad}; };
    for (int k = 0; k < 2; ++k) {
        const uint8_t owner = k == 0 ? attacker : defender;
        const int n = int(a[k].floorInt());
        for (int i = 0; i < n; ++i) {
            const Vec3 p = spot();
            if (gy.valid() && attacker != 0 && owner != 0 && attacker != kHostileNeutral) spawnSoul(sim, *st, p, owner, gy);
            else sim.emit({kEvEnv, owner, kNullEntity, s, kEnvSoul << 16, p});
        }
        a[k] -= Fixed::fromInt(n);
    }
}

void Environment::onCaptured(Simulation& sim, Entity s, uint8_t, uint8_t to) {
    EnvState* st = state(sim);
    if (!st) return;
    const int pi = pointIndex(sim, s);
    if (pi >= 0 && to < st->lastAction.size()) {   // BW SetOwner: the new owner's last action building
        auto& la = st->lastAction[to];
        la.erase(std::remove(la.begin(), la.end(), pi), la.end());
        la.push_back(pi);
    }
    for (auto& f : st->fires) if (f.e == s) f.timer = Fixed{};   // BW OnOwnerChanged: the fire goes out
}

void Environment::onSend(Simulation&, uint8_t, Entity) {}

// ================================================================= sends

bool Environment::queueSend(Simulation& sim, EnvState& st, Entity from, Entity to, int count, bool unchecked, uint8_t owner, bool minion) {
    World& w = sim.world();
    if (count <= 0 || !w.alive(from) || !w.alive(to) || from == to) return false;
    auto* g = w.tryGet<Garrison>(from);
    const uint16_t unit = m_->rules().types[w.get<Unit>(from).type].spawnUnit;
    if (!g || unit == kNoType) return false;
    if (!unchecked) {
        count = std::min(count, g->count);
        if (count <= 0) return false;
        g->count -= count;
    }
    EnvSend s;
    s.from = from; s.to = to; s.left = count; s.owner = owner; s.unit = unit;
    s.group = m_->state(sim).nextGroupId++;
    st.sends.push_back(s);
    if (minion) { st.minionGroups.push_back(s.group); st.minionSpeed.push_back(Fixed::fromInt(1)); }
    return true;
}

void Environment::sendsTick(Simulation& sim, EnvState& st) {
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    for (size_t i = 0; i < st.sends.size();) {
        EnvSend& s = st.sends[i];
        if (!w.alive(s.to) || (s.lane < 0 && !w.alive(s.from))) { st.sends.erase(st.sends.begin() + long(i)); continue; }
        for (int k = 0; k < 2 && s.left > 0; ++k) {   // a few per tick so they stream out (like sysGarrison)
            Entity n;
            if (s.lane >= 0 && size_t(s.lane) < gs.lanes.size()) {
                const Lane& L = gs.lanes[size_t(s.lane)];
                const size_t idx = std::min<size_t>(s.lanePos, L.points.size() - 1);
                Vec3 at = L.points[s.reverse ? L.points.size() - 1 - idx : idx];
                n = m_->spawn(sim, s.unit, s.owner, at);
                if (w.has<Orders>(n)) {
                    w.add<LaneWalker>(n, LaneWalker{s.lane, s.lanePos, s.reverse});
                    Order ord; ord.type = OrderType::Enter; ord.target = s.to; ord.point = flat(w.get<Transform>(s.to).pos); ord.state = 3;
                    m_->issue(sim, n, ord, false);
                }
            } else n = m_->releaseUnit(sim, s.from, s.to, s.unit, s.owner, s.group);
            if (w.alive(n)) {
                if (auto* gt = w.tryGet<GroupTag>(n)) gt->id = s.group;
                else w.add<GroupTag>(n, GroupTag{s.group, s.from, s.to});
            }
            --s.left;
        }
        if (s.left <= 0) { st.sends.erase(st.sends.begin() + long(i)); continue; }
        ++i;
    }
}

// ================================================================= souls and graveyards

void Environment::spawnSoul(Simulation& sim, EnvState& st, Vec3 from, uint8_t owner, Entity target) {
    World& w = sim.world();
    EnvSoul s;
    s.from = flat(from); s.to = flat(w.get<Transform>(target).pos);
    s.owner = owner; s.target = target; s.targetOwner = w.get<Owner>(target).player;
    const Fixed d = (s.to - s.from).xz().length();
    s.total = fxMax(d / def_.souls.velocity, Fixed::fromRatio(1, 20));
    s.gravity = def_.souls.jump * 2 / (s.total * s.total);   // the view arcs it: height = jump at the middle
    st.souls.push_back(s);
}

void Environment::soulsTick(Simulation& sim, EnvState& st) {
    if (st.souls.empty()) return;
    World& w = sim.world();
    for (size_t i = 0; i < st.souls.size();) {
        EnvSoul& s = st.souls[i];
        if (!w.alive(s.target)) { st.souls.erase(st.souls.begin() + long(i)); continue; }
        const bool graveyard = def_.graveyardType != 0xFFFF && w.get<Unit>(s.target).type == def_.graveyardType;
        if (!graveyard && w.get<Owner>(s.target).player != s.targetOwner) { st.souls.erase(st.souls.begin() + long(i)); continue; }
        s.t += sim.dt();
        if (s.t < s.total) { ++i; continue; }
        // BW GraveyardModule.OnSoulCame
        bool add = s.add;
        if (graveyard) {
            size_t gi = 0;
            while (gi < st.graveyards.size() && st.graveyards[gi] != s.target) ++gi;
            Garrison* g = w.tryGet<Garrison>(s.target);
            if (gi >= st.graveyardTimer.size() || !g || st.graveyardTimer[gi].raw > 0) add = false;
            else if (g->count >= def_.graveyard.limit - 1) {
                bool flying = false;
                for (auto& sd : st.sends) if (sd.from == s.target) flying = true;
                if (!flying) w.each<GroupTag>([&](Entity, GroupTag& gt) { if (gt.from == s.target) flying = true; });
                if (flying) add = false;
                else { st.graveyardTimer[gi] = def_.graveyard.await; sim.emit({kEvEnv, 0, s.target, kNullEntity, kEnvGraveyard << 16 | 1, s.to}); }
            }
        }
        if (add) if (auto* g = w.tryGet<Garrison>(s.target)) { ++g->count; sim.emit({kEvGarrisonChanged, w.get<Owner>(s.target).player, s.target, kNullEntity, 1, s.to}); }
        sim.emit({kEvEnv, s.owner, s.target, kNullEntity, kEnvSoul << 16 | 1, s.to});
        st.souls.erase(st.souls.begin() + long(i));
    }
}

void Environment::graveyards(Simulation& sim, EnvState& st) {
    if (def_.graveyard.limit <= 0) return;
    World& w = sim.world();
    for (size_t i = 0; i < st.graveyards.size() && i < st.graveyardTimer.size(); ++i) {
        Entity gy = st.graveyards[i];
        if (!w.alive(gy) || st.graveyardTimer[i].raw <= 0) continue;
        st.graveyardTimer[i] -= sim.dt();
        if (st.graveyardTimer[i].raw > 0) continue;
        // BW MoveUnits: to the last action building of every player, as many as the target holds
        std::vector<Entity> targets;
        for (size_t p = 1; p < st.lastAction.size(); ++p) {
            if (st.lastAction[p].empty()) continue;
            Entity t = point(sim, st.lastAction[p].back());
            if (!t.valid() || t == gy || std::find(targets.begin(), targets.end(), t) != targets.end()) continue;
            targets.push_back(t);
            const int n = w.has<Garrison>(t) ? w.get<Garrison>(t).count : 0;
            queueSend(sim, st, gy, t, n, false, w.get<Owner>(gy).player);
        }
        sim.emit({kEvEnv, 0, gy, kNullEntity, kEnvGraveyard << 16 | 2, w.get<Transform>(gy).pos});
    }
}

// ================================================================= caves

void Environment::caves(Simulation& sim, EnvState& st) {
    World& w = sim.world();
    for (size_t gi = 0; gi < def_.caves.size() && gi < st.caves.size(); ++gi) {
        EnvCave& c = st.caves[gi];
        uint8_t owner = c.owner;
        int total = c.count;
        bool captured = false;
        for (int pi : def_.caves[gi]) {
            Entity e = point(sim, pi);
            if (!e.valid()) continue;
            const uint8_t o = w.get<Owner>(e).player;
            const int n = w.has<Garrison>(e) ? w.get<Garrison>(e).count : c.count;
            if (o != c.owner && !captured) { captured = true; owner = o; total = n; }   // caught one cave = caught all
            else if (!captured) total += n - c.count;
        }
        total = std::max(0, total);
        for (int pi : def_.caves[gi]) {
            Entity e = point(sim, pi);
            if (!e.valid()) continue;
            Owner& o = w.get<Owner>(e);
            if (o.player != owner) {
                const uint8_t old = o.player;
                o.player = owner;
                sim.emit({kEvCaptured, owner, e, kNullEntity, old, w.get<Transform>(e).pos});
            }
            if (auto* g = w.tryGet<Garrison>(e)) g->count = total;
        }
        c.owner = owner; c.count = total;
    }
}

// ================================================================= fire, walls

void Environment::firesTick(Simulation& sim, EnvState& st) {
    World& w = sim.world();
    for (size_t i = 0; i < st.fires.size();) {
        EnvFire& f = st.fires[i];
        if (!w.alive(f.e)) { st.fires.erase(st.fires.begin() + long(i)); continue; }
        f.timer -= sim.dt();
        if (f.timer.raw > 0) {
            const Fixed v = Fixed::fromInt(w.has<Garrison>(f.e) ? w.get<Garrison>(f.e).count : 0);
            f.start = fxMax(v, f.start);
            f.percent = f.start.raw > 0 ? v / f.start : Fixed{};
        } else f.timer = Fixed{};
        ++i;
    }
}

void Environment::wallsTick(Simulation& sim, EnvState& st) {
    World& w = sim.world();
    // BW destructIfCatched: a team owning every point of one condition breaks the wall
    bool anyCapture = false;
    for (const SimEvent& ev : sim.events()) if (ev.type == kEvCaptured) anyCapture = true;
    for (size_t i = 0; i < st.walls.size(); ++i) {
        EnvWall& wl = st.walls[i];
        const EnvWallDef& wd = def_.walls[i];
        if (wl.state == 0 && anyCapture && !wd.destructIf.empty()) {
            for (auto& cond : wd.destructIf) {
                uint8_t team = 0; bool all = !cond.empty();
                for (int pi : cond) {
                    Entity e = point(sim, pi);
                    const uint8_t o = e.valid() ? w.get<Owner>(e).player : 0;
                    const uint8_t t = (o == 0 || o == kHostileNeutral) ? 0 : m_->teamOf(sim, o);
                    if (t == 0 || (team && t != team)) { all = false; break; }
                    team = t;
                }
                if (all) { wl.hits = 0; wl.state = 1; if (w.alive(wl.e)) w.get<Owner>(wl.e).player = 0; sim.emit({kEvEnv, 0, wl.e, kNullEntity, kEnvWall << 16 | 1, {}}); break; }
            }
        }
        if (wl.state == 1) {   // rubble stays (routes stay locked) for destroyDuration, then the wall is gone
            wl.timer -= sim.dt();
            if (wl.timer.raw <= 0) {
                wl.state = 2;
                if (w.alive(wl.e)) { Unit& u = w.get<Unit>(wl.e); u.hp = Fixed{}; u.flags |= kUnitSilent; }
                sim.emit({kEvEnv, 0, wl.e, kNullEntity, kEnvWall << 16 | 2, {}});
            }
        }
    }
}

// ================================================================= view bodies

void Environment::bodies(Simulation& sim, std::vector<Body>& out) const {
    EnvState* st = state(sim);
    if (!st) return;
    auto toF = [](Fixed f) { return float(f.toDouble()); };
    for (size_t i = 0; i < st->worms.size(); ++i) {
        const auto& ws = st->worms[i];
        const EnvWormDef& d = def_.worms[i];
        if (ws.state == 4 || ws.path < 0) continue;
        for (size_t k = 0; k < ws.seg.size(); ++k) {
            const uint16_t look = k == 0 ? d.look : d.lookTail;
            if (look == 0xFFFF) continue;
            Vec3 f = ws.seg[k].pos - ws.seg[k].prev;
            out.push_back({look, ws.seg[k].pos, f, 1, 1.0f});
        }
    }
    for (size_t i = 0; i < st->toads.size(); ++i)
        if (def_.toads[i].look != 0xFFFF) {
            Vec3 f = st->toads[i].lastTarget - def_.toads[i].pos;
            out.push_back({def_.toads[i].look, def_.toads[i].pos, f, 2, st->toads[i].explode.raw > 0 ? 0.5f : 1.0f});
        }
    for (size_t i = 0; i < st->flyers.size(); ++i)
        if (def_.flyers[i].bombLook != 0xFFFF)
            for (auto& b : st->flyers[i].bombs) out.push_back({def_.flyers[i].bombLook, b.pos, b.vel, 3, 1.0f});
    if (def_.souls.look != 0xFFFF)
        for (auto& s : st->souls) {
            const Fixed k = s.total.raw > 0 ? s.t / s.total : Fixed{};
            Vec3 p = s.from + (s.to - s.from) * k;
            p.y = def_.souls.jump * 4 * k * (Fixed::fromInt(1) - k);   // parabola peaking at jumpHeight
            out.push_back({def_.souls.look, p, s.to - s.from, 4, toF(Fixed::fromInt(1) - k * k / 2)});
        }
}

} // namespace aiverse::game
