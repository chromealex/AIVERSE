// Cross-genre ECS reload, regeneration and time-to-live contract.
// Same systems run in a shooter, an RTS-like host and optional COW snapshots.
#include "engine/mechanics/EcsTimedMechanics.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace lg=aiverse::lib::mechanics;

static void require(bool valid,const char* msg) {
    if(!valid)throw std::runtime_error(msg);
}
struct Shooter {};
struct Strategy {};

template<class Tag>
class Host final : public GameModule {
public:
    Entity weapon=kNullEntity,unit=kNullEntity,temporary=kNullEntity;
    std::string name()const override{return "ecs-shared-timed-mechanics";}
    void setup(Simulation& sim,const Json&,const Json&)override {
        auto& world=sim.world();
        weapon=world.create();
        unit=world.create();
        temporary=world.create();
        world.add<Tag>(weapon);
        world.add<lg::WeaponStateComponent>(weapon,lg::WeaponStateComponent{0,0});
        world.add<lg::WeaponReloadComponent>(weapon,lg::WeaponReloadComponent{8,5,2,0});
        world.add<lg::ReloadIntentComponent>(weapon,lg::ReloadIntentComponent{true});
        world.add<lg::FireIntentComponent>(weapon,
            lg::FireIntentComponent{{},{1_fx,Fixed{},Fixed{}},true});
        world.add<lg::HitPointsComponent>(unit,lg::HitPointsComponent{50,100});
        world.add<lg::HealthRegenerationComponent>(unit,
            lg::HealthRegenerationComponent{20_fx,Fixed{},true});
        world.add<lg::EnergyRegenerationComponent>(unit,
            lg::EnergyRegenerationComponent{2_fx,5_fx,20_fx,true});
        world.add<lg::TimedExpiryComponent>(temporary,lg::TimedExpiryComponent{2});
        lg::addWeaponReloadSystem(sim,"reload");
        lg::addReloadFireGateSystem(sim,"reload-fire-gate");
        lg::addHealthRegenerationSystem(sim,"health-regen");
        lg::addEnergyRegenerationSystem(sim,"energy-regen");
        lg::addTimedExpirySystem(sim,"ttl");
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation& sim,Hasher& hash)override {
        const auto& world=sim.world();
        const auto* w=world.tryGetConst<lg::WeaponStateComponent>(weapon);
        const auto* r=world.tryGetConst<lg::WeaponReloadComponent>(weapon);
        const auto* intent=world.tryGetConst<lg::ReloadIntentComponent>(weapon);
        const auto* fire=world.tryGetConst<lg::FireIntentComponent>(weapon);
        const auto* h=world.tryGetConst<lg::HitPointsComponent>(unit);
        const auto* regen=world.tryGetConst<lg::HealthRegenerationComponent>(unit);
        const auto* e=world.tryGetConst<lg::EnergyRegenerationComponent>(unit);
        require(w&&r&&intent&&fire&&h&&regen&&e,"missing ECS authority");
        hash.i64(w->ammo);hash.u32(w->nextFireTick);
        hash.i64(r->reserve);hash.i64(r->magazineCapacity);
        hash.u32(r->durationTicks);hash.u32(r->finishesAt);
        hash.u32(intent->requested?1:0);hash.u32(fire->requested?1:0);
        hash.i64(h->current);hash.i64(h->maximum);
        hash.i64(regen->remainder.raw);hash.i64(e->current.raw);
        hash.u32(world.alive(temporary)?1:0);
    }
    int result(Simulation&)override{return 0;}
};

template<class Tag>uint64_t exercise(bool cow) {
    auto module=std::make_unique<Host<Tag>>();
    auto* host=module.get();
    Simulation sim(SimConfig{71,20,0,true},std::move(module));
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    SimSnapshot before;
    sim.saveState(before);
    sim.step(); // tick 0: reload starts, fire is suppressed
    const auto& first=sim.world();
    const auto* gate=first.tryGetConst<lg::FireIntentComponent>(host->weapon);
    const auto* reloading=first.tryGetConst<lg::WeaponReloadComponent>(host->weapon);
    require(gate&&!gate->requested,"reload did not cancel fire");
    require(reloading&&reloading->finishesAt==2,"reload must start at tick 0");
    sim.step(); // tick 1: still reloading
    sim.step(); // tick 2: reload finishes, expiry removes temporary
    const auto& world=sim.world();
    const auto* gun=world.tryGetConst<lg::WeaponStateComponent>(host->weapon);
    const auto* reserve=world.tryGetConst<lg::WeaponReloadComponent>(host->weapon);
    const auto* health=world.tryGetConst<lg::HitPointsComponent>(host->unit);
    const auto* energy=world.tryGetConst<lg::EnergyRegenerationComponent>(host->unit);
    require(gun&&gun->ammo==5,"magazine fill mismatch");
    require(reserve&&reserve->reserve==3&&reserve->finishesAt==0,"reload reserve mismatch");
    require(health&&health->current==53,"health regeneration should be 1 per tick");
    require(energy&&energy->current==5_fx,"energy saturation mismatch");
    require(!world.alive(host->temporary),"TTL did not despawn entity");
    const uint64_t expected=sim.checksum();
    sim.loadState(before);
    require(sim.world().alive(host->temporary),"rollback failed to restore expired entity");
    for(int i=0;i<3;++i)sim.step();
    require(sim.checksum()==expected,"rollback ECS checksum diverged");
    auto* intent=sim.world().tryGet<lg::FireIntentComponent>(host->weapon);
    require(intent!=nullptr,"fire intent missing");
    intent->requested=true;
    sim.step(); // tick 3: no reload, so fire request survives gate
    require(sim.world().tryGetConst<lg::FireIntentComponent>(host->weapon)->requested,
            "ready weapon fire intent unexpectedly suppressed");
    return expected;
}
int main() {
    const uint64_t baseline=exercise<Shooter>(false);
    require(exercise<Strategy>(false)==baseline,"cross-genre state mismatch");
    require(exercise<Shooter>(true)==baseline,"shooter COW checksum mismatch");
    require(exercise<Strategy>(true)==baseline,"strategy COW checksum mismatch");
    std::cout<<"PASS: cross-genre reload, fire gate, regen, expiry and rollback\n";
}
