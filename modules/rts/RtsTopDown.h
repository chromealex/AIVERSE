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
#include "../../engine/net/HeroInput.h"

namespace aiverse { class Simulation; class Hasher; class Archive; struct Command; }

namespace aiverse::game {

class RtsModule;

// ---- command payloads: the engine's hero wire format (engine/net/HeroInput.h). kCmdTopDown = net::kCmdHeroInput
// carries topdown::pack(Input) (binary-turn angles: 0 = +Z, 16384 = +X), kCmdTopDownAbility = net::kCmdHeroAbility
// carries topdown::abilityParam(Ability) (index, tap / press / release / cancel, optional stick direction).
namespace topdown = ::aiverse::topdown;

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
    // free fire (aim.freeFire): aiming / fire held with no target fires the weapon along the aim; the shot flies the
    // weapon range and stops on the first hostile body or wall (Drone Fall). Off: the hero only shoots at targets.
    bool freeFire = false;
    // abilities: "hold" abilities repeat while their button is held; windups (castTime) run in the sim
    std::vector<std::string> holdIds;     // "abilities.hold": ability ids (of any hero type)
    bool rootWhileCasting = false;        // "abilities.rootWhileCasting": no movement during a windup
    // squad ("squad"): own units of these types follow the hero in a ring (Drone Fall Wander + unit group)
    struct Squad {
        bool enabled = false;
        std::vector<uint16_t> types;
        int size = 0;                     // members per hero (0: every own unit of the types)
        Fixed radius = Fixed::fromInt(2); // ring radius around the hero
        Fixed tolerance = Fixed::fromRatio(1, 2);   // closer than this to its slot: a member stops
        bool rotate = true;               // the ring turns with the hero's facing
        bool fireWhileMoving = false;     // Drone Fall SupressFireOnMove: members hold fire while the hero moves
        uint32_t repathTicks = 5;         // "repath" seconds -> ticks: slot refresh period per member
        Fixed repathSeconds = Fixed::fromRatio(1, 4);
    } squad;
    // escape ("escape"): the player picks a point within speed * duration (client preview, Drone Fall HeroEscape);
    // the sim validates it and moves the hero and its squad there, then the ability cools down
    struct Escape {
        bool enabled = false;
        Fixed duration = Fixed::fromInt(5);   // seconds of preview movement (the budget = hero speed * duration)
        Fixed cooldown = Fixed::fromInt(30);
        bool pause = true;                // client: a single-player match pauses while the point is chosen
    } escape;
    bool botProgression = false;          // "bot": {"progression": true}: skirmish bots walk to xp orbs / summon circles
                                          // first (raid bots always do)
    bool extended = false;                // any of the blocks above: extra pilot state is hashed / saved
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
    // extended state (rules.extended only: hashed and saved then)
    int8_t held = -1;                     // ability whose button is held (press .. release)
    int32_t heldAngle = 0;                // the button's stick direction (binary turns), heldDir: present
    bool heldDir = false;
    int8_t casting = -1;                  // ability in its windup
    uint32_t castAt = 0;                  // tick the windup ends
    Entity castTarget = kNullEntity;
    Vec3 castPoint{};
    uint32_t escapeReadyAt = 0;           // escape cooldown (per player: survives hero replacement)
    std::vector<Entity> squad;            // followers in slot order (alive ones first)
};

// Ability button event in sim terms (kCmdTopDownAbility after decoding / clamping)
struct TopDownCast { int index = -1; Entity target = kNullEntity; Vec3 point{}; bool havePoint = false; bool haveDir = false; int32_t angle = 0; int32_t distance = 0; };

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
    // Computer pilot toward a goal (raid bot, any AI that would give the hero a move order): walk the path to `goal`
    // until within `stand` (then stop, so channels / interactions work), and give ground to armed foes that come
    // closer than `kite` (0: 35% of the weapon range) on the way. false: the input would not change.
    bool steerCommand(Simulation& sim, uint8_t player, Vec3 goal, Fixed stand, Command& out, Fixed kite = Fixed{});
    // squad followers of a player's hero (alive, slot order); isFollower: a squad member of anyone
    std::vector<Entity> squadOf(Simulation& sim, uint8_t player) const;
    bool isFollower(Simulation& sim, Entity e) const;
    // escape: the furthest a player may go now (0: not available) and whether `to` is a valid destination
    Fixed escapeBudget(Simulation& sim, uint8_t player) const;
    bool escapeAllowed(Simulation& sim, uint8_t player, Vec3 to, std::string* why = nullptr) const;
    uint32_t escapeReadyAt(Simulation& sim, uint8_t player) const;
    // ring slot of member i of n around `centre` (facing `facingAngle`), pulled in front of walls (the view previews it)
    Vec3 squadSlot(Simulation& sim, Vec3 centre, int32_t facingAngle, int i, int n, Fixed memberRadius) const;
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
    void tickSquad(Simulation& sim, TopDownPilot& p, uint8_t player);
    void adoptSquad(Simulation& sim, TopDownStore& st);
    void tickCasts(Simulation& sim, TopDownPilot& p);
    bool requestCast(Simulation& sim, TopDownPilot& p, const TopDownCast& c);   // windup or cast now
    void castNow(Simulation& sim, TopDownPilot& p, const TopDownCast& c);
    void doEscape(Simulation& sim, TopDownPilot& p, uint8_t player, Vec3 to);
    void freeFire(Simulation& sim, TopDownPilot& p, uint8_t owner);
    Vec3 walkDir(Simulation& sim, Entity hero, Vec3 to) const;   // straight when walkable, else the next path waypoint
    bool holds(Simulation& sim, Entity hero, int index) const;   // a hold ability (repeats while pressed)
    bool walkable(Simulation& sim, Vec3 from, Vec3 to, Fixed radius, uint8_t layer, Vec3* stop) const;   // straight walk; stop: before the first block
    void aimHero(Simulation& sim, TopDownPilot& p, uint8_t player);
    bool validTarget(Simulation& sim, Entity hero, uint8_t owner, Entity cand, Fixed range, Vec3 coneDir, Fixed coneCos, Fixed* dist) const;
    bool lineOfFire(Simulation& sim, Vec3 from, Entity target) const;
    Fixed weaponRange(Simulation& sim, Entity hero) const;
    RtsModule* m_ = nullptr;
    TopDownRules rules_;
};

} // namespace aiverse::game
