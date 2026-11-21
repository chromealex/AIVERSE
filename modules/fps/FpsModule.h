#pragma once
#include "engine/sim/Simulation.h"
#include "engine/world/SpatialWorld.h"
#include <cstddef>
#include <deque>
#include <map>
#include "FpsSystems.h"
#include "engine/world/CollisionStream.h"
#include "libraries/include/aiverselib/genre/Melee.h"
#include "libraries/include/aiverselib/genre/royale/BattleRoyale.h"
#include "libraries/include/aiverselib/systems/Gear.h"
#include "libraries/include/aiverselib/systems/BuildingGen.h"
#include "libraries/include/aiverselib/genre/VehicleData.h"
#include "libraries/include/aiverselib/genre/RaceSessionData.h"
#include <optional>

namespace aiverse::fps {
constexpr uint16_t kInputCommand=1;
constexpr uint32_t kBuildingColliderBase=0x40000000;
constexpr uint32_t kRotatedColliderBase=0x50000000;   // triangles of map colliders with a "yaw" (12 per box)   // collider ids of map `buildings` (building-gen)
enum Buttons : uint32_t { Fire=1, Jump=2, Crouch=4, Reload=8, Sprint=16, WeaponMask=224, Aim=256, Prone=512, Melee=1024, Block=2048, LockOn=4096 };
constexpr uint32_t kButtonBits=8191;
constexpr uint32_t selectWeapon(uint32_t index) { return (index+1)<<5; }
// Weapon selection occupies bits 5..7; zero means keep the equipped weapon.
struct Input {
    int32_t forward=0, right=0; // [-1000,1000], relative to yaw
    Fixed yaw{},pitch{};       // turns; pitch positive up
    uint32_t buttons=0,viewTick=0,sequence=0;
    Action action=Action::None;uint32_t argument=0;
    bool operator==(const Input&) const = default;
};
struct MotorConfig {
    Fixed radius=Fixed::fromRatio(3,10),height=Fixed::fromRatio(18,10),crouchHeight=1_fx;
    Fixed speed=5_fx,sprintSpeed=8_fx,jumpSpeed=6_fx,gravity=20_fx,stepHeight=Fixed::fromRatio(3,10);
};
struct WeaponConfig {
    int32_t damage=25,magazine=30,reserve=120;
    uint32_t interval=6,reloadTicks=120;
    Fixed range=300_fx,spread{},hipSpread{},moveSpread{},recoil{},bulletSpeed{},bulletGravity=10_fx,penetration{};
    Fixed headMultiplier=2_fx,limbMultiplier=Fixed::fromRatio(3,4);
    int32_t price=600;
    uint32_t pellets=1,switchTicks=15;
    bool automatic=true;
    std::string nameKey="fps.weapon.rifle";
    projectile::ProjectileFlight flight;
};
// Opt-in close combat (`fps.melee`); timing and hit history use the shared melee-combat library.
struct MeleeConfig {
    bool enabled=false;
    int32_t damage=50;
    uint32_t windup=6,active=4,recovery=12,bufferTicks=8,parryTicks=8,staggerTicks=30;
    Fixed range=2_fx,radius=Fixed::fromRatio(3,10),guardTurns=Fixed::fromRatio(1,8),lockRange=8_fx;
};
struct Config {
    uint32_t tickRate=60,snapshotRate=20,maxPlayers=100,rewindTicks=12,inputTimeout=15;
    Fixed interestRadius=250_fx,worldLimit=100000_fx;
    MotorConfig motor; WeaponConfig weapon;SystemsConfig systems;MeleeConfig melee;
    std::vector<WeaponConfig> weapons;
    std::string mode="deathmatch"; // deathmatch / elimination / battleRoyale / bomb (a map "race" decides the round itself)
    uint32_t respawnTicks=180,roundTicks=7200,zoneStart=1800,zoneDuration=7200;
    Vec3 zoneCenter{};Fixed zoneRadius=2000_fx,zoneEndRadius=10_fx;
    int32_t zoneDamage=5;
    bool friendlyFire=false;
    // Opt-in `fps.royale` (battle-royale package; a map's own `royale` block replaces it): phased zone, air
    // insertion, bombardment, supply drops and standings. `crate` = items of a landed supply drop.
    std::optional<lib::genre::RoyaleRules> royale;
    std::vector<Item> crate;
    // Opt-in `fps.gear` (gear-tiers package): tiered helmets / vests / backpacks as loot type 200 + id.
    lib::systems::GearCatalog gear;
    // Opt-in `fps.vehicleTypes` (vehicle-controllers JSON): map vehicles with a `type` drive with the package's
    // controller and seats (driver, passengers, gunners); vehicles without one keep the legacy arcade car.
    std::optional<lib::genre::VehicleDefinitions> vehicleTypes;
    // Opt-in `fps.bots` (bots-fps settings): map players with "bot": true use the library controller with
    // regional navmeshes, loot / heal / zone goals instead of the reference fallback bot.
    Json bots;
    bool ecsAuthority=false; // opt-in: full actor state canonical in ECS, legacy vector is tick workspace
};
struct Actor {
    uint8_t slot=0,team=0;
    Vec3 pos{},velocity{};
    Fixed yaw{},pitch{};
    int32_t health=100,ammo=30,reserve=120,kills=0;
    uint32_t life=1;
    uint8_t weaponIndex=0;
    int32_t storedAmmo[4]{},storedReserve[4]{};
    uint32_t shots=0,hits=0,switchUntil=0;
    bool fireHeld=false;
    uint32_t reloadUntil=0,nextFire=0,respawnAt=0,lastInputTick=0,lastSequence=0;
    bool grounded=false,crouched=false;
    Input input{};
    int32_t armor=0,money=0;uint8_t owned=15,attachments[4]{};
    Fixed recoil{};bool aiming=false,prone=false,downed=false,climbing=false,frozen=false;
    uint32_t bleedUntil=0,blindUntil=0,vehicle=0,lastDamage=0;
    Action activeAction=Action::None;uint32_t actionUntil=0,actionTarget=0;Vec3 actionOrigin{};
    std::vector<Item> bag;
    // Melee (saved from FPS save version 7; zero when fps.melee is disabled).
    lib::genre::MeleeState melee;
    uint32_t parryUntil=0,staggerUntil=0;uint8_t lockTarget=0;
    bool blockHeld=false,meleeHasTip=false;Vec3 meleeTip{};

