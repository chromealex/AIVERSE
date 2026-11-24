// Generic, data-driven RTS gameplay module. One implementation for every game built on the engine.
// Implementation is split over several files:
//   GameCore.cpp    setup, commands, queries, hashing
//   Economy.cpp   resources, capacity/power, fields, production, research, construction, garrisons
//   Units.cpp     orders, movement, gathering, building behaviour
//   Combat.cpp    targeting, weapons, projectiles, abilities, buffs, heroes, squads, death
//   World.cpp     vision (with heights), creep, control points, regions, waves, neutrals, victory
#pragma once
#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "../sim/CommonComponents.h"
#include "../sim/Simulation.h"
#include "GameData.h"
#include "Components.h"
#include "Stats.h"
#include "GameState.h"
#include "GameHost.h"
#include "AIControllers.h"
#include "MapGrid.h"
#include "Colliders.h"
#include "Triggers.h"
#include "Script.h"
#include "Mech.h"
#include "GridFields.h"
#include "Raid.h"
#include "Native.h"
#include "TopDown.h"
#include "Encounters.h"
#include "Environment.h"
#include "Driving.h"
#include "Commerce.h"
#include "Career.h"
#include "Prompts.h"
#include "Sensors.h"
#include "HostSystems.h"
#include "FieldOps.h"
#include "Routes.h"
#include "Arsenal.h"
#include "Traffic.h"
#include <memory>

namespace aiverse::game {

// ---------------------------------------------------------------- module
struct VisionCache;   // Vision.cpp

// Match outcome, frozen at the end of the first tick that has a result (docs/battle-net.md): result() keeps returning
// it and the report blob never changes again, so every client's hosted-battle result report is identical however far
// past the result tick its rollback session predicted. A world resource: rollback snapshots copy it; it is neither
// hashed nor saved (checksums and save layouts of every game stay as they were).
struct MatchOutcome { int result = 0; uint32_t tick = 0; std::vector<uint8_t> blob; };

class Progression;   // Progression.h (rules "progression")
class GameCore final : public GameModule, public GameHost {
public:
    GameCore();
    explicit GameCore(std::vector<NativeDefinition> nativeDefinitions);
    const std::vector<NativeDefinition>& nativeDefinitions() const { return nativeDefinitions_; }
    // Entity spawned for each map "entities" item of the last setup (kNullEntity for static colliders). Editor
    // tooling only (Studio "Keep play changes"); not part of the simulation state or its checksum.
    const std::vector<Entity>& mapPlaced() const { return mapPlaced_; }
    ~GameCore() override;
    // Systems that run the whole match when the project's data asks for them (HostSystems.h): actor systems on
    // world-space maps (map "colliders" + "spawns"), voxel worlds (rules "voxelGame"). nullptr on grid maps.
    HostSystems* systems() const { return systems_.get(); }
    static bool worldSpaceMap(const Json& map) { return map["colliders"].isArray() && map["spawns"].isArray(); }
    // The HostSystems the data asks for, or nullptr for the grid systems (HostSystems.cpp).
    static std::shared_ptr<HostSystems> makeHostSystems(const Json& rules, const Json& map);
    bool readyToStep(Simulation& sim) override { return systems_ ? systems_->readyToStep(sim) : true; }
    std::string name() const override { return "game"; }
    void setup(Simulation& sim, const Json& rules, const Json& map) override;
    void applyCommand(Simulation& sim, const Command& cmd) override;
    void hashState(Simulation& sim, Hasher& h) override;
    void prepareSnapshot(Simulation& sim) override { if (script_) script_->flush(sim); }   // world-space: script_ is null
    bool serializeState(Simulation& sim, Archive& a) override;   // Save.cpp
    int result(Simulation& sim) override;
    // Frozen outcome (null / empty until the match has a result). The blob is what hosted battle clients report
    // (NetClient::setResultReporter): result, tick, per player slot / team / alive / units / structures / team score.
    const MatchOutcome* outcome(Simulation& sim) const;
    std::vector<uint8_t> outcomeBlob(Simulation& sim) const;
    double speedScale(Simulation& sim) override;   // match modes: time limit speed steps, sudden death preview (Modes.cpp)
    // Shared engine hazards (PHYSIS tornadoes): pure damage to on-map units / buildings (Combat.cpp).
    void environmentDamage(Simulation& sim, Entity target, Fixed amount, Entity source) override;
    Fixed environmentWaterDepth(Simulation& sim, Fixed x, Fixed z) override;
    // ---- match modes (Modes.cpp): the effective modes of this match (rules + matchMode preset + map)
    const ModesRules& modes() const { return modes_; }
    bool modeZone(Simulation& sim, uint8_t player, ModeRect& out) const;   // battle-royal zone of a player (false: none)
    int64_t matchStat(Simulation& sim, uint8_t player, MatchStat key) const;
    int modeLevel(Simulation& sim, uint8_t player, int16_t res) const;     // level index of a resource with levels (-1)
    Fixed modesLevelStat(Simulation& sim, uint8_t player, uint16_t type, Stat s, Fixed& mul) const;

