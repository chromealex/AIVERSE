// Environment bosses (RtsEnv.h): BW Worm (path crawler with kill zone, slime traces and armies), Toad (eats
// units in its zone, explodes and downgrades every building), FlyBomb (bomber crossing the map).
#include "RtsEnv.h"
#include "RtsInternal.h"
#include "../../engine/core/FixedMath.h"
#include <algorithm>

namespace aiverse::game {

using namespace detail;
namespace detail { void changeUnitType(RtsModule& m, Simulation& sim, Entity e, uint16_t type); }   // RtsEffects.cpp

namespace {
enum WormSt : uint8_t { kNoUnits = 0, kCatching, kFull, kPreparing, kUnder, kAwaitArmy, kArmy, kPostArmy };
bool onGround(uint8_t s) { return s <= kPreparing; }
bool armyState(uint8_t s) { return s >= kAwaitArmy; }
Fixed fromInterval(Simulation& sim, const std::vector<std::pair<Fixed, Fixed>>& cycles, int& counter) {
    if (cycles.empty()) return {};
    counter = std::clamp(counter, 0, int(cycles.size()) - 1);
    const auto& c = cycles[size_t(counter)];
    Fixed v = c.first + (c.second - c.first) * sim.rng().unit();
    counter = (counter + 1) % int(cycles.size());
    return v;
}
Fixed laneLength(const std::vector<Vec3>& pts) {
    Fixed l{};
    for (size_t i = 1; i < pts.size(); ++i) l += (pts[i] - pts[i - 1]).xz().length();
    return l;
}
// index of the lane point at `d` along the lane (from the start or from the end when reverse) and its position
uint16_t lanePosAt(const std::vector<Vec3>& pts, Fixed d, bool reverse, Vec3& at) {
    Fixed acc{};
    for (size_t k = 1; k < pts.size(); ++k) {
        const Vec3& a = pts[reverse ? pts.size() - k : k - 1];
        const Vec3& b = pts[reverse ? pts.size() - 1 - k : k];
        Fixed seg = (b - a).xz().length();
        if (acc + seg >= d) { at = seg.raw > 0 ? a + (b - a) * ((d - acc) / seg) : a; return uint16_t(k); }
        acc += seg;
    }
    at = reverse ? pts.front() : pts.back();
    return uint16_t(pts.size());
}
Vec3 norm2(Vec3 v) { Fixed l = v.xz().length(); return l.raw > 0 ? Vec3{v.x / l, Fixed{}, v.z / l} : Vec3{}; }
} // namespace

// ================================================================= worm

void Environment::wormDigUp(Simulation& sim, EnvState& st, size_t i) {
    const EnvWormDef& d = def_.worms[i];
    EnvWormState& ws = st.worms[i];
    ws.path = d.paths[sim.rng().below(uint32_t(d.paths.size()))];
    const EnvSpline& sp = def_.splines[size_t(ws.path)];
    const Fixed start = sp.length * sim.rng().unit();
    ws.forward = sim.rng().below(2) == 0;
    ws.timer += fromInterval(sim, d.onGround, ws.downCounter);
    ws.state = kNoUnits;
    ws.armySpawned = false;
    for (auto& s : ws.seg) { s.rel = Fixed{}; s.dist = start; s.pos = s.prev = sp.at(start); }
    sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWorm << 16) | int64_t(i) | (int64_t(ws.state) << 8), ws.seg.empty() ? Vec3{} : ws.seg[0].pos});
}

void Environment::wormDigDown(Simulation& sim, EnvState& st, size_t i) {
    EnvWormState& ws = st.worms[i];
    ws.timer += fromInterval(sim, def_.worms[i].underground, ws.upCounter);
    ws.state = kUnder;
    if (sim.world().alive(ws.reverseArea)) sim.world().destroyLater(ws.reverseArea);   // BW DisableReverse
    ws.reverseArea = kNullEntity;
    sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWorm << 16) | int64_t(i) | (int64_t(kUnder) << 8), ws.seg.empty() ? Vec3{} : ws.seg[0].pos});
}

