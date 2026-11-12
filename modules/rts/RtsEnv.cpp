// Environment runtime (RtsEnv.h): setup, system order, weather, wind, zones, trample fields, per-unit state,
// speed hook, hash and saves. Bosses: RtsEnvBoss.cpp; Mouro / scroller / off-map / auto skills:
// RtsEnvScript.cpp; caves / graveyards / souls / fire / walls: RtsEnvPoints.cpp.
#include "RtsEnv.h"
#include "RtsInternal.h"
#include "../../engine/core/FixedMath.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {

using namespace detail;

void loadEnvironment(EnvDef& d, const Rules& r, const Json& rulesJ, const Json& mapJ);   // RtsEnvData.cpp

// ================================================================= geometry

void EnvSpline::build(const std::vector<Vec3>& pts, bool lp, int smooth) {
    points = pts; loop = lp;
    nodes.clear(); cum.clear(); length = Fixed{};
    const size_t n = pts.size();
    if (n == 0) return;
    if (n == 1) { nodes = pts; cum = {Fixed{}}; return; }
    // uniform Catmull-Rom through every control point; open splines clamp the end tangents
    auto P = [&](long i) -> Vec3 {
        if (loop) return pts[size_t(((i % long(n)) + long(n)) % long(n))];
        return pts[size_t(std::clamp<long>(i, 0, long(n) - 1))];
    };
    const long segs = loop ? long(n) : long(n) - 1;
    const Fixed half = Fixed::fromRatio(1, 2);
    for (long sgm = 0; sgm < segs; ++sgm) {
        Vec3 p0 = P(sgm - 1), p1 = P(sgm), p2 = P(sgm + 1), p3 = P(sgm + 2);
        for (int k = 0; k < smooth; ++k) {
            Fixed t = Fixed::fromInt(k) / smooth, t2 = t * t, t3 = t2 * t;
            auto f = [&](Fixed a, Fixed b, Fixed c, Fixed dd) {
                return half * (b * 2 + (c - a) * t + (a * 2 - b * 5 + c * 4 - dd) * t2 + (b * 3 - a - c * 3 + dd) * t3);
            };
            nodes.push_back({f(p0.x, p1.x, p2.x, p3.x), Fixed{}, f(p0.z, p1.z, p2.z, p3.z)});
        }
    }
    nodes.push_back(loop ? pts[0] : pts[n - 1]);
    cum.assign(nodes.size(), Fixed{});
    for (size_t i = 1; i < nodes.size(); ++i) cum[i] = cum[i - 1] + (nodes[i] - nodes[i - 1]).xz().length();
    length = cum.back();
}

Vec3 EnvSpline::at(Fixed d) const {
    if (nodes.empty()) return {};
    if (d <= Fixed{} || nodes.size() == 1) return nodes.front();
    if (d >= length) return nodes.back();
    size_t i = size_t(std::upper_bound(cum.begin(), cum.end(), d) - cum.begin());
    i = std::clamp<size_t>(i, 1, nodes.size() - 1);
    Fixed seg = cum[i] - cum[i - 1];
    if (seg.raw <= 0) return nodes[i];
    return nodes[i - 1] + (nodes[i] - nodes[i - 1]) * ((d - cum[i - 1]) / seg);
}

bool EnvZone::contains(Vec3 p) const {
    if (radius.raw > 0) return (p - center).xz().lengthSq() <= radius * radius;
    bool in = false;
    for (size_t i = 0, j = poly.size() - 1; i < poly.size(); j = i++) {
        const Vec3& a = poly[i]; const Vec3& b = poly[j];
        if ((a.z > p.z) != (b.z > p.z)) {
            Fixed x = a.x + (b.x - a.x) * ((p.z - a.z) / (b.z - a.z));
            if (p.x < x) in = !in;
        }
    }
    return in;
}

// ================================================================= setup

EnvState* Environment::state(Simulation& sim) const { return sim.world().resource<EnvState>(); }

bool Environment::complexityOk(const std::string& c) const { return c.empty() || c == "any" || c == matchComplexity_; }

