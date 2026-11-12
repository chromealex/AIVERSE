// World tree: generic reading of the RTS module's ECS for the editor (see WorldInspect.h).
#include "WorldInspect.h"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <set>
#include "SimTools.h"
#if AIVERSE_PREVIEW_THREAD
#include <thread>
#endif

namespace aiverse::editor {

using namespace aiverse::game;

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

#define AIVERSE_C(T, TITLE, ...) Desc{{#T, TITLE}, &hasC<T>, &touchC<T>, [](J& j, Entity e) { auto& x = *j.w.tryGet<T>(e); (void)x; __VA_ARGS__; }}

const std::vector<Desc>& descs() {
    static const std::vector<Desc> d = {
        AIVERSE_C(Transform, "Положение", j.f("pos", x.pos).f("yaw", x.yaw)),
        AIVERSE_C(Owner, "Владелец", j.f("player", x.player)),
        AIVERSE_C(Unit, "Объект", {
            j.type("type", x.type).f("hp", x.hp).f("energy", x.energy).f("facing", x.facing).f("lastAttacker", x.lastAttacker).f("lastHitBy", x.lastHitBy);
            Json fl = Json::array();
            static const char* names[] = {"hold", "underConstruction", "disabled", "silent", "inHand", "possessed", "inTransit", "inCargo", "infected"};
            for (int b = 0; b < 9; ++b) if (x.flags & (1u << b)) fl.push(Json::string(names[b]));
            j.o.set("flags", fl);
        }),
        AIVERSE_C(Orders, "Приказы", {
            Json a = Json::array();
            for (auto& o : x.queue) a.push(orderJson(j, o));
            j.o.set("queue", a);
        }),
        AIVERSE_C(Mobile, "Движение", j.f("moving", x.moving).f("goal", x.goal).f("goalRadius", x.goalRadius).f("pathLength", x.path.size()).f("pathPos", x.pathPos)
                                     .f("velocity", x.vel).f("stuck", x.stuck).f("flow", x.flow).f("yielded", x.yielded)),
        AIVERSE_C(Combat, "Бой", j.f("target", x.target).f("autoTarget", x.autoTarget).list("cooldown", std::vector<Fixed>(x.cooldown, x.cooldown + 4))
                                 .f("chargeTarget", x.chargeTarget).f("slot", int(x.slot))),
        AIVERSE_C(Cargo, "Груз", j.res("resource", x.res).f("amount", x.amount).f("lastNode", x.lastNode).f("field", x.field)),
        AIVERSE_C(Production, "Производство", {
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
        AIVERSE_C(Construction, "Стройка", j.f("progress", x.progress).f("paid", x.paid).f("builder", x.builder).f("selfBuild", x.selfBuild)),
        AIVERSE_C(ResourceStock, "Запас ресурса", j.res("resource", x.res).f("amount", x.amount).f("infinite", x.infinite)),
        AIVERSE_C(Extractor, "Добытчик", j.type("nodeType", x.nodeType).f("remaining", x.remaining)),
        AIVERSE_C(Footprint, "Площадь", j.f("x", x.x).f("y", x.y).f("w", x.w).f("h", x.h)),
        AIVERSE_C(Garrison, "Гарнизон", j.f("count", x.count).f("growth", x.growth).f("pendingOut", x.pendingOut).f("pendingTarget", x.pendingTarget).f("lost", x.lost)),
        AIVERSE_C(Projectile, "Снаряд", j.f("source", x.source).f("target", x.target).f("aim", x.aim).f("speed", x.speed).f("damage", x.damage).f("splash", x.splash)
                                        .f("owner", x.owner).type("sourceType", x.sourceType).f("travel", x.travel).f("total", x.total)),
        AIVERSE_C(Hero, "Герой", j.f("level", x.level).f("xp", x.xp).f("points", x.points).list("skills", std::vector<int>(x.skill, x.skill + 8))),
        AIVERSE_C(Transit, "Перелёт", j.f("from", x.from).f("to", x.to).ticks("depart", x.depart).ticks("arrive", x.arrive).f("arrival", x.arrival)),
        AIVERSE_C(Squad, "Отряд", j.f("reinforcing", x.reinforcing).f("reinforceTimer", x.reinforceTimer)),
        AIVERSE_C(Buffs, "Баффы", {
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
        AIVERSE_C(AbilityState, "Способности", j.list("readyAt", std::vector<uint32_t>(x.readyAt, x.readyAt + 8)).list("charges", std::vector<int>(x.charges, x.charges + 8))
                                               .ticks("busyUntil", x.busyUntil)),
        AIVERSE_C(Statuses, "Статусы", {
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
        AIVERSE_C(Area, "Зона эффекта", j.f("caster", x.caster).type("sourceType", x.sourceType).f("radius", x.radius).ticks("fireAt", x.fireAt).ticks("endAt", x.endAt)
                                        .f("triggers", x.triggers).f("follow", x.follow).f("dest", x.dest).f("lane", x.lane).f("goal", x.goal).f("arrived", x.arrived)),
        AIVERSE_C(Channel, "Поддержание", j.f("target", x.target).f("point", x.point).f("ability", x.ability).ticks("nextAt", x.nextAt).ticks("endAt", x.endAt)),
        AIVERSE_C(GroupTag, "Группа", j.f("id", x.id).f("from", x.from).f("to", x.to)),
        AIVERSE_C(Capture, "Захват", j.f("progress", x.progress).f("team", x.team)),
        AIVERSE_C(Guard, "Охрана", j.f("home", x.home).f("leash", x.leash).f("returning", x.returning)),
        AIVERSE_C(Summon, "Призванный", j.ticks("expire", x.expire).type("summonerType", x.summonerType).f("summoner", x.summoner)),
        AIVERSE_C(Needs, "Потребности", j.f("hunger", x.hunger).f("sleep", x.sleep).f("happiness", x.happiness).f("leaving", x.leaving)),
        AIVERSE_C(Possessed, "Под управлением", j.f("dir", x.dir).f("target", x.target).f("fire", x.fire)),
        AIVERSE_C(PortalState, "Портал", j.f("timer", x.timer)),
        AIVERSE_C(AutoTrainState, "Автонайм", j.f("timer", x.timer)),
        AIVERSE_C(ClaimState, "Захват клеток", j.f("acc", x.acc)),
        AIVERSE_C(Advancer, "Наступает", (void)0),
        AIVERSE_C(Inventory, "Склад", j.resList("have", x.have).resList("incoming", x.incoming).resList("reserved", x.reserved)),
        AIVERSE_C(Workshop, "Мастерская", j.f("progress", x.progress).f("running", x.running)),
        AIVERSE_C(Carrier, "Носильщик", j.f("from", x.from).f("to", x.to).res("resource", x.res).f("amount", x.amount).f("loaded", x.loaded)),
        AIVERSE_C(LaneWalker, "Идёт по линии", j.f("lane", x.lane).f("pos", x.pos).f("reverse", x.reverse)),
        AIVERSE_C(Stance, "Стойка", j.f("mode", std::string(stanceName(x.mode))).f("home", x.home).f("homeSet", x.homeSet)),
        AIVERSE_C(Retreating, "Отступает", j.f("to", x.to).f("point", x.point)),
        AIVERSE_C(Transport, "Транспорт", j.list("cargo", x.cargo).f("used", x.used).f("unloading", x.unloading).f("unloadAt", x.unloadAt)),
        AIVERSE_C(Boarded, "В транспорте", j.f("carrier", x.carrier).f("fires", x.fires)),
        AIVERSE_C(Veteran, "Ветеран", j.f("level", x.level).f("xp", x.xp).f("lastHit", x.lastHit)),
        AIVERSE_C(RallyPath, "Путь сбора", j.list("points", x.points)),
        AIVERSE_C(Shield, "Щит", j.f("value", x.value).ticks("lastHit", x.lastHit)),
        AIVERSE_C(Eater, "Пожиратель", j.f("kills", x.kills)),
        AIVERSE_C(Formation, "Строй", j.f("group", x.group).f("shape", x.shape).f("offset", x.offset)),
        AIVERSE_C(BridgeLaid, "Мост / ворота", j.f("laid", x.laid).f("cells", x.cells.size())),
        AIVERSE_C(Ammo, "Боезапас", j.list("left", std::vector<int>(x.left, x.left + 4))),
        AIVERSE_C(Merging, "Слияние", j.f("partner", x.partner)),
        AIVERSE_C(Trader, "Торговец", j.f("home", x.home).f("dest", x.dest).f("loaded", x.loaded)),
        AIVERSE_C(ItemBag, "Предметы", j.list("items", x.items).list("charges", x.charges)),
        AIVERSE_C(Suppression, "Подавление", j.f("value", x.value)),
        AIVERSE_C(Turret, "Башня", j.list("dir", std::vector<Vec3>(x.dir, x.dir + 4))),
        AIVERSE_C(GrowState, "Рост", j.f("timer", x.timer)),
        AIVERSE_C(Morale, "Мораль", j.f("value", x.value).f("fatigue", x.fatigue).f("state", x.state).f("outOfSupply", x.outOfSupply)),
        AIVERSE_C(Weathering, "Выветривание", j.f("bare", x.bare)),
        AIVERSE_C(TrapState, "Ловушка", j.f("charges", x.charges).ticks("readyAt", x.readyAt)),
        AIVERSE_C(TeleportBack, "Возврат телепорта", j.f("home", x.home).ticks("at", x.at).f("caster", x.caster)),
        AIVERSE_C(Belt, "Конвейер", j.f("dir", x.dir)),
        AIVERSE_C(Crafter, "Производитель", j.resList("in", x.in).resList("out", x.out).f("timer", x.timer)),
        AIVERSE_C(TurnBudget, "Ход", j.f("move", x.move).f("shots", x.shots).f("turn", x.turn)),
        AIVERSE_C(Altitude, "Высота полёта", j.f("target", x.target).f("y", x.y)),
        AIVERSE_C(ScriptData, "Данные скрипта", j.o = decodeScriptBlob(x.blob)),
        AIVERSE_C(ScriptStats, "Базы статов скрипта", {
            Json m = Json::object();
            for (auto& s : x.list) m.set(std::to_string(int(s.stat)), J::num(s.base));
            j.o.set("base", m);
        }),
        AIVERSE_C(Pulled, "Притягивается", j.f("target", x.target).f("way", x.way).f("dist", x.dist).f("vel", x.vel).f("done", x.done)),   // skill objects
        AIVERSE_C(Transfer, "Телепорт гарнизона", j.f("from", x.from).f("to", x.to).f("collected", x.collected).f("toGrab", x.toGrab).f("wait", x.wait)),   // skill objects
    };
    return d;
}
#undef AIVERSE_C

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

namespace {
// The row fields of one alive entity into r (reused: clears what it sets).
void fillRow(Simulation& sim, RtsModule& mod, const WorldConfig& cfg, const GameState* gs, Entity e, WorldRow& r, const WorldReadOptions& opt) {
    World& w = sim.world();
    const Rules& R = mod.rules();
    r.e = e;
    r.ref = worldRef(e);
    r.type = -1; r.owner = 0; r.kind = WorldKind::Other; r.hasPos = false; r.x = r.y = r.z = 0; r.hp = r.maxHp = 0; r.flags = 0;
    r.parent = 0; r.parentRel = ""; r.region = -1;
    r.comps.reset();
    const auto& D = descs();
    for (size_t c = 0; c < D.size() && c < kMaxWorldComponents; ++c) if (D[c].has(w, e)) r.comps.set(c);
    if (auto* o = w.tryGet<Owner>(e)) r.owner = o->player == kHostileNeutral ? -1 : int(o->player);
    if (auto* tr = w.tryGet<Transform>(e)) {
        r.hasPos = true;
        r.x = float(tr->pos.x.toDouble()); r.y = float(tr->pos.y.toDouble()); r.z = float(tr->pos.z.toDouble());
        if (gs && !gs->surface && !gs->regionNames.empty()) r.region = mod.regionAt(sim, tr->pos);
    }
    const UnitType* ut = nullptr;
    if (auto* u = w.tryGet<Unit>(e)) {
        r.type = int(u->type);
        ut = size_t(u->type) < R.types.size() ? &R.types[u->type] : nullptr;
        r.hp = float(u->hp.toDouble());
        r.maxHp = ut ? float(ut->maxHp.toDouble()) : 0;
        r.flags = u->flags;
    }
    if (auto* p = w.tryGet<Projectile>(e)) {
        r.kind = WorldKind::Projectile;
        r.owner = p->owner == kHostileNeutral ? -1 : int(p->owner);
        if (p->source.valid()) { r.parent = worldRef(p->source); r.parentRel = "выпущен"; }
    } else if (auto* a = w.tryGet<Area>(e)) {
        r.kind = WorldKind::Effect;
        if (a->caster.valid()) { r.parent = worldRef(a->caster); r.parentRel = "создан"; }
    } else if (ut) {
        r.kind = ut->hero ? WorldKind::Hero : ut->resourceNode >= 0 ? WorldKind::Resource : ut->structure ? WorldKind::Structure : WorldKind::Unit;
    }
    if (auto* b = w.tryGet<Boarded>(e); b && b->carrier.valid()) { r.parent = worldRef(b->carrier); r.parentRel = "в транспорте"; }
    else if (auto* s = w.tryGet<Summon>(e); s && s->summoner.valid()) { r.parent = worldRef(s->summoner); r.parentRel = "призван"; }
    const char* category = worldKindTitle(r.kind);
    if (ut)
        for (auto& tag : cfg.categoryTags) {
            bool has = false;
            for (size_t ti = 0; ti < R.tagNames.size() && ti < 64 && !has; ++ti) has = R.tagNames[ti] == tag && (ut->tags >> ti & 1);
            if (has) { category = tag.c_str(); break; }
        }
    r.category = category;
    r.label.clear();
    r.data.clear();
    if (!opt.scriptData) return;
    if (auto* sd = w.tryGet<ScriptData>(e)) setWorldRowData(cfg, sd->blob, r);
}
} // namespace

void setWorldRowData(const WorldConfig& cfg, const std::string& blob, WorldRow& r) {
    r.data.clear();
    r.label.clear();
    if (blob.empty()) return;
    const Json data = decodeScriptBlob(blob);
    for (auto& [key, v] : data.members()) if (std::string t = scalarText(v); !t.empty()) r.data[key] = t;
    if (!cfg.labelKey.empty()) if (auto it = r.data.find(cfg.labelKey); it != r.data.end()) r.label = it->second;
}

bool readWorldRow(Simulation& sim, RtsModule& mod, const WorldConfig& cfg, Entity e, WorldRow& r, const WorldReadOptions& opt) {
    if (!sim.world().alive(e)) return false;
    fillRow(sim, mod, cfg, sim.world().resource<GameState>(), e, r, opt);
    return true;
}

void readWorld(Simulation& sim, RtsModule& mod, const WorldConfig& cfg, const std::vector<uint64_t>& links,
               const std::vector<uint64_t>& setupRefs, const MapData* doc, WorldModel& out, const WorldReadOptions& opt) {
    World& w = sim.world();
    GameState* gs = w.resource<GameState>();
    out.tick = sim.tick();
    ++out.serial;
    out.scriptData = opt.scriptData;
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
    // map links by entity index (refs carry the generation: compared below)
    const auto st = w.entityState();
    out.byIndex.assign(st.alive.size(), -1);
    std::vector<int32_t> docAt(st.alive.size(), -1);
    for (size_t i = 0; i < links.size(); ++i)
        if (links[i]) if (const uint32_t k = worldEntity(links[i]).index; k < docAt.size()) docAt[k] = int32_t(i);
    out.byDoc.assign(links.size(), -1);
    size_t n = 0;
    for (uint32_t k = 0; k < st.alive.size(); ++k) {
        if (!st.alive[k]) continue;
        if (n == out.rows.size()) out.rows.emplace_back();
        WorldRow& r = out.rows[n];
        fillRow(sim, mod, cfg, gs, Entity{k, st.gens[k]}, r, opt);
        r.doc = -1;
        if (const int32_t d = docAt[k]; d >= 0 && links[size_t(d)] == r.ref) { r.doc = d; r.origin = WorldOrigin::Map; out.byDoc[size_t(d)] = int32_t(n); }
        else r.origin = std::binary_search(setupRefs.begin(), setupRefs.end(), r.ref) ? WorldOrigin::Setup : WorldOrigin::Runtime;
        out.byIndex[k] = int32_t(n);
        ++n;
    }
    out.rows.resize(n);
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
    if (key == "component") { const int c = r.firstComp(); return c < 0 ? "Без компонентов" : worldComponents()[size_t(c)].title; }
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
            bool any = false;
            r.eachComp([&](int c) { any = any || has(worldComponents()[size_t(c)].id, v) || has(worldComponents()[size_t(c)].title, v); });
            return any;
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

WorldPreview::Result WorldPreview::run(const Json* simRules, const Json* simMap, const Prepare& prepare, const MapData& doc, const WorldConfig& cfg) {
    const auto t0 = std::chrono::steady_clock::now();
    Result res;
    try {
        Json preparedRules, preparedMap;
        if (prepare) { prepare(preparedRules, preparedMap); simRules = &preparedRules; simMap = &preparedMap; }
        SimConfig sc;
        sc.seed = 1;   // fixed: the same data gives the same entity ids, so a selection survives a refresh
        auto sim = std::make_unique<Simulation>(sc, std::make_unique<game::RtsModule>());
        sim->setup(*simRules, *simMap);
        auto* mod = dynamic_cast<game::RtsModule*>(&sim->module());
        res.links = linkMapEntities(*sim, *mod, doc);
        for (int k = 0; k < cfg.previewTicks; ++k) sim->step();
        sim->module().prepareSnapshot(*sim);   // Lua entity tables into the world (this sim is ours)
        const auto st = sim->world().entityState();
        for (uint32_t k = 0; k < st.alive.size(); ++k) if (st.alive[k]) res.setupRefs.push_back(worldRef(Entity{k, st.gens[k]}));
        std::sort(res.setupRefs.begin(), res.setupRefs.end());
        if (auto* se = mod->scripts()) {
            res.scriptErrors = se->errorCount();
            for (auto& l : se->log()) if (l.error) { res.error = "Скрипт: " + l.text; break; }
        }
        res.sim = std::move(sim);
    } catch (const std::exception& e) {
        res.error = e.what();
    } catch (...) {
        res.error = "неизвестная ошибка";
    }
    res.ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
    return res;
}

WorldPreview::WorldPreview() = default;

void WorldPreview::adopt(Result&& r) {
#if AIVERSE_PREVIEW_THREAD
    if (sim_) trash_.push_back(std::move(sim_));
#endif
    sim_ = std::move(r.sim);
    links_ = std::move(r.links);
    setupRefs_ = std::move(r.setupRefs);
    error_ = std::move(r.error);
    scriptErrors_ = r.scriptErrors;
    ms_ = r.ms;
}

bool WorldPreview::build(const Json& simRules, const Json& simMap, const MapData& doc, const WorldConfig& cfg) {
    adopt(run(&simRules, &simMap, {}, doc, cfg));
    return sim_ != nullptr;
}

#if AIVERSE_PREVIEW_THREAD
struct WorldPreview::Job {
    Request req;
    std::vector<std::unique_ptr<Simulation>> trash;
    Result res;
    std::atomic<bool> done{false};
    std::thread thread;
};

void WorldPreview::launch(Request&& r) {
    static const bool warm = [] { World w; touchWorldComponentPools(w); return true; }();   // pool ids assigned here first
    (void)warm;
    job_ = std::make_unique<Job>();
    job_->req = std::move(r);
    job_->trash = std::move(trash_);
    trash_.clear();
    Job* j = job_.get();
    job_->thread = std::thread([j] {
        j->trash.clear();
        j->res = run(nullptr, nullptr, j->req.prepare, j->req.doc, j->req.cfg);
        j->done.store(true, std::memory_order_release);
    });
}

void WorldPreview::start(Prepare prepare, const MapData& doc, const WorldConfig& cfg) {
    Request r;
    r.prepare = std::move(prepare);
    r.doc.entities = doc.entities;   // linking needs the entities and whether it is a planet map
    r.doc.bodies = doc.bodies;
    r.cfg = cfg;
    if (job_) queued_ = std::make_unique<Request>(std::move(r));
    else launch(std::move(r));
}

bool WorldPreview::poll() {
    if (!job_ || !job_->done.load(std::memory_order_acquire)) return false;
    job_->thread.join();
    Result res = std::move(job_->res);
    job_.reset();
    if (queued_) {   // the data changed while it ran: its result is already old (freed by the next job)
        if (res.sim) trash_.push_back(std::move(res.sim));
        auto q = std::move(queued_);
        launch(std::move(*q));
        return false;
    }
    adopt(std::move(res));
    return true;
}

WorldPreview::~WorldPreview() {
    if (job_ && job_->thread.joinable()) job_->thread.join();
}
#else
struct WorldPreview::Job {};
void WorldPreview::start(Prepare prepare, const MapData& doc, const WorldConfig& cfg) {
    adopt(run(nullptr, nullptr, prepare, doc, cfg));
    adopted_ = true;
}
bool WorldPreview::poll() { const bool a = adopted_; adopted_ = false; return a; }
WorldPreview::~WorldPreview() = default;
#endif

} // namespace aiverse::editor
