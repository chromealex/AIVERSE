// One complete ECS shooting mechanic in two independent game genres:
// input -> weapon cooldown/ammo -> spawn -> flight -> hit -> health damage.
// No FPS/RTS include or library link. Snapshot/COW replay parity is required.
#include "engine/mechanics/EcsWeaponLaunchSystems.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

using namespace aiverse;
namespace lg=aiverse::lib::mechanics;
static void require(bool ok,const char* why) {
    if(!ok)throw std::runtime_error(why);
}

struct ShooterTag {};
struct ArtilleryTag {};
struct DamageSummary {uint32_t count=0;int32_t total=0;};

template<class Tag>
class Host final:public GameModule {
    world::SpatialWorld collision_{4};
public:
    Entity gun=kNullEntity,target=kNullEntity;
    std::string name()const override{return "composable-ecs-weapons";}
    void setup(Simulation& sim,const Json&,const Json&)override {
        World& world=sim.world();
        require(collision_.add({42,{{3_fx,-1_fx,-1_fx},{4_fx,1_fx,1_fx}}}),
                "collision fixture setup failed");
        gun=world.create();
        target=world.create();
        world.add<Tag>(gun);
        world.add<lg::WeaponParametersComponent>(gun,
            lg::WeaponParametersComponent{20_fx,Fixed{},Fixed{},60,6,35});
        world.add<lg::WeaponStateComponent>(gun,lg::WeaponStateComponent{1,0});
        world.add<lg::FireIntentComponent>(gun,
            lg::FireIntentComponent{{Fixed{},Fixed{},Fixed{}},
                                    {1_fx,Fixed{},Fixed{}},true});
        world.add<lg::HitPointsComponent>(target,lg::HitPointsComponent{100,100});
        lg::ColliderEntityIndex targets;
        targets.byCollider.emplace(42,target);
        world.setResource<lg::ColliderEntityIndex>(std::move(targets));
        world.setResource<lg::ProjectileSpawnQueue>();
        world.setResource<lg::DamageQueue>();
        world.setResource<DamageSummary>();

        lg::addWeaponLaunchSystem(sim,"shared-launch");
        lg::addProjectileSpawnSystem(sim,"shared-spawn");
        lg::addProjectileFlightSystem(sim,"shared-flight",
            [this](Simulation&)->const world::SpatialWorld&{return collision_;},
            [](Simulation& current,const lg::ProjectileImpactEvent& impact) {
                lg::enqueueProjectileDamage(current,impact);
            });
        lg::addDamageResolutionSystem(sim,"shared-damage",
            [](Simulation& current,const lg::DamageApplied& report) {
                auto& summary=*current.world().resource<DamageSummary>();
                ++summary.count;
                summary.total+=report.applied;
            });
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation& sim,Hasher& hash)override {
        const World& world=sim.world();
        const auto* gunState=world.tryGetConst<lg::WeaponStateComponent>(gun);
        const auto* fire=world.tryGetConst<lg::FireIntentComponent>(gun);
        const auto* life=world.tryGetConst<lg::HitPointsComponent>(target);
        const auto* queue=world.resourceConst<lg::ProjectileSpawnQueue>();
        const auto* summary=world.resourceConst<DamageSummary>();
        require(gunState&&fire&&life&&queue&&summary,"missing authoritative ECS state");
        hash.i64(gunState->ammo);
        hash.u32(gunState->nextFireTick);
        hash.u32(fire->requested?1:0);
        hash.i64(life->current);
        hash.u32(queue->nextShotId);
        hash.u32(summary->count);
        hash.i64(summary->total);
        world.eachExistingConst<lg::ProjectileFlightComponent>(
            [&](Entity e,const lg::ProjectileFlightComponent& p) {
                if(!world.alive(e))return;
                hash.u64(e.packed());
                hash.u32(p.shotId);
                hash.i64(p.state.position.x.raw);
                hash.u32(p.state.age);
            });
    }
    int result(Simulation&)override{return 0;}
};

