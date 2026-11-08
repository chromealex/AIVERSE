// World tree: generic reading of the RTS module's ECS for the editor (see WorldInspect.h).
#include "WorldInspect.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <set>
#include "SimTools.h"

namespace rts::editor {

using namespace rts::game;

namespace {

// ---------------------------------------------------------------- JSON builder for component fields
struct J {
    World& w;
    const Rules& R;
    Json o = Json::object();

    static Json num(Fixed v) { return Json::number(v.toDouble(), 3); }
    static Json vec(Vec3 v) {
        Json a = Json::array();
        a.push(num(v.x)); a.push(num(v.y)); a.push(num(v.z));
        return a;
    }
    std::string typeName(int t) const { return t >= 0 && size_t(t) < R.types.size() ? R.types[size_t(t)].id : std::string(); }
    Json ref(Entity e) const {
        if (!e.valid()) return Json();
        Json r = Json::object();
        r.set("$entity", Json::number(int64_t(worldRef(e))));
        std::string label = "#" + std::to_string(e.index);
        if (auto* u = w.tryGet<Unit>(e)) label = typeName(u->type) + " " + label;
        else if (!w.alive(e)) label += " (нет)";
        r.set("label", Json::string(label));
        return r;
    }
    J& f(const char* k, Fixed v) { o.set(k, num(v)); return *this; }
    J& f(const char* k, Vec3 v) { o.set(k, vec(v)); return *this; }
    J& f(const char* k, Entity e) { o.set(k, ref(e)); return *this; }
    J& f(const char* k, bool v) { o.set(k, Json::fromBool(v)); return *this; }
    J& f(const char* k, const std::string& v) { o.set(k, Json::string(v)); return *this; }
    template <class T> requires std::is_integral_v<T> J& f(const char* k, T v) { o.set(k, Json::number(int64_t(v))); return *this; }
    J& type(const char* k, int t) { o.set(k, t < 0 || t == kNoType ? Json() : Json::string(typeName(t))); return *this; }
    J& res(const char* k, int r) {
        o.set(k, r >= 0 && size_t(r) < R.resources.size() ? Json::string(R.resources[size_t(r)].id) : Json());
        return *this;
    }
    J& ticks(const char* k, uint32_t t) { o.set(k, Json::number(int64_t(t))); return *this; }
    template <class T> J& list(const char* k, const std::vector<T>& v) {
        Json a = Json::array();
        for (auto& x : v) {
            if constexpr (std::is_same_v<T, Fixed>) a.push(num(x));
            else if constexpr (std::is_same_v<T, Vec3>) a.push(vec(x));
            else if constexpr (std::is_same_v<T, Entity>) a.push(ref(x));
            else a.push(Json::number(int64_t(x)));
        }
        o.set(k, a);
        return *this;
    }
    J& resList(const char* k, const std::vector<Fixed>& v) {   // per resource
        Json m = Json::object();
        for (size_t i = 0; i < v.size(); ++i) m.set(i < R.resources.size() ? R.resources[i].id : std::to_string(i), num(v[i]));
        o.set(k, m);
        return *this;
    }
};

template <class T> bool hasC(const World& w, Entity e) { return w.has<T>(e); }
template <class T> void touchC(World& w) { (void)w.pool<T>(); }
using DumpFn = void (*)(J&, Entity);
struct Desc { WorldComponent info; bool (*has)(const World&, Entity); void (*touch)(World&); DumpFn dump; };

const char* stanceName(uint8_t m) {
    static const char* n[] = {"aggressive", "defensive", "hold", "passive"};
    return m < 4 ? n[m] : "?";
}

std::string statusName(const Rules& R, int d) { return d >= 0 && size_t(d) < R.statuses.size() ? R.statuses[size_t(d)].id : std::to_string(d); }

Json orderJson(J& j, const Order& o) {
    Json r = Json::object();
    r.set("type", Json::string(orderName(o.type)));
    if (o.target.valid()) r.set("target", j.ref(o.target));
    r.set("point", J::vec(o.point));
    if (o.point2.x.raw || o.point2.z.raw) r.set("point2", J::vec(o.point2));
    if (o.param) r.set("param", Json::number(int64_t(o.param)));
    r.set("state", Json::number(int64_t(o.state)));
    if (o.flow) r.set("flow", Json::fromBool(true));
    return r;
}

#define RTS_C(T, TITLE, ...) Desc{{#T, TITLE}, &hasC<T>, &touchC<T>, [](J& j, Entity e) { auto& x = *j.w.tryGet<T>(e); (void)x; __VA_ARGS__; }}

const std::vector<Desc>& descs() {
    static const std::vector<Desc> d = {
        RTS_C(Transform, "Положение", j.f("pos", x.pos).f("yaw", x.yaw)),
        RTS_C(Owner, "Владелец", j.f("player", x.player)),
        RTS_C(Unit, "Объект", {
            j.type("type", x.type).f("hp", x.hp).f("energy", x.energy).f("facing", x.facing).f("lastAttacker", x.lastAttacker).f("lastHitBy", x.lastHitBy);
            Json fl = Json::array();
            static const char* names[] = {"hold", "underConstruction", "disabled", "silent", "inHand", "possessed", "inTransit", "inCargo", "infected"};
            for (int b = 0; b < 9; ++b) if (x.flags & (1u << b)) fl.push(Json::string(names[b]));
            j.o.set("flags", fl);
        }),
        RTS_C(Orders, "Приказы", {
            Json a = Json::array();
            for (auto& o : x.queue) a.push(orderJson(j, o));
            j.o.set("queue", a);
        }),
        RTS_C(Mobile, "Движение", j.f("moving", x.moving).f("goal", x.goal).f("goalRadius", x.goalRadius).f("pathLength", x.path.size()).f("pathPos", x.pathPos)
                                     .f("velocity", x.vel).f("stuck", x.stuck).f("flow", x.flow).f("yielded", x.yielded)),
        RTS_C(Combat, "Бой", j.f("target", x.target).f("autoTarget", x.autoTarget).list("cooldown", std::vector<Fixed>(x.cooldown, x.cooldown + 4))
                                 .f("chargeTarget", x.chargeTarget).f("slot", int(x.slot))),
        RTS_C(Cargo, "Груз", j.res("resource", x.res).f("amount", x.amount).f("lastNode", x.lastNode).f("field", x.field)),
        RTS_C(Production, "Производство", {
            Json q = Json::array();
            for (auto& it : x.queue) {
                Json r = Json::object();
                if (it.item >= 0) r.set("type", Json::string(j.typeName(it.item)));
                else r.set("tech", Json::string(size_t(~it.item) < j.R.techs.size() ? j.R.techs[size_t(~it.item)].id : std::to_string(~it.item)));
                r.set("progress", J::num(it.progress));
                r.set("paid", J::num(it.paid));
                if (it.upgrade) r.set("upgrade", Json::fromBool(true));
                q.push(r);
            }
            j.o.set("queue", q);
            j.f("hasRally", x.hasRally);
            if (x.hasRally) j.f("rally", x.rally);
            j.f("rallyTarget", x.rallyTarget).type("ready", x.ready).f("charges", x.charges);
        }),
        RTS_C(Construction, "Стройка", j.f("progress", x.progress).f("paid", x.paid).f("builder", x.builder).f("selfBuild", x.selfBuild)),
        RTS_C(ResourceStock, "Запас ресурса", j.res("resource", x.res).f("amount", x.amount).f("infinite", x.infinite)),
        RTS_C(Extractor, "Добытчик", j.type("nodeType", x.nodeType).f("remaining", x.remaining)),
        RTS_C(Footprint, "Площадь", j.f("x", x.x).f("y", x.y).f("w", x.w).f("h", x.h)),
        RTS_C(Garrison, "Гарнизон", j.f("count", x.count).f("growth", x.growth).f("pendingOut", x.pendingOut).f("pendingTarget", x.pendingTarget).f("lost", x.lost)),
        RTS_C(Projectile, "Снаряд", j.f("source", x.source).f("target", x.target).f("aim", x.aim).f("speed", x.speed).f("damage", x.damage).f("splash", x.splash)
                                        .f("owner", x.owner).type("sourceType", x.sourceType).f("travel", x.travel).f("total", x.total)),
        RTS_C(Hero, "Герой", j.f("level", x.level).f("xp", x.xp).f("points", x.points).list("skills", std::vector<int>(x.skill, x.skill + 8))),
        RTS_C(Transit, "Перелёт", j.f("from", x.from).f("to", x.to).ticks("depart", x.depart).ticks("arrive", x.arrive).f("arrival", x.arrival)),
        RTS_C(Squad, "Отряд", j.f("reinforcing", x.reinforcing).f("reinforceTimer", x.reinforceTimer)),
        RTS_C(Buffs, "Баффы", {
            Json a = Json::array();
            for (auto& b : x.list) {
                Json r = Json::object();
                r.set("stat", Json::number(int64_t(b.stat)));
                r.set("add", J::num(b.add)); r.set("mul", J::num(b.mul));
                r.set("until", Json::number(int64_t(b.until)));
                if (b.sourceType != kNoType) r.set("source", Json::string(j.typeName(b.sourceType)));
                a.push(r);
            }
            j.o.set("list", a);
            j.ticks("stunUntil", x.stunUntil);
        }),
        RTS_C(AbilityState, "Способности", j.list("readyAt", std::vector<uint32_t>(x.readyAt, x.readyAt + 8)).list("charges", std::vector<int>(x.charges, x.charges + 8))
                                               .ticks("busyUntil", x.busyUntil)),
        RTS_C(Statuses, "Статусы", {
            Json a = Json::array();
            for (auto& s : x.list) {
                Json r = Json::object();
                r.set("status", Json::string(statusName(j.R, s.def)));
                r.set("owner", Json::number(int64_t(s.owner)));
                if (s.source.valid()) r.set("source", j.ref(s.source));
                r.set("until", Json::number(int64_t(s.until)));
                r.set("stacks", Json::number(int64_t(s.stacks)));
                if (s.shield.raw) r.set("shield", J::num(s.shield));
                a.push(r);
            }
            j.o.set("list", a);
            j.f("flags", x.flags);
        }),
        RTS_C(Area, "Зона эффекта", j.f("caster", x.caster).type("sourceType", x.sourceType).f("radius", x.radius).ticks("fireAt", x.fireAt).ticks("endAt", x.endAt)
                                        .f("triggers", x.triggers).f("follow", x.follow).f("dest", x.dest).f("lane", x.lane)),
        RTS_C(Channel, "Поддержание", j.f("target", x.target).f("point", x.point).f("ability", x.ability).ticks("nextAt", x.nextAt).ticks("endAt", x.endAt)),
        RTS_C(GroupTag, "Группа", j.f("id", x.id).f("from", x.from).f("to", x.to)),
        RTS_C(Capture, "Захват", j.f("progress", x.progress).f("team", x.team)),
        RTS_C(Guard, "Охрана", j.f("home", x.home).f("leash", x.leash).f("returning", x.returning)),
        RTS_C(Summon, "Призванный", j.ticks("expire", x.expire).type("summonerType", x.summonerType).f("summoner", x.summoner)),
        RTS_C(Needs, "Потребности", j.f("hunger", x.hunger).f("sleep", x.sleep).f("happiness", x.happiness).f("leaving", x.leaving)),
        RTS_C(Possessed, "Под управлением", j.f("dir", x.dir).f("target", x.target).f("fire", x.fire)),
        RTS_C(PortalState, "Портал", j.f("timer", x.timer)),
        RTS_C(AutoTrainState, "Автонайм", j.f("timer", x.timer)),
        RTS_C(ClaimState, "Захват клеток", j.f("acc", x.acc)),
        RTS_C(Advancer, "Наступает", (void)0),
        RTS_C(Inventory, "Склад", j.resList("have", x.have).resList("incoming", x.incoming).resList("reserved", x.reserved)),
        RTS_C(Workshop, "Мастерская", j.f("progress", x.progress).f("running", x.running)),
        RTS_C(Carrier, "Носильщик", j.f("from", x.from).f("to", x.to).res("resource", x.res).f("amount", x.amount).f("loaded", x.loaded)),
        RTS_C(LaneWalker, "Идёт по линии", j.f("lane", x.lane).f("pos", x.pos).f("reverse", x.reverse)),
        RTS_C(Stance, "Стойка", j.f("mode", std::string(stanceName(x.mode))).f("home", x.home).f("homeSet", x.homeSet)),
        RTS_C(Retreating, "Отступает", j.f("to", x.to).f("point", x.point)),
        RTS_C(Transport, "Транспорт", j.list("cargo", x.cargo).f("used", x.used).f("unloading", x.unloading).f("unloadAt", x.unloadAt)),
        RTS_C(Boarded, "В транспорте", j.f("carrier", x.carrier).f("fires", x.fires)),
        RTS_C(Veteran, "Ветеран", j.f("level", x.level).f("xp", x.xp).f("lastHit", x.lastHit)),
        RTS_C(RallyPath, "Путь сбора", j.list("points", x.points)),
        RTS_C(Shield, "Щит", j.f("value", x.value).ticks("lastHit", x.lastHit)),
        RTS_C(Eater, "Пожиратель", j.f("kills", x.kills)),
        RTS_C(Formation, "Строй", j.f("group", x.group).f("shape", x.shape).f("offset", x.offset)),
        RTS_C(BridgeLaid, "Мост / ворота", j.f("laid", x.laid).f("cells", x.cells.size())),
        RTS_C(Ammo, "Боезапас", j.list("left", std::vector<int>(x.left, x.left + 4))),
        RTS_C(Merging, "Слияние", j.f("partner", x.partner)),
        RTS_C(Trader, "Торговец", j.f("home", x.home).f("dest", x.dest).f("loaded", x.loaded)),
        RTS_C(ItemBag, "Предметы", j.list("items", x.items).list("charges", x.charges)),
        RTS_C(Suppression, "Подавление", j.f("value", x.value)),
        RTS_C(Turret, "Башня", j.list("dir", std::vector<Vec3>(x.dir, x.dir + 4))),
        RTS_C(GrowState, "Рост", j.f("timer", x.timer)),
        RTS_C(Morale, "Мораль", j.f("value", x.value).f("fatigue", x.fatigue).f("state", x.state).f("outOfSupply", x.outOfSupply)),
        RTS_C(Weathering, "Выветривание", j.f("bare", x.bare)),
        RTS_C(TrapState, "Ловушка", j.f("charges", x.charges).ticks("readyAt", x.readyAt)),
        RTS_C(TeleportBack, "Возврат телепорта", j.f("home", x.home).ticks("at", x.at).f("caster", x.caster)),
        RTS_C(Belt, "Конвейер", j.f("dir", x.dir)),
        RTS_C(Crafter, "Производитель", j.resList("in", x.in).resList("out", x.out).f("timer", x.timer)),
        RTS_C(TurnBudget, "Ход", j.f("move", x.move).f("shots", x.shots).f("turn", x.turn)),
        RTS_C(Altitude, "Высота полёта", j.f("target", x.target).f("y", x.y)),
        RTS_C(ScriptData, "Данные скрипта", j.o = decodeScriptBlob(x.blob)),
        RTS_C(ScriptStats, "Базы статов скрипта", {
            Json m = Json::object();
            for (auto& s : x.list) m.set(std::to_string(int(s.stat)), J::num(s.base));
            j.o.set("base", m);
        }),
    };
    return d;
}
#undef RTS_C

std::string lower(std::string s) { for (auto& c : s) c = char(std::tolower(uint8_t(c))); return s; }

// ---------------------------------------------------------------- script blob decoding (RtsScript.cpp format)
uint64_t getU64(const std::string& in, size_t pos) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= uint64_t(uint8_t(in[pos + size_t(i)])) << (8 * i);
    return v;
}
size_t decodeValue(const std::string& in, size_t pos, int depth, Json& out) {   // 0 on malformed input
    if (pos >= in.size() || depth > 40) return 0;
    const char t = in[pos++];
    switch (t) {
    case 't': out = Json::fromBool(true); return pos;
    case 'f': out = Json::fromBool(false); return pos;
    case 'i': if (pos + 8 > in.size()) return 0; out = Json::number(int64_t(getU64(in, pos))); return pos + 8;
    case 'x': if (pos + 8 > in.size()) return 0; out = Json::number(Fixed::fromRaw(int64_t(getU64(in, pos))).toDouble(), 3); return pos + 8;
    case 's': {
        if (pos + 8 > in.size()) return 0;
        const uint64_t n = getU64(in, pos);
        pos += 8;
        if (n > in.size() - pos) return 0;
        out = Json::string(in.substr(pos, size_t(n)));
        return pos + size_t(n);
    }
    case '{': {
        if (pos + 8 > in.size()) return 0;
        const uint64_t n = getU64(in, pos);
        pos += 8;
        std::vector<std::pair<Json, Json>> kv;
        for (uint64_t i = 0; i < n; ++i) {
            Json k, v;
            if (!(pos = decodeValue(in, pos, depth + 1, k))) return 0;
            if (!(pos = decodeValue(in, pos, depth + 1, v))) return 0;
            kv.emplace_back(std::move(k), std::move(v));
        }
        // a Lua sequence 1..n becomes an array
        bool seq = !kv.empty();
        for (size_t i = 0; i < kv.size() && seq; ++i) seq = kv[i].first.isNumber() && kv[i].first.integer() == int64_t(i + 1);
        if (seq) { out = Json::array(); for (auto& p : kv) out.push(std::move(p.second)); return pos; }
        out = Json::object();
        for (auto& [k, v] : kv)
            out.set(k.isString() ? k.str() : k.isNumber() ? std::to_string(k.integer()) : std::string(k.boolean() ? "true" : "false"), std::move(v));
        return pos;
    }
    default: return 0;
    }
}

std::string scalarText(const Json& v) {
    if (v.isString()) return v.str();
    if (v.isNumber()) return v.dump(-1);
    if (v.type() == Json::Type::Bool) return v.boolean() ? "true" : "false";
    return {};
}

std::string ownerName(const WorldModel& m, int owner) {
    if (owner == -1) return "Враждебные монстры";
    if (owner == 0) return "Нейтральные";
    if (owner > 0 && size_t(owner) < m.playerNames.size() && !m.playerNames[size_t(owner)].empty()) return m.playerNames[size_t(owner)];
    return "Игрок " + std::to_string(owner);
}

} // namespace

