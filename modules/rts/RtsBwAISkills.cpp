// Bunker Wars computer player: per-skill casters (AISkills/AISkill*.cs, AISkillAssistant, AIHeatmap). Each caster
// waits for its slot's delay (profile skillSlotAwait), then aims the player ability with its BW heuristic and
// casts it through kCmdPlayerAbility, like a player clicking. Abilities without a BW caster fall back to the
// engine's smart targeting. Which caster an ability gets: rules ai.bw.skills, else inferred from its effects.
#include "RtsBwAIInternal.h"

namespace aiverse::game {

namespace {
constexpr Fixed kOne = Fixed::fromInt(1);

const char* const kSkillNames[] = {
    "None", "Generic", "AverageMorale", "BreakAllMagic", "Downgrade", "FireRing", "FogOfWar", "Freeze", "Hypnosis", "IgnoreDefence",
    "IllusionWave", "IncreaseDefence", "IncreaseMorale", "IncreaseProductionX2", "IncreaseUnitsSpeed", "Invisibility", "Landing", "Lock",
    "Plague", "Rage", "Reverse", "Sabotage", "Shield", "Spy", "Teleport", "WarChains", "GoHome", "Victim", "Panic", "Reaver", "Invulnerability",
    "Slowdown", "BetrayalGarden2", "EnhanceWeapon", "ChickenCoop", "BFG9000", "Fortification"};
static_assert(sizeof(kSkillNames) / sizeof(kSkillNames[0]) == size_t(BwSkill::Count));

// what the ability does, read from its effects (and the payloads its areas / projectiles deliver)
struct Info { Fixed range{}, duration{}, percent{}, amount{}; int levelDelta = 0; int toType = -1; const EffectDef* first = nullptr; };
void scanEffects(const Rules& R, const std::vector<EffectDef>& effs, Info& in, int depth) {
    for (auto& e : effs) {
        if (!in.first && e.kind != EffectKind::Area && e.kind != EffectKind::Projectile && e.kind != EffectKind::Cast) in.first = &e;
        in.range = fxMax(in.range, e.radius);
        if (e.duration.raw > 0 && in.duration.raw == 0) in.duration = e.duration;
        if (e.percent.raw > 0 && in.percent.raw == 0) in.percent = e.percent;
        if (e.kind == EffectKind::Status && e.status >= 0 && size_t(e.status) < R.statuses.size() &&
            (R.statuses[size_t(e.status)].flags & kStHypnotised) && in.percent.raw == 0) in.percent = R.statuses[size_t(e.status)].sendShare;
        if (e.amount.raw != 0 && in.amount.raw == 0) in.amount = e.amount;
        if (e.levelDelta && !in.levelDelta) in.levelDelta = e.levelDelta;
        if (e.toType != 0xFFFF && in.toType < 0) in.toType = e.toType;
        if (e.kind == EffectKind::Area && e.delivery.lifetime.raw > 0 && in.duration.raw == 0) in.duration = e.delivery.lifetime;
        if (e.payload >= 0 && depth < 4 && size_t(e.payload) < R.payloads.size()) scanEffects(R, R.payloads[size_t(e.payload)], in, depth + 1);
    }
}
Info infoOf(BwAI::Ctx& c, int ability) {
    Info in;
    const PlayerAbilityDef& a = c.m.playerAbilityFor(c.sim, c.me(), ability);
    scanEffects(c.R, a.effects, in, 0);
    return in;
}

BwSkill infer(const Rules& R, const PlayerAbilityDef& a) {
    Info in;
    scanEffects(R, a.effects, in, 0);
    if (!in.first) return BwSkill::Generic;
    const EffectDef& e = *in.first;
    switch (e.kind) {
        case EffectKind::GarrisonConvert: return BwSkill::Hypnosis;
        case EffectKind::GarrisonKill: return a.target == TargetKind::Structure ? BwSkill::Plague : BwSkill::FireRing;
        case EffectKind::GarrisonAdd: return BwSkill::Landing;
        case EffectKind::GarrisonPanic: return BwSkill::Panic;
        case EffectKind::Level: return e.levelDelta < 0 ? BwSkill::Downgrade : BwSkill::Generic;
        case EffectKind::Transform: return BwSkill::Fortification;
        case EffectKind::SendBack: return e.mode == 0 ? BwSkill::GoHome : BwSkill::Reverse;
        case EffectKind::Teleport: case EffectKind::TeleportGroup: return BwSkill::Teleport;
        case EffectKind::Clone: return BwSkill::IllusionWave;
        case EffectKind::Dispel: return BwSkill::BreakAllMagic;
        case EffectKind::EqualizeResource: return BwSkill::AverageMorale;
        case EffectKind::Stun: return BwSkill::Freeze;
        case EffectKind::Kill: case EffectKind::Damage: return BwSkill::FireRing;
        case EffectKind::Resource:
            for (uint16_t r : R.perUnitResources) if (e.resource == int(r) && e.amount.raw > 0) return BwSkill::IncreaseMorale;
            return BwSkill::Generic;
        case EffectKind::Status: {
            if (e.status < 0 || size_t(e.status) >= R.statuses.size()) return BwSkill::Generic;
            const StatusDef& s = R.statuses[size_t(e.status)];
            if (s.flags & kStHypnotised) return BwSkill::Hypnosis;
            if (s.flags & kStInvulnerable) return BwSkill::Invulnerability;
            if (s.flags & kStStun) return BwSkill::Freeze;
            if (s.flags & kStIgnoreDefense) return BwSkill::IgnoreDefence;
            if (s.flags & kStSpreads) return BwSkill::Plague;
            if (s.flags & kStLock) return BwSkill::Sabotage;
            if (s.flags & kStInvisible) return BwSkill::Invisibility;
            if (s.shield.raw > 0) return BwSkill::IncreaseDefence;
            for (auto& m : s.mods) {
                if (m.stat == Stat::ProductionSpeed && m.mul > kOne) return BwSkill::IncreaseProductionX2;
                if (m.stat == Stat::Defense && m.mul > kOne) return BwSkill::IncreaseDefence;
                if (m.stat == Stat::Damage && m.mul > kOne) return BwSkill::Rage;
                if (m.stat == Stat::Speed) return m.mul > kOne ? BwSkill::IncreaseUnitsSpeed : BwSkill::Slowdown;
            }
            return BwSkill::Generic;
        }
        default: return BwSkill::Generic;
    }
}

// AIHeatmap: 20 x 15 cells over the map
struct Heat {
    static constexpr int W = 20, H = 15;
    int v[W * H] = {};
    Fixed mw, mh;
    Heat(BwWorld& w) : mw(w.mapW), mh(w.mapH) {}
    void add(Vec3 p, int weight) {
        const Fixed px = p.x / mw * W, py = p.z / mh * H;
        if (px.raw < 0 || py.raw < 0 || px > Fixed::fromInt(W - 1) || py > Fixed::fromInt(H - 1)) return;
        v[std::clamp(int(px.toInt()), 0, W - 1) + std::clamp(int(py.toInt()), 0, H - 1) * W] += weight;
    }
    void addGroup(BwAI::Ctx& c, const BwGroup& g, bool positive, Fixed maxDist = Fixed::fromInt(-1)) {
        const Vec3 to = c.world().pts[size_t(g.to)].pos;
        for (auto& u : g.units) {
            if (!c.m.onScreen(c.sim, u)) continue;   // IsPointInsideScreen(me, group)
            if (maxDist.raw >= 0 && (u - to).xz().length() > maxDist) continue;
            add(u, positive ? 1 : -1);
        }
    }
    int best(Random& rng, Vec3& pos) const {
        int mx = 0, bx = 0, by = 0;
        for (int i = 0; i < W; ++i) for (int j = 0; j < H; ++j) if (v[i + j * W] > mx) { mx = v[i + j * W]; bx = i; by = j; }
        if (mx <= 0) { pos = {}; return 0; }
        pos = {mw / W * (Fixed::fromInt(bx) + rng.unit()), Fixed{}, mh / H * (Fixed::fromInt(by) + rng.unit())};
        return mx;
    }
};

struct Caster {
    BwAI::Ctx& c;
    BwAI::Caster& k;
    BwWorld& W() { return c.world(); }
    std::vector<BwAI::PS>& ms() { return c.ai.ms_; }
    uint8_t me() { return c.me(); }
    Info info() { return infoOf(c, k.ability); }
    void reset() { k.heat = Fixed{}; }