void Environment::setup(Simulation& sim, RtsModule& m, const Json& rules, const Json& map) {
    m_ = &m;
    def_ = EnvDef{};
    loadEnvironment(def_, m.rules(), rules, map);
    if (!def_.active) return;
    matchComplexity_ = def_.complexity;
    // grid field references (trample, lightning burn)
    GridFields* gf = m.gridFields();
    auto fieldRef = [&](const std::string& id, const char* where) -> int16_t {
        if (id.empty()) return -1;
        int f = gf ? gf->find(id) : -1;
        if (f < 0) throw std::runtime_error(std::string("environment.") + where + ": unknown grid field '" + id + "' (rules \"fields\")");
        return int16_t(f);
    };
    def_.weather.burnField = fieldRef(def_.weather.burnId, "weatherParams.lightningBurn");
    for (auto& t : def_.trample) { t.field = fieldRef(t.fieldId, "trample"); t.killField = fieldRef(t.killId, "trample.killField"); }
    // only the worm configs of this match's complexity (BW WormConfigs.GetByComplexity: the first match)
    std::vector<EnvWormDef> worms;
    for (auto& w : def_.worms) if (complexityOk(w.complexity)) { worms.push_back(w); break; }
    def_.worms = std::move(worms);
    for (auto& h : def_.mouro.holders) {
        bool ok = h.complexities.empty();
        for (auto& c : h.complexities) ok |= complexityOk(c);
        if (!ok) h.actions.clear();
    }

    EnvState& st = sim.world().setResource<EnvState>();
    World& w = sim.world();
    GameState& gs = m.state(sim);
    // map points: map entity index -> spawned entity (same type, nearest to the map position, each used once)
    const auto& ents = map["entities"].items();
    st.points.assign(ents.size(), kNullEntity);
    std::vector<uint64_t> used;
    for (size_t i = 0; i < ents.size(); ++i) {
        int t = m.rules().findType(ents[i].getStr("type"));
        const Json& pj = ents[i].has("at") ? ents[i]["at"] : ents[i]["pos"];
        if (t < 0 || !pj.isArray()) continue;
        Vec3 p{pj[0].fx(), Fixed{}, pj[pj.size() >= 3 ? 2 : 1].fx()};
        Entity best; Fixed bd{};
        w.each<Unit, Transform>([&](Entity e, Unit& u, Transform& tr) {
            if (u.type != uint16_t(t) || std::find(used.begin(), used.end(), e.packed()) != used.end()) return;
            Fixed d = dist2D(tr.pos, p);
            if (!best.valid() || d < bd) { best = e; bd = d; }
        });
        if (best.valid()) { st.points[i] = best; used.push_back(best.packed()); }
    }
    const size_t np = gs.players.size();
    st.lastAction.assign(np, {});
    st.invincible.assign(np, 0);
    st.skillCast.assign(np, 0);
    st.soulAcc.assign(ents.size() * 2, Fixed{});
    // weather: BW starts the puddle cycle at 0 filling; no rain = dry ground (puddles 1 = full water slowdown)
    st.puddles = def_.weather.rain ? Fixed{} : Fixed::fromInt(1);
    st.blizzardState = def_.weather.blizzard ? 0 : 1;
    if (def_.wind.enabled) { st.windIdle = range(sim, def_.wind.idleMin, def_.wind.idleMax); st.windVisual = -1; }
    st.worms.assign(def_.worms.size(), {});
    for (size_t i = 0; i < def_.worms.size(); ++i) {
        auto& ws = st.worms[i];
        ws.seg.assign(def_.worms[i].segments.size(), {});
        ws.headTimer = def_.worms[i].headSwitch; ws.scanTimer = def_.worms[i].scanPeriod;
        wormDigDown(sim, st, i);   // BW: the worm starts underground
    }
    st.toads.assign(def_.toads.size(), {});
    for (size_t i = 0; i < def_.toads.size(); ++i) st.toads[i].idle = def_.toads[i].idleSwitch;
    st.flyers.assign(def_.flyers.size(), {});
    for (size_t i = 0; i < def_.flyers.size(); ++i) {
        st.flyers[i].health = def_.flyers[i].health;
        st.flyers[i].idle = range(sim, def_.flyers[i].idleMin, def_.flyers[i].idleMax);
    }
    st.mouro.holders.assign(def_.mouro.holders.size(), {});
    for (size_t i = 0; i < def_.mouro.holders.size(); ++i) {
        auto& hs = st.mouro.holders[i];
        hs.delay = def_.mouro.holders[i].delay;
        hs.actions.assign(def_.mouro.holders[i].actions.size(), {});
        for (size_t k = 0; k < hs.actions.size(); ++k) hs.order.push_back(uint16_t(k));
    }
    st.mouro.emotionPlays.assign(def_.mouro.emotions.size(), 0);
    st.scrollerState.assign(def_.scroller.size(), 0);
    st.scrollerTimer.assign(def_.scroller.size(), Fixed{});
    st.screenPos = def_.screen.start;
    st.autos.assign(def_.splines.size(), {});
    for (size_t i = 0; i < def_.splines.size(); ++i) {
        const EnvSpline& sp = def_.splines[i];
        st.autos[i].timer = sp.firstEmission.raw > 0 ? sp.firstEmission : sp.emitPeriod;
    }
    for (auto& g : def_.caves) {
        EnvCave c;
        Entity e0 = st.points[size_t(g[0])];
        if (w.alive(e0)) { c.owner = w.get<Owner>(e0).player; if (auto* ga = w.tryGet<Garrison>(e0)) c.count = ga->count; }
        st.caves.push_back(c);
        for (int pi : g) if (Entity e = st.points[size_t(pi)]; w.alive(e)) { w.get<Owner>(e).player = c.owner; if (auto* ga = w.tryGet<Garrison>(e)) ga->count = c.count; }
    }
    if (def_.graveyardType != 0xFFFF)
        w.each<Unit>([&](Entity e, Unit& u) { if (u.type == def_.graveyardType) { st.graveyards.push_back(e); st.graveyardTimer.push_back(Fixed{}); } });
    for (auto& wd : def_.walls) {
        EnvWall wl; wl.e = st.points[size_t(wd.point)]; wl.hits = wd.hits; wl.timer = wd.destroyTime;
        if (w.alive(wl.e) && !wd.ignoreTowers) w.get<Owner>(wl.e).player = kHostileNeutral;   // every tower shoots walls
        st.walls.push_back(wl);
    }
}

