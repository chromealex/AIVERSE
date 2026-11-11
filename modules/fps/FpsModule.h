#pragma once
#include "engine/sim/Simulation.h"
#include "engine/world/SpatialWorld.h"
#include <deque>
#include "FpsSystems.h"

namespace aiverse::fps {
constexpr uint16_t kInputCommand=1;
enum Buttons : uint32_t { Fire=1, Jump=2, Crouch=4, Reload=8, Sprint=16, WeaponMask=224, Aim=256, Prone=512 };
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
};
struct Config {
    uint32_t tickRate=60,snapshotRate=20,maxPlayers=100,rewindTicks=12,inputTimeout=15;
    Fixed interestRadius=250_fx,worldLimit=100000_fx;
    MotorConfig motor; WeaponConfig weapon;SystemsConfig systems;
    std::vector<WeaponConfig> weapons;
    std::string mode="deathmatch"; // deathmatch / elimination / battleRoyale
    uint32_t respawnTicks=180,roundTicks=7200,zoneStart=1800,zoneDuration=7200;
    Vec3 zoneCenter{};Fixed zoneRadius=2000_fx,zoneEndRadius=10_fx;
    int32_t zoneDamage=5;
    bool friendlyFire=false;
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

    bool operator==(const Actor&) const = default;
};
struct HistoryFrame { uint32_t tick=0; std::vector<Actor> actors; };
struct State {
    std::vector<Actor> actors;
    std::deque<HistoryFrame> history;
    uint32_t round=1; int32_t winner=0;
    uint32_t nextObject=1;std::vector<Projectile> projectiles;std::vector<Loot> loot;std::vector<Vehicle> vehicles;RoundState match;

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
class Module final : public GameModule {
public:
    std::string name() const override { return "fps"; }
    void setup(Simulation&,const Json&,const Json&) override;
    void applyCommand(Simulation&,const Command&) override;
    void hashState(Simulation&,Hasher&) override;
    bool serializeState(Simulation&,Archive&) override;
    int result(Simulation& sim) override { return state(sim).winner; }
    const Config& config() const { return config_; }
    const world::SpatialWorld& collision() const { return collision_; }
    State& state(Simulation& sim) const { return *sim.world().resource<State>(); }
private:
    void tick(Simulation&);
    void shoot(Simulation&,Actor&);
    void equipInitial(Actor&) const;
    void setupSystems(Simulation&,const Json&,State&);
    void tickSystems(Simulation&);
    void actorSystems(Simulation&,Actor&,Input&);
    bool traceShot(Simulation&,uint8_t,Vec3,Vec3,int32_t,const WeaponConfig&,const std::vector<Actor>&,Projectile* projectile=nullptr);
    void damage(Simulation&,uint8_t,Actor&,int32_t,bool head=false);
    void action(Simulation&,Actor&,Action,uint32_t);
    void finishRound(Simulation&,int32_t);
    std::vector<world::Bounds> ladders_,sites_;
    std::map<uint32_t,Fixed> penetrable_;
    std::vector<Vehicle> initialVehicles_;
    std::vector<Loot> initialLoot_;

    Config config_;
    world::SpatialWorld collision_;
    std::vector<Vec3> spawns_;
    std::vector<bool> bots_;
    uint64_t contentHash_=0;
};
}
