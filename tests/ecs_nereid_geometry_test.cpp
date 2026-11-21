// NEREID polygon/spline water geometry in two unrelated native game hosts.
#include <aiverselib/water/NereidWaterSystems.h>
#include <aiverselib/water/NereidFootprintDistance.h>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace nw=aiverse::lib::nereid;
static void check(bool condition,const char* error) {
    if(!condition)throw std::runtime_error(error);
}
struct SailingGameTag {};
struct AdventureGameTag {};

template<class Tag> struct Host final:GameModule {
    Entity probe=kNullEntity;
    std::string name()const override{return "nereid-geometry-host";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        nw::registerNereidWaterPersistence(sim);
        nw::registerNereidGeometryPersistence(sim);
        auto& w=sim.world();
        const Entity ocean=w.create();
        nw::WaterBodyComponent sea;
        sea.kind=nw::WaterKind::Ocean;
        sea.footprint=nw::WaterFootprint::Infinite;
        sea.surfaceY=10_fx; sea.bottomY=-50_fx;
        w.add<nw::WaterBodyComponent>(ocean,sea);

        const Entity lake=w.create();
        nw::WaterBodyComponent lakeBody;
        lakeBody.kind=nw::WaterKind::Lake;
        lakeBody.footprint=nw::WaterFootprint::Box; // overridden
        lakeBody.center={0_fx,0_fx,0_fx};
        lakeBody.surfaceY=12_fx; lakeBody.bottomY=2_fx; lakeBody.priority=1;
        w.add<nw::WaterBodyComponent>(lake,lakeBody);
        nw::WaterGeometryComponent polygon;
        polygon.points={{-5_fx,-5_fx,Fixed{}},{5_fx,-5_fx,Fixed{}},
                        {5_fx,5_fx,Fixed{}},{-5_fx,5_fx,Fixed{}}};
        w.add<nw::WaterGeometryComponent>(lake,polygon);

        const Entity river=w.create();
        nw::WaterBodyComponent riverBody;
        riverBody.kind=nw::WaterKind::River;
        riverBody.footprint=nw::WaterFootprint::Box;
        riverBody.surfaceY=11_fx;riverBody.bottomY=3_fx;riverBody.priority=2;
        w.add<nw::WaterBodyComponent>(river,riverBody);
        nw::WaterGeometryComponent spline;
        spline.kind=nw::WaterGeometryKind::RiverSpline;
        spline.points={{-10_fx,0_fx,1_fx},{10_fx,0_fx,3_fx}};
        w.add<nw::WaterGeometryComponent>(river,spline);

        probe=w.create(); w.add<Tag>(probe);
        Transform transform;transform.pos={0_fx,9_fx,0_fx};
        w.add<Transform>(probe,transform);
        w.add<nw::WaterProbeComponent>(probe);
        w.add<nw::WaterContactComponent>(probe);
        nw::addWaterProbeFromTransformSystem(sim,"water.probe");
        nw::addWaterSamplingSystem(sim,"water.sample");
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation&,Hasher&) override {}
    int result(Simulation&) override {return 0;}
    bool serializeState(Simulation&,Archive& a) override {return a.ok();}
};