// ================================================================= helpers

Fixed Environment::range(Simulation& sim, Fixed lo, Fixed hi) const {
    if (hi <= lo) return lo;
    return lo + (hi - lo) * sim.rng().unit();
}

uint8_t Environment::slotOf(Simulation&, uint8_t who) const {
    // BW MouroBoss.GetPlayer: the human player / the bot; here map "environment.mouro.player" / "bot" (default slots 1 / 2)
    if (who == 2) return 0;
    if (who == 1) return def_.mouro.bot ? def_.mouro.bot : 2;
    return def_.mouro.player ? def_.mouro.player : 1;
}

std::vector<Entity> Environment::units_(Simulation& sim) const {
    std::vector<Entity> out;
    World& w = sim.world();
    const Rules& R = m_->rules();
    w.each<Unit, Owner, Transform>([&](Entity e, Unit& u, Owner&, Transform&) {
        const UnitType& t = R.types[u.type];
        if (u.hp.raw <= 0 || t.structure || t.speed.raw <= 0 || (u.flags & kUnitOffMap) || t.invulnerable) return;
        out.push_back(e);
    });
    return out;
}

void Environment::kill(Simulation& sim, EnvState& st, Entity e, bool noSoul) {
    World& w = sim.world();
    if (!w.alive(e)) return;
    Unit& u = w.get<Unit>(e);
    if (u.hp.raw <= 0) return;
    if (m_->rules().types[u.type].invulnerable || (m_->statusFlags(sim, e) & kStInvulnerable)) return;
    u.hp = Fixed{};
    u.lastAttacker = 0;
    if (noSoul) st.noSoul.push_back(e.packed());
}

void Environment::cast(Simulation& sim, uint8_t player, int skill, Vec3 point, Entity target, std::vector<Entity>* areas) {
    const Rules& R = m_->rules();
    if (skill < 0 || size_t(skill) >= R.playerAbilities.size()) return;
    const size_t ev0 = sim.events().size();
    sim.emit({kEvPlayerAbility, player, kNullEntity, target, skill, flat(point)});
    EffectCtx ctx;
    ctx.owner = player; ctx.point = flat(point); ctx.origin = ctx.point; ctx.target = target;
    m_->applyPayload(sim, R.playerAbilities[size_t(skill)].payload, ctx);
    if (areas)
        for (size_t i = ev0; i < sim.events().size(); ++i)
            if (sim.events()[i].type == kEvAreaStart) areas->push_back(sim.events()[i].a);
}

