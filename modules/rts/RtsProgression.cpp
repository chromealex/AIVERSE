// In-match progression on real units: store, system, unit pipeline hooks, commands, bot (RtsProgression.h).
#include "RtsProgression.h"
#include "RtsInternal.h"
#include "RtsScript.h"
#include "libraries/include/aiverselib/genre/PerksData.h"
#include "libraries/include/aiverselib/genre/PerksPersistence.h"
#include "../../engine/core/Archive.h"
#include "../../engine/core/Hash.h"
#include "../../engine/sim/Simulation.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {

using namespace detail;
namespace lg = aiverse::lib::genre;

namespace {
inline int64_t toMilli(Fixed v) { return (v.raw * 1000 + (v.raw >= 0 ? 32768 : -32768)) / 65536; }
inline Fixed fromMilli(int64_t m) { return Fixed::fromRatio(m, 1000); }
inline int64_t allyId(Entity e) { return int64_t(e.packed()) + 1; }
inline Entity allyEntity(int64_t id) { return Entity::fromPacked(uint64_t(id - 1)); }
uint32_t msToTicks(uint32_t ms, uint32_t rate) { return uint32_t((uint64_t(ms) * rate + 999) / 1000); }
// engine stat -> library stat id (0: not mapped)
int64_t libStat(Stat s) {
    switch (s) {
        case Stat::Damage: return lg::perk_stat::OutgoingDamage;
        case Stat::Speed: return lg::perk_stat::MoveSpeed;
        case Stat::AttackSpeed: return lg::perk_stat::FireInterval;
        case Stat::Range: return lg::perk_stat::AttackRange;
        case Stat::MaxHp: return lg::perk_stat::MaxHealth;
        default: return 0;
    }
}
} // namespace

const ProgUnit* ProgressionStore::find(Entity e) const {
    const uint64_t k = e.packed();
    auto it = std::lower_bound(units.begin(), units.end(), k, [](const ProgUnit& u, uint64_t key) { return u.entity < key; });
    return it != units.end() && it->entity == k ? &*it : nullptr;
}

ProgressionStore* Progression::store(Simulation& sim) const { return R_ ? sim.world().resource<ProgressionStore>() : nullptr; }

const lg::PlayerProgress* Progression::progress(Simulation& sim, uint8_t player) const {
    const ProgressionStore* st = store(sim);
    return st ? lg::progressOf(st->perks, player) : nullptr;
}

// ================================================================= setup

lg::BattleDeck Progression::deckFor(const Json& map, uint8_t player) const {
    const ProgressionRules& R = *R_;
    const std::string where = "progression: deck of player " + std::to_string(player);
    auto read = [&](const Json& j) {
        try { lg::BattleDeck d = lg::readBattleDeck(j); d.player = player; return d; }
        catch (const JsonError& e) { throw std::runtime_error(where + ": " + e.what()); }
    };
    // the match start payload (hosted battles put each slot's loadout into the map's player entry)
    if (map["players"].isArray() && player >= 1 && size_t(player) <= map["players"].size()) {
        const Json& pj = map["players"][size_t(player - 1)];
        if (pj.isObject() && pj.has("deck")) return read(pj["deck"]);
    }
    if (R.decks.isObject() && R.decks.has(std::to_string(player))) return read(R.decks[std::to_string(player)]);
    if (R.decks.isArray())
        for (auto& d : R.decks.items()) if (d.getInt("player", 0) == player) return read(d);
    if (!R.defaultDeck.isNull()) return read(R.defaultDeck);
    // no payload: the hero (lowest hero unit id) in slot 0, every other deck unit after it, all recruitable
    lg::BattleDeck d;
    d.player = player;
    d.enforceUnlocks = false;
    for (auto& [id, u] : R.defs.units) if (u.hero) { d.slots.push_back({0, id}); break; }
    for (auto& [id, u] : R.defs.units) {
        if (u.hero || d.slots.size() >= R.defs.deck.maxSlots) continue;
        d.slots.push_back({int32_t(d.slots.size()), id});
    }
    return d;
}

