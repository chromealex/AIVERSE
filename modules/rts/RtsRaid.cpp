// Extraction raids: match phases, extraction points, channels, interactables, loot, objectives, receipts
// (RtsRaid.h, docs/raid.md).
#include "RtsInternal.h"
#include <algorithm>
#include <cstdio>

namespace aiverse::game {

using namespace detail;

namespace {
uint32_t ticksOf(Fixed s, uint32_t rate) { return s.raw <= 0 ? 0 : uint32_t((s * Fixed::fromInt(rate)).toInt()); }
Fixed sq(Fixed v) { return v * v; }
bool within(Vec3 a, Fixed x, Fixed z, Fixed r) { return sq(a.x - x) + sq(a.z - z) <= sq(r); }
const char* statusName(uint8_t s) {
    static const char* n[] = {"waiting", "in_raid", "extracted", "dead", "timeout", "abandoned"};
    return s < 6 ? n[s] : "?";
}
std::string hex(uint64_t v) { char b[20]; std::snprintf(b, sizeof b, "%016llx", (unsigned long long)v); return b; }
bool resolved(uint8_t s) { return s >= uint8_t(RaidStatus::Extracted); }
} // namespace

RaidStore* Raid::store(Simulation& sim) const { return sim.world().resource<RaidStore>(); }

bool Raid::isRaider(Simulation& sim, Entity e) const {
    const Unit* u = sim.world().tryGet<Unit>(e);
    if (!u) return false;
    const UnitType& t = m_->rules().types[u->type];
    if (!R_.raiderTypes.empty() || R_.raiderTags)
        return std::find(R_.raiderTypes.begin(), R_.raiderTypes.end(), u->type) != R_.raiderTypes.end() || (t.tags & R_.raiderTags);
    return t.mech.inventorySlots > 0;
}

std::vector<Entity> Raid::raidersOf(Simulation& sim, uint8_t player) const {
    std::vector<Entity> out;
    sim.world().each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
        if (o.player == player && u.hp.raw > 0 && !(u.flags & kUnitOffMap) && isRaider(sim, e)) out.push_back(e);
    });
    return out;
}

Vec3 Raid::raiderPos(Simulation& sim, uint8_t player, bool& ok) const {
    auto rs = raidersOf(sim, player);
    ok = !rs.empty();
    return ok ? sim.world().get<Transform>(rs[0]).pos : Vec3{};
}

void Raid::setup(Simulation& sim, RtsModule& m, const Json& map) {
    m_ = &m;
    R_ = raidRulesForMap(m.rules(), map);
    if (!R_.enabled) return;
    RaidStore& st = sim.world().setResource<RaidStore>();
    Hasher h;
    h.u64(sim.config().seed);
    h.str(map.dump(-1));
    st.matchId = h.value();
    for (auto& d : R_.points) {
        RaidPointState ps;
        ps.x = d.x; ps.z = d.z;
        st.points.push_back(ps);
    }
    const size_t nres = m.rules().resources.size();
    for (auto& pi : sim.players()) {
        if (raidersOf(sim, pi.slot).empty()) continue;   // enemies / AI armies are not raiders
        RaidPlayer p;
        p.slot = pi.slot;
        p.team = pi.team;
        p.hadRaider = true;
        p.carriedRes.assign(nres, Fixed{});
        p.securedRes.assign(nres, Fixed{});
        p.objProgress.assign(R_.objectives.size(), 0);
        p.objDone.assign(R_.objectives.size(), 0);
        st.players.push_back(p);
    }
    st.receipts.assign(st.players.size(), std::string());
}

void Raid::setPointPhase(Simulation& sim, size_t i, RaidPointPhase ph, uint32_t until) {
    RaidPointState& ps = store(sim)->points[i];
    if (ps.phase == uint8_t(ph) && ps.until == until) return;
    ps.phase = uint8_t(ph);
    ps.until = until;
    if (ph == RaidPointPhase::Open) { ps.fill = Fixed{}; ps.used = 0; }
    if (ph == RaidPointPhase::Sleep) ps.caller = 0;
    sim.emit({kEvRaidPoint, ps.caller, kNullEntity, kNullEntity, int64_t(ph) | int64_t(i) << 8, {ps.x, Fixed{}, ps.z}});
}

void Raid::start(Simulation& sim) {
    RaidStore& st = *store(sim);
    st.phase = uint8_t(RaidPhase::Running);
    st.phaseTick = sim.tick();
    st.endTick = R_.duration.raw > 0 ? sim.tick() + ticksOf(R_.duration, sim.tickRate()) : 0;
    for (auto& p : st.players) {
        p.status = uint8_t(RaidStatus::InRaid);
        sim.emit({kEvRaidPlayer, p.slot, kNullEntity, kNullEntity, p.status, {}});
    }
    sim.emit({kEvRaidPhase, 0, kNullEntity, kNullEntity, int64_t(RaidPhase::Running), {}});
}