bool Environment::wormArmySpot(Simulation& sim, EnvState& st, size_t i) {
    const EnvWormDef& d = def_.worms[i];
    if (d.army.empty()) return false;
    World& w = sim.world();
    const GameState& gs = m_->state(sim);
    auto target = [&](Entity e) {
        if (!w.alive(e)) return false;
        uint8_t o = w.get<Owner>(e).player;
        if (o == 0 || o == kHostileNeutral) return false;
        return d.targetPlayers.empty() || std::find(d.targetPlayers.begin(), d.targetPlayers.end(), o) != d.targetPlayers.end();
    };
    const Fixed minLen = d.minToBuilding * 2;
    std::vector<int> lanes;
    for (size_t k = 0; k < gs.lanes.size(); ++k) {
        const Lane& L = gs.lanes[k];
        if (L.offset != 0 || (!target(L.a) && !target(L.b))) continue;
        if (laneLength(L.points) < minLen) continue;
        lanes.push_back(int(k));
    }
    if (lanes.empty()) return false;
    const Lane& L = gs.lanes[size_t(lanes[sim.rng().below(uint32_t(lanes.size()))])];
    const Fixed len = laneLength(L.points);
    Fixed dist = d.minToBuilding;
    Vec3 at;
    lanePosAt(L.points, dist, false, at);
    for (int tries = 0; tries < 100; ++tries) {
        dist = d.minToBuilding + (len - minLen) * sim.rng().unit();
        lanePosAt(L.points, dist, false, at);
        bool near = false;
        w.each<Garrison, Transform>([&](Entity, Garrison&, Transform& t) { if (dist2D(t.pos, at) <= d.minToBuilding) near = true; });
        if (!near) break;
    }
    EnvWormState& ws = st.worms[i];
    ws.armyFrom = L.a; ws.armyTo = L.b; ws.armyDist = dist; ws.armyPos = at;
    ws.armyLane = int(&L - gs.lanes.data());
    return true;
}

void Environment::wormSpawnArmy(Simulation& sim, EnvState& st, size_t i) {
    EnvWormState& ws = st.worms[i];
    if (ws.armyGroup) return;
    const EnvWormDef& d = def_.worms[i];
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    if (ws.armyLane < 0 || size_t(ws.armyLane) >= gs.lanes.size() || !w.alive(ws.armyFrom) || !w.alive(ws.armyTo)) { ws.armySpawned = true; ws.state = kPostArmy; return; }
    auto target = [&](Entity e) {
        uint8_t o = w.get<Owner>(e).player;
        if (o == 0 || o == kHostileNeutral) return false;
        return d.targetPlayers.empty() || std::find(d.targetPlayers.begin(), d.targetPlayers.end(), o) != d.targetPlayers.end();
    };
    // BW: towards "to" unless only "from" belongs to a target player
    const bool forward = !target(ws.armyFrom) ? true : !target(ws.armyTo) ? false : false;
    const Lane& L = gs.lanes[size_t(ws.armyLane)];
    const Fixed len = laneLength(L.points);
    Vec3 at;
    const bool reverse = !forward;
    const uint16_t pos = lanePosAt(L.points, reverse ? len - ws.armyDist : ws.armyDist, reverse, at);
    EnvSend s;
    s.to = forward ? L.b : L.a;
    s.left = std::max(1, int(fromInterval(sim, d.army, ws.armyCounter).floorInt()));
    s.owner = 0;
    s.group = gs.nextGroupId++;
    s.unit = d.armyUnit;
    if (s.unit == 0xFFFF)
        w.each<Garrison, Unit>([&](Entity, Garrison&, Unit& u) { if (s.unit == 0xFFFF && m_->rules().types[u.type].spawnUnit != kNoType) s.unit = m_->rules().types[u.type].spawnUnit; });
    s.lane = ws.armyLane; s.lanePos = pos; s.reverse = reverse;
    s.speedMul = d.armySpeed;
    if (s.unit == 0xFFFF) { ws.armySpawned = true; ws.state = kPostArmy; ws.timer = d.disappearTime; return; }
    st.sends.push_back(s);
    st.minionGroups.push_back(s.group);
    st.minionSpeed.push_back(d.armySpeed);
    ws.armyGroup = s.group;
    ws.armySpawned = true;
    sim.emit({kEvEnv, 0, kNullEntity, s.to, (kEnvWorm << 16) | int64_t(i) | (int64_t(kArmy) << 8), at});
}