void Progression::setup(Simulation& sim, RtsModule& m, const Json& map) {
    m_ = &m;
    R_ = m.rules().progression.get();
    if (!R_ || !R_->enabled) { R_ = nullptr; return; }
    const uint32_t rate = sim.tickRate();
    // seconds -> ticks, per-second speeds -> per tick
    pickup_ = R_->pickup;
    pickup_.lifetime = msToTicks(R_->pickup.lifetime, rate);
    pickup_.landing = msToTicks(R_->pickup.landing, rate);
    pickup_.attractSpeed = R_->pickup.attractSpeed / int64_t(rate);
    pickup_.attractAccel = R_->pickup.attractAccel / int64_t(rate) / int64_t(rate);
    pickup_.maxSpeed = fxMax(pickup_.attractSpeed, R_->pickup.maxSpeed / int64_t(rate));
    if (auto e = lg::pickupConfigError(pickup_); !e.empty()) throw std::runtime_error("progression." + e);
    summon_ = R_->summon;
    summon_.progressTicks = std::max<uint32_t>(1, msToTicks(R_->summon.progressTicks, rate));
    summon_.cooldownTicks = msToTicks(R_->summon.cooldownTicks, rate);
    summon_.chargeRecharge = msToTicks(R_->summon.chargeRecharge, rate);
    if (auto e = lg::summonConfigError(summon_); !e.empty()) throw std::runtime_error("progression." + e);
    points_ = R_->points;   // the library file counts progressTicks / cooldownTicks in ticks already
    allyLifetime_ = uint32_t((R_->allyLifetimeSeconds * int64_t(rate)).toInt());
    // map summon circles: "progression": {"summonPoints": [...]} in the map adds / replaces points by id
    if (map["progression"].isObject()) {
        for (const auto& [k, v] : map["progression"].members()) {
            (void)v;
            if (k != "summonPoints") throw std::runtime_error("map progression: unknown key '" + k + "' (summonPoints)");
        }
        Json wrap = Json::object();
        wrap.set("summonPoints", map["progression"]["summonPoints"]);
        std::vector<lg::SummonPointDef> extra;
        try { extra = lg::readSummonPoints(wrap); } catch (const JsonError& e) { throw std::runtime_error(std::string("map progression: ") + e.what()); }
        for (auto& p : extra) {
            auto it = std::find_if(points_.begin(), points_.end(), [&](const lg::SummonPointDef& q) { return q.id == p.id; });
            if (it != points_.end()) *it = p; else points_.push_back(p);
        }
        std::sort(points_.begin(), points_.end(), [](const lg::SummonPointDef& a, const lg::SummonPointDef& b) { return a.id < b.id; });
    }
    ProgressionStore& st = sim.world().setResource<ProgressionStore>();
    const size_t slots = sim.players().size() + 1;
    st.magnet.assign(slots, 0);
    st.kills.assign(slots, 0);
    lg::seedPerks(st.perks, sim.config().seed ^ 0x50524f47ull);
    for (const auto& pi : sim.players()) {
        const uint8_t p = pi.slot;
        if (!R_->players.empty() && !std::binary_search(R_->players.begin(), R_->players.end(), p)) continue;
        lg::BattleDeck deck = deckFor(map, p);
        auto err = lg::battleDeckError(R_->defs.units, R_->defs.deck, deck, [&](int64_t id) { return R_->defs.perks.contains(id); });
        if (!err.empty()) throw std::runtime_error("progression: deck of player " + std::to_string(p) + ": " + err);
        if (!lg::addPlayer(R_->defs, st.perks, deck, 0)) throw std::runtime_error("progression: player " + std::to_string(p) + " could not join (too many perks?)");
        lg::addSummoner(summon_, st.summons, p, 0);
    }
    adoptHeroes(sim, st);
}

// ================================================================= unit table

void Progression::registerUnit(Simulation& sim, ProgressionStore& st, Entity e, uint8_t player, int slot, uint8_t bearer) {
    const uint64_t k = e.packed();
    auto it = std::lower_bound(st.units.begin(), st.units.end(), k, [](const ProgUnit& u, uint64_t key) { return u.entity < key; });
    if (it != st.units.end() && it->entity == k) return;
    st.units.insert(it, ProgUnit{k, player, int8_t(slot), bearer});
    // a fresh unit at its type's health gets the perked maximum
    World& w = sim.world();
    if (auto* u = w.tryGet<Unit>(e)) {
        const Fixed base = m_->rules().types[u->type].maxHp;
        if (u->hp == base) u->hp = m_->stat(sim, e, Stat::MaxHp);
    }
}

Entity Progression::heroOf(Simulation& sim, uint8_t player) const {
    const ProgressionStore* st = store(sim);
    if (!st) return kNullEntity;
    World& w = sim.world();
    for (auto& u : st->units)
        if (u.player == player && u.bearer == kProgBearerHero) {
            Entity e = Entity::fromPacked(u.entity);
            if (w.alive(e) && w.has<Unit>(e) && w.get<Unit>(e).hp.raw > 0) return e;
        }
    return kNullEntity;
}

void Progression::adoptHeroes(Simulation& sim, ProgressionStore& st) {
    World& w = sim.world();
    // forget removed entities (ECS ids are reused with a new generation, so a dead handle never matches again)
    std::erase_if(st.units, [&](const ProgUnit& u) { return !w.alive(Entity::fromPacked(u.entity)); });
    TopDown* td = m_->topDown();
    for (auto& [player, p] : st.perks.players) {
        const uint8_t slot = uint8_t(player);
        if (heroOf(sim, slot).valid()) continue;
        Entity hero = td && td->active() ? td->heroOf(sim, slot) : kNullEntity;
        if (!hero.valid() && !p.deck.slots.empty()) {
            auto ut = R_->unitType.find(p.deck.slots[0].unit);
            if (ut != R_->unitType.end())
                w.each<Unit, Owner>([&](Entity e, Unit& u, Owner& o) {
                    if (!hero.valid() && o.player == slot && u.type == ut->second && u.hp.raw > 0 && !st.find(e)) hero = e;
                });
        }
        if (hero.valid()) registerUnit(sim, st, hero, slot, 0, uint8_t(kProgBearerHero));
    }
}