void Raid::tick(Simulation& sim) {
    RaidStore* sp = store(sim);
    if (!sp || sp->phase == uint8_t(RaidPhase::Over)) return;
    if (sp->phase == uint8_t(RaidPhase::Briefing)) {
        if (sim.tick() < ticksOf(R_.startDelay, sim.tickRate())) return;
        start(sim);
    }
    // damage this tick (interrupt rules): compare the raiders' hp with the last tick
    for (auto& p : store(sim)->players) {
        Fixed cur{};
        for (Entity e : raidersOf(sim, p.slot)) cur += sim.world().get<Unit>(e).hp;
        p.hit = cur < p.hpSum;
        p.hpSum = cur;
    }
    tickDeaths(sim);
    tickPoints(sim);
    tickInteract(sim);
    tickPickup(sim);
    tickObjectives(sim);
    tickChannels(sim);
    RaidStore& st = *store(sim);
    bool all = true, any = false;
    for (auto& p : st.players) { all &= resolved(p.status); any |= p.status == uint8_t(RaidStatus::Extracted); }
    if (all || (st.endTick && sim.tick() >= st.endTick) || (R_.endOnFirstExtract && any)) finish(sim);
}

// ---------------------------------------------------------------- deaths (before the "death" system)
void Raid::tickDeaths(Simulation& sim) {
    World& w = sim.world();
    RaidStore& st = *store(sim);
    std::vector<Entity> dying;
    w.each<Unit>([&](Entity e, Unit& u) { if (u.hp.raw <= 0 && !(u.flags & kUnitSilent)) dying.push_back(e); });
    for (Entity e : dying) {
        Unit& u = w.get<Unit>(e);
        const uint8_t owner = w.get<Owner>(e).player;
        RaidPlayer* killer = st.find(u.lastAttacker);
        if (killer && killer->status == uint8_t(RaidStatus::InRaid) && m_->hostile(sim, u.lastAttacker, owner)) {
            ++killer->kills;
            for (size_t k = 0; k < R_.objectives.size(); ++k) {
                const RaidObjectiveDef& o = R_.objectives[k];
                if (o.kind == RaidObjKind::Kill && (o.type == 0xFFFF || o.type == u.type)) progress(sim, killer->slot, int(k), 1);
            }
        }
        RaidPlayer* p = st.find(owner);
        if (!p || p->status != uint8_t(RaidStatus::InRaid) || !isRaider(sim, e)) continue;
        if (auto* bag = w.tryGet<ItemBag>(e))
            for (size_t i = 0; i < bag->items.size(); ++i)
                if (bag->items[i] >= 0) {
                    addSorted(p->lost, uint16_t(bag->items[i]), 1);
                    if (R_.loss == RaidLoss::Destroy) bag->items[i] = -1;   // else the item system drops it
                }
        if (raidersOf(sim, owner).empty()) {
            p->killer = u.lastAttacker;
            resolve(sim, owner, RaidStatus::Dead);
        }
    }
}

// ---------------------------------------------------------------- extraction points
void Raid::tickPoints(Simulation& sim) {
    RaidStore& st = *store(sim);
    const uint32_t now = sim.tick(), rate = sim.tickRate();
    const uint32_t elapsed = now - st.phaseTick;
    for (size_t i = 0; i < R_.points.size(); ++i) {
        const RaidPointDef& d = R_.points[i];
        RaidPointState& ps = st.points[i];
        const bool due = ps.until && now >= ps.until;
        auto openNow = [&] {
            if (d.arrival.raw > 0) setPointPhase(sim, i, RaidPointPhase::Inbound, now + ticksOf(d.arrival, rate));
            else setPointPhase(sim, i, RaidPointPhase::Open, d.open.raw > 0 ? now + ticksOf(d.open, rate) : 0);
        };
        switch (RaidPointPhase(ps.phase)) {
        case RaidPointPhase::Locked:
            if (elapsed >= ticksOf(d.opensAt, rate)) { if (d.call) setPointPhase(sim, i, RaidPointPhase::Sleep, 0); else openNow(); }
            break;
        case RaidPointPhase::Sleep:
            if (due && !d.call) openNow();
            break;
        case RaidPointPhase::Inbound:
            if (due) setPointPhase(sim, i, RaidPointPhase::Open, d.open.raw > 0 ? now + ticksOf(d.open, rate) : 0);
            break;
        case RaidPointPhase::Open:
            if (due || (d.capacity > 0 && ps.used >= d.capacity)) setPointPhase(sim, i, RaidPointPhase::Departing, now + std::max<uint32_t>(1, ticksOf(d.departure, rate)));
            break;
        case RaidPointPhase::Departing:
            if (due) {
                if (d.call) setPointPhase(sim, i, RaidPointPhase::Sleep, 0);
                else if (d.open.raw > 0) {
                    if (d.sleep.raw > 0) setPointPhase(sim, i, RaidPointPhase::Sleep, now + ticksOf(d.sleep, rate));
                    else openNow();
                } else setPointPhase(sim, i, RaidPointPhase::Sleep, 0);   // a spent permanent point closes
            }
            break;
        }
        // follow points stay around the caller's raider while called
        if (d.follow && ps.caller && (ps.phase == uint8_t(RaidPointPhase::Inbound) || ps.phase == uint8_t(RaidPointPhase::Open))) {
            bool ok = false;
            const Vec3 at = raiderPos(sim, ps.caller, ok);
            if (ok) { ps.x = at.x; ps.z = at.z; }
        }
    }
}

bool Raid::canUsePoint(Simulation& sim, uint8_t player, int point) const {
    RaidStore* st = store(sim);
    if (!st || point < 0 || size_t(point) >= R_.points.size()) return false;
    const RaidPointDef& d = R_.points[size_t(point)];
    if (d.team > 0 && m_->teamOf(sim, player) != d.team) return false;
    if (d.follow && st->points[size_t(point)].caller && st->points[size_t(point)].caller != player) return false;
    for (auto& q : d.requires_) if (!objectiveDone(sim, player, R_.findObjective(q))) return false;
    return true;
}

