// NEREID cross-genre buoyancy + drag: same component policy in unrelated hosts.
#include "engine/world/nereid/NereidBuoyancySystems.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace nw=aiverse::lib::nereid;
static void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
struct ShipTag {};
struct SwimmerTag {};
template<class Tag>
class Host final:public GameModule {
public:
    Entity object=kNullEntity;
    std::string name()const override{return "nereid-buoyancy-host";}
    void setup(Simulation& sim,const Json&,const Json&)override {
        nw::registerNereidWaterPersistence(sim);
        nw::registerNereidBuoyancyPersistence(sim);
        auto& w=sim.world();
        const Entity sea=w.create();
        nw::WaterBodyComponent water;
        water.kind=nw::WaterKind::Ocean;
        water.footprint=nw::WaterFootprint::Infinite;
        water.surfaceY=10_fx;water.bottomY=0_fx;
        water.flow={1_fx,Fixed{},Fixed{}};
        w.add<nw::WaterBodyComponent>(sea,water);
        object=w.create();
        w.add<Tag>(object);
        Transform t;t.pos={0_fx,9_fx,0_fx};
        w.add<Transform>(object,t);
        w.add<nw::WaterProbeComponent>(object);
        w.add<nw::WaterContactComponent>(object);
        nw::WaterBuoyancyComponent p;
        p.buoyancyAcceleration=12_fx;
        p.linearDrag=2_fx;
        p.immersionHeight=2_fx;
        w.add<nw::WaterBuoyancyComponent>(object,p);
        nw::WaterVelocityInputComponent v;v.velocity={2_fx,Fixed{},Fixed{}};
        w.add<nw::WaterVelocityInputComponent>(object,v);
        w.add<nw::WaterForceComponent>(object);
        nw::addWaterProbeFromTransformSystem(sim,"nereid.probe");
        nw::addWaterSamplingSystem(sim,"nereid.sample");
        nw::addWaterBuoyancySystem(sim,"nereid.buoyancy");
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
template<class Tag>
uint64_t exercise(bool cow) {
    auto module=std::make_unique<Host<Tag>>();
    auto* host=module.get();
    Simulation s(SimConfig{481,20,0,true},std::move(module));
    s.setup(Json::object(),Json::object());
    if(cow)s.world().enableCowSnapshots(true);
    const nw::WaterSample dry=nw::sampleWater(s.world(),{0_fx,12_fx,0_fx});
    check(nw::computeWaterForces(dry,nw::WaterBuoyancyComponent{},Vec3{}).acceleration.y==Fixed{},
          "dry sample accelerated");
    s.step();
    const auto& force=s.world().get<nw::WaterForceComponent>(host->object);
    check(force.immersedFraction==Fixed::fromRatio(1,2),"immersion fraction");
    check(force.acceleration.y==6_fx,"wrong upward acceleration");
    check(force.acceleration.x==-1_fx,"flow-relative drag");
    check(force.acceleration.z==Fixed{},"unexpected lateral force");
    const uint64_t initial=s.checksum();
    const auto bytes=s.serialize();
    auto restored=std::make_unique<Simulation>(SimConfig{481,20,0,true},
                                               std::make_unique<Host<Tag>>());
    restored->setup(Json::object(),Json::object());
    std::string err;
    check(restored->deserialize(bytes,&err),"native save/load");
    check(restored->checksum()==initial,"native checksum mismatch");
    SimSnapshot saved;s.saveState(saved);
    s.world().get<Transform>(host->object).pos.y=12_fx;
    s.step();
    const auto& dryForce=s.world().get<nw::WaterForceComponent>(host->object);
    check(dryForce.immersedFraction==Fixed{} &&
          dryForce.acceleration.y==Fixed{} &&
          dryForce.acceleration.x==Fixed{},"dry force not cleared");
    const uint64_t expected=s.checksum();
    check(expected!=initial,"changed ECS state untracked");
    s.loadState(saved);
    s.world().get<Transform>(host->object).pos.y=12_fx;
    s.step();
    check(s.checksum()==expected,"rollback buoyancy divergence");
    return expected;
}
int main() {
    const uint64_t value=exercise<ShipTag>(false);
    check(exercise<SwimmerTag>(false)==value,"different host results");
    check(exercise<ShipTag>(true)==value,"ship COW differs");
    check(exercise<SwimmerTag>(true)==value,"swimmer COW differs");
    std::cout<<"PASS NEREID buoyancy, drag, native state, two hosts and rollback\n";
}