// ---------------------------------------------------------------- components
const std::vector<WorldComponent>& worldComponents() {
    static const std::vector<WorldComponent> v = [] {
        std::vector<WorldComponent> r;
        for (auto& d : descs()) r.push_back(d.info);
        return r;
    }();
    return v;
}

int findWorldComponent(const std::string& s) {
    const std::string l = lower(s);
    const auto& c = worldComponents();
    for (size_t i = 0; i < c.size(); ++i) if (lower(c[i].id) == l || lower(c[i].title) == l) return int(i);
    return -1;
}

size_t touchWorldComponentPools(World& w) {
    for (auto& d : descs()) d.touch(w);
    return descs().size();
}

const char* worldKindId(WorldKind k) {
    static const char* n[] = {"unit", "structure", "hero", "resource", "projectile", "effect", "other"};
    return size_t(k) < size_t(WorldKind::Count) ? n[size_t(k)] : "other";
}
const char* worldKindTitle(WorldKind k) {
    static const char* n[] = {"Юниты", "Здания", "Герои", "Ресурсы", "Снаряды", "Эффекты на земле", "Прочее"};
    return size_t(k) < size_t(WorldKind::Count) ? n[size_t(k)] : "Прочее";
}
const char* worldOriginTitle(WorldOrigin o) {
    return o == WorldOrigin::Map ? "С карты" : o == WorldOrigin::Setup ? "Создан игрой на старте" : "Создан во время игры";
}

