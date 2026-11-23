#pragma once
#include "engine/sim/Simulation.h"
#include "engine/world/SpatialWorld.h"
#include <cstddef>
#include <deque>
#include <map>
#include "ActorSystems.h"
#include "ActorComponents.h"
#include "engine/world/CollisionStream.h"
#include "engine/world/VehicleRegionTransfer.h"
#include "engine/mechanics/Melee.h"
#include "engine/mechanics/royale/BattleRoyale.h"
#include "engine/systems/Gear.h"
#include "engine/systems/BuildingGen.h"
#include "engine/mechanics/VehicleData.h"
#include "engine/mechanics/RaceSessionData.h"
#include "engine/vehicles/VehicleRace.h"
#include <optional>
#include <span>
#include <utility>

#include "../game/HostSystems.h"
namespace aiverse::actor {
constexpr uint32_t kBuildingColliderBase=0x40000000;
constexpr uint32_t kRotatedColliderBase=0x50000000;   // triangles of map colliders with a "yaw" (12 per box)   // collider ids of map `buildings` (building-gen)
struct WeaponConfig {
    int32_t damage=25,magazine=30,reserve=120;
    uint32_t interval=6,reloadTicks=120;
    Fixed range=300_fx,spread{},hipSpread{},moveSpread{},recoil{},bulletSpeed{},bulletGravity=10_fx,penetration{};
    Fixed headMultiplier=2_fx,limbMultiplier=Fixed::fromRatio(3,4);
    int32_t price=600;
    uint32_t pellets=1,switchTicks=15;
    bool automatic=true;
    std::string nameKey="actor.weapon.rifle";
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
    std::optional<lib::mechanics::RoyaleRules> royale;
    // Opt-in rules `locomotion` (character-locomotion package, the locomotion.* keys of docs/gameplay-settings.md):
    // characters move through the mode machine (dash, wall slide, wall run, grapple, slopes) instead of the plain
    // motor. The motor's speed, jumpSpeed and gravity still apply; absent = the plain motor, unchanged bytes.
    std::optional<lib::mechanics::LocomotionModeConfig> locomotion;
    std::vector<Item> crate;
    // Opt-in `fps.gear` (gear-tiers package): tiered helmets / vests / backpacks as loot type 200 + id.
    lib::systems::GearCatalog gear;
    // Opt-in `fps.vehicleTypes` (vehicle-controllers JSON): map vehicles with a `type` drive with the package's
    // controller and seats (driver, passengers, gunners); vehicles without one keep the arcade car.
    std::optional<lib::mechanics::VehicleDefinitions> vehicleTypes;
    // Opt-in `fps.bots` (bots-combatant settings): map players with "bot": true use the library controller with
    // regional navmeshes, loot / heal / zone goals instead of the reference fallback bot.
    Json bots;
};
// Battle-royale run (empty when fps.royale is absent). Slot i-1 indexes divers / credit.
struct RoyaleRun {
    bool enabled=false;
    lib::mechanics::SafeZoneState zone;lib::mechanics::BombardmentState bombardment;lib::mechanics::SupplyDropState drops;
    lib::mechanics::InsertionFlight flight;std::vector<lib::mechanics::SkydiverState> divers;lib::mechanics::RoyaleStandings standings;
    std::vector<uint8_t> lastAttacker,lastWeapon;
    std::vector<lib::mechanics::KillFeedEvent> feed;   // the last 6 kill-feed lines for the HUD
    bool operator==(const RoyaleRun&)const=default;
};
// What one client needs to draw the battle royale (zone ring + timer, carrier, own phase, counts, hazards).
struct RoyaleView {
    bool enabled=false;uint8_t stage=0,phase=0,divePhase=3;uint32_t ticksLeft=0;uint16_t alive=0,teamsIn=0,placement=0,kills=0;
    lib::mechanics::RoyaleCircle circle,target;Vec3 carrier{};bool bombardment=false;lib::mechanics::RoyaleCircle bombArea;std::vector<Vec3> crates;
    std::vector<lib::mechanics::KillFeedEvent> feed;bool finished=false,doorOpen=false,outside=false;int32_t winner=0;uint16_t teams=0;
    bool operator==(const RoyaleView&)const=default;
};
void io(Archive&,RoyaleView&);
// One rewind frame: every character as it was at `tick` (lag-compensated hitscan).
struct HistoryFrame { uint32_t tick=0; std::vector<CharacterSnapshot> characters; };
// World resource with the match-level data. Characters, projectiles, loot and vehicles are entities
// (ActorComponents.h); read them through the ActorHost accessors.
struct State {
    std::deque<HistoryFrame> history;
    uint32_t round=1; int32_t winner=0;
    uint32_t nextObject=1;   // next id of a projectile / loot item / vehicle (ascending id = deterministic order)
    RoundState match;
    RoyaleRun royale;
};
Config readConfig(const Json& rules);
Command command(uint8_t slot,const Input& input);
bool decodeInput(const Command& command,Input& input);
Vec3 direction(Fixed yaw,Fixed pitch);
struct BotHost;   // ActorBots.cpp: bot settings and regional navmeshes (derived from collision, never saved)
// Actor systems (characters, weapons, projectiles, vehicles, royale, race, bots) on world-space maps.
// game::GameCore owns one and runs it when the map is world-space (colliders + spawns).
class ActorHost final : public game::HostSystems {
public:
    // Limits of the dynamic object pools (spawns beyond them are dropped).
    static constexpr size_t kMaxProjectiles=256,kMaxLoot=256,kMaxVehicles=32;
    void setup(Simulation&,const Json&,const Json&) override;
    void applyCommand(Simulation&,const Command&) override;
    void hashState(Simulation&,Hasher&) override;
    bool serializeState(Simulation&,Archive&) override;
    int result(Simulation& sim) override { return state(sim).winner; }
    const Config& config() const { return config_; }