    bool use(Vec3 p, Entity target = kNullEntity) {
        if (!target.valid()) {   // the clicked object: nearest point / unit within 4 cells (castPlayerAbility resolves the rest)
            const PlayerAbilityDef& a = c.m.playerAbilityFor(c.sim, me(), k.ability);
            if (a.target == TargetKind::Structure) {
                Fixed bd = Fixed::fromInt(4);
                for (auto& pt : W().pts) { const Fixed d = (pt.pos - p).xz().length(); if (d <= bd) { bd = d; target = pt.e; } }
            }
        }
        if (!c.m.onScreen(c.sim, p)) return false;   // UseSkill: only inside the screen
        return c.cast(k.ability, p, target);
    }
    bool usePoint(int i) { return i >= 0 && use(W().pts[size_t(i)].pos, W().pts[size_t(i)].e); }

    // "fire at the peak": cast when the heat stops growing (Freeze, FireRing, Shield, Reverse, Slowdown, ...)
    bool peak(const Heat& h, bool resetOnZero = true) {
        Vec3 pos;
        const int w = h.best(c.ai.rng_, pos);
        if (w <= 0) { if (resetOnZero) reset(); return false; }
        if (Fixed::fromInt(w) > k.heat) { k.heat = Fixed::fromInt(w); return false; }
        reset();
        return use(pos);
    }

