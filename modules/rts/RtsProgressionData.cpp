// rules "progression": the match-progression library format plus engine keys (RtsProgressionData.h).
#include "RtsProgressionData.h"
#include "RtsEffectsData.h"
#include "libraries/include/aiverselib/genre/PerksData.h"
#include "libraries/include/aiverselib/genre/PerksPersistence.h"
#include <algorithm>
#include <stdexcept>

namespace aiverse::game {

namespace {
namespace lg = aiverse::lib::genre;
[[noreturn]] void bad(const std::string& where, const std::string& what) { throw std::runtime_error("progression" + (where.empty() ? "" : "." + where) + ": " + what); }

void keysOnly(const Json& j, const std::string& where, std::initializer_list<const char*> names) {
    if (!j.isObject()) bad(where, "expected an object");
    for (const auto& [k, v] : j.members()) {
        (void)v;
        bool ok = false;
        for (const char* n : names) if (k == n) ok = true;
        if (!ok) {
            std::string list;
            for (const char* n : names) list += std::string(list.empty() ? "" : ", ") + n;
            bad(where, "unknown key '" + k + "' (" + list + ")");
        }
    }
}
// The library reader runs on a copy without the engine keys; asking for every key of the original marks the block
// as read for rules_lint / rules_schema (the reader itself rejects unknown keys).
void touch(const Json& j) {
    if (j.isObject()) for (const auto& [k, v] : j.members()) { (void)v; touch(j[k]); }
    else if (j.isArray()) for (size_t i = 0; i < j.size(); ++i) touch(j[i]);
}
Fixed seconds(const Json& j, const char* key, Fixed def, const std::string& where) {
    if (!j.has(key)) return def;
    if (!j[key].isNumber()) bad(where + "." + key, "expected seconds");
    Fixed v = j[key].fx();
    if (v.raw < 0 || v > Fixed::fromInt(100000)) bad(where + "." + key, "must be 0..100000 seconds");
    return v;
}
int64_t milli(Fixed v) { return (v.raw * 1000 + (v.raw >= 0 ? 32768 : -32768)) / 65536; }

// Bullet variants: every projectile weapon of a progression unit type x every non-empty subset of bullet perks.
void buildVariants(Rules& r, ProgressionRules& p) {
    const size_t k = p.bulletPerks.size();
    if (k == 0) return;
    for (size_t t = 0; t < r.types.size(); ++t) {
        if (!p.typeUnit[t]) continue;
        auto& ut = r.types[t];
        for (size_t w = 0; w < ut.weapons.size() && w < 4; ++w) {
            const WeaponDef& wd = ut.weapons[w];
            if (wd.projectileSpeed.raw <= 0) continue;
            for (uint32_t mask = 1; mask < (1u << k); ++mask) {
                ProgFlightVariant v;
                v.type = uint16_t(t); v.weapon = uint8_t(w); v.mask = mask;
                v.flight = wd.flight;
                if (!v.flight.enabled) { v.flight = projectile::ProjectileFlight{}; v.flight.enabled = true; v.flight.homing = true; }   // keeps the homing shot
                for (size_t m = 0; m < v.flight.modifiers.size(); ++m) { v.perkOf.push_back(-1); v.baseCount.push_back(v.flight.modifiers[m].count); }
                for (size_t b = 0; b < k; ++b) {
                    if (!(mask & (1u << b))) continue;
                    const auto& def = p.defs.perks.at(p.bulletPerks[b]);
                    const uint32_t stack = def.maxSelections ? std::min<uint32_t>(def.maxSelections, 32) : 32;
                    for (auto m : p.ext.at(p.bulletPerks[b]).bullets) {
                        v.perkOf.push_back(int8_t(b)); v.baseCount.push_back(m.count);
                        if (m.kind == projectile::ProjectileModKind::Ricochet || m.kind == projectile::ProjectileModKind::FlyThrough) m.count = uint16_t(std::min<uint32_t>(32, uint32_t(m.count) * stack));
                        v.flight.modifiers.push_back(m);
                    }
                }
                if (v.flight.modifiers.size() > 16)
                    bad("perks", "type '" + ut.id + "' weapon " + std::to_string(w) + ": its flight modifiers plus the bullet perks exceed 16 entries");
                if (p.variants.size() >= 126) bad("perks", "too many bullet perk combinations (at most 126 weapon x perk-set variants; use fewer bullet perks or tier chains)");
                p.variantOf[uint64_t(t) << 40 | uint64_t(w) << 32 | mask] = int(p.variants.size());
                p.variants.push_back(std::move(v));
            }
        }
    }
}
} // namespace

void loadProgressionRules(Rules& r, const Json& j0, EffectParser& ep) {
    const Json& j = j0["progression"];
    if (j.isNull()) return;
    if (!j.isObject()) bad("", "expected an object");
    auto pr = std::make_shared<ProgressionRules>();
    ProgressionRules& p = *pr;
    p.enabled = true;
    keysOnly(j, "", {"stats", "perks", "units", "deck", "xp", "offers", "summonPoints",   // library format (PerksData.h)
                     "players", "killXp", "orbs", "orbOwner", "autoOpen", "offerChoices", "offerRerolls", "rerollAvoidsRepeat",
                     "maxLevel", "xpGrowthPerLevel", "maxRecruitsPerSlot", "maxSquad", "pickups", "summons", "squad", "critDamage",
                     "decks", "defaultDeck", "offerAt"});
    // ---- library definitions (a copy without the engine keys of units[] and perks[])
    Json lib = Json::object();
    for (const char* k : {"stats", "perks", "units", "deck", "xp", "offers", "summonPoints"}) if (j.has(k)) lib.set(k, j[k]);
    std::vector<std::pair<int64_t, std::string>> unitTypes;
    if (Json* units = lib.find("units"); units && units->isArray())
        for (size_t i = 0; i < units->size(); ++i) {
            Json& u = units->at(i);
            if (!u.isObject()) continue;
            const std::string where = "units[" + std::to_string(i) + "]";
            if (!u["type"].isString()) bad(where + ".type", "expected the unit type id");
            unitTypes.push_back({u.getInt("id", 0), u.getStr("type")});
            u.erase("type");
        }
    std::vector<std::pair<size_t, Json>> perkExt;   // (perks index, {bullet, onHit, onHitChance})
    if (Json* perks = lib.find("perks"); perks && perks->isArray())
        for (size_t i = 0; i < perks->size(); ++i) {
            Json& pj = perks->at(i);
            if (!pj.isObject()) continue;
            Json e = Json::object();
            for (const char* k : {"bullet", "onHit", "onHitChance"}) if (pj.has(k)) { e.set(k, pj[k]); pj.erase(k); }
            if (e.size()) perkExt.push_back({i, std::move(e)});
        }
    try {
        p.defs = lg::readPerkDefinitions(lib);
        p.points = lg::readSummonPoints(lib);
    } catch (const JsonError& e) { bad("", e.what()); }
    touch(j);
    // deck units -> unit types
    p.typeUnit.assign(r.types.size(), 0);
    for (auto& [id, name] : unitTypes) {
        const int t = r.findType(name);
        if (t < 0) bad("units", "unit " + std::to_string(id) + ": unknown type '" + name + "'");
        if (p.typeUnit[size_t(t)]) bad("units", "type '" + name + "' is used by two deck units");
        p.unitType[id] = uint16_t(t);
        p.typeUnit[size_t(t)] = id;
    }
    if (p.defs.units.empty()) bad("units", "at least one deck unit (the hero: {\"id\": 1, \"type\": \"...\", \"hero\": true})");
    // perk engine keys
    for (auto& [i, e] : perkExt) {
        const Json& raw = j["perks"][i];
        const int64_t id = raw.getInt("id", 0);
        const std::string where = "perk " + std::to_string(id);
        ProgPerkExt x;
        if (e.has("bullet")) {
            keysOnly(e["bullet"], where + ".bullet", {"modifiers"});
            auto f = projectile::parseProjectileFlight(e["bullet"], "progression." + where + ".bullet");
            if (f.modifiers.empty()) bad(where + ".bullet", "expected modifiers");
            x.bullets = std::move(f.modifiers);
        }
        if (e.has("onHit")) x.onHit = int16_t(ep.payload("progression." + where + " onHit", e["onHit"]));
        if (e.has("onHitChance")) {
            x.onHitChance = e["onHitChance"].fx();
            if (x.onHitChance.raw < 0 || x.onHitChance > Fixed::fromInt(1)) bad(where + ".onHitChance", "must be 0..1");
        }
        p.ext[id] = std::move(x);
    }
    for (auto& [id, x] : p.ext) if (!x.bullets.empty()) p.bulletPerks.push_back(id);
    if (p.bulletPerks.size() > 6) bad("perks", "at most 6 perks with bullet modifiers");
    // ---- configs (library settings names: docs/genre-packages.md § Match progression)
    auto num = [&](const Json& o, const char* key, int64_t def, int64_t lo, int64_t hi, const std::string& where) -> int64_t {
        if (!o.has(key)) return def;
        if (!o[key].isNumber()) bad(where + key, "expected a number");
        int64_t v = o[key].integer(def);
        if (v < lo || v > hi) bad(where + key, "must be " + std::to_string(lo) + ".." + std::to_string(hi));
        return v;
    };
    p.perk.choices = uint32_t(num(j, "offerChoices", 3, 1, 8, ""));
    p.perk.rerolls = uint32_t(num(j, "offerRerolls", 0, 0, 100, ""));
    p.perk.rerollAvoidsRepeat = j.getBool("rerollAvoidsRepeat", true);
    p.perk.maxLevel = uint32_t(num(j, "maxLevel", 0, 0, 1000, ""));
    p.perk.xpGrowth = num(j, "xpGrowthPerLevel", 0, 0, 1000000000, "");
    p.perk.maxRecruitsPerSlot = uint32_t(num(j, "maxRecruitsPerSlot", 0, 0, 1000, ""));
    p.perk.maxSquad = uint32_t(num(j, "maxSquad", 15, 0, 1000, ""));
    if (auto e = lg::perkConfigError(p.perk); !e.empty()) bad("", e);
    // times are kept in milliseconds and speeds per second here; Progression::setup converts them to ticks
    if (j.has("pickups")) {
        const Json& pk = j["pickups"];
        keysOnly(pk, "pickups", {"radius", "magnetRadius", "grabRadius", "attractSpeed", "attractAccel", "maxSpeed", "lifetime", "landing", "maxOrbs"});
        p.pickup.collectRadius = pk.getFx("radius", p.pickup.collectRadius);
        p.pickup.magnetRadius = pk.getFx("magnetRadius", p.pickup.magnetRadius);
        p.pickup.grabRadius = pk.getFx("grabRadius", p.pickup.grabRadius);
        p.pickup.attractSpeed = pk.getFx("attractSpeed", p.pickup.attractSpeed);   // units / s (per tick at setup)
        p.pickup.attractAccel = pk.getFx("attractAccel", p.pickup.attractAccel);   // units / s^2
        p.pickup.maxSpeed = pk.getFx("maxSpeed", Fixed::fromInt(40));
        p.pickup.lifetime = uint32_t(milli(seconds(pk, "lifetime", Fixed{}, "pickups")));   // ms (ticks at setup)
        p.pickup.landing = uint32_t(milli(seconds(pk, "landing", Fixed{}, "pickups")));
        p.pickup.maxOrbs = uint32_t(num(pk, "maxOrbs", 1024, 1, 65536, "pickups."));
    } else p.pickup.maxSpeed = Fixed::fromInt(40);
    if (j.has("summons")) {
        const Json& sm = j["summons"];
        keysOnly(sm, "summons", {"progress", "cooldown", "maxAllies", "charges", "chargeRecharge", "resetOnExit"});
        p.summon.progressTicks = uint32_t(milli(seconds(sm, "progress", Fixed::fromInt(9), "summons")));   // ms (ticks at setup)
        p.summon.cooldownTicks = uint32_t(milli(seconds(sm, "cooldown", Fixed{}, "summons")));
        p.summon.maxAllies = uint32_t(num(sm, "maxAllies", 15, 0, 65536, "summons."));
        p.summon.charges = uint32_t(num(sm, "charges", 0, 0, 1000, "summons."));
        p.summon.chargeRecharge = uint32_t(milli(seconds(sm, "chargeRecharge", Fixed{}, "summons")));
        p.summon.resetOnExit = sm.getBool("resetOnExit", true);
    } else p.summon.progressTicks = 9000;
    if (j.has("squad")) {
        const Json& sq = j["squad"];
        keysOnly(sq, "squad", {"follow", "lifetime"});
        p.allyFollow = sq.getFx("follow", p.allyFollow);
        p.allyLifetimeSeconds = seconds(sq, "lifetime", Fixed{}, "squad");
    }
    p.critDamageMilli = num(j, "critDamage", 2000, 0, 1000000, "");
    // ---- players, kills, orbs
    for (auto& s : j["players"].items()) {
        const int64_t v = s.integer(0);
        if (v < 1 || v > 64) bad("players", "player slots 1..64");
        p.players.push_back(uint8_t(v));
    }
    std::sort(p.players.begin(), p.players.end());
    p.players.erase(std::unique(p.players.begin(), p.players.end()), p.players.end());
    p.killXp.assign(r.types.size(), 0);
    if (j.has("killXp")) {
        const Json& kx = j["killXp"];
        if (kx.isNumber()) { const int64_t v = num(j, "killXp", 0, 0, 1000000000, ""); std::fill(p.killXp.begin(), p.killXp.end(), v); }
        else {
            if (!kx.isObject()) bad("killXp", "expected xp per kill or {\"default\": n, \"<type>\": n}");
            const int64_t def = num(kx, "default", 0, 0, 1000000000, "killXp.");
            std::fill(p.killXp.begin(), p.killXp.end(), def);
            for (auto& [k, v] : kx.members()) {
                if (k == "default") continue;
                const int t = r.findType(k);
                if (t < 0) bad("killXp", "unknown type '" + k + "'");
                p.killXp[size_t(t)] = num(kx, k.c_str(), 0, 0, 1000000000, "killXp.");
            }
        }
    }
    p.orbs = j.getBool("orbs", true);
    const std::string owner = j.getStr("orbOwner", "anyone");
    if (owner != "anyone" && owner != "killer") bad("orbOwner", "expected 'anyone' or 'killer'");
    p.orbsForKiller = owner == "killer";
    p.autoOpen = j.getBool("autoOpen", true);
    const std::string at = j.getStr("offerAt", "anywhere");
    if (at != "anywhere" && at != "summonPoints") bad("offerAt", "expected 'anywhere' or 'summonPoints'");
    p.offerAtCircles = at == "summonPoints";
    if (j.has("decks") && !j["decks"].isObject() && !j["decks"].isArray()) bad("decks", "expected payloads by player ({\"1\": {...}}) or a list with \"player\"");
    p.decks = j["decks"];
    p.defaultDeck = j["defaultDeck"];
    if (!p.defaultDeck.isNull()) {
        try { lg::readBattleDeck(p.defaultDeck); } catch (const JsonError& e) { bad("defaultDeck", e.what()); }
    }
    buildVariants(r, p);
    p.defsHash = lg::perkDefinitionsHash(p.defs);
    p.configHash = lg::progressionConfigHash(p.perk, p.pickup, p.summon);
    r.progression = std::move(pr);
}

} // namespace aiverse::game