int64_t Progression::slotOfUnit(const lg::PlayerProgress& p, int64_t unit) const {
    for (auto& s : p.deck.slots) if (s.unit == unit) return s.slot;
    return -1;
}

Entity Progression::spawnSlot(Simulation& sim, uint8_t player, int slot, Vec3 at, bool nearHero) {
    ProgressionStore* st = store(sim);
    const lg::PlayerProgress* p = st ? lg::progressOf(st->perks, player) : nullptr;
    const lg::DeckSlot* ds = p ? lg::deckSlot(*p, slot) : nullptr;
    if (!ds) return kNullEntity;
    auto ut = R_->unitType.find(ds->unit);
    if (ut == R_->unitType.end()) return kNullEntity;
    const UnitType& t = m_->rules().types[ut->second];
    if (nearHero) {
        Entity hero = heroOf(sim, player);
        if (hero.valid()) at = m_->freeSpotNear(sim, hero, t.layer);
    }
    Entity e = m_->spawn(sim, ut->second, player, at);
    if (!e.valid()) return e;
    registerUnit(sim, *st, e, player, slot, uint8_t(slot == 0 ? kProgBearerHero : kProgBearerAlly));
    return e;
}

// ================================================================= events

lg::PerkHooks Progression::hooks(Simulation& sim) const {
    lg::PerkHooks h;
    ScriptEngine* se = m_->scripts();
    if (!se || !se->hasFunction("progression_can_offer")) return h;
    const ProgressionRules* R = R_;
    RtsModule* m = m_;
    Simulation* s = &sim;
    h.weight = [R, m, s, se](int64_t player, const lg::OfferItem& item, int64_t weight) -> int64_t {
        std::string arg;
        if (item.type == lg::OfferVariant::Recruit) {
            const ProgressionStore* st = s->world().resource<ProgressionStore>();
            const lg::PlayerProgress* p = st ? lg::progressOf(st->perks, player) : nullptr;
            const lg::DeckSlot* ds = p ? lg::deckSlot(*p, item.slot) : nullptr;
            auto ut = ds ? R->unitType.find(ds->unit) : R->unitType.end();
            arg = "recruit:" + (ut != R->unitType.end() ? m->rules().types[ut->second].id : std::string("?"));
        } else {
            auto d = R->defs.perks.find(item.perk);
            arg = d != R->defs.perks.end() && !d->second.key.empty() ? d->second.key : std::to_string(item.perk);
        }
        return se->allowNamed(*s, "progression_can_offer", uint8_t(player), arg) ? weight : 0;
    };
    return h;
}

void Progression::dispatch(Simulation& sim, uint8_t player, const std::vector<lg::PerkEvent>& ev) {
    World& w = sim.world();
    ProgressionStore& st = *store(sim);
    Entity hero = heroOf(sim, player);
    const Vec3 hp = hero.valid() ? w.get<Transform>(hero).pos : Vec3{};
    for (const auto& e : ev) {
        switch (e.kind) {
        case lg::PerkEventKind::XpGained:
            if (e.value > 0) sim.emit({kEvProgXp, player, hero, kNullEntity, e.value, hp});
            break;
        case lg::PerkEventKind::LevelReady:
            sim.emit({kEvProgLevel, player, hero, kNullEntity, e.value | e.extra << 32, hp});
            break;
        case lg::PerkEventKind::OfferOpened: case lg::PerkEventKind::OfferRerolled: case lg::PerkEventKind::OfferEmpty: {
            const ProgOfferEv k = e.kind == lg::PerkEventKind::OfferOpened ? ProgOfferEv::Opened : e.kind == lg::PerkEventKind::OfferRerolled ? ProgOfferEv::Rerolled : ProgOfferEv::Empty;
            sim.emit({kEvProgOffer, player, hero, kNullEntity, (e.value & 0xFFFFFFFF) | (e.extra & 0xFF) << 32 | int64_t(k) << 40, hp});
            break;
        }
        case lg::PerkEventKind::PerkSelected: case lg::PerkEventKind::PerkReplaced: {
            if (e.kind == lg::PerkEventKind::PerkReplaced) break;   // reported with the selection below
            Entity unit = hero;
            if (e.slot >= 0)
                for (auto& u : st.units)
                    if (u.player == player && u.slot == e.slot && w.alive(Entity::fromPacked(u.entity))) { unit = Entity::fromPacked(u.entity); break; }
            bool replaced = false;
            for (const auto& x : ev) if (x.kind == lg::PerkEventKind::PerkReplaced && x.slot == e.slot) replaced = true;
            sim.emit({kEvProgPerk, player, unit, kNullEntity, e.perk | int64_t(e.slot + 1) << 32 | (replaced ? int64_t(1) << 48 : 0), hp});
            if (!e.hook.empty() && m_->scripts() && m_->scripts()->hasFunction(e.hook)) {
                Json args = Json::object();
                auto d = R_->defs.perks.find(e.perk);
                args.set("perk", Json::string(d != R_->defs.perks.end() ? d->second.key : std::to_string(e.perk)));
                args.set("id", Json::number(e.perk));
                args.set("slot", Json::number(int64_t(e.slot)));
                args.set("level", Json::number(e.extra));
                m_->scripts()->callAction(sim, e.hook, args, player, unit, hp, "progression");
            }
            break;
        }
        case lg::PerkEventKind::Recruit: {
            Entity a = spawnSlot(sim, player, e.slot, hp, true);
            if (a.valid()) {
                lg::AllyRecord rec{allyId(a), player, 0, e.slot, sim.tick(), allyLifetime_ ? sim.tick() + allyLifetime_ : 0, true, true};
                if (const auto* p = lg::progressOf(st.perks, player); p && lg::deckSlot(*p, e.slot)) rec.unit = lg::deckSlot(*p, e.slot)->unit;
                lg::registerAlly(st.summons, rec);
                sim.emit({kEvProgRecruit, player, a, kNullEntity, e.slot, w.get<Transform>(a).pos});
            }
            break;
        }
        default: break;
        }
    }
}