void Raid::tickChannels(Simulation& sim) {
    RaidStore& st = *store(sim);
    const Fixed dt = sim.dt();
    ScriptEngine* se = m_->scripts();
    auto vetoed = [&](uint8_t player, size_t i) {
        if (!se) return false;
        const RaidPointDef& d = R_.points[i];
        if (!d.fn.empty() && !se->allowNamed(sim, d.fn, player, d.id)) return true;
        return !se->allowNamed(sim, "raid_can_extract", player, d.id);
    };
    for (size_t i = 0; i < R_.points.size(); ++i) {
        const RaidPointDef& d = R_.points[i];
        RaidPointState& ps = st.points[i];
        const bool open = ps.phase == uint8_t(RaidPointPhase::Open);
        // who is inside (slot order)
        std::vector<uint8_t> inside, hostileIn;
        for (auto& p : st.players) {
            if (p.status != uint8_t(RaidStatus::InRaid)) continue;
            bool in = false;
            for (Entity e : raidersOf(sim, p.slot)) in |= within(sim.world().get<Transform>(e).pos, ps.x, ps.z, d.radius);
            if (!in) continue;
            if (canUsePoint(sim, p.slot, int(i))) inside.push_back(p.slot);
        }
        auto contested = [&](uint8_t slot) {
            for (auto& q : st.players) {
                if (q.slot == slot || q.status != uint8_t(RaidStatus::InRaid) || !m_->hostile(sim, slot, q.slot)) continue;
                for (Entity e : raidersOf(sim, q.slot)) if (within(sim.world().get<Transform>(e).pos, ps.x, ps.z, d.radius)) return true;
            }
            return false;
        };
        if (d.scope == RaidScope::Point) {
            if (!open) continue;
            if (inside.empty()) { if (d.leave == RaidLeave::Reset) ps.fill = Fixed{}; continue; }
            bool block = false, reset = false;
            for (uint8_t s : inside) {
                if (contested(s)) { block |= d.contest == RaidContest::Block; reset |= d.contest == RaidContest::Reset; }
                if (d.interruptOnDamage && st.find(s)->hit) reset = true;
            }
            if (reset) { if (ps.fill.raw > 0) sim.emit({kEvRaidChannel, inside[0], kNullEntity, kNullEntity, int64_t(i) | int64_t(RaidChannelEv::Interrupted) << 16, {ps.x, Fixed{}, ps.z}}); ps.fill = Fixed{}; continue; }
            if (block) continue;
            if (ps.fill.raw == 0) sim.emit({kEvRaidChannel, inside[0], kNullEntity, kNullEntity, int64_t(i) | int64_t(RaidChannelEv::Start) << 16, {ps.x, Fixed{}, ps.z}});
            ps.fill += dt;
            if (ps.fill >= d.channel) {
                ps.fill = Fixed{};
                for (uint8_t s : inside) {
                    if (d.capacity > 0 && ps.used >= d.capacity) break;
                    if (!vetoed(s, i)) extract(sim, s, int(i));
                }
            }
            continue;
        }
        // per-player channels
        for (auto& p : st.players) {
            auto it = std::find_if(st.channels.begin(), st.channels.end(), [&](const RaidChannel& c) { return c.player == p.slot && c.point == i; });
            const bool in = open && std::find(inside.begin(), inside.end(), p.slot) != inside.end();
            if (!in) {
                if (it != st.channels.end() && (d.leave == RaidLeave::Reset || !open || p.status != uint8_t(RaidStatus::InRaid))) {
                    sim.emit({kEvRaidChannel, p.slot, kNullEntity, kNullEntity, int64_t(i) | int64_t(RaidChannelEv::Interrupted) << 16, {ps.x, Fixed{}, ps.z}});
                    st.channels.erase(it);
                }
                continue;
            }
            if (it == st.channels.end()) {
                RaidChannel c{p.slot, uint16_t(i), Fixed{}};
                it = st.channels.insert(std::upper_bound(st.channels.begin(), st.channels.end(), c,
                                                         [](const RaidChannel& a, const RaidChannel& b) { return a.player != b.player ? a.player < b.player : a.point < b.point; }), c);
                sim.emit({kEvRaidChannel, p.slot, kNullEntity, kNullEntity, int64_t(i) | int64_t(RaidChannelEv::Start) << 16, {ps.x, Fixed{}, ps.z}});
            }
            if (contested(p.slot)) {
                if (d.contest == RaidContest::Block) continue;
                if (d.contest == RaidContest::Reset) { it->progress = Fixed{}; continue; }
            }
            if (d.interruptOnDamage && p.hit && it->progress.raw > 0) {
                it->progress = Fixed{};
                sim.emit({kEvRaidChannel, p.slot, kNullEntity, kNullEntity, int64_t(i) | int64_t(RaidChannelEv::Interrupted) << 16, {ps.x, Fixed{}, ps.z}});
                continue;
            }
            it->progress += dt;
            if (it->progress >= d.channel && !(d.capacity > 0 && ps.used >= d.capacity)) {
                if (vetoed(p.slot, i)) { it->progress = Fixed{}; continue; }
                const uint8_t slot = p.slot;
                st.channels.erase(it);
                extract(sim, slot, int(i));
            }
        }
    }
}