    const Rules& rules() const override { return rules_; }
    // map players[i].ai of slot p (preset / knobs for ComputerPlayer), nullptr when out of range
    const AIPlayerRules* aiPlayer(uint8_t p) const { return p < aiPlayers_.size() ? &aiPlayers_[p] : nullptr; }
    const TriggerSet& triggers() const { return triggers_; }
    ScriptEngine* scripts() const override { return script_.get(); }   // the Lua VM (standard library + game scripts)
    GridFields* gridFields() const override { return fields_.get(); }  // grid fields (GridFields.h); null before setup
    Raid* raid() const override { return raid_.get(); }                // extraction raids (Raid.h); inactive without rules "raid"
    Prompts* prompts() const override { return prompts_.get(); }        // script choice menus (Prompts.h); null before setup
    Sensors* sensors() const override { return sensors_ && sensors_->active() ? sensors_.get() : nullptr; }      // radar / sonar / jammers (Sensors.h)
    FieldOps* fieldOps() const override { return fieldOps_ && fieldOps_->active() ? fieldOps_.get() : nullptr; }  // repair, reclaim, stockpiles... (FieldOps.h)
    Career* career() const override { return career_.get(); }           // career ladders and perks (Career.h); null before setup
    Commerce* commerce() const override { return commerce_.get(); }     // premises, businesses, the bank (Commerce.h); null before setup
    Arsenal* arsenal() const override { return arsenal_.get(); }        // the hero's weapons, ammo, crates, money (Arsenal.h); null before setup
    Traffic* traffic() const override { return traffic_.get(); }        // city traffic: AI cars, signals, pedestrians (Traffic.h); null before setup
    Driving* driving() const override { return driving_.get(); }        // vehicles the hero drives (Driving.h); null before setup
    TopDown* topDown() const override { return topDown_.get(); }        // top-down heroes (TopDown.h); null before setup
    Progression* progression() const override { return progression_.get(); }   // in-match progression (Progression.h); inactive without rules "progression"
    Encounters* encounters() const override { return encounters_.get(); }   // boss encounters (Encounters.h); null before setup
    Environment* environment() const override { return env_ && env_->active() ? env_.get() : nullptr; }   // map environment (Environment.h); null without one
    const NativeModules& native() const { return native_; }   // native modules of this game (hooks: docs/native-modules.md)
    // The map the simulation was set up from when the map had a "generate" block (nullptr otherwise):
    // presentation reads terrainMaterial / heights / regions from it (agent-terrain-gen).
    const Json* generatedMap() const { return generated_.isObject() ? &generated_ : nullptr; }
    using GameHost::state;

    // ---- queries shared with AI, client and editor
    Entity spawn(Simulation& sim, uint16_t type, uint8_t owner, Vec3 pos, bool underConstruction = false) override ;
    // Full placement check for `player` (terrain, blocking, radius, regions, creep, placeOn...).
    bool canPlace(Simulation& sim, uint8_t player, uint16_t type, Vec3 centre, Entity ignore = kNullEntity, std::string* why = nullptr);
    // Footprint cells of a structure centred near `centre` (grid coordinates). On planets (surface given) the
    // footprint lies inside one face; one that would cross a face seam gets x0 = y0 = -1 (never buildable).
    static void footprintAt(const UnitType& t, Vec3 centre, int& x0, int& y0, const Surface* surface = nullptr);
    static Vec3 snapToFootprint(const UnitType& t, Vec3 p, const Surface* surface = nullptr);
    bool requirementsMet(Simulation& sim, uint8_t player, const std::vector<Requirement>& req) const;
    bool canTrain(Simulation& sim, uint8_t player, uint16_t type) const; // requirements + unique
    bool canAfford(Simulation& sim, uint8_t player, const std::vector<Amount>& cost, Fixed supply = {}, int* missing = nullptr) const;
    bool isVisible(Simulation& sim, uint8_t player, Vec3 p) const override ;
    // Tests / tools: run the brute-force reference vision instead of the cached one (results are identical;
    // AIVERSE_VISION=ref does the same, AIVERSE_VISION=check runs both and aborts on a difference).
    void setVisionReference(bool on);
    bool hostile(Simulation& sim, uint8_t a, uint8_t b) const override ;
    // Authored routes (map "routes"): true when the map has them. Then units are sent only between
    // garrisons with an open route (canSend); routeLength = length of line 0, -1 when there is none.
    bool routesAuthored() const { return !routeDefs_.empty(); }
    bool canSend(Simulation& sim, Entity from, Entity to) const;
    Fixed routeLength(Simulation& sim, Entity from, Entity to) const;
    Fixed routeAiLength(Simulation& sim, Entity from, Entity to) const;   // slowdown zones count longer; -1: none
    // Flying sends: a garrison with a `flying` status sends along a straight flying route (made on
    // first use) even without a ground route; rows flyRowSpacing apart (lanes.groups flyAll / flySingle).
    bool sendsFlying(Simulation& sim, Entity from) const;
    // Route length within `range` of tower point `tower` (map towersCoverage), 0 when
    // the map has none; and the AI estimates built on it (TowerModule.GetUnitsDamageForSpline / GetDamageBySpline).
    Fixed towerCoverage(Simulation& sim, Entity from, Entity to, Entity tower, Fixed range) const;
    int towerUnitsDamage(Simulation& sim, Entity from, Entity to, Entity tower, Fixed range, Fixed firePerSecond, int count, Fixed rowDistance, Fixed unitSpeed) const;
    int towerDamageBySpline(Simulation& sim, Entity from, Entity to, Entity tower, Fixed range, Fixed firePerSecond, Fixed waveLength, Fixed unitSpeed) const;
    // Alpha of a route walker at the route ends: the view fades it.
    Fixed routeAlpha(Simulation& sim, Entity unit) const;
    // Walkers are ordinary unit entities (LaneWalker + Enter order + GroupTag); they never acquire targets themselves.
    // routeWalkerTargets: living hostile walkers inside the query ellipse / sector, nearest first (ties: entity index),
    // for skills that let walkers shoot. Kill them with dealDamage / hp = 0 as any unit: groups count
    // only spawned units, nothing else to keep consistent. routeWalkerDistances: walked / left along its own line.
    std::vector<Entity> routeWalkerTargets(Simulation& sim, const WalkerQuery& q) const;
    bool routeWalkerDistances(Simulation& sim, Entity unit, Fixed& walked, Fixed& left) const;
    const std::vector<RouteDef>& routeDefs() const { return routeDefs_; }
    // Multiscreen (map "screen"): the current screen and whether a point is on it (always true
    // without a screen). Garrisons off the screen do not grow. setScreenCenter clamps to the screen bounds.
    const ScreenDef& screen() const { return screen_; }
    bool onScreen(Simulation& sim, Vec3 p) const override ;            // map "screen", else the env map scroller screen
    bool screenView(Simulation& sim, Vec3& center, Vec2& size) const;   // current screen (either source); false: none
    Vec3 screenCenter(Simulation& sim) const;
    void setScreenCenter(Simulation& sim, Vec3 c);
    Fixed stat(Simulation& sim, Entity e, Stat s) const override ;          // effective stat with techs/levels/buffs
    // stat of a type for a player without an entity (GameCore::stat without per-entity parts), + moraleDelta perUnit levels
    Fixed typeStatWithMorale(Simulation& sim, uint8_t player, uint16_t type, Stat s, int moraleDelta) const;
    // settings of the computer player controller of rules ai.controller (null for builtin / scripted), AIControllers.h
    const std::shared_ptr<const AIControllerConfig>& aiConfig() const { return aiConfig_; }
    const AIControllerKind* aiKind() const { return aiKind_; }
    // slots played by computer controllers in this process (not sim state: controllers may tell them from humans)
    void registerBotSlot(uint8_t slot) const { if (std::find(botSlots_.begin(), botSlots_.end(), slot) == botSlots_.end()) botSlots_.push_back(slot); }
    const std::vector<uint8_t>& botSlots() const { return botSlots_; }
    Fixed typeStat(Simulation& sim, uint8_t player, uint16_t type, Stat s, Fixed base) const;
    int membersAlive(Simulation& sim, Entity e) const;
    int levelAt(Simulation& sim, Vec3 p) const;
    // Heightfield terrain (rules terrain.model "heightfield"): ground height under p (0 otherwise), and the
    // height advantage of a over b in cliff levels - whole levels on cliff maps, fractional on heightfields.
    Fixed groundHeight(Simulation& sim, Vec3 p) const;
    Fixed heightAdvantage(Simulation& sim, Vec3 a, Vec3 b) const;
    Fixed slopeSpeed(Simulation& sim, Vec3 p, Vec2 dir) const;   // uphill / downhill speed factor (1 off heightfields)
    // p lifted to `h` above the ground (flat: y = ground height + h; planets: along the local up)
    Vec3 lift(Simulation& sim, Vec3 p, Fixed h) const override ;
    // ---- planets: what the view / editor / AI call (all no-ops / flat answers on flat maps)
    const Surface* surface(Simulation& sim) const;                // null on flat maps
    int bodyOf(Simulation& sim, Vec3 p) const;                     // 0 on flat maps
    // Body centre in world space at a sim tick (circular orbits, Fixed). View: float(world) at tick + alpha.
    Vec3 bodyWorldPosition(Simulation& sim, int body, uint32_t tick) const;
    // World-space position of an entity at a tick: body centre + body-local position; units in transit are
    // interpolated between the departure and arrival shell points.
    Vec3 worldPosition(Simulation& sim, Entity e, uint32_t tick) const;
    // Visibility of a planet cell (body, face, x, y) for a player: 0 unexplored, 1 explored, 2 visible.
    uint8_t cellVision(Simulation& sim, uint8_t player, int body, int face, int x, int y) const;
    bool canTransit(Simulation& sim, Entity e, int body) const;
    int regionAt(Simulation& sim, Vec3 p) const;
    bool canClaim(Simulation& sim, uint8_t player, int region, std::vector<Amount>* cost = nullptr) const;
    Entity findNearestFieldCell(Simulation& sim, Vec3 p, int res, Vec3& out, int maxRadius = 24) const;
    uint8_t teamOf(Simulation& sim, uint8_t player) const override ;