int Environment::pointIndex(Simulation& sim, Entity e) const {
    if (EnvState* st = state(sim))
        for (size_t i = 0; i < st->points.size(); ++i) if (st->points[i] == e) return int(i);
    return -1;
}
Entity Environment::point(Simulation& sim, int i) const {
    EnvState* st = state(sim);
    if (!st || i < 0 || size_t(i) >= st->points.size() || !sim.world().alive(st->points[size_t(i)])) return kNullEntity;
    return st->points[size_t(i)];
}

bool Environment::insideScreen(Simulation& sim, Vec3 p) const {
    if (!def_.screen.enabled) return true;
    EnvState* st = state(sim);
    if (!st) return true;
    return p.x >= st->screenPos.x && p.z >= st->screenPos.z && p.x <= st->screenPos.x + def_.screen.size.x && p.z <= st->screenPos.z + def_.screen.size.z;
}

bool Environment::producing(Simulation& sim, Entity s) const {
    if (!def_.screen.enabled) return true;
    return insideScreen(sim, sim.world().get<Transform>(s).pos);
}

// ================================================================= tick

void Environment::tick(Simulation& sim) {
    EnvState* stp = state(sim);
    if (!stp) return;
    EnvState& st = *stp;
    st.noSoul.clear();
    weather(sim, st);
    wind(sim, st);
    units(sim, st);
    trample(sim, st);
    worms(sim, st);
    toads(sim, st);
    flyers(sim, st);
    outmap(sim, st);
    autos(sim, st);
    mouro(sim, st);
    scroller(sim, st);
    soulsTick(sim, st);
    sendsTick(sim, st);
    graveyards(sim, st);
    firesTick(sim, st);
}

void Environment::lateTick(Simulation& sim) {
    EnvState* stp = state(sim);
    if (!stp) return;
    EnvState& st = *stp;
    // captures of this tick (any system): last action buildings, cave groups, walls that fall with their points
    for (const SimEvent& ev : sim.events())
        if (ev.type == kEvCaptured) onCaptured(sim, ev.a, uint8_t(ev.value), ev.player);
    caves(sim, st);
    wallsTick(sim, st);
    std::fill(st.skillCast.begin(), st.skillCast.end(), uint8_t(0));
}

// ================================================================= weather (BW Environment.Module)