void Raid::secureCarried(Simulation& sim, RaidPlayer& p) {
    World& w = sim.world();
    w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
        if (o.player != p.slot || u.hp.raw <= 0) return;
        if (auto* bag = w.tryGet<ItemBag>(e))
            for (size_t i = 0; i < bag->items.size(); ++i)
                if (bag->items[i] >= 0) { addSorted(p.secured, uint16_t(bag->items[i]), 1); bag->items[i] = -1; }
    });
    for (size_t r = 0; r < p.carriedRes.size(); ++r) { p.securedRes[r] += p.carriedRes[r]; p.carriedRes[r] = Fixed{}; }
}

bool Raid::extract(Simulation& sim, uint8_t player, int point) {
    RaidStore* st = store(sim);
    RaidPlayer* p = st ? st->find(player) : nullptr;
    if (!p || p->status != uint8_t(RaidStatus::InRaid)) return false;
    World& w = sim.world();
    secureCarried(sim, *p);
    Entity body = kNullEntity;
    Vec3 at{};
    w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
        if (o.player != player || u.hp.raw <= 0 || (!R_.removeAll && !isRaider(sim, e))) return;
        if (!body.valid() && isRaider(sim, e)) { body = e; at = w.get<Transform>(e).pos; }
        u.hp = Fixed{};
        u.flags |= kUnitSilent;   // leaves with the helicopter: no death effects, nothing dropped
    });
    p->point = int16_t(point);
    if (point >= 0 && size_t(point) < st->points.size()) ++st->points[size_t(point)].used;
    sim.emit({kEvRaidExtracted, player, body, kNullEntity, point, at});
    resolve(sim, player, RaidStatus::Extracted);
    return true;
}

bool Raid::resolve(Simulation& sim, uint8_t player, RaidStatus s) {
    RaidStore* st = store(sim);
    RaidPlayer* p = st ? st->find(player) : nullptr;
    if (!p || resolved(p->status)) return false;
    if (s != RaidStatus::Extracted) {
        World& w = sim.world();
        // what was still carried is lost (dead raiders' bags were recorded in tickDeaths)
        w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
            if (o.player != player || u.hp.raw <= 0) return;
            if (auto* bag = w.tryGet<ItemBag>(e))
                for (size_t i = 0; i < bag->items.size(); ++i)
                    if (bag->items[i] >= 0) {
                        addSorted(p->lost, uint16_t(bag->items[i]), 1);
                        if (s == RaidStatus::Abandoned) bag->items[i] = -1;
                    }
            if (s == RaidStatus::Abandoned) { u.hp = Fixed{}; u.flags |= kUnitSilent; }
        });
        for (auto& r : p->carriedRes) r = Fixed{};
    }
    st->channels.erase(std::remove_if(st->channels.begin(), st->channels.end(), [&](const RaidChannel& c) { return c.player == player; }), st->channels.end());
    p->status = uint8_t(s);
    p->endTick = sim.tick();
    sim.emit({kEvRaidPlayer, player, kNullEntity, kNullEntity, p->status, {}});
    return true;
}