WorldConfig parseWorldConfig(const Json& manifest) {
    WorldConfig c;
    const Json& h = manifest["editor"]["hierarchy"];
    if (h.isNull()) return c;
    if (!h.isObject()) { c.errors.push_back("editor.hierarchy: must be an object"); return c; }
    if (h.has("previewTicks")) c.previewTicks = int(std::clamp<int64_t>(h["previewTicks"].integer(2), 0, 600));
    c.labelKey = h.getStr("label");
    for (auto& t : h["categories"].items()) if (t.isString()) c.categoryTags.push_back(t.str());
    for (auto& g : h["groupings"].items()) {
        WorldConfig::Grouping gr;
        gr.title = g.getStr("title");
        for (auto& k : g["by"].items()) {
            const std::string key = k.str();
            static const char* known[] = {"owner", "type", "kind", "category", "origin", "region", "parent", "tag", "component"};
            bool ok = key.rfind("data:", 0) == 0 && key.size() > 5;
            for (auto* kk : known) ok |= key == kk;
            if (ok) gr.by.push_back(key);
            else c.errors.push_back("editor.hierarchy.groupings: unknown key '" + key + "' (owner, type, kind, category, origin, region, parent, tag, component, data:<key>)");
        }
        if (gr.title.empty()) c.errors.push_back("editor.hierarchy.groupings: a grouping needs a title");
        else if (!gr.by.empty()) c.groupings.push_back(std::move(gr));
    }
    return c;
}

