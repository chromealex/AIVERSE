// NEREID stage 4: indexed-vs-reference sampling, native hosts and COW replay.
#include "engine/world/nereid/NereidQueryIndex.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace nw=aiverse::lib::nereid;

static void ensure(bool ok,const char* message) {
    if(!ok)throw std::runtime_error(message);
}
static bool same(const nw::WaterSample& a,const nw::WaterSample& b) {
    return a.body==b.body && a.kind==b.kind && a.surfaceY==b.surfaceY &&
           a.bottomY==b.bottomY && a.depth==b.depth &&
           a.flow==b.flow && a.priority==b.priority &&
           a.found==b.found && a.submerged==b.submerged;
}
struct RacingHostTag {};
struct NavalHostTag {};
template<class Tag> struct Host final:GameModule {
    Entity probe=kNullEntity,river=kNullEntity;
    std::string name()const override{return "nereid-index-host";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        nw::registerNereidWaterPersistence(sim);
        nw::registerNereidGeometryPersistence(sim);
        auto& w=sim.world();
        const Entity sea=w.create();
        nw::WaterBodyComponent ocean;
        ocean.footprint=nw::WaterFootprint::Infinite;
        ocean.kind=nw::WaterKind::Ocean;
        ocean.surfaceY=10_fx;ocean.bottomY=-50_fx;
        w.add<nw::WaterBodyComponent>(sea,ocean);
        for(int x=-6;x<=6;++x) {
            const Entity lake=w.create();
            nw::WaterBodyComponent water;
            water.kind=nw::WaterKind::Lake;
            water.footprint=nw::WaterFootprint::Disc;
            water.center={Fixed::fromInt(x*20),Fixed{},15_fx};
            water.radius=4_fx;
            water.priority=1;
            water.surfaceY=12_fx; water.bottomY=2_fx;
            w.add<nw::WaterBodyComponent>(lake,water);
        }
        river=w.create();
        nw::WaterBodyComponent stream;
        stream.kind=nw::WaterKind::River;
        stream.footprint=nw::WaterFootprint::Box;
        stream.surfaceY=11_fx;stream.bottomY=3_fx;stream.priority=2;
        w.add<nw::WaterBodyComponent>(river,stream);
        nw::WaterGeometryComponent spline;
        spline.kind=nw::WaterGeometryKind::RiverSpline;
        spline.points={{-30_fx,0_fx,1_fx},{0_fx,0_fx,2_fx},
                       {30_fx,0_fx,3_fx}};
        w.add<nw::WaterGeometryComponent>(river,spline);

        const Entity polygon=w.create();
        nw::WaterBodyComponent polygonBody;
        polygonBody.kind=nw::WaterKind::Lake;
        polygonBody.surfaceY=14_fx;polygonBody.bottomY=1_fx;
        polygonBody.priority=3;
        w.add<nw::WaterBodyComponent>(polygon,polygonBody);
        nw::WaterGeometryComponent shape;
        shape.kind=nw::WaterGeometryKind::Polygon;
        shape.points={{-5_fx,-5_fx,Fixed{}},{5_fx,-5_fx,Fixed{}},
                      {5_fx,5_fx,Fixed{}},{-5_fx,5_fx,Fixed{}}};
        w.add<nw::WaterGeometryComponent>(polygon,shape);

        // Invalid enabled override must not silently become infinite.
        const Entity invalid=w.create();
        nw::WaterBodyComponent wrong;
        wrong.priority=100;
        wrong.footprint=nw::WaterFootprint::Infinite;
        wrong.surfaceY=100_fx;wrong.bottomY=-100_fx;
        w.add<nw::WaterBodyComponent>(invalid,wrong);
        nw::WaterGeometryComponent bad;
        bad.kind=nw::WaterGeometryKind::Polygon;
        bad.points={{0_fx,0_fx,Fixed{}}};
        w.add<nw::WaterGeometryComponent>(invalid,bad);

        probe=w.create();w.add<Tag>(probe);
        Transform t;t.pos={0_fx,9_fx,0_fx};
        w.add<Transform>(probe,t);
        w.add<nw::WaterProbeComponent>(probe);
        w.add<nw::WaterContactComponent>(probe);
        nw::addWaterProbeFromTransformSystem(sim,"nereid.probe");
        nw::addIndexedWaterSamplingSystem(sim,"nereid.indexed.sample");
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation&,Hasher&) override {}
    int result(Simulation&) override {return 0;}
    bool serializeState(Simulation&,Archive& a) override {return a.ok();}
};