// ---------------------------------------------------------------- interactables (hold to use)
void Raid::tickInteract(Simulation& sim) {
    if (!R_.anyInteract) return;
    World& w = sim.world();
    RaidStore& st = *store(sim);
    const uint32_t now = sim.tick();
    const Rules& rules = m_->rules();
    struct Done { Entity e; uint8_t user; uint16_t type; Vec3 pos; Entity by; };
    std::vector<Done> done;
    std::vector<std::pair<uint8_t, Entity>> users;   // per raid player: its raider in range
    w.each<Unit, Transform>([&](Entity e, Unit& u, Transform& t) {
        if (u.type >= R_.interact.size() || !R_.interact[u.type].on || u.hp.raw <= 0) return;
        const RaidInteractDef& d = R_.interact[u.type];
        const uint64_t key = e.packed();
        auto it = std::lower_bound(st.uses.begin(), st.uses.end(), key, [](const RaidUse& a, uint64_t k) { return a.entity < k; });
        RaidUse* use = it != st.uses.end() && it->entity == key ? &*it : nullptr;
        if (use && d.uses > 0 && use->done >= d.uses) return;   // spent (kept: consume false)
        users.clear();
        const Fixed reach = d.range + rules.types[u.type].radius;
        for (auto& p : st.players) {
            if (p.status != uint8_t(RaidStatus::InRaid)) continue;
            for (Entity r : raidersOf(sim, p.slot))
                if (within(w.get<Transform>(r).pos, t.pos.x, t.pos.z, reach + rules.types[w.get<Unit>(r).type].radius)) { users.push_back({p.slot, r}); break; }
        }
        if (users.empty() || (d.single && users.size() > 1)) {
            if (use && use->progress.raw > 0 && (d.leave == RaidLeave::Reset || users.size() > 1)) {
                sim.emit({kEvRaidInteract, use->user, kNullEntity, e, int64_t(u.type) | int64_t(RaidChannelEv::Interrupted) << 16, t.pos});
                use->progress = Fixed{};
            }
            return;
        }
        if (!use) {
            RaidUse nu;
            nu.entity = key;
            use = &*st.uses.insert(it, nu);
        }
        if (now < use->readyAt) return;
        // the current user keeps it while in range; else the lowest slot
        auto cur = std::find_if(users.begin(), users.end(), [&](auto& x) { return x.first == use->user; });
        if (cur == users.end()) { cur = users.begin(); use->user = cur->first; use->progress = Fixed{}; }
        RaidPlayer* p = st.find(cur->first);
        if (d.interruptOnDamage && p->hit) {
            if (use->progress.raw > 0) sim.emit({kEvRaidInteract, p->slot, cur->second, e, int64_t(u.type) | int64_t(RaidChannelEv::Interrupted) << 16, t.pos});
            use->progress = Fixed{};
            return;
        }
        if (use->progress.raw == 0) sim.emit({kEvRaidInteract, p->slot, cur->second, e, int64_t(u.type) | int64_t(RaidChannelEv::Start) << 16, t.pos});
        use->progress += sim.dt();
        if (use->progress < d.hold) return;
        use->progress = Fixed{};
        ++use->done;
        use->readyAt = now + ticksOf(d.cooldown, sim.tickRate());
        done.push_back({e, p->slot, u.type, t.pos, cur->second});
    });
    // effects after the loop (loot, objectives, Lua, removal)
    for (const Done& d : done) {
        const RaidInteractDef& def = R_.interact[d.type];
        sim.emit({kEvRaidInteract, d.user, d.by, d.e, int64_t(d.type) | int64_t(RaidChannelEv::Done) << 16, d.pos});
        if (def.loot >= 0) spawnLoot(sim, def.loot, d.pos, d.user);
        if (!def.objective.empty()) progress(sim, d.user, R_.findObjective(def.objective), 1);
        for (size_t k = 0; k < R_.objectives.size(); ++k) {
            const RaidObjectiveDef& o = R_.objectives[k];
            if (o.kind == RaidObjKind::Interact && o.type == d.type && o.id != def.objective) progress(sim, d.user, int(k), 1);
        }
        if (!def.fn.empty() && m_->scripts()) {
            Json args = Json::object();
            args.set("target", Json::number(int64_t(d.e.packed())));
            m_->scripts()->callAction(sim, def.fn, args, d.user, d.by, d.pos, "raid");
        }
        const RaidStore& s2 = *store(sim);
        auto it = std::lower_bound(s2.uses.begin(), s2.uses.end(), d.e.packed(), [](const RaidUse& a, uint64_t k) { return a.entity < k; });
        if (def.consume && def.uses > 0 && it != s2.uses.end() && it->done >= def.uses && w.alive(d.e)) {
            Unit& u = w.get<Unit>(d.e);
            u.hp = Fixed{};
            u.flags |= kUnitSilent;
        }
    }
    // forget objects that are gone
    st.uses.erase(std::remove_if(st.uses.begin(), st.uses.end(), [&](const RaidUse& x) { return !w.alive(Entity::fromPacked(x.entity)); }), st.uses.end());
}

int Raid::spawnLoot(Simulation& sim, int table, Vec3 at, uint8_t player) {
    if (table < 0 || size_t(table) >= R_.loot.size()) return 0;
    const RaidLootTable& t = R_.loot[size_t(table)];
    MechState& ms = m_->state(sim).mech;
    RaidPlayer* p = store(sim) ? store(sim)->find(player) : nullptr;
    const int rolls = t.rollsMin == t.rollsMax ? t.rollsMin : sim.rng().range(t.rollsMin, t.rollsMax);
    int placed = 0;
    // ground spots around the container: a fixed ring, so the layout is part of the rules, not of the rng
    static const int kRing[8][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}, {1, 1}, {-1, 1}, {-1, -1}, {1, -1}};
    for (int r = 0; r < rolls; ++r) {
        if (t.total == 0) break;
        uint32_t pick = sim.rng().below(t.total);
        const RaidLootEntry* e = nullptr;
        for (auto& x : t.entries) { if (pick < x.weight) { e = &x; break; } pick -= x.weight; }
        if (!e) continue;
        const int count = e->countMin == e->countMax ? e->countMin : sim.rng().range(e->countMin, e->countMax);
        if (e->res >= 0) {
            if (p && p->status == uint8_t(RaidStatus::InRaid)) p->carriedRes[size_t(e->res)] += e->amount * int64_t(count);
            sim.emit({kEvRaidLoot, player, kNullEntity, kNullEntity, int64_t(e->res) | int64_t(count) << 16 | int64_t(1) << 40, at});
            continue;
        }
        if (e->item < 0) continue;
        for (int c = 0; c < count; ++c) {
            const int k = int(ms.groundItems.size()) % 8;
            Vec3 pos = at;
            pos.x += Fixed::fromRatio(kRing[k][0] * 3, 4);
            pos.z += Fixed::fromRatio(kRing[k][1] * 3, 4);
            ms.groundItems.push_back({pos, uint16_t(e->item), m_->rules().mech.items[size_t(e->item)].charges});
            ++placed;
        }
        sim.emit({kEvRaidLoot, player, kNullEntity, kNullEntity, int64_t(e->item) | int64_t(count) << 16, at});
    }
    return placed;
}

bool Raid::give(Simulation& sim, uint8_t player, int item, int count, bool secured) {
    RaidPlayer* p = store(sim) ? store(sim)->find(player) : nullptr;
    if (!p || item < 0 || size_t(item) >= m_->rules().mech.items.size() || count <= 0) return false;
    if (secured) { addSorted(p->secured, uint16_t(item), count); return true; }
    int given = 0;
    for (Entity e : raidersOf(sim, player)) {
        auto* bag = sim.world().tryGet<ItemBag>(e);
        if (!bag) continue;
        for (size_t i = 0; i < bag->items.size() && given < count; ++i)
            if (bag->items[i] < 0) { bag->items[i] = int16_t(item); bag->charges[i] = m_->rules().mech.items[size_t(item)].charges; ++given; }
    }
    return given == count;
}