    bool operator==(const Actor&) const = default;
};
// Battle-royale run (saved from FPS save version 8; empty when fps.royale is absent). Slot i-1 indexes divers / credit.
struct RoyaleRun {
    bool enabled=false;
    lib::genre::SafeZoneState zone;lib::genre::BombardmentState bombardment;lib::genre::SupplyDropState drops;
    lib::genre::InsertionFlight flight;std::vector<lib::genre::SkydiverState> divers;lib::genre::RoyaleStandings standings;
    std::vector<uint8_t> lastAttacker,lastWeapon;
    std::vector<lib::genre::KillFeedEvent> feed;   // the last 6 kill-feed lines for the HUD
    bool operator==(const RoyaleRun&)const=default;
};
// What one client needs to draw the battle royale (zone ring + timer, carrier, own phase, counts, hazards).
struct RoyaleView {
    bool enabled=false;uint8_t stage=0,phase=0,divePhase=3;uint32_t ticksLeft=0;uint16_t alive=0,teamsIn=0,placement=0,kills=0;
    lib::genre::RoyaleCircle circle,target;Vec3 carrier{};bool bombardment=false;lib::genre::RoyaleCircle bombArea;std::vector<Vec3> crates;
    std::vector<lib::genre::KillFeedEvent> feed;bool finished=false,doorOpen=false,outside=false;int32_t winner=0;uint16_t teams=0;
    bool operator==(const RoyaleView&)const=default;
};
void io(Archive&,RoyaleView&);
// One typed vehicle (FPS save version 9): controller and seat state of the vehicle with id `vehicle`.
// Race run (FPS save version 10; disabled without a map "race"): the session plus the vehicle each slot drives.
struct RaceRun {
    bool enabled=false;
    lib::genre::RaceSessionState session;
    std::vector<uint32_t> vehicleOf;   // slot i+1 -> vehicle id
    bool operator==(const RaceRun&)const=default;
};
struct VehicleRun {
    uint32_t vehicle=0;int64_t type=0;lib::genre::VehicleState controller;lib::genre::VehicleSeatState seats;
    std::vector<lib::genre::VehicleSeatRequest> requests;   // filled and consumed within one tick
};
// Full gameplay Actor payload. In fps.ecsAuthority mode this ECS component
// is canonical between tick barriers, and State::actors is a temporary
// compatibility workspace for older simulation algorithms/save v9.
struct ActorRuntime { Actor value; };

// Stable ECS identities for FPS actors. In default mode, State::actors
// remains the gameplay authority and these are read-only ECS projections.
// With fps.ecsAuthority=true, ActorRuntime holds canonical full actor payloads
// between ticks; State::actors is a contiguous gameplay compatibility workspace.
// Identity/Transform/Vitals remain lightweight editor projections.
struct ActorEntityIdentity {
    uint8_t slot = 0, team = 0;
    uint32_t life = 0;
};
struct ActorEntityTransform {
    Vec3 position{}, velocity{};
    Fixed yaw{}, pitch{};
};
struct ActorEntityVitals {
    int32_t health = 0, armor = 0, ammo = 0, reserve = 0;
    bool downed = false, crouched = false, prone = false;
};
struct ActorEntityIndex {
    std::vector<Entity> bySlot; // slot 1 -> index 0; stable across ticks
};
 
// Spawned FPS objects have ECS identities as well. IDs are stable source IDs
// already present in the legacy save/net protocol. The ECS view is derived
// until the authoritative simulation has migrated off State vectors.
struct ProjectileEntity {
    uint32_t id = 0;
    uint8_t owner = 0, kind = 0;
    Vec3 position{}, velocity{};
    int32_t damage = 0;
};
struct LootEntity {
    uint32_t id = 0;
    Vec3 position{};
    int32_t type = 0, count = 0;
};
struct VehicleEntity {
    uint32_t id = 0;
    Vec3 position{}, velocity{};
    Fixed yaw{};
    int32_t health = 0;
    uint8_t driver = 0;
};
// Full authoritative dynamic object payloads. The order field preserves
// legacy vector iteration order (important for deterministic hits/drops).
// When ecsAuthority is enabled, State arrays are tick-local workspaces only.
struct ProjectileRuntime { Projectile value; uint32_t order=0; };
struct LootRuntime { Loot value; uint32_t order=0; };
struct VehicleRuntime { Vehicle value; uint32_t order=0; };

struct DynamicEntityIndex {
    // Packed key: top 8 bits = kind, low 32 bits = existing object ID.
    std::map<uint64_t, Entity> byKey;
};


struct HistoryFrame { uint32_t tick=0; std::vector<Actor> actors; };
struct State {
    std::vector<Actor> actors;
    std::deque<HistoryFrame> history;
    uint32_t round=1; int32_t winner=0;
    uint32_t nextObject=1;std::vector<Projectile> projectiles;std::vector<Loot> loot;std::vector<Vehicle> vehicles;RoundState match;
    RoyaleRun royale;
    std::vector<lib::systems::GearLoadout> gear;   // per slot when fps.gear is set (FPS save version 9)
    std::vector<VehicleRun> vehicleRuns;
    RaceRun race;
    std::vector<std::string> botStates;   // lib::bots::saveFps bytes per slot when fps.bots is set (save version 9)

};
Config readConfig(const Json& rules);
Command command(uint8_t slot,const Input& input);
bool decodeInput(const Command& command,Input& input);
Vec3 direction(Fixed yaw,Fixed pitch);
world::Bounds actorBounds(const Actor& a,const MotorConfig& config);
void moveActor(Actor& actor,const Input& input,const MotorConfig& config,const world::SpatialWorld& world,Fixed dt,Fixed limit);
void io(Archive&,Input&);
void io(Archive&,Actor&);
Fixed stanceHeight(const Actor&,const MotorConfig&);
struct BotHost;   // FpsBots.cpp: bot settings and regional navmeshes (derived from collision, never saved)
class Module final : public GameModule {
public:
    std::string name() const override { return "fps"; }
    void setup(Simulation&,const Json&,const Json&) override;
    void applyCommand(Simulation&,const Command&) override;
    void hashState(Simulation&,Hasher&) override;
    bool serializeState(Simulation&,Archive&) override;
    int result(Simulation& sim) override { return state(sim).winner; }
    const Config& config() const { return config_; }
    // Debug counters (do not affect deterministic game checksums).
    uint64_t experimentalMotorBatches() const { return experimentalMotorBatches_; }
    uint64_t experimentalMotorCommits() const { return experimentalMotorCommits_; }