template<class Tag> uint64_t run(bool cow) {
    auto module=std::make_unique<Host<Tag>>();auto* host=module.get();
    Simulation sim(SimConfig{417,20,0,true},std::move(module));
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    const auto& world=sim.world();

    // River takes precedence over the lake even at a lower surface level.
    check(nw::sampleWater(world,{0_fx,9_fx,0_fx}).kind==nw::WaterKind::River,
          "river overlap priority");
    check(nw::sampleWater(world,{0_fx,9_fx,4_fx}).kind==nw::WaterKind::Lake,
          "polygon interior");
    check(nw::sampleWater(world,{5_fx,9_fx,5_fx}).kind==nw::WaterKind::Lake,
          "polygon edge inclusive");
    check(nw::sampleWater(world,{5_fx,9_fx,6_fx}).kind==nw::WaterKind::Ocean,
          "polygon exterior");
    check(nw::sampleWater(world,{10_fx,9_fx,3_fx}).kind==nw::WaterKind::River,
          "river varying width endpoint");
    check(nw::sampleWater(world,{10_fx,9_fx,4_fx}).kind==nw::WaterKind::Ocean,
          "river width exterior");

    sim.step();
    const auto& contact=sim.world().get<nw::WaterContactComponent>(host->probe);
    check(contact.sample.kind==nw::WaterKind::River && contact.sample.depth==2_fx,
          "ECS geometry contact");
    const uint64_t initial=sim.checksum();
    const std::string saved=sim.serialize();
    auto reloaded=std::make_unique<Simulation>(
        SimConfig{417,20,0,true},std::make_unique<Host<Tag>>());
    reloaded->setup(Json::object(),Json::object());
    std::string error;
    check(reloaded->deserialize(saved,&error),"native geometry load");
    check(reloaded->checksum()==initial,"geometry checksum after load");
    check(nw::sampleWater(reloaded->world(),{0_fx,9_fx,4_fx}).kind==
          nw::WaterKind::Lake,"loaded polygon geometry");

    SimSnapshot rollback;sim.saveState(rollback);
    sim.world().get<Transform>(host->probe).pos.z=4_fx;
    sim.step();
    check(sim.world().get<nw::WaterContactComponent>(host->probe).sample.kind==
          nw::WaterKind::Lake,"geometry resample after moving");
    const uint64_t moved=sim.checksum();
    check(moved!=initial,"changed geometry contact not hashed");
    sim.loadState(rollback);
    sim.world().get<Transform>(host->probe).pos.z=4_fx;
    sim.step();
    check(sim.checksum()==moved,"geometry COW/rollback diverged");

    nw::WaterGeometryComponent invalid;
    invalid.kind=nw::WaterGeometryKind::RiverSpline;
    invalid.points={{0_fx,0_fx,1_fx},{1_fx,0_fx,Fixed{}}};
    check(!nw::coversWaterGeometry(invalid,{0_fx,0_fx,0_fx}),
          "invalid spline width accepted");
    check(!nw::validWaterGeometry(nw::WaterGeometryComponent{}),
          "empty polygon accepted");
    return moved;
}
int main() {
    {
        nw::WaterBodyComponent b;b.halfExtents={2_fx,3_fx};
        check(nw::waterFootprintDistance(b,nullptr,{0_fx,0_fx})==-2_fx,"box interior distance");
        check(nw::waterFootprintDistance(b,nullptr,{2_fx,0_fx})==0_fx,"box boundary distance");
        check(nw::waterFootprintDistance(b,nullptr,{5_fx,0_fx})==3_fx,"box exterior distance");
        b.footprint=nw::WaterFootprint::Disc;b.radius=3_fx;
        check(nw::waterFootprintDistance(b,nullptr,{-5_fx,0_fx})==2_fx,"disc distance");
        nw::WaterGeometryComponent g;g.kind=nw::WaterGeometryKind::RiverSpline;
        g.points={{0_fx,-10_fx,2_fx},{0_fx,10_fx,4_fx}};
        check(nw::waterFootprintDistance(b,&g,{5_fx,0_fx})==2_fx,"tapered river exterior");
        check(nw::waterFootprintDistance(b,&g,{0_fx,0_fx})==-3_fx,"river interior");
        check(nw::waterFootprintDistance(b,&g,{0_fx,13_fx})==-1_fx,"river round end");
        g.kind=nw::WaterGeometryKind::Polygon;g.points={{0_fx,0_fx,{}},{4_fx,0_fx,{}},{4_fx,4_fx,{}},{0_fx,4_fx,{}}};
        check(nw::waterFootprintDistance(b,&g,{2_fx,2_fx})==-2_fx,"polygon interior");
        check(nw::waterFootprintDistance(b,&g,{7_fx,2_fx})==3_fx,"polygon exterior");
        check(nw::waterFootprintDistance(b,&g,{4_fx,4_fx})==0_fx,"polygon vertex");
        for(int x=-8;x<=8;++x)for(int z=-8;z<=8;++z) {
            const Vec2 point{Fixed::fromInt(x),Fixed::fromInt(z)};
            const auto distance=nw::waterFootprintDistance(b,&g,point);
            check(distance.has_value(),"valid polygon missing distance");
            check((*distance<=0_fx)==nw::coversWaterBody(b,&g,{point.x,{},point.y}),"clearance membership mismatch");
        }
        g.points.clear();b.footprint=nw::WaterFootprint::Infinite;
        check(!nw::waterFootprintDistance(b,&g,{}),"invalid geometry fell back to ocean");
        g.enabled=false;check(nw::waterFootprintDistance(b,&g,{}).value()<0_fx,"disabled override fallback");
        b.enabled=false;check(!nw::waterFootprintDistance(b,nullptr,{}),"disabled water distance");
    }
    const uint64_t baseline=run<SailingGameTag>(false);
    check(run<AdventureGameTag>(false)==baseline,"different genre divergence");
    check(run<SailingGameTag>(true)==baseline,"sailing COW divergence");
    check(run<AdventureGameTag>(true)==baseline,"adventure COW divergence");
    std::cout<<"PASS NEREID polygon/spline, native save/load, two hosts and rollback\n";
}