// ---------------------------------------------------------------- reading the world
std::vector<uint64_t> linkMapEntities(Simulation& sim, RtsModule& mod, const MapData& doc) {
    std::vector<uint64_t> out(doc.entities.size(), 0);
    World& w = sim.world();
    const auto st = w.entityState();
    const Rules& R = mod.rules();
    uint32_t next = 0;   // map entities are spawned in document order: never match backwards
    for (size_t i = 0; i < doc.entities.size(); ++i) {
        const DocEntity& d = doc.entities[i];
        const int t = R.findType(d.type);
        if (t < 0) continue;
        const uint8_t owner = d.owner == -1 ? kHostileNeutral : uint8_t(std::clamp(d.owner, 0, 254));
        for (uint32_t k = next; k < st.alive.size(); ++k) {
            if (!st.alive[k]) continue;
            Entity e{k, st.gens[k]};
            auto* u = w.tryGet<Unit>(e);
            auto* o = w.tryGet<Owner>(e);
            auto* tr = w.tryGet<Transform>(e);
            if (!u || !o || !tr || int(u->type) != t || o->player != owner) continue;
            if (!doc.planet() && std::hypot(tr->pos.x.toDouble() - d.x, tr->pos.z.toDouble() - d.z) > 3.0) continue;
            out[i] = worldRef(e);
            next = k + 1;
            break;
        }
    }
    return out;
}

