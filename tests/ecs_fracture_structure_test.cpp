// FRACTURE ECS: generic structure support, cascade, durable events and persistence.
#include "aiverselib/world/FractureEcs.h"
#include "aiverselib/world/FracturePhysisBridge.h"
#include "tests/AlwaysCheck.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

using namespace aiverse;
namespace fr=aiverse::fracture;

template<int N> struct StructureHost final:GameModule {
    Entity root=kNullEntity,mid=kNullEntity,tip=kNullEntity;
    Entity bridgeA=kNullEntity,bridgeB=kNullEntity;
    std::string name()const override{return "shared-structure-host";}
    void setup(Simulation& sim,const Json&,const Json&)override {
        fr::install(sim);
        auto& w=sim.world();
        root=w.create();w.add<fr::Member>(root,fr::Member{true,false,false});
        mid=w.create();w.add<fr::Member>(mid);
        tip=w.create();w.add<fr::Member>(tip);
        bridgeA=w.create();w.add<fr::Bond>(bridgeA,fr::Bond{root,mid,100,0,true});
        bridgeB=w.create();w.add<fr::Bond>(bridgeB,fr::Bond{mid,tip,100,0,true});
        for(Entity e:{root,mid,tip})w.add<fr::Collapse>(e);
    }
    void applyCommand(Simulation&,const Command&)override{}
    // No parallel native-state copies or duplicate component hashing.
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
template<int N> std::unique_ptr<Simulation> make(bool cow=false) {
    auto sim=std::make_unique<Simulation>(SimConfig{42,20,0,true},
                                         std::make_unique<StructureHost<N>>());
    sim->setup(Json::object(),Json::object());
    if(cow)sim->world().enableCowSnapshots();
    return sim;
}
static void checkLocalSupportCache(){
    lib::SupportGraphCache cache;
    lib::SupportGraphActivity stats{};
    std::vector<bool> roots{true,false,false,true,false,false};
    std::vector<std::pair<size_t,size_t>> edges{{0,1},{1,2},{3,4},{4,5}};
    auto check=[&](){
        std::vector<lib::worldprocess::Edge> referenceEdges;
        for(auto [a,b]:edges)referenceEdges.push_back({a,b,1});
        const auto actual=cache.update(roots.size(),roots,edges,&stats);
        AIVERSE_TEST_CHECK(actual&&*actual==lib::worldprocess::supported(roots.size(),roots,referenceEdges));
    };
    check();AIVERSE_TEST_CHECK(stats.rebuiltVertices==6);
    check();AIVERSE_TEST_CHECK(stats.rebuiltVertices==0);
    edges.erase(edges.begin()+1);
    check();AIVERSE_TEST_CHECK(stats.rebuiltVertices==3);
    roots[2]=true;check();AIVERSE_TEST_CHECK(stats.rebuiltVertices==0);
    edges.push_back({2,5});
    check();AIVERSE_TEST_CHECK(stats.rebuiltVertices==4);
    // Reverse/duplicate bonds do not alter the effective topology.
    edges.push_back({5,2});check();AIVERSE_TEST_CHECK(stats.rebuiltVertices==0);
    // Restoring a previous topology is exact, even without a cache reset.
    edges={{0,1},{1,2},{3,4},{4,5}};roots[2]=false;check();
    AIVERSE_TEST_CHECK(stats.rebuiltVertices==6);
    uint64_t rng=0x8a5c32ULL;
    auto next=[&](){rng=rng*6364136223846793005ULL+1;return rng>>32;};
    roots.assign(64,false);roots[0]=true;edges.clear();cache.reset();check();
    for(size_t i=0;i<256;++i){
        size_t a=next()%roots.size(),b=next()%roots.size();
        if(i%3==0&&!edges.empty())edges.erase(edges.begin()+size_t(next()%edges.size()));
        else edges.emplace_back(a,b);
        if(i%5==0)roots[a]=!roots[a];
        check();
    }
}
int main(){
    checkLocalSupportCache();
    auto a=make<1>(),b=make<2>(true);
    a->step();b->step();
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    AIVERSE_TEST_CHECK(fr::pending(a->world()).empty());
    auto& ah=static_cast<StructureHost<1>&>(a->module());
    auto& bh=static_cast<StructureHost<2>&>(b->module());
    SimSnapshot base;a->saveState(base);
    a->world().get<fr::Bond>(ah.bridgeA).damage=100;
    b->world().get<fr::Bond>(bh.bridgeA).damage=100;
    a->step();b->step();
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    AIVERSE_TEST_CHECK(!a->world().get<fr::Member>(ah.root).detached);
    AIVERSE_TEST_CHECK(a->world().get<fr::Member>(ah.mid).detached);
    AIVERSE_TEST_CHECK(a->world().get<fr::Member>(ah.tip).detached);
    const auto events=fr::pending(a->world());
    AIVERSE_TEST_CHECK(events.size()==2);
    AIVERSE_TEST_CHECK(events[0].entity==ah.mid&&events[1].entity==ah.tip);
    AIVERSE_TEST_CHECK(!fr::acknowledge(a->world(),fr::Event{ah.mid,events[0].key+1,events[0].tick}));
    AIVERSE_TEST_CHECK(fr::acknowledge(a->world(),events[0]));
    AIVERSE_TEST_CHECK(!fr::acknowledge(a->world(),events[0]));
    AIVERSE_TEST_CHECK(fr::pending(a->world()).size()==1);
    a->step();
    AIVERSE_TEST_CHECK(fr::pending(a->world()).size()==1); // no duplicate on later tick
    const std::string saved=a->serialize();
    auto restored=make<1>();
    std::string why;
    AIVERSE_TEST_CHECK(restored->deserialize(saved,&why));
    AIVERSE_TEST_CHECK(restored->checksum()==a->checksum());
    AIVERSE_TEST_CHECK(fr::pending(restored->world()).size()==1);
    a->loadState(base);
    AIVERSE_TEST_CHECK(!a->world().get<fr::Member>(ah.mid).detached);
    AIVERSE_TEST_CHECK(fr::pending(a->world()).empty());
    a->world().get<fr::Bond>(ah.bridgeA).damage=100;
    a->step();
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());

    auto c=make<3>();
    auto& ch=static_cast<StructureHost<3>&>(c->module());
    c->step();
    // A structurally failed anchor no longer supports its descendants.
    c->world().get<fr::Member>(ch.root).broken=true;
    c->step();
    AIVERSE_TEST_CHECK(fr::pending(c->world()).size()==3);
    AIVERSE_TEST_CHECK(c->world().get<fr::Member>(ch.root).detached);

    auto stale=make<4>();
    auto& sh=static_cast<StructureHost<4>&>(stale->module());
    stale->step();
    const Entity old=sh.mid;
    stale->world().destroy(old);
    const Entity recycled=stale->world().create();
    AIVERSE_TEST_CHECK(recycled.index==old.index&&recycled.gen!=old.gen);
    stale->world().add<fr::Member>(recycled,fr::Member{true,false,false});
    stale->step();
    AIVERSE_TEST_CHECK(!stale->world().get<fr::Member>(sh.root).detached);
    AIVERSE_TEST_CHECK(stale->world().get<fr::Member>(sh.tip).detached);

    // Synthetic canonical PHYSIS stress snapshot, without recalculating wind
    // or installing a second climate solver. Host schedules bridge after PHYSIS.
    auto nature=make<6>();
    auto& nh=static_cast<StructureHost<6>&>(nature->module());
    physis::NatureStressOutputComponent stress{};
    stress.valid=true;
    stress.fractured=true;
    nature->world().add<physis::NatureStressOutputComponent>(nh.root,stress);
    AIVERSE_TEST_CHECK(fr::applyPhysisFractures(nature->world())==1);
    AIVERSE_TEST_CHECK(fr::applyPhysisFractures(nature->world())==0);
    nature->step();
    AIVERSE_TEST_CHECK(fr::pending(nature->world()).size()==3);

    auto repaired=make<7>();
    auto& rh=static_cast<StructureHost<7>&>(repaired->module());
    repaired->step();
    repaired->world().get<fr::Bond>(rh.bridgeA).damage=100;
    repaired->step();
    const auto toRepair=fr::pending(repaired->world());
    AIVERSE_TEST_CHECK(toRepair.size()==2);
    for(const auto& event:toRepair)
        AIVERSE_TEST_CHECK(fr::acknowledge(repaired->world(),event));
    AIVERSE_TEST_CHECK(!fr::rearmAfterRepair(repaired->world(),toRepair[0],
                                           toRepair[0].tick));
    repaired->step(); // host has durably repaired physical bodies by this barrier
    repaired->world().get<fr::Bond>(rh.bridgeA).damage=0;
    for(const auto& event:toRepair){
        AIVERSE_TEST_CHECK(fr::rearmAfterRepair(repaired->world(),event,
                                               repaired->tick()+1));
        AIVERSE_TEST_CHECK(!fr::acknowledge(repaired->world(),event));
    }
    repaired->step();
    AIVERSE_TEST_CHECK(fr::pending(repaired->world()).empty());
    AIVERSE_TEST_CHECK(!repaired->world().get<fr::Member>(rh.mid).detached);
    AIVERSE_TEST_CHECK(!repaired->world().get<fr::Member>(rh.tip).detached);

    auto large=make<5>();
    auto& lh=static_cast<StructureHost<5>&>(large->module());
    for(std::size_t i=0;i<fr::kMaxMembers;++i){
        Entity e=large->world().create();
        large->world().add<fr::Member>(e);
    }
    AIVERSE_TEST_CHECK(!fr::advance(large->world(),99));
    AIVERSE_TEST_CHECK(!large->world().get<fr::Member>(lh.mid).detached);
    std::cout<<"PASS FRACTURE ECS, native hosts, graph cascade, ack, overflow, COW, save and rollback\n";
}