    const world::SpatialWorld& collision() const { return collision_; }
    void setContentReader(world::AssetReader);
    bool readyToStep(Simulation&) override;
    const world::AsyncChunkStream::Status* streamingStatus()const{return streamedCollision_?&streamedCollision_->status():nullptr;}
    // Derived navigation generation for diagnostic/acceptance tests only.
    // No mutable simulation state is affected by rebuilding a navmesh.
    uint64_t navigationRebuilds()const{return navigationRebuilds_;}
    uint64_t navigationCollisionRevision()const{return navigationCollisionRevision_;}
    const std::string& navigationReadinessError()const{return navigationReadinessError_;}

    // Match-level data (round, winner, nextObject, match, royale, history).
    State& state(Simulation& sim) const { return *sim.world().resource<State>(); }
    const State& state(const Simulation& sim) const { return *sim.world().resourceConst<State>(); }
    // Characters: one entity per player slot (1..characterCount), iterated in slot order.
    size_t characterCount(const Simulation& sim) const;
    Entity characterEntity(const Simulation& sim,uint8_t slot) const;   // kNullEntity for an invalid slot
    CharacterRef character(Simulation& sim,uint8_t slot) const;          // throws std::out_of_range for an invalid slot
    ConstCharacterRef character(const Simulation& sim,uint8_t slot) const;
    CharacterSnapshot snapshot(const Simulation& sim,uint8_t slot) const;
    std::vector<CharacterSnapshot> snapshots(const Simulation& sim) const;
    template<class Fn> void forEachCharacter(Simulation& sim,Fn&& fn) const {
        for(size_t slot=1,n=characterCount(sim);slot<=n;++slot)fn(character(sim,uint8_t(slot)));
    }
    template<class Fn> void forEachCharacter(const Simulation& sim,Fn&& fn) const {
        for(size_t slot=1,n=characterCount(sim);slot<=n;++slot)fn(character(sim,uint8_t(slot)));
    }
    // Per-slot gear (nullptr without `fps.gear`).
    lib::systems::GearLoadout* gear(Simulation& sim,uint8_t slot) const;
    const lib::systems::GearLoadout* gear(const Simulation& sim,uint8_t slot) const;
    // Locomotion mode state of a character (rules `locomotion`); nullptr without it.
    const LocomotionBody* locomotion(const Simulation& sim,uint8_t slot) const;
    // Dynamic objects in ascending id (= creation order). Entities carry Projectile / Loot / Vehicle
    // (+ VehicleDrive on typed vehicles); the value variants return copies for read-only consumers.
    std::vector<Entity> projectileEntities(const Simulation& sim) const;
    std::vector<Entity> lootEntities(const Simulation& sim) const;
    std::vector<Entity> vehicleEntities(const Simulation& sim) const;
    std::vector<Projectile> projectiles(const Simulation& sim) const;
    std::vector<Loot> loot(const Simulation& sim) const;
    std::vector<Vehicle> vehicles(const Simulation& sim) const;
    Entity vehicleEntity(const Simulation& sim,uint32_t id) const;   // kNullEntity when absent
    // Thin world-space host adapter: resolve integer seat slots to real
    // CharacterIndex ECS entities, then use shared vehicle group validation.
    bool vehicleRegionGroup(const Simulation& sim,Entity vehicle,
                            world::VehicleRegionGroup& out,
                            std::string* error=nullptr) const;
    // Resolves the actual world-space components; no duplicate Transform.
    static world::RegionPositionReader regionPositionReader();
    // Requires opt-in regionOwnership in rules and destination collision/nav
    // readiness, then transfers the complete shared-vehicle occupant group.
    bool transferVehicleRegion(Simulation& sim,Entity vehicle,
                               world::Cell from,world::Cell to,
                               std::string* error=nullptr);

