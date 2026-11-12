#pragma once
// survival-ecology: animal behavior states (Wander / Graze / Flee / Hunt / Sleep) for individual animals.
// Decisions come only from senses-like inputs: genre::PerceivedTarget records (stealth-senses, observer = animal
// id) plus owner facts about each perceived target (species, current position). Priority per tick: flee a feared
// target within fleeDistance (and keep fleeing for fleeCalmTicks) > sleep window / exhaustion > hunt visible prey
// when hungry > graze while hungry and vegetation is available > wander around home. Wander goals use the passed-in
// engine Random. Movement is kinematic on the XZ plane (Fixed); the owner may correct positions through `poses`.
// Kills and grazing are events: the owner applies them to ecology populations / renewal nodes.
#include "Senses.h"
#include "engine/core/Random.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr std::size_t kEcologyMaxAnimals=65536;
inline constexpr int64_t kEcologyMeterMax=1000;
enum class EcologyBehavior:uint8_t {Wander,Graze,Flee,Hunt,Sleep};
struct EcologyAnimalDef {
    int64_t species=0;
    std::vector<int64_t> prey,fears;            // sorted species ids; species 0 = humans / unaffiliated owner targets
    Fixed walkSpeed=Fixed::fromRatio(1,20),runSpeed=Fixed::fromRatio(1,5); // metres per tick
    Fixed fleeDistance=12_fx,huntRange=25_fx,catchDistance=1_fx,wanderRadius=10_fx;
    int64_t hungerPerTick=1,grazeGain=4,huntGain=600,grazeBelow=500,huntBelow=400,fullAt=950; // meters 0..1000
    int64_t energyPerTick=1,sleepGain=3,sleepBelow=150;
    int64_t sleepFrom=-1,sleepTo=-1;             // hours [from, to) of the owner day; -1 = no fixed window
    int64_t wanderTicks=120,huntGiveUpTicks=300,fleeCalmTicks=90,huntCooldownTicks=240;
    Awareness fleeFrom=Awareness::Suspicious,huntFrom=Awareness::Alert;
};
struct EcologyAnimalDefs {int64_t ticksPerHour=60;Vec2 boundsMin{-1000_fx,-1000_fx},boundsMax{1000_fx,1000_fx};std::map<int64_t,EcologyAnimalDef> species;};
struct EcologyAnimal {
    int64_t id=0,species=0,region=0;Vec2 position{},home{},goal{};
    EcologyBehavior behavior=EcologyBehavior::Wander;int64_t hunger=kEcologyMeterMax,energy=kEcologyMeterMax;
    int64_t target=0,since=0,nextWander=0,calmUntil=0,huntReadyAt=0;bool alive=true;
    bool operator==(const EcologyAnimal&)const=default;
};
struct EcologyAnimalState {std::map<int64_t,EcologyAnimal> animals;int64_t lastTick=0;bool initialized=false;}; // record kind 108
struct EcologyTargetFacts {int64_t species=0;Vec2 position{};};   // what the animal can observe of a perceived target
struct EcologyAnimalInput {
    std::vector<PerceivedTarget> perceived;       // stealth-senses records; only observer == animal id are used
    std::map<int64_t,EcologyTargetFacts> facts;   // target id -> facts; animals of this state are known implicitly
    std::map<int64_t,int64_t> vegetation;         // region -> grazing units available this tick
    std::map<int64_t,Vec2> poses;                 // owner-corrected positions (collision)
    std::vector<int64_t> removed;                 // animals killed/removed by other owners
    int64_t activityMilli=1000;                   // weather factor on walking speed (SurvivalWeatherEffects::activityMilli)
};
enum class EcologyAnimalEventKind:uint8_t {Behavior,Killed,Grazed,Starved}; // Behavior value = new state; Killed target = prey; Grazed value = region
struct EcologyAnimalEvent {EcologyAnimalEventKind kind=EcologyAnimalEventKind::Behavior;int64_t animal=0,target=0,value=0;};
struct EcologyAnimalReport {std::vector<EcologyAnimalEvent> events;std::map<int64_t,int64_t> grazed;}; // grazed: region -> units eaten

