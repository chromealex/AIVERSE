// Cross-genre NEREID native setup: adaptive index/reference parity and rollback.
#include "engine/world/nereid/NereidNativeWater.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
using namespace aiverse;
namespace nw=aiverse::lib::nereid;
static void check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct ExplorerTag{};
struct RacerTag{};
template<class Tag> struct Host final:GameModule {
    bool indexed=false;
    Entity probe=kNullEntity;
    explicit Host(bool indexedMode):indexed(indexedMode){}
    std::string name()const override{return "nereid-native-host";}
    void setup(Simulation& s,const Json&,const Json&)override {
        nw::NativeWaterPipelineConfig cfg;
        cfg.adaptive.minWaterBodies=indexed?1:1000;
        cfg.adaptive.minProbes=indexed?1:1000;
        nw::registerNativeWaterPipeline(s,"water",cfg);
        std::vector<nw::WaterBodyComponent> bodies;
        for(int i=0;i<24;++i) {
            nw::WaterBodyComponent b;
            b.footprint=nw::WaterFootprint::Disc;
            b.center={Fixed::fromInt(i*20),Fixed{},Fixed{}};
            b.radius=7_fx;b.surfaceY=10_fx;b.bottomY=-10_fx;
            b.priority=i==0?2:1;
            bodies.push_back(b);
        }
        const auto ids=nw::spawnNativeWaterBodies(s.world(),bodies);
        check(ids.size()==24 && ids.front().valid(),"native bodies not created");
        probe=s.world().create();
        s.world().add<Tag>(probe);
        Transform t;t.pos={2_fx,8_fx,0_fx};
        s.world().add<Transform>(probe,t);
        s.world().add<nw::WaterProbeComponent>(probe);
        s.world().add<nw::WaterContactComponent>(probe);
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
template<class Tag> std::unique_ptr<Simulation> make(bool indexed,bool cow) {
    auto s=std::make_unique<Simulation>(SimConfig{471,20,0,true},
                                        std::make_unique<Host<Tag>>(indexed));
    s->setup(Json::object(),Json::object());
    if(cow)s->world().enableCowSnapshots(true);
    return s;
}
template<class Tag> uint64_t exercise(bool cow) {
    auto linear=make<Tag>(false,cow),indexed=make<Tag>(true,cow);
    check(nw::chooseIndexedWaterSampling(24,1,{1,1}),"indexed branch");
    check(!nw::chooseIndexedWaterSampling(24,1,{1000,1000}),"linear branch");
    check(!nw::chooseIndexedWaterSampling(0,1,{0,0}),"empty body branch");
    for(int i=0;i<3;++i) {
        linear->step();indexed->step();
        const auto first=static_cast<Host<Tag>&>(linear->module()).probe;
        const auto second=static_cast<Host<Tag>&>(indexed->module()).probe;
        const auto& a=linear->world().template get<nw::WaterContactComponent>(first);
        const auto& b=indexed->world().template get<nw::WaterContactComponent>(second);
        check(a.sample.found && b.sample.found && a.sample.depth==2_fx &&
              a.sample.body==b.sample.body && a.sample.flow==b.sample.flow &&
              a.sampledTick==b.sampledTick,"adaptive water contact parity");
        check(linear->checksum()==indexed->checksum(),"adaptive hash mismatch");
    }
    const uint64_t initial=linear->checksum();
    const std::string saved=linear->serialize();
    auto restored=make<Tag>(true,cow);
    std::string err;
    check(restored->deserialize(saved,&err),"native save/load");
    check(restored->checksum()==initial,"native checksum");
    SimSnapshot a,b;linear->saveState(a);indexed->saveState(b);
    const Entity e=static_cast<Host<Tag>&>(linear->module()).probe;
    const Entity f=static_cast<Host<Tag>&>(indexed->module()).probe;
    linear->world().template get<Transform>(e).pos.y=12_fx;
    indexed->world().template get<Transform>(f).pos.y=12_fx;
    linear->step();indexed->step();
    check(!linear->world().template get<nw::WaterContactComponent>(e).sample.submerged &&
          !indexed->world().template get<nw::WaterContactComponent>(f).sample.submerged,
          "water contact not cleared above surface");
    const uint64_t after=linear->checksum();
    check(after==indexed->checksum() && after!=initial,"adaptive replay parity");
    linear->loadState(a);indexed->loadState(b);
    linear->world().template get<Transform>(e).pos.y=12_fx;
    indexed->world().template get<Transform>(f).pos.y=12_fx;
    linear->step();indexed->step();
    check(linear->checksum()==after && indexed->checksum()==after,
          "adaptive rollback divergence");
    return after;
}
int main() {
    const auto reference=exercise<ExplorerTag>(false);
    check(exercise<RacerTag>(false)==reference,"different host result");
    check(exercise<ExplorerTag>(true)==reference,"explorer COW divergence");
    check(exercise<RacerTag>(true)==reference,"racer COW divergence");
    std::cout<<"PASS NEREID adaptive/native setup, save, cross-genre, COW, rollback\n";
}