void readWorld(Simulation& sim, RtsModule& mod, const WorldConfig& cfg, const std::vector<uint64_t>& links,
               const std::vector<uint64_t>& setupRefs, const MapData* doc, WorldModel& out) {
    World& w = sim.world();
    const Rules& R = mod.rules();
    GameState* gs = w.resource<GameState>();
    out.rows.clear();
    out.byRef.clear();
    out.tick = sim.tick();
    ++out.serial;
    out.regionNames = gs ? gs->regionNames : std::vector<std::string>();
    out.playerNames.assign(1, "Нейтральные");
    for (auto& p : sim.players()) {
        if (out.playerNames.size() <= p.slot) out.playerNames.resize(size_t(p.slot) + 1);
        out.playerNames[p.slot] = p.name;
    }
    if (doc)
        for (size_t p = 0; p < doc->players.size(); ++p) {
            if (out.playerNames.size() <= p + 1) out.playerNames.resize(p + 2);
            if (!doc->players[p].name.empty()) out.playerNames[p + 1] = doc->players[p].name;
        }
    std::map<uint64_t, int> docOf;
    for (size_t i = 0; i < links.size(); ++i) if (links[i]) docOf[links[i]] = int(i);
    const auto st = w.entityState();
    const auto& D = descs();
    for (uint32_t k = 0; k < st.alive.size(); ++k) {
        if (!st.alive[k]) continue;
        WorldRow r;
        r.e = Entity{k, st.gens[k]};
        r.ref = worldRef(r.e);
        for (size_t c = 0; c < D.size(); ++c) if (D[c].has(w, r.e)) r.comps.push_back(uint8_t(c));
        if (auto* o = w.tryGet<Owner>(r.e)) r.owner = o->player == kHostileNeutral ? -1 : int(o->player);
        if (auto* tr = w.tryGet<Transform>(r.e)) {
            r.hasPos = true;
            r.x = float(tr->pos.x.toDouble()); r.y = float(tr->pos.y.toDouble()); r.z = float(tr->pos.z.toDouble());
            if (gs && !gs->surface && !gs->regionNames.empty()) r.region = mod.regionAt(sim, tr->pos);
        }
        const UnitType* ut = nullptr;
        if (auto* u = w.tryGet<Unit>(r.e)) {
            r.type = int(u->type);
            ut = size_t(u->type) < R.types.size() ? &R.types[u->type] : nullptr;
            r.hp = float(u->hp.toDouble());
            r.maxHp = ut ? float(ut->maxHp.toDouble()) : 0;
            r.flags = u->flags;
        }
        if (auto* p = w.tryGet<Projectile>(r.e)) {
            r.kind = WorldKind::Projectile;
            r.owner = p->owner == kHostileNeutral ? -1 : int(p->owner);
            if (p->source.valid()) { r.parent = worldRef(p->source); r.parentRel = "выпущен"; }
        } else if (auto* a = w.tryGet<Area>(r.e)) {
            r.kind = WorldKind::Effect;
            if (a->caster.valid()) { r.parent = worldRef(a->caster); r.parentRel = "создан"; }
        } else if (ut) {
            r.kind = ut->hero ? WorldKind::Hero : ut->resourceNode >= 0 ? WorldKind::Resource : ut->structure ? WorldKind::Structure : WorldKind::Unit;
        }
        if (auto* b = w.tryGet<Boarded>(r.e); b && b->carrier.valid()) { r.parent = worldRef(b->carrier); r.parentRel = "в транспорте"; }
        else if (auto* s = w.tryGet<Summon>(r.e); s && s->summoner.valid()) { r.parent = worldRef(s->summoner); r.parentRel = "призван"; }
        r.category = worldKindTitle(r.kind);
        if (ut) {
            auto tagged = [&](const std::string& tag) {
                for (size_t ti = 0; ti < R.tagNames.size() && ti < 64; ++ti) if (R.tagNames[ti] == tag && (ut->tags >> ti & 1)) return true;
                return false;
            };
            for (auto& tag : cfg.categoryTags) if (tagged(tag)) { r.category = tag; break; }
        }
        if (auto* sd = w.tryGet<ScriptData>(r.e); sd && !sd->blob.empty()) {
            const Json data = decodeScriptBlob(sd->blob);
            for (auto& [key, v] : data.members()) if (std::string t = scalarText(v); !t.empty()) r.data[key] = t;
            if (!cfg.labelKey.empty()) if (auto it = r.data.find(cfg.labelKey); it != r.data.end()) r.label = it->second;
        }
        if (auto it = docOf.find(r.ref); it != docOf.end()) { r.doc = it->second; r.origin = WorldOrigin::Map; }
        else r.origin = std::binary_search(setupRefs.begin(), setupRefs.end(), r.ref) ? WorldOrigin::Setup : WorldOrigin::Runtime;
        out.byRef[r.ref] = out.rows.size();
        out.rows.push_back(std::move(r));
    }
}

