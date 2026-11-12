// Map environment (Bunker Wars / Mushroom Wars 2 "Environment" + point modules), docs/game-format.md
// § Environment: weather (rain puddles, snow, blizzard, lightning), directional wind, map zones, worms, toads,
// flying bombers, the Mouro boss director (actions / conditions / emotions), map scroller (moving play
// screen + scripted actions), auto skills moving along splines, off-map senders, linked caves, graveyards
// fed by souls, burning buildings, destructible walls, trample fields (grass / winter snow over grid fields).
//
// Data: rules "environment" (defaults) + map "environment" (merged key by key). Everything is optional:
// a game without the block has no Environment state and keeps its checksums and saves.
// State: the EnvState world resource (rollback copies it, saves append it, hashState hashes it).
// Determinism: Fixed math only, sim.rng() in a fixed order, units visited in ECS order.
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include "../../engine/core/ECS.h"
#include "../../engine/core/Fixed.h"

namespace aiverse { class Simulation; class Hasher; class Archive; class Json; }

namespace aiverse::game {

class RtsModule;
struct EffectCtx;

// ---------------------------------------------------------------- shared geometry
// BW SkillsSpline: Catmull-Rom through the control points (10 nodes per segment), walked by distance.
struct EnvSpline {
    std::string id;
    std::vector<Vec3> points, nodes;
    std::vector<Fixed> cum;      // distance at each node
    Fixed length{};
    bool loop = false;
    // auto skill (BW AutoSkills.SkillSpline): emits `skill` at the first point and moves it along
    int16_t skill = -1;          // player ability index (rules playerAbilities); -1 = path only (worms)
    Fixed velocity = Fixed::fromInt(100), emitPeriod = Fixed::fromInt(10), firstEmission = Fixed::fromInt(-1);
    bool enabled = true;
    void build(const std::vector<Vec3>& pts, bool loop, int smooth = 10);
    Vec3 at(Fixed d) const;      // by distance from the start, clamped to [0, length]
};

enum class EnvZoneKind : uint8_t { Water = 0, Road = 1, Toad = 2, Other = 3 };
struct EnvZone {
    std::string id;
    EnvZoneKind kind = EnvZoneKind::Other;
    std::vector<Vec3> poly;      // polygon (x, z); a rect / circle is converted to a polygon / radius
    Vec3 center{}; Fixed radius{};   // radius > 0: circle zone
    Fixed slowdown = Fixed::fromInt(1);   // BW ZoneType slowdown coefficient: speed / slowdown inside
    bool contains(Vec3 p) const;
};

// ---------------------------------------------------------------- definitions
struct EnvWeatherDef {
    bool rain = false, snow = false, lightning = false, blizzard = false, winter = false;
    Fixed puddlesFill = Fixed::fromInt(20), puddlesDry = Fixed::fromInt(20), rainWait = Fixed::fromInt(60), noRainWait = Fixed::fromInt(30);
    Fixed snowRestore = Fixed::fromRatio(5, 1000), snowWait = Fixed::fromInt(30), snowFillChance = Fixed::fromRatio(6, 10);
    Fixed snowFillMin = Fixed::fromInt(40), snowFillMax = Fixed::fromInt(60);
    Fixed blizzardWait = Fixed::fromInt(30), blizzardFill = Fixed::fromInt(40), blizzardKillSpeed = Fixed::fromInt(5);
    Fixed blizzardSpeed = Fixed::fromRatio(1, 2), blizzardResistMin = Fixed::fromInt(1), blizzardResistMax = Fixed::fromInt(2);
    Fixed lightningKillTime = Fixed::fromInt(2), lightningRange = Fixed::fromInt(80), lightningTime = Fixed::fromInt(30), lightningWait = Fixed::fromInt(30);
    int lightningKillChance = 70, lightningStructureKill = 10;
    std::string burnId;
    Fixed rainSpread = Fixed::fromRatio(1, 2);   // grassRainBurnRandomFactor: rain scales the fire spread of trample killFields
    std::string blizzardVar = "blizzard";      // trigger var set to 1 while the blizzard blows (agent-bw-ai: canMoveInBlizzard)
    int16_t burnField = -1; int32_t burnValue = 100; Fixed burnRadius{};   // lightning sets a grid field (fire / snow)
};
struct EnvWindDef {
    bool enabled = false;
    Fixed idleMin = Fixed::fromInt(5), idleMax = Fixed::fromInt(10), blowMin = Fixed::fromInt(5), blowMax = Fixed::fromInt(10);
    Fixed forceMin = Fixed::fromInt(40), forceMax = Fixed::fromInt(80);
};
struct EnvTrampleDef {          // GrassModule / WinterModule over a grid field (fixed 0..1)
    std::string fieldId, killId;  // grid field ids (rules "fields"), resolved at setup
    int16_t field = -1;
    Fixed perSecond{};          // added under every moving ground unit (winter: +0.1 trampled, grass: -scaleSpeed)
    std::vector<std::pair<Fixed, Fixed>> speed;   // winter: [health, mul] (last row with value <= health wins)
    Fixed lerpSpeed = Fixed::fromInt(-1);         // grass: speed lerp(1, lerpSpeed, value) while value > 0
    Fixed restore{};            // per second toward 0 while the weather's snow phase restores (winter)
    bool snowRestore = false;
    int16_t killField = -1; int32_t killAbove = 1;  // units on cells of killField >= killAbove die (burning grass)
    int16_t hideStatus = -1; Fixed hideAbove{};     // status while standing in value > hideAbove (grass hides units)
    bool skipRoads = true;                          // road zones: no speed table (BW winter)
};
struct EnvWormDef {
    std::string complexity;     // "" = any; else matches rules/match "complexity"
    std::vector<int> paths;     // spline indices
    std::vector<std::pair<Fixed, Fixed>> onGround, underground, army;   // BW WormConfig cycles [min, max]
    std::vector<uint8_t> targetPlayers;   // empty: every non-neutral
    Fixed speed = Fixed::fromInt(100), noUnits = Fixed::fromInt(1), catching = Fixed::fromInt(2), full = Fixed::fromRatio(11, 10), under = Fixed::fromRatio(1, 2);
    Fixed headSwitch = Fixed::fromRatio(1, 2), scanPeriod = Fixed::fromRatio(1, 5);
    Fixed killBegin{}, killEnd = Fixed::fromInt(20), killWidth = Fixed::fromInt(80), vision = Fixed::fromInt(200);
    Fixed traceWidth = Fixed::fromInt(80), traceEmit = Fixed::fromRatio(1, 2), traceDecay = Fixed::fromInt(2), traceSlowdown = Fixed::fromRatio(1, 2);
    Fixed minToBuilding = Fixed::fromInt(160), appearDistance = Fixed::fromInt(80), reverseOffset = Fixed::fromInt(50);
    Fixed armySpeed = Fixed::fromInt(2), appearTime = Fixed::fromInt(1), disappearTime = Fixed::fromInt(1);
    std::vector<Fixed> segments;   // tail shifts (distance kept behind the segment in front); [0] = head
    uint16_t armyUnit = 0xFFFF;    // unit type of the spawned army (default: neutral spawnUnit of any garrison)
    int16_t reverseSkill = -1;     // player ability cast (neutral) at the head while on the ground
    uint16_t look = 0xFFFF, lookTail = 0xFFFF;   // unit types drawn for the head / tail segments (view only)
};
struct EnvToadDef {
    Vec3 pos{};
    Fixed attack = Fixed::fromRatio(3, 10), postAttack = Fixed::fromRatio(2, 10), idleSwitch = Fixed::fromInt(5);
    int maxUnits = 30;
    Fixed explodeDelay{}, explosion = Fixed::fromRatio(1, 2);
    std::string zone;              // zone id (kind toad); "" = every toad zone
    uint16_t look = 0xFFFF;
};
struct EnvFlyerDef {               // BW FlyBomb
    std::vector<std::pair<Vec3, Vec3>> rally;
    Fixed velocity = Fixed::fromInt(300), dropMin = Fixed::fromInt(1), dropMax = Fixed::fromInt(5);
    int maxBombs = -1, health = 15;
    Fixed idleMin = Fixed::fromInt(3), idleMax = Fixed::fromInt(7), heightMin = Fixed::fromInt(170), heightMax = Fixed::fromInt(230);
    Fixed gravity = Fixed::fromInt(-500);
    Vec3 amplitude{Fixed::fromInt(40), Fixed{}, Fixed::fromInt(80)}, frequency{Fixed::fromRatio(1, 2), Fixed{}, Fixed::fromRatio(1, 2)};
    int16_t skill = -1;            // explosion (player ability, neutral)
    uint16_t type = 0xFFFF;        // the flying body (an air unit type, towers shoot it); 0xFFFF = no body
    uint16_t bombLook = 0xFFFF;    // view: type drawn for falling bombs
};
// Mouro boss director: action holders started in time order, each runs one ready action at a time.
struct EnvCondDef { uint8_t kind = 0; int count = 0; Fixed value{}; std::vector<int> points; bool byBot = false, byPlayer = false, win = false, lose = false; };
struct EnvActionDef {
    std::string name;
    uint8_t kind = 0;            // EnvActionKind
    int16_t skill = -1;          // player ability index
    Fixed duration = Fixed::fromInt(2), castAt = Fixed::fromInt(1), nextTimeout{};
    int minDomination = 0;       // condition: player owns at least N domination points
    Fixed moraleDiff{};          // AverageMoral: morale difference (bot - player) to trigger
    std::vector<int> sources, targets;   // AddUnits: map point indices
    int targetCount = 3, unitsPerPoint = 24;
    bool toPlayer = true, toBot = true;
    std::string animation;       // view: animation / emotion name
};
struct EnvHolderDef { std::vector<std::string> complexities; Fixed startsFrom{}, delay{}; std::vector<EnvActionDef> actions; };
struct EnvEmotionDef { std::string emotion; std::vector<EnvCondDef> conds; };
struct EnvMouroDef {
    bool enabled = false;
    std::vector<EnvHolderDef> holders;
    std::vector<EnvEmotionDef> emotions;
    Fixed emotionsCooldown{};
    uint8_t bot = 0, player = 0;  // slots (0: first bot / first human)
    uint16_t dominationType = 0xFFFF;   // structure type counted as a domination point
    Vec3 castPos{};               // where castAt / castRing skills land (BW: the Mouro cast object)
    int16_t morale = -1;          // resource counted as morale stars (AverageMoral)
    int16_t energy = -1;          // skill energy of the bot, refilled before every Mouro cast (BW ResetCastSkillValues)
};
struct EnvScrollAction {
    uint8_t kind = 0;            // EnvScrollKind
    uint8_t who = 0;             // 0 player, 1 bot, 2 neutral
    std::vector<int> points;
    Fixed seconds{}, score{};
    Vec3 pos{};
    int from = -1, to = -1, count = 0, step = 0;
    bool flag = false;
};
struct EnvOutmapMove { Fixed time{}; int from = -1, to = -1, count = 0; };
struct EnvOutmapDef { std::string complexity; Fixed repeat = Fixed::fromInt(-1); std::vector<EnvOutmapMove> moves; std::vector<int> sources; };
struct EnvGraveyardDef { int limit = 200; Fixed await = Fixed::fromInt(2); };
struct EnvSoulsDef { bool enabled = false; Fixed velocity = Fixed::fromInt(200), jump = Fixed::fromInt(500); int attackerDivider = 2, defenderDivider = 2; uint16_t look = 0xFFFF; };
struct EnvFireDef { bool enabled = false; Fixed duration = Fixed::fromInt(5); };
struct EnvWallDef { int point = -1; int hits = 10; Fixed destroyTime{}; std::vector<std::vector<int>> destructIf; bool ignoreTowers = false; };
struct EnvScreenDef { bool enabled = false; Vec3 size{}; Vec3 start{}; };

struct EnvMultiDef { int16_t skill = -1; int count = 0; bool random = false, transfer = true; };   // BW AutoSkills.MultiSplineSkill
struct EnvFogDef { bool memory = true; Fixed startSize{}, startTime{}; };   // BW FogOfWar: explored memory, start reveal

struct EnvDef {
    bool active = false;
    int screenY = -1;                        // BW screen y (up) along world z: -1 = up is -z (bomb fall arcs)
    Fixed scale = Fixed::fromInt(1);        // BW distance units per world unit (distances / speeds divide by it)
    std::string complexity;                  // match AI complexity ("" = any)
    EnvWeatherDef weather;
    EnvWindDef wind;
    std::vector<EnvZone> zones;
    std::vector<EnvSpline> splines;
    std::vector<EnvTrampleDef> trample;
    std::vector<EnvWormDef> worms;
    std::vector<EnvToadDef> toads;
    std::vector<EnvFlyerDef> flyers;
    EnvMouroDef mouro;
    std::vector<EnvScrollAction> scroller;
    EnvScreenDef screen;
    EnvOutmapDef outmap;
    EnvGraveyardDef graveyard;
    EnvSoulsDef souls;
    EnvFireDef fire;
    std::vector<EnvWallDef> walls;
    std::vector<EnvMultiDef> multis;
    EnvFogDef fog;
    std::vector<std::vector<int>> caves;     // linked caves: groups of map point indices sharing owner + garrison
    std::vector<int> noSendTargets;          // map point indices that never accept sends (graveyards)
    uint16_t graveyardType = 0xFFFF;         // structure type of graveyards (souls fly there)
};

enum EnvActionKind : uint8_t { kActCastAt = 0, kActCastRing, kActCaldron, kActSkeletons, kActAverageMorale, kActAddUnits, kActAnimation, kActDeath };
enum EnvCondKind : uint8_t { kCondCooldown = 0, kCondBadAttack, kCondDomination, kCondBotPointCaptured, kCondChance, kCondMaxPlays, kCondEndGame, kCondSkill, kCondActionDone };
enum EnvScrollKind : uint8_t { kScrWaitCaptured = 0, kScrWaitTime, kScrWaitScore, kScrWaitNoUnits, kScrMove, kScrConvertAll, kScrConvertOutside, kScrInvincible, kScrSend, kScrGoto };

// kEvEnv value = kind << 16 | data (presentation: weather / wind sounds, worm / toad / flyer / Mouro animations)
enum EnvEventKind : int64_t {
    kEnvWeather = 1,      // data: weather bits | phase << 8 (rain 1, snow 2, blizzard 4, lightning 8)
    kEnvLightning = 2,    // pos: strike, a: unit (if any)
    kEnvWind = 3,         // data: direction 0..7 (blow start) or 0xFF (calm)
    kEnvWorm = 4,         // data: worm index | state << 8, pos: head
    kEnvToadAttack = 5,   // a: target unit, pos: toad
    kEnvToadExplode = 6,
    kEnvBomb = 7,         // pos: drop point; data 0 drop, 1 explode
    kEnvMouroAction = 8,  // data: holder << 8 | action
    kEnvMouroEmotion = 9, // data: emotion index
    kEnvGraveyard = 10,   // a: graveyard, data: 1 = ready (blinking), 2 = sends
    kEnvSoul = 11,        // a: target structure, pos: from, data: 1 arrived
    kEnvScroll = 12,      // pos: screen position, data: step
    kEnvWall = 13,        // a: wall, data: 0 hit, 1 destroyed, 2 removed
};

// ---------------------------------------------------------------- state
struct EnvSegment { Fixed dist{}, rel{}; Vec3 pos{}, prev{}; };
struct EnvTrace { Vec3 pos{}, left{}, right{}; Fixed timer{}, rel{}; bool kill = true; };
struct EnvWormState {
    uint8_t state = 4;           // 0 no units, 1 catching, 2 full, 3 preparing, 4 underground, 5 await army, 6 army, 7 post army
    int path = -1; bool forward = true;
    Fixed timer{}, headTimer{}, scanTimer{}, traceTimer{};
    int upCounter = 0, downCounter = 0, armyCounter = 0;
    uint8_t headFrame = 0;
    bool armySpawned = false;
    uint32_t armyGroup = 0;
    Entity armyFrom = kNullEntity, armyTo = kNullEntity; int armyLane = -1; Fixed armyDist{};
    Vec3 armyPos{};
    std::vector<EnvSegment> seg;
    std::vector<EnvTrace> traces;
    Entity reverseArea = kNullEntity;
};
struct EnvToadState { Fixed idle{}, attack = Fixed::fromInt(-1), explode{}, explodeDelay{}; int units = 0; Entity target = kNullEntity; Vec3 lastTarget{}; uint8_t post = 0; };
struct EnvBomb { Vec3 start{}, vel{}, pos{}; Fixed t{}, height{}, lift{}; };   // lift: the bomber's height (BW towerShift)
struct EnvFlyerState { uint8_t state = 0; Fixed idle{}, fly{}, drop{}, length{}, height{}; int rally = 0, dropped = 0, health = 0; Vec3 from{}, to{}, pos{}; Entity body = kNullEntity; std::vector<EnvBomb> bombs; };
struct EnvActionState { uint8_t state = 0; Fixed timer{}, await{}; bool executed = false; };
struct EnvHolderState { Fixed time{}, delay{}, suspend{}; std::vector<uint16_t> order; std::vector<EnvActionState> actions; };
struct EnvMouroState {
    uint8_t mode = 0;            // 0 active, 1 win (laugh), 2 die
    int holder = 0, maxDomination = 0, actionsDone = 0;
    std::vector<EnvHolderState> holders;
    Fixed emotionCooldown{};
    std::vector<int> emotionPlays;
    int lastEmotion = -1;
};
struct EnvSoul { Vec3 from{}, to{}, vel{}; Fixed t{}, total{}, gravity{}; Entity target = kNullEntity; uint8_t owner = 0, targetOwner = 0; bool add = true; };
struct EnvSend { Entity from = kNullEntity, to = kNullEntity; int left = 0; uint8_t owner = 0; uint32_t group = 0; uint16_t unit = 0xFFFF;
                 int lane = -1; uint16_t lanePos = 0; bool reverse = false; Fixed speedMul = Fixed::fromInt(1); };   // lane >= 0: spawned on the lane (worm army)
struct EnvAuto { Fixed timer{}; bool late = false; struct Moving { Entity area = kNullEntity; Fixed dist{}; }; std::vector<Moving> moving; };
struct EnvCave { uint8_t owner = 0; int count = 0; };
struct EnvMulti { bool created = false; struct Moving { Entity area = kNullEntity; Fixed dist{}; int cur = 0, from = -1; bool transfer = false; }; std::vector<Moving> moving; };
struct EnvWall { Entity e = kNullEntity; int hits = 0; Fixed timer{}; uint8_t state = 0; };   // 0 standing, 1 destroyed (timer), 2 gone
struct EnvFire { Entity e = kNullEntity; Fixed timer{}, start{}, percent{}; };
struct EnvUnit { uint32_t gen = 0; Fixed blizzard = Fixed::fromInt(1), resist = Fixed::fromInt(1); bool init = false; };

struct EnvState {
    // weather (BW Environment.Module.Values.Data)
    uint8_t puddlesState = 0, snowState = 0, blizzardState = 0, lightningState = 0;
    Fixed puddles{}, rainTimer{}, snowTimer{}, snowFill{}, blizzardTimer{}, lightningTimer{}, lightningKill{};
    Vec3 lightningPos{};
    // wind
    Vec3 windForce{}; Fixed windBlow{}, windIdle{}; int windDir = 0, windVisual = -1;
    std::vector<EnvWormState> worms;
    std::vector<EnvToadState> toads;
    std::vector<EnvFlyerState> flyers;
    EnvMouroState mouro;
    std::vector<uint8_t> scrollerState;   // per action: 0 ready, 1 running, 2 done
    std::vector<Fixed> scrollerTimer;
    Vec3 screenPos{}, screenFrom{};
    std::vector<uint8_t> invincible;      // per player slot (scroller)
    Fixed outmapTimer{}; int outmapIndex = 0; bool outmapDone = false;
    std::vector<EnvAuto> autos;           // per spline with a skill
    std::vector<EnvSoul> souls;
    std::vector<EnvSend> sends;
    std::vector<EnvCave> caves;
    std::vector<Fixed> graveyardTimer;    // per graveyard (env graveyards list order)
    std::vector<Entity> graveyards;
    std::vector<std::vector<int>> lastAction;   // per player: map point indices, last = most recent (BW lastActionBuildings)
    std::vector<Fixed> soulAcc;           // per map point: attacker, defender fractional souls (2 per point)
    std::vector<EnvFire> fires;
    std::vector<EnvWall> walls;
    std::vector<Entity> points;           // map point index -> entity (resolved at setup; dead entities stay as they were)
    std::vector<uint32_t> minionGroups;   // groups spawned by worms / bosses (worms ignore them)
    std::vector<Fixed> minionSpeed;       // speed multiplier per minion group
    std::vector<EnvUnit> units;           // per entity index: blizzard health
    std::vector<uint64_t> noSoul;         // units killed this tick without a soul (lightning, toad, blizzard)
    std::vector<uint8_t> skillCast;       // per player: player abilities cast (Mouro emotions), cleared each tick
    std::vector<EnvMulti> multis;         // per multi-spline auto skill
    std::vector<std::vector<int32_t>> fogSeen;   // per player: cells visible after the last vision pass (fog without memory)
};

// ---------------------------------------------------------------- runtime
class Environment {
public:
    bool active() const { return def_.active; }
    const EnvDef& def() const { return def_; }
    // setup after setupMap (map entities exist); rules / map "environment" blocks
    void setup(Simulation& sim, RtsModule& m, const Json& rules, const Json& map);
    void tick(Simulation& sim);              // the "environment" system (before death)
    void lateTick(Simulation& sim);          // after death / capture: souls of the dead, caves, walls
    void fogTick(Simulation& sim);           // after vision: fog without explored memory, start reveal
    bool hasFog() const { return !def_.fog.memory || def_.fog.startTime.raw > 0; }
    bool invincible(Simulation& sim, uint8_t slot) const;   // map scroller setInvincible: never defeated
    // the bot's / player's slot of the scripted boss and scroller (0 player, 1 bot, 2 neutral)
    uint8_t slot(Simulation& sim, uint8_t who) const { return slotOf(sim, who); }
    void hash(Simulation& sim, Hasher& h) const;
    void io(Simulation& sim, Archive& a);
    EnvState* state(Simulation& sim) const;