    // ---- internals (used across implementation files)
    void issue(Simulation& sim, Entity e, const Order& o, bool queue) override ;
    void moveTo(Simulation& sim, Entity e, Vec3 goal, Fixed radius) override ;
    // Shared flow field toward a goal area of `radius` world units (falls back to moveTo when no field applies).
    void moveToFlow(Simulation& sim, Entity e, Vec3 goal, Fixed radius);
    void sysFlowFields(Simulation& sim);   // refreshes the waypoints of units following fields (start of movement)
    void stopMoving(Simulation& sim, Entity e) override ;
    void dealDamage(Simulation& sim, Entity target, Fixed dmg, uint16_t dmgType, Entity source, uint8_t sourceOwner, const WeaponDef* wd = nullptr) override ;
    Fixed contactDistance(Simulation& sim, Entity a, Entity b) override ;
    bool pay(Simulation& sim, uint8_t player, const std::vector<Amount>& cost, Fixed supply, bool emitFail = true);
    void refund(Simulation& sim, uint8_t player, const std::vector<Amount>& cost, Fixed fraction);
    // free cell around a structure; with `toward` (a rally point) the free cell of the first ring closest to it
    Vec3 freeSpotNear(Simulation& sim, Entity structure, uint8_t layer, const Vec3* toward = nullptr) override ;
    void recomputeMods(Simulation& sim, uint8_t player);
    void castAbility(Simulation& sim, Entity caster, int index, Entity target, Vec3 point) override ;
    bool canCast(Simulation& sim, Entity caster, int index) const override ;
    Entity placeStructure(Simulation& sim, uint8_t player, uint16_t type, Vec3 centre, Entity builder, bool complete);
    void addResource(Simulation& sim, uint8_t player, uint16_t res, Fixed amount) override ;
    // Deterministic spot for an auto-placed structure (nearest valid to the player's key/first structure).
    bool findAutoSpot(Simulation& sim, uint8_t player, uint16_t type, Vec3& out);
    std::vector<Amount> autoBuildCost(Simulation& sim, uint8_t player, uint16_t type) const;
    uint32_t ticksUntilWave(Simulation& sim) const;
    // ---- dungeon / territory
    void setTerritory(Simulation& sim, int cell, uint8_t owner);
    int territoryOf(Simulation& sim, uint8_t player) const;
    std::vector<Amount> techCost(Simulation& sim, uint8_t player, uint16_t tech) const; // grows with levels
    bool canUsePlayerAbility(Simulation& sim, uint8_t player, int index) const;
    int roomCellsOf(Simulation& sim, uint8_t player, int room) const;
    void applyEffects(Simulation& sim, Entity caster, uint8_t owner, uint16_t sourceType, const std::vector<EffectDef>& effects, Entity target, Vec3 point);
    void addHeroXp(Simulation& sim, Entity hero, Fixed xp, uint8_t player);
    // ---- abilities and effects (Effects.cpp, Skills.cpp)
    void applyPayload(Simulation& sim, int payload, const EffectCtx& ctx) override ;
    const AbilityDef& abilityFor(Simulation& sim, Entity e, int index) const override ;   // level variant of the unit's ability
    const PlayerAbilityDef& playerAbilityFor(Simulation& sim, uint8_t player, int index) const;
    uint32_t statusFlags(Simulation& sim, Entity e) const override ;                       // union of active status flags
    bool isStunned(Simulation& sim, Entity e) const;
    void addStatus(Simulation& sim, Entity target, int def, Fixed duration, const EffectCtx& ctx) override ;
    void addPlayerStatus(Simulation& sim, uint8_t player, int def, Fixed duration, const EffectCtx& ctx);
    int dispel(Simulation& sim, Entity target, uint64_t tags, uint8_t from);    // removed count
    bool castPlayerAbility(Simulation& sim, uint8_t player, int index, Vec3 point, Entity target);
    // The payload of a player ability applied for `player` at once: no cost, cooldown, charges or cast time
    // (scripted casts of native modules and rules). False for an unknown ability / player.
    bool castPlayerAbilityFree(Simulation& sim, uint8_t player, int index, Vec3 point, Entity target);
    // The map this match was set up from (after generation): native modules read their own map keys in setup().
    const Json& matchMap() const { return matchMap_; }
    Fixed playerStat(Simulation& sim, uint8_t player, Stat s, Fixed base) const; // player statuses
    std::vector<Entity> groupMembers(Simulation& sim, uint32_t group) const;
    // Where to aim a skill (autocast, auto skills, AI): fills point / target; false when there is nothing worth it.
    bool smartTarget(Simulation& sim, uint8_t player, int payload, TargetKind kind, SmartRule rule, Vec3& point, Entity& target,
                     Vec3 from = {}, Fixed range = {});
    // ---- settlers economy
    bool isGood(uint16_t res) const;       // stored in buildings (rules.storage == "buildings")
    bool roadConnected(Simulation& sim, Entity a, Entity b);
    std::vector<Amount> upfrontCost(const std::vector<Amount>& cost) const; // the part paid at once (goods are delivered)
    // lanes between garrison buildings a -> b (main lane first), empty when none
    std::vector<int> lanesBetween(Simulation& sim, Entity a, Entity b) const;
    // one unit of `unit` leaves garrison `from` for `target` (lanes / roads / straight, like sysGarrison releases)
    Entity releaseUnit(Simulation& sim, Entity from, Entity target, uint16_t unit, uint8_t owner, uint32_t group);
    // the first effect filter of a payload (through area / projectile / chain delivery) accepts `e` for `owner`
    bool payloadAccepts(Simulation& sim, int payload, Entity e, uint8_t owner) const;
    // false: a native module keeps the player in the match (kHookDefeat); match modes and rDefeat ask it
    bool defeatAllowed(Simulation& sim, uint8_t slot) { return !native_.has(kHookDefeat) || native_.allowDefeat(*this, sim, slot); }
    // the map the match was set up from; kept only for games with native modules (their setup reads its blocks)
    const Json& mapData() const { return matchMap_; }   // same as matchMap()
    // ---- fidelity mechanics (Originals.cpp)
    std::vector<Amount> trainCost(Simulation& sim, uint8_t player, uint16_t type, Fixed* timeMult = nullptr) const; // growth, free hero, revive
    bool heroAllowed(Simulation& sim, uint8_t player, uint16_t type) const;   // limit and tiers
    bool skillLearned(Simulation& sim, Entity e, int index) const;
    bool canLearn(Simulation& sim, Entity e, int index) const;
    bool inField(Simulation& sim, uint8_t player, int16_t field, Vec3 centre, const UnitType& t) const;
    Fixed gatherMultFor(Simulation& sim, uint8_t player, int res) const;
    int gatherersAt(Simulation& sim, Entity node) const;
    bool canCrew(Simulation& sim, Entity unit, Entity vehicle) const;