    bool areaOnGroups(int mode) {   // 0: Freeze / FireRing (+enemies -allies), 1: Shield (enemies only)
        if (info().range.raw <= 0) return false;
        Heat h(W());
        for (auto& g : W().groups) {
            const bool bad = !c.ally(me(), g.owner);
            if (mode == 1 && !bad) continue;
            h.addGroup(c, g, bad);
        }
        return peak(h, mode != 2);
    }

    bool tryUse() {
        BwWorld& w = W();
        const int n = int(w.pts.size());
        switch (k.kind) {
        case BwSkill::Freeze: return areaOnGroups(0);
        case BwSkill::FireRing: return areaOnGroups(0);
        case BwSkill::Shield: return areaOnGroups(1);
        case BwSkill::Rage: {
            if (info().range.raw <= 0) return false;
            Heat h(w);
            for (auto& g : w.groups) {
                if (!c.ally(me(), g.owner)) h.addGroup(c, g, false);
                else if (!c.ally(w.pts[size_t(g.to)].owner, g.owner)) h.addGroup(c, g, true);
            }
            Vec3 pos;
            const int v = h.best(c.ai.rng_, pos);
            if (k.heat <= Fixed::fromInt(v)) { k.heat = Fixed::fromInt(v); return false; }
            reset();
            return use(pos);
        }
        case BwSkill::Reverse: {
            if (info().range.raw <= 0) return false;
            Heat h(w);
            for (auto& g : w.groups) if (w.pts[size_t(g.to)].owner == me() && !c.ally(me(), g.owner)) h.addGroup(c, g, true);
            return peak(h, false);
        }
        case BwSkill::GoHome: {
            const Fixed range = info().range;
            if (range.raw <= 0) return false;
            Heat h(w);
            bool any = false;
            for (auto& g : w.groups) {
                if (!g.closed() || g.owner != me() || c.ally(me(), w.pts[size_t(g.to)].owner)) continue;
                if (ms()[size_t(g.to)].fullDefense().raw > 0) { h.addGroup(c, g, true, range); any = true; }
            }
            if (!any) return false;
            Vec3 pos;
            const int v = h.best(c.ai.rng_, pos);
            if (Fixed::fromInt(v) >= k.heat) { k.heat = Fixed::fromInt(v); return false; }
            reset();
            return use(pos);
        }
        case BwSkill::Slowdown: case BwSkill::BetrayalGarden2: {
            const Fixed range = info().range;
            if (range.raw <= 0) return false;
            Heat h(w);
            for (auto& g : w.groups) {
                if (w.pts[size_t(g.to)].owner != me() || c.ally(me(), g.owner)) continue;
                const BwAI::PS& st = ms()[size_t(g.to)];
                if (st.defense > st.attackOnWay) continue;
                if (k.kind == BwSkill::Slowdown && (st.defenseOnWay.raw <= 0 || st.minTimeAttack > st.minTimeDefense)) continue;
                h.addGroup(c, g, true, k.kind == BwSkill::BetrayalGarden2 ? range / 2 : Fixed::fromInt(-1));
            }
            return peak(h);
        }
        case BwSkill::Invisibility: {
            const Fixed range = info().range;
            Heat h(w);
            for (auto& g : w.groups) {
                if (g.owner != me() || g.routeLen <= range || g.routeLen.raw <= 0) continue;
                if ((g.routeLen - g.minDist) / g.routeLen >= Fixed::fromRatio(3, 10)) continue;
                const uint8_t to = w.pts[size_t(g.to)].owner;
                if (c.neutral(to) || !c.enemy(me(), to)) continue;
                h.addGroup(c, g, true);
            }
            return peak(h, false);
        }
        case BwSkill::IncreaseUnitsSpeed:
            return false;   // BW AISkillIncreaseUnitsSpeed only adds negative weights: it never fires (kept)
        case BwSkill::Landing: {
            int best = -1; Fixed mv = kBwInf;
            for (int i = 0; i < n; ++i) if (w.pts[size_t(i)].owner == me() && (best < 0 || mv > ms()[size_t(i)].fullDefense())) { best = i; mv = ms()[size_t(i)].fullDefense(); }
            return usePoint(best);
        }
        case BwSkill::IncreaseProductionX2: {
            int best = -1; int room = INT32_MIN;
            for (int i = 0; i < n; ++i) {
                const BwPoint& p = w.pts[size_t(i)];
                if (p.role != BwRole::House || p.owner != me() || ms()[size_t(i)].attackOnWay.raw > 0) continue;
                if (room < p.max - p.value) { room = p.max - p.value; best = i; }
            }
            const BwPlayer& pl = w.players[me()];
            return best >= 0 && pl.maxUnits - pl.units >= room && room >= 15 && usePoint(best);
        }
        case BwSkill::IncreaseDefence: {
            const Info in = info();
            if (in.duration.raw <= 0) return false;
            int best = -1;
            for (int i = 0; i < n; ++i) {
                const BwAI::PS& st = ms()[size_t(i)];
                if (w.pts[size_t(i)].owner != me() || st.attackOnWay < st.protection() || st.minTimeAttack > in.duration / 4) continue;
                if (st.fullDefense().raw < 0 && -st.fullDefense() / st.attackOnWay > Fixed::fromRatio(3, 10))
                    if (best < 0 || st.fullDefense() < ms()[size_t(best)].fullDefense()) best = i;
            }
            return usePoint(best);
        }
        case BwSkill::Invulnerability: {
            const Info in = info();
            const PlayerAbilityDef& a = c.m.playerAbilityFor(c.sim, me(), k.ability);
            if (in.duration.raw <= 0) return false;
            for (int i = 0; i < n; ++i) {
                const BwAI::PS& st = ms()[size_t(i)];
                if (st.fullDefense().raw > 0 || w.pts[size_t(i)].owner != me() || st.minTimeAttack > a.castTime) continue;
                if (st.lastTimeAttack - st.minTimeAttack > in.duration) continue;   // the attack outlasts the shield
                if (usePoint(i)) return true;
            }
            return false;
        }
        case BwSkill::Panic: {
            int best = -1;
            for (int i = 0; i < n; ++i) {
                const BwAI::PS& st = ms()[size_t(i)];
                if (st.firstEnemy < 0 || !c.ally(me(), uint8_t(st.firstEnemy))) continue;
                if (best < 0 || ms()[size_t(best)].available < st.available) best = i;
            }
            return usePoint(best);
        }
        case BwSkill::Plague: {
            int best = -1, cnt = 0, units = 0;
            for (int i = 0; i < n; ++i) {
                if (!c.enemy(me(), w.pts[size_t(i)].owner)) continue;
                ++cnt;
                if (best < 0 || w.pts[size_t(best)].value < w.pts[size_t(i)].value) best = i;
            }
            if (best < 0 || cnt == 0) return false;
            for (auto& pi : c.sim.players()) if (c.enemy(me(), pi.slot)) units += w.players[pi.slot].units;
            return units / cnt <= w.pts[size_t(best)].value && usePoint(best);
        }
        case BwSkill::Sabotage: {
            const int sab = int(info().amount.toInt());
            int cnt = 0, mn = INT32_MAX;
            for (auto& p : w.pts) if (c.enemy(me(), p.owner)) { ++cnt; mn = std::min(mn, p.value); }
            if (cnt == 0 || Fixed::fromInt(sab) / int64_t(cnt) <= Fixed::fromInt(mn)) return false;
            return use({});
        }
        case BwSkill::IgnoreDefence: {
            const Fixed dur = info().duration;
            int best = -1;
            for (int i = 0; i < n; ++i) {
                const BwAI::PS& st = ms()[size_t(i)];
                const uint8_t o = w.pts[size_t(i)].owner;
                if (st.attackOnWay.raw <= 0 || st.fullDefense().raw < 0 || st.minTimeAttack > dur / 2 || c.neutral(o)) continue;
                if (st.firstEnemy < 0 || !c.ally(me(), uint8_t(st.firstEnemy)) || c.ally(me(), o)) continue;
                if (Fixed::fromInt(w.pts[size_t(i)].value) > st.attackOnWay) continue;
                if (best < 0 || w.pts[size_t(best)].value > w.pts[size_t(i)].value) best = i;
            }
            return usePoint(best);
        }
        case BwSkill::AverageMorale: {
            int sum = 0, cnt = 0;
            for (auto& pi : c.sim.players()) if (pi.alive) { sum += w.players[pi.slot].moraleLevel; ++cnt; }
            if (cnt == 0 || Fixed::fromInt(w.players[me()].moraleLevel - 2) < Fixed::fromInt(sum) / int64_t(cnt)) return false;
            return use({});
        }
        case BwSkill::IncreaseMorale: {
            const int stars = std::max(1, int(info().amount.toInt()));
            int maxLevel = 0;
            for (uint16_t r : c.R.perUnitResources) maxLevel += int(c.R.resources[r].storage.floorInt());
            const int mine = w.players[me()].moraleLevel;
            if (maxLevel - stars < mine) return false;
            int value = 0;
            for (auto& g : w.groups) {
                if (g.from < 0) continue;
                const uint8_t so = w.pts[size_t(g.from)].owner, to = w.pts[size_t(g.to)].owner;
                if (!c.enemy(so, to)) continue;
                if (so == me() && w.players[to].moraleLevel > mine) value += g.need();
                else if (to == me() && w.players[so].moraleLevel > mine) value += w.pts[size_t(g.to)].value;
            }
            return value > 50 && use({});
        }
        case BwSkill::Victim: {
            if (w.players[me()].moraleLevel >= 3) return false;
            int allies = 0, enemies = 0; Fixed am{}, em{};
            for (auto& pi : c.sim.players()) {
                if (!pi.alive) continue;
                if (c.ally(me(), pi.slot)) { ++allies; am += w.players[pi.slot].morale; } else { ++enemies; em += w.players[pi.slot].morale; }
            }
            if (!allies || !enemies || em - am < Fixed{}) return false;
            int best = -1;
            for (int i = 0; i < n; ++i) {
                if (ms()[size_t(i)].attackOnWay.raw > 0) continue;
                if (best < 0 || ms()[size_t(i)].available > ms()[size_t(best)].available) best = i;
            }
            return usePoint(best);
        }
        case BwSkill::Reaver: {
            for (int i = 0; i < n; ++i) {
                const BwPoint& p = w.pts[size_t(i)];
                if (p.owner != me() || !p.tower || ms()[size_t(i)].fullDefense().raw < 0) continue;
                auto* cb = c.sim.world().tryGet<Combat>(p.e);
                if (!cb || !c.sim.world().alive(cb->target) || !c.sim.world().has<Owner>(cb->target)) continue;
                if (!c.enemy(c.sim.world().get<Owner>(cb->target).player, me())) continue;
                if (usePoint(i)) return true;
            }
            return false;
        }
        case BwSkill::EnhanceWeapon: case BwSkill::ChickenCoop: {
            const int p = w.pointOf(k.a);
            return p >= 0 && usePoint(p);
        }
        case BwSkill::BFG9000: {
            const int p = w.pointOf(k.a);
            k.a = kNullEntity;
            return p >= 0 && usePoint(p);
        }
        case BwSkill::Fortification: {
            const Info in = info();
            if (in.toType < 0) return false;
            const UnitType& ct = c.R.types[size_t(in.toType)];
            const Fixed range = ct.weapons.empty() ? Fixed{} : ct.weapons[0].range;
            const BwRole castRole = bwRoleOf(c.R, c.cfg, uint16_t(in.toType));
            std::map<int, int> attack;
            for (auto& g : w.groups) {
                const BwPoint& tp = w.pts[size_t(g.to)];
                if (tp.owner != me() || (tp.role == castRole && tp.level >= ct.level) || tp.value <= 1 || c.ally(g.owner, me()) || g.units.empty()) continue;
                if ((g.units.front() - tp.pos).xz().length() > range) continue;
                attack[g.to] += g.count;
            }
            if (attack.size() < 2) return false;
            for (auto& [p, cnt] : attack) if (w.pts[size_t(p)].value < cnt) return usePoint(p);
            return false;
        }
        case BwSkill::WarChains: {
            std::map<int, int> to;
            for (auto& g : w.groups) if (!g.fake && g.owner == me() && c.enemy(me(), w.pts[size_t(g.to)].owner)) to[g.to] += g.count;
            if (to.empty()) return false;
            int target = -1, mx = 0;
            for (auto& [p, cnt] : to) if (target < 0 || cnt > mx) { target = p; mx = cnt; }
            std::map<int, int> from;
            const uint8_t towner = w.pts[size_t(target)].owner;
            for (auto& g : w.groups) if (!g.closed() && g.to == target && g.from >= 0 && c.ally(g.owner, towner)) from[g.from] += g.need();
            int src = -1; mx = 0;
            for (auto& [p, cnt] : from) if (src < 0 || mx < cnt) { src = p; mx = cnt; }
            return usePoint(src);
        }
        case BwSkill::BreakAllMagic: {
            uint64_t buff = 0, debuff = 0;
            for (size_t i = 0; i < c.R.tagNames.size(); ++i) { if (c.R.tagNames[i] == "buff") buff |= uint64_t(1) << i; if (c.R.tagNames[i] == "debuff") debuff |= uint64_t(1) << i; }
            auto counter = [&](Entity e, uint8_t owner) {
                int cnt = 0;
                if (auto* st = c.sim.world().tryGet<Statuses>(e))
                    for (auto& in : st->list) {
                        const uint64_t t = c.R.statuses[in.def].tags;
                        const int aff = (t & buff) ? 1 : (t & debuff) ? -1 : 0;
                        if (!aff) continue;
                        cnt += c.ally(me(), owner) ? aff : -aff;
                    }
                return cnt;
            };
            int best = -1, mn = 0;
            for (int i = 0; i < n; ++i) {
                if (c.neutral(w.pts[size_t(i)].owner)) continue;
                const int cnt = counter(w.pts[size_t(i)].e, w.pts[size_t(i)].owner);
                if (cnt >= 0) continue;
                if (best < 0 || cnt < mn) { best = i; mn = cnt; }
            }
            if (best >= 0) return usePoint(best);
            Heat h(w);
            for (auto& g : w.groups)
                for (size_t u = 0; u < g.ents.size(); ++u) {
                    const int cnt = counter(g.ents[u], g.owner);
                    if (cnt != 0) h.add(g.units[u], cnt < 0 ? 1 : -1);
                }
            Vec3 pos;
            const int v = h.best(c.ai.rng_, pos);
            if (k.heat <= Fixed::fromInt(v)) { k.heat = Fixed::fromInt(v); return false; }
            reset();
            return use(pos);
        }
        case BwSkill::IllusionWave: {
            int donor = -1; Fixed mx{};
            for (int i = 0; i < n; ++i) if (w.pts[size_t(i)].owner == me() && (donor < 0 || mx < ms()[size_t(i)].attack)) { mx = ms()[size_t(i)].attack; donor = i; }
            if (donor < 0) return false;
            int target = -1; Fixed mn = kBwInf;
            for (int i = 0; i < n; ++i) {
                const BwAI::PS& st = ms()[size_t(i)];
                const uint8_t o = w.pts[size_t(i)].owner;
                if (st.defense > mx || c.neutral(o) || st.defenseOnWay.raw > 0 || !c.enemy(me(), o) || st.available >= mx) continue;
                if (target < 0 || mn > st.available) { target = i; mn = st.available; }
            }
            if (target < 0 || !usePoint(donor)) return false;
            k.a = w.pts[size_t(donor)].e; k.b = w.pts[size_t(target)].e;   // the illusions follow next tick
            return true;
        }
        case BwSkill::Hypnosis: {
            const Info in = info();
            if (in.percent.raw <= 0) return false;
            int own = 0, attacked = 0; Vec3 avg{};
            for (int i = 0; i < n; ++i) {
                if (w.pts[size_t(i)].owner != me()) continue;
                if (ms()[size_t(i)].attackOnWay.raw > 0) ++attacked;
                avg += w.pts[size_t(i)].pos; ++own;
            }
            if (own == 0 || Fixed::fromInt(attacked) / int64_t(own) > Fixed::fromRatio(3, 10)) return false;
            avg = avg / Fixed::fromInt(own);
            int best = -1; Fixed bd = kBwInf;
            for (int i = 0; i < n; ++i) {
                if (c.ally(me(), w.pts[size_t(i)].owner) || ms()[size_t(i)].fullDefense().raw < 0) continue;
                const Fixed d = (w.pts[size_t(i)].pos - avg).xz().lengthSq();
                if (best < 0 || bd > d) { best = i; bd = d; }
            }
            if (best < 0 || !usePoint(best)) return false;
            c.ai.moveTasks_.push_back({w.pts[size_t(best)].e, w.pts[size_t(best)].owner, Fixed::fromRatio(1, 5), Fixed::fromRatio(1, 5), fxMin(in.percent, kOne)});
            return true;
        }
        case BwSkill::Teleport: {
            const Fixed pct = info().percent.raw > 0 ? info().percent : kOne;
            int donor = -1; Fixed mx{};
            for (int i = 0; i < n; ++i) {
                if (w.pts[size_t(i)].owner != me() || ms()[size_t(i)].attackOnWay.raw > 0) continue;
                const Fixed v = Fixed::fromInt(w.pts[size_t(i)].value) * pct;
                if (donor < 0 || mx < v) { donor = i; mx = v; }
            }
            if (donor < 0) return false;
            int rec = -1; Fixed best{};
            for (int i = 0; i < n; ++i) {
                const BwAI::PS& st = ms()[size_t(i)];
                if (i == donor || w.pts[size_t(i)].owner != me() || st.attackOnWay.raw <= 0 || st.fullDefense().raw <= 0) continue;
                const Fixed help = st.fullDefense() + mx * w.pts[size_t(i)].def;
                if (help.raw <= 0) continue;
                if (rec < 0 || best < help) { rec = i; best = help; }
            }
            if (rec < 0 || !usePoint(donor)) return false;
            c.sendPercent(donor, rec, kOne);
            return true;
        }
        case BwSkill::FogOfWar: {
            const Info in = info();
            if (in.duration.raw <= 0) return false;
            std::map<int, int> targets;
            for (auto& g : w.groups) {
                if (g.owner != me()) continue;
                const Fixed t = g.minDist / w.players[me()].speed;
                if (t >= in.duration / 4 && t <= in.duration * 3 / 4) continue;   // BW skips the groups inside the window
                const uint8_t o = w.pts[size_t(g.to)].owner;
                if (c.neutral(o) || !c.enemy(me(), o)) continue;
                targets[g.to] += g.need();
            }
            struct T { int point; int near; Vec3 pos; };
            std::vector<T> sorted;
            const Fixed r = in.range * 2 / 3;
            for (auto& [tp, cnt] : targets) {
                T t{tp, 0, w.pts[size_t(tp)].pos};
                const uint8_t mo = w.pts[size_t(tp)].owner;
                for (int i = 0; i < n; ++i) {
                    if (i == tp) continue;
                    const BwPoint& p = w.pts[size_t(i)];
                    if (!c.ally(mo, p.owner) && !p.tower && !c.neutral(p.owner)) continue;
                    if ((p.pos - w.pts[size_t(tp)].pos).xz().length() > r) continue;
                    ++t.near; t.pos += p.pos;
                }
                if (t.near == 0) continue;
                t.pos = t.pos / Fixed::fromInt(t.near + 1);
                sorted.push_back(t);
            }
            std::stable_sort(sorted.begin(), sorted.end(), [](const T& a, const T& b) { return a.near > b.near; });
            for (auto& t : sorted) if (ms()[size_t(t.point)].fullDefense().raw < 0) return use(t.pos);
            return false;
        }
        case BwSkill::Downgrade: {   // the enemy house that produced most / the tower that killed most (>= 100)
            const int delta = info().levelDelta;
            int best = -1, mx = 0;
            for (int i = 0; i < n; ++i) {
                const BwPoint& p = w.pts[size_t(i)];
                if (p.level + delta <= 2 || (p.role != BwRole::House && p.role != BwRole::Tower) || !c.enemy(me(), p.owner)) continue;
                const int v = p.role == BwRole::House ? p.produced : p.kills * 2;
                if (v < 100) continue;
                if (best < 0 || v > mx) { best = i; mx = v; }
            }
            return usePoint(best);
        }
        case BwSkill::Lock: case BwSkill::Spy: case BwSkill::None: return false;   // BW: disabled casters
        case BwSkill::Generic: default: {
            const PlayerAbilityDef& a = c.m.playerAbilityFor(c.sim, me(), k.ability);
            Vec3 pt; Entity tg;
            if (!c.m.smartTarget(c.sim, me(), a.payload, a.target, a.smart, pt, tg)) return false;
            return use(pt, tg);
        }
        }
    }
};
} // namespace

BwSkill bwSkillByName(const std::string& s) {
    for (size_t i = 0; i < size_t(BwSkill::Count); ++i) if (s == kSkillNames[i]) return BwSkill(i);
    if (s == "IncreaseDefense") return BwSkill::IncreaseDefence;
    return BwSkill::Count;
}
const char* bwSkillName(BwSkill k) { return size_t(k) < size_t(BwSkill::Count) ? kSkillNames[size_t(k)] : "?"; }

void BwImpl::initCasters(Ctx& c) {
    BwAI& ai = c.ai;
    ai.casters_.clear();
    GameState& gs = c.m.state(c.sim);
    if (c.me() >= gs.playerAbilities.size()) return;
    int slot = 0;
    for (size_t k = 0; k < c.R.playerAbilities.size() && k < gs.playerAbilities[c.me()].size(); ++k) {
        if (!gs.playerAbilities[c.me()][k].owned) continue;
        const PlayerAbilityDef& a = c.m.playerAbilityFor(c.sim, c.me(), int(k));
        if (a.passive || a.autoEvery.raw > 0) continue;
        BwAI::Caster cs;
        cs.ability = int(k); cs.slot = slot++;
        cs.kind = BwSkill::None;
        for (auto& [id, sk] : c.cfg.skills) if (id == int(k)) cs.kind = sk;
        if (cs.kind == BwSkill::None) cs.kind = infer(c.R, a);
        cs.timer = c.prof.skillSlotAwait[std::min(cs.slot, 3)];
        ai.casters_.push_back(cs);
    }
}

// AISkillAssistant.PrepareToSendUnits (BFG9000 aims at the source when the road bends or an enemy point is closer)
void BwImpl::prepareToSend(Ctx& c, int from, int to, int count) {
    (void)count;
    BwWorld& W = c.world();
    for (auto& k : c.ai.casters_) {
        if (k.kind != BwSkill::BFG9000) continue;
        k.a = kNullEntity;
        if (k.timer.raw > 0 || !c.m.canUsePlayerAbility(c.sim, c.me(), k.ability)) continue;
        if (W.pts[size_t(from)].owner != c.me() || c.ally(W.pts[size_t(to)].owner, c.me()) || !c.route(from, to)) continue;
        const Fixed len = c.dist(from, to);
        if (len.raw <= 0) continue;
        const Fixed straight = (W.pts[size_t(from)].pos - W.pts[size_t(to)].pos).xz().length();
        if (straight.raw <= 0) continue;
        if ((straight * straight) / (len * len) >= Fixed::fromRatio(9, 4)) { k.a = W.pts[size_t(from)].e; continue; }
        const uint8_t towner = W.pts[size_t(to)].owner;
        for (int i = 0; i < int(W.pts.size()); ++i) {
            if (i == to || W.pts[size_t(i)].owner != towner) continue;
            if ((W.pts[size_t(i)].pos - W.pts[size_t(from)].pos).xz().length() < straight) { k.a = W.pts[size_t(from)].e; break; }
        }
    }
}

void BwImpl::skills(Ctx& c) {
    BwAI& ai = c.ai;
    if (ai.casters_.empty() && ai.moveTasks_.empty()) return;
    BwWorld& W = c.world();
    if (W.players[c.me()].units <= 0) return;   // owner.hasAnyUnits
    // OnGroupStarted: new groups since the last tick (EnhanceWeapon / ChickenCoop candidates)
    uint32_t maxId = ai.lastGroupId_;
    for (auto& g : W.groups) {
        if (g.id <= ai.lastGroupId_ || g.from < 0) continue;
        maxId = std::max(maxId, g.id);
        for (auto& k : ai.casters_) {
            const BwPoint& tp = W.pts[size_t(g.to)];
            if (k.kind == BwSkill::EnhanceWeapon) {
                if (c.ally(tp.owner, c.me()) || !c.ally(g.owner, c.me()) || g.need() >= tp.value || k.n > g.need()) continue;
                k.n = g.need(); k.a = W.pts[size_t(g.from)].e;
            } else if (k.kind == BwSkill::ChickenCoop) {
                if (!c.ally(tp.owner, c.me()) || c.ally(g.owner, c.me()) || g.need() < tp.value || k.n > g.need()) continue;
                k.n = g.need(); k.a = W.pts[size_t(g.from)].e;
            }
        }
    }
    ai.lastGroupId_ = maxId;
    // Update: follow-ups (AISkillHypnosis.MoveUnitsTask: the hypnotised garrison is sent where it hurts its owner)
    for (auto& t : ai.moveTasks_) {
        t.timer -= c.dt;
        if (t.timer.raw > 0) continue;
        const int s = W.pointOf(t.from);
        if (s < 0 || W.pts[size_t(s)].owner != t.owner) { t.timer = Fixed::fromInt(-1000); continue; }   // the point changed hands
        bool hyp = false;   // the hypnosis is still on (cast resolved, not used yet)
        if (auto* st = c.sim.world().tryGet<Statuses>(t.from))
            for (auto& in : st->list) if ((c.R.statuses[in.def].flags & kStHypnotised) && in.owner == c.me() && in.until > c.sim.tick()) hyp = true;
        if (!hyp) { t.timer = Fixed::fromInt(-1000); continue; }
        const int count = int((Fixed::fromInt(W.pts[size_t(s)].value) * t.percent).floorInt());
        int to = -1;
        for (int i = 0; i < int(W.pts.size()) && to < 0; ++i) {
            if (!c.screen(i) || i == s) continue;
            const uint8_t o = W.pts[size_t(i)].owner;
            if (!c.neutral(t.owner)) { if (c.neutral(o) && W.pts[size_t(i)].value >= count) to = i; }   // into a stronger neutral
            else if (c.enemy(c.me(), o)) to = i;                                                       // neutrals: at an enemy
        }
        if (to < 0) {   // else the farthest point of its own side (at least 200 px away)
            Fixed fd{};
            for (int i = 0; i < int(W.pts.size()); ++i) {
                if (!c.screen(i) || i == s || !c.ally(t.owner, W.pts[size_t(i)].owner) || !c.route(s, i)) continue;
                const size_t n = c.ai.routes_.nodes.size();
                const Fixed L = c.ai.routes_.geo[size_t(s) * n + size_t(i)];
                if (L < c.px(200)) continue;
                if (to < 0 || fd < L) { to = i; fd = L; }
            }
        }
        if (to < 0) { t.timer = t.duration; continue; }
        c.sendPercent(s, to, t.percent);
        t.timer = Fixed::fromInt(-1000);
    }
    ai.moveTasks_.erase(std::remove_if(ai.moveTasks_.begin(), ai.moveTasks_.end(), [](const BwAI::MoveTask& t) { return t.timer <= Fixed::fromInt(-1000); }), ai.moveTasks_.end());
    for (auto& k : ai.casters_)
        if (k.kind == BwSkill::IllusionWave && k.a.valid()) {
            const int s = W.pointOf(k.a), t = W.pointOf(k.b);
            if (s >= 0 && t >= 0 && W.pts[size_t(s)].owner == c.me()) c.sendPercent(s, t, kOne);
            k.a = k.b = kNullEntity;
        }
    // UpdateTick (BW walks the slots backwards)
    for (int i = int(ai.casters_.size()) - 1; i >= 0; --i) {
        BwAI::Caster& k = ai.casters_[size_t(i)];
        const bool candidate = k.kind == BwSkill::EnhanceWeapon || k.kind == BwSkill::ChickenCoop;
        if (!c.m.canUsePlayerAbility(c.sim, c.me(), k.ability)) {
            k.heat = Fixed{};
        } else {
            k.timer -= c.dt;
            if (k.timer.raw <= 0) {
                k.timer = Fixed{};
                Caster cs{c, k};
                if (cs.tryUse()) k.timer = c.prof.skillSlotAwait[std::min(k.slot, 3)];
            }
        }
        if (candidate) { k.a = kNullEntity; k.n = 0; }
    }
}

} // namespace aiverse::game
