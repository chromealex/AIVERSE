// Linked map-authoring -> authoritative ECS references across all 3 systems.
#include "aiverselib/world/WorldProcessesMapHydration.h"
#include "aiverselib/world/EvolveSimulationLod.h"
#include "aiverselib/world/EvolveRegionWorkBudget.h"
#include "aiverselib/world/WorldProcessBudgetedQueries.h"
#include "aiverselib/world/WorldProcessReportJobs.h"
#include <array>
#include "tests/AlwaysCheck.h"
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
using namespace aiverse;
namespace wa=aiverse::lib::worldprocess::authoring;
namespace wl=aiverse::lib::worldprocess::linked;

static Json linkedMap(){
    return Json::parse(R"({"entities":[
      {"id":"zone1","components":{"EchoZone":{"enabled":true}}},
      {"id":"zone2","components":{"EchoZone":{"enabled":true}}},
      {"id":"portal","components":{"EchoPortal":{
        "from":"zone1","to":"zone2","loss":25,"bidirectional":true,"enabled":true}}},
      {"id":"source","components":{"EchoEmitter":{
        "zone":"zone1","intensity":100,"enabled":true}}},
      {"id":"listener","components":{"EchoReceiver":{
        "zone":"zone2","threshold":30,"enabled":true}}},
      {"id":"base","components":{"FractureMember":{"anchored":true}}},
      {"id":"beam","components":{"FractureMember":{"anchored":false}}},
      {"id":"bond","components":{"FractureBond":{
        "first":"base","second":"beam","strength":100,"damage":0,"enabled":true}}},
      {"id":"air1","components":{"AetherGasCell":{
        "smoke":100,"toxin":0,"oxygen":0,"enabled":true}}},
      {"id":"air2","components":{"AetherGasCell":{
        "smoke":0,"toxin":0,"oxygen":0,"enabled":true}}},
      {"id":"duct","components":{"AetherDuct":{
        "from":"air1","to":"air2","maximumPerTick":10,
        "oneWay":false,"enabled":true}}},
      {"id":"flux","components":{"AetherFlux":{
        "cell":"air1","smoke":5,"toxin":0,"oxygen":0,"enabled":true}}}
    ]})");
}
template<int N>struct Host final:GameModule {
    std::vector<Entity> created;
    wa::MapHydration counts;
    std::string name()const override{return "world-processes-linked-host";}
    void setup(Simulation& sim,const Json&,const Json& map)override{
        for(const auto& entry:map["entities"].items()){
            (void)entry;created.push_back(sim.world().create());
        }
        counts=wa::hydrateMap(sim,map,created);
    }
    void applyCommand(Simulation&,const Command&)override{}
    void hashState(Simulation&,Hasher&)override{}
    int result(Simulation&)override{return 0;}
    bool serializeState(Simulation&,Archive& a)override{return a.ok();}
};
template<int N>std::unique_ptr<Simulation> make(const Json& map,bool cow=false){
    auto sim=std::make_unique<Simulation>(SimConfig{17,20,0,true},
                                         std::make_unique<Host<N>>());
    sim->setup(Json::object(),map);
    if(cow)sim->world().enableCowSnapshots();
    return sim;
}
static void checkRegionLod(){
    World ecs;
    const auto area=ecs.create(),missing=ecs.create(),disabled=ecs.create();
    ecs.add<evolve::Region>(area,evolve::Region{});
    ecs.add<evolve::Region>(missing,evolve::Region{});
    auto off=evolve::Region{};off.enabled=false;
    ecs.add<evolve::Region>(disabled,off);
    AIVERSE_TEST_CHECK(evolve::setArea(ecs,area,{-1000,-1000,1000,1000,true}));
    world::SimulationLodConfig cfg{true,0,2,4,16};
    const auto bounds=evolve::extents(ecs).front();
    std::array<world::Cell,1> observers{{{-1,100,-1}}};
    auto d=evolve::regionWorkDecision(cfg,bounds,observers,1000,1);
    AIVERSE_TEST_CHECK(d.tier==world::SimulationLodTier::Hot&&d.due);
    // maxX/maxZ are exclusive: cell 0 lies just outside.
    observers[0]={0,0,0};
    d=evolve::regionWorkDecision(cfg,bounds,observers,1000,1);
    AIVERSE_TEST_CHECK(d.tier==world::SimulationLodTier::Warm&&d.period==4);
    observers[0]={30,0,30};
    unsigned due=0;
    for(uint32_t tick=1;tick<=16;++tick){
        d=evolve::regionWorkDecision(cfg,bounds,observers,1000,tick);
        AIVERSE_TEST_CHECK(d.tier==world::SimulationLodTier::Cold&&d.period==16);
        if(d.due)++due;
    }
    AIVERSE_TEST_CHECK(due==1);
    std::array<world::Cell,2> multiple{{{30,0,30},{-1,100,-1}}};
    AIVERSE_TEST_CHECK(evolve::regionWorkDecision(cfg,bounds,multiple,1000,1).due);
    AIVERSE_TEST_CHECK(evolve::regionWorkDecision(cfg,bounds,observers,1000,1,true).due);
    AIVERSE_TEST_CHECK(evolve::regionWorkDecision(cfg,bounds,{},1000,1).due);
    AIVERSE_TEST_CHECK(evolve::regionWorkDecision(cfg,bounds,observers,0,1).due);
    const auto plan=evolve::planRegionWork(ecs,cfg,observers,1000,1);
    AIVERSE_TEST_CHECK(plan.regions.size()==2&&plan.diagnostics.hot==1&&plan.diagnostics.cold==1);
    AIVERSE_TEST_CHECK(plan.regions[0].region.packed()<plan.regions[1].region.packed());
    AIVERSE_TEST_CHECK(plan.diagnostics.executed+plan.diagnostics.deferred==2);
    AIVERSE_TEST_CHECK(ecs.get<evolve::Region>(area).sampledTick==UINT32_MAX);
}
static void checkRegionBudget(){
    World ecs;
    std::vector<Entity> entities;
    for(int i=0;i<5;++i)entities.push_back(ecs.create());
    std::sort(entities.begin(),entities.end(),[](Entity a,Entity b){
        return a.packed()<b.packed();
    });
    evolve::RegionWorkPlan plan;
    plan.diagnostics.tick=1;
    for(auto e:entities)plan.regions.push_back({e,{}});
    evolve::RegionBudgetState state;
    auto first=evolve::prepareRegionBudget(plan,state,2);
    AIVERSE_TEST_CHECK(first&&first->selected.size()==2&&first->remaining==3);
    AIVERSE_TEST_CHECK(first->selected[0].region==entities[0]);
    AIVERSE_TEST_CHECK(first->selected[1].region==entities[1]);
    AIVERSE_TEST_CHECK(!state.lastTick&&state.pending.empty());
    state=first->next;
    AIVERSE_TEST_CHECK(!evolve::prepareRegionBudget(plan,state,2));
    plan.diagnostics.tick=2;
    for(auto& item:plan.regions)item.lod.due=false;
    const auto snapshot=state;
    auto second=evolve::prepareRegionBudget(plan,state,2);
    AIVERSE_TEST_CHECK(second&&second->selected.size()==2&&second->remaining==1);
    AIVERSE_TEST_CHECK(second->selected[0].region==entities[2]);
    AIVERSE_TEST_CHECK(second->selected[1].region==entities[3]);
    AIVERSE_TEST_CHECK(second->selected[0].firstDueTick==1);
    auto replay=evolve::prepareRegionBudget(plan,snapshot,2);
    AIVERSE_TEST_CHECK(replay&&replay->selected[0].region==second->selected[0].region);
    AIVERSE_TEST_CHECK(replay->next.lastServedId==second->next.lastServedId);
    state=second->next;
    plan.diagnostics.tick=3;
    for(auto& item:plan.regions)item.lod.due=true;
    auto paused=evolve::prepareRegionBudget(plan,state,0);
    AIVERSE_TEST_CHECK(paused&&paused->selected.empty()&&paused->remaining==5&&paused->queued==4);
    state=paused->next;
    plan.diagnostics.tick=4;
    // Region removed while pending; stable identity cursor must not shift.
    plan.regions.erase(plan.regions.begin()+1);
    auto wrapped=evolve::prepareRegionBudget(plan,state,2);
    AIVERSE_TEST_CHECK(wrapped&&wrapped->removed==1&&wrapped->remaining==2);
    AIVERSE_TEST_CHECK(wrapped->selected[0].region==entities[4]);
    AIVERSE_TEST_CHECK(wrapped->selected[0].firstDueTick==1);
    AIVERSE_TEST_CHECK(wrapped->selected[1].region==entities[0]);
    plan.regions.push_back(plan.regions.back());
    AIVERSE_TEST_CHECK(!evolve::prepareRegionBudget(plan,state,2));
}
static void checkRegionPriority(){
    World ecs;
    const auto urgent=ecs.create(),normal=ecs.create(),later=ecs.create(),absent=ecs.create();
    evolve::RegionWorkPlan plan;
    plan.regions={{urgent,{world::SimulationLodTier::Cold,16,false}},
                  {normal,{}},{later,{}}};
    std::sort(plan.regions.begin(),plan.regions.end(),[](const auto& a,const auto& b){
        return a.region.packed()<b.region.packed();
    });
    evolve::RegionBudgetState state;
    const std::array<Entity,3> wake{urgent,urgent,absent};
    plan.diagnostics.tick=1;
    auto batch=evolve::prepareRegionBudget(plan,state,evolve::RegionBudgetOptions{0,wake});
    AIVERSE_TEST_CHECK(batch&&batch->woken==1&&batch->ignoredWake==1);
    AIVERSE_TEST_CHECK(batch->remaining==3&&batch->urgentRemaining==1);
    state=batch->next;
    plan.diagnostics.tick=2;
    batch=evolve::prepareRegionBudget(plan,state,1);
    AIVERSE_TEST_CHECK(batch&&batch->urgentSelected==1&&batch->selected[0].region==urgent);
    AIVERSE_TEST_CHECK(batch->selected[0].firstDueTick==1&&batch->selected[0].urgent);
    state=batch->next;
    // Repeated forced wake cannot consume the next reserved ordinary slot.
    const std::array<Entity,1> repeated{urgent};
    plan.diagnostics.tick=3;
    batch=evolve::prepareRegionBudget(plan,state,evolve::RegionBudgetOptions{1,repeated});
    AIVERSE_TEST_CHECK(batch&&batch->urgentSelected==0&&batch->urgentRemaining==1);
    AIVERSE_TEST_CHECK(batch->selected[0].region!=urgent);
    const auto ordinary=batch->selected[0].region;
    state=batch->next;
    plan.diagnostics.tick=4;
    batch=evolve::prepareRegionBudget(plan,state,1);
    AIVERSE_TEST_CHECK(batch&&batch->urgentSelected==1&&batch->selected[0].firstDueTick==3);
    state=batch->next;
    plan.diagnostics.tick=5;
    batch=evolve::prepareRegionBudget(plan,state,evolve::RegionBudgetOptions{1,repeated});
    AIVERSE_TEST_CHECK(batch&&batch->urgentSelected==0);
    AIVERSE_TEST_CHECK(batch->selected[0].region!=ordinary&&batch->selected[0].region!=urgent);
    state=batch->next;
    plan.diagnostics.tick=6;
    plan.regions.erase(std::remove_if(plan.regions.begin(),plan.regions.end(),
        [&](const auto& x){return x.region==urgent;}),plan.regions.end());
    batch=evolve::prepareRegionBudget(plan,state,evolve::RegionBudgetOptions{1,repeated});
    AIVERSE_TEST_CHECK(batch&&batch->removed==1&&batch->ignoredWake==1&&batch->urgentRemaining==0);
    const std::array<Entity,1> invalid{kNullEntity};
    AIVERSE_TEST_CHECK(!evolve::prepareRegionBudget(plan,state,evolve::RegionBudgetOptions{1,invalid}));
}
static void checkSharedReports(){
    namespace wp=aiverse::lib::worldprocess;
    World ecs;
    auto near=ecs.create(),far=ecs.create(),person=ecs.create();
    crowd::Agent a{};a.x=-1;a.z=-1;
    ecs.add<crowd::Agent>(near,a);
    a.x=30000;a.z=30000;ecs.add<crowd::Agent>(far,a);
    society::Person p{};p.knowledge=17;
    ecs.add<society::Person>(person,p);
    world::SimulationLodConfig cfg{true,0,2,4,16};
    const std::array<world::Cell,1> observers{{{-1,100,-1}}};
    const std::array<Entity,1> wake{far};
    wp::EntityBudgetState crowdState,societyState;
    const auto first=wp::prepareCrowdReports(ecs,cfg,observers,1000,1,
        crowdState,{1,wake});
    AIVERSE_TEST_CHECK(first&&first->samples.size()==1);
    AIVERSE_TEST_CHECK(first->samples[0].entity==far&&first->scheduling.urgentSelected==1);
    AIVERSE_TEST_CHECK(first->lod.hot==1&&first->lod.cold==1);
    crowdState=first->scheduling.next;
    const auto second=wp::prepareCrowdReports(ecs,cfg,observers,1000,2,
        crowdState,{1,{}});
    AIVERSE_TEST_CHECK(second&&second->samples[0].entity==near);
    AIVERSE_TEST_CHECK(second->samples[0].requestedTick==1&&second->samples[0].sampleTick==2);
    const auto unknown=[](Entity)->std::optional<world::Cell>{return std::nullopt;};
    const auto social=wp::prepareSocietyReports(ecs,cfg,observers,1,
        societyState,{1,{}},unknown);
    AIVERSE_TEST_CHECK(social&&social->lod.hot==1&&social->samples.size()==1);
    AIVERSE_TEST_CHECK(social->samples[0].entity==person&&social->samples[0].value.knowledge==17);
    AIVERSE_TEST_CHECK(ecs.get<crowd::Agent>(near).sampledTick==UINT32_MAX);
    AIVERSE_TEST_CHECK(ecs.get<society::Person>(person).sampledTick==UINT32_MAX);
    societyState=social->scheduling.next;
    const auto remote=[](Entity)->std::optional<world::Cell>{return world::Cell{300,0,300};};
    const std::array<Entity,1> socialWake{person};
    const auto forced=wp::prepareSocietyReports(ecs,cfg,observers,2,
        societyState,{1,socialWake},remote);
    AIVERSE_TEST_CHECK(forced&&forced->lod.cold==1&&forced->scheduling.urgentSelected==1);
}
static void checkReportJobs(){
    namespace wp=aiverse::lib::worldprocess;
    using namespace aiverse::ecs_experiment;
    World ecs;
    wp::EntityReportBatch<wp::queries::CrowdSample> crowdBatch;
    wp::EntityReportBatch<wp::queries::SocietySample> societyBatch;
    evolve::RegionReportBatch evolveBatch;
    for(uint32_t i=0;i<127;++i){
        const auto e=ecs.create();
        const auto slot=i%2?e:kNullEntity;
        crowdBatch.samples.push_back({e,10,20,{e,slot,int64_t(i),-int64_t(i)}});
        societyBatch.samples.push_back({e,11,20,{uint64_t(1)<<(i%64),1000,250000}});
        evolve::Region region{};
        region.fertility=i;region.pollution=2*i;
        evolveBatch.samples.push_back({e,12,20,region});
    }
    const auto serialC=wp::analyzeCrowdReports(crowdBatch);
    const auto serialS=wp::analyzeSocietyReports(societyBatch);
    const auto serialE=wp::analyzeEvolveReports(evolveBatch);
    AIVERSE_TEST_CHECK(!serialC.parallel&&!serialS.parallel&&!serialE.parallel);
    AIVERSE_TEST_CHECK(serialC.values.size()==127&&serialS.values[63].knownTopicCount==1);
    AIVERSE_TEST_CHECK(serialE.values[126].pollution==252);
    AIVERSE_TEST_CHECK(serialE.values[0].requestedTick==12&&serialE.values[0].sampleTick==20);
#if !defined(__EMSCRIPTEN__)
    for(unsigned n:{0u,1u,3u}){
        PersistentWorkers pool(n);
        const wp::ReportJobOptions forced{&pool,ExecutionMode::Parallel,1,1};
        const auto c=wp::analyzeCrowdReports(crowdBatch,forced);
        const auto s=wp::analyzeSocietyReports(societyBatch,forced);
        const auto e=wp::analyzeEvolveReports(evolveBatch,forced);
        AIVERSE_TEST_CHECK(c.values==serialC.values&&s.values==serialS.values&&e.values==serialE.values);
        AIVERSE_TEST_CHECK(c.parallel==(n>0)&&s.parallel==(n>0)&&e.parallel==(n>0));
        const wp::ReportJobOptions conservative{&pool,ExecutionMode::Auto,2048,256};
        AIVERSE_TEST_CHECK(!wp::analyzeCrowdReports(crowdBatch,conservative).parallel);
        AIVERSE_TEST_CHECK(!wp::analyzeCrowdReports({},forced).parallel);
        const std::array<int,4> values{1,2,3,4};
        bool failed=false;
        try{
            (void)wp::analyzeReportValues(std::span<const int>(values),forced,
                [](int v){if(v==3)throw std::runtime_error("test");return v*2;});
        }catch(const std::runtime_error&){failed=true;}
        AIVERSE_TEST_CHECK(failed);
        AIVERSE_TEST_CHECK(wp::analyzeCrowdReports(crowdBatch,forced).values==serialC.values);
    }
#endif
    AIVERSE_TEST_CHECK(!crowdBatch.scheduling.next.lastTick);
    AIVERSE_TEST_CHECK(!societyBatch.scheduling.next.lastTick);
    AIVERSE_TEST_CHECK(!evolveBatch.scheduling.next.lastTick);
}
int main(){
    checkRegionLod();
    checkRegionBudget();
    checkRegionPriority();
    checkSharedReports();
    checkReportJobs();
    auto portal=wl::defaults("EchoPortal");
    AIVERSE_TEST_CHECK(wl::validate("EchoPortal",portal));
    AIVERSE_TEST_CHECK(!wl::edit("EchoPortal","enabled",Json::fromBool(true),portal));
    AIVERSE_TEST_CHECK(wl::edit("EchoPortal","from",Json::string("zone1"),portal));
    AIVERSE_TEST_CHECK(wl::edit("EchoPortal","to",Json::string("zone2"),portal));
    AIVERSE_TEST_CHECK(wl::edit("EchoPortal","enabled",Json::fromBool(true),portal));
    AIVERSE_TEST_CHECK(!wl::edit("EchoPortal","loss",Json::number(int64_t(-1)),portal));
    auto bond=wl::defaults("FractureBond");
    AIVERSE_TEST_CHECK(!wl::edit("FractureBond","damage",Json::number(int64_t(2)),bond));
    auto flux=wl::defaults("AetherFlux");
    AIVERSE_TEST_CHECK(wl::edit("AetherFlux","smoke",Json::number(int64_t(-12)),flux));

    const Json map=linkedMap();
    auto a=make<1>(map),b=make<2>(map,true);
    auto& h=static_cast<Host<1>&>(a->module());
    auto& k=static_cast<Host<2>&>(b->module());
    AIVERSE_TEST_CHECK(h.counts.zones==2&&h.counts.portals==1&&
                      h.counts.emitters==1&&h.counts.receivers==1);
    AIVERSE_TEST_CHECK(h.counts.members==2&&h.counts.bonds==1);
    AIVERSE_TEST_CHECK(h.counts.gasCells==2&&h.counts.ducts==1&&h.counts.fluxes==1);
    AIVERSE_TEST_CHECK(a->world().get<echo::AcousticPortal>(h.created[2]).from==h.created[0]);
    AIVERSE_TEST_CHECK(a->world().get<echo::AcousticPortal>(h.created[2]).to==h.created[1]);
    AIVERSE_TEST_CHECK(a->world().has<echo::AcousticHearing>(h.created[4]));
    AIVERSE_TEST_CHECK(a->world().get<fracture::Bond>(h.created[7]).second==h.created[6]);
    AIVERSE_TEST_CHECK(a->world().get<aether::GasDuct>(h.created[10]).to==h.created[9]);

    a->step();b->step();
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    AIVERSE_TEST_CHECK(a->world().get<echo::AcousticHearing>(h.created[4]).audible);
    AIVERSE_TEST_CHECK(a->world().get<echo::AcousticHearing>(h.created[4]).intensity==75);
    AIVERSE_TEST_CHECK(!a->world().get<fracture::Member>(h.created[6]).detached);
    AIVERSE_TEST_CHECK(a->world().get<aether::GasCell>(h.created[8]).smoke==95);
    AIVERSE_TEST_CHECK(a->world().get<aether::GasCell>(h.created[9]).smoke==10);
    const auto saved=a->serialize();
    auto restored=make<1>(map);
    std::string error;
    AIVERSE_TEST_CHECK(restored->deserialize(saved,&error));
    AIVERSE_TEST_CHECK(restored->checksum()==a->checksum());
    SimSnapshot snap;
    b->saveState(snap);
    b->world().get<fracture::Bond>(k.created[7]).damage=100;
    a->world().get<fracture::Bond>(h.created[7]).damage=100;
    b->step();a->step();
    AIVERSE_TEST_CHECK(a->checksum()==b->checksum());
    AIVERSE_TEST_CHECK(a->world().get<fracture::Member>(h.created[6]).detached);
    b->loadState(snap);
    AIVERSE_TEST_CHECK(!b->world().get<fracture::Member>(k.created[6]).detached);

    auto bad=map;
    bad.member("entities").at(2).member("components")
       .member("EchoPortal").set("to",Json::string("never-existed"));
    bool failed=false;
    try {(void)make<3>(bad);}catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    bad=map;
    bad.member("entities").at(1).set("id",Json::string("zone1"));
    failed=false;
    try {(void)make<3>(bad);}catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    bad=map;
    bad.member("entities").at(1).member("components").erase("EchoZone");
    failed=false;
    try {(void)make<3>(bad);}catch(const std::invalid_argument&){failed=true;}
    AIVERSE_TEST_CHECK(failed);
    std::cout<<"PASS World Processes stable-id links, ECHO+FRACTURE+AETHER, save, replay and invalid refs\n";
}