Json decodeScriptBlob(const std::string& blob) {
    Json out;
    if (blob.empty() || !decodeValue(blob, 0, 0, out)) return Json::object();
    return out;
}

Json inspectEntity(Simulation& sim, RtsModule& mod, Entity e, const Json& rulesJson) {
    World& w = sim.world();
    Json r = Json::object();
    if (!w.alive(e)) { r.set("error", Json::string("object is gone")); return r; }
    const Rules& R = mod.rules();
    Json comps = Json::object();
    for (auto& d : descs()) {
        if (!d.has(w, e)) continue;
        J j{w, R};
        d.dump(j, e);
        comps.set(d.info.id, std::move(j.o));
    }
    r.set("components", comps);
    if (auto* u = w.tryGet<Unit>(e); u && size_t(u->type) < R.types.size()) {
        const UnitType& t = R.types[u->type];
        // effective values (techs, levels, buffs, statuses) next to the type's base
        struct S { const char* id; Stat s; Fixed base; };
        const S stats[] = {{"maxHp", Stat::MaxHp, t.maxHp}, {"armor", Stat::Armor, t.armor}, {"speed", Stat::Speed, t.speed}, {"sight", Stat::Sight, t.sight},
                           {"damage", Stat::Damage, t.weapons.empty() ? Fixed{} : t.weapons[0].damage},
                           {"range", Stat::Range, t.weapons.empty() ? Fixed{} : t.weapons[0].range}};
        Json s = Json::object();
        for (auto& x : stats) {
            Json v = Json::object();
            v.set("base", J::num(x.base));
            Fixed eff = mod.stat(sim, e, x.s);
            if (x.s == Stat::Damage) eff = x.base * eff;        // the stat is a multiplier on weapon damage
            else if (x.s == Stat::Range) eff = x.base + eff;    // ... a bonus on weapon range
            v.set("effective", J::num(eff));
            s.set(x.id, v);
        }
        r.set("stats", s);
        r.set("rules", rulesJson["types"][t.id]);
        r.set("type", Json::string(t.id));
    }
    return r;
}

