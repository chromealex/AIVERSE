// NEREID shared water-body ECS sampling in two independent native hosts.
#include "engine/world/nereid/NereidWaterSystems.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace nw=aiverse::lib::nereid;
static void require(bool yes,const char* msg){if(!yes)throw std::runtime_error(msg);}
struct ShooterTag {};
struct StrategyTag {};

template<class Tag> struct Host final:GameModule {
    Entity lake=kNullEntity,probe=kNullEntity,other=kNullEntity;
    std::string name()const override{return "nereid-water-host";}
    void setup(Simulation& s,const Json&,const Json&)override{
        nw::registerNereidWaterPersistence(s);
        World& w=s.world();
        const Entity ocean=w.create();
        nw::WaterBodyComponent sea;
        sea.kind=nw::WaterKind::Ocean;
        sea.footprint=nw::WaterFootprint::Infinite;
        sea.surfaceY=10_fx;
        sea.bottomY=-100_fx;
        w.add<nw::WaterBodyComponent>(ocean,sea);
        lake=w.create();
        nw::WaterBodyComponent local;
        local.kind=nw::WaterKind::Lake;
        local.footprint=nw::WaterFootprint::Box;
        local.center={0_fx,0_fx,0_fx};
        local.halfExtents={5_fx,5_fx};
        local.surfaceY=12_fx;
        local.bottomY=4_fx;
        local.priority=1;
        w.add<nw::WaterBodyComponent>(lake,local);
        probe=w.create();
        w.add<Tag>(probe);
        Transform transform;transform.pos={0_fx,9_fx,0_fx};
        w.add<Transform>(probe,transform);
        w.add<nw::WaterProbeComponent>(probe);
        w.add<nw::WaterContactComponent>(probe);
        other=w.create();
        w.add<Transform>(other,Transform{{20_fx,8_fx,0_fx}});
        w.add<nw::WaterProbeComponent>(other);
        w.add<nw::WaterContactComponent>(other);
        nw::addWaterProbeFromTransformSystem(s,"nereid-probe-transforms");
        nw::addWaterSamplingSystem(s,"nereid-sample");
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
template<class Tag>
uint64_t exercise(bool cow){
    auto module=std::make_unique<Host<Tag>>();
    auto* host=module.get();
    Simulation sim(SimConfig{313,20,0,true},std::move(module));
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    const nw::WaterSample above=nw::sampleWater(sim.world(),{0_fx,15_fx,0_fx});
    require(above.found && !above.submerged && above.depth==0_fx,"above-surface query");
    const nw::WaterSample outside=nw::sampleWater(sim.world(),{20_fx,8_fx,0_fx});
    require(outside.found && outside.kind==nw::WaterKind::Ocean &&
            outside.depth==2_fx,"ocean fallback");
    sim.step();
    const auto& c=sim.world().get<nw::WaterContactComponent>(host->probe);
    require(c.sample.found && c.sample.body==host->lake &&
            c.sample.depth==3_fx && c.sample.submerged,"lake sample");
    require(sim.world().get<nw::WaterContactComponent>(host->other).sample.kind==
            nw::WaterKind::Ocean,"second probe should use ocean");

    const std::string saved=sim.serialize();
    const uint64_t state=sim.checksum();
    auto loaded=std::make_unique<Simulation>(SimConfig{313,20,0,true},
                                             std::make_unique<Host<Tag>>());
    loaded->setup(Json::object(),Json::object());
    std::string err;
    require(loaded->deserialize(saved,&err),"native NEREID save/load");
    require(loaded->checksum()==state,"native NEREID hash differs");
    SimSnapshot before;sim.saveState(before);
    sim.world().get<Transform>(host->probe).pos.y=15_fx;
    sim.step();
    const auto& dry=sim.world().get<nw::WaterContactComponent>(host->probe);
    require(dry.sample.found && !dry.sample.submerged && dry.sample.depth==0_fx,
            "lake above-surface state");
    const uint64_t changed=sim.checksum();
    require(changed!=state,"contact mutation not hashed");
    sim.loadState(before);
    sim.world().get<Transform>(host->probe).pos.y=15_fx;
    sim.step();
    require(sim.checksum()==changed,"rollback replay diverged");
    return changed;
}
int main(){
    const uint64_t baseline=exercise<ShooterTag>(false);
    require(exercise<StrategyTag>(false)==baseline,"different host results");
    require(exercise<ShooterTag>(true)==baseline,"shooter COW divergence");
    require(exercise<StrategyTag>(true)==baseline,"strategy COW divergence");
    std::cout<<"PASS: NEREID sampling, native save/load, two hosts, COW and replay\n";
}