    // ---- agent-mech mechanics (RtsMech*.cpp)
    bool mechOn(Simulation& sim) const { return rules_.mech.any || sim.world().resource<GameState>()->mechActive; }
    bool applyMechCommand(Simulation& sim, const Command& cmd);   // true: handled (kCmdStance...; smart click on a transport)
    void sysMech(Simulation& sim);                                 // stances, retreat, transports
    void hashMech(Simulation& sim, Hasher& h, Entity e);           // per object, only components that exist
    void mechStatMod(Simulation& sim, Entity e, Stat s, Fixed& add, Fixed& mul) const;   // retreat speed, cargo arrows, veterancy
    Fixed mechDamageTaken(Simulation& sim, Entity e, Entity source) const;   // retreat, cover, armor arcs
    bool mechCanAcquire(Simulation& sim, Entity e) const;          // auto-targeting allowed (passive stance, cargo)
    bool mechMayChase(Simulation& sim, Entity e, Vec3 targetPos);  // defensive stance leash
    bool runBoard(Simulation& sim, Entity e, Order& o);
    bool runUnload(Simulation& sim, Entity e, Order& o);
    void mechOnDeath(Simulation& sim, Entity e);                   // a dying transport drops / kills its passengers
    bool board(Simulation& sim, Entity passenger, Entity carrier); // false: no room / not accepted
    void unloadOne(Simulation& sim, Entity carrier, size_t index);
    bool accepts(Simulation& sim, Entity carrier, Entity passenger) const;
    Entity nearestRetreatPoint(Simulation& sim, uint8_t owner, Vec3 from) const;
    bool detected(Simulation& sim, Entity e, uint8_t viewer) const;      // an allied detector reveals it
    bool canSee(Simulation& sim, Entity e, uint8_t viewer) const override ;        // not invisible to viewer (statuses + detectors)
    bool mechToggle(Simulation& sim, Entity caster, int ability);        // true: a toggle ability, handled
    void mechOnHit(Simulation& sim, Entity target, Entity source, Fixed dmg);       // veterancy: who hit last
    void mechOnSpawn(Simulation& sim, Entity e, Entity producer);        // rally waypoints
    bool runFollow(Simulation& sim, Entity e, Order& o);
    bool applyMechCommand2(Simulation& sim, const Command& cmd);    // guard, follow, rally waypoints
    void mechTick(Simulation& sim);                                  // veterans, toggles
    void mechVeteranKill(Simulation& sim, Entity victim);
    // combat / economy models (RtsMechCombat.cpp)
    // stage 2 (MechWorld.cpp, MechEconomy.cpp, MechGroups.cpp)
    bool applyMechCommand3(Simulation& sim, const Command& cmd);
    void mechTickWorld(Simulation& sim);          // gates, bridges, hazards, cycles, docks
    void mechTickEconomy(Simulation& sim);        // traders, prices, deck setup
    void mechTickGroups(Simulation& sim);         // formation speeds, merges
    bool mechUseAmmo(Simulation& sim, Entity e, size_t weapon);
    bool runRearm(Simulation& sim, Entity e, Order& o);
    bool runTrade(Simulation& sim, Entity e, Order& o);
    bool runPickItem(Simulation& sim, Entity e, Order& o);
    bool runMerge(Simulation& sim, Entity e, Order& o);
    bool mechDeckAllows(Simulation& sim, uint8_t player, int ability, Vec3 point) const;
    bool mechInHand(Simulation& sim, uint8_t player, int ability) const;
    void mechDeckPlayed(Simulation& sim, uint8_t player, int ability);
    void mechDropItems(Simulation& sim, Entity victim);
    void mechBridgeDied(Simulation& sim, Entity e);
    void hashMechGlobal(Simulation& sim, Hasher& h);
    Fixed marketPrice(Simulation& sim, int res) const;
    void mechSetup(Simulation& sim, const Json& map);   // decks, prices, alliances (after setupMap)
    void mechTunnelGather(Simulation& sim, Entity tunnel);   // pull the network's passengers into this exit
    bool applyMechEconomyCommand(Simulation& sim, const Command& cmd);   // market, trade, tribute, diplomacy, items
    void campaignAction(Simulation& sim, const TrigAction& a, const std::vector<uint8_t>& players, Entity unit, Vec3 at);
    // stage 3 (MechTactics.cpp, MechSociety.cpp)
    bool mechOnFire(Simulation& sim, Entity e, size_t weapon, Entity target);   // turret aim, ammo, suppression, crews, craters
    void mechTickTactics(Simulation& sim);     // suppression decay, turrets, crush
    void mechTickSociety(Simulation& sim);     // growth, autonomy, rounds
    void mechTacticsDeath(Simulation& sim, Entity e);   // ruins, infection
    bool mechSwitchedOff(Simulation& sim, Entity e) const;   // unpowered / unstaffed structures
    Fixed mechIncomeMult(Simulation& sim, Entity e) const;
    bool mechPowered(Simulation& sim, uint8_t player, Vec3 at) const;
    Fixed staffFraction(Simulation& sim, Entity e) const;
    bool applyMechSocietyCommand(Simulation& sim, const Command& cmd);   // bounty, garrison-paid upgrades
    // stage 4 (MechBattle.cpp): morale, rout, fatigue, charge, supply
    void mechTickBattle(Simulation& sim);
    void mechMoraleHit(Simulation& sim, Entity target, Entity source, Fixed dmg);
    void mechMoraleDeath(Simulation& sim, Entity victim);
    bool mechBattleFire(Simulation& sim, Entity e);      // false while routing; starts a charge
    void mechBattleStat(Simulation& sim, Entity e, Stat s, Fixed& mul) const;
    bool mechRouting(Simulation& sim, Entity e) const;
    Fixed moraleMaxOf(uint16_t type) const;
    // logistics (MechLogistics.cpp): belts, producers, crafters
    void mechTickLogistics(Simulation& sim);
    bool applyMechLogisticsCommand(Simulation& sim, const Command& cmd);
    void mechLogisticsSetup(Simulation& sim, const Json& map);
    void hashLogistics(Simulation& sim, Hasher& h);
    // turn-based mode (MechTurns.cpp)
    void mechTickTurns(Simulation& sim);
    bool mechTurnCommand(Simulation& sim, const Command& cmd);   // true: swallowed (not your turn) or handled (end turn)
    bool mechTurnFire(Simulation& sim, Entity e);
    void mechTurnStat(Simulation& sim, Entity e, Stat s, Fixed& mul) const;
    uint8_t turnPlayer(Simulation& sim) const;
    // free 3D flight (MechSpace.cpp)
    void mechAltitudeCommand(Simulation& sim, const Command& cmd);
    void mechTickAltitude(Simulation& sim);
    Fixed mechContact3D(Simulation& sim, Entity a, Entity b);
    bool genericSeparation = false;   // tests: compare the BA separation against the generic loop
    // BA: separation of overlapping units with a per-cell grid and flat arrays (exactly the pushes of sysMovement)
    void mechSeparation(Simulation& sim, const std::vector<Fixed>& weight, const std::vector<uint8_t>& anchored, Fixed deadZone, size_t,
                        const std::function<void(Entity, Vec3)>& push);
    // BA: a big attack-move group forms a block of rows facing the goal instead of one wide arc (MechBattle.cpp)
    void mechArmyBlock(Simulation& sim, const std::vector<Entity>& sortedByLateral, Vec3 target, Vec3 dir, Vec3 perp, Fixed spacing,
                       std::vector<std::pair<Entity, Vec3>>& out);
    void mechWaveRoute(Simulation& sim, Entity e, const std::vector<Vec3>& route);

private:
    std::shared_ptr<HostSystems> systems_;   // whole-match systems chosen by data (HostSystems.h)
    ModesRules modes_;               // match modes of this match (read-only after setup, Modes.cpp)
    // gameplay time scale (trigger variable "gameplay_scale"): whether this tick runs the gameplay
    // systems; recomputed from the (hashed, saved) variables at the start of every tick
    bool gameplayRun_ = true;
    void gameplayClock(Simulation& sim);
    void modesSetup(Simulation& sim, const Json& rulesJson, const Json& map);
    void sysModes(Simulation& sim);
    void sysMatchStats(Simulation& sim);
    void hashModes(Simulation& sim, Hasher& h);
    friend bool ioModesState(GameCore& m, Simulation& sim, Archive& a);
    std::shared_ptr<const AIControllerConfig> aiConfig_;   // rules ai.controller settings (AIControllers.h)
    const AIControllerKind* aiKind_ = nullptr;
    mutable std::vector<uint8_t> botSlots_;            // controller bot slots of this process (never read by the sim)
    void notePointProduced(Simulation& sim, Entity e, int n);   // Hypnosis.cpp: per point produced units (controllers with pointStats)
    bool hypnotisedSend(Simulation& sim, const Command& cmd);   // Hypnosis.cpp: kCmdSend out of a hypnotised garrison
    Json generated_;                 // generated map (see generatedMap())
    void setupMap(Simulation& sim, const Json& map);
    // ---- planets (Planets.cpp)
    void setupWorld(Simulation& sim, const Json& map);               // world block -> Surface + stacked grid
    Vec3 mapPos(Simulation& sim, const Json& p) const;               // [x, z] / [x, y, z] or {"body", "face", "x", "y"}
    void moveSphere(Simulation& sim);                                // movement + separation on planet surfaces / shells
    // vision (Vision.cpp): fast cached passes and the brute-force reference they must match exactly
    void visionFlatFast(Simulation& sim);
    void visionSphereFast(Simulation& sim);
    void visionFlatRef(Simulation& sim);
    void visionSphereRef(Simulation& sim);
    std::shared_ptr<VisionCache> vcache_;   // derived, never state (see Vision.cpp)
    bool visionRef_ = false;
    void sysTransit(Simulation& sim);
    void applyTransit(Simulation& sim, const Command& cmd);
    void hashWorld(Simulation& sim, Hasher& h);
    // systems
    void sysEconomy(Simulation& sim);
    void sysFields(Simulation& sim);
    void sysProduction(Simulation& sim);
    void sysConstruction(Simulation& sim);
    void sysGarrison(Simulation& sim);
    void sysOrders(Simulation& sim);
    void sysNeutrals(Simulation& sim);
    void sysTargeting(Simulation& sim);
    void sysMovement(Simulation& sim);
    void sysWeapons(Simulation& sim);
    void sysProjectiles(Simulation& sim);
    void sysAbilities(Simulation& sim);
    void sysDeath(Simulation& sim);
    void sysCapture(Simulation& sim);
    void sysCreep(Simulation& sim);
    void sysVision(Simulation& sim);
    void sysDungeon(Simulation& sim);     // Dungeon.cpp: jobs, needs, portals, territory, possession
    void setupDungeon(Simulation& sim, const Json& map);
    void applyDungeonCommand(Simulation& sim, const Command& cmd);
    bool runDig(Simulation& sim, Entity e, Order& o);
    bool runClaimCell(Simulation& sim, Entity e, Order& o);
    bool runNeed(Simulation& sim, Entity e, Order& o);
    void movePossessed(Simulation& sim);
    void moveAgents(Simulation& sim);     // Avoidance.cpp: path following with ORCA avoidance and yielding
    void recomputeRooms(Simulation& sim);
    void roomEconomy(Simulation& sim);
    void hashDungeon(Simulation& sim, Hasher& h);
    void applyEffect(Simulation& sim, const EffectDef& e, int list, int index, EffectCtx ctx);
    std::vector<Entity> effectTargets(Simulation& sim, const EffectDef& e, const EffectCtx& ctx);
    std::vector<uint8_t> effectPlayers(Simulation& sim, const EffectDef& e, const EffectCtx& ctx);
    bool filterOk(Simulation& sim, const TargetFilter& f, Entity e, uint8_t owner, Entity caster) const;
    void effectGarrison(Simulation& sim, const EffectDef& e, EffectCtx& ctx, const std::vector<Entity>& targets);
    void effectGroup(Simulation& sim, const EffectDef& e, EffectCtx& ctx, const std::vector<Entity>& targets);
    void effectPlayer(Simulation& sim, const EffectDef& e, EffectCtx& ctx);
    void sysEffects(Simulation& sim);     // statuses, areas, channels, player skills
    void tickAreas(Simulation& sim);
    void tickStatuses(Simulation& sim);
    void tickPlayerSkills(Simulation& sim);
    void impactProjectile(Simulation& sim, Entity p);
    const ProjectileFlight* projectileFlight(const Projectile& p) const;
    const WeaponDef* projectileWeapon(const Projectile& p) const;   // the weapon a shot came from (also bullet perk variants)
    void initProjectileProgram(Simulation& sim, Projectile& p, Vec3 start) const override ;
    int liveResult(Simulation& sim);                 // the result the state shows now (result(): frozen once decided)
    void freezeOutcome(Simulation& sim);             // "outcome" system: latches the first result + its report blob
    friend class Arsenal;   // free fire launches weapon projectile programs at a point (Arsenal.cpp)
    void bakeProjectileProgram(Projectile& p, const ProjectileFlight& f) const;
    void sysProjectilePrograms(Simulation& sim, const std::vector<Entity>& projectiles);
    // skill objects (SkillObjects.cpp): objects spawned from units, goals, magnets, death payloads
    bool spawnSkillObjects(Simulation& sim, const EffectDef& e, int list, int index, const EffectCtx& ctx);
    void spawnArea(Simulation& sim, const EffectDef& e, int list, int index, const EffectCtx& ctx, Vec3 at, Entity source);
    void spawnProjectile(Simulation& sim, const EffectDef& e, int list, int index, const EffectCtx& ctx, Vec3 start, Entity source);
    Entity resolveGoal(Simulation& sim, uint8_t goal, const EffectCtx& ctx, Entity source, Vec3 from);
    bool areaArrival(Simulation& sim, Entity a, Fixed step);   // goal motion; true when arrived
    void areaFinished(Simulation& sim, Entity a, bool usedUp);
    void effectPull(Simulation& sim, const EffectDef& e, const EffectCtx& ctx, const std::vector<Entity>& targets);
    bool runPull(Simulation& sim, Entity e, Order& o);          // Enter orders of pulled units; true when inside
    void tickSkillObjects(Simulation& sim);
    void noteDeathInAreas(Simulation& sim, Entity dead, Vec3 pos);
    void flushAreaDeaths(Simulation& sim);
    void hashSkillObjects(Simulation& sim, Hasher& h);
    int* budgetCounter(Simulation& sim, const EffectCtx& ctx);
    bool routeFilterOk(Simulation& sim, uint8_t route, Entity unit, const EffectCtx& ctx);
    void onUnitReleased(Simulation& sim, Entity from, Entity unit);   // a garrison sent a unit out (onSpawn, hypnosis)
    bool skillSend(Simulation& sim, Entity from, Entity to, uint8_t player, int n);   // teleport sends; true: handled
    bool hypnoticSend(Simulation& sim, const Command& cmd);           // sends from a structure the player commands
    bool silencedFor(Simulation& sim, uint8_t player, int index) const;
    void endCaptureStatuses(Simulation& sim, Entity s);
    Fixed levelOfStock(int16_t res, Fixed stock) const;   // resource levels (morale stars), fractional
    Fixed stockOfLevel(int16_t res, Fixed level) const;
    void tickTransfers(Simulation& sim);
    bool everyDue(Simulation& sim, const EffectDef& e, const EffectCtx& ctx);
    void garrisonSend(Simulation& sim, const EffectDef& e, const EffectCtx& ctx, const std::vector<Entity>& targets);
    void payDeathGain(Simulation& sim, Entity unit, uint8_t owner);
    void swapRandom(Simulation& sim, const EffectDef& e, const EffectCtx& ctx, Entity h);
    void closeGroups(Simulation& sim, const std::vector<Entity>& structures);
    void areaOrbit(Simulation& sim, Entity a);
    void staggerCast(Simulation& sim, int list, int index, const EffectCtx& ctx, Entity target, Fixed delay);
    void staggerCastAt(Simulation& sim, int list, int index, const EffectCtx& ctx, Vec3 at, Fixed delay);
    void setupSkills(Simulation& sim, const Json& map);
    void hashEffects(Simulation& sim, Hasher& h);
    void enterGarrison(Simulation& sim, Entity unit, Entity structure);   // assault / reinforce with statuses (advanced)
    Fixed rollAmount(Simulation& sim, const EffectDef& e);
    void sysSettlers(Simulation& sim);    // Settlers.cpp: workshops, carriers, storehouse stock, roads
    void applySettlersCommand(Simulation& sim, const Command& cmd);
    bool runHaul(Simulation& sim, Entity e, Order& o);
    void recomputeRoads(Simulation& sim);
    void depositGoods(Simulation& sim, uint8_t player, uint16_t res, Fixed amount);
    void withdrawGoods(Simulation& sim, uint8_t player, uint16_t res, Fixed amount);
    void syncStorehouses(Simulation& sim);
    void hashSettlers(Simulation& sim, Hasher& h);
    void sysLanes(Simulation& sim);       // Lanes.cpp
    void rebuildLanes(Simulation& sim);
    void rebuildLanesSphere(Simulation& sim, const std::vector<Entity>& nodes);   // planets: same-body pairs, on the surface
    bool runLaneWalk(Simulation& sim, Entity e, Order& o);
    // authored routes / row groups / multiscreen (Routes.cpp)
    void setupRoutes(Simulation& sim, const Json& map, const std::vector<Entity>& placed);
    void rebuildRoutes(Simulation& sim);
    void sysRoutes(Simulation& sim);
    void sysSendGroups(Simulation& sim);
    bool sendAlongRoute(Simulation& sim, Entity from, Entity to, uint8_t player, int n);
    void tagReleased(Simulation& sim, Entity unit, uint32_t group, Entity from, Entity to);   // Economy.cpp
    void ioRoutes(Archive& a, GameState& gs);
    void hashRoutes(Simulation& sim, Hasher& h);
    std::vector<RouteDef> routeDefs_;                 // map "routes" (static after setup)
    std::vector<std::vector<std::vector<Vec2>>> routeLines_;   // per route: its sub-splines (buildRouteLines)
    std::vector<Vec3> routeAnchors_;                  // 2 per route: the end positions (first / last control point)
    std::vector<std::vector<Entity>> routeBlockers_;  // per route: entities that close it while alive
    ScreenDef screen_;                                // map "screen"
    std::vector<std::vector<std::pair<Entity, std::array<Fixed, 21>>>> routeCoverage_;   // per route: tower -> covered length per range step
    std::vector<int> routeLanes(Simulation& sim, Entity from, Entity to, bool flying) const;   // ground lanes of an open route / flying lanes
    std::vector<int> flyingLanes(Simulation& sim, Entity from, Entity to);                   // made on first use
    int routeIndex(Simulation& sim, Entity a, Entity b) const;
    void startRouteWalk(Simulation& sim, Entity unit, int lane);
    bool attackSlot(Simulation& sim, Entity e, Entity target, Fixed range, Vec3& out); // melee surround
    void moveToVia(Simulation& sim, Entity e, Vec3 goal, Fixed radius, bool aroundAllies);