// ---------------------------------------------------------------- grouping / search
std::string worldGroupValue(const WorldModel& m, const Rules& R, const WorldRow& r, const std::string& key) {
    if (key == "owner") return ownerName(m, r.owner);
    if (key == "type") return r.type >= 0 && size_t(r.type) < R.types.size() ? R.types[size_t(r.type)].name : worldKindTitle(r.kind);
    if (key == "kind") return worldKindTitle(r.kind);
    if (key == "category") return r.category;
    if (key == "origin") return worldOriginTitle(r.origin);
    if (key == "region") return r.region >= 0 && size_t(r.region) < m.regionNames.size() ? m.regionNames[size_t(r.region)] : "Вне регионов";
    if (key == "component") return r.comps.empty() ? "Без компонентов" : worldComponents()[r.comps[0]].title;
    if (key == "parent") {
        if (!r.parent) return "Верхний уровень";
        const WorldRow* p = m.find(r.parent);
        return p ? worldRowName(m, R, nullptr, *p) : "#" + std::to_string(worldEntity(r.parent).index);
    }
    if (key == "tag") {
        if (r.type >= 0 && size_t(r.type) < R.types.size())
            for (size_t ti = 0; ti < R.tagNames.size() && ti < 64; ++ti) if (R.types[size_t(r.type)].tags >> ti & 1) return R.tagNames[ti];
        return "Без тегов";
    }
    if (key.rfind("data:", 0) == 0) {
        auto it = r.data.find(key.substr(5));
        return it == r.data.end() ? "—" : it->second;
    }
    return {};
}

std::string worldRowName(const WorldModel& m, const Rules& R, const MapData* doc, const WorldRow& r) {
    (void)m;
    if (!r.label.empty()) return r.label;
    std::string n = r.type >= 0 && size_t(r.type) < R.types.size() ? R.types[size_t(r.type)].name
                  : r.kind == WorldKind::Projectile ? "Снаряд" : r.kind == WorldKind::Effect ? "Эффект" : "Объект";
    if (doc && r.doc >= 0 && size_t(r.doc) < doc->entities.size() && !doc->entities[size_t(r.doc)].id.empty()) n += "  #" + doc->entities[size_t(r.doc)].id;
    return n;
}