void Environment::worms(Simulation& sim, EnvState& st) {
    World& w = sim.world();
    const Fixed dt = sim.dt();
    for (size_t i = 0; i < st.worms.size(); ++i) {
        const EnvWormDef& d = def_.worms[i];
        EnvWormState& ws = st.worms[i];
        // segments crawl along the path: the head at the state's speed, every other one keeps its shift behind
        if (ws.path >= 0 && !ws.seg.empty()) {
            const EnvSpline& sp = def_.splines[size_t(ws.path)];
            Fixed vel = ws.state == kCatching ? d.catching : ws.state == kFull ? d.full : ws.state == kUnder ? d.under : armyState(ws.state) ? Fixed{} : d.noUnits;
            vel = vel * d.speed;
            for (size_t k = 0; k < ws.seg.size(); ++k) {
                EnvSegment& s = ws.seg[k];
                Fixed delta{};
                if (k == 0) delta = vel * dt;
                else { Fixed gap = ws.seg[k - 1].rel - s.rel; if (gap > d.segments[k]) delta = gap - d.segments[k]; }
                if (delta.raw <= 0) { if (k) s.prev = s.pos; continue; }
                s.dist += ws.forward ? delta : -delta;
                s.rel += delta;
                if (sp.length.raw > 0) {   // Mathf.Repeat
                    while (s.dist < Fixed{}) s.dist += sp.length;
                    while (s.dist >= sp.length) s.dist -= sp.length;
                }
                s.prev = s.pos;
                s.pos = sp.at(s.dist);
            }
        }
        // the reverse skill rides in front of the head (or on the army spot) while the worm is above ground
        if (d.reverseSkill >= 0 && !ws.seg.empty()) {
            if (onGround(ws.state) || armyState(ws.state)) {
                Vec3 dir = norm2(ws.seg[0].pos - ws.seg[0].prev);
                Vec3 rp = armyState(ws.state) ? ws.armyPos : ws.seg[0].pos + dir * d.reverseOffset;
                if (!w.alive(ws.reverseArea)) {
                    std::vector<Entity> areas;
                    cast(sim, 0, d.reverseSkill, rp, kNullEntity, &areas);
                    ws.reverseArea = areas.empty() ? kNullEntity : areas.front();
                }
                if (w.alive(ws.reverseArea)) { Transform& t = w.get<Transform>(ws.reverseArea); t.pos.x = rp.x; t.pos.z = rp.z; }
            }
        }
        // head animation frame (view), scan the kill zone in front of the head
        ws.headTimer -= dt;
        if (ws.headTimer.raw <= 0) { ws.headTimer += d.headSwitch; ws.headFrame ^= 1; }
        if (onGround(ws.state) && !ws.seg.empty()) {
            ws.scanTimer -= dt;
            if (ws.scanTimer.raw <= 0) {
                ws.scanTimer += d.scanPeriod;
                Vec3 x = ws.seg[0].pos - ws.seg[0].prev;
                if (x.xz().lengthSq().raw > 0) {
                    x = norm2(x);
                    const Vec3 y{x.z, Fixed{}, -x.x};
                    bool found = false, killed = false;
                    for (Entity e : units_(sim)) {
                        if ((m_->rules().types[w.get<Unit>(e).type].layer & kLayerAir)) continue;
                        if (auto* gt = w.tryGet<GroupTag>(e); gt && std::find(st.minionGroups.begin(), st.minionGroups.end(), gt->id) != st.minionGroups.end()) continue;
                        Vec3 r = w.get<Transform>(e).pos - ws.seg[0].pos;
                        Fixed rx = r.x * x.x + r.z * x.z;
                        if (rx < d.killBegin || rx > d.vision) continue;
                        Fixed ry = r.x * y.x + r.z * y.z;
                        if (fxAbs(ry) > d.killWidth / 2) continue;
                        if (rx <= d.killEnd) { kill(sim, st, e, false); killed = true; }
                        else found = true;
                    }
                    const uint8_t was = ws.state;
                    if (ws.state == kNoUnits) { if (found) ws.state = kCatching; }
                    else if (ws.state == kCatching) { if (killed) ws.state = kFull; else if (!found) ws.state = kNoUnits; }
                    else if (ws.state == kFull) { if (!killed) ws.state = found ? kCatching : kNoUnits; }
                    if (was != ws.state) sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWorm << 16) | int64_t(i) | (int64_t(ws.state) << 8), ws.seg[0].pos});
                }
            }
        }
        // state machine (BW Worm.UpdateState)
        switch (ws.state) {
        case kNoUnits: case kCatching: case kFull: case kPreparing:
            ws.timer -= dt;
            if (ws.state == kNoUnits) {
                const Fixed v = d.noUnits * d.speed;
                const Fixed digTime = v.raw > 0 ? d.appearDistance / v : Fixed{};
                if (ws.timer <= digTime) {
                    ws.timer = digTime; ws.state = kPreparing;
                    sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWorm << 16) | int64_t(i) | (int64_t(kPreparing) << 8), ws.seg.empty() ? Vec3{} : ws.seg[0].pos});
                }
            } else if (ws.state == kPreparing && ws.timer.raw <= 0) wormDigDown(sim, st, i);
            break;
        case kUnder:
            ws.timer -= dt;
            if (ws.timer.raw > 0) break;
            if (!ws.armySpawned && wormArmySpot(sim, st, i)) {
                ws.timer = d.appearTime; ws.state = kAwaitArmy;
                sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWorm << 16) | int64_t(i) | (int64_t(kAwaitArmy) << 8), ws.armyPos});
                break;
            }
            wormDigUp(sim, st, i);
            break;
        case kAwaitArmy:
            ws.timer -= dt;
            if (ws.timer.raw <= 0) ws.state = kArmy;
            break;
        case kArmy:
            wormSpawnArmy(sim, st, i);
            break;
        case kPostArmy:
            ws.timer -= dt;
            if (ws.timer.raw <= 0) wormDigDown(sim, st, i);
            break;
        }
        // the army group is gone once every unit of it entered a building or died (BW OnGroupRemoving)
        if (ws.armyGroup) {
            bool pending = false, alive = false;
            for (auto& s : st.sends) if (s.group == ws.armyGroup) pending = true;
            if (!pending) w.each<GroupTag>([&](Entity, GroupTag& g) { if (g.id == ws.armyGroup) alive = true; });
            if (!pending && ws.state == kArmy) { ws.state = kPostArmy; ws.timer = d.disappearTime; }   // BW OnGroupClosed
            if (!pending && !alive) {
                for (size_t k = 0; k < st.minionGroups.size(); ++k)
                    if (st.minionGroups[k] == ws.armyGroup) { st.minionGroups.erase(st.minionGroups.begin() + long(k)); st.minionSpeed.erase(st.minionSpeed.begin() + long(k)); break; }
                ws.armyGroup = 0;
            }
        }
        // slime traces: emitted behind the moving head, decaying; a kill zone until the tail has passed
        for (size_t k = 0; k < ws.traces.size();) {
            EnvTrace& t = ws.traces[k];
            t.timer -= dt;
            if (t.timer.raw < 0) { ws.traces.erase(ws.traces.begin() + long(k)); continue; }
            if (t.kill && !ws.seg.empty()) t.kill = ws.seg.back().rel <= t.rel;
            ++k;
        }
        if (onGround(ws.state) && !ws.seg.empty()) {
            ws.traceTimer -= dt;
            if (ws.traceTimer.raw <= 0) {
                ws.traceTimer += d.traceEmit;
                Vec3 x = ws.seg[0].pos - ws.seg[0].prev;
                if (x.xz().lengthSq().raw > 0) {
                    x = norm2(x);
                    const Vec3 y{x.z, Fixed{}, -x.x};
                    EnvTrace t;
                    t.rel = ws.seg[0].rel; t.timer = d.traceDecay; t.pos = ws.seg[0].pos;
                    t.left = t.pos - y * (d.traceWidth / 2); t.right = t.pos + y * (d.traceWidth / 2);
                    ws.traces.push_back(t);
                }
            }
        }
        // walkers on a trace: the kill part eats them, the rest is slowed (speed hook)
        if (ws.traces.size() >= 2) {
            auto side = [](Vec3 o, Vec3 a, Vec3 b) { return (a.x - o.x) * (b.z - o.z) - (a.z - o.z) * (b.x - o.x); };
            auto inTri = [&](Vec3 q, Vec3 t0, Vec3 t1, Vec3 t2) {
                Fixed d1 = side(t0, t1, q), d2 = side(t1, t2, q), d3 = side(t2, t0, q);
                bool neg = d1.raw < 0 || d2.raw < 0 || d3.raw < 0, pos = d1.raw > 0 || d2.raw > 0 || d3.raw > 0;
                return !(neg && pos);
            };
            for (Entity e : units_(sim)) {
                if (!w.has<LaneWalker>(e) || (m_->rules().types[w.get<Unit>(e).type].layer & kLayerAir)) continue;
                Vec3 p = w.get<Transform>(e).pos;
                for (size_t k = 1; k < ws.traces.size(); ++k) {
                    const EnvTrace& a = ws.traces[k - 1]; const EnvTrace& b = ws.traces[k];
                    if (!inTri(p, a.right, a.left, b.right) && !inTri(p, a.left, b.right, b.left)) continue;
                    if (b.kill) kill(sim, st, e, false);
                    break;
                }
            }
        }
    }
}