void Environment::weather(Simulation& sim, EnvState& st) {
    const EnvWeatherDef& W = def_.weather;
    const Fixed dt = sim.dt(), one = Fixed::fromInt(1);
    if (W.rain) {
        const uint8_t before = st.puddlesState;
        if (st.puddlesState == 0) { st.puddles += dt / W.puddlesFill; if (st.puddles >= one) st.puddlesState = 1; }
        else if (st.puddlesState == 1) { st.rainTimer += dt / W.rainWait; if (st.rainTimer >= one) { st.rainTimer = Fixed{}; st.puddlesState = 2; } }
        else if (st.puddlesState == 2) { st.puddles -= dt / W.puddlesDry; if (st.puddles <= Fixed{}) st.puddlesState = 3; }
        else { st.rainTimer += dt / W.noRainWait; if (st.rainTimer >= one) { st.rainTimer = Fixed{}; st.puddlesState = 0; } }
        if (before != st.puddlesState) sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWeather << 16) | 1 | (int64_t(st.puddlesState) << 8), {}});
    } else st.puddles = one;
    if (W.winter && W.snow) {
        if (st.snowState == 1) {
            // restoring snow over every trample field that follows the snow phase
            for (auto& t : def_.trample)
                if (t.snowRestore && t.field >= 0) {
                    GridFields* gf = m_->gridFields();
                    const GridFieldDef& fd = gf->defs()[size_t(t.field)];
                    const auto& f = gf->store(sim)->fields[size_t(t.field)];
                    gf->fillRect(sim, t.field, 0, 0, f.w - 1, f.h - 1, 0, -fd.raw(dt * W.snowRestore), 1);
                }
            st.snowTimer += dt / st.snowFill;
            if (st.snowTimer >= one) { st.snowTimer = Fixed{}; st.snowState = 0; sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWeather << 16) | 2, {}}); }
        } else {
            st.snowTimer += dt / W.snowWait;
            if (st.snowTimer >= one) {
                st.snowTimer = Fixed{};
                if (sim.rng().unit() <= W.snowFillChance) {
                    st.snowFill = fxMax(range(sim, W.snowFillMin, W.snowFillMax), Fixed::fromRatio(1, 10));
                    st.snowState = 1;
                    sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWeather << 16) | 2 | (1 << 8), {}});
                }
            }
        }
    }
    if (W.blizzard) {
        if (st.blizzardState == 0) { st.blizzardTimer += dt / W.blizzardFill; if (st.blizzardTimer >= one) { st.blizzardTimer = Fixed{}; st.blizzardState = 1; sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWeather << 16) | 4 | (1 << 8), {}}); } }
        else { st.blizzardTimer += dt / W.blizzardWait; if (st.blizzardTimer >= one) { st.blizzardTimer = Fixed{}; st.blizzardState = 0; sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWeather << 16) | 4, {}}); } }
    } else st.blizzardState = 1;
    if (W.lightning) {
        if (st.lightningState == 0) {
            st.lightningKill += dt / W.lightningKillTime;
            if (st.lightningKill >= one) {
                st.lightningKill = Fixed{};
                if (int(sim.rng().below(100)) <= W.lightningKillChance) {
                    World& w = sim.world();
                    std::vector<Entity> objs = units_(sim);
                    const size_t nUnits = objs.size();
                    w.each<Garrison, Unit>([&](Entity e, Garrison&, Unit& u) { if (u.hp.raw > 0) objs.push_back(e); });
                    if (!objs.empty()) {
                        const size_t k = sim.rng().below(uint32_t(objs.size()));
                        Entity obj = objs[k];
                        Vec3 p = w.get<Transform>(obj).pos;
                        if (k < nUnits) {
                            kill(sim, st, obj, true);
                            const Fixed rx = W.lightningRange, rz = W.lightningRange / 2;   // BW ellipse (range, range / 2)
                            for (size_t i = 0; i < nUnits; ++i) {
                                Vec3 q = w.get<Transform>(objs[i]).pos - p;
                                if (rx.raw <= 0 || rz.raw <= 0) break;
                                Fixed a = q.x / rx, b = q.z / rz;
                                if (a * a + b * b <= one) kill(sim, st, objs[i], true);
                            }
                            if (W.burnField >= 0) {
                                GridFields* gf = m_->gridFields();
                                gf->fillCircle(sim, W.burnField, p.x, p.z, W.burnRadius.raw > 0 ? W.burnRadius : W.lightningRange, 0, W.burnValue, 2);
                            }
                        } else if (auto* g = w.tryGet<Garrison>(obj)) {
                            int n = std::min(g->count, W.lightningStructureKill);
                            g->count -= n; g->lost += n;
                            if (n) sim.emit({kEvGarrisonChanged, w.get<Owner>(obj).player, obj, kNullEntity, -n, p});
                        }
                        st.lightningPos = p;
                        sim.emit({kEvEnv, 0, obj, kNullEntity, kEnvLightning << 16, p});
                    }
                }
            }
            st.lightningTimer += dt / W.lightningTime;
            if (st.lightningTimer >= one) { st.lightningTimer = Fixed{}; st.lightningState = 1; }
        } else {
            st.lightningTimer += dt / W.lightningWait;
            if (st.lightningTimer >= one) { st.lightningTimer = Fixed{}; st.lightningState = 0; }
        }
    }
}

// ================================================================= wind (BW Wind)

void Environment::wind(Simulation& sim, EnvState& st) {
    const EnvWindDef& N = def_.wind;
    if (!N.enabled) return;
    const Fixed dt = sim.dt();
    if (st.windIdle.raw > 0) {
        st.windIdle -= dt;
        if (st.windIdle.raw <= 0) {
            st.windBlow = range(sim, N.blowMin, N.blowMax);
            st.windDir = (st.windDir + 1) % 8;
            st.windVisual = st.windDir;
            const Fixed f = range(sim, N.forceMin, N.forceMax);
            const Fixed turns = Fixed::fromInt(st.windDir) / 8;   // pi * dir / 4
            st.windForce = {f * fxSinTurns(turns), Fixed{}, f * fxCosTurns(turns)};
            sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWind << 16) | st.windDir, {}});
        }
    } else if (st.windBlow.raw > 0) {
        st.windBlow -= dt;
        if (st.windBlow.raw <= 0) {
            st.windIdle = range(sim, N.idleMin, N.idleMax);
            st.windVisual = -1;
            sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvWind << 16) | 0xFF, {}});
        }
    }
}

Fixed Environment::windMultiplier(Simulation& sim, Vec3 dir) const {
    EnvState* st = state(sim);
    if (!st || !def_.wind.enabled || st->windBlow.raw <= 0) return {};
    Fixed len = dir.xz().length();
    if (len.raw <= 0) return {};
    return (dir.x * st->windForce.x + dir.z * st->windForce.z) / len;
}