    // ---- hooks (RtsModule)
    void speed(Simulation& sim, Entity e, Fixed& add, Fixed& mul) const;   // wind, puddles, zones, blizzard, worm traces, trample
    void onDeath(Simulation& sim, Entity e, uint8_t owner, Vec3 pos, bool silent);   // souls fly to the graveyard
    void onAssault(Simulation& sim, Entity structure, uint8_t attacker, uint8_t defender, int attackerLost, int defenderLost);
    bool sendAllowed(Simulation& sim, Entity from, Entity to) const;   // caves of one group, graveyards
    bool damageOverride(Simulation& sim, Entity target, Fixed& dmg);   // walls: one hit per projectile
    void onCaptured(Simulation& sim, Entity structure, uint8_t from, uint8_t to);
    void onSend(Simulation& sim, uint8_t player, Entity from);
    bool producing(Simulation& sim, Entity structure) const;           // map scroller: off-screen houses don't grow
    bool insideScreen(Simulation& sim, Vec3 p) const;
    // queries for the view / tests
    Fixed windMultiplier(Simulation& sim, Vec3 dir) const;
    int pointIndex(Simulation& sim, Entity e) const;
    Entity point(Simulation& sim, int index) const;
    // the drawn bodies of worms / toads / bombs / souls: (look type, position, facing) for the view
    struct Body { uint16_t type; Vec3 pos, facing; uint8_t kind; float alpha; };
    void bodies(Simulation& sim, std::vector<Body>& out) const;

private:
    void weather(Simulation& sim, EnvState& st);
    void wind(Simulation& sim, EnvState& st);
    void trample(Simulation& sim, EnvState& st);
    void units(Simulation& sim, EnvState& st);
    void worms(Simulation& sim, EnvState& st);
    void wormDigUp(Simulation& sim, EnvState& st, size_t i);
    void wormDigDown(Simulation& sim, EnvState& st, size_t i);
    bool wormArmySpot(Simulation& sim, EnvState& st, size_t i);
    void wormSpawnArmy(Simulation& sim, EnvState& st, size_t i);
    void toads(Simulation& sim, EnvState& st);
    void flyers(Simulation& sim, EnvState& st);
    void mouro(Simulation& sim, EnvState& st);
    bool mouroExecute(Simulation& sim, EnvState& st, const EnvActionDef& a);
    bool mouroCondition(Simulation& sim, EnvState& st, const EnvCondDef& c, const EnvActionDef* a, int emotion) const;
    void scroller(Simulation& sim, EnvState& st);
    void outmap(Simulation& sim, EnvState& st);
    void autos(Simulation& sim, EnvState& st);
    void multiAutos(Simulation& sim, EnvState& st);
    void soulsTick(Simulation& sim, EnvState& st);
    void sendsTick(Simulation& sim, EnvState& st);
    void caves(Simulation& sim, EnvState& st);
    void graveyards(Simulation& sim, EnvState& st);
    void firesTick(Simulation& sim, EnvState& st);
    void wallsTick(Simulation& sim, EnvState& st);
    // helpers
    Fixed range(Simulation& sim, Fixed lo, Fixed hi) const;           // uniform [lo, hi] from sim.rng()
    Fixed dist(Fixed v) const { return v / def_.scale; }             // BW units -> world
    void kill(Simulation& sim, EnvState& st, Entity e, bool noSoul);
    void cast(Simulation& sim, uint8_t player, int skill, Vec3 point, Entity target = kNullEntity, std::vector<Entity>* areas = nullptr);
    bool queueSend(Simulation& sim, EnvState& st, Entity from, Entity to, int count, bool unchecked, uint8_t owner, bool minion = false);
    void spawnSoul(Simulation& sim, EnvState& st, Vec3 from, uint8_t owner, Entity target);
    uint8_t slotOf(Simulation& sim, uint8_t who) const;              // 0 player, 1 bot, 2 neutral
    bool complexityOk(const std::string& c) const;
    std::vector<Entity> units_(Simulation& sim) const;               // live mobile ground units in ECS order

    RtsModule* m_ = nullptr;
    EnvDef def_;
    std::string matchComplexity_;
};

} // namespace aiverse::game