void Raid::tickPickup(Simulation& sim) {
    if (R_.pickupRadius.raw <= 0) return;
    World& w = sim.world();
    MechState& ms = m_->state(sim).mech;
    if (ms.groundItems.empty()) return;
    for (auto& p : store(sim)->players) {
        if (p.status != uint8_t(RaidStatus::InRaid)) continue;
        for (Entity e : raidersOf(sim, p.slot)) {
            auto* bag = w.tryGet<ItemBag>(e);
            if (!bag) continue;
            auto slot = std::find(bag->items.begin(), bag->items.end(), int16_t(-1));
            if (slot == bag->items.end()) continue;
            const Vec3 at = w.get<Transform>(e).pos;
            size_t best = ms.groundItems.size();
            Fixed bd{};
            for (size_t i = 0; i < ms.groundItems.size(); ++i) {
                const Fixed d = dist2D(ms.groundItems[i].pos, at);
                if (d <= R_.pickupRadius && (best == ms.groundItems.size() || d < bd)) { best = i; bd = d; }
            }
            if (best == ms.groundItems.size()) continue;
            const size_t si = size_t(slot - bag->items.begin());
            bag->items[si] = int16_t(ms.groundItems[best].item);
            bag->charges[si] = ms.groundItems[best].charges;
            sim.emit({kEvItem, p.slot, e, kNullEntity, ms.groundItems[best].item, at});
            ms.groundItems.erase(ms.groundItems.begin() + ptrdiff_t(best));
            if (ms.groundItems.empty()) return;
        }
    }
}

// ---------------------------------------------------------------- objectives
bool Raid::objectiveDone(Simulation& sim, uint8_t player, int k) const {
    RaidStore* st = store(sim);
    const RaidPlayer* p = st ? st->find(player) : nullptr;
    return p && k >= 0 && size_t(k) < p->objDone.size() && p->objDone[size_t(k)];
}

void Raid::progress(Simulation& sim, uint8_t player, int k, int amount) {
    RaidStore* st = store(sim);
    RaidPlayer* p = st ? st->find(player) : nullptr;
    if (!p || k < 0 || size_t(k) >= R_.objectives.size() || p->objDone[size_t(k)] || resolved(p->status)) return;
    const RaidObjectiveDef& o = R_.objectives[size_t(k)];
    p->objProgress[size_t(k)] = std::max(0, std::min(o.count, p->objProgress[size_t(k)] + amount));
    if (p->objProgress[size_t(k)] < o.count) return;
    const uint8_t team = p->team;
    for (auto& q : st->players) {
        if (q.slot != player && !(o.shared && q.team == team && !resolved(q.status))) continue;
        if (q.objDone[size_t(k)]) continue;
        q.objDone[size_t(k)] = 1;
        q.objProgress[size_t(k)] = o.count;
        sim.emit({kEvRaidObjective, q.slot, kNullEntity, kNullEntity, int64_t(k) | int64_t(1) << 16, {}});
    }
}

void Raid::tickObjectives(Simulation& sim) {
    RaidStore& st = *store(sim);
    World& w = sim.world();
    for (size_t k = 0; k < R_.objectives.size(); ++k) {
        const RaidObjectiveDef& o = R_.objectives[k];
        if (o.kind != RaidObjKind::Reach && o.kind != RaidObjKind::Collect) continue;
        for (auto& p : st.players) {
            if (p.status != uint8_t(RaidStatus::InRaid) || p.objDone[k]) continue;
            int have = 0;
            for (Entity e : raidersOf(sim, p.slot)) {
                if (o.kind == RaidObjKind::Reach) { if (within(w.get<Transform>(e).pos, o.x, o.z, o.radius)) have = o.count; continue; }
                if (auto* bag = w.tryGet<ItemBag>(e)) for (int16_t it : bag->items) have += it == o.item;
            }
            if (o.kind == RaidObjKind::Collect) for (auto& [it, n] : p.secured) if (it == o.item) have += n;
            if (have > p.objProgress[k]) progress(sim, p.slot, int(k), have - p.objProgress[k]);
        }
    }
}

// ---------------------------------------------------------------- end of the raid, receipts
int64_t Raid::securedValue(const RaidPlayer& p) const {
    int64_t v = 0;
    for (auto& [it, n] : p.secured) v += R_.itemValue[it] * n;
    for (size_t r = 0; r < p.securedRes.size(); ++r) v += p.securedRes[r].toInt();
    return v;
}

