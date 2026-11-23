// ECHO: native ECS propagation, two independent module types, rollback and save proof.
#include "engine/world/echo/EchoAcoustics.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
using namespace aiverse;
namespace ea=aiverse::echo;

template<int HostId>
struct EchoHost final : GameModule {
    Entity hall=kNullEntity,room=kNullEntity,exit=kNullEntity;
    Entity firstPortal=kNullEntity,secondPortal=kNullEntity;
    Entity emitter=kNullEntity,receiver=kNullEntity;
    std::string name()const override{return "echo-native";}

    void setup(Simulation& sim,const Json&,const Json&) override {
        ea::install(sim);
        auto& world=sim.world();
        hall=world.create();world.add<ea::AcousticZone>(hall);
        room=world.create();world.add<ea::AcousticZone>(room);
        exit=world.create();world.add<ea::AcousticZone>(exit);
        firstPortal=world.create();
        world.add<ea::AcousticPortal>(firstPortal,ea::AcousticPortal{hall,room,25,true,true});
        secondPortal=world.create();
        world.add<ea::AcousticPortal>(secondPortal,ea::AcousticPortal{room,exit,20,true,true});
        emitter=world.create();
        world.add<ea::AcousticEmitter>(emitter,ea::AcousticEmitter{hall,100,true});
        receiver=world.create();
        world.add<ea::AcousticReceiver>(receiver,ea::AcousticReceiver{exit,30,true});
        world.add<ea::AcousticHearing>(receiver);
    }
    void applyCommand(Simulation&,const Command&)override{}
    // Registered ECS components are the authoritative state: do not hash twice.
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& archive)override{return archive.ok();}
};

template<int N>
std::unique_ptr<Simulation> make(bool cow=false){
    auto sim=std::make_unique<Simulation>(SimConfig{61,20,0,true},
                                         std::make_unique<EchoHost<N>>());
    sim->setup(Json::object(),Json::object());
    if(cow)sim->world().enableCowSnapshots();
    return sim;
}
template<int N>
const ea::AcousticHearing& hearing(const Simulation& sim){
    const auto& host=static_cast<const EchoHost<N>&>(sim.module());
    const auto* output=sim.world().tryGetConst<ea::AcousticHearing>(host.receiver);
    AIVERSE_TEST_CHECK(output);
    return *output;
}
int main(){
    auto a=make<1>();
    auto b=make<2>(true);
    for(int i=0;i<4;++i){a->step();b->step();}
    AIVERSE_TEST_CHECK(hearing<1>(*a).valid && hearing<1>(*a).audible);
    AIVERSE_TEST_CHECK(hearing<1>(*a).intensity==55);
    AIVERSE_TEST_CHECK(hearing<1>(*a).source==static_cast<EchoHost<1>&>(a->module()).emitter);
    AIVERSE_TEST_CHECK(hearing<2>(*b).intensity==55);
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());

    const std::string saved=a->serialize();
    auto loaded=make<1>();
    std::string error;
    AIVERSE_TEST_CHECK(loaded->deserialize(saved,&error));
    AIVERSE_TEST_CHECK(loaded->checksum()==a->checksum());
    AIVERSE_TEST_CHECK(hearing<1>(*loaded).intensity==55);
    SimSnapshot snapshot;
    b->saveState(snapshot);
    const auto previous=b->checksum();
    auto& h=static_cast<EchoHost<2>&>(b->module());
    b->world().get<ea::AcousticPortal>(h.secondPortal).enabled=false;
    b->step();
    AIVERSE_TEST_CHECK(hearing<2>(*b).valid&&!hearing<2>(*b).audible);
    b->loadState(snapshot);
    AIVERSE_TEST_CHECK(b->checksum()==previous);
    b->step();
    a->step();
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());

    auto& h1=static_cast<EchoHost<1>&>(a->module());
    // A newer generation at the same index must not reconnect an old portal.
    Entity stale=h1.room;
    a->world().destroy(stale);
    Entity reused=a->world().create();
    AIVERSE_TEST_CHECK(reused.index==stale.index&&reused.gen!=stale.gen);
    a->world().add<ea::AcousticZone>(reused);
    a->step();
    AIVERSE_TEST_CHECK(!hearing<1>(*a).audible&&hearing<1>(*a).valid);

    auto tie=make<1>();
    auto& th=static_cast<EchoHost<1>&>(tie->module());
    Entity second=tie->world().create();
    tie->world().add<ea::AcousticEmitter>(second,ea::AcousticEmitter{th.exit,55,true});
    tie->step();
    AIVERSE_TEST_CHECK(hearing<1>(*tie).source==th.emitter);
    AIVERSE_TEST_CHECK(hearing<1>(*tie).intensity==55);
    tie->world().get<ea::AcousticReceiver>(th.receiver).threshold=56;
    tie->step();
    AIVERSE_TEST_CHECK(!hearing<1>(*tie).audible);

    // Fail closed if the graph would exceed a bounded planning budget.
    auto huge=make<1>();
    auto& hh=static_cast<EchoHost<1>&>(huge->module());
    for(std::size_t i=0;i<ea::kMaxZones;++i){
        auto e=huge->world().create();huge->world().add<ea::AcousticZone>(e);
    }
    AIVERSE_TEST_CHECK(!ea::propagate(huge->world(),123));
    AIVERSE_TEST_CHECK(!hearing<1>(*huge).valid &&
                      hearing<1>(*huge).sampledTick==123);
    (void)hh;
    std::cout<<"PASS ECHO native ECS, 2 hosts, thresholds, generation, stable ties, budgets, save and COW rollback\n";
}