// ================================================================= per-unit state (blizzard), trample

void Environment::units(Simulation& sim, EnvState& st) {
    const EnvWeatherDef& W = def_.weather;
    if (!W.blizzard) return;
    World& w = sim.world();
    const Fixed delta = (st.blizzardState == 0 ? sim.dt() : Fixed{}) / W.blizzardKillSpeed;
    for (Entity e : units_(sim)) {
        if (st.units.size() <= e.index) st.units.resize(e.index + 1);
        EnvUnit& eu = st.units[e.index];
        if (!eu.init || eu.gen != e.gen) { eu = EnvUnit{}; eu.gen = e.gen; eu.init = true; eu.resist = range(sim, W.blizzardResistMin, W.blizzardResistMax); }
        if (delta.raw <= 0 || !w.has<LaneWalker>(e)) continue;   // BW: only units on the way between buildings freeze
        eu.blizzard -= delta / fxMax(eu.resist, Fixed::fromRatio(1, 100));
        if (eu.blizzard.raw <= 0) kill(sim, st, e, true);
    }
}

void Environment::trample(Simulation& sim, EnvState& st) {
    if (def_.trample.empty()) return;
    GridFields* gf = m_->gridFields();
    World& w = sim.world();
    for (auto& t : def_.trample) {
        if (t.field < 0) continue;
        const GridFieldDef& fd = gf->defs()[size_t(t.field)];
        const int32_t add = fd.raw(t.perSecond * sim.dt());
        for (Entity e : units_(sim)) {
            const bool moving = w.has<LaneWalker>(e) || (w.has<Mobile>(e) && w.get<Mobile>(e).moving);
            if (!moving || (m_->rules().types[w.get<Unit>(e).type].layer & kLayerAir)) continue;
            Vec3 p = w.get<Transform>(e).pos;
            int cx, cz;
            if (add != 0 && gf->cellOf(t.field, p.x, p.z, cx, cz)) gf->write(sim, t.field, cx, cz, 0, add, 1);
            if (t.killField >= 0 && gf->cellOf(t.killField, p.x, p.z, cx, cz) && gf->get(sim, t.killField, cx, cz) >= t.killAbove) kill(sim, st, e, false);
            if (t.hideStatus >= 0 && gf->cellOf(t.field, p.x, p.z, cx, cz) && fd.toFixed(gf->get(sim, t.field, cx, cz)) > t.hideAbove) {
                EffectCtx ctx; ctx.owner = 0;
                m_->addStatus(sim, e, t.hideStatus, Fixed::fromRatio(1, 5), ctx);
            }
        }
    }
}

// ================================================================= speed hook