bool worldRowMatches(const WorldModel& m, const Rules& R, const MapData* doc, const WorldRow& r, const std::string& query) {
    size_t i = 0;
    while (i < query.size()) {
        while (i < query.size() && query[i] == ' ') ++i;
        size_t j = query.find(' ', i);
        if (j == std::string::npos) j = query.size();
        std::string tok = lower(query.substr(i, j - i));
        i = j;
        if (tok.empty()) continue;
        auto has = [](const std::string& hay, const std::string& needle) { return lower(hay).find(needle) != std::string::npos; };
        std::string prefix, v = tok;
        if (size_t c = tok.find(':'); c != std::string::npos && c > 0 && c <= 2) { prefix = tok.substr(0, c); v = tok.substr(c + 1); }
        else if (tok[0] == '#') { prefix = "#"; v = tok.substr(1); }
        if (v.empty()) continue;
        const std::string typeId = r.type >= 0 && size_t(r.type) < R.types.size() ? R.types[size_t(r.type)].id : "";
        const std::string mapId = doc && r.doc >= 0 && size_t(r.doc) < doc->entities.size() ? doc->entities[size_t(r.doc)].id : "";
        auto typeOk = [&] { return has(typeId, v) || has(worldGroupValue(m, R, r, "type"), v) || has(r.label, v) || has(r.category, v); };
        auto ownerOk = [&] { return has(ownerName(m, r.owner), v) || v == std::to_string(r.owner); };
        auto compOk = [&] {
            for (uint8_t c : r.comps) if (has(worldComponents()[c].id, v) || has(worldComponents()[c].title, v)) return true;
            return false;
        };
        auto regionOk = [&] { return r.region >= 0 && has(worldGroupValue(m, R, r, "region"), v); };
        auto dataOk = [&] {
            for (auto& [k, val] : r.data) if (has(k, v) || has(val, v)) return true;
            return false;
        };
        auto idOk = [&] { return v == std::to_string(r.e.index) || has(mapId, v); };
        bool ok;
        if (prefix == "t") ok = typeOk();
        else if (prefix == "p") ok = ownerOk();
        else if (prefix == "c") ok = compOk();
        else if (prefix == "r") ok = regionOk();
        else if (prefix == "d") ok = dataOk();
        else if (prefix == "#" || prefix == "id") ok = idOk();
        else ok = typeOk() || ownerOk() || compOk() || regionOk() || dataOk() || idOk() || has(worldOriginTitle(r.origin), v);
        if (!ok) return false;
    }
    return true;
}

// ---------------------------------------------------------------- preview
game::RtsModule* WorldPreview::module() { return sim_ ? dynamic_cast<game::RtsModule*>(&sim_->module()) : nullptr; }

bool WorldPreview::build(const Json& simRules, const Json& simMap, const MapData& doc, const WorldConfig& cfg) {
    const auto t0 = std::chrono::steady_clock::now();
    error_.clear();
    scriptErrors_ = 0;
    links_.clear();
    setupRefs_.clear();
    sim_.reset();
    try {
        SimConfig sc;
        sc.seed = 1;   // fixed: the same data gives the same entity ids, so a selection survives a refresh
        auto sim = std::make_unique<Simulation>(sc, std::make_unique<game::RtsModule>());
        sim->setup(simRules, simMap);
        auto* mod = dynamic_cast<game::RtsModule*>(&sim->module());
        links_ = linkMapEntities(*sim, *mod, doc);
        for (int k = 0; k < cfg.previewTicks; ++k) sim->step();
        sim->module().prepareSnapshot(*sim);   // Lua entity tables into the world (this sim is ours)
        const auto st = sim->world().entityState();
        for (uint32_t k = 0; k < st.alive.size(); ++k) if (st.alive[k]) setupRefs_.push_back(worldRef(Entity{k, st.gens[k]}));
        std::sort(setupRefs_.begin(), setupRefs_.end());
        if (auto* se = mod->scripts()) {
            scriptErrors_ = se->errorCount();
            for (auto& l : se->log()) if (l.error) { error_ = "Скрипт: " + l.text; break; }
        }
        sim_ = std::move(sim);
    } catch (const std::exception& e) {
        error_ = e.what();
    } catch (...) {
        error_ = "неизвестная ошибка";
    }
    ms_ = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return sim_ != nullptr;
}

} // namespace rts::editor