// ================================================================= toad

void Environment::toads(Simulation& sim, EnvState& st) {
    World& w = sim.world();
    const Fixed dt = sim.dt();
    const Rules& R = m_->rules();
    for (size_t i = 0; i < st.toads.size(); ++i) {
        const EnvToadDef& d = def_.toads[i];
        EnvToadState& ts = st.toads[i];
        ts.idle -= dt;
        if (ts.idle.raw < 0) ts.idle = d.idleSwitch;   // view: idle animation switch
        auto downgrade = [&]() {
            // every building above level 1 drops to its level-1 type (walks the upgrade chain back)
            std::vector<std::pair<Entity, uint16_t>> todo;
            w.each<Garrison, Unit>([&](Entity e, Garrison&, Unit& u) {
                uint16_t t = u.type;
                for (int guard = 0; guard < 16 && R.types[t].level > 1; ++guard) {
                    bool found = false;
                    for (size_t ti = 0; ti < R.types.size() && !found; ++ti)
                        for (uint16_t up : R.types[ti].upgrades) if (up == t) { t = uint16_t(ti); found = true; break; }
                    if (!found) break;
                }
                if (t != u.type) todo.push_back({e, t});
            });
            for (auto& [e, t] : todo) detail::changeUnitType(*m_, sim, e, t);
        };
        const bool exploding = ts.explode.raw > 0 || ts.explodeDelay.raw > 0;
        // attack (BW Toad.UpdateAttack)
        bool skipFind = false;
        if (ts.attack.raw >= 0) {
            ts.attack -= dt;
            if (ts.target.valid() && !w.alive(ts.target)) {   // DiscardTarget: it reached its building
                ts.target = kNullEntity;
                if (ts.attack < d.attack / 2) ts.attack = d.attack - ts.attack;
            }
            if (ts.target.valid() && ts.attack <= d.attack / 2) {
                Entity victim = ts.target;
                ts.target = kNullEntity;
                Unit& u = w.get<Unit>(victim);
                if (u.hp.raw > 0 && !R.types[u.type].invulnerable && !(m_->statusFlags(sim, victim) & kStInvulnerable)) {
                    kill(sim, st, victim, true);
                    if (++ts.units >= d.maxUnits && d.maxUnits > 0) {
                        ts.units = 0;
                        ts.explodeDelay = d.explodeDelay; ts.explode = d.explosion;
                        if (ts.explodeDelay.raw <= 0) downgrade();
                        sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, kEnvToadExplode << 16 | int64_t(i), d.pos});
                    }
                }
            }
            skipFind = true;
        } else if (ts.post == 1 && d.postAttack.raw > 0) {
            ts.attack = d.postAttack; ts.post = 2; skipFind = true;
        }
        if (!skipFind && !exploding) {
            // a random unit in the toad zone that will not reach its building before the tongue does
            std::vector<Entity> cand;
            const Fixed catchTime = d.attack / 2;
            for (Entity e : units_(sim)) {
                if (cand.size() >= 100) break;
                Vec3 p = w.get<Transform>(e).pos;
                bool in = false;
                for (auto& z : def_.zones) if (z.kind == EnvZoneKind::Toad && (d.zone.empty() || z.id == d.zone) && z.contains(p)) { in = true; break; }
                if (!in) continue;
                Fixed sp = m_->stat(sim, e, Stat::Speed);
                if (auto* od = w.tryGet<Orders>(e); od && !od->queue.empty() && w.alive(od->queue.front().target) && sp.raw > 0) {
                    Fixed left = dist2D(p, w.get<Transform>(od->queue.front().target).pos) / sp;
                    if (left < catchTime) continue;
                }
                cand.push_back(e);
            }
            if (!cand.empty()) {
                ts.target = cand[sim.rng().below(uint32_t(cand.size()))];
                ts.lastTarget = w.get<Transform>(ts.target).pos;
                ts.attack = d.attack; ts.post = 1;
                sim.emit({kEvEnv, 0, ts.target, kNullEntity, kEnvToadAttack << 16 | int64_t(i), d.pos});
            }
        }
        if (ts.post == 2 && ts.attack.raw < 0) ts.post = 0;
        if (ts.target.valid() && w.alive(ts.target)) ts.lastTarget = w.get<Transform>(ts.target).pos;
        // explosion (BW Toad.UpdateExplosion)
        if (ts.explode.raw > 0 || ts.explodeDelay.raw > 0) {
            Fixed step = dt;
            bool run = true;
            if (ts.explodeDelay.raw > 0) {
                ts.explodeDelay -= dt;
                if (ts.explodeDelay.raw <= 0) { step = dt + ts.explodeDelay; downgrade(); }
                else run = false;
            }
            if (run) ts.explode -= step;
        }
    }
}