// Max health follows perks: growth keeps the health fraction, shrink clamps (library rescaleHealth).
void Progression::maxHpBefore(Simulation& sim, uint8_t player, std::vector<std::pair<Entity, Fixed>>& out) const {
    World& w = sim.world();
    for (auto& u : store(sim)->units) {
        Entity e = Entity::fromPacked(u.entity);
        if (u.player == player && w.alive(e) && w.has<Unit>(e)) out.push_back({e, m_->stat(sim, e, Stat::MaxHp)});
    }
}
void Progression::maxHpAfter(Simulation& sim, const std::vector<std::pair<Entity, Fixed>>& before) const {
    World& w = sim.world();
    for (auto& [e, oldMax] : before) {
        if (!w.alive(e)) continue;
        Unit& u = w.get<Unit>(e);
        const Fixed newMax = m_->stat(sim, e, Stat::MaxHp);
        if (newMax == oldMax) continue;
        if (u.hp.raw <= 0 || oldMax.raw <= 0 || newMax.raw <= 0) continue;
        // lg::rescaleHealth semantics in Fixed (raw products of large health pools would overflow int64)
        u.hp = newMax > oldMax ? fxMin(u.hp, oldMax) * newMax / oldMax : fxMin(u.hp, newMax);
    }
}

// ================================================================= actions

bool Progression::grantXp(Simulation& sim, uint8_t player, int64_t amount, bool pickup) {
    ProgressionStore* st = store(sim);
    if (!st) return false;
    auto ev = lg::grantXp(R_->defs, R_->perk, st->perks, player, amount, pickup);
    if (!ev) return false;
    dispatch(sim, player, *ev);
    return true;
}

bool Progression::grantPerk(Simulation& sim, uint8_t player, int64_t perk, int slot) {
    ProgressionStore* st = store(sim);
    if (!st) return false;
    std::vector<std::pair<Entity, Fixed>> before;
    maxHpBefore(sim, player, before);
    auto ev = lg::grantPerk(R_->defs, st->perks, player, perk, slot, sim.tick());
    if (!ev) return false;
    maxHpAfter(sim, before);
    dispatch(sim, player, {*ev});
    return true;
}

bool Progression::choose(Simulation& sim, uint8_t player, uint32_t revision, int index) {
    ProgressionStore* st = store(sim);
    if (!st || index < 0) return false;
    std::vector<std::pair<Entity, Fixed>> before;
    maxHpBefore(sim, player, before);
    auto ev = lg::selectOffer(R_->defs, R_->perk, st->perks, player, revision, size_t(index), sim.tick(), hooks(sim));
    if (!ev) return false;
    maxHpAfter(sim, before);
    dispatch(sim, player, *ev);
    return true;
}

bool Progression::reroll(Simulation& sim, uint8_t player, uint32_t revision) {
    ProgressionStore* st = store(sim);
    if (!st) return false;
    auto ev = lg::rerollOffer(R_->defs, R_->perk, st->perks, player, revision, hooks(sim));
    if (!ev) return false;
    dispatch(sim, player, *ev);
    return true;
}

bool Progression::open(Simulation& sim, uint8_t player) {
    ProgressionStore* st = store(sim);
    if (!st) return false;
    auto ev = lg::openOffer(R_->defs, R_->perk, st->perks, player, hooks(sim));
    if (!ev) return false;
    dispatch(sim, player, *ev);
    return true;
}

bool Progression::close(Simulation& sim, uint8_t player) {
    ProgressionStore* st = store(sim);
    if (!st || !lg::closeOffer(st->perks, player)) return false;
    const auto* p = lg::progressOf(st->perks, player);
    sim.emit({kEvProgOffer, player, heroOf(sim, player), kNullEntity, int64_t(p->offer.revision) | int64_t(ProgOfferEv::Closed) << 40, {}});
    return true;
}

bool Progression::command(Simulation& sim, const Command& cmd) {
    if (cmd.type != kCmdProgression) return false;
    if (!active() || !store(sim)) return true;
    const int64_t op = cmd.param & 0xFF;
    const int index = int((cmd.param >> 8) & 0xFF);
    const uint32_t revision = uint32_t(cmd.param >> 16);
    switch (op) {
        case kProgChoose: choose(sim, cmd.player, revision, index); break;
        case kProgReroll: reroll(sim, cmd.player, revision); break;
        case kProgClose: close(sim, cmd.player); break;
        case kProgOpen: if (!R_->offerAtCircles || atCircle(sim, cmd.player)) open(sim, cmd.player); break;
        default: break;   // invalid commands are ignored silently
    }
    return true;
}