void Raid::finish(Simulation& sim) {
    RaidStore& st = *store(sim);
    for (auto& p : st.players)
        if (!resolved(p.status)) resolve(sim, p.slot, RaidStatus::TimedOut);
    // winner: the team that brought out the most (value + one per extracted raider); nobody out: the hostiles (PvE)
    std::vector<std::pair<int, int64_t>> score;   // team -> score
    for (auto& p : st.players) {
        if (p.status != uint8_t(RaidStatus::Extracted)) continue;
        auto it = std::find_if(score.begin(), score.end(), [&](auto& x) { return x.first == p.team; });
        if (it == score.end()) { score.push_back({p.team, 0}); it = score.end() - 1; }
        it->second += securedValue(p) + 1;
    }
    std::sort(score.begin(), score.end());
    int winner = 0;
    int64_t best = -1;
    bool tie = false;
    for (auto& [team, s] : score) {
        if (s > best) { best = s; winner = team; tie = false; }
        else if (s == best) tie = true;
    }
    if (tie) winner = 0;
    if (score.empty() && !R_.pvp)
        for (auto& pi : sim.players())
            if (pi.alive && !st.find(pi.slot) && pi.team) {
                bool foe = false;
                for (auto& p : st.players) foe |= m_->hostile(sim, pi.slot, p.slot);
                if (foe) { winner = pi.team; break; }
            }
    st.winnerTeam = winner;
    st.phase = uint8_t(RaidPhase::Over);
    for (size_t i = 0; i < st.players.size(); ++i) st.receipts[i] = receiptJson(sim, st.players[i]);
    m_->state(sim).winnerTeam = winner;   // result(): 0 = draw
    sim.emit({kEvRaidPhase, 0, kNullEntity, kNullEntity, int64_t(RaidPhase::Over), {}});
}

std::string Raid::receiptJson(Simulation& sim, const RaidPlayer& p) const {
    const Rules& rules = m_->rules();
    const RaidStore& st = *store(sim);
    const std::string id = "raid-" + hex(st.matchId) + "-p" + std::to_string(p.slot);
    const bool out = p.status == uint8_t(RaidStatus::Extracted);
    Json j = Json::object();
    j.set("schema", Json::number(int64_t(1)));
    j.set("id", Json::string(id));                 // the meta operation id: consume once
    j.set("match", Json::string(hex(st.matchId)));
    j.set("mode", Json::string(R_.pvp ? "pvp" : "pve"));
    j.set("player", Json::number(int64_t(p.slot)));
    j.set("team", Json::number(int64_t(p.team)));
    j.set("outcome", Json::string(statusName(p.status)));
    j.set("final", Json::fromBool(st.phase == uint8_t(RaidPhase::Over)));
    if (out && p.point >= 0 && size_t(p.point) < R_.points.size()) j.set("point", Json::string(R_.points[size_t(p.point)].id));
    const uint32_t end = p.endTick ? p.endTick : sim.tick();
    j.set("ms", Json::number(int64_t(end >= st.phaseTick ? end - st.phaseTick : 0) * 1000 / int64_t(sim.tickRate())));   // raid time
    j.set("winner", Json::fromBool(st.phase == uint8_t(RaidPhase::Over) && st.winnerTeam > 0 && st.winnerTeam == p.team));
    bool req = out;
    for (size_t k = 0; k < R_.objectives.size(); ++k) if (R_.objectives[k].required && !p.objDone[k]) req = false;
    j.set("completed", Json::fromBool(req));
    j.set("value", Json::number(securedValue(p)));
    auto items = [&](const std::vector<std::pair<uint16_t, int>>& v) {
        Json o = Json::object();
        for (auto& [it, n] : v) o.set(rules.mech.items[it].id, Json::number(int64_t(n)));
        return o;
    };
    j.set("secured", items(p.secured));
    j.set("lost", items(p.lost));
    Json res = Json::object();
    for (size_t r = 0; r < p.securedRes.size(); ++r) if (p.securedRes[r].raw) res.set(rules.resources[r].id, Json::number(p.securedRes[r]));
    j.set("resources", res);
    Json objs = Json::object();
    for (size_t k = 0; k < R_.objectives.size(); ++k) {
        Json o = Json::object();
        o.set("done", Json::fromBool(p.objDone[k] != 0));
        o.set("progress", Json::number(int64_t(p.objProgress[k])));
        o.set("count", Json::number(int64_t(R_.objectives[k].count)));
        objs.set(R_.objectives[k].id, o);
    }
    j.set("objectives", objs);
    j.set("kills", Json::number(int64_t(p.kills)));
    if (p.status == uint8_t(RaidStatus::Dead)) j.set("killer", Json::number(int64_t(p.killer)));
    // reward bundle for the meta (asset id -> amount): only what was brought out
    std::vector<std::pair<std::string, int64_t>> bundle;
    auto add = [&](const std::string& a, int64_t n) {
        if (n <= 0) return;
        auto it = std::find_if(bundle.begin(), bundle.end(), [&](auto& x) { return x.first == a; });
        if (it == bundle.end()) bundle.push_back({a, n}); else it->second += n;
    };
    if (out) {
        for (auto& [it, n] : p.secured) add(R_.itemAsset[it], n);
        for (size_t r = 0; r < p.securedRes.size(); ++r) add(rules.resources[r].id, p.securedRes[r].toInt());
        for (size_t k = 0; k < R_.objectives.size(); ++k)
            if (p.objDone[k]) for (auto& [a, n] : R_.objectives[k].reward) add(a, n);
    }
    std::sort(bundle.begin(), bundle.end());
    Json rw = Json::object();
    for (auto& [a, n] : bundle) rw.set(a, Json::number(n));
    j.set("reward", rw);
    return j.dump(-1);
}

void Raid::addSorted(std::vector<std::pair<uint16_t, int>>& v, uint16_t item, int n) const {
    auto it = std::lower_bound(v.begin(), v.end(), item, [](const std::pair<uint16_t, int>& a, uint16_t k) { return a.first < k; });
    if (it != v.end() && it->first == item) it->second += n;
    else v.insert(it, {item, n});
}