    RoyaleView royaleView(Simulation&,uint8_t slot) const;
    lib::mechanics::RaceView raceView(Simulation&,uint8_t slot) const;   // racer id = slot
    bool racing() const { return race_!=nullptr; }   // this map has a race (map "race": vehicles::RaceState resource)
private:
    void tick(Simulation&);
    void shoot(Simulation&,CharacterRef);
    void spawnProjectile(Simulation&,Projectile);   // caller checks kMaxProjectiles
    void spawnLoot(Simulation&,Loot);               // caller checks kMaxLoot
    Entity spawnVehicle(Simulation&,Vehicle);
    void clearProjectiles(Simulation&);
    void resetLootAndVehicles(Simulation&);         // round reset: initial loot, initial vehicle values
    void equipInitial(CharacterSnapshot&) const;
    void setupSystems(Simulation&,const Json&);
    void tickOrdinaryProjectiles(Simulation&);
    bool stepOrdinaryProjectile(Simulation&,Projectile&);
    void tickSystems(Simulation&);
    void tickProjectilePrograms(Simulation&);
    bool stepProgrammedProjectile(Simulation&,Projectile&,std::vector<Projectile>&,std::size_t liveCount);
    void actorSystems(Simulation&,CharacterRef,Input&);
    std::vector<HitPose> livePoses(Simulation&) const;
    // rewind = lag-compensated poses, nullptr = the live characters at the time of the call
    bool traceShot(Simulation&,uint8_t,Vec3,Vec3,int32_t,const WeaponConfig&,const std::vector<HitPose>* rewind,Projectile* projectile=nullptr);
    void damage(Simulation&,uint8_t,CharacterRef,int32_t,bool head=false);
    void damageVehicle(Simulation&,uint8_t source,uint32_t vehicleId,int32_t amount);
    void action(Simulation&,CharacterRef,Action,uint32_t);
    void tickMelee(Simulation&,CharacterRef,const Input&);
    void finishRound(Simulation&,int32_t);
    void setupRoyale(Simulation&,const Json&);
    bool skydive(Simulation&,CharacterRef);
    void tickRoyale(Simulation&);
    void settleRoyale(Simulation&);
    void steerRoyaleBot(Simulation&,CharacterRef);
    Fixed groundBelow(Vec3) const;
    std::vector<Loot> buildMapBuildings(const Json& map);
    void setupVehicles(Simulation&,const Json&);
    // race: the race session's pedals this tick (countdown, race bots: vehicles::VehicleRace::pedals).
    void tickVehicles(Simulation&,std::span<const vehicles::RacePedals> race={});
    lib::mechanics::VehicleInput driverInput(const ConstCharacterRef&,uint32_t now) const;
    void setupRace(Simulation&,const Json& rules,const Json& map);
    void tickRace(Simulation&);
    bool validRace(const vehicles::RaceState&,const std::vector<Entity>& typedVehicles) const;
    bool vehicleInteract(Simulation&,CharacterRef);
    vehicles::VehicleDrive* vehicleDrive(Simulation&,uint32_t vehicle) const;
    friend class VehicleWorld;
    friend class RaceSeats;
    bool canFireFromVehicle(Simulation&,const CharacterRef&) const;
    void setupBots(Simulation&,const Json&);
    bool rebuildBotNavigation(std::string* error=nullptr);
    bool thinkBot(Simulation&,CharacterRef);
    template<class T> std::vector<Entity> orderedById(const Simulation&) const;
    std::shared_ptr<BotHost> botHost_;
    // The vehicle system (engine/vehicles): fps.vehicleTypes as shared vehicle types (metres, seat lists), and the
    // race adapter (rules "race" + map "race", docs/races.md); derived, never saved.
    vehicles::DriveRules drive_;
    std::shared_ptr<const vehicles::VehicleRace> race_;
    struct BuildingRegion{world::Bounds bounds;Vec3 door{};std::vector<std::pair<Vec3,Vec3>> stairs,halls;};
    std::vector<BuildingRegion> buildingRegions_;   // generated buildings: footprint (+margin), door, stair flights
    std::vector<world::Bounds> ladders_,sites_;
    std::map<uint32_t,Fixed> penetrable_;
    std::vector<Vehicle> initialVehicles_;
    std::vector<Loot> initialLoot_;
    lib::mechanics::AttackSet meleeAttacks_;

    Config config_;
    world::SpatialWorld collision_;
    std::vector<Vec3> spawns_;
    std::vector<bool> bots_;
    uint64_t contentHash_=0;
    Json streamManifest_;
    std::unique_ptr<world::CollisionStream> streamedCollision_;
    uint64_t navigationCollisionRevision_=UINT64_MAX;
    uint64_t navigationRebuilds_=0;
    std::string navigationReadinessError_;

};
// The actor systems of a simulation run by game::GameCore (engine/game/GameCore.cpp); throws when the map is not world-space.
ActorHost& host(Simulation&);
const ActorHost& host(const Simulation&);
ActorHost* tryHost(Simulation&);
}
