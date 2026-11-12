// Executable deterministic example for settlement-production + survival-ecology: 10 colonists survive 20 in-game
// days (24000 ticks at 50 ticks/hour) with needs, schedules, job preferences, cooking bills, hauling, gathering and
// hunting over renewable resources, a power grid (wind/solar/battery + built wood generators with brownout
// shedding) and a water fluid graph, a fire disaster on day 8, weather driving the body model and a deer/wolf
// ecosystem (population model + individual animals reacting to perceived colonists), all driven by the colony
// planner bot through ordinary ColonyCommands. The owner below is the authoritative host: it routes events
// between packages in a fixed order every tick. Run: production_survival <data dir> [--quiet]
// Exit code 0 only when the survival criteria hold and two runs plus a save/restore run give identical hashes.
#include <aiverselib/genre/Colony.h>
#include <aiverselib/genre/SurvivalEcology.h>
#include <aiverselib/bots/ColonyPlannerData.h>
#include <cstdio>
#include <string>
namespace {
using namespace aiverse;using namespace aiverse::lib;using namespace aiverse::lib::genre;
constexpr int64_t kTicksPerHour=50,kDay=kTicksPerHour*24,kDays=20,kTicks=kDay*kDays,kFireTick=8*kDay+10*kTicksPerHour;
constexpr int64_t kRawFood=1,kWood=2,kMeal=3,kField=1,kStore=2,kKitchen=50,kHeater=10,kPump=11,kWind=1,kSolar=2,kWater=20,kTap=22;
constexpr int64_t kHuntNode1=9001,kHuntNode2=9002,kDeer=1,kWolf=2;
struct Data {ColonyDefs colony;PowerGridState grid;EcologyDefs ecology;EcologyAnimalDefs animals;EcologyResourceState resources;bots::ColonyPlannerConfig planner;SurvivalConfig survival;};
// Everything outcome-affecting: package states (saved through genre::save/load) + owner bookkeeping + RNG.
struct World {
    ColonyState colony;PowerGridState grid;SurvivalState bodies;EcologyState ecology;EcologyAnimalState animals;EcologyResourceState resources;bots::ColonyPlannerState planner;
    uint64_t rngState=0,rngInc=0;int64_t nextAnimal=3000,fireResolvedAt=-1,dayRain=0,dayWind=0,generators=0,kitchens=1;
    int64_t meals=0,deaths=0,brownouts=0,huntKills=0,wolfKills=0,starved=0,flights=0,commands=0,fireWorked=0,minCoreMilli=37000,drinks=0;
    std::map<int64_t,int64_t> generatorNodes;  // structure id -> fuel
};
struct Owner {
    const Data& d;World w;Random rng;
    explicit Owner(const Data& data):d(data){}
    static Vec2 nodePos(int64_t node){
        switch(node){case 1:return {20_fx,10_fx};case 2:return {-15_fx,25_fx};case 3:return {60_fx,60_fx};case 4:return {-30_fx,-20_fx};case 5:return {70_fx,-50_fx};
            case kHuntNode1:return {40_fx,30_fx};case kHuntNode2:return {80_fx,80_fx};default:return {5_fx,5_fx};}
    }
    void setup(){
        w.colony={};w.grid=d.grid;w.resources=d.resources;w.rngState=0;rng.reseed(20261112);
        colonyAddStockpile(w.colony,kField,400,{{kRawFood,20},{kWood,30}});
        colonyAddStockpile(w.colony,kStore,600,{{kRawFood,30},{kWood,40},{kMeal,10}});
        colonyAddWorkstation(w.colony,kKitchen,1,kStore,true);
        for(int64_t i=1;i<=10;++i){
            // Two sleep shifts keep someone awake; schedules are player-equivalent data.
            colonyAddColonist(d.colony,w.colony,i,i%2?colonySchedule(22,6,19,22):colonySchedule(0,8,20,24));
            auto& c=w.colony.colonists.at(i);
            c.priorities[1+(i%4)]=1;                  // each colonist has a favourite work type among 1..4
            c.skills[1+(i%4)]=1300;c.skills[1+((i+1)%4)]=800;
            survivalAddBody(d.survival,w.bodies,i);
        }
        for(auto [r,n]:std::map<int64_t,int64_t>{{1,60},{2,80}})ecologySetPopulation(d.ecology,w.ecology,r,kDeer,n);
        for(auto [r,n]:std::map<int64_t,int64_t>{{1,5},{2,7}})ecologySetPopulation(d.ecology,w.ecology,r,kWolf,n);
        for(int64_t i=0;i<6;++i)ecologyAddAnimal(d.animals,w.animals,1001+i,kDeer,1,Vec2{Fixed::fromInt(30+5*i),Fixed::fromInt(40-4*i)});
        for(int64_t i=0;i<2;++i)ecologyAddAnimal(d.animals,w.animals,2001+i,kWolf,1,Vec2{Fixed::fromInt(70+10*i),60_fx});
    }
    SurvivalWeather weather(int64_t tick){
        const auto day=tick/kDay,hour=(tick/kTicksPerHour)%24;
        if(tick%kDay==0){w.dayRain=rng.below(1000)<350?600:0;w.dayWind=2000+int64_t(rng.below(10001));}
        static constexpr int64_t kHourly[24]={-5000,-5500,-6000,-6000,-5500,-4500,-3000,-1500,0,1500,2500,3500,4000,4000,3500,2500,1500,0,-1000,-2000,-3000,-3500,-4000,-4500};
        SurvivalWeather wx;wx.temperatureMilli=16000-day*900+kHourly[hour];wx.windMilli=w.dayWind;wx.precipitationMilli=hour>=10&&hour<17?w.dayRain:w.dayRain/3;
        wx.sunMilli=hour>=7&&hour<18?900:0;return wx;
    }
    bool outdoors(const ColonyColonist& c)const{
        if(c.activity==ColonyActivity::Broken)return true;
        if(c.activity!=ColonyActivity::Working||!c.job)return false;
        auto k=w.colony.jobs.at(c.job).kind;return k!=ColonyJobKind::Recipe;
    }
    Vec2 colonistPos(const ColonyColonist& c)const{
        if(!outdoors(c))return {};const auto& j=c.job?w.colony.jobs.at(c.job):ColonyJob{};
        return nodePos(j.kind==ColonyJobKind::Gather?j.node:0);
    }
    int64_t huntAvailable(int64_t region)const{return std::max<int64_t>(0,(ecologyCount(w.ecology,region,kDeer)-20)*10);}
    bool step(int64_t tick){
        auto wx=weather(tick);const auto fx=survivalWeatherEffects(d.survival.weather,wx);
        // 1. Power and water. Wind/solar from weather, generators burn wood, the pump drives the water producer.
        PowerInput pin;pin.outputMilli[kWind]=fx.windPowerMilli;pin.outputMilli[kSolar]=fx.solarMilli;
        auto& store=w.colony.stockpiles.at(kStore);
        for(auto& [node,fuel]:w.generatorNodes){
            if(fuel==0&&colonyAvailable(store,kWood)>0){if(--store.stock[kWood]==0)store.stock.erase(kWood);fuel=kTicksPerHour*2;}
            pin.outputMilli[node]=fuel>0?1000:0;if(fuel>0)--fuel;
        }
        pin.enabled[kHeater]=w.colony.alerts.empty();                    // the fire knocks the heater out until resolved
        pin.outputMilli[kWater]=w.grid.nodes.at(kPump).powered?1000:0;
        auto power=stepPower(w.grid,tick,pin);if(!power)return false;
        for(const auto& e:power->events)if(e.kind==PowerEventKind::Shed)++w.brownouts;
        int64_t deficit=0;for(const auto& c:power->components)if(c.medium==1)deficit+=c.demand-c.served;
        // 2. Colony tick: planner commands from the previous plan, owner facts.
        ColonyInput cin;cin.unpowered=power->unpowered;
        for(const auto& [id,b]:w.bodies.bodies)if(w.colony.colonists.contains(id))cin.moodModifiers[id]=survivalMood(b);
        for(const auto& [id,n]:w.resources.nodes)cin.gatherAvailable[id]=n.amount;
        cin.gatherAvailable[kHuntNode1]=huntAvailable(1);cin.gatherAvailable[kHuntNode2]=huntAvailable(2);
        if(tick==kFireTick)cin.raise.push_back({1,7,6});
        bots::ColonyPlannerFacts facts;facts.powerDeficit=deficit;facts.custom[1]=w.generators;
        for(const auto& b:w.bodies.bodies)if(b.second.status&kSurvivalCold)++facts.cold;
        for(auto [node,amount]:cin.gatherAvailable)facts.gatherNodes.push_back({node>=kHuntNode1?3:(w.resources.nodes.at(node).resource==kRawFood?1:2),node,amount});
        auto plan=bots::planColony(d.planner,d.colony,w.colony,facts,w.planner,tick);if(!plan)return false;
        cin.commands=plan->commands;w.commands+=int64_t(plan->commands.size());
        auto report=stepColony(d.colony,w.colony,tick,cin);if(!report)return false;
        EcologyResourceInput rin;rin.growthMilli=fx.growthMilli;EcologyInput ein;ein.growthMilli=500+fx.growthMilli/2; // deer still breed slowly in the cold
        for(const auto& e:report->events){
            if(e.kind==ColonyEventKind::Produced)++w.meals;
            if(e.kind==ColonyEventKind::Gathered&&e.value>0){
                if(e.id==kHuntNode1||e.id==kHuntNode2){ein.harvests.push_back({e.id==kHuntNode1?1:2,kDeer,1});++w.huntKills;}
                else rin.harvests.push_back({e.id,e.colonist,e.value});
            }
            if(e.kind==ColonyEventKind::Built){
                if(d.colony.blueprints.at(e.id).workstationType==1){++w.kitchens;PowerNode k;k.id=e.value;k.kind=PowerNodeKind::Consumer;k.demand=40;k.priority=8;powerAddNode(w.grid,k);powerLink(w.grid,4,e.value);}
                else{++w.generators;PowerNode g;g.id=e.value;g.kind=PowerNodeKind::Producer;g.output=70;powerAddNode(w.grid,g);powerLink(w.grid,4,e.value);w.generatorNodes[e.value]=0;}
            }
            if(e.kind==ColonyEventKind::AlertWorked)++w.fireWorked;
            if(e.kind==ColonyEventKind::AlertResolved&&w.fireResolvedAt<0)w.fireResolvedAt=tick;
        }
        // 3. Bodies: shelter/heater, clothing, exertion and water from the tap (fluid graph).
        const bool heated=w.grid.nodes.at(kHeater).powered,tap=w.grid.nodes.at(kTap).powered;
        SurvivalInput sin;sin.weather=wx;
        for(const auto& [id,c]:w.colony.colonists){
            SurvivalBodyInput b;b.clothing={450,400,300};b.sheltered=!outdoors(c);
            b.shelterMilli=heated?21000:std::min<int64_t>(21000,wx.temperatureMilli+7000);
            b.exertionMilli=c.activity==ColonyActivity::Working?(b.sheltered?400:700):(c.activity==ColonyActivity::Broken?200:0);
            b.resting=c.activity==ColonyActivity::Sleeping;
            if(tap&&w.bodies.bodies.at(id).thirst<60000&&!b.resting){b.eatThirst=40000;++w.drinks;}
            sin.bodies[id]=b;
        }
        auto bodies=stepSurvival(d.survival,w.bodies,tick,sin);if(!bodies)return false;
        for(const auto& e:bodies->events)if(e.kind==SurvivalEventKind::Died){++w.deaths;colonyRemoveColonist(w.colony,e.body);}
        for(const auto& [id,b]:w.bodies.bodies)if(!b.dead)w.minCoreMilli=std::min(w.minCoreMilli,b.coreMicro/1000);
        // 4. Animals: senses-like records from distances to outdoor colonists and other animals.
        EcologyAnimalInput ain;ain.activityMilli=fx.activityMilli;
        for(auto r:{1,2})ain.vegetation[r]=std::max<int64_t>(1,5*fx.growthMilli/1000);
        for(const auto& [id,a]:w.animals.animals){
            auto sense=[&](int64_t target,Vec2 at){
                auto dist=(at-a.position).lengthSq();if(dist>3600_fx)return;
                PerceivedTarget p;p.observer=id;p.target=target;p.level=dist<400_fx?Awareness::Alert:Awareness::Suspicious;p.visibleNow=true;p.sensedNow=true;
                p.lastKnown={at.x,Fixed{},at.y};ain.perceived.push_back(p);
            };
            for(const auto& [cid,c]:w.colony.colonists)if(outdoors(c)){auto at=colonistPos(c);sense(cid,at);ain.facts[cid]={0,at};}
            for(const auto& [oid,o]:w.animals.animals)if(oid!=id)sense(oid,o.position);
        }
        auto animals=stepEcologyAnimals(d.animals,w.animals,tick,ain,rng);if(!animals)return false;
        for(const auto& e:animals->events){
            if(e.kind==EcologyAnimalEventKind::Killed){ein.harvests.push_back({e.value?e.value:1,kDeer,1});++w.wolfKills;}
            if(e.kind==EcologyAnimalEventKind::Starved)++w.starved;
            if(e.kind==EcologyAnimalEventKind::Behavior&&e.value==int64_t(EcologyBehavior::Flee))++w.flights;
        }
        // 5. Populations and renewal; individual animals follow their population (respawn every period).
        for(auto r:{1,2})ein.capacityMilli[r]=800+fx.growthMilli*200/1000;
        auto eco=stepEcology(d.ecology,w.ecology,tick,ein,rng);if(!eco)return false;
        auto res=stepEcologyResources(w.resources,tick,rin);if(!res)return false;
        if(eco->period){
            int64_t deer=0,wolves=0;for(const auto& [id,a]:w.animals.animals){(void)id;(a.species==kDeer?deer:wolves)++;}
            for(;deer<std::min<int64_t>(6,ecologyCount(w.ecology,1,kDeer)/10);++deer)ecologyAddAnimal(d.animals,w.animals,w.nextAnimal++,kDeer,1,Vec2{Fixed::fromInt(30+rng.range(-10,10)),Fixed::fromInt(40+rng.range(-10,10))});
            for(;wolves<std::min<int64_t>(2,ecologyCount(w.ecology,1,kWolf));++wolves)ecologyAddAnimal(d.animals,w.animals,w.nextAnimal++,kWolf,1,Vec2{70_fx,Fixed::fromInt(60+rng.range(-10,10))});
        }
        w.rngState=rng.state();w.rngInc=rng.inc();
        return true;
    }
    uint64_t hash()const{
        Hasher h;hashColony(h,w.colony);hashColony(h,w.grid);hashSurvival(h,w.bodies);hashSurvival(h,w.ecology);hashSurvival(h,w.animals);hashSurvival(h,w.resources);
        h.u64(bots::colonyPlannerHash(w.planner));h.u64(w.rngState);h.u64(w.rngInc);
        for(auto v:{w.starved,w.flights,w.nextAnimal,w.fireResolvedAt,w.generators,w.kitchens,w.meals,w.deaths,w.brownouts,w.huntKills,w.wolfKills,w.commands,w.fireWorked,w.minCoreMilli,w.drinks})h.i64(v);
        for(auto [k,v]:w.generatorNodes){h.i64(k);h.i64(v);}
        return h.value();
    }
};
// Save every package record through genre::save and rebuild a fresh owner from the bytes (+ owner fields).
bool restoreInto(const Owner& from,Owner& to){
    auto copy=[](const auto& src,auto& dst){auto bytes=save(src);return !bytes.empty()&&load(bytes,dst);};
    if(!copy(from.w.colony,to.w.colony)||!copy(from.w.grid,to.w.grid)||!copy(from.w.bodies,to.w.bodies)||!copy(from.w.ecology,to.w.ecology)
        ||!copy(from.w.animals,to.w.animals)||!copy(from.w.resources,to.w.resources)||!copy(from.w.planner,to.w.planner))return false;
    auto keep=to.w;to.w=from.w;to.w.colony=keep.colony;to.w.grid=keep.grid;to.w.bodies=keep.bodies;to.w.ecology=keep.ecology;to.w.animals=keep.animals;to.w.resources=keep.resources;to.w.planner=keep.planner;
    to.rng.restore(from.w.rngState,from.w.rngInc);return true;
}
std::optional<uint64_t> run(const Data& d,bool print,int64_t restoreAt){
    Owner o(d);o.setup();
    for(int64_t t=0;t<kTicks;++t){
        if(t==restoreAt){Owner fresh(d);if(!restoreInto(o,fresh)){std::printf("restore failed at %lld\n",(long long)t);return std::nullopt;}o.w=fresh.w;o.rng=fresh.rng;}
        if(!o.step(t)){std::printf("step rejected at tick %lld\n",(long long)t);return std::nullopt;}
        if(print&&(t+1)%kDay==0){
            int64_t meals=0,raw=0,wood=0;for(const auto& [id,p]:o.w.colony.stockpiles){(void)id;meals+=colonyStock(p,kMeal);raw+=colonyStock(p,kRawFood);wood+=colonyStock(p,kWood);}
            std::printf("day %2lld: colonists %zu meals %lld raw %lld wood %lld | deer %lld/%lld wolves %lld/%lld | gens %lld kitchens %lld battery %lld\n",(long long)((t+1)/kDay),
                o.w.colony.colonists.size(),(long long)meals,(long long)raw,(long long)wood,(long long)ecologyCount(o.w.ecology,1,kDeer),(long long)ecologyCount(o.w.ecology,2,kDeer),
                (long long)ecologyCount(o.w.ecology,1,kWolf),(long long)ecologyCount(o.w.ecology,2,kWolf),(long long)o.w.generators,(long long)o.w.kitchens,(long long)o.w.grid.nodes.at(3).charge);
        }
    }
    const auto& w=o.w;int64_t condition=0,alive=0;
    for(const auto& [id,b]:w.bodies.bodies){(void)id;if(!b.dead){++alive;condition+=b.condition;}}
    const int64_t deer=ecologyCount(w.ecology,1,kDeer)+ecologyCount(w.ecology,2,kDeer),wolves=ecologyCount(w.ecology,1,kWolf)+ecologyCount(w.ecology,2,kWolf);
    const bool ok=alive==10&&w.colony.colonists.size()==10&&w.deaths==0&&w.fireResolvedAt>0&&w.meals>=60&&deer>0&&wolves>0&&w.generators>=1&&w.commands>0&&condition/std::max<int64_t>(1,alive)>=50000;
    if(print){
        std::printf("summary: days %lld alive %lld deaths %lld meals cooked %lld avg condition %lld min core %lld mC\n",(long long)kDays,(long long)alive,(long long)w.deaths,(long long)w.meals,(long long)(condition/std::max<int64_t>(1,alive)),(long long)w.minCoreMilli);
        std::printf("summary: fire raised %lld resolved %lld (jobs %lld), generators %lld, kitchens %lld, shed events %lld, drinks %lld\n",(long long)kFireTick,(long long)w.fireResolvedAt,(long long)w.fireWorked,(long long)w.generators,(long long)w.kitchens,(long long)w.brownouts,(long long)w.drinks);
        std::printf("summary: deer %lld wolves %lld, hunted %lld, wolf kills %lld, animal flights %lld, starved animals %lld, planner commands %lld, criteria %s\n",(long long)deer,(long long)wolves,(long long)w.huntKills,(long long)w.wolfKills,(long long)w.flights,(long long)w.starved,(long long)w.commands,ok?"met":"FAILED");
    }
    if(!ok)return std::nullopt;
    return o.hash();
}
}
int main(int argc,char** argv){
    const std::string dir=argc>1?argv[1]:"examples/libraries/genre/production-survival";const bool quiet=argc>2&&std::string(argv[2])=="--quiet";
    Data d;
    try{
        auto settings=gameplay::readGameplaySettings(Json::parseFile(dir+"/settings.json"));
        if(!settings.value){for(auto& e:settings.errors)std::printf("settings: %s\n",e.c_str());return 2;}
        if(!colonySettingsErrors(*settings.value).empty()||!survivalSettingsErrors(*settings.value).empty())return 2;
        d.colony=readColonyDefinitions(Json::parseFile(dir+"/colony.json"),colonyConfig(*settings.value));
        d.grid=readPowerGrid(Json::parseFile(dir+"/power.json"));
        auto eco=Json::parseFile(dir+"/ecology.json");
        d.ecology=readEcologyDefinitions(eco["ecology"]);d.animals=readEcologyAnimals(eco["animals"]);d.resources=readEcologyResources(eco["resources"]);
        d.planner=bots::readColonyPlanner(Json::parseFile(dir+"/planner.json"),d.colony);d.survival=survivalConfig(*settings.value);
    }catch(const std::exception& e){std::printf("data error: %s\n",e.what());return 2;}
    auto first=run(d,!quiet,-1);if(!first)return 1;
    auto second=run(d,false,-1);auto restored=run(d,false,10*kDay+7);
    std::printf("final hash %016llx (second run %016llx, restored at day 10 %016llx)\n",(unsigned long long)*first,(unsigned long long)(second?*second:0),(unsigned long long)(restored?*restored:0));
    if(!second||!restored||*second!=*first||*restored!=*first){std::printf("determinism check FAILED\n");return 1;}
    std::printf("determinism: identical hashes across two runs and a save/restore run\n");
    return 0;
}
