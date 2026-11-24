// AETHER ECS: conservation, diffusion, flux, PHYSIS fire, native save and rollback.
#include "aiverselib/world/AetherEcs.h"
#include "aiverselib/world/AetherPhysisBridge.h"
#include "tests/AlwaysCheck.h"
#include <array>
#include <cstdint>
#include <limits>
#include <iostream>
#include <memory>
#include <string>
using namespace aiverse;
namespace ae=aiverse::aether;

template<int N>struct GasHost final:GameModule{
    Entity source=kNullEntity,target=kNullEntity,duct=kNullEntity,flux=kNullEntity;
    std::string name()const override{return "gas-native";}
    void setup(Simulation& sim,const Json&,const Json&)override{
        ae::install(sim);
        auto& w=sim.world();
        source=w.create();
        w.add<ae::GasCell>(source,ae::GasCell{300,200,0});
        target=w.create();
        w.add<ae::GasCell>(target,ae::GasCell{0,0,200});
        duct=w.create();
        w.add<ae::GasDuct>(duct,ae::GasDuct{source,target,100,false,true});
        flux=w.create();
        w.add<ae::GasFlux>(flux,ae::GasFlux{source,10,0,0,true});
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
template<int N>std::unique_ptr<Simulation> make(bool cow=false){
    auto s=std::make_unique<Simulation>(SimConfig{81,20,0,true},
                                        std::make_unique<GasHost<N>>());
    s->setup(Json::object(),Json::object());
    if(cow)s->world().enableCowSnapshots();
    return s;
}
template<int N>ae::GasCell gas(const Simulation& sim,bool source){
    const auto& host=static_cast<const GasHost<N>&>(sim.module());
    const auto* cell=sim.world().tryGetConst<ae::GasCell>(
        source?host.source:host.target);
    AIVERSE_TEST_CHECK(cell!=nullptr);
    return *cell;
}
// Keep the previous scalar kernel as the oracle for exact field parity.
static void checkActiveSpeciesDiffusion(){
    namespace wp=aiverse::lib::worldprocess;
    uint64_t rng=0xa34239b1cfULL;
    auto next=[&](){rng=rng*6364136223846793005ULL+1;return rng>>32;};
    for(size_t trial=0;trial<128;++trial){
        const size_t n=1+next()%64;
        std::array<std::vector<wp::I>,3> fields;
        for(size_t s=0;s<fields.size();++s){
            fields[s].resize(n);
            for(auto& v:fields[s])
                v=(trial%3==s)?42:wp::I(next()%500);
        }
        std::vector<wp::Flow> flows;
        for(size_t i=0;i<n*5;++i)
            flows.push_back({next()%n,next()%n,wp::I(next()%120)});
        auto expected=fields;
        for(auto& field:expected)AIVERSE_TEST_CHECK(wp::diffuse(field,flows));
        auto sum=[](const auto& field){wp::I total=0;for(auto m:field)total+=m;return total;};
        std::array<wp::I,3> totals{};
        for(size_t s=0;s<fields.size();++s)totals[s]=sum(fields[s]);
        wp::DiffusionActivity stats{};
        AIVERSE_TEST_CHECK(wp::diffuseSpecies(fields,flows,&stats));
        AIVERSE_TEST_CHECK(fields==expected);
        AIVERSE_TEST_CHECK(stats.directedFlows==flows.size()&&stats.activeSpecies<=2);
        AIVERSE_TEST_CHECK(stats.activeFlows<=flows.size()&&stats.transfers<=stats.activeFlows*3);
        for(size_t s=0;s<fields.size();++s)AIVERSE_TEST_CHECK(sum(fields[s])==totals[s]);
    }
    std::array<std::vector<wp::I>,3> still{{{0,1,1},{50,50,50},{8,8,8}}};
    const auto oldStill=still;
    wp::DiffusionActivity quiet{};
    AIVERSE_TEST_CHECK(wp::diffuseSpecies(still,{{0,1,20},{1,0,20},{1,2,0},{2,2,20}},&quiet));
    AIVERSE_TEST_CHECK(still==oldStill&&quiet.activeFlows==0&&quiet.activeSpecies==0&&quiet.transfers==0);

    // One donor's available mass is consumed in stable edge order. Incoming
    // mass cannot be forwarded during the same pass.
    std::array<std::vector<wp::I>,3> competing{{{10,0,0,0,0},{0,0,0,0,0},{0,0,0,0,0}}};
    const std::vector<wp::Flow> ordered{{0,1,100},{0,2,100},{0,3,100},{1,4,100}};
    AIVERSE_TEST_CHECK(wp::diffuseSpecies(competing,ordered,&quiet));
    AIVERSE_TEST_CHECK((competing[0]==std::vector<wp::I>{0,5,5,0,0}));
    AIVERSE_TEST_CHECK(quiet.activeFlows==2&&quiet.transfers==2&&quiet.activeSpecies==1);

    // Reject a malformed late species without committing any earlier field.
    auto invalid=oldStill;
    invalid[2][0]=-1;const auto before=invalid;
    AIVERSE_TEST_CHECK(!wp::diffuseSpecies(invalid,{{0,1,2}},&quiet)&&invalid==before);
    invalid=oldStill;invalid[1].pop_back();const auto mismatch=invalid;
    AIVERSE_TEST_CHECK(!wp::diffuseSpecies(invalid,{},&quiet)&&invalid==mismatch);
    invalid=oldStill;
    AIVERSE_TEST_CHECK(!wp::diffuseSpecies(invalid,{{0,3,0}},&quiet)&&invalid==oldStill);
    AIVERSE_TEST_CHECK(!wp::diffuseSpecies(invalid,{{0,1,-1}},&quiet)&&invalid==oldStill);

    constexpr wp::I max=std::numeric_limits<wp::I>::max();
    std::array<std::vector<wp::I>,3> overflow{{
        {10,0,0,0},{0,0,0,0},{max,max,max,max-10}}};
    const auto unchanged=overflow;
    const auto previousStats=quiet;
    AIVERSE_TEST_CHECK(!wp::diffuseSpecies(overflow,{{0,3,100},{1,3,100},{2,3,100}},&quiet));
    AIVERSE_TEST_CHECK(overflow==unchanged);
    AIVERSE_TEST_CHECK(quiet.directedFlows==previousStats.directedFlows&&quiet.activeFlows==previousStats.activeFlows);
}
static void checkQuietNetworkWake(){
    World ecs;
    const auto a=ecs.create(),b=ecs.create(),duct=ecs.create(),flux=ecs.create();
    ecs.add<ae::GasCell>(a,ae::GasCell{10,0,100});
    ecs.add<ae::GasCell>(b,ae::GasCell{10,0,100});
    ecs.add<ae::GasDuct>(duct,ae::GasDuct{a,b,20,false,true});
    ecs.add<ae::GasFlux>(flux,ae::GasFlux{a,20,0,0,true});
    AIVERSE_TEST_CHECK(ae::advance(ecs,1));
    // Flux is applied after diffusion, wakes the duct on the following tick.
    AIVERSE_TEST_CHECK(ecs.get<ae::GasCell>(a).smoke==30);
    AIVERSE_TEST_CHECK(ecs.get<ae::GasCell>(b).smoke==10);
    AIVERSE_TEST_CHECK(ecs.get<ae::GasCell>(b).sampledTick==1);
    ecs.get<ae::GasFlux>(flux).enabled=false;
    AIVERSE_TEST_CHECK(ae::advance(ecs,2));
    AIVERSE_TEST_CHECK(ecs.get<ae::GasCell>(a).smoke==20);
    AIVERSE_TEST_CHECK(ecs.get<ae::GasCell>(b).smoke==20);
    ecs.get<ae::GasDuct>(duct).enabled=false;
    ecs.get<ae::GasCell>(a).smoke=60;
    AIVERSE_TEST_CHECK(ae::advance(ecs,3));
    AIVERSE_TEST_CHECK(ecs.get<ae::GasCell>(b).smoke==20);
    ecs.get<ae::GasDuct>(duct).enabled=true;
    AIVERSE_TEST_CHECK(ae::advance(ecs,4));
    AIVERSE_TEST_CHECK(ecs.get<ae::GasCell>(a).smoke==40);
    AIVERSE_TEST_CHECK(ecs.get<ae::GasCell>(b).smoke==40);
    // Disabled cells still advance the existing per-cell stamp.
    ecs.get<ae::GasCell>(b).enabled=false;
    ecs.get<ae::GasCell>(a).smoke=60;
    AIVERSE_TEST_CHECK(ae::advance(ecs,5));
    AIVERSE_TEST_CHECK(ecs.get<ae::GasCell>(b).smoke==40);
    AIVERSE_TEST_CHECK(ecs.get<ae::GasCell>(b).sampledTick==5);
}
int main(){
    checkActiveSpeciesDiffusion();
    checkQuietNetworkWake();
    auto a=make<1>(),b=make<2>(true);
    a->step();b->step();
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    auto src=gas<1>(*a,true),dst=gas<1>(*a,false);
    AIVERSE_TEST_CHECK(src.smoke==210&&dst.smoke==100);
    AIVERSE_TEST_CHECK(src.toxin==100&&dst.toxin==100);
    AIVERSE_TEST_CHECK(src.oxygen==100&&dst.oxygen==100);
    AIVERSE_TEST_CHECK(src.smoke+dst.smoke==310);
    AIVERSE_TEST_CHECK(src.toxin+dst.toxin==200);
    AIVERSE_TEST_CHECK(src.oxygen+dst.oxygen==200);
    const auto sameTick=a->checksum();
    AIVERSE_TEST_CHECK(ae::advance(a->world(),src.sampledTick));
    AIVERSE_TEST_CHECK(a->checksum()==sameTick);
    std::string saved=a->serialize();
    auto load=make<1>();
    std::string error;
    AIVERSE_TEST_CHECK(load->deserialize(saved,&error));
    AIVERSE_TEST_CHECK(load->checksum()==a->checksum());
    // Corrupt/truncated native saves must fail without altering live state.
    AIVERSE_TEST_CHECK(!load->deserialize(saved.substr(0,saved.size()-3),&error));
    AIVERSE_TEST_CHECK(load->checksum()==a->checksum());
    SimSnapshot base;b->saveState(base);
    auto& ah=static_cast<GasHost<1>&>(a->module());
    auto& bh=static_cast<GasHost<2>&>(b->module());
    a->world().get<ae::GasDuct>(ah.duct).enabled=false;
    b->world().get<ae::GasDuct>(bh.duct).enabled=false;
    a->step();b->step();
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    AIVERSE_TEST_CHECK(gas<1>(*a,false).smoke==100);
    AIVERSE_TEST_CHECK(gas<1>(*a,true).smoke==220);
    b->loadState(base);
    AIVERSE_TEST_CHECK(b->world().get<ae::GasDuct>(bh.duct).enabled);
    b->world().get<ae::GasDuct>(bh.duct).enabled=false;
    b->step();
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());

    // An invalid source must not partially apply fluxes or diffusion.
    auto invalid=make<3>();
    auto& ih=static_cast<GasHost<3>&>(invalid->module());
    invalid->world().get<ae::GasFlux>(ih.flux).smoke=ae::kMaxDuctRate+1;
    const auto before=gas<3>(*invalid,true);
    AIVERSE_TEST_CHECK(!ae::advance(invalid->world(),19));
    const auto after=gas<3>(*invalid,true);
    AIVERSE_TEST_CHECK(before.smoke==after.smoke&&before.sampledTick==after.sampledTick);
    invalid->world().get<ae::GasFlux>(ih.flux).smoke=10;
    invalid->world().get<ae::GasDuct>(ih.duct).maximumPerTick=-1;
    AIVERSE_TEST_CHECK(!ae::advance(invalid->world(),20));

    // Dead/recycled cell IDs never reconnect old ducts or sources.
    auto stale=make<4>();
    auto& sh=static_cast<GasHost<4>&>(stale->module());
    const Entity old=sh.target;
    stale->world().destroy(old);
    const Entity replacement=stale->world().create();
    AIVERSE_TEST_CHECK(replacement.index==old.index&&replacement.gen!=old.gen);
    stale->world().add<ae::GasCell>(replacement,ae::GasCell{0,0,200});
    stale->step();
    AIVERSE_TEST_CHECK(gas<4>(*stale,true).smoke==310);
    const auto* replacementCell=stale->world().tryGetConst<ae::GasCell>(replacement);
    AIVERSE_TEST_CHECK(replacementCell&&replacementCell->smoke==0);

    // Budget is checked before writing any cell and returns failure.
    auto budget=make<5>();
    for(std::size_t i=0;i<ae::kMaxCells;++i){
        Entity e=budget->world().create();
        budget->world().add<ae::GasCell>(e);
    }
    AIVERSE_TEST_CHECK(!ae::advance(budget->world(),33));

    // Read-only PHYSIS hazard field drives opt-in chemical heuristic.
    World world;
    const Entity cell=world.create();
    world.add<ae::GasCell>(cell,ae::GasCell{10,0,100});
    world.add<ae::FireCoupling>(cell,ae::FireCoupling{20,30});
    physis::HazardCell fire;fire.fire=Fixed::fromInt(1);
    world.add<physis::HazardCell>(cell,fire);
    physis::State climate;climate.tick=100;
    world.setResource<physis::State>(climate);
    AIVERSE_TEST_CHECK(ae::applyPhysisFire(world));
    AIVERSE_TEST_CHECK(world.get<ae::GasCell>(cell).oxygen==80);
    AIVERSE_TEST_CHECK(world.get<ae::GasCell>(cell).smoke==40);
    AIVERSE_TEST_CHECK(ae::applyPhysisFire(world)); // same climate tick: no duplicate
    AIVERSE_TEST_CHECK(world.get<ae::GasCell>(cell).oxygen==80);
    world.resource<physis::State>()->tick=101;
    world.get<physis::HazardCell>(cell).fire=Fixed::fromRatio(1,2);
    AIVERSE_TEST_CHECK(ae::applyPhysisFire(world));
    AIVERSE_TEST_CHECK(world.get<ae::GasCell>(cell).oxygen==70);
    AIVERSE_TEST_CHECK(world.get<ae::GasCell>(cell).smoke==55);

    std::cout<<"PASS AETHER ECS diffusion, mass, flux, PHYSIS, budgets, generation and COW persistence\n";
}