// ---------------------------------------------------------------- commands
bool Raid::command(Simulation& sim, const Command& cmd) {
    RaidStore* st = store(sim);
    RaidPlayer* p = st ? st->find(cmd.player) : nullptr;
    if (!p || p->status != uint8_t(RaidStatus::InRaid) || st->phase != uint8_t(RaidPhase::Running)) return true;
    const int64_t op = cmd.param & 0xFF;
    if (op == kRaidAbandon) { resolve(sim, cmd.player, RaidStatus::Abandoned); return true; }
    if (op != kRaidCall) return true;
    bool ok = false;
    const Vec3 at = raiderPos(sim, cmd.player, ok);
    if (!ok) return true;
    int want = int(cmd.param >> 8);
    if (want < 0) {   // the nearest callable point
        Fixed bd{};
        for (size_t i = 0; i < R_.points.size(); ++i) {
            if (!R_.points[i].call || st->points[i].phase != uint8_t(RaidPointPhase::Sleep) || !canUsePoint(sim, cmd.player, int(i))) continue;
            const Fixed d = R_.points[i].follow ? Fixed{} : (sq(at.x - st->points[i].x) + sq(at.z - st->points[i].z));
            if (want < 0 || d < bd) { want = int(i); bd = d; }
        }
    }
    if (want < 0 || size_t(want) >= R_.points.size()) return true;
    const RaidPointDef& d = R_.points[size_t(want)];
    RaidPointState& ps = st->points[size_t(want)];
    if (!d.call || ps.phase != uint8_t(RaidPointPhase::Sleep) || !canUsePoint(sim, cmd.player, want)) return true;
    if (m_->scripts() && ((!d.fn.empty() && !m_->scripts()->allowNamed(sim, d.fn, cmd.player, d.id)) ||
                          !m_->scripts()->allowNamed(sim, "raid_can_extract", cmd.player, d.id))) return true;
    if (d.follow) { ps.x = at.x; ps.z = at.z; }
    const uint32_t now = sim.tick(), rate = sim.tickRate();
    if (d.arrival.raw > 0) setPointPhase(sim, size_t(want), RaidPointPhase::Inbound, now + ticksOf(d.arrival, rate));
    else setPointPhase(sim, size_t(want), RaidPointPhase::Open, d.open.raw > 0 ? now + ticksOf(d.open, rate) : 0);
    ps.caller = cmd.player;
    return true;
}

// ---------------------------------------------------------------- hashing and saves
void Raid::hash(Simulation& sim, Hasher& h) const {
    const RaidStore* st = store(sim);
    if (!st) return;
    h.u32(0x8A1D);
    h.u32(st->phase); h.u32(st->phaseTick); h.u32(st->endTick); h.i32(st->winnerTeam);
    for (auto& p : st->points) { h.u32(p.phase); h.u32(p.until); h.fx(p.fill); h.i32(p.used); h.u32(p.caller); h.fx(p.x); h.fx(p.z); }
    h.u32(uint32_t(st->channels.size()));
    for (auto& c : st->channels) { h.u32(c.player); h.u32(c.point); h.fx(c.progress); }
    h.u32(uint32_t(st->uses.size()));
    for (auto& u : st->uses) { h.u64(u.entity); h.u32(u.user); h.fx(u.progress); h.i32(u.done); h.u32(u.readyAt); }
    for (auto& p : st->players) {
        h.u32(p.slot); h.u32(p.status); h.u32(p.endTick); h.i32(p.point); h.i32(p.kills); h.fx(p.hpSum);
        for (auto& [it, n] : p.secured) { h.u32(it); h.i32(n); }
        h.u32(0xFFFF);
        for (auto& [it, n] : p.lost) { h.u32(it); h.i32(n); }
        for (auto& r : p.carriedRes) h.fx(r);
        for (auto& r : p.securedRes) h.fx(r);
        for (size_t k = 0; k < p.objProgress.size(); ++k) { h.i32(p.objProgress[k]); h.u32(p.objDone[k]); }
    }
    for (auto& r : st->receipts) h.str(r);
}

namespace {
void ioPlayer(Archive& a, RaidPlayer& p) {
    a(p.slot, p.team, p.status, p.endTick, p.point, p.killer, p.kills, p.hadRaider, p.secured, p.lost, p.carriedRes, p.securedRes,
      p.objProgress, p.objDone, p.hpSum, p.hit);
}
} // namespace

void Raid::io(Simulation& sim, Archive& a) {
    RaidStore* st = store(sim);
    if (!st) return;
    a(st->phase, st->phaseTick, st->endTick, st->winnerTeam, st->matchId, st->receipts);
    uint32_t np = a.count(st->points.size(), 4);
    if (!a.writing() && np != st->points.size()) { a.fail(); return; }
    for (auto& p : st->points) a(p.phase, p.until, p.fill, p.used, p.caller, p.x, p.z);
    uint32_t nc = a.count(st->channels.size(), 4);
    if (!a.writing()) st->channels.assign(nc, {});
    for (auto& c : st->channels) a(c.player, c.point, c.progress);
    uint32_t nu = a.count(st->uses.size(), 8);
    if (!a.writing()) st->uses.assign(nu, {});
    for (auto& u : st->uses) a(u.entity, u.user, u.progress, u.done, u.readyAt);
    uint32_t nl = a.count(st->players.size(), 4);
    if (!a.writing() && nl != st->players.size()) { a.fail(); return; }
    for (auto& p : st->players) ioPlayer(a, p);
}

} // namespace aiverse::game