bool Progression::botCommand(Simulation& sim, uint8_t player, Command& out) const {
    const ProgressionStore* st = store(sim);
    const lg::PlayerProgress* p = st ? lg::progressOf(st->perks, player) : nullptr;
    if (!p) return false;
    if (!p->offer.open) {
        if (R_->autoOpen || !lg::availableLevels(R_->defs, R_->perk, *p)) return false;
        out = Command{}; out.type = kCmdProgression; out.param = progressionParam(kProgOpen, 0, 0);
        return true;
    }
    bool low = false;
    if (Entity h = heroOf(sim, player); h.valid()) {
        const Fixed max = m_->stat(sim, h, Stat::MaxHp);
        low = max.raw > 0 && sim.world().get<Unit>(h).hp * 100 < max * 35;
    }
    auto pick = lg::botOfferChoice(R_->defs, R_->perk, *p, low);
    if (!pick) return false;
    out = Command{};
    out.type = kCmdProgression;
    out.param = progressionParam(kProgChoose, int(*pick), p->offer.revision);
    return true;
}

// ================================================================= the system

void Progression::collectDeaths(Simulation& sim, ProgressionStore& st, std::vector<int64_t>& alliesDied) {
    World& w = sim.world();
    const Rules& rules = m_->rules();
    std::vector<lg::OrbSpawn> spawns;
    std::vector<std::pair<uint8_t, int64_t>> direct;
    w.each<Unit, Owner, Transform>([&](Entity e, Unit& u, Owner& o, Transform& t) {
        if (u.hp.raw > 0) return;
        const UnitType& ut = rules.types[u.type];
        if ((ut.invulnerable && ut.resourceNode < 0) || (u.flags & kUnitSilent)) return;   // sysDeath restores / removes them silently
        if (const ProgUnit* pu = st.find(e); pu && pu->bearer == kProgBearerAlly) alliesDied.push_back(allyId(e));
        const uint8_t killer = u.lastAttacker;
        if (killer == 0 || killer == kHostileNeutral || !m_->hostile(sim, killer, o.player)) return;
        if (killer < st.kills.size() && lg::progressOf(st.perks, killer)) ++st.kills[killer];
        const int64_t xp = u.type < R_->killXp.size() ? R_->killXp[u.type] : 0;
        if (xp <= 0) return;
        if (!R_->orbs) { if (lg::progressOf(st.perks, killer)) direct.push_back({killer, xp}); return; }
        if (!R_->orbsForKiller || lg::progressOf(st.perks, killer)) spawns.push_back({R_->orbsForKiller ? int64_t(killer) : 0, xp, flat(t.pos)});
    });
    for (auto& s : spawns) st.orbQueue.push_back(s);
    for (auto& [p, xp] : direct) grantXp(sim, p, xp, false);
}

void Progression::stepPickups(Simulation& sim, ProgressionStore& st) {
    World& w = sim.world();
    lg::PickupInput in;
    in.spawns = std::move(st.orbQueue);
    st.orbQueue.clear();
    for (auto& [player, p] : st.perks.players) {
        (void)p;
        Entity h = heroOf(sim, uint8_t(player));
        if (!h.valid() || (w.get<Unit>(h).flags & kUnitOffMap)) continue;
        lg::XpCollector c;
        c.player = player;
        c.position = flat(w.get<Transform>(h).pos);
        c.radius = fromMilli(statOf(sim, h, lg::perk_stat::PickupRadius, toMilli(pickup_.collectRadius)));
        if (c.radius.raw < 0) c.radius = Fixed{};
        c.magnet = size_t(player) < st.magnet.size() && st.magnet[size_t(player)];
        in.collectors.push_back(c);
    }
    for (auto& m : st.magnet) m = 0;
    if (in.spawns.empty() && st.pickups.orbs.empty()) return;
    auto rep = lg::stepPickups(pickup_, st.pickups, sim.tick() + 1, in);
    if (!rep) return;   // a malformed input (impossible from here) leaves the state untouched
    for (auto& e : rep->events) {
        ProgOrbEv k;
        switch (e.kind) {
            case lg::PickupEventKind::Spawned: k = ProgOrbEv::Spawned; break;
            case lg::PickupEventKind::Collected: k = ProgOrbEv::Collected; break;
            case lg::PickupEventKind::Expired: case lg::PickupEventKind::Evicted: k = ProgOrbEv::Expired; break;
            case lg::PickupEventKind::Merged: k = ProgOrbEv::Merged; break;
            default: continue;
        }
        Vec3 at{};
        for (auto& o : st.pickups.orbs) if (o.id == e.orb) at = o.position;
        if (k == ProgOrbEv::Collected) if (Entity h = heroOf(sim, uint8_t(e.player)); h.valid()) at = w.get<Transform>(h).pos;
        sim.emit({kEvProgOrb, uint8_t(k == ProgOrbEv::Collected ? e.player : 0), kNullEntity, kNullEntity, e.value | int64_t(k) << 40, at});
    }
    for (auto& [player, xp] : rep->collected) grantXp(sim, uint8_t(player), xp, true);
}