template<class Tag> uint64_t run(bool cow) {
    auto module=std::make_unique<Host<Tag>>();
    auto* host=module.get();
    Simulation sim(SimConfig{923,20,0,true},std::move(module));
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);

    const nw::WaterQueryIndex index(sim.world());
    ensure(index.infiniteBodyCount()==1,"unexpected infinite waters");
    ensure(index.finiteBodyCount()==15,"wrong finite water count");
    for(int x=-140;x<=140;x+=7)
        for(int z=-35;z<=35;z+=5)
            for(int y=8;y<=16;y+=4) {
                const Vec3 p{Fixed::fromInt(x),Fixed::fromInt(y),Fixed::fromInt(z)};
                ensure(same(index.sample(p),nw::sampleWater(sim.world(),p)),
                       "indexed and reference queries differ");
            }
    ensure(index.sample({0_fx,9_fx,0_fx}).priority==3,
           "indexed polygon priority");
    ensure(index.sample({12_fx,9_fx,0_fx}).kind==nw::WaterKind::River,
           "indexed spline lookup");
    ensure(index.sample({120_fx,9_fx,40_fx}).kind==nw::WaterKind::Ocean,
           "indexed ocean fallback");
    sim.step();
    auto contact=sim.world().get<nw::WaterContactComponent>(host->probe);
    ensure(contact.sample.kind==nw::WaterKind::Lake &&
           contact.sample.priority==3,"indexed ECS sampling");

    const uint64_t initial=sim.checksum();
    const std::string bytes=sim.serialize();
    auto loaded=std::make_unique<Simulation>(
        SimConfig{923,20,0,true},std::make_unique<Host<Tag>>());
    loaded->setup(Json::object(),Json::object());
    std::string err;
    ensure(loaded->deserialize(bytes,&err),"native indexed host load");
    ensure(loaded->checksum()==initial,"native indexed host checksum");

    SimSnapshot snapshot;sim.saveState(snapshot);
    // Both geometry mutation and probe movement must be reflected next tick;
    // no spatial cache may survive across the rollback boundary.
    sim.world().get<nw::WaterGeometryComponent>(host->river).points[1].halfWidth=8_fx;
    sim.world().get<Transform>(host->probe).pos={6_fx,9_fx,0_fx};
    sim.step();
    const auto sampled=sim.world().get<nw::WaterContactComponent>(host->probe).sample;
    ensure(sampled.kind==nw::WaterKind::River,"index not rebuilt on edit");
    const uint64_t updated=sim.checksum();
    ensure(updated!=initial,"indexed update checksum unchanged");
    sim.loadState(snapshot);
    sim.world().get<nw::WaterGeometryComponent>(host->river).points[1].halfWidth=8_fx;
    sim.world().get<Transform>(host->probe).pos={6_fx,9_fx,0_fx};
    sim.step();
    ensure(sim.checksum()==updated,"indexed rollback replay mismatch");
    return updated;
}
int main() {
    World empty;
    ensure(!nw::WaterQueryIndex(empty).sample({}).found,"empty-world query");
    const uint64_t baseline=run<NavalHostTag>(false);
    ensure(run<RacingHostTag>(false)==baseline,"cross-genre result differs");
    ensure(run<NavalHostTag>(true)==baseline,"naval COW result differs");
    ensure(run<RacingHostTag>(true)==baseline,"racing COW result differs");
    std::cout<<"PASS NEREID broadphase/reference parity, native save and rollback\n";
}