// ================================================================= flying bomber

void Environment::flyers(Simulation& sim, EnvState& st) {
    World& w = sim.world();
    const Fixed dt = sim.dt();
    for (size_t i = 0; i < st.flyers.size(); ++i) {
        const EnvFlyerDef& d = def_.flyers[i];
        EnvFlyerState& fs = st.flyers[i];
        auto removeBody = [&]() {
            if (w.alive(fs.body)) { Unit& u = w.get<Unit>(fs.body); u.hp = Fixed{}; u.flags |= kUnitSilent; }
            fs.body = kNullEntity;
        };
        auto posAt = [&](Fixed t) {
            const Fixed T = fs.length / d.velocity;
            Fixed k = T.raw > 0 ? fxClamp(t, Fixed{}, T) / T : Fixed{};
            Vec3 p = fs.from + (fs.to - fs.from) * k;
            p.x += fxSinTurns(d.frequency.x * t) * d.amplitude.x;
            p.z += fxSinTurns(d.frequency.z * t) * d.amplitude.z;
            return p;
        };
        if (fs.state == 0) {   // idle
            fs.idle -= dt;
            if (fs.idle.raw <= 0) {
                fs.state = 1; fs.fly = Fixed{}; fs.dropped = 0;
                fs.height = d.heightMin + (d.heightMax - d.heightMin) * sim.rng().unit();
                fs.rally = int(sim.rng().below(uint32_t(d.rally.size())));
                fs.from = d.rally[size_t(fs.rally)].first; fs.to = d.rally[size_t(fs.rally)].second;
                fs.length = (fs.to - fs.from).xz().length();
                fs.pos = fs.from;
                fs.drop = (d.maxBombs >= 0 && fs.dropped >= d.maxBombs) ? Fixed{} : d.dropMin + (d.dropMax - d.dropMin) * sim.rng().unit();
                if (d.type != 0xFFFF && fs.health > 0) fs.body = m_->spawn(sim, d.type, kHostileNeutral, fs.from);
            }
        } else if (fs.state == 1) {   // flying
            fs.fly += dt;
            const Vec3 last = fs.pos;
            fs.pos = posAt(fs.fly);
            const Vec3 vel = (fs.pos - last) / dt;
            if (w.alive(fs.body)) { Transform& t = w.get<Transform>(fs.body); t.pos.x = fs.pos.x; t.pos.z = fs.pos.z; }
            if (fs.drop.raw > 0) {
                fs.drop -= dt;
                if (fs.drop.raw <= 0) {
                    EnvBomb b;
                    b.start = b.pos = fs.pos; b.vel = vel;
                    b.height = d.heightMin + (d.heightMax - d.heightMin) * sim.rng().unit();
                    fs.bombs.push_back(b);
                    ++fs.dropped;
                    sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, kEnvBomb << 16, fs.pos});
                    fs.drop = (d.maxBombs >= 0 && fs.dropped >= d.maxBombs) ? Fixed{} : d.dropMin + (d.dropMax - d.dropMin) * sim.rng().unit();
                }
            }
            if (fs.fly * d.velocity >= fs.length) {
                fs.state = 0;
                fs.idle = d.idleMin + (d.idleMax - d.idleMin) * sim.rng().unit();
                removeBody();
            }
        }
        // bombs fall (BW Bomb: start + v t + g t^2 / 2 until they dropped `height`)
        for (size_t k = 0; k < fs.bombs.size();) {
            EnvBomb& b = fs.bombs[k];
            b.t += dt;
            const Fixed fall = -d.gravity * b.t * b.t / 2;
            b.pos = b.start + b.vel * b.t;
            if (fall < b.height) { ++k; continue; }
            if (d.skill >= 0) cast(sim, 0, d.skill, b.pos);
            sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, kEnvBomb << 16 | 1, b.pos});
            fs.bombs.erase(fs.bombs.begin() + long(k));
        }
        if (fs.state == 1 && fs.health <= 0) { fs.state = 2; removeBody(); }   // shot down: never comes back
    }
}

} // namespace aiverse::game