void Progression::stepSummons(Simulation& sim, ProgressionStore& st, const std::vector<int64_t>& alliesDied) {
    World& w = sim.world();
    if (points_.empty() && st.summons.allies.empty() && !summon_.chargeRecharge) return;
    lg::SummonInput in;
    in.died = alliesDied;
    for (auto& [id, a] : st.summons.allies) if (a.alive && !w.alive(allyEntity(id))) in.removed.push_back(id);   // removed without dying
    for (auto& [player, p] : st.perks.players) {
        (void)p;
        Entity h = heroOf(sim, uint8_t(player));
        if (!h.valid()) continue;
        in.players.push_back({player, int64_t(m_->teamOf(sim, uint8_t(player))), flat(w.get<Transform>(h).pos), true});
    }
    auto rep = lg::stepSummons(summon_, points_, st.summons, sim.tick() + 1, in);
    if (!rep) return;
    auto centre = [&](int64_t point) { for (auto& d : points_) if (d.id == point) return d.position; return Vec3{}; };
    for (auto& e : rep->events) {
        if (e.kind == lg::SummonEventKind::Confirmed) continue;
        sim.emit({kEvProgSummon, uint8_t(e.player), e.ally ? allyEntity(e.ally) : kNullEntity, kNullEntity, e.point | int64_t(e.kind) << 32, centre(e.point)});
    }
    for (int64_t id : rep->despawn) {
        Entity e = allyEntity(id);
        if (!w.alive(e) || !w.has<Unit>(e)) continue;
        Unit& u = w.get<Unit>(e);
        u.hp = Fixed{};
        u.flags |= kUnitSilent;   // the lifetime ran out: removed without death effects
    }
    for (auto& q : rep->requests) {
        int slot = q.slot;
        if (q.recruit) {   // the lowest recruitable slot of the deck
            slot = -1;
            if (const auto* p = lg::progressOf(st.perks, q.player))
                for (auto& s : p->deck.slots) if (lg::canRecruit(R_->defs, R_->perk, *p, s.slot)) { slot = s.slot; break; }
        }
        Entity a = slot >= 0 ? spawnSlot(sim, uint8_t(q.player), slot, centre(q.point), false) : kNullEntity;
        if (!a.valid()) { lg::cancelSummon(summon_, st.summons, q); continue; }
        int64_t unit = 0;
        if (auto* p = lg::progressOf(st.perks, q.player)) {
            if (const auto* ds = lg::deckSlot(*p, slot)) unit = ds->unit;
            if (q.recruit) ++p->recruits[slot];   // slot perks reach the new squad member
        }
        lg::confirmSummon(summon_, points_, st.summons, q, allyId(a), unit, slot, sim.tick() + 1, allyLifetime_, true);
        sim.emit({kEvProgSummon, uint8_t(q.player), a, kNullEntity, q.point | int64_t(lg::SummonEventKind::Confirmed) << 32, centre(q.point)});
        sim.emit({kEvProgRecruit, uint8_t(q.player), a, kNullEntity, slot, w.get<Transform>(a).pos});
    }
}

void Progression::stepUnits(Simulation& sim, ProgressionStore& st) {
    World& w = sim.world();
    const Fixed dt = sim.dt();
    for (auto& pu : st.units) {
        Entity e = Entity::fromPacked(pu.entity);
        if (!w.alive(e) || !w.has<Unit>(e)) continue;
        Unit& u = w.get<Unit>(e);
        if (u.hp.raw <= 0) continue;
        // regeneration: healthRegen (milli hp per second)
        const int64_t regen = statOf(sim, e, lg::perk_stat::HealthRegen, 0);
        if (regen > 0) {
            const Fixed max = m_->stat(sim, e, Stat::MaxHp);
            if (u.hp < max) u.hp = fxMin(max, u.hp + fromMilli(regen) * dt);
        }
        // squad: idle allies walk back to their hero
        if (pu.bearer != kProgBearerAlly || R_->allyFollow.raw <= 0 || (sim.tick() + e.index) % 10 != 0) continue;
        Entity hero = heroOf(sim, pu.player);
        if (!hero.valid()) continue;
        auto* ord = w.tryGet<Orders>(e);
        auto* mob = w.tryGet<Mobile>(e);
        auto* cb = w.tryGet<Combat>(e);
        if (!mob || !ord || !ord->queue.empty() || (cb && w.alive(cb->target))) continue;
        const Vec3 hp = w.get<Transform>(hero).pos;
        if (dist2D(w.get<Transform>(e).pos, hp) > R_->allyFollow) m_->moveTo(sim, e, hp, R_->allyFollow / 2);
    }
}

bool Progression::atCircle(Simulation& sim, uint8_t player) const {
    Entity h = heroOf(sim, player);
    if (!h.valid()) return false;
    const Vec3 at = sim.world().get<Transform>(h).pos;
    const int64_t team = m_->teamOf(sim, player);
    for (auto& d : points_) {
        if (d.team && d.team != team) continue;
        const Fixed dx = at.x - d.position.x, dz = at.z - d.position.z;
        if (dx * dx + dz * dz <= d.radius * d.radius) return true;
    }
    return false;
}