    const world::SpatialWorld& collision() const { return collision_; }
    void setContentReader(world::AssetReader);
    bool readyToStep(Simulation&) override;
    const world::AsyncChunkStream::Status* streamingStatus()const{return streamedCollision_?&streamedCollision_->status():nullptr;}
    State& state(Simulation& sim) const { return *sim.world().resource<State>(); }
    bool actorEcsAuthority() const { return config_.ecsAuthority; }
    Entity actorEntity(Simulation& sim, uint8_t slot) const;
    void syncActorEntities(Simulation& sim) const; // rebuild ECS actor state and inspector projections
    void hydrateActorsFromEcs(Simulation& sim) const; // authoritative -> compatibility tick workspace
    void syncDynamicEntities(Simulation& sim) const; // retain original native ABI for setup/v9 legacy restore
    void syncDynamicEntities(Simulation& sim,bool projectProjectilesFromEcs) const; // ECS -> State projectile projection at mirror
    void hydrateDynamicsFromEcs(Simulation& sim) const; // canonical dynamic pools -> ordered tick workspaces
    State authoritativeStateCopy(Simulation& sim) const; // v9 save/hash assembled from ECS payloads
    RoyaleView royaleView(Simulation&,uint8_t slot) const;
    lib::genre::RaceView raceView(Simulation&,uint8_t slot) const;   // racer id = slot
    bool racing() const { return raceRules_.has_value(); }   // this map has a race (map "race")
private:
    void tick(Simulation&);
    void moveActorEcs(Simulation&,Actor&,const Input&); // direct ECS motor, legacy workspace updated for other systems
    void shoot(Simulation&,Actor&);
    void spawnProjectile(Simulation&,Projectile); // native ECS authority on spawn; legacy State remains tick projection
    void clearProjectiles(Simulation&); // synchronous round-reset ECS despawn barrier
    void equipInitial(Actor&) const;
    void setupSystems(Simulation&,const Json&,State&);
    void tickOrdinaryProjectiles(Simulation&); // original same-tick impact stage
    bool stepOrdinaryProjectile(Simulation&,Projectile&); // shared legacy/ECS solver
    void tickSystems(Simulation&);
    void tickProjectilePrograms(Simulation&);
    bool stepProgrammedProjectile(Simulation&,Projectile&,std::vector<Projectile>&,std::size_t liveCount);
    void actorSystems(Simulation&,Actor&,Input&);
    bool traceShot(Simulation&,uint8_t,Vec3,Vec3,int32_t,const WeaponConfig&,const std::vector<Actor>&,Projectile* projectile=nullptr);
    void damage(Simulation&,uint8_t,Actor&,int32_t,bool head=false);
    void action(Simulation&,Actor&,Action,uint32_t);
    void tickMelee(Simulation&,Actor&,const Input&);
    void finishRound(Simulation&,int32_t);
    void setupRoyale(Simulation&,const Json&,State&);
    bool skydive(Simulation&,Actor&);
    void tickRoyale(Simulation&);
    void settleRoyale(Simulation&);
    void steerRoyaleBot(Simulation&,Actor&);
    Fixed groundBelow(Vec3) const;
    std::vector<Loot> buildMapBuildings(const Json& map);
    void setupVehicles(Simulation&,const Json&,State&);
    void tickVehicles(Simulation&,std::span<const std::pair<uint32_t,lib::genre::VehicleInput>> drive={});
    lib::genre::VehicleInput driverInput(const Actor&,uint32_t now) const;
    void setupRace(Simulation&,const Json& rules,const Json& map,State&);
    void tickRace(Simulation&);
    bool validRace(const State&) const;
    bool vehicleInteract(Simulation&,Actor&);
    VehicleRun* vehicleRun(State&,uint32_t vehicle) const;
    bool canFireFromVehicle(State&,const Actor&) const;
    void setupBots(Simulation&,const Json&,State&);
    bool thinkBot(Simulation&,Actor&);
    std::shared_ptr<BotHost> botHost_;
    // Races (rules "race" + map "race", shared format: genre/RaceSessionData.h, docs/races.md); derived, never saved.
    std::optional<lib::genre::RaceRules> raceRules_;lib::genre::RaceTrack raceTrack_;
    std::optional<lib::bots::RaceLine> raceLine_;
    struct BuildingRegion{world::Bounds bounds;Vec3 door{};std::vector<std::pair<Vec3,Vec3>> stairs,halls;};
    std::vector<BuildingRegion> buildingRegions_;   // generated buildings: footprint (+margin), door, stair flights
    std::vector<world::Bounds> ladders_,sites_;
    std::map<uint32_t,Fixed> penetrable_;
    std::vector<Vehicle> initialVehicles_;
    std::vector<Loot> initialLoot_;
    lib::genre::AttackSet meleeAttacks_;

    uint64_t experimentalMotorBatches_=0, experimentalMotorCommits_=0;
    Config config_;
    world::SpatialWorld collision_;
    std::vector<Vec3> spawns_;
    std::vector<bool> bots_;
    uint64_t contentHash_=0;
    Json streamManifest_;
    std::unique_ptr<world::CollisionStream> streamedCollision_;
};
}
