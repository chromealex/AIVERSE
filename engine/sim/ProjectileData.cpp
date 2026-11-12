#include "ProjectileData.h"
#include <stdexcept>
#include <initializer_list>
namespace aiverse::projectile {
namespace {
[[noreturn]] void bad(const std::string& w, const std::string& s) { throw std::runtime_error(w + ": " + s); }
void keys(const Json& j, const std::string& w, std::initializer_list<const char*> allowed) {
    if (!j.isObject()) bad(w, "expected object");
    for (const auto& [k, v] : j.members()) {
        (void)v; bool found = false;
        for (auto a : allowed) if (k == a) found = true;
        if (!found) bad(w, "unknown field '" + k + "'");
    }
}
Fixed number(const Json& j, const char* k, Fixed d, Fixed lo, Fixed hi, const std::string& w) {
    if (!j.has(k)) return d;
    if (!j[k].isNumber()) bad(w, std::string(k) + " must be a number");
    auto v = j[k].fx();
    if (v < lo || v > hi) bad(w, std::string(k) + " out of range");
    return v;
}
uint16_t count(const Json& j, const char* k, int d, int lo, int hi, const std::string& w) {
    auto v = number(j, k, Fixed::fromInt(d), Fixed::fromInt(lo), Fixed::fromInt(hi), w);
    if (v.raw % Fixed::kOne) bad(w, std::string(k) + " must be an integer");
    return uint16_t(v.toInt());
}
bool boolean(const Json& j, const char* k, bool d, const std::string& w) {
    if (j.has(k) && j[k].type() != Json::Type::Bool) bad(w, std::string(k) + " must be boolean");
    return j.getBool(k, d);
}
}
ProjectileFlight parseProjectileFlight(const Json& j, const std::string& w) {
    ProjectileFlight f;
    keys(j, w, {"radius", "maxDistance", "lifetime", "homing", "blockTerrain", "splitBudget", "modifiers", "gravity", "verticalSpeed"});
    f.enabled = true;
    f.gravity = number(j, "gravity", Fixed{}, Fixed{}, 1000_fx, w);
    f.verticalSpeed = number(j, "verticalSpeed", Fixed{}, -1024_fx, 1024_fx, w);
    f.radius = number(j, "radius", f.radius, Fixed{}, 4_fx, w);
    f.maxDistance = number(j, "maxDistance", f.maxDistance, Fixed::fromRatio(1,100), 4096_fx, w);
    f.lifetime = number(j, "lifetime", f.lifetime, Fixed::fromRatio(1,100), 120_fx, w);
    f.homing = boolean(j, "homing", false, w); f.blockTerrain = boolean(j, "blockTerrain", true, w);
    f.splitBudget = count(j, "splitBudget", 64, 0, 256, w);
    if (j.has("modifiers") && (!j["modifiers"].isArray() || j["modifiers"].size() > 16)) bad(w, "modifiers must be an array of at most 16 entries");
    for (const auto& m : j["modifiers"].items()) {
        const auto at = w + " modifiers[" + std::to_string(f.modifiers.size()) + "]";
        ProjectileModifier d; auto kind = m.getStr("kind");
        if (kind == "damage" || kind == "speed" || kind == "radius") {
            keys(m, at, {"kind", "factor"});
            d.kind = kind == "damage" ? ProjectileModKind::Damage : kind == "speed" ? ProjectileModKind::Speed : ProjectileModKind::Radius;
            d.factor = number(m, "factor", 1_fx, kind == "speed" ? Fixed::fromRatio(1,100) : Fixed{}, 4_fx, at);
        } else if (kind == "split") {
            keys(m, at, {"kind", "count", "angle", "trigger", "after"});
            d.kind = ProjectileModKind::Split; d.count = count(m, "count", 2, 2, 8, at);
            d.angle = number(m, "angle", 30_fx, Fixed{}, 360_fx, at);
            if (m.has("trigger") && !m["trigger"].isString()) bad(at, "trigger must be a string");
            auto tr = m.getStr("trigger", "hit");
            if (tr != "hit" && tr != "time") bad(at, "trigger must be hit or time");
            d.timed = tr == "time";
            d.after = number(m, "after", Fixed{}, Fixed{}, 120_fx, at);
            if (!d.timed && m.has("after")) bad(at, "after requires trigger time");
        } else if (kind == "ricochet" || kind == "flyThrough") {
            const bool ricochet = kind == "ricochet";
            if (ricochet) keys(m, at, {"kind", "count", "radius"});
            else keys(m, at, {"kind", "count", "falloff"});
            d.kind = ricochet ? ProjectileModKind::Ricochet : ProjectileModKind::FlyThrough;
            d.count = count(m, "count", 1, 1, 32, at);
            if (ricochet) d.radius = number(m, "radius", 5_fx, Fixed::fromRatio(1,100), 128_fx, at);
            else d.factor = number(m, "falloff", 1_fx, Fixed{}, 1_fx, at);
        } else bad(at, "unknown modifier kind '" + kind + "'");
        f.modifiers.push_back(d);
    }
    return f;
}
}
