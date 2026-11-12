// Raid bot (RtsRaid.h): required objectives first, then loot while the clock allows, then the nearest usable
// extraction point (calling it when it waits for a call). It reads the state and submits Commands only.
#include "RtsInternal.h"
#include <algorithm>

namespace aiverse::game {

using namespace detail;

namespace {
Fixed d2(Vec3 a, Fixed x, Fixed z) { return (a.x - x) * (a.x - x) + (a.z - z) * (a.z - z); }
} // namespace

void raidBotThink(Simulation& sim, RtsModule& m, uint8_t player) {
    Raid* raid = m.raid();
    if (!raid || !raid->active()) return;
    RaidStore* st = raid->store(sim);
    RaidPlayer* p = st ? st->find(player) : nullptr;
    World& w = sim.world();
    // a top-down hero ignores move / attack orders: steer its stick instead (kCmdTopDown, RtsTopDown.cpp)
    TopDown* td = m.topDown();
    const Entity myHero = td && td->active() ? td->heroOf(sim, player) : kNullEntity;
    auto steer = [&](Vec3 goal, Fixed stand) {
        Command c;
        if (td->steerCommand(sim, player, goal, stand, c)) sim.submit(c);
    };
    if (!p || p->status != uint8_t(RaidStatus::InRaid) || st->phase != uint8_t(RaidPhase::Running)) {
        if (myHero.valid()) steer(w.get<Transform>(myHero).pos, Fixed::fromInt(1000));   // before the start / between phases: keep alive (kite)
        return;
    }
    const RaidRules& R = raid->rules();
    std::vector<Entity> raiders = raid->raidersOf(sim, player);
    if (raiders.empty()) return;
    const Vec3 at = w.get<Transform>(raiders[0]).pos;
    const bool hero = myHero.valid() && td->isHero(sim, raiders[0]);
    bool have = false;
    Vec3 goal{};
    Fixed stand = Fixed::fromRatio(1, 2);   // close enough
    auto consider = [&](Vec3 pos, Fixed radius) {
        if (!have || d2(at, pos.x, pos.z) < d2(at, goal.x, goal.z)) { goal = pos; stand = radius; have = true; }
    };
    // usable interactables of a type / objective
    auto interactables = [&](auto&& want) {
        w.each<Unit, Transform>([&](Entity e, Unit& u, Transform& t) {
            if (u.hp.raw <= 0 || u.type >= R.interact.size() || !R.interact[u.type].on || !want(u.type)) return;
            const RaidInteractDef& d = R.interact[u.type];
            auto it = std::find_if(st->uses.begin(), st->uses.end(), [&](const RaidUse& x) { return x.entity == e.packed(); });
            if (it != st->uses.end() && ((d.uses > 0 && it->done >= d.uses) || sim.tick() < it->readyAt)) return;
            consider(t.pos, d.range + m.rules().types[u.type].radius - Fixed::fromRatio(1, 4));
        });
    };
    // 1. objectives the extraction needs (or the receipt marks required)
    std::vector<int> needed;
    for (size_t k = 0; k < R.objectives.size(); ++k) {
        bool need = R.objectives[k].required;
        for (auto& pt : R.points) for (auto& q : pt.requires_) need |= q == R.objectives[k].id;
        if (need && !p->objDone[k]) needed.push_back(int(k));
    }
    for (int k : needed) {
        const RaidObjectiveDef& o = R.objectives[size_t(k)];
        if (o.kind == RaidObjKind::Reach) consider({o.x, Fixed{}, o.z}, o.radius - Fixed::fromRatio(1, 2));
        else if (o.kind == RaidObjKind::Interact)
            interactables([&](uint16_t t) { return t == o.type || R.interact[t].objective == o.id; });
        else if (o.kind == RaidObjKind::Collect) {
            for (auto& g : m.state(sim).mech.groundItems) if (g.item == o.item) consider(g.pos, Fixed::fromRatio(1, 2));
            if (!have) interactables([&](uint16_t t) { return R.interact[t].loot >= 0; });
        } else if (o.kind == RaidObjKind::Kill) {
            Entity best = kNullEntity;
            Fixed bd{};
            w.each<Unit, Owner, Transform>([&](Entity e, Unit& u, Owner& ow, Transform& t) {
                if (u.hp.raw <= 0 || !m.hostile(sim, player, ow.player) || (o.type != 0xFFFF && u.type != o.type)) return;
                const Fixed d = d2(at, t.pos.x, t.pos.z);
                if (!best.valid() || d < bd) { best = e; bd = d; }
            });
            if (best.valid() && hero) {   // walk into weapon range; auto-fire does the rest
                steer(w.get<Transform>(best).pos, td->rules().autoRange.raw > 0 ? td->rules().autoRange : Fixed::fromInt(4));
                return;
            }
            if (best.valid()) {
                Command c; c.type = kCmdAttack; c.player = player; c.units = raiders; c.target = best;
                sim.submit(c);
                return;
            }
        }
        if (have) break;
    }
    // 2. loot while there is room and time (more than a third of the clock left)
    const bool time = !st->endTick || (st->endTick - sim.tick()) * 3 > (st->endTick - st->phaseTick);
    bool room = false;
    for (Entity e : raiders) if (auto* bag = w.tryGet<ItemBag>(e)) room |= std::find(bag->items.begin(), bag->items.end(), int16_t(-1)) != bag->items.end();
    if (!have && time && room) {
        for (auto& g : m.state(sim).mech.groundItems) consider(g.pos, Fixed::fromRatio(1, 2));
        interactables([&](uint16_t t) { return R.interact[t].loot >= 0; });
    }
    // 3. extraction
    if (!have) {
        int callable = -1;
        for (size_t i = 0; i < R.points.size(); ++i) {
            if (!raid->canUsePoint(sim, player, int(i))) continue;
            const RaidPointState& ps = st->points[i];
            const auto ph = RaidPointPhase(ps.phase);
            if (ph == RaidPointPhase::Open || ph == RaidPointPhase::Inbound || (ph == RaidPointPhase::Sleep && !R.points[i].call && ps.until) ||
                (ph == RaidPointPhase::Locked && !R.points[i].call))
                consider({ps.x, Fixed{}, ps.z}, R.points[i].radius - Fixed::fromRatio(1, 2));
            else if (ph == RaidPointPhase::Sleep && R.points[i].call && callable < 0) callable = int(i);
        }
        if (!have && callable >= 0) {
            Command c; c.type = kCmdRaid; c.player = player; c.param = kRaidCall | int64_t(callable) << 8;
            sim.submit(c);
            if (!hero) return;   // a hero keeps steering (kiting) while it waits for the call
        }
    }
    if (!have) { if (hero) steer(at, Fixed::fromInt(1000)); return; }   // nothing to do: stand (kiting only)
    if (hero) { steer(goal, fxMax(stand, Fixed::fromRatio(1, 4))); return; }
    if (dist2D(at, {goal.x, at.y, goal.z}) <= fxMax(stand, Fixed::fromRatio(1, 4))) {
        // arrived: stand still (a channel needs the raider inside)
        if (auto* mob = w.tryGet<Mobile>(raiders[0]); mob && mob->moving) { Command c; c.type = kCmdStop; c.player = player; c.units = raiders; sim.submit(c); }
        return;
    }
    if (auto* mob = w.tryGet<Mobile>(raiders[0]); mob && mob->moving && dist2D(mob->goal, {goal.x, mob->goal.y, goal.z}) < kHalf) return;
    Command c; c.type = kCmdMove; c.player = player; c.units = raiders; c.point = {goal.x, Fixed{}, goal.z};
    sim.submit(c);
}

} // namespace aiverse::game