inline bool ecologySortedIds(const std::vector<int64_t>& v){return std::is_sorted(v.begin(),v.end())&&std::adjacent_find(v.begin(),v.end())==v.end()&&(v.empty()||v.front()>=0);}
inline std::string ecologyAnimalDefsError(const EcologyAnimalDefs& d){
    if(d.ticksPerHour<1||d.ticksPerHour>1000000)return "animals.ticksPerHour: 1..1000000";
    if(!(d.boundsMin.x<d.boundsMax.x&&d.boundsMin.y<d.boundsMax.y))return "animals.bounds: min < max";
    auto m=[](int64_t v){return v>=0&&v<=kEcologyMeterMax;};
    for(const auto& [id,a]:d.species){
        auto p="animals.species "+std::to_string(id);
        if(id<=0||a.species!=id)return p+": id mismatch";
        if(!ecologySortedIds(a.prey)||!ecologySortedIds(a.fears))return p+": prey/fears must be sorted unique ids";
        for(auto f:{a.walkSpeed,a.runSpeed,a.fleeDistance,a.huntRange,a.catchDistance,a.wanderRadius})if(f<Fixed{}||f>1000_fx)return p+": distances/speeds 0..1000";
        if(!m(a.hungerPerTick)||!m(a.grazeGain)||!m(a.huntGain)||!m(a.grazeBelow)||!m(a.huntBelow)||!m(a.fullAt)||!m(a.energyPerTick)||!m(a.sleepGain)||!m(a.sleepBelow))return p+": meters 0..1000";
        if(a.sleepFrom< -1||a.sleepFrom>23||a.sleepTo< -1||a.sleepTo>23||((a.sleepFrom<0)!=(a.sleepTo<0)))return p+": sleep hours -1 or 0..23 (both)";
        for(auto t:{a.wanderTicks,a.huntGiveUpTicks,a.fleeCalmTicks,a.huntCooldownTicks})if(t<1||t>10000000)return p+": ticks 1..10000000";
        if(uint8_t(a.fleeFrom)>uint8_t(Awareness::Alert)||uint8_t(a.huntFrom)>uint8_t(Awareness::Alert))return p+": awareness";
    }
    return {};
}
inline bool valid(const EcologyAnimalDefs& d){return ecologyAnimalDefsError(d).empty();}
inline bool valid(const EcologyAnimal& a){
    return a.id>0&&a.species>0&&a.region>=0&&uint8_t(a.behavior)<=uint8_t(EcologyBehavior::Sleep)&&a.hunger>=0&&a.hunger<=kEcologyMeterMax&&a.energy>=0&&a.energy<=kEcologyMeterMax
        &&a.target>=0&&a.since>=0&&a.nextWander>=0&&a.calmUntil>=0&&a.huntReadyAt>=0;
}
inline bool valid(const EcologyAnimalState& s){
    if(s.animals.size()>kEcologyMaxAnimals||s.lastTick<0)return false;
    for(const auto& [id,a]:s.animals)if(id!=a.id||!valid(a)||!a.alive)return false;
    return true;
}
inline bool ecologyAddAnimal(const EcologyAnimalDefs& d,EcologyAnimalState& s,int64_t id,int64_t species,int64_t region,Vec2 position){
    if(id<=0||s.animals.contains(id)||!d.species.contains(species)||region<0||s.animals.size()>=kEcologyMaxAnimals)return false;
    EcologyAnimal a;a.id=id;a.species=species;a.region=region;a.position=a.home=a.goal=position;s.animals.emplace(id,a);return true;
}
namespace detail_ecology_animals {
inline bool has(const std::vector<int64_t>& v,int64_t x){return std::binary_search(v.begin(),v.end(),x);}
inline Vec2 clampTo(const EcologyAnimalDefs& d,Vec2 p){return {std::clamp(p.x,d.boundsMin.x,d.boundsMax.x),std::clamp(p.y,d.boundsMin.y,d.boundsMax.y)};}
inline Vec2 moveToward(Vec2 from,Vec2 to,Fixed step){
    auto d=to-from;auto len=d.length();if(len.raw<=0||len<=step)return to;return from+d*(step/len);
}
inline bool inWindow(int64_t hour,int64_t from,int64_t to){if(from<0)return false;return from<=to?(hour>=from&&hour<to):(hour>=from||hour<to);}
}
inline std::optional<EcologyAnimalReport> stepEcologyAnimals(const EcologyAnimalDefs& d,EcologyAnimalState& state,int64_t tick,const EcologyAnimalInput& in,Random& rng){
    using namespace detail_ecology_animals;
    if(tick<0||(state.initialized&&tick<=state.lastTick)||!valid(d)||!valid(state)||in.activityMilli<0||in.activityMilli>1000||in.perceived.size()>1000000)return std::nullopt;
    for(const auto& [id,a]:state.animals){(void)id;if(!d.species.contains(a.species))return std::nullopt;}
    for(const auto& [id,p]:in.poses){(void)p;if(!state.animals.contains(id))return std::nullopt;}
    for(auto [r,v]:in.vegetation)if(r<0||v<0)return std::nullopt;
    auto s=state;EcologyAnimalReport rep;
    for(auto id:in.removed)s.animals.erase(id);
    for(auto& [id,p]:in.poses)if(auto it=s.animals.find(id);it!=s.animals.end())it->second.position=clampTo(d,p);
    // Perceived records per observer (input order is irrelevant: the nearest by (distance, id) wins).
    std::map<int64_t,std::vector<const PerceivedTarget*>> seen;
    for(const auto& p:in.perceived)if(p.target>0&&s.animals.contains(p.observer))seen[p.observer].push_back(&p);
    const auto snapshot=s.animals; // decisions read positions at the start of the tick
    auto factOf=[&](const PerceivedTarget& p)->std::optional<EcologyTargetFacts>{
        if(auto f=in.facts.find(p.target);f!=in.facts.end())return f->second;
        if(auto a=snapshot.find(p.target);a!=snapshot.end())return EcologyTargetFacts{a->second.species,a->second.position};
        return std::nullopt;
    };
    std::map<int64_t,int64_t> vegetation=in.vegetation;std::vector<int64_t> dead;
    const auto hour=(tick/d.ticksPerHour)%24;
    for(auto& [id,a]:s.animals){
        if(std::find(dead.begin(),dead.end(),id)!=dead.end())continue;
        const auto& def=d.species.at(a.species);const auto before=a.behavior;
        // Nearest feared target and nearest huntable prey from this animal's senses.
        const EcologyTargetFacts* threat=nullptr;EcologyTargetFacts threatFact{};Fixed threatDist{};int64_t threatId=0;
        int64_t preyId=0;Vec2 preyAt{};Fixed preyDist{};
        for(const auto* p:seen[id]){
            auto f=factOf(*p);Vec2 at=f?f->position:Vec2{p->lastKnown.x,p->lastKnown.z};const auto species=f?f->species:0;
            auto dist=(at-a.position).lengthSq();
            if(p->level>=def.fleeFrom&&has(def.fears,species)&&(!threat||dist<threatDist||(dist==threatDist&&p->target<threatId))){threatFact={species,at};threat=&threatFact;threatDist=dist;threatId=p->target;}
            if(f&&p->visibleNow&&p->level>=def.huntFrom&&has(def.prey,species)&&(!preyId||dist<preyDist||(dist==preyDist&&p->target<preyId))){preyId=p->target;preyAt=at;preyDist=dist;}
        }
        const bool threatNear=threat&&threatDist<=def.fleeDistance*def.fleeDistance;
        if(threatNear){
            a.behavior=EcologyBehavior::Flee;a.calmUntil=tick+def.fleeCalmTicks;a.target=threatId;
            auto away=a.position-threat->position;auto len=away.length();
            if(len.raw<=0)away=Vec2{1_fx,Fixed{}};else away=away*(def.fleeDistance/len);
            a.goal=clampTo(d,a.position+away);
        }else if(a.behavior==EcologyBehavior::Flee&&tick<a.calmUntil){
            // keep running to the last flee goal
        }else if(inWindow(hour,def.sleepFrom,def.sleepTo)||a.energy<def.sleepBelow||(a.behavior==EcologyBehavior::Sleep&&a.energy<kEcologyMeterMax&&a.energy<def.sleepBelow*2)){
            a.behavior=EcologyBehavior::Sleep;a.target=0;
        }else if(!def.prey.empty()&&preyId&&tick>=a.huntReadyAt&&(a.hunger<def.huntBelow||(a.behavior==EcologyBehavior::Hunt&&a.target==preyId))){
            if(a.behavior!=EcologyBehavior::Hunt||a.target!=preyId){a.since=tick;a.target=preyId;}
            a.behavior=EcologyBehavior::Hunt;a.goal=preyAt;
            if(tick-a.since>def.huntGiveUpTicks){a.behavior=EcologyBehavior::Wander;a.target=0;a.huntReadyAt=tick+def.huntCooldownTicks;a.nextWander=tick;}
        }else if(def.grazeGain>0&&(a.hunger<def.grazeBelow||(a.behavior==EcologyBehavior::Graze&&a.hunger<def.fullAt))&&vegetation[a.region]>0){
            a.behavior=EcologyBehavior::Graze;a.target=0;
        }else{
            if(a.behavior!=EcologyBehavior::Wander){a.behavior=EcologyBehavior::Wander;a.target=0;a.nextWander=tick;}
        }
        if(a.behavior==EcologyBehavior::Wander&&(tick>=a.nextWander||a.position==a.goal)){
            auto jitter=[&](Fixed r){return r.raw<=0?Fixed{}:Fixed::fromRaw(int64_t(rng.below(uint32_t(std::min<int64_t>(r.raw*2+1,int64_t(UINT32_MAX)))))-r.raw);};
            auto dx=jitter(def.wanderRadius);auto dz=jitter(def.wanderRadius);
            a.goal=clampTo(d,a.home+Vec2{dx,dz});a.nextWander=tick+def.wanderTicks;
        }
        // Movement and meters.
        const bool running=a.behavior==EcologyBehavior::Flee||a.behavior==EcologyBehavior::Hunt;
        if(a.behavior!=EcologyBehavior::Sleep&&a.behavior!=EcologyBehavior::Graze){
            auto speed=running?def.runSpeed:def.walkSpeed*Fixed::fromRatio(in.activityMilli,1000);
            a.position=clampTo(d,moveToward(a.position,a.goal,speed));
        }
        a.hunger=std::max<int64_t>(0,a.hunger-(a.behavior==EcologyBehavior::Sleep?def.hungerPerTick/2:def.hungerPerTick));
        if(a.behavior==EcologyBehavior::Sleep)a.energy=std::min(kEcologyMeterMax,a.energy+def.sleepGain);
        else a.energy=std::max<int64_t>(0,a.energy-(running?2*def.energyPerTick:def.energyPerTick));
        if(a.behavior==EcologyBehavior::Graze){
            auto& v=vegetation[a.region];if(v>0){--v;++rep.grazed[a.region];a.hunger=std::min(kEcologyMeterMax,a.hunger+def.grazeGain);rep.events.push_back({EcologyAnimalEventKind::Grazed,id,0,a.region});}
        }
        if(a.behavior==EcologyBehavior::Hunt&&(a.position-preyAt).lengthSq()<=def.catchDistance*def.catchDistance){
            rep.events.push_back({EcologyAnimalEventKind::Killed,id,preyId,a.region});a.hunger=std::min(kEcologyMeterMax,a.hunger+def.huntGain);
            if(s.animals.contains(preyId))dead.push_back(preyId);
            a.behavior=EcologyBehavior::Wander;a.target=0;a.huntReadyAt=tick+def.huntCooldownTicks;a.nextWander=tick+1;
        }
        if(a.behavior!=before)rep.events.push_back({EcologyAnimalEventKind::Behavior,id,a.target,int64_t(a.behavior)});
        if(a.hunger==0){rep.events.push_back({EcologyAnimalEventKind::Starved,id,0,a.region});dead.push_back(id);}
    }
    for(auto id:dead)s.animals.erase(id);
    s.lastTick=tick;s.initialized=true;state=std::move(s);return rep;
}
}
