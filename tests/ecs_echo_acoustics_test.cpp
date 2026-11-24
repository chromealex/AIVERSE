// ECHO: native ECS propagation, two independent module types, rollback and save proof.
#include "aiverselib/world/EchoEcs.h"
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
// Compare the new shared graph solver against the unchanged single-source
// reference kernel. Fixed integer generator, not simulation RNG state.
static void checkMultiSourceParity(){
    namespace wp=aiverse::lib::worldprocess;
    uint64_t rng=0x1234abcd5678ef90ULL;
    auto next=[&](){rng=rng*6364136223846793005ULL+1;return rng>>32;};
    for(unsigned trial=0;trial<64;++trial){
        const size_t n=1+next()%32;
        lib::WeightedGraph graph(n);
        std::vector<wp::AcousticEdge> edges;
        for(size_t i=0;i<n*5;++i){
            const size_t a=next()%n,b=next()%n;
            const auto loss=int64_t(next()%11);
            graph[a].push_back({b,uint64_t(loss)});
            edges.push_back({a,b,loss});
        }
        std::vector<lib::WeightedSeed> seeds;
        std::vector<int64_t> best(n,-1);
        std::vector<uint64_t> owners(n,0);
        const size_t count=trial%9;
        for(size_t i=0;i<count;++i){
            const size_t zone=next()%n;
            const int64_t intensity=1+int64_t(next()%30);
            const uint64_t id=100-i;
            seeds.push_back({zone,uint64_t(ea::kMaxIntensity-intensity),id});
            const auto reference=wp::audibility(n,zone,intensity,edges);
            for(size_t v=0;v<n;++v){
                const auto value=reference[v];
                if(value>0&&(value>best[v]||(value==best[v]&&id<owners[v]))){
                    best[v]=value;owners[v]=id;
                }
            }
        }
        const auto actual=lib::multiSourceDijkstra(graph,seeds,ea::kMaxIntensity-1);
        AIVERSE_TEST_CHECK(actual);
        std::reverse(seeds.begin(),seeds.end());
        const auto reversed=lib::multiSourceDijkstra(graph,seeds,ea::kMaxIntensity-1);
        AIVERSE_TEST_CHECK(reversed&&actual->distance==reversed->distance&&actual->sourceId==reversed->sourceId);
        for(size_t v=0;v<n;++v){
            if(best[v]<0)AIVERSE_TEST_CHECK(actual->distance[v]==lib::kInfiniteCost);
            else{
                AIVERSE_TEST_CHECK(actual->distance[v]==uint64_t(ea::kMaxIntensity-best[v]));
                AIVERSE_TEST_CHECK(actual->sourceId[v]==owners[v]);
            }
        }
    }
    // Equal cost through zero-cost cycles, inclusive cutoff and overflow guard.
    lib::WeightedGraph graph(4);
    graph[0]={{1,0},{2,7},{3,8}};graph[1]={{0,0}};
    const std::vector<lib::WeightedSeed> seeds{{0,0,20},{1,0,10}};
    const auto result=lib::multiSourceDijkstra(graph,seeds,7);
    AIVERSE_TEST_CHECK(result&&result->sourceId[0]==10&&result->sourceId[1]==10);
    AIVERSE_TEST_CHECK(result->distance[2]==7&&result->sourceId[2]==10);
    AIVERSE_TEST_CHECK(result->distance[3]==lib::kInfiniteCost);
    graph[0].push_back({4,0});
    AIVERSE_TEST_CHECK(!lib::multiSourceDijkstra(graph,seeds,7));
    graph[0].pop_back();
    AIVERSE_TEST_CHECK(!lib::multiSourceDijkstra(graph,{{4,0,1}},7));
    AIVERSE_TEST_CHECK(!lib::multiSourceDijkstra(graph,{{0,8,1}},7));
    graph[0]={{1,lib::kInfiniteCost-1}};graph[1].clear();
    const auto huge=lib::multiSourceDijkstra(graph,{{0,2,1}});
    AIVERSE_TEST_CHECK(huge&&huge->distance[1]==lib::kInfiniteCost);
}
static void checkDirectedCutoff(){
    World ecs;
    const auto a=ecs.create(),b=ecs.create(),c=ecs.create();
    for(auto e:{a,b,c})ecs.add<ea::AcousticZone>(e);
    auto p=ecs.create();
    ecs.add<ea::AcousticPortal>(p,ea::AcousticPortal{a,b,10,false,true});
    auto q=ecs.create();
    ecs.add<ea::AcousticPortal>(q,ea::AcousticPortal{b,c,5,false,true});
    auto em=ecs.create();
    ecs.add<ea::AcousticEmitter>(em,ea::AcousticEmitter{a,15,true});
    std::vector<Entity> receivers;
    for(auto zone:{a,b,c}){
        auto e=ecs.create();receivers.push_back(e);
        ecs.add<ea::AcousticReceiver>(e,ea::AcousticReceiver{zone,0,true});
        ecs.add<ea::AcousticHearing>(e);
    }
    AIVERSE_TEST_CHECK(ea::propagate(ecs,1));
    AIVERSE_TEST_CHECK(ecs.get<ea::AcousticHearing>(receivers[0]).intensity==15);
    AIVERSE_TEST_CHECK(ecs.get<ea::AcousticHearing>(receivers[1]).intensity==5);
    AIVERSE_TEST_CHECK(ecs.get<ea::AcousticHearing>(receivers[2]).valid);
    AIVERSE_TEST_CHECK(!ecs.get<ea::AcousticHearing>(receivers[2]).audible);
    ecs.get<ea::AcousticEmitter>(em).zone=c;
    AIVERSE_TEST_CHECK(ea::propagate(ecs,2));
    AIVERSE_TEST_CHECK(!ecs.get<ea::AcousticHearing>(receivers[0]).audible);
    AIVERSE_TEST_CHECK(!ecs.get<ea::AcousticHearing>(receivers[1]).audible);
    AIVERSE_TEST_CHECK(ecs.get<ea::AcousticHearing>(receivers[2]).source==em);
}
int main(){
    checkMultiSourceParity();
    checkDirectedCutoff();
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
