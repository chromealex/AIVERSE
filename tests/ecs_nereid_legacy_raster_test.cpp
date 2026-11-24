// NEREID stage 5: read-only legacy waterNetwork raster parity, native ECS.
#include "engine/view/game/NereidLegacyWaterAdapter.h"
#include "engine/world/nereid/NereidQueryIndex.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace nw=aiverse::lib::nereid;
namespace oldwater=aiverse::game::water;
static void check(bool ok,const char* why) {
    if(!ok)throw std::runtime_error(why);
}
static oldwater::Network network() {
    const auto types=oldwater::loadShoreTypes(Json());
    return oldwater::parseNetwork(Json::parse(R"({
        "bodies":[
           {"id":1,"kind":"sea","side":"south","shore":[[-1,62],[24,61],[48,59],[80,62]]},
           {"id":2,"kind":"river","points":[[28,-1,14],[30,32,18],[33,65,16]]},
           {"id":3,"kind":"strait","points":[[-1,12,24],[30,15,26],[65,16,24]]}
        ]
    })"),types);
}
static std::vector<nw::WaterBodyComponent> imported() {
    nw::WaterRasterImportConfig cfg;
    cfg.cellMeters=4_fx;cfg.surfaceY=8_fx;cfg.bottomY=-4_fx;
    return oldwater::importWaterNetworkToNereid(network(),80,64,4.0f,cfg);
}
static void testRasterParity(const World& w) {
    const oldwater::Network n=network();
    const auto g=oldwater::buildGeometry(n,80,64,4.0f);
    const auto wet=oldwater::waterCells(n,g,80,64,4.0f);
    const nw::WaterQueryIndex index(w);
    for(int z=0;z<64;++z)for(int x=0;x<80;++x) {
        const Vec3 center{Fixed::fromInt(x*4+2),7_fx,Fixed::fromInt(z*4+2)};
        const bool expected=wet[size_t(z)*80+size_t(x)]!=0;
        check(nw::sampleWater(w,center).found==expected,"legacy reference cell mismatch");
        check(index.sample(center).found==expected,"legacy indexed cell mismatch");
    }
    check(!index.sample({-6_fx,7_fx,-6_fx}).found,"outside map incorrectly wet");
    check(!index.sample({400_fx,7_fx,300_fx}).found,"outside max map wet");
}
struct TacticalHostTag{};
struct DrivingHostTag{};
template<class Tag> struct Host final:GameModule {
    Entity probe=kNullEntity;
    std::string name()const override{return "nereid-legacy-raster-host";}
    void setup(Simulation& sim,const Json&,const Json&) override {
        nw::registerNereidWaterPersistence(sim);
        auto& w=sim.world();
        for(const auto& body:imported()) {
            const Entity e=w.create();
            w.add<nw::WaterBodyComponent>(e,body);
        }
        probe=w.create();
        w.add<Tag>(probe);
        Transform transform;transform.pos={40_fx,7_fx,252_fx};
        w.add<Transform>(probe,transform);
        w.add<nw::WaterProbeComponent>(probe);
        w.add<nw::WaterContactComponent>(probe);
        nw::addWaterProbeFromTransformSystem(sim,"water.probe");
        nw::addIndexedWaterSamplingSystem(sim,"water.contact");
    }
    void applyCommand(Simulation&,const Command&) override {}
    void hashState(Simulation&,Hasher&) override {}
    int result(Simulation&) override{return 0;}
    bool serializeState(Simulation&,Archive& archive) override{return archive.ok();}
};
template<class Tag> uint64_t run(bool cow) {
    auto module=std::make_unique<Host<Tag>>();
    auto* host=module.get();
    Simulation sim(SimConfig{9001,20,0,true},std::move(module));
    sim.setup(Json::object(),Json::object());
    if(cow)sim.world().enableCowSnapshots(true);
    testRasterParity(sim.world());
    sim.step();
    check(sim.world().get<nw::WaterContactComponent>(host->probe).sampledTick==
          sim.tick()-1,"indexed contact tick");
    const std::string bytes=sim.serialize();
    check(sim.world().get<nw::WaterContactComponent>(host->probe).sample.found,
          "initial probe should sample sea water");
    const uint64_t original=sim.checksum();
    auto copy=std::make_unique<Simulation>(SimConfig{9001,20,0,true},
                                           std::make_unique<Host<Tag>>());
    copy->setup(Json::object(),Json::object());
    std::string error;
    check(copy->deserialize(bytes,&error),"native raster import load");
    check(copy->checksum()==original,"native raster import checksum");
    SimSnapshot snapshot;sim.saveState(snapshot);
    sim.world().get<Transform>(host->probe).pos={40_fx,7_fx,220_fx};
    sim.step();
    check(!sim.world().get<nw::WaterContactComponent>(host->probe).sample.found,
          "moved probe must be on dry land");
    const uint64_t moved=sim.checksum();
    check(moved!=original,"raster probe mutation not hashed");
    sim.loadState(snapshot);
    sim.world().get<Transform>(host->probe).pos={40_fx,7_fx,220_fx};
    sim.step();
    check(sim.checksum()==moved,"raster rollback replay mismatch");
    return moved;
}
int main() {
    const std::vector<uint8_t> exact={
        1,1,0,1,
        1,1,0,1,
        0,1,0,1
    };
    const auto rect=nw::rectangleWaterCells(exact,4,3);
    check(rect.size()==3,"wrong vertical run merge");
    std::vector<uint8_t> none(12,0);
    check(nw::waterBodiesFromRaster(none,4,3).empty(),"dry raster produced water");
    // Importing from canonical game tiles avoids any rerasterization.
    std::vector<char> tiles={'~','~','.','~','.','.','.','~'};
    nw::WaterRasterImportConfig settings;
    settings.cellMeters=4_fx;
    const auto baked=oldwater::importBakedWaterCellsToNereid(tiles,4,2,settings);
    check(baked.size()==2,"canonical tile rectangles not preserved");
    World bakedWorld;
    for(const auto& body:baked) {
        const Entity e=bakedWorld.create();
        bakedWorld.add<nw::WaterBodyComponent>(e,body);
    }
    for(int z=0;z<2;++z)for(int x=0;x<4;++x) {
        const Vec3 p{Fixed::fromInt(x*4+2),Fixed{},Fixed::fromInt(z*4+2)};
        check(nw::sampleWater(bakedWorld,p).found==(tiles[size_t(z)*4+size_t(x)]=='~'),
              "actual baked water tile parity");
    }
    const uint64_t baseline=run<TacticalHostTag>(false);
    check(run<DrivingHostTag>(false)==baseline,"genre raster divergence");
    check(run<TacticalHostTag>(true)==baseline,"COW tactical divergence");
    check(run<DrivingHostTag>(true)==baseline,"COW driving divergence");
    std::cout<<"PASS NEREID exact legacy wet cells, indexed parity, native state and rollback\n";
}
