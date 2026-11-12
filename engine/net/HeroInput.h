// Direct-control hero input: the one wire format for top-down heroes (docs/topdown.md, docs/battle-net.md).
//
// Two command types, reserved by the engine (modules choose ids below 0xF000):
//   kCmdHeroInput   (0xF100) = RtsModule kCmdTopDown: sticky stick / aim / fire state, topdown::pack(Input)
//   kCmdHeroAbility (0xF101) = RtsModule kCmdTopDownAbility: one ability button event, topdown::abilityParam(...)
//
// The rollback session predicts "no new commands" for remote players. Hero input is therefore STICKY STATE: a
// kCmdHeroInput command carries the whole quantized stick / aim / button state and the simulation keeps it until the
// next one. "No command" then means "same input as before", so predicting remote heroes = repeating their last input
// (dead reckoning), a misprediction only happens when a remote player actually changes input, and a still player
// costs no bandwidth. Button events (tap, press, release, cancel; with an optional stick direction / distance or
// point) are separate kCmdHeroAbility commands.
//
// Angles are binary turns: 0 = +Z, 16384 = +X, i.e. clockwise seen from above; the ground plane is (x, z).
// Floats exist only on the client side (TopDownControls / HeroInputQuantizer turn a touch stick into integers);
// everything the simulation reads is integers / Fixed (moveVector, aimVector).
#pragma once
#include <cmath>
#include <cstdint>
#include <optional>
#include "../core/FixedMath.h"
#include "../sim/Command.h"