void Environment::speed(Simulation& sim, Entity e, Fixed& add, Fixed& mul) const {
    EnvState* st = state(sim);
    if (!st) return;
    World& w = sim.world();
    const Unit& u = w.get<Unit>(e);
    const UnitType& t = m_->rules().types[u.type];
    if (t.structure) return;
    const Vec3 p = w.get<Transform>(e).pos;
    const bool air = t.layer & kLayerAir;
    const Fixed one = Fixed::fromInt(1);
    // blizzard health: lerp(blizzardSpeed * speed, speed, health)
    if (def_.weather.blizzard && e.index < st->units.size() && st->units[e.index].init && st->units[e.index].gen == e.gen) {
        const Fixed hb = fxClamp(st->units[e.index].blizzard, Fixed{}, one);
        mul *= def_.weather.blizzardSpeed + (one - def_.weather.blizzardSpeed) * hb;
    }
    // zones (ground only): speed / slowdown; water zones dry out with the puddles (summer rain)
    bool road = false;
    if (!air)
        for (auto& z : def_.zones) {
            if (!z.contains(p)) continue;
            if (z.kind == EnvZoneKind::Road) road = true;
            Fixed slow = z.slowdown;
            if (z.kind == EnvZoneKind::Water && !def_.weather.winter) slow = slow + (one - slow) * (one - st->puddles);
            if (slow.raw > 0) mul = mul / slow;
            break;   // BW zones: the first zone containing the point
        }
    // worm traces slow walkers down (skillSlowdownCoefficient /= trace coefficient)
    if (!air)
        for (size_t i = 0; i < st->worms.size(); ++i) {
            const auto& ws = st->worms[i];
            for (size_t k = 1; k < ws.traces.size(); ++k) {
                const EnvTrace& a = ws.traces[k - 1]; const EnvTrace& b = ws.traces[k];
                if (b.timer.raw < 0 || b.kill) continue;
                auto side = [](Vec3 o, Vec3 x, Vec3 y) { return (x.x - o.x) * (y.z - o.z) - (x.z - o.z) * (y.x - o.x); };
                auto inTri = [&](Vec3 q, Vec3 t0, Vec3 t1, Vec3 t2) {
                    Fixed d1 = side(t0, t1, q), d2 = side(t1, t2, q), d3 = side(t2, t0, q);
                    bool neg = d1.raw < 0 || d2.raw < 0 || d3.raw < 0, pos = d1.raw > 0 || d2.raw > 0 || d3.raw > 0;
                    return !(neg && pos);
                };
                if (inTri(p, a.right, a.left, b.right) || inTri(p, a.left, b.right, b.left)) { mul *= def_.worms[i].traceSlowdown; goto traced; }
            }
        }
traced:
    // trample fields: winter speed table / grass lerp
    if (!air && !def_.trample.empty()) {
        GridFields* gf = m_->gridFields();
        for (auto& tr : def_.trample) {
            int cx, cz;
            if (tr.field < 0 || !gf->cellOf(tr.field, p.x, p.z, cx, cz)) continue;
            const Fixed v = gf->defs()[size_t(tr.field)].toFixed(gf->get(sim, tr.field, cx, cz));
            if (!tr.speed.empty() && !(road && tr.skipRoads)) {
                Fixed m = one;
                for (auto& row : tr.speed) if (v <= row.first) m = row.second;
                mul *= m;
            }
            if (tr.lerpSpeed.raw >= 0 && v.raw > 0) mul *= one + (tr.lerpSpeed - one) * fxClamp(v, Fixed{}, one);
        }
    }
    // groups of bosses walk faster (worm army: armySpeedCoefficient)
    if (!st->minionGroups.empty())
        if (auto* gt = w.tryGet<GroupTag>(e))
            for (size_t k = 0; k < st->minionGroups.size(); ++k) if (st->minionGroups[k] == gt->id) { mul *= st->minionSpeed[k]; break; }
    // wind: distance per second along the movement direction
    if (def_.wind.enabled && st->windBlow.raw > 0) add += windMultiplier(sim, u.facing);
}

// ================================================================= hash / saves

void Environment::hash(Simulation& sim, Hasher& h) const {
    EnvState* st = state(sim);
    if (!st) return;
    h.u32(st->puddlesState | st->snowState << 8 | st->blizzardState << 16 | st->lightningState << 24);
    h.fx(st->puddles); h.fx(st->rainTimer); h.fx(st->snowTimer); h.fx(st->blizzardTimer); h.fx(st->lightningTimer); h.fx(st->lightningKill);
    h.v3(st->windForce); h.fx(st->windBlow); h.fx(st->windIdle); h.i32(st->windDir);
    for (auto& w : st->worms) {
        h.u32(w.state); h.i32(w.path); h.fx(w.timer); h.fx(w.scanTimer); h.fx(w.traceTimer); h.u32(uint32_t(w.traces.size()));
        for (auto& s : w.seg) { h.fx(s.dist); h.v3(s.pos); }
        h.u32(w.armyGroup);
    }
    for (auto& t : st->toads) { h.fx(t.attack); h.i32(t.units); h.u64(t.target.packed()); h.fx(t.explode); }
    for (auto& f : st->flyers) { h.u32(f.state); h.fx(f.idle); h.fx(f.fly); h.v3(f.pos); h.i32(f.health); h.u32(uint32_t(f.bombs.size())); }
    h.u32(st->mouro.mode); h.i32(st->mouro.holder); h.i32(st->mouro.maxDomination);
    for (auto& hs : st->mouro.holders) { h.fx(hs.time); h.fx(hs.suspend); for (auto& a : hs.actions) { h.u32(a.state); h.fx(a.timer); } }
    for (size_t i = 0; i < st->scrollerState.size(); ++i) { h.u32(st->scrollerState[i]); h.fx(st->scrollerTimer[i]); }
    h.v3(st->screenPos); h.fx(st->outmapTimer); h.i32(st->outmapIndex);
    for (auto& a : st->autos) { h.fx(a.timer); h.u32(uint32_t(a.moving.size())); for (auto& mv : a.moving) h.fx(mv.dist); }
    h.u32(uint32_t(st->souls.size()));
    for (auto& s : st->souls) { h.fx(s.t); h.u64(s.target.packed()); }
    for (auto& s : st->sends) { h.u64(s.from.packed()); h.u64(s.to.packed()); h.i32(s.left); }
    for (auto& c : st->caves) { h.u32(c.owner); h.i32(c.count); }
    for (auto& g : st->graveyardTimer) h.fx(g);
    for (auto& f : st->fires) { h.u64(f.e.packed()); h.fx(f.timer); }
    for (auto& w : st->walls) { h.i32(w.hits); h.u32(w.state); h.fx(w.timer); }
    for (auto& u : st->units) h.fx(u.blizzard);
}