    // fidelity mechanics hooks (Originals.cpp)
    void sysOriginals(Simulation& sim);
    void hashOriginals(Simulation& sim, Hasher& h);
    void economyOriginals(Simulation& sim);                         // fields, upkeep per resource, territory income
    Fixed mitigate(Simulation& sim, Entity target, Fixed raw, const WeaponDef* wd, uint16_t dmgType);   // the damage pipeline
    void classDamage(Simulation& sim, Entity attacker, size_t weapon, Entity target, Fixed& dmg, uint16_t& dmgType) const;   // classes model
    void afterHit(Simulation& sim, Entity target, Entity source, const WeaponDef* wd, bool killed);
    void onDeathOriginals(Simulation& sim, Entity e, uint8_t owner, Vec3 pos, bool silent);
    void onHeroSpawn(Simulation& sim, Entity e, uint8_t owner);
    void gainOn(Simulation& sim, uint8_t player, Fixed ResourceDef::*field, Fixed count);
    bool crewEnter(Simulation& sim, Entity unit, Entity vehicle);
    void applyLearn(Simulation& sim, Entity e, int index);
    Fixed playerModsStat(Simulation& sim, uint8_t player, uint16_t type, Stat s, Fixed& mul) const;
    bool runOrder(Simulation& sim, Entity e, Order& o);
    bool runGather(Simulation& sim, Entity e, Order& o);
    bool runBuild(Simulation& sim, Entity e, Order& o);
    Entity findNearestDropoff(Simulation& sim, uint8_t owner, Vec3 p, int res);
    Entity findNearestResource(Simulation& sim, uint8_t owner, Vec3 p, int res, Fixed maxDist);
    void grantKill(Simulation& sim, Entity victim, uint8_t killer);