namespace aiverse {

namespace topdown {
constexpr int64_t kAngleSteps = 65536;
constexpr int64_t kMagMax = 1000;                  // per mille
constexpr int64_t kAim = int64_t(1) << 42;          // aim direction present (aim stick / mouse held)
constexpr int64_t kFire = int64_t(1) << 43;         // fire button held (fires without aiming, or when aiming does not fire)

struct Input {
    int32_t moveAngle = 0, moveMag = 0;             // [0, 65536), [0, 1000]
    int32_t aimAngle = 0;
    bool aim = false, fire = false;
    bool operator==(const Input&) const = default;
};
// 16 bits move angle, 10 bits magnitude, 16 bits aim angle, aim bit, fire bit.
inline int64_t pack(const Input& in) {
    int64_t p = int64_t(in.moveAngle & 0xFFFF) | (int64_t(in.moveMag < 0 ? 0 : in.moveMag > kMagMax ? kMagMax : in.moveMag) << 16) |
                (int64_t(in.aimAngle & 0xFFFF) << 26);
    if (in.aim) p |= kAim;
    if (in.fire) p |= kFire;
    return p;
}
inline Input unpack(int64_t p) {   // hostile bit patterns are clamped, never trusted
    Input in;
    in.moveAngle = int32_t(p & 0xFFFF);
    in.moveMag = int32_t((p >> 16) & 0x3FF);
    if (in.moveMag > kMagMax) in.moveMag = int32_t(kMagMax);
    in.aimAngle = int32_t((p >> 26) & 0xFFFF);
    in.aim = (p & kAim) != 0;
    in.fire = (p & kFire) != 0;
    return in;
}
// Nearest of `sectors` evenly spaced directions (sectors <= 0: full binary-turn resolution).
inline int32_t quantizeAngle(int64_t angle, int sectors) {
    angle = ((angle % kAngleSteps) + kAngleSteps) % kAngleSteps;
    if (sectors <= 0 || sectors >= kAngleSteps) return int32_t(angle);
    int64_t s = (angle * sectors * 2 + kAngleSteps) / (kAngleSteps * 2);   // round to the nearest sector
    return int32_t((s % sectors) * kAngleSteps / sectors);
}
// Magnitude rounded up to `steps` levels (Drone Fall: ceil(m / step) * step); at or below the dead zone: 0.
inline int32_t quantizeMag(int64_t mag, int steps, int64_t deadzone) {
    if (mag <= deadzone || mag <= 0) return 0;
    if (mag > kMagMax) mag = kMagMax;
    if (steps <= 0) return int32_t(mag);
    int64_t s = (mag * steps + kMagMax - 1) / kMagMax;
    return int32_t(s * kMagMax / steps);
}
// Ground-plane unit vector (x, z) of a binary-turn angle, fixed point (Q16 turns: one binary step = one raw unit).
inline Vec2 directionXZ(int32_t angle) {
    const Fixed t = Fixed::fromRaw(int64_t(angle & 0xFFFF));
    return {fxSinTurns(t), fxCosTurns(t)};
}
inline Vec2 moveVector(const Input& in) {        // (x, z) * magnitude
    if (in.moveMag <= 0) return {};
    const Vec2 d = directionXZ(in.moveAngle);
    const Fixed m = Fixed::fromRatio(in.moveMag, kMagMax);
    return {d.x * m, d.y * m};
}
inline Vec2 aimVector(const Input& in) { return in.aim ? directionXZ(in.aimAngle) : Vec2{}; }

// ---- ability button events (kCmdHeroAbility param)
enum class Phase : uint8_t {
    Tap = 0,       // cast now (the only phase older clients send: param = ability index)
    Press = 1,     // button down: hold abilities start repeating; aimed abilities remember the aim
    Release = 2,   // button up: aimed abilities cast along the button's stick (hold abilities stop)
    Cancel = 3,    // finger dragged back onto the button: nothing is cast
};
constexpr int kEscapeAbility = 255;                   // hero escape mode (rules topDown.escape), not a hero type ability
constexpr int64_t kAbilityAngle = int64_t(1) << 10;   // bits 16..31 carry a direction (the button's stick)
constexpr int64_t kAbilityPoint = int64_t(1) << 11;   // Command::point is meant even when it is (0, 0, 0)
struct Ability {
    int index = 0;                  // 0..7 hero ability, kEscapeAbility; -1 = garbage
    Phase phase = Phase::Tap;
    bool hasAngle = false, hasPoint = false;
    int32_t angle = 0;              // binary turns
    int32_t distance = 0;           // per mille of the ability range (0: full range)
    bool operator==(const Ability&) const = default;
};
inline int64_t abilityParam(const Ability& a) {
    int64_t p = int64_t(a.index & 0xFF) | (int64_t(uint8_t(a.phase) & 3) << 8) | (int64_t(a.angle & 0xFFFF) << 16) |
                (int64_t(a.distance < 0 ? 0 : a.distance > kMagMax ? kMagMax : a.distance) << 32);
    if (a.hasAngle) p |= kAbilityAngle;
    if (a.hasPoint) p |= kAbilityPoint;
    return p;
}
inline Ability unpackAbility(int64_t p) {
    Ability a;
    a.index = int(p & 0xFF);
    a.phase = Phase((p >> 8) & 3);
    a.hasAngle = (p & kAbilityAngle) != 0;
    a.hasPoint = (p & kAbilityPoint) != 0;
    a.angle = a.hasAngle ? int32_t((p >> 16) & 0xFFFF) : 0;
    a.distance = int32_t((p >> 32) & 0x3FF);
    if (a.distance > kMagMax) a.distance = int32_t(kMagMax);
    if (p < 0 || (p >> 42) != 0) a.index = -1;      // garbage in the high bits: not a valid ability event
    return a;
}
} // namespace topdown

namespace net {

constexpr uint16_t kCmdHeroInput = 0xF100;     // = RtsModule kCmdTopDown (sticky topdown::Input)
constexpr uint16_t kCmdHeroAbility = 0xF101;   // = RtsModule kCmdTopDownAbility (topdown::Ability)
constexpr uint16_t kCmdHeroAction = kCmdHeroAbility;   // deprecated stage-1 name

inline Command heroInputCommand(uint8_t player, const topdown::Input& in) {
    Command c; c.player = player; c.type = kCmdHeroInput; c.param = topdown::pack(in);
    return c;
}
inline Command heroAbilityCommand(uint8_t player, const topdown::Ability& a, Vec3 point = {}) {
    Command c; c.player = player; c.type = kCmdHeroAbility; c.param = topdown::abilityParam(a); c.point = point;
    return c;
}

// Client side: sticks as world ground directions (x, z), length <= 1 -> quantized input; update() returns the input
// only when it changed (send it as heroInputCommand). Hysteresis is not needed: a sector boundary costs one tiny
// command. The simulation re-quantizes every command with its rules, so these settings only save bandwidth.
struct HeroInputQuantizer {
    int sectors = 45;                // Drone Fall GD_Input
    int magnitudeSteps = 1;
    float deadzone = 0.05f;          // stick length at or below this = not moving / aim released
    topdown::Input last;
    bool sentOnce = false;

    static int32_t angleOf(float x, float z) {
        double a = std::atan2(double(x), double(z)) / 6.283185307179586;   // 0 = +z, a quarter turn = +x
        if (a < 0) a += 1.0;
        return int32_t(std::lround(a * double(topdown::kAngleSteps))) & 0xFFFF;
    }
    topdown::Input quantize(float mx, float mz, float ax, float az, bool fire) const {
        topdown::Input s;
        const float len = std::sqrt(mx * mx + mz * mz);
        const int64_t mag = int64_t(std::lround(std::fmin(1.0f, len) * float(topdown::kMagMax)));
        s.moveMag = topdown::quantizeMag(mag, magnitudeSteps, int64_t(deadzone * float(topdown::kMagMax)));
        if (s.moveMag > 0) s.moveAngle = topdown::quantizeAngle(angleOf(mx, mz), sectors);
        if (std::sqrt(ax * ax + az * az) > deadzone) { s.aim = true; s.aimAngle = angleOf(ax, az); }
        s.fire = fire;
        return s;
    }
    std::optional<topdown::Input> update(float mx, float mz, float ax, float az, bool fire) {
        topdown::Input s = quantize(mx, mz, ax, az, fire);
        if (sentOnce && s == last) return std::nullopt;
        last = s; sentOnce = true;
        return s;
    }
};

} // namespace net
} // namespace aiverse