// save io of the environment state (found by ADL from Archive)
static void io(Archive& a, EnvSegment& x) { a(x.dist, x.rel, x.pos, x.prev); }
static void io(Archive& a, EnvTrace& x) { a(x.pos, x.left, x.right, x.timer, x.rel, x.kill); }
static void io(Archive& a, EnvWormState& x) {
    a(x.state, x.path, x.forward, x.timer, x.headTimer, x.scanTimer, x.traceTimer, x.upCounter, x.downCounter, x.armyCounter, x.headFrame, x.armySpawned,
      x.armyGroup, x.armyFrom, x.armyTo, x.armyLane, x.armyDist, x.armyPos, x.seg, x.traces, x.reverseArea);
}
static void io(Archive& a, EnvToadState& x) { a(x.idle, x.attack, x.explode, x.explodeDelay, x.units, x.target, x.lastTarget, x.post); }
static void io(Archive& a, EnvBomb& x) { a(x.start, x.vel, x.pos, x.t, x.height); }
static void io(Archive& a, EnvFlyerState& x) { a(x.state, x.idle, x.fly, x.drop, x.length, x.height, x.rally, x.dropped, x.health, x.from, x.to, x.pos, x.body, x.bombs); }
static void io(Archive& a, EnvActionState& x) { a(x.state, x.timer, x.await, x.executed); }
static void io(Archive& a, EnvHolderState& x) { a(x.time, x.delay, x.suspend, x.order, x.actions); }
static void io(Archive& a, EnvMouroState& x) { a(x.mode, x.holder, x.maxDomination, x.actionsDone, x.holders, x.emotionCooldown, x.emotionPlays, x.lastEmotion); }
static void io(Archive& a, EnvSoul& x) { a(x.from, x.to, x.vel, x.t, x.total, x.gravity, x.target, x.owner, x.targetOwner, x.add); }
static void io(Archive& a, EnvSend& x) { a(x.from, x.to, x.left, x.owner, x.group, x.unit, x.lane, x.lanePos, x.reverse, x.speedMul); }
static void io(Archive& a, EnvAuto::Moving& x) { a(x.area, x.dist); }
static void io(Archive& a, EnvAuto& x) { a(x.timer, x.late, x.moving); }
static void io(Archive& a, EnvCave& x) { a(x.owner, x.count); }
static void io(Archive& a, EnvWall& x) { a(x.e, x.hits, x.timer, x.state); }
static void io(Archive& a, EnvFire& x) { a(x.e, x.timer, x.start, x.percent); }
static void io(Archive& a, EnvUnit& x) { a(x.gen, x.blizzard, x.resist, x.init); }

void Environment::io(Simulation& sim, Archive& a) {
    EnvState* st = state(sim);
    if (!st) return;
    a(st->puddlesState, st->snowState, st->blizzardState, st->lightningState, st->puddles, st->rainTimer, st->snowTimer, st->snowFill, st->blizzardTimer,
      st->lightningTimer, st->lightningKill, st->lightningPos, st->windForce, st->windBlow, st->windIdle, st->windDir, st->windVisual);
    a(st->worms, st->toads, st->flyers, st->mouro, st->scrollerState, st->scrollerTimer, st->screenPos, st->screenFrom, st->invincible, st->outmapTimer,
      st->outmapIndex, st->outmapDone, st->autos, st->souls, st->sends, st->caves, st->graveyardTimer, st->graveyards, st->lastAction, st->soulAcc,
      st->fires, st->walls, st->points, st->minionGroups, st->minionSpeed, st->units);
}

} // namespace aiverse::game