void Progression::openReady(Simulation& sim, ProgressionStore& st) {
    if (!R_->autoOpen) return;
    std::vector<uint8_t> ready, leave;
    for (auto& [player, p] : st.perks.players) {
        const bool here = !R_->offerAtCircles || atCircle(sim, uint8_t(player));
        if (p.offer.open && !here) leave.push_back(uint8_t(player));   // stations: walking away closes the cards, xp stays banked
        else if (!p.offer.open && here && lg::availableLevels(R_->defs, R_->perk, p)) ready.push_back(uint8_t(player));
    }
    for (uint8_t p : leave) close(sim, p);
    for (uint8_t p : ready) open(sim, p);
}

void Progression::tick(Simulation& sim) {
    ProgressionStore* st = store(sim);
    if (!st) return;
    adoptHeroes(sim, *st);
    std::vector<int64_t> alliesDied;
    collectDeaths(sim, *st, alliesDied);
    stepPickups(sim, *st);
    stepSummons(sim, *st, alliesDied);
    stepUnits(sim, *st);
    openReady(sim, *st);
}

// ================================================================= unit pipeline hooks

int64_t Progression::statOf(Simulation& sim, Entity e, int64_t stat, int64_t base) const {
    const ProgressionStore* st = store(sim);
    const ProgUnit* pu = st ? st->find(e) : nullptr;
    const lg::PlayerProgress* p = pu ? lg::progressOf(st->perks, pu->player) : nullptr;
    if (!p) return base;
    return lg::evaluateStat(R_->defs, *p, stat, base, pu->slot, pu->bearer);
}

void Progression::modStat(Simulation& sim, Entity e, Stat s, Fixed& value) const {
    const int64_t id = libStat(s);
    if (!id) return;
    const ProgressionStore* st = store(sim);
    const ProgUnit* pu = st ? st->find(e) : nullptr;
    const lg::PlayerProgress* p = pu ? lg::progressOf(st->perks, pu->player) : nullptr;
    if (!p || p->mods.empty()) return;
    const UnitType& t = m_->rules().types[sim.world().get<Unit>(e).type];
    std::vector<lg::StatStep> steps;   // no effect on this stat: the engine value stays exact (no milli rounding)
    auto eval = [&](int64_t base) { steps.clear(); return lg::evaluateStat(R_->defs, *p, id, base, pu->slot, pu->bearer, &steps); };
    switch (s) {
        case Stat::Range: {   // a bonus on top of the weapon range: the pipeline sees the whole range
            const Fixed wr = maxWeaponRange(t);
            const int64_t r = eval(toMilli(wr + value));
            if (!steps.empty()) value = fromMilli(r) - wr;
            break;
        }
        case Stat::AttackSpeed: {   // fireInterval in ms of the first weapon: interval = cooldown / attack speed
            if (t.weapons.empty() || value.raw <= 0 || t.weapons[0].cooldown.raw <= 0) return;
            const Fixed cd = t.weapons[0].cooldown;
            const int64_t after = eval(toMilli(cd / value));
            if (!steps.empty()) value = after <= 0 ? Fixed::fromInt(1000) : fxMin(Fixed::fromInt(1000), cd * 1000 / after);
            break;
        }
        default: {
            const int64_t r = eval(toMilli(value));
            if (!steps.empty()) value = fromMilli(r);
            break;
        }
    }
}

Fixed Progression::damage(Simulation& sim, Entity target, Entity source, Fixed amount) const {
    const ProgressionStore* st = store(sim);
    if (!st || amount.raw <= 0) return amount;
    if (st->find(source)) {   // crits of the attacker
        const int64_t chance = statOf(sim, source, lg::perk_stat::CritChance, 0);
        if (chance > 0 && int64_t(sim.rng().below(1000)) < chance)
            amount = amount * statOf(sim, source, lg::perk_stat::CritDamage, R_->critDamageMilli) / 1000;
    }
    if (st->find(target)) {
        const int64_t mul = statOf(sim, target, lg::perk_stat::IncomingDamage, 1000);
        if (mul != 1000) amount = mul <= 0 ? Fixed{} : amount * mul / 1000;
    }
    return amount;
}

int16_t Progression::hitPayload(Simulation& sim, Entity source) const {
    const ProgressionStore* st = store(sim);
    const ProgUnit* pu = st ? st->find(source) : nullptr;
    const lg::PlayerProgress* p = pu ? lg::progressOf(st->perks, pu->player) : nullptr;
    if (!p) return -1;
    // the first owned perk (occurrence order) with an on-hit payload that reaches this unit
    for (auto& o : p->mods) {
        if (!lg::appliesTo(o, pu->slot)) continue;
        auto x = R_->ext.find(o.perk);
        if (x == R_->ext.end() || x->second.onHit < 0) continue;
        const auto& def = R_->defs.perks.at(o.perk);
        if (def.bearer && def.bearer != pu->bearer) continue;
        if (x->second.onHitChance < Fixed::fromInt(1) && !(sim.rng().unit() < x->second.onHitChance)) return -1;
        return x->second.onHit;
    }
    return -1;
}

