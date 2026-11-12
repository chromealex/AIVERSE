// settlement-production + survival-ecology + colony planner bot tests (own CTest: library_genre_production_survival).
#include <aiverselib/genre/Colony.h>
#include <aiverselib/genre/SurvivalEcology.h>
#include <aiverselib/bots/ColonyPlannerData.h>
#include <iostream>
#include <stdexcept>
#include <string>
using namespace aiverse;using namespace aiverse::lib;using namespace aiverse::lib::genre;
#define CHECK(x) do{if(!(x))throw std::runtime_error(std::string(__FILE__)+":"+std::to_string(__LINE__)+": " #x);}while(false)
namespace {
template<class State> void roundtrip(const State& s){
    auto bytes=save(s);CHECK(!bytes.empty());State copy;CHECK(load(bytes,copy));CHECK(save(copy)==bytes);
    CHECK(!load(bytes+"x",copy));CHECK(!load(bytes.substr(0,bytes.size()-1),copy));
    auto wrongKind=bytes;wrongKind[4]=char(wrongKind[4]^1);CHECK(!load(wrongKind,copy));
}
template<class F> void expectError(F&& f,const std::string& part){
    try{f();}catch(const JsonError& e){if(std::string(e.what()).find(part)==std::string::npos)throw std::runtime_error("unexpected error text: "+std::string(e.what())+" (wanted "+part+")");return;}
    throw std::runtime_error("expected JsonError containing "+part);
}
bool has(const ColonyReport& r,ColonyEventKind k,int64_t colonist=-1){for(const auto& e:r.events)if(e.kind==k&&(colonist<0||e.colonist==colonist))return true;return false;}
// Small colony: food (1), rest (2); work types cook 1, haul 2, build 3, gather 4, fight fire 5.
ColonyDefs smallDefs(){
    ColonyDefs d;d.config.ticksPerHour=10;
    d.needs[1]={1,100,80000,40000,10000,100000,{{10000,-50000},{40000,-10000}}};
    d.needs[2]={2,50,90000,30000,5000,95000,{{5000,-30000}}};
    d.restNeed=2;d.restores={{ColonyActivity::Sleeping,2,400}};d.meals={{3,1,50000,5}};
    for(int64_t w=1;w<=5;++w)d.workTypes[w]={w,3};
    d.recipes[1]={1,1,1,10,{{1,2}},{{3,1}}};
    d.blueprints[1]={1,3,20,1,true,{{2,5}}};
    d.gathers[1]={1,4,1,5,8};
    d.hauls[1]={1,1,2,1,10,4,6,2};
    d.alerts[1]={1,5,1000,6,-5000,true,true};
    return d;
}
std::array<ColonyBlock,kColonyHours> allWork(){std::array<ColonyBlock,kColonyHours> s{};s.fill(ColonyBlock::Work);return s;}

void colonyNeedsSchedulesAndMeals(){
    auto d=smallDefs();CHECK(valid(d));ColonyState s;
    CHECK(colonyAddStockpile(s,1,0,{{3,2}}));CHECK(colonyAddColonist(d,s,1,allWork()));CHECK(!colonyAddColonist(d,s,1,allWork()));
    auto sched=colonySchedule(22,6,18,22);CHECK(sched[23]==ColonyBlock::Sleep&&sched[5]==ColonyBlock::Sleep&&sched[6]==ColonyBlock::Work&&sched[19]==ColonyBlock::Leisure);
    CHECK(colonySchedule(0,8,20,24)[21]==ColonyBlock::Leisure);
    // Food decays 100/tick from 80000: low at 40000 after 400 ticks -> eats a meal (one unit) and gets +50000.
    bool ate=false,low=false;
    for(int64_t t=0;t<=400;++t){auto r=stepColony(d,s,t,{});CHECK(r);ate|=has(*r,ColonyEventKind::Ate,1);low|=has(*r,ColonyEventKind::NeedLow,1);}
    CHECK(low&&ate&&colonyStock(s.stockpiles.at(1),3)==1&&s.colonists.at(1).needs.at(1)>80000);
    CHECK(!stepColony(d,s,400,{}));CHECK(!stepColony(d,s,399,{}));   // repeated / backwards ticks
    // Schedule: hour 0 (tick 410 = hour 1 of day 1? ticksPerHour 10 -> day = 240) sleep block makes the colonist sleep.
    ColonyCommand sleep{1,ColonyCommandKind::SetSchedule,1,std::size_t(0),0,int64_t(ColonyBlock::Sleep),0};
    for(int h=0;h<24;++h){sleep.seq=1+h;sleep.target=h;ColonyInput in;in.commands={sleep};CHECK(stepColony(d,s,401+h,in));}
    CHECK(s.colonists.at(1).activity==ColonyActivity::Sleeping);
    const auto rest=s.colonists.at(1).needs.at(2);CHECK(stepColony(d,s,430,{}));CHECK(s.colonists.at(1).needs.at(2)>rest);
    // Stale command sequence numbers are ignored with an event, the rest of the tick runs.
    ColonyInput stale;stale.commands={{3,ColonyCommandKind::SetPriority,1,0,1,1,0}};auto r=stepColony(d,s,431,stale);CHECK(r&&has(*r,ColonyEventKind::CommandIgnored));
    // Malformed owner input leaves the state untouched.
    auto before=save(s);ColonyInput bad;bad.moodModifiers[99]=5;CHECK(!stepColony(d,s,432,bad));CHECK(save(s)==before);
    roundtrip(s);
}
void colonyJobsAndReservations(){
    auto d=smallDefs();ColonyState s;CHECK(colonyAddStockpile(s,1,0,{{1,3},{2,5}}));CHECK(colonyAddStockpile(s,2,0));
    CHECK(colonyAddWorkstation(s,10,1,1,false));CHECK(colonyAddWorkstation(s,11,1,1,false));
    for(int64_t id:{1,2,3})CHECK(colonyAddColonist(d,s,id,allWork()));
    // Preferences: colonist 1 cooks (priority 1), 2 and 3 only cook at priority 4 (or not at all for 3).
    ColonyInput in;in.commands={{1,ColonyCommandKind::SetPriority,1,0,1,1,0},{2,ColonyCommandKind::SetPriority,2,0,1,4,0},{3,ColonyCommandKind::SetPriority,3,0,1,0,0},
        {4,ColonyCommandKind::SetBill,0,10,1,0,0},{5,ColonyCommandKind::SetBill,0,11,1,0,0},{6,ColonyCommandKind::SetBill,0,12,1,0,0},
        {7,ColonyCommandKind::SetPriority,1,0,2,0,0},{8,ColonyCommandKind::SetPriority,2,0,2,0,0},{9,ColonyCommandKind::SetPriority,3,0,2,0,0}}; // no hauling yet
    auto r=stepColony(d,s,0,in);CHECK(r);
    int ignored=0;for(const auto& e:r->events)if(e.kind==ColonyEventKind::CommandIgnored)++ignored;CHECK(ignored==1); // workstation 12 does not exist
    // Only 3 raw food: one recipe job reserves 2, the other cannot be claimed (reservations are shared per stockpile).
    CHECK(s.colonists.at(1).job&&s.jobs.at(s.colonists.at(1).job).site==10); // lowest job id wins the tie
    CHECK(colonyReserved(s.stockpiles.at(1),1)==2&&colonyAvailable(s.stockpiles.at(1),1)==1);
    CHECK(s.colonists.at(2).job==0);
    for(int64_t t=1;t<=10;++t)CHECK(stepColony(d,s,t,{}));
    CHECK(colonyStock(s.stockpiles.at(1),3)==1&&colonyStock(s.stockpiles.at(1),1)==1&&colonyReserved(s.stockpiles.at(1),1)==0);
    // Hauling moves raw food 1 -> 2 (keep 10, batch 4) once a hauler is free; gather clamps to the node's availability.
    ColonyInput g;g.commands={{10,ColonyCommandKind::OrderGather,0,77,1,0,1},{11,ColonyCommandKind::OrderBuild,0,0,1,0,1},{12,ColonyCommandKind::SetPriority,3,0,2,3,0}};g.gatherAvailable[77]=3;
    CHECK(stepColony(d,s,11,g));
    bool gathered=false,built=false,hauled=false;int64_t newStation=0;
    for(int64_t t=12;t<=60;++t){
        ColonyInput x;x.gatherAvailable[77]=3;auto rr=stepColony(d,s,t,x);CHECK(rr);
        for(const auto& e:rr->events){if(e.kind==ColonyEventKind::Gathered){gathered=true;CHECK(e.value==3);}if(e.kind==ColonyEventKind::Built){built=true;newStation=e.value;}if(e.kind==ColonyEventKind::Hauled)hauled=true;}
    }
    CHECK(gathered&&built&&hauled&&s.workstations.contains(newStation)&&s.workstations.at(newStation).needsPower);
    CHECK(colonyStock(s.stockpiles.at(1),2)==0); // the blueprint consumed 5 wood
    // Power: an unpowered workstation blocks its recipe job.
    ColonyInput bill;bill.commands={{13,ColonyCommandKind::SetBill,0,newStation,1,0,0},{14,ColonyCommandKind::ClearBill,0,10,0,0,0},{15,ColonyCommandKind::ClearBill,0,11,0,0,0}};CHECK(stepColony(d,s,61,bill));
    s.stockpiles.at(1).stock[1]=10;
    bool blocked=false;for(int64_t t=62;t<80;++t){ColonyInput x;x.unpowered={newStation};auto rr=stepColony(d,s,t,x);CHECK(rr);blocked|=has(*rr,ColonyEventKind::JobBlocked);}
    CHECK(blocked);roundtrip(s);
}
void colonyDisasterResponse(){
    auto d=smallDefs();ColonyState s;CHECK(colonyAddStockpile(s,1,0,{{1,100}}));CHECK(colonyAddWorkstation(s,10,1,1,false));
    for(int64_t id:{1,2})CHECK(colonyAddColonist(d,s,id,allWork()));
    ColonyInput in;in.commands={{1,ColonyCommandKind::SetBill,0,10,1,0,0},{2,ColonyCommandKind::SetPriority,1,0,1,1,0},{3,ColonyCommandKind::SetPriority,2,0,5,0,0}};
    CHECK(stepColony(d,s,0,in));CHECK(s.colonists.at(1).job);
    // A fire alert (severity 2) creates two response jobs; colonist 1 is pulled off cooking (preempt), colonist 2 refuses firefighting.
    ColonyInput fire;fire.raise={{1,42,2}};auto r=stepColony(d,s,1,fire);CHECK(r&&has(*r,ColonyEventKind::AlertRaised));
    CHECK(s.colonists.at(1).job&&s.jobs.at(s.colonists.at(1).job).kind==ColonyJobKind::Response);
    CHECK(!(s.colonists.at(2).job&&s.jobs.at(s.colonists.at(2).job).kind==ColonyJobKind::Response));
    CHECK(s.colonists.at(1).mood<0);
    bool resolved=false;int worked=0;
    for(int64_t t=2;t<40&&!resolved;++t){auto rr=stepColony(d,s,t,{});CHECK(rr);for(const auto& e:rr->events){if(e.kind==ColonyEventKind::AlertWorked){++worked;CHECK(e.value==42);}if(e.kind==ColonyEventKind::AlertResolved)resolved=true;}}
    CHECK(resolved&&worked==2&&s.alerts.empty());
    // Owner clearing cancels remaining response jobs.
    ColonyInput again;again.raise={{1,7,3}};CHECK(stepColony(d,s,100,again));CHECK(!s.alerts.empty());
    ColonyInput clear;clear.clear={s.alerts.begin()->first};auto rr=stepColony(d,s,101,clear);CHECK(rr&&has(*rr,ColonyEventKind::AlertResolved)&&s.alerts.empty());
    for(const auto& [id,j]:s.jobs){(void)id;CHECK(j.kind!=ColonyJobKind::Response);}
    roundtrip(s);
}
void colonyMentalBreak(){
    auto d=smallDefs();d.config.breakAt=-20000;d.config.lowAt=-5000;d.config.breakTicks=20;ColonyState s;CHECK(colonyAddStockpile(s,1,0));CHECK(colonyAddColonist(d,s,1,allWork()));
    ColonyInput in;in.moodModifiers[1]=-90000;auto r=stepColony(d,s,0,in);CHECK(r&&has(*r,ColonyEventKind::MentalBreak,1)&&s.colonists.at(1).activity==ColonyActivity::Broken);
    for(int64_t t=1;t<20;++t){ColonyInput x;x.moodModifiers[1]=-90000;auto rr=stepColony(d,s,t,x);CHECK(rr&&!has(*rr,ColonyEventKind::MentalBreak));}
    CHECK(s.colonists.at(1).calmUntil==40);
}
void power(){
    PowerGridState g;g.restoreDelayTicks=5;
    CHECK(powerAddNode(g,{1,1,PowerNodeKind::Producer,100}));
    PowerNode bat;bat.id=2;bat.kind=PowerNodeKind::Storage;bat.capacity=100;bat.rate=30;bat.charge=50;CHECK(powerAddNode(g,bat));
    auto consumer=[](int64_t id,int64_t demand,int64_t priority){PowerNode n;n.id=id;n.kind=PowerNodeKind::Consumer;n.demand=demand;n.priority=priority;return n;};
    CHECK(powerAddNode(g,consumer(10,60,5)));CHECK(powerAddNode(g,consumer(11,50,5)));CHECK(powerAddNode(g,consumer(12,20,1)));
    PowerNode water;water.id=20;water.medium=2;water.kind=PowerNodeKind::Producer;water.output=10;CHECK(powerAddNode(g,water));CHECK(powerAddNode(g,[&]{auto n=consumer(21,5,0);n.medium=2;return n;}()));
    for(auto [a,b]:std::vector<std::pair<int64_t,int64_t>>{{1,10},{1,11},{1,12},{2,10},{20,21}})CHECK(powerLink(g,a,b));
    CHECK(!powerLink(g,1,20));CHECK(!powerLink(g,10,1));CHECK(!powerLink(g,3,1));
    CHECK(powerComponents(g).size()==2);
    // Demand 130 vs 100 supply + 30 storage: everything fits, storage drains by 30.
    auto r=stepPower(g,0,{});CHECK(r&&r->unpowered.empty()&&g.nodes.at(2).charge==20&&r->components[0].discharged==30);
    CHECK(r->components[1].medium==2&&r->components[1].served==5);
    // Wind drops to 70%: 70 supply + 20 storage = 90 -> keep 10 (60, prio 5, lowest id first), skip 11 (50), keep 12 (skip-fit).
    PowerInput half;half.outputMilli[1]=700;r=stepPower(g,1,half);CHECK(r);
    CHECK(g.nodes.at(10).powered&&!g.nodes.at(11).powered&&g.nodes.at(12).powered);
    bool shed=false;for(const auto& e:r->events)if(e.kind==PowerEventKind::Shed&&e.node==11)shed=true;CHECK(shed);
    // Supply recovers but 11 stays off until its reconnect tick (hysteresis), then comes back; surplus charges storage.
    r=stepPower(g,2,{});CHECK(r&&!g.nodes.at(11).powered);
    for(int64_t t=3;t<6;++t)CHECK(stepPower(g,t,{}));CHECK(!g.nodes.at(11).powered);
    r=stepPower(g,6,{});CHECK(r&&g.nodes.at(11).powered);
    PowerInput off;off.enabled[10]=false;off.enabled[11]=false;off.enabled[12]=false;const auto charge=g.nodes.at(2).charge;r=stepPower(g,7,off);CHECK(r&&g.nodes.at(2).charge==std::min<int64_t>(100,charge+30));
    CHECK(!stepPower(g,7,{}));PowerInput badIn;badIn.outputMilli[99]=1;CHECK(!stepPower(g,8,badIn));
    roundtrip(g);
    auto json=readPowerGrid(Json::parse(R"({"restoreDelayTicks":3,"nodes":[{"id":1,"kind":"producer","output":5},{"id":2,"kind":"consumer","demand":3}],"links":[[1,2]]})"));
    CHECK(json.nodes.size()==2&&json.links.size()==1&&json.restoreDelayTicks==3);
    expectError([]{readPowerGrid(Json::parse(R"({"nodes":[{"id":1,"kind":"reactor"}]})"));},"power.nodes[0].kind: unknown value 'reactor'");
    expectError([]{readPowerGrid(Json::parse(R"({"nodes":[{"id":1,"kind":"producer"},{"id":2,"medium":2,"kind":"consumer"}],"links":[[1,2]]})"));},"power.links[0]");
}
void survival(){
    SurvivalConfig c;CHECK(valid(c));SurvivalState s;CHECK(survivalAddBody(c,s,1));CHECK(survivalAddBody(c,s,2));CHECK(survivalAddBody(c,s,3));
    SurvivalInput in;in.weather={-15000,8000,800,0};
    in.bodies[1]={};                                          // naked outdoors in a cold rain
    SurvivalBodyInput dressed;dressed.clothing={800,800,800};in.bodies[2]=dressed;
    SurvivalBodyInput inside;inside.sheltered=true;inside.shelterMilli=21000;inside.shelterInsulationMilli=1000;in.bodies[3]=inside;
    bool coldEvent=false;
    for(int64_t t=0;t<2000;++t){auto r=stepSurvival(c,s,t,in);CHECK(r);for(const auto& e:r->events)if(e.kind==SurvivalEventKind::StatusOn&&e.body==1&&e.value==kSurvivalCold)coldEvent=true;}
    const auto& naked=s.bodies.at(1);const auto& warm=s.bodies.at(2);const auto& home=s.bodies.at(3);
    CHECK(coldEvent&&naked.coreMicro<warm.coreMicro&&warm.coreMicro<=home.coreMicro&&home.coreMicro==37000000);
    CHECK(naked.wetness>warm.wetness&&home.wetness==0&&(naked.status&kSurvivalWet));
    CHECK(naked.hunger<home.hunger);                          // shivering burns food
    CHECK(survivalMood(naked)<survivalMood(home));
    CHECK(!stepSurvival(c,s,1999,in));
    // Deprivation: no food or water -> condition drops to 0 -> Died once, then the body is frozen.
    SurvivalState d;CHECK(survivalAddBody(c,d,7));d.bodies.at(7).hunger=0;d.bodies.at(7).thirst=0;
    bool died=false;int64_t t=0;for(;t<10000&&!died;++t){auto r=stepSurvival(c,d,t,{});CHECK(r);for(const auto& e:r->events)if(e.kind==SurvivalEventKind::Died)died=true;}
    CHECK(died&&d.bodies.at(7).dead);auto r=stepSurvival(c,d,t,{});CHECK(r&&r->events.empty());
    // Stamina: exertion drains, rest regenerates, hunger halves regeneration and caps the maximum.
    SurvivalState st;CHECK(survivalAddBody(c,st,1));SurvivalInput run;run.bodies[1].exertionMilli=1000;CHECK(stepSurvival(c,st,0,run));CHECK(st.bodies.at(1).stamina==kSurvivalMeterMax-600);
    st.bodies.at(1).hunger=c.lowAt-1;st.bodies.at(1).stamina=0;CHECK(stepSurvival(c,st,1,{}));CHECK(st.bodies.at(1).stamina==150);
    // Weather modifiers feed power and growth.
    auto e=survivalWeatherEffects(c.weather,{20000,12000,0,1000});CHECK(e.windPowerMilli==1000&&e.solarMilli==1000&&e.growthMilli==1000);
    auto cold=survivalWeatherEffects(c.weather,{-5000,20000,1000,200});CHECK(cold.growthMilli==0&&cold.activityMilli==300&&cold.solarMilli==100&&cold.wetPerTick==200);
    roundtrip(s);
}
void ecology(){
    EcologyDefs d;d.periodTicks=10;
    d.species[1]={1,300,10,0,0,300,{}};
    d.species[2]={2,50,20,500,200,0,{{1,600,10,200}}};
    d.regions[1]={1,{{1,100},{2,10}},{2}};d.regions[2]={2,{{1,100}},{1}};
    CHECK(valid(d));EcologyState s;CHECK(ecologySetPopulation(d,s,1,1,10));CHECK(!ecologySetPopulation(d,s,9,1,10));
    Random rng(5);
    // Logistic growth towards K without predators.
    for(int64_t t=0;t<400;++t)CHECK(stepEcology(d,s,t,{},rng));
    auto deer=ecologyCount(s,1,1);CHECK(deer>=80&&deer<=100);
    EcologyState crowded;CHECK(ecologySetPopulation(d,crowded,1,1,150));auto m=stepEcology(d,crowded,0,{},rng);  // above K: 30% of the excess moves
    CHECK(m&&m->period&&ecologyCount(crowded,2,1)==15&&!m->events.empty()&&m->events[0].kind==EcologyEventKind::Arrived);
    // Predators eat prey; without prey they starve to extinction.
    CHECK(ecologySetPopulation(d,s,1,2,8));const auto before=ecologyCount(s,1,1);
    for(int64_t t=400;t<420;++t)CHECK(stepEcology(d,s,t,{},rng));CHECK(ecologyCount(s,1,1)<before);
    EcologyState hungry;CHECK(ecologySetPopulation(d,hungry,1,2,8));bool extinct=false;
    for(int64_t t=0;t<2000&&!extinct;++t){auto r=stepEcology(d,hungry,t,{},rng);CHECK(r);for(const auto& e:r->events)if(e.kind==EcologyEventKind::Extinct&&e.species==2)extinct=true;}
    CHECK(extinct);
    // Harvest clamps to the population.
    EcologyInput h;h.harvests={{1,1,1000000}};auto r=stepEcology(d,s,1000,h,rng);CHECK(r&&ecologyCount(s,1,1)==0);
    CHECK(!stepEcology(d,s,1000,{},rng));
    // Determinism with birth noise: same seed, same populations.
    d.noiseMilli=200;EcologyState a,b;CHECK(ecologySetPopulation(d,a,1,1,30));b=a;Random ra(9),rb(9);
    for(int64_t t=0;t<300;++t){CHECK(stepEcology(d,a,t,{},ra));CHECK(stepEcology(d,b,t,{},rb));}
    CHECK(save(a)==save(b)&&ra.state()==rb.state());roundtrip(a);
    // Data and errors.
    auto json=readEcologyDefinitions(Json::parse(R"({"periodTicks":5,"species":[{"id":1,"growthMilli":100},{"id":2,"diet":[{"prey":1,"attackMilli":10}],"foodNeedMilli":5}],"regions":[{"id":1,"capacity":[[1,10]]}]})"));
    CHECK(json.species.size()==2&&json.regions.at(1).capacity.at(1)==10);
    expectError([]{readEcologyDefinitions(Json::parse(R"({"species":[{"id":1,"diet":[{"prey":7}]}]})"));},"ecology.species 1.diet");
    expectError([]{readEcologyDefinitions(Json::parse(R"({"species":[{"id":1,"speed":3}]})"));},"ecology.species[0]: unknown key 'speed'");
    expectError([]{readEcologyDefinitions(Json::parse(R"({"species":[{"id":1},{"id":1}]})"));},"ecology.species[1].id: duplicate id 1");
    expectError([]{readEcologyDefinitions(Json::parse(R"({"species":[{"id":1,"growthMilli":1.5}]})"));},"ecology.species[0].growthMilli: expected integer");
}
void animals(){
    EcologyAnimalDefs d;d.ticksPerHour=10;
    EcologyAnimalDef deer;deer.species=1;deer.fears={0,2};deer.sleepFrom=-1;deer.sleepTo=-1;d.species[1]=deer;
    EcologyAnimalDef wolf;wolf.species=2;wolf.prey={1};wolf.runSpeed=Fixed::fromRatio(1,2);wolf.huntFrom=Awareness::Suspicious;wolf.fears={};d.species[2]=wolf;
    CHECK(valid(d));EcologyAnimalState s;CHECK(ecologyAddAnimal(d,s,1,1,1,{0_fx,0_fx}));CHECK(ecologyAddAnimal(d,s,2,2,1,{5_fx,0_fx}));
    s.animals.at(2).hunger=100;
    auto perceive=[](int64_t observer,int64_t target,Vec2 at){PerceivedTarget p;p.observer=observer;p.target=target;p.level=Awareness::Alert;p.visibleNow=true;p.lastKnown={at.x,{},at.y};return p;};
    Random rng(3);EcologyAnimalInput in;in.perceived={perceive(1,2,{5_fx,0_fx}),perceive(2,1,{0_fx,0_fx})};
    auto r=stepEcologyAnimals(d,s,0,in,rng);CHECK(r);
    CHECK(s.animals.at(1).behavior==EcologyBehavior::Flee&&s.animals.at(1).position.x<0_fx);   // deer runs away from the wolf
    CHECK(s.animals.at(2).behavior==EcologyBehavior::Hunt&&s.animals.at(2).target==1);
    bool killed=false;
    for(int64_t t=1;t<200&&!killed;++t){
        EcologyAnimalInput x;if(s.animals.contains(1)){auto a=s.animals.at(1).position,b=s.animals.at(2).position;x.perceived={perceive(1,2,b),perceive(2,1,a)};}
        auto rr=stepEcologyAnimals(d,s,t,x,rng);CHECK(rr);for(const auto& e:rr->events)if(e.kind==EcologyAnimalEventKind::Killed&&e.target==1)killed=true;
    }
    CHECK(killed&&!s.animals.contains(1)&&s.animals.at(2).hunger>100);
    // Grazing consumes vegetation; humans (species 0) scare deer through owner facts.
    EcologyAnimalState g;CHECK(ecologyAddAnimal(d,g,5,1,3,{0_fx,0_fx}));g.animals.at(5).hunger=100;
    EcologyAnimalInput veg;veg.vegetation[3]=2;r=stepEcologyAnimals(d,g,0,veg,rng);CHECK(r&&r->grazed[3]==1&&g.animals.at(5).behavior==EcologyBehavior::Graze);
    EcologyAnimalInput human;human.perceived={perceive(5,77,{3_fx,0_fx})};human.facts[77]={0,{3_fx,0_fx}};human.vegetation[3]=5;
    CHECK(stepEcologyAnimals(d,g,1,human,rng));CHECK(g.animals.at(5).behavior==EcologyBehavior::Flee);
    // Wander goals depend only on the passed RNG.
    EcologyAnimalState a,b;CHECK(ecologyAddAnimal(d,a,9,1,0,{0_fx,0_fx}));b=a;Random ra(11),rb(11);
    for(int64_t t=0;t<500;++t){CHECK(stepEcologyAnimals(d,a,t,{},ra));CHECK(stepEcologyAnimals(d,b,t,{},rb));}
    CHECK(a.animals.at(9)==b.animals.at(9)&&a.animals.at(9).position!=Vec2{});
    CHECK(!stepEcologyAnimals(d,a,499,{},ra));roundtrip(a);
}
void resources(){
    EcologyResourceState s;CHECK(ecologyAddResource(s,{1,1,0,10,10,2,5,20}));CHECK(!ecologyAddResource(s,{1,1,0,10,10,2,5,20}));
    EcologyResourceInput h;h.harvests={{1,2,6},{1,1,6}};
    auto r=stepEcologyResources(s,1,h);CHECK(r&&r->taken[1]==10&&s.nodes.at(1).amount==0);
    CHECK(r->events[0].requester==1&&r->events[0].value==6);   // sorted by requester: 1 first, 2 gets the rest
    for(int64_t t=2;t<=20;++t)CHECK(stepEcologyResources(s,t,{}));CHECK(s.nodes.at(1).amount==0); // regrow delay
    bool regrowing=false;for(int64_t t=21;t<=60;++t){auto x=stepEcologyResources(s,t,{});CHECK(x);for(const auto& e:x->events)if(e.kind==EcologyResourceEventKind::Regrowing)regrowing=true;}
    CHECK(regrowing&&s.nodes.at(1).amount==10);
    EcologyResourceInput slow;slow.growthMilli=250;s.nodes.at(1).amount=0;s.nodes.at(1).depletedAt=-1;
    for(int64_t t=61;t<=80;++t)CHECK(stepEcologyResources(s,t,slow));CHECK(s.nodes.at(1).amount==2); // 4 periods * 2 * 0.25
    roundtrip(s);
    auto json=readEcologyResources(Json::parse(R"([{"id":3,"resource":2,"capacity":5}])"));CHECK(json.nodes.at(3).amount==5);
    expectError([]{readEcologyResources(Json::parse(R"([{"id":3,"resource":2,"capacity":5,"amount":6}])"));},"resources[0].amount: must be in 0..5");
}
void planner(){
    auto d=smallDefs();ColonyState s;CHECK(colonyAddStockpile(s,1,0,{{2,10}}));CHECK(colonyAddWorkstation(s,10,1,1,false));
    for(int64_t id:{1,2})CHECK(colonyAddColonist(d,s,id,allWork()));
    auto cfg=bots::readColonyPlanner(Json::parse(R"({"intervalTicks":10,"maxCommands":3,"rules":[
        {"id":1,"priority":10,"action":"bill","ref":1,"value":5},
        {"id":2,"priority":20,"action":"build","ref":1,"stockpile":1,"cooldownTicks":100,"when":[{"metric":"powerDeficit","below":false,"threshold":1}]},
        {"id":3,"priority":5,"action":"gather","ref":1,"stockpile":1,"maxOpen":1,"when":[{"metric":"stock","ref":1,"below":true,"threshold":10}]},
        {"id":4,"priority":1,"action":"priority","ref":5,"value":1}]})"),d);
    bots::ColonyPlannerState ps;bots::ColonyPlannerFacts f;f.powerDeficit=5;f.gatherNodes={{1,40,3},{1,30,7},{1,20,7}};
    auto p=bots::planColony(cfg,d,s,f,ps,0);CHECK(p&&p->commands.size()==3);
    CHECK(p->commands[0].kind==ColonyCommandKind::OrderBuild&&p->commands[1].kind==ColonyCommandKind::SetBill&&p->commands[2].kind==ColonyCommandKind::OrderGather);
    CHECK(p->commands[2].target==20);                   // most available, lowest node id
    CHECK(p->commands[0].seq==1&&p->commands[2].seq==3);
    // Commands go through the same validated colony path as player input.
    ColonyInput in;in.commands=p->commands;auto r=stepColony(d,s,0,in);CHECK(r&&!has(*r,ColonyEventKind::CommandIgnored)&&s.workstations.at(10).recipe==1);
    CHECK(bots::planColony(cfg,d,s,f,ps,5)->commands.empty());       // interval
    auto q=bots::planColony(cfg,d,s,f,ps,10);CHECK(q);               // build on cooldown, bill already set, gather open -> priorities
    for(const auto& c:q->commands)CHECK(c.kind==ColonyCommandKind::SetPriority&&c.ref==5);
    CHECK(!bots::planColony(cfg,d,s,f,ps,10));
    bots::ColonyPlannerState copy;CHECK(load(save(ps),copy)&&bots::colonyPlannerHash(copy)==bots::colonyPlannerHash(ps));roundtrip(ps);
    expectError([&]{bots::readColonyPlanner(Json::parse(R"({"rules":[{"id":1,"action":"build","ref":99,"stockpile":1}]})"),d);},"planner.rule 1: unknown blueprint");
    expectError([&]{bots::readColonyPlanner(Json::parse(R"({"rules":[{"id":1,"action":"fly","ref":1}]})"),d);},"planner.rules[0].action: unknown value 'fly'");
}
void data(const std::string& root){
    const auto dir=root+"/examples/libraries/genre/production-survival/";
    auto settings=gameplay::readGameplaySettings(Json::parseFile(dir+"settings.json"));CHECK(settings.value);
    CHECK(colonySettingsErrors(*settings.value).empty()&&survivalSettingsErrors(*settings.value).empty());
    CHECK(colonyConfig(*settings.value).ticksPerHour==50&&survivalConfig(*settings.value).hungerPerTick==0);
    auto defs=readColonyDefinitions(Json::parseFile(dir+"colony.json"),colonyConfig(*settings.value));CHECK(defs.recipes.size()==1&&defs.gathers.size()==3);
    auto grid=readPowerGrid(Json::parseFile(dir+"power.json"));CHECK(valid(grid)&&powerComponents(grid).size()==2);
    auto eco=Json::parseFile(dir+"ecology.json");CHECK(valid(readEcologyDefinitions(eco["ecology"]))&&valid(readEcologyAnimals(eco["animals"]))&&readEcologyResources(eco["resources"]).nodes.size()==5);
    CHECK(bots::readColonyPlanner(Json::parseFile(dir+"planner.json"),defs).rules.size()==9);
    CHECK(colonyDefsHash(defs)==colonyDefsHash(readColonyDefinitions(Json::parseFile(dir+"colony.json"),colonyConfig(*settings.value))));
    auto bad=gameplay::readGameplaySettings(Json::parse(R"({"schemaVersion":1,"colony":{"moodLowAt":50000}})"));CHECK(!bad.value);
    auto order=gameplay::readGameplaySettings(Json::parse(R"({"schemaVersion":1,"colony":{"moodBreakAt":-100,"moodLowAt":-100}})"));CHECK(order.value&&!colonySettingsErrors(*order.value).empty());
    expectError([]{readColonyDefinitions(Json::parse(R"({"needs":[{"id":1,"decayPerTick":1,"lowAt":5,"criticalAt":1,"colour":2}]})"));},"colony.needs[0]: unknown key 'colour'");
    expectError([]{readColonyDefinitions(Json::parse(R"({"workTypes":[{"id":1}],"recipes":[{"id":5,"workstationType":1,"workType":9,"work":1,"outputs":[[1,1]]}]})"));},"colony.recipe 5.workType: unknown work type 9");
    expectError([]{readColonyDefinitions(Json::parse(R"({"workTypes":[{"id":1}],"recipes":[{"id":5,"workstationType":1,"workType":1,"work":1,"outputs":[[1,1],[1,2]]}]})"));},"colony.recipes[0].outputs[1][0]: duplicate id 1");
    expectError([]{readColonyDefinitions(Json::parse(R"({"restores":[{"activity":"dancing","need":1,"perTick":1}]})"));},"colony.restores[0].activity: unknown value 'dancing'");
    expectError([]{readColonyDefinitions(Json::parse(R"({"workTypes":[{"id":1},{"id":1}]})"));},"colony.workTypes[1].id: duplicate id 1");
}
// A save/restore mid-run continues bit-identically (all record kinds of both packages).
void restoreContinuation(){
    auto d=smallDefs();ColonyState s;CHECK(colonyAddStockpile(s,1,0,{{1,40},{2,20},{3,3}}));CHECK(colonyAddStockpile(s,2,0));CHECK(colonyAddWorkstation(s,10,1,1,false));
    for(int64_t id=1;id<=4;++id)CHECK(colonyAddColonist(d,s,id,colonySchedule(int(id%3),int(id%3)+6,-1,-1)));
    ColonyInput setup;setup.commands={{1,ColonyCommandKind::SetBill,0,10,1,0,0}};CHECK(stepColony(d,s,0,setup));
    auto run=[&](ColonyState& c,int64_t from,int64_t to){for(int64_t t=from;t<to;++t){ColonyInput in;if(t==300)in.raise={{1,1,2}};in.gatherAvailable[5]=100;
        if(t%97==0)in.commands={{t+10,ColonyCommandKind::OrderGather,0,5,1,0,1}};CHECK(stepColony(d,c,t,in));}};
    auto a=s;run(a,1,600);
    auto b=s;run(b,1,250);ColonyState restored;CHECK(load(save(b),restored));run(restored,250,600);
    CHECK(save(a)==save(restored)&&colonyHash(a)==colonyHash(restored));
}
}
int main(int argc,char** argv){
    try{
        CHECK(argc==2);
        colonyNeedsSchedulesAndMeals();colonyJobsAndReservations();colonyDisasterResponse();colonyMentalBreak();power();
        survival();ecology();animals();resources();planner();data(argv[1]);restoreContinuation();
        std::cout<<"production/survival genre tests passed\n";return 0;
    }catch(const std::exception& e){std::cerr<<e.what()<<"\n";return 1;}
}