template<class Tag>
uint64_t exercise(bool cow) {
    auto module=std::make_unique<Host<Tag>>();
    auto* host=module.get();
    Simulation sim(SimConfig{31,20,0,true},std::move(module));
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    SimSnapshot original;
    sim.saveState(original);
    for(int i=0;i<8;++i)sim.step();
    const World& world=sim.world();
    const auto* hp=world.tryGetConst<lg::HitPointsComponent>(host->target);
    const auto* gun=world.tryGetConst<lg::WeaponStateComponent>(host->gun);
    const auto* summary=world.resourceConst<DamageSummary>();
    const auto* spawns=world.resourceConst<lg::ProjectileSpawnQueue>();
    require(hp&&hp->current==65,"wrong projectile damage");
    require(gun&&gun->ammo==0,"weapon ammo not consumed");
    require(spawns&&spawns->nextShotId==2,"non-deterministic shot identity");
    require(summary&&summary->count==1&&summary->total==35,"damage event mismatch");
    require(world.countExisting<lg::ProjectileFlightComponent>()==0,
            "projectile not despawned");
    const auto expected=sim.checksum();
    sim.loadState(original);
    require(sim.world().tryGetConst<lg::WeaponStateComponent>(host->gun)->ammo==1,
            "rollback failed to restore ammo");
    for(int i=0;i<8;++i)sim.step();
    require(sim.checksum()==expected,"replayed firing checksum mismatch");
    SimSnapshot afterFirstShot;
    sim.saveState(afterFirstShot);
    // A second request with an empty magazine must be consumed without
    // spawning a projectile or advancing the shot sequence.
    auto* retry=sim.world().tryGet<lg::FireIntentComponent>(host->gun);
    require(retry!=nullptr,"missing fire intent after shot");
    retry->requested=true;
    sim.step();
    const auto* afterDryFire=sim.world().resourceConst<lg::ProjectileSpawnQueue>();
    const auto* afterDryState=sim.world().tryGetConst<lg::WeaponStateComponent>(host->gun);
    const auto* afterDryIntent=sim.world().tryGetConst<lg::FireIntentComponent>(host->gun);
    require(afterDryFire&&afterDryFire->nextShotId==2&&afterDryFire->pending.empty(),
            "dry fire must not allocate projectile IDs");
    require(afterDryState&&afterDryState->ammo==0,"dry fire changed ammo");
    require(afterDryIntent&&!afterDryIntent->requested,"dry fire request was not consumed");
    const auto dryChecksum=sim.checksum();
    sim.loadState(afterFirstShot);
    auto* replayIntent=sim.world().tryGet<lg::FireIntentComponent>(host->gun);
    require(replayIntent!=nullptr,"dry fire replay lost intent component");
    replayIntent->requested=true;
    sim.step();
    require(sim.checksum()==dryChecksum,"dry fire rollback/replay checksum mismatch");
    const auto* finalHp=sim.world().tryGetConst<lg::HitPointsComponent>(host->target);
    const auto* finalSummary=sim.world().resourceConst<DamageSummary>();
    require(finalHp&&finalHp->current==65,"dry fire changed target health");
    require(finalSummary&&finalSummary->count==1&&finalSummary->total==35,
            "dry fire generated an extra damage event");
    return dryChecksum;
}
// A second input inside the fire interval must not produce another shot.
// Check both snapshot implementations and both independent host types.
template<class Tag>
uint64_t exerciseCooldown(bool cow) {
    auto module=std::make_unique<Host<Tag>>();
    auto* host=module.get();
    Simulation sim(SimConfig{31,20,0,true},std::move(module));
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    // Keep a second round available to distinguish cooldown from dry fire.
    auto* weapon=sim.world().tryGet<lg::WeaponStateComponent>(host->gun);
    require(weapon!=nullptr,"cooldown fixture missing weapon");
    weapon->ammo=2;
    sim.step();
    SimSnapshot afterShot;
    sim.saveState(afterShot);
    auto fireAgain=[&]() {
        auto* intent=sim.world().tryGet<lg::FireIntentComponent>(host->gun);
        require(intent!=nullptr,"cooldown fixture missing intent");
        intent->requested=true;
        sim.step();
        const auto* state=sim.world().tryGetConst<lg::WeaponStateComponent>(host->gun);
        const auto* queue=sim.world().resourceConst<lg::ProjectileSpawnQueue>();
        const auto* consumed=sim.world().tryGetConst<lg::FireIntentComponent>(host->gun);
        require(state&&state->ammo==1,"cooldown consumed extra ammo");
        require(queue&&queue->nextShotId==2&&queue->pending.empty(),
                "cooldown allocated extra projectile");
        require(consumed&&!consumed->requested,"cooldown did not consume input");
        return sim.checksum();
    };
    const uint64_t expected=fireAgain();
    sim.loadState(afterShot);
    require(fireAgain()==expected,"cooldown snapshot replay mismatch");
    // Once the interval has elapsed, the remaining round must fire exactly
    // once. This distinguishes a rejected early request from a stuck weapon.
    while(sim.tick()<6)sim.step();
    SimSnapshot ready;
    sim.saveState(ready);
    auto fireReady=[&]() {
        auto* intent=sim.world().tryGet<lg::FireIntentComponent>(host->gun);
        require(intent!=nullptr,"ready fixture missing intent");
        intent->requested=true;
        sim.step();
        const auto* state=sim.world().tryGetConst<lg::WeaponStateComponent>(host->gun);
        const auto* queue=sim.world().resourceConst<lg::ProjectileSpawnQueue>();
        const auto* consumed=sim.world().tryGetConst<lg::FireIntentComponent>(host->gun);
        require(state&&state->ammo==0,"ready weapon did not consume final round");
        require(queue&&queue->nextShotId==3&&queue->pending.empty(),
                "ready weapon did not spawn exactly one shot");
        require(consumed&&!consumed->requested,"ready shot intent not consumed");
        return sim.checksum();
    };
    const uint64_t readyChecksum=fireReady();
    sim.loadState(ready);
    require(fireReady()==readyChecksum,"ready shot rollback/replay mismatch");
    return readyChecksum;
}
int main(){
    const uint64_t expected=exercise<ShooterTag>(false);
    require(exercise<ArtilleryTag>(false)==expected,"artillery/shooter mismatch");
    require(exercise<ShooterTag>(true)==expected,"shooter COW mismatch");
    require(exercise<ArtilleryTag>(true)==expected,"artillery COW mismatch");
    const uint64_t cooldown=exerciseCooldown<ShooterTag>(false);
    require(exerciseCooldown<ArtilleryTag>(false)==cooldown,"cooldown cross-host mismatch");
    require(exerciseCooldown<ShooterTag>(true)==cooldown,"cooldown COW mismatch");
    require(exerciseCooldown<ArtilleryTag>(true)==cooldown,"artillery cooldown COW mismatch");
    std::cout<<"PASS reusable ECS launch/spawn/ballistics/impact/damage across RTS and FPS style hosts\n";
}
