// Top-down direct control (twin-stick heroes, Drone Fall style): rules "topDown" (docs/game-format.md § Top-down heroes).
//
// One hero per player is steered by a sticky move vector + optional aim direction instead of RTS orders. Input reaches
// the simulation only as kCmdTopDown commands whose payload is quantized integers (binary-turn angles, per-mille
// magnitude, re-quantized by the sim to the rules' sectors / steps), so lockstep, rollback and replays see exactly the
// same values on every peer. The input is sticky (sent on change only): a rollback client that predicts "no commands"
// for a remote player keeps that hero moving with its last input, which is the dead reckoning a shooter wants.
//
// State lives in the TopDownStore world resource (copied by rollback snapshots, hashed, saved by RtsSave), so games
// without the block keep their checksums and save layout. Heroes carry kUnitPossessed: RTS orders, the computer
// player's army code and automatic target acquisition leave them alone; this file moves them (acceleration, wall
// sliding over the map grid, push-out from other units), turns them and picks their weapon target (aim snap cone,
// auto-fire policy). The ordinary weapon system then fires at that target (cooldowns, projectiles, damage).
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"
#include "../../engine/data/Json.h"

namespace aiverse { class Simulation; class Hasher; class Archive; struct Command; }

namespace aiverse::game {

class RtsModule;

// ---- command payload (kCmdTopDown param). Angles are binary turns: 0 = +Z, 16384 = +X (clockwise seen from above).
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
inline int64_t pack(const Input& in) {
    int64_t p = int64_t(in.moveAngle & 0xFFFF) | (int64_t(in.moveMag < 0 ? 0 : in.moveMag > kMagMax ? kMagMax : in.moveMag) << 16) |
                (int64_t(in.aimAngle & 0xFFFF) << 26);
    if (in.aim) p |= kAim;
    if (in.fire) p |= kFire;
    return p;
}
inline Input unpack(int64_t p) {
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
} // namespace topdown

// ---- rules "topDown"
enum class RetargetMode : uint8_t { Nearest, Keep };

struct TopDownRules {
    bool enabled = false;
    std::vector<uint16_t> heroTypes;      // "heroes": the first own unit of one of these types is the player's hero
    Fixed respawn{};                      // seconds; 0: a dead hero stays dead
    // move
    Fixed accel = Fixed::fromInt(40), decel = Fixed::fromInt(60);   // units / s^2; 0 = instant
    Fixed turnRate{};                     // turns / s (data: degrees / s); 0 = instant facing
    bool collideUnits = true;
    // input quantization (clients use the same values; the sim re-quantizes every command)
    int sectors = 0, magnitudeSteps = 0;
    int32_t deadzone = 50;                // per mille
    // manual aim
    Fixed snapCos{};                      // cos(snapCone): aim snaps to the nearest target inside the cone
    bool snapOn = true, aimFires = true;
    Fixed aimRange{};                     // 0: weapon range
    // auto-fire
    bool autoFire = true, fireWhileMoving = false, lineOfFire = true, preferArmed = false;
    Fixed autoCos = Fixed::fromInt(-2);   // cos(cone / 2); < -1: all around
    Fixed autoRange{};                    // 0: weapon range
    RetargetMode retarget = RetargetMode::Nearest;
    Fixed retargetSeconds{};              // "interval": nearest-target re-evaluation period (0: every tick)
    uint32_t retargetTicks = 0;           // the same in ticks (setup)
    Fixed switchMargin{};
    std::vector<uint16_t> priority;       // types preferred over everything else in range (first = highest)
    Fixed movingSpeed = Fixed::fromRatio(1, 10);   // |velocity| above this counts as moving (fireWhileMoving)
};

struct TopDownPilot {
    Entity hero = kNullEntity;
    uint16_t heroType = UINT16_MAX;
    Vec3 home{};                          // respawn point (where the hero started)
    uint32_t respawnAt = 0;               // tick of a pending respawn (0: none)
    topdown::Input input;                 // sticky, already re-quantized
    uint32_t inputTick = 0;
    Vec3 vel{};
    Entity target = kNullEntity;
    uint32_t retargetAt = 0;
};

struct TopDownStore {
    std::vector<TopDownPilot> pilots;     // by player slot (0 unused)
};

class TopDown {
public:
    bool active() const { return rules_.enabled; }
    const TopDownRules& rules() const { return rules_; }
    // parses rules "topDown" (throws readable errors), adopts the heroes placed by the map / faction start
    void setup(Simulation& sim, RtsModule& m, const Json& rulesJson);
    bool command(Simulation& sim, const Command& cmd);   // true: kCmdTopDown / kCmdTopDownAbility handled
    void tick(Simulation& sim);                          // movement + targeting (inside the movement system)
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    TopDownStore* store(Simulation& sim) const;
    Entity heroOf(Simulation& sim, uint8_t player) const;
    bool isHero(Simulation& sim, Entity e) const;
    bool heroVelocity(Simulation& sim, Entity e, Vec3& vel) const;   // local avoidance: heroes are moving obstacles
    // Computer player: the steering command a bot of `player` would send now (false: nothing to change)
    bool botCommand(Simulation& sim, uint8_t player, Command& out);
    // what the sim makes of raw input (clients use it to avoid sending commands that change nothing)
    topdown::Input requantize(topdown::Input in) const { return requantize(rules_, in); }
    static topdown::Input requantize(const TopDownRules& r, topdown::Input in);
    static Vec3 direction(int32_t angle);                // unit vector of a binary-turn angle
    static int32_t angleOf(Vec3 dir);                    // binary-turn angle of a ground direction (0 for a zero vector)
    // Read-only (client aim assist, docs/topdown.md): what the hero of `player` could shoot now by the manual-aim
    // rules (hostile, alive, visible, a layer its weapons hit, line of fire) within `range` (0: aim range, else the
    // weapon range), sorted by handle. Empty without a hero on the map (dead, at the wheel).
    struct AimCandidate { Entity e; Vec3 pos; Fixed dist; };
    std::vector<AimCandidate> aimCandidates(Simulation& sim, uint8_t player, Fixed range = {}) const;

    // rules "topDown" with type names resolved against `typeIds` (throws readable errors)
    static TopDownRules parse(const Json& j, const std::vector<std::string>& typeIds);

private:
    void adopt(Simulation& sim, TopDownPilot& p, Entity e);
    void respawnAndAdopt(Simulation& sim, TopDownStore& st);
    void moveHero(Simulation& sim, TopDownPilot& p);
    void aimHero(Simulation& sim, TopDownPilot& p, uint8_t player);
    bool validTarget(Simulation& sim, Entity hero, uint8_t owner, Entity cand, Fixed range, Vec3 coneDir, Fixed coneCos, Fixed* dist) const;
    bool lineOfFire(Simulation& sim, Vec3 from, Entity target) const;
    Fixed weaponRange(Simulation& sim, Entity hero) const;
    RtsModule* m_ = nullptr;
    TopDownRules rules_;
};

} // namespace aiverse::game