    Rules rules_;
    std::vector<AIPlayerRules> aiPlayers_;   // by slot, from map players[i].ai (setup)
    TriggerSet triggers_;
    struct SlotClaim { uint64_t target = 0; int slot = 0; uint64_t claimer = 0; };
    std::vector<SlotClaim> slotClaims_;   // rebuilt every tick from Combat slots (derived, not state)
    struct DeathSpawn { uint16_t type; uint8_t owner; Vec3 pos; int count; };
    std::vector<DeathSpawn> deathSpawns_; // filled and emptied inside sysDeath (infection)
    struct AreaDeath { Entity area; Vec3 pos; };
    std::vector<AreaDeath> areaDeaths_;   // filled and emptied inside sysDeath (onDeath areas: souls)
    mutable std::vector<uint16_t> gatherCount_;   // derived per tick: workers harvesting at each node (by entity index)
    mutable uint32_t gatherCountTick_ = UINT32_MAX;
    void assaultOriginals(Simulation& sim, Entity unit, Entity structure);   // gains for units lost in an assault
    void captureOriginals(Simulation& sim, Entity structure, uint8_t from, uint8_t to);
    void flushDeathSpawns(Simulation& sim);
    std::unique_ptr<GridFields> fields_;   // grid fields config + kernels (state: GridFieldStore resource)
    std::unique_ptr<Raid> raid_;           // extraction raids (state: RaidStore resource)
    friend class TopDown;                  // free fire launches weapon projectiles (initProjectileProgram)
    std::unique_ptr<Prompts> prompts_;     // rules "prompts" (state: PromptStore resource)
    std::unique_ptr<Sensors> sensors_;     // type "radar" / "sonar" / "jammer" ... (no state)
    std::unique_ptr<FieldOps> fieldOps_;   // rules / type field operations (state: FieldOpsStore resource)
    std::unique_ptr<Career> career_;       // rules "careers" (state: CareerStore resource)
    std::unique_ptr<Commerce> commerce_;   // rules "commerce" (state: CommerceStore resource)
    std::unique_ptr<Arsenal> arsenal_;     // rules "arsenal" (state: ArsenalStore resource)
    std::unique_ptr<Traffic> traffic_;     // rules "traffic" (state: TrafficStore resource)
    std::unique_ptr<Driving> driving_;     // rules "driving": the grid-map host of the vehicle system (engine/vehicles)
    std::unique_ptr<TopDown> topDown_;     // rules "topDown" (state: TopDownStore resource)
    std::unique_ptr<Progression> progression_;   // rules "progression" (state: ProgressionStore resource)
    std::unique_ptr<Encounters> encounters_;   // rules "encounters" (state: EncounterStore resource)
    std::unique_ptr<Environment> env_;     // map environment (Environment.h; state: EnvState resource)
    std::vector<NativeDefinition> nativeDefinitions_;
    std::vector<Entity> mapPlaced_;
    NativeModules native_;
    bool nativeFiltering_ = false;   // inside NativeModule::filterCommand's re-entry (never set between commands)
    Json matchMap_;                  // see matchMap()
    std::unique_ptr<ScriptEngine> script_; // Lua VM (Script.h); its persistent state is the ScriptStore resource
    // foundations, traps, group teleports, charge effects (Fidelity.cpp)
    void sysFidelity(Simulation& sim);
    void hashFidelity(Simulation& sim, Hasher& h);
    void spawnFidelity(Simulation& sim, Entity e, bool underConstruction);   // slabs pave, weathering, hidden traps
    void builtFidelity(Simulation& sim, Entity e);                           // construction finished
    void deathFidelity(Simulation& sim, Entity e);                           // foundation under a destroyed structure
    void effectFidelity(Simulation& sim, const EffectDef& e, EffectCtx& ctx);
    void takePoint2(Simulation& sim, uint8_t player, int64_t param);
public:
    // pointPair player abilities: kCmdPlayerAbility param = index | packPoint2(second point)
    static int64_t packPoint2(Vec3 p);
    bool pairTarget(Simulation& sim, uint8_t player, int payload, Vec3& from, Vec3& to);   // AI: own group -> enemy base
    Fixed spotScoreFidelity(Simulation& sim, uint8_t player, uint16_t type, int x0, int y0, Vec2 d, Vec2 away);   // findAutoSpot: slabs, pads, traps (>= 1<<19: skip)
    int bareCells(Simulation& sim, uint8_t player, int x0, int y0, int w, int h) const;
};

} // namespace aiverse::game