const projectile::ProjectileFlight* Progression::variantFlight(int weapon) const {
    const int v = variantIndex(weapon);
    return R_ && v >= 0 && size_t(v) < R_->variants.size() ? &R_->variants[size_t(v)].flight : nullptr;
}

void Progression::beforeShot(Simulation& sim, Projectile& pr) const {
    const ProgressionStore* st = store(sim);
    const ProgUnit* pu = st ? st->find(pr.source) : nullptr;
    const lg::PlayerProgress* p = pu ? lg::progressOf(st->perks, pu->player) : nullptr;
    if (!p || p->mods.empty() || pr.weapon < 0) return;
    std::vector<lg::StatStep> steps;
    const int64_t splash = lg::evaluateStat(R_->defs, *p, lg::perk_stat::SplashRadius, toMilli(pr.splash), pu->slot, pu->bearer, &steps);
    if (!steps.empty()) pr.splash = splash <= 0 ? Fixed{} : fromMilli(splash);
    if (pr.onHit < 0) pr.onHit = hitPayload(sim, pr.source);
    uint32_t mask = 0;
    for (size_t k = 0; k < R_->bulletPerks.size(); ++k) {
        const auto& def = R_->defs.perks.at(R_->bulletPerks[k]);
        if (def.bearer && def.bearer != pu->bearer) continue;
        for (auto& o : p->mods) if (o.perk == R_->bulletPerks[k] && lg::appliesTo(o, pu->slot)) { mask |= 1u << k; break; }
    }
    if (!mask) return;
    const int v = R_->variantFor(pr.sourceType, pr.weapon, mask);
    if (v >= 0) pr.weapon = int8_t(-2 - v);
}

void Progression::afterShot(Simulation& sim, Projectile& pr) const {
    const int v = variantIndex(pr.weapon);
    if (v < 0 || size_t(v) >= R_->variants.size() || !pr.program.enabled) return;
    const ProgFlightVariant& fv = R_->variants[size_t(v)];
    const ProgressionStore* st = store(sim);
    const ProgUnit* pu = st ? st->find(pr.source) : nullptr;
    const lg::PlayerProgress* p = pu ? lg::progressOf(st->perks, pu->player) : nullptr;
    if (!p || pr.program.remaining.size() != fv.flight.modifiers.size()) return;
    // ricochet / fly-through counts grow with the number of times the perk was taken
    for (size_t i = 0; i < fv.perkOf.size(); ++i) {
        if (fv.perkOf[i] < 0) continue;
        const auto& m = fv.flight.modifiers[i];
        if (m.kind != projectile::ProjectileModKind::Ricochet && m.kind != projectile::ProjectileModKind::FlyThrough) continue;
        const int64_t perk = R_->bulletPerks[size_t(fv.perkOf[i])];
        uint32_t n = 0;
        for (auto& o : p->mods) if (o.perk == perk && lg::appliesTo(o, pu->slot)) ++n;
        pr.program.remaining[i] = uint16_t(std::min<uint32_t>(m.count, uint32_t(fv.baseCount[i]) * std::max<uint32_t>(1, n)));
    }
}

// ================================================================= hash / save

void Progression::hash(Simulation& sim, Hasher& h) const {
    const ProgressionStore* st = store(sim);
    if (!st) return;
    h.u64(R_->defsHash); h.u64(R_->configHash);
    h.u64(lg::progressionHash(st->perks));
    h.u64(lg::progressionHash(st->pickups));
    h.u64(lg::progressionHash(st->summons));
    h.u32(uint32_t(st->units.size()));
    for (auto& u : st->units) { h.u64(u.entity); h.u32(u.player); h.i32(u.slot); h.u32(u.bearer); }
    for (auto m : st->magnet) h.u32(m);
    for (auto k : st->kills) h.i64(k);
    h.u32(uint32_t(st->orbQueue.size()));
    for (auto& o : st->orbQueue) { h.i64(o.owner); h.i64(o.value); h.v3(o.position); }
}

void io(Archive& a, ProgUnit& u) { a(u.entity, u.player, u.slot, u.bearer); }

void Progression::io(Simulation& sim, Archive& a) {
    ProgressionStore* st = store(sim);
    if (!st) { a.fail(); return; }
    a(st->perks, st->pickups, st->summons, st->units, st->magnet, st->kills);
    uint32_t n = uint32_t(st->orbQueue.size());
    a(n);
    if (!a.writing()) { if (n > lg::kPickupMaxSpawns) { a.fail(); return; } st->orbQueue.resize(n); }
    for (auto& o : st->orbQueue) a(o.owner, o.value, o.position);
    if (!a.writing()) {
        const size_t slots = sim.players().size() + 1;
        bool sorted = std::is_sorted(st->units.begin(), st->units.end(), [](const ProgUnit& x, const ProgUnit& y) { return x.entity < y.entity; });
        if (!lg::valid(st->perks) || !lg::valid(st->pickups) || !lg::valid(st->summons) || !sorted ||
            st->magnet.size() != slots || st->kills.size() != slots) a.fail();
    }
}

} // namespace aiverse::game
