// Direct-control hero input over the rollback session (top-down movement + aiming + shooting).
//
// The RTS rollback session predicts "no new commands" for remote players. Hero input is therefore
// sent as STICKY STATE: a kCmdHeroInput command carries the whole quantized stick/aim/button state
// and the simulation keeps it until the next one. "No command" then means "same input as before",
// so predicting remote heroes = repeating their last input (what ME.BECS-style games do), a
// mispredict only happens when a remote player actually changes input, and a still player costs no
// bandwidth. One-shot actions (ability / perk press and release, with an optional target point)
// are separate kCmdHeroAction commands.
//
// Floats exist only on the client side (HeroInputQuantizer turns a touch stick into integers);
// everything the simulation reads is integers / Fixed (heroMoveVector, heroAimVector).
// The hero controller module (agent-topdown-control) decodes these in GameModule::applyCommand.
#pragma once
#include <cmath>
#include <cstdint>
#include <optional>
#include "../core/FixedMath.h"
#include "../sim/Command.h"

namespace aiverse::net {

constexpr uint16_t kCmdHeroInput = 0xF100;    // reserved command type ids (modules choose < 0xF000)
constexpr uint16_t kCmdHeroAction = 0xF101;
constexpr uint32_t kAimReleased = 360000;     // aim in millidegrees; this value = stick released (Drone Fall's convention)

struct HeroInputState {
    uint16_t moveSector = 0;      // 0 = not moving; 1..sectors: direction (sector - 1) / sectors turns, counter-clockwise from +x
    uint8_t moveMagnitude = 0;    // 0..255 of full speed
    uint32_t aimMilli = kAimReleased;
    uint16_t held = 0;            // held buttons (bit 0 = fire by convention)
    bool operator==(const HeroInputState&) const = default;
};

// 16 + 8 + 20 + 16 bits in Command::param.
inline int64_t packHeroInput(const HeroInputState& s) {
    const uint64_t aim = s.aimMilli > kAimReleased ? kAimReleased : s.aimMilli;
    return int64_t(uint64_t(s.moveSector) | uint64_t(s.moveMagnitude) << 16 | aim << 24 | uint64_t(s.held) << 44);
}
inline HeroInputState unpackHeroInput(int64_t p) {
    const uint64_t v = uint64_t(p);
    HeroInputState s;
    s.moveSector = uint16_t(v & 0xffff);
    s.moveMagnitude = uint8_t((v >> 16) & 0xff);
    s.aimMilli = uint32_t((v >> 24) & 0xfffff);
    if (s.aimMilli > kAimReleased) s.aimMilli = kAimReleased;   // hostile input: clamp, never trust
    s.held = uint16_t((v >> 44) & 0xffff);
    return s;
}

inline Command heroInputCommand(uint8_t player, const HeroInputState& s) {
    Command c; c.player = player; c.type = kCmdHeroInput; c.param = packHeroInput(s);
    return c;
}
// phase: 0 press, 1 release, 2 tap. point is used when hasPoint (aimed abilities).
inline Command heroActionCommand(uint8_t player, uint16_t action, uint8_t phase, bool hasPoint = false, Vec3 point = {}) {
    Command c; c.player = player; c.type = kCmdHeroAction;
    c.param = int64_t(action) | int64_t(phase & 3) << 16 | int64_t(hasPoint ? 1 : 0) << 18;
    c.point = point;
    return c;
}
inline uint16_t heroActionIndex(const Command& c) { return uint16_t(c.param & 0xffff); }
inline uint8_t heroActionPhase(const Command& c) { return uint8_t((c.param >> 16) & 3); }
inline bool heroActionHasPoint(const Command& c) { return (c.param >> 18) & 1; }

// Simulation side: deterministic fixed-point vectors on the ground plane (x, y).
inline Vec2 heroMoveVector(const HeroInputState& s, uint16_t sectors) {
    if (!s.moveSector || !sectors || s.moveSector > sectors) return {};
    const Fixed turns = Fixed::fromRatio(s.moveSector - 1, sectors);
    const Fixed mag = Fixed::fromRatio(s.moveMagnitude, 255);
    return {fxCosTurns(turns) * mag, fxSinTurns(turns) * mag};
}
inline bool heroAiming(const HeroInputState& s) { return s.aimMilli < kAimReleased; }
inline Vec2 heroAimVector(const HeroInputState& s) {
    if (!heroAiming(s)) return {};
    const Fixed turns = Fixed::fromRatio(s.aimMilli, kAimReleased);
    return {fxCosTurns(turns), fxSinTurns(turns)};
}

// Client side: virtual stick -> quantized state; returns a state only when it changed (send it as
// heroInputCommand). Hysteresis is not needed: a sector boundary costs one tiny command.
struct HeroInputQuantizer {
    uint16_t sectors = 32;           // Drone Fall uses a configurable sector count too
    float deadzone = 0.15f;          // stick length below this = not moving / aim released
    uint8_t magnitudeSteps = 4;      // 1..255 distinct speeds
    uint32_t aimStepMilli = 1000;    // 1 degree
    HeroInputState last;
    bool sentOnce = false;

    HeroInputState quantize(float mx, float my, float ax, float ay, uint16_t held) const {
        HeroInputState s;
        s.held = held;
        const float len = std::sqrt(mx * mx + my * my);
        if (len > deadzone && sectors) {
            float a = std::atan2(my, mx) / 6.2831853f;
            if (a < 0) a += 1.0f;
            s.moveSector = uint16_t(1 + (uint32_t(std::lround(a * sectors)) % sectors));
            const float m = std::fmin(1.0f, (len - deadzone) / (1.0f - deadzone));
            const uint32_t steps = magnitudeSteps ? magnitudeSteps : 1;
            const uint32_t q = uint32_t(std::ceil(m * float(steps)));
            s.moveMagnitude = uint8_t(std::min<uint32_t>(255, q * 255 / steps));
            if (!s.moveMagnitude) s.moveMagnitude = uint8_t(255 / steps);
        }
        const float alen = std::sqrt(ax * ax + ay * ay);
        if (alen > deadzone) {
            float a = std::atan2(ay, ax) / 6.2831853f;
            if (a < 0) a += 1.0f;
            const uint32_t step = aimStepMilli ? aimStepMilli : 1;
            s.aimMilli = (uint32_t(std::lround(a * float(kAimReleased) / float(step))) * step) % kAimReleased;
        }
        return s;
    }
    std::optional<HeroInputState> update(float mx, float my, float ax, float ay, uint16_t held) {
        HeroInputState s = quantize(mx, my, ax, ay, held);
        if (sentOnce && s == last) return std::nullopt;
        last = s; sentOnce = true;
        return s;
    }
};

} // namespace aiverse::net
