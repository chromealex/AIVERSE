// Scripted environment (RtsEnv.h): the Mouro boss director (BW MouroBoss / MouroActionHolder / MouroEmotions),
// the map scroller (BW MapScroller + MapScrollerActions), off-map senders (BW OutmapUnitsHandler) and auto
// skills moving along splines (BW MapAutoSkills / AutoSkill).
#include "RtsEnv.h"
#include "RtsInternal.h"
#include <algorithm>

namespace aiverse::game {

using namespace detail;

namespace {
Fixed battleTime(Simulation& sim) { return Fixed::fromInt(int64_t(sim.tick())) / int64_t(std::max<uint32_t>(sim.tickRate(), 1)); }
}

// ================================================================= Mouro

bool Environment::mouroCondition(Simulation& sim, EnvState& st, const EnvCondDef& c, const EnvActionDef* a, int emotion) const {
    World& w = sim.world();
    const uint8_t player = slotOf(sim, 0), bot = slotOf(sim, 1);
    switch (c.kind) {
    case kCondCooldown: return st.mouro.emotionCooldown.raw <= 0 || c.value.raw <= 0;
    case kCondBadAttack: {   // the bot lost at least N units in assaults this tick
        int lost = 0;
        for (const SimEvent& ev : sim.events()) if (ev.type == kEvGarrisonChanged && ev.player != bot && ev.value < 0) lost -= int(ev.value);
        return lost >= c.count;
    }
    case kCondDomination: {
        if (def_.mouro.dominationType == 0xFFFF) return false;
        bool captured = false;
        for (const SimEvent& ev : sim.events())
            if (ev.type == kEvCaptured && w.alive(ev.a) && w.get<Unit>(ev.a).type == def_.mouro.dominationType &&
                ((c.byBot && ev.player == bot) || (c.byPlayer && ev.player == player))) captured = true;
        if (!captured) return false;
        if (c.points.empty()) return true;
        int n = 0;
        w.each<Unit, Owner>([&](Entity, Unit& u, Owner& o) { if (u.type == def_.mouro.dominationType && o.player == player) ++n; });
        return std::find(c.points.begin(), c.points.end(), n) != c.points.end();
    }
    case kCondBotPointCaptured:
        for (const SimEvent& ev : sim.events()) if (ev.type == kEvCaptured && ev.player == bot) return true;
        return false;
    case kCondChance: return sim.rng().unit() * 100 <= c.value;
    case kCondMaxPlays: return emotion < 0 || size_t(emotion) >= st.mouro.emotionPlays.size() || st.mouro.emotionPlays[size_t(emotion)] < c.count;
    case kCondEndGame: return (c.win && st.mouro.mode == 1) || (c.lose && st.mouro.mode == 2);
    case kCondSkill:
        return (c.byPlayer && player < st.skillCast.size() && st.skillCast[player]) || (c.byBot && bot < st.skillCast.size() && st.skillCast[bot]);
    case kCondActionDone: return st.mouro.actionsDone > 0;
    }
    (void)a;
    return false;
}

bool Environment::mouroExecute(Simulation& sim, EnvState& st, const EnvActionDef& a) {
    World& w = sim.world();
    const Rules& R = m_->rules();
    const uint8_t player = slotOf(sim, 0), bot = slotOf(sim, 1);
    // BW ResetCastSkillValues: the bot's skills are ready again (cooldowns); the cast itself is free
    GameState& gs = m_->state(sim);
    if (bot < gs.playerAbilities.size()) for (auto& ps : gs.playerAbilities[bot]) ps.readyAt = 0;
    if (def_.mouro.energy >= 0 && bot < gs.players.size()) {   // skillEnergy = maxSkillEnergy
        auto& pe = gs.players[bot];
        const size_t r = size_t(def_.mouro.energy);
        if (r < pe.stock.size() && r < pe.cap.size() && pe.cap[r].raw > 0) pe.stock[r] = pe.cap[r];
    }
    switch (a.kind) {
    case kActCastAt: case kActCastRing:
        cast(sim, bot, a.skill, def_.mouro.castPos);
        return true;
    case kActAverageMorale:
        cast(sim, bot, a.skill, {});
        return true;
    case kActCaldron: {
        Entity byLevel, byUnits;
        w.each<Garrison, Unit, Owner>([&](Entity e, Garrison& g, Unit& u, Owner& o) {
            if (o.player != player || u.type == def_.mouro.dominationType) return;
            if (!byLevel.valid() || R.types[w.get<Unit>(byLevel).type].level < R.types[u.type].level) byLevel = e;
            if (!byUnits.valid() || w.get<Garrison>(byUnits).count < g.count) byUnits = e;
        });
        Entity t = sim.rng().below(2) ? byLevel : byUnits;
        if (!t.valid()) return false;
        cast(sim, bot, a.skill, w.get<Transform>(t).pos, t);
        return true;
    }
    case kActSkeletons: {
        // BW GetFilteredPoints: the points the skill's own filter accepts for the bot
        std::vector<Entity> pts;
        const int payload = R.playerAbilities[size_t(a.skill)].payload;
        w.each<Garrison, Owner>([&](Entity e, Garrison&, Owner&) { if (m_->envFilterAccepts(sim, payload, e, bot)) pts.push_back(e); });
        int casted = 0;
        for (int k = 0; k < a.targetCount && !pts.empty(); ++k) {
            const size_t idx = sim.rng().below(uint32_t(pts.size()));
            cast(sim, bot, a.skill, w.get<Transform>(pts[idx]).pos, pts[idx]);
            ++casted;
        }
        return casted > 0;
    }
    case kActAddUnits: {
        if (!a.toPlayer && !a.toBot) return false;
        std::vector<Entity> targets;
        if (!a.targets.empty()) { for (int i : a.targets) if (Entity e = point(sim, i); e.valid()) targets.push_back(e); }
        else {
            const bool attackPlayer = a.toPlayer && a.toBot ? sim.rng().below(2) == 1 : a.toPlayer;
            const uint8_t tp = attackPlayer ? player : bot;
            w.each<Garrison, Owner>([&](Entity e, Garrison&, Owner& o) { if (o.player == tp) targets.push_back(e); });
            if (int(targets.size()) > a.targetCount) {
                std::vector<Entity> pick;
                for (int k = 0; k < a.targetCount; ++k) { size_t idx = sim.rng().below(uint32_t(targets.size())); pick.push_back(targets[idx]); targets.erase(targets.begin() + long(idx)); }
                targets = pick;
            }
        }
        if (targets.empty()) return false;
        const int per = a.unitsPerPoint / int(targets.size());
        for (int si : a.sources) {
            Entity s = point(sim, si);
            if (!s.valid()) continue;
            if (auto* g = w.tryGet<Garrison>(s)) g->count = a.unitsPerPoint;
            for (Entity t : targets) queueSend(sim, st, s, t, per, false, w.get<Owner>(s).player);
        }
        return true;
    }
    default: return true;   // animation / death: presentation only
    }
}

void Environment::mouro(Simulation& sim, EnvState& st) {
    const EnvMouroDef& M = def_.mouro;
    if (!M.enabled) return;
    const uint8_t player = slotOf(sim, 0), bot = slotOf(sim, 1);
    // BW OnPlayerVictory: the end of the match stops every holder (laugh when the bot won, death otherwise)
    for (const SimEvent& ev : sim.events())
        if (ev.type == kEvPlayerDefeated && st.mouro.mode == 0) {
            if (ev.player == player) st.mouro.mode = 1;
            else if (ev.player == bot) st.mouro.mode = 2;
        }
    // domination points the player captured (platform breaking: suspend timer reset)
    if (M.dominationType != 0xFFFF) {
        int n = 0;
        sim.world().each<Unit, Owner>([&](Entity, Unit& u, Owner& o) { if (u.type == M.dominationType && o.player == player) ++n; });
        if (n > st.mouro.maxDomination) {
            st.mouro.maxDomination = n;
            if (size_t(st.mouro.holder) < st.mouro.holders.size()) st.mouro.holders[size_t(st.mouro.holder)].suspend = Fixed{};
        }
    }
    st.mouro.actionsDone = 0;
    const Fixed dt = sim.dt(), now = battleTime(sim);
    if (st.mouro.mode == 0 && size_t(st.mouro.holder) < M.holders.size()) {
        const size_t hi = size_t(st.mouro.holder);
        const EnvHolderDef& hd = M.holders[hi];
        EnvHolderState& hs = st.mouro.holders[hi];
        if (hd.startsFrom <= now) {
            hs.time += dt;
            bool running = false;
            for (size_t k = 0; k < hs.actions.size(); ++k) {
                EnvActionState& as = hs.actions[k];
                const EnvActionDef& ad = hd.actions[k];
                if (as.state == 2) {
                    as.timer += dt;
                    if (!as.executed && as.timer >= ad.castAt) {
                        as.executed = true;
                        if (mouroExecute(sim, st, ad)) ++st.mouro.actionsDone;
                    }
                    if (as.timer >= ad.duration) { as.executed = false; as.state = 0; as.await = Fixed{}; }
                } else as.await += dt;
                if (as.state == 2) running = true;
            }
            bool ready = !running;
            if (ready && hs.delay.raw > 0) { hs.delay -= dt; ready = false; }
            if (ready && hs.suspend.raw > 0) { hs.suspend -= dt; ready = false; }
            if (ready)
                for (size_t oi = 0; oi < hs.order.size(); ++oi) {
                    const uint16_t k = hs.order[oi];
                    const EnvActionDef& ad = hd.actions[k];
                    bool ok = true;
                    if (ad.minDomination > 0) {
                        int n = 0;
                        sim.world().each<Unit, Owner>([&](Entity, Unit& u, Owner& o) { if (u.type == M.dominationType && o.player == player) ++n; });
                        ok = n >= ad.minDomination;
                    }
                    if (ok && ad.kind == kActAverageMorale && M.morale >= 0) {
                        GameState& gs = m_->state(sim);
                        auto stars = [&](uint8_t p) { return p < gs.players.size() ? gs.players[p].stock[size_t(M.morale)].floorInt() : 0; };
                        ok = Fixed::fromInt(stars(player) - stars(bot)) >= ad.moraleDiff;
                    }
                    if (ok && ad.kind == kActCaldron) {
                        bool any = false;
                        sim.world().each<Garrison, Unit, Owner>([&](Entity, Garrison&, Unit& u, Owner& o) { if (o.player == player && u.type != M.dominationType) any = true; });
                        ok = any;
                    }
                    if (!ok) { hs.actions[k].state = 0; continue; }
                    EnvActionState& as = hs.actions[k];
                    as.state = 2; as.timer = Fixed{}; as.executed = false;
                    hs.suspend = ad.nextTimeout;
                    hs.order.erase(hs.order.begin() + long(oi));
                    hs.order.push_back(k);
                    sim.emit({kEvEnv, bot, kNullEntity, kNullEntity, (kEnvMouroAction << 16) | int64_t(hi << 8) | k, M.castPos});
                    running = true;
                    break;
                }
            if (!running && hi + 1 < M.holders.size() && M.holders[hi + 1].startsFrom <= now) ++st.mouro.holder;
        }
    }
    // emotions: the first holder whose conditions all hold plays (view event)
    if (st.mouro.emotionCooldown.raw > 0) st.mouro.emotionCooldown -= dt;
    for (size_t e = 0; e < M.emotions.size(); ++e) {
        bool ok = !M.emotions[e].conds.empty();
        for (auto& c : M.emotions[e].conds) if (!mouroCondition(sim, st, c, nullptr, int(e))) { ok = false; break; }
        if (!ok) continue;
        ++st.mouro.emotionPlays[e];
        st.mouro.lastEmotion = int(e);
        st.mouro.emotionCooldown = M.emotionsCooldown;
        sim.emit({kEvEnv, bot, kNullEntity, kNullEntity, (kEnvMouroEmotion << 16) | int64_t(e), M.castPos});
        break;
    }
}

// ================================================================= map scroller

void Environment::scroller(Simulation& sim, EnvState& st) {
    if (def_.scroller.empty()) return;
    World& w = sim.world();
    GameState& gs = m_->state(sim);
    const Fixed dt = sim.dt();
    for (size_t i = 0; i < def_.scroller.size(); ++i) {
        const EnvScrollAction& a = def_.scroller[i];
        uint8_t& state = st.scrollerState[i];
        if (state == 2) continue;
        const uint8_t who = slotOf(sim, a.who);
        auto done = [&]() { state = 2; sim.emit({kEvEnv, 0, kNullEntity, kNullEntity, (kEnvScroll << 16) | int64_t(i), st.screenPos}); };
        if (state == 0) {   // Start
            state = 1;
            switch (a.kind) {
            case kScrWaitTime: st.scrollerTimer[i] = a.seconds; break;
            case kScrMove:
                st.screenFrom = m_->screen().enabled ? m_->screenCenter(sim) : st.screenPos;
                st.scrollerTimer[i] = Fixed{};
                break;
            case kScrConvertAll: {
                w.each<Garrison, Owner>([&](Entity e, Garrison&, Owner& o) {
                    if (o.player == who) return;
                    const uint8_t old = o.player;
                    o.player = who;
                    sim.emit({kEvCaptured, who, e, kNullEntity, old, w.get<Transform>(e).pos});
                });
                for (Entity e : units_(sim)) w.get<Owner>(e).player = who;
                done();
                continue;
            }
            case kScrConvertOutside: {
                const uint8_t from = slotOf(sim, uint8_t(a.from));
                if (from == who) { done(); continue; }
                w.each<Garrison, Owner, Transform>([&](Entity e, Garrison&, Owner& o, Transform& t) {
                    if (o.player != from || insideScreen(sim, t.pos)) return;
                    o.player = who;
                    sim.emit({kEvCaptured, who, e, kNullEntity, from, t.pos});
                });
                done();
                continue;
            }
            case kScrInvincible:
                if (who < st.invincible.size()) st.invincible[who] = a.flag;
                done();
                continue;
            case kScrSend: {
                Entity f = point(sim, a.from), t = point(sim, a.to);
                if (f.valid() && t.valid()) queueSend(sim, st, f, t, a.count, false, w.get<Owner>(f).player);
                done();
                continue;
            }
            default: break;
            }
        }
        // Update
        switch (a.kind) {
        case kScrWaitCaptured: {
            bool all = true;
            for (int pi : a.points) { Entity e = point(sim, pi); if (!e.valid() || w.get<Owner>(e).player != who) all = false; }
            if (all) done();
            break;
        }
        case kScrWaitTime:
            if (st.scrollerTimer[i].raw <= 0) done();
            else st.scrollerTimer[i] -= dt;
            break;
        case kScrWaitScore: {
            const uint8_t team = m_->teamOf(sim, who);
            if (team < gs.score.size() && gs.score[team] >= a.score) done();
            break;
        }
        case kScrWaitNoUnits: {
            bool any = false;
            w.each<Garrison, Owner, Transform>([&](Entity, Garrison& g, Owner& o, Transform& t) { if (o.player == who && g.count >= 1 && insideScreen(sim, t.pos)) any = true; });
            for (Entity e : units_(sim)) if (w.get<Owner>(e).player == who) any = true;
            if (!any) done();
            break;
        }
        case kScrMove:
            if (st.scrollerTimer[i] >= a.seconds) { st.screenPos = a.pos; done(); }
            else {
                st.scrollerTimer[i] += dt;
                Fixed k = a.seconds.raw > 0 ? fxMin(st.scrollerTimer[i] / a.seconds, Fixed::fromInt(1)) : Fixed::fromInt(1);
                st.screenPos = st.screenFrom + (a.pos - st.screenFrom) * k;
            }
            // a map "screen" follows the scroller (centre = corner + half the scroller screen)
            if (m_ && m_->screen().enabled) m_->setScreenCenter(sim, st.screenPos + def_.screen.size / Fixed::fromInt(2));
            break;
        case kScrGoto:
            done();
            for (size_t k = 0; k < st.scrollerState.size(); ++k) st.scrollerState[k] = int(k) < a.step ? 2 : 0;
            break;
        default: break;
        }
        break;   // one running action at a time (BW UpdateActions)
    }
}

// ================================================================= off-map senders

void Environment::outmap(Simulation& sim, EnvState& st) {
    const EnvOutmapDef& O = def_.outmap;
    if (O.moves.empty()) return;
    World& w = sim.world();
    if (size_t(st.outmapIndex) < O.moves.size()) {
        while (size_t(st.outmapIndex) < O.moves.size()) {
            const EnvOutmapMove& mv = O.moves[size_t(st.outmapIndex)];
            if (mv.time > st.outmapTimer) break;
            Entity f = point(sim, mv.from), t = point(sim, mv.to);
            if (f.valid() && t.valid()) queueSend(sim, st, f, t, mv.count, true, w.get<Owner>(f).player);
            ++st.outmapIndex;
        }
        if (size_t(st.outmapIndex) >= O.moves.size()) {
            st.outmapDone = true;
            for (int si : O.sources) if (Entity e = point(sim, si); e.valid()) w.get<Owner>(e).player = 0;
        }
    }
    st.outmapTimer += sim.dt();
    if (O.repeat.raw > 0 && st.outmapTimer >= O.repeat) { st.outmapTimer = Fixed{}; st.outmapIndex = 0; }
}

// ================================================================= auto skills on splines

void Environment::autos(Simulation& sim, EnvState& st) {
    World& w = sim.world();
    const Fixed dt = sim.dt();
    for (size_t i = 0; i < def_.splines.size(); ++i) {
        const EnvSpline& sp = def_.splines[i];
        if (sp.skill < 0 || !sp.enabled) continue;
        bool onScreen = true;
        for (auto& p : sp.points) if (!insideScreen(sim, p)) onScreen = false;
        if (!onScreen) { st.autos[i].late = true; continue; }
        EnvAuto& au = st.autos[i];
        // emission (BW UpdateCreateSkillTimer)
        if (!(sp.emitPeriod.raw <= 0 && au.timer.raw <= 0 && !au.late)) {
            au.timer -= dt;
            if (au.timer.raw <= 0) {
                au.late = false;
                std::vector<Entity> areas;
                cast(sim, 0, sp.skill, sp.points.front(), kNullEntity, &areas);
                if (sp.points.size() > 1) for (Entity a : areas) au.moving.push_back({a, Fixed{}});
                au.timer += sp.emitPeriod;
            }
        }
        // the emitted areas ride along the spline
        for (size_t k = 0; k < au.moving.size();) {
            auto& mv = au.moving[k];
            if (!w.alive(mv.area)) { au.moving.erase(au.moving.begin() + long(k)); continue; }
            mv.dist += dt * sp.velocity;
            if (sp.loop && sp.emitPeriod.raw <= 0 && sp.length.raw > 0) while (mv.dist >= sp.length) mv.dist -= sp.length;
            Vec3 target = sp.at(mv.dist);
            Transform& t = w.get<Transform>(mv.area);
            Vec3 cur{t.pos.x, Fixed{}, t.pos.z};
            moveTowards(cur, target, dt * sp.velocity);
            t.pos.x = cur.x; t.pos.z = cur.z;
            if (mv.dist.raw < 0 || mv.dist > sp.length) { w.destroyLater(mv.area); au.moving.erase(au.moving.begin() + long(k)); continue; }
            ++k;
        }
    }
}

// ================================================================= multi-spline auto skills

namespace {
// BW AutoSkill.GetTransferSpline: Catmull-Rom over [from: last two points (loop: last, first)] + [to: first two],
// with the head and tail point cut: the curve from the end of one spline to the start of the next.
EnvSpline transferSpline(const EnvSpline& a, const EnvSpline& b) {
    const auto& pa = a.points; const auto& pb = b.points;
    std::vector<Vec3> p;
    if (!a.loop) { p.push_back(pa[pa.size() - 2]); p.push_back(pa.back()); } else { p.push_back(pa.back()); p.push_back(pa.front()); }
    p.push_back(pb[0]); p.push_back(pb[1]);
    EnvSpline full; full.build(p, false);
    EnvSpline t;   // keep the middle segment (nodes 10..20 of 3 segments x 10)
    for (size_t k = 10; k <= 20 && k < full.nodes.size(); ++k) t.nodes.push_back(full.nodes[k]);
    t.points = {p[1], p[2]};
    t.cum.assign(t.nodes.size(), Fixed{});
    for (size_t k = 1; k < t.nodes.size(); ++k) t.cum[k] = t.cum[k - 1] + (t.nodes[k] - t.nodes[k - 1]).xz().length();
    t.length = t.cum.empty() ? Fixed{} : t.cum.back();
    return t;
}
}

void Environment::multiAutos(Simulation& sim, EnvState& st) {
    World& w = sim.world();
    const Fixed dt = sim.dt();
    for (size_t i = 0; i < def_.multis.size() && i < st.multis.size(); ++i) {
        const EnvMultiDef& md = def_.multis[i];
        EnvMulti& mu = st.multis[i];
        // BW MultiSplineSkill.GetSplineIndex: next in order, or a random one with randomJump
        auto next = [&](int prev) { return md.random ? int(sim.rng().below(uint32_t(md.count))) : (prev < 0 ? 0 : (prev + 1) % md.count); };
        if (!mu.created) {
            // emitted once (no emit period), when the first spline lies on the screen
            const int idx = next(-1);
            bool onScreen = true;
            for (auto& p : def_.splines[size_t(idx)].points) if (!insideScreen(sim, p)) onScreen = false;
            if (!onScreen) continue;
            mu.created = true;
            std::vector<Entity> areas;
            cast(sim, 0, md.skill, def_.splines[size_t(idx)].points.front(), kNullEntity, &areas);
            for (Entity a : areas) mu.moving.push_back({a, Fixed{}, idx, -1, false});
        }
        for (size_t k = 0; k < mu.moving.size();) {
            auto& mv = mu.moving[k];
            if (!w.alive(mv.area)) { mu.moving.erase(mu.moving.begin() + long(k)); continue; }
            const Fixed vel = def_.splines[size_t(mv.cur)].velocity;
            mv.dist += dt * vel;
            EnvSpline tr;
            auto current = [&]() -> const EnvSpline& {
                if (mv.transfer && mv.from >= 0) { tr = transferSpline(def_.splines[size_t(mv.from)], def_.splines[size_t(mv.cur)]); return tr; }
                return def_.splines[size_t(mv.cur)];
            };
            const EnvSpline* sp = &current();
            if (mv.dist >= sp->length) {
                mv.dist -= sp->length;
                const int from = mv.cur, to = next(from);
                const bool wasTransfer = mv.transfer;
                mv.cur = to;
                mv.transfer = md.transfer && !wasTransfer;
                mv.from = mv.transfer ? from : -1;
                // BW: the end of a transfer advances the spline index once more (transferSpline = null, next index)
                sp = &current();
            }
            Transform& t = w.get<Transform>(mv.area);
            Vec3 cur{t.pos.x, Fixed{}, t.pos.z};
            moveTowards(cur, sp->at(mv.dist), dt * vel);
            t.pos.x = cur.x; t.pos.z = cur.z;
            ++k;
        }
    }
}

bool RtsModule::envFilterAccepts(Simulation& sim, int payload, Entity e, uint8_t owner) const {
    for (int depth = 0; depth < 4 && payload >= 0 && size_t(payload) < rules_.payloads.size() && !rules_.payloads[size_t(payload)].empty(); ++depth) {
        const EffectDef& ef = rules_.payloads[size_t(payload)][0];
        if ((ef.kind == EffectKind::Area || ef.kind == EffectKind::Projectile || ef.kind == EffectKind::Chain) && ef.payload >= 0) { payload = ef.payload; continue; }
        return filterOk(sim, ef.filter, e, owner, kNullEntity);
    }
    return sim.world().has<Owner>(e) && hostile(sim, owner, sim.world().get<Owner>(e).player);
}

} // namespace aiverse::game
