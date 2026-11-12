#pragma once
// settlement-production: colony definitions, dynamic state, commands and validation.
// Integer math only (needs/mood in 0..100000, work in "ticks at normal speed" x 1000), stable positive ids,
// std::map iteration for deterministic order. Jobs reuse lib::Task claiming (Tasks.h) and stock reservations
// (systems/Reservations.h, one book per stockpile, ticket = job id, owner = colonist id).
#include "../Tasks.h"
#include "../systems/Reservations.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr int64_t kColonyNeedMax=100000;
inline constexpr int64_t kColonyMoodLimit=100000;           // mood range -limit..limit
inline constexpr int kColonyHours=24;
inline constexpr int kColonyLowestPriority=4;               // work priority 1 (highest) .. 4, 0 = disabled
inline constexpr std::size_t kColonyMaxColonists=1024,kColonyMaxJobs=16384,kColonyMaxDefs=4096,kColonyMaxEntries=256;
inline constexpr int64_t kColonyAmountLimit=1000000000000;  // stock / work / capacity bound
inline constexpr int64_t kColonySpeedLimit=100000;          // milli speed multipliers

enum class ColonyBlock:uint8_t {Anything,Work,Sleep,Leisure};
enum class ColonyActivity:uint8_t {Idle,Working,Sleeping,Leisure,Eating,Broken,Incapacitated};
enum class ColonyMoodBand:uint8_t {Breaking,Low,Content,High};
enum class ColonyJobKind:uint8_t {Recipe,Haul,Build,Gather,Response};

// ---- Immutable definitions (bind colonyDefsHash in the save envelope).
struct ColonyMoodStep {int64_t below=0,mood=0;};             // need value < below adds mood (first matching step, ascending below)
struct ColonyNeedDef {
    int64_t id=0,decayPerTick=0,initial=kColonyNeedMax;
    int64_t lowAt=0,criticalAt=0;                            // events + overrides: critical rest -> sleep anywhere, critical meal need -> eat
    int64_t recoveredAt=kColonyNeedMax;                     // sleep/leisure for this need continues until this value
    std::vector<ColonyMoodStep> mood;
};
struct ColonyRestoreDef {ColonyActivity activity=ColonyActivity::Sleeping;int64_t need=0,perTick=0;};
struct ColonyMealDef {int64_t resource=0,need=0,amount=0,ticks=1;}; // eats one unit, restores amount, busy for ticks
struct ColonyWorkTypeDef {int64_t id=0;int defaultPriority=3;};
struct ColonyRecipeDef {int64_t id=0,workstationType=0,workType=0,work=1;systems::Quantities inputs,outputs;};
struct ColonyBlueprintDef {int64_t id=0,workType=0,work=1,workstationType=0;bool needsPower=false;systems::Quantities cost;};
struct ColonyGatherDef {int64_t id=0,workType=0,resource=0,yield=1,work=1;};
struct ColonyHaulDef {int64_t id=0,from=0,to=0,resource=0,keep=0,batch=1,work=1,workType=0;}; // keep `to` stocked from `from`
struct ColonyAlertDef {
    int64_t id=0,workType=0,priority=100,workPerSeverity=1,mood=0;
    bool preempt=true,ignoreSchedule=true;                  // responders drop other jobs / work through sleep blocks
};
struct ColonyConfig {
    int64_t ticksPerHour=60,moodBase=0;
    int64_t breakAt=-60000,lowAt=-20000,highAt=40000;       // mood band thresholds: < breakAt Breaking, < lowAt Low, >= highAt High
    int64_t breakTicks=120,lowSpeedMilli=700,contentSpeedMilli=1000,highSpeedMilli=1200;
    int64_t maxResponseJobs=64;
};
struct ColonyDefs {
    ColonyConfig config;
    std::map<int64_t,ColonyNeedDef> needs;std::vector<ColonyRestoreDef> restores;std::vector<ColonyMealDef> meals;
    int64_t restNeed=0;                                     // need restored by sleeping; critical -> sleep anywhere
    std::map<int64_t,ColonyWorkTypeDef> workTypes;std::map<int64_t,ColonyRecipeDef> recipes;std::map<int64_t,ColonyBlueprintDef> blueprints;
    std::map<int64_t,ColonyGatherDef> gathers;std::map<int64_t,ColonyHaulDef> hauls;std::map<int64_t,ColonyAlertDef> alerts;
};

// ---- Dynamic state (record kind 104).
struct ColonyColonist {
    int64_t id=0;
    std::map<int64_t,int64_t> priorities;                  // work type -> 0..4 (missing: work type default)
    std::map<int64_t,int64_t> skills;                      // work type -> speed milli (missing: 1000)
    std::array<ColonyBlock,kColonyHours> schedule{};
    std::map<int64_t,int64_t> needs;                       // need id -> 0..kColonyNeedMax
    int64_t mood=0;ColonyMoodBand band=ColonyMoodBand::Content;
    ColonyActivity activity=ColonyActivity::Idle;int64_t busyUntil=0,calmUntil=0,job=0,restoring=0; // restoring: need recovered by sleep/leisure; no new break before calmUntil
    bool incapacitated=false;
    bool operator==(const ColonyColonist&)const=default;
};
struct ColonyStockpile {int64_t id=0,capacity=0;systems::Quantities stock;systems::ReservationBook reserved;}; // capacity 0 = unlimited
struct ColonyWorkstation {int64_t id=0,type=0,stockpile=0,recipe=0,target=0,job=0;bool needsPower=false;}; // recipe 0: no bill, target 0: repeat
struct ColonyJob {
    Task task{};ColonyJobKind kind=ColonyJobKind::Recipe;
    int64_t workType=0,ref=0,site=0,from=0,to=0,node=0,amount=0,work=1,progress=0,alert=0,createdAt=0;
    bool blocked=false;
};
struct ColonyAlert {int64_t id=0,kind=0,ref=0,severity=0,raisedAt=0,worked=0;};
struct ColonyState {
    std::map<int64_t,ColonyColonist> colonists;std::map<int64_t,ColonyStockpile> stockpiles;std::map<int64_t,ColonyWorkstation> workstations;
    std::map<int64_t,ColonyJob> jobs;std::map<int64_t,ColonyAlert> alerts;
    int64_t nextJob=1,nextAlert=1,nextStructure=100000,lastCommand=0,lastTick=0;bool initialized=false;
};

// ---- Per-tick input: player/bot commands (same path) and owner facts.
enum class ColonyCommandKind:uint8_t {SetPriority,SetSchedule,SetBill,ClearBill,OrderBuild,OrderGather,CancelJob};
struct ColonyCommand {
    int64_t seq=0;ColonyCommandKind kind=ColonyCommandKind::SetPriority;
    int64_t colonist=0,target=0,ref=0,value=0,stockpile=0; // see colonyCommandValid for per-kind fields
};
struct ColonyRaise {int64_t kind=0,ref=0,severity=1;};
struct ColonyInput {
    std::vector<ColonyCommand> commands;                   // ascending seq; seq <= lastCommand is ignored
    std::vector<ColonyRaise> raise;std::vector<int64_t> clear;      // owner alerts (fire/flood/raid from field/combat owners)
    std::map<int64_t,int64_t> moodModifiers;               // colonist -> extra mood (survival comfort, rooms, ...)
    std::map<int64_t,bool> incapacitated;                  // colonist -> owner health state
    std::vector<int64_t> unpowered;                        // structure ids without power this tick (PowerReport)
    std::map<int64_t,int64_t> gatherAvailable;             // gather node -> amount available now (renewal/ecology owner)
};
enum class ColonyEventKind:uint8_t {
    NeedLow,NeedCritical,NeedRecovered,   // colonist, id = need, value = need value
    MoodBand,MentalBreak,                 // value = new band
    Ate,Starving,                         // id = resource / need
    JobCreated,JobAssigned,JobReleased,JobCompleted,JobBlocked, // job; value = job kind
    Produced,Hauled,Built,Gathered,       // job; id = recipe/haul/blueprint/node; value = amount or new structure id
    AlertRaised,AlertWorked,AlertResolved,// id = alert, value = ref
    CommandIgnored                        // value = seq
};
struct ColonyEvent {ColonyEventKind kind=ColonyEventKind::NeedLow;int64_t colonist=0,job=0,id=0,value=0;};
struct ColonyReport {std::vector<ColonyEvent> events;int64_t working=0,idle=0,sleeping=0,broken=0;};

// ---- Validation.
inline bool colonyQuantitiesValid(const systems::Quantities& q,bool allowEmpty){
    if(q.empty())return allowEmpty;
    if(q.size()>kColonyMaxEntries)return false;
    for(auto [id,n]:q)if(id<=0||n<=0||n>kColonyAmountLimit)return false;
    return true;
}
inline std::string colonyDefsError(const ColonyDefs& d){
    const auto& c=d.config;
    if(c.ticksPerHour<1||c.ticksPerHour>1000000)return "config.ticksPerHour: 1..1000000";
    if(!(c.breakAt<c.lowAt&&c.lowAt<c.highAt)||c.breakAt< -kColonyMoodLimit||c.highAt>kColonyMoodLimit)return "config: breakAt < lowAt < highAt within mood limits";
    if(c.moodBase< -kColonyMoodLimit||c.moodBase>kColonyMoodLimit)return "config.moodBase: out of range";
    if(c.breakTicks<1||c.breakTicks>10000000)return "config.breakTicks: 1..10000000";
    for(auto v:{c.lowSpeedMilli,c.contentSpeedMilli,c.highSpeedMilli})if(v<0||v>kColonySpeedLimit)return "config: speeds 0..100000";
    if(c.maxResponseJobs<1||c.maxResponseJobs>1024)return "config.maxResponseJobs: 1..1024";
    for(auto* m:{&d.needs})if(m->size()>kColonyMaxDefs)return "needs: too many";
    for(const auto& [id,n]:d.needs){
        auto p="need "+std::to_string(id);
        if(id<=0||n.id!=id)return p+": id mismatch";
        if(n.decayPerTick<0||n.decayPerTick>kColonyNeedMax||n.initial<0||n.initial>kColonyNeedMax)return p+": decay/initial out of range";
        if(!(0<=n.criticalAt&&n.criticalAt<=n.lowAt&&n.lowAt<=n.recoveredAt&&n.recoveredAt<=kColonyNeedMax))return p+": 0 <= criticalAt <= lowAt <= recoveredAt <= max";
        int64_t prev= -1;for(const auto& s:n.mood){if(s.below<=prev||s.below>kColonyNeedMax+1||s.mood< -kColonyMoodLimit||s.mood>kColonyMoodLimit)return p+".mood: ascending below, mood within limits";prev=s.below;}
    }
    if(d.restNeed&&!d.needs.contains(d.restNeed))return "restNeed: unknown need";
    for(std::size_t i=0;i<d.restores.size();++i){const auto& r=d.restores[i];if(!d.needs.contains(r.need)||r.perTick<=0||r.perTick>kColonyNeedMax||(r.activity!=ColonyActivity::Sleeping&&r.activity!=ColonyActivity::Leisure&&r.activity!=ColonyActivity::Working))return "restores["+std::to_string(i)+"]: unknown need or invalid rate/activity";}
    for(std::size_t i=0;i<d.meals.size();++i){const auto& m=d.meals[i];if(m.resource<=0||!d.needs.contains(m.need)||m.amount<=0||m.amount>kColonyNeedMax||m.ticks<1||m.ticks>1000000)return "meals["+std::to_string(i)+"]: invalid";}
    if(d.workTypes.size()>kColonyMaxDefs||d.recipes.size()>kColonyMaxDefs||d.blueprints.size()>kColonyMaxDefs||d.gathers.size()>kColonyMaxDefs||d.hauls.size()>kColonyMaxDefs||d.alerts.size()>kColonyMaxDefs)return "too many definitions";
    for(const auto& [id,w]:d.workTypes)if(id<=0||w.id!=id||w.defaultPriority<0||w.defaultPriority>kColonyLowestPriority)return "work "+std::to_string(id)+": id mismatch or default priority 0..4";
    auto work=[&](int64_t w){return d.workTypes.contains(w);};
    auto amount=[](int64_t v){return v>=1&&v<=kColonyAmountLimit;};
    for(const auto& [id,r]:d.recipes)if(id<=0||r.id!=id||r.workstationType<=0||!work(r.workType)||!amount(r.work)||!colonyQuantitiesValid(r.inputs,true)||!colonyQuantitiesValid(r.outputs,false))return "recipe "+std::to_string(id)+": invalid (workstation type, work type, work or quantities)";
    for(const auto& [id,b]:d.blueprints)if(id<=0||b.id!=id||!work(b.workType)||!amount(b.work)||b.workstationType<0||!colonyQuantitiesValid(b.cost,true))return "blueprint "+std::to_string(id)+": invalid";
    for(const auto& [id,g]:d.gathers)if(id<=0||g.id!=id||!work(g.workType)||g.resource<=0||!amount(g.yield)||!amount(g.work))return "gather "+std::to_string(id)+": invalid";
    for(const auto& [id,h]:d.hauls)if(id<=0||h.id!=id||h.from<=0||h.to<=0||h.from==h.to||h.resource<=0||h.keep<1||h.keep>kColonyAmountLimit||!amount(h.batch)||!amount(h.work)||!work(h.workType))return "haul "+std::to_string(id)+": invalid";
    for(const auto& [id,a]:d.alerts)if(id<=0||a.id!=id||!work(a.workType)||a.priority< -1000000||a.priority>1000000||!amount(a.workPerSeverity)||a.mood< -kColonyMoodLimit||a.mood>kColonyMoodLimit)return "alert "+std::to_string(id)+": invalid";
    return {};
}
inline bool valid(const ColonyDefs& d){return colonyDefsError(d).empty();}
inline int64_t colonyPriority(const ColonyDefs& d,const ColonyColonist& c,int64_t workType){
    if(auto it=c.priorities.find(workType);it!=c.priorities.end())return it->second;
    auto w=d.workTypes.find(workType);return w==d.workTypes.end()?0:w->second.defaultPriority;
}
inline int64_t colonySkill(const ColonyColonist& c,int64_t workType){auto it=c.skills.find(workType);return it==c.skills.end()?1000:it->second;}
inline int64_t colonyHour(const ColonyConfig& c,int64_t tick){return (tick/c.ticksPerHour)%kColonyHours;}
inline int64_t colonyDay(const ColonyConfig& c,int64_t tick){return tick/(c.ticksPerHour*kColonyHours);}
inline int64_t colonyStock(const ColonyStockpile& s,int64_t resource){auto it=s.stock.find(resource);return it==s.stock.end()?0:it->second;}
inline int64_t colonyReserved(const ColonyStockpile& s,int64_t resource){
    int64_t n=0;for(const auto& [t,r]:s.reserved.tickets){(void)t;if(auto it=r.amounts.find(resource);it!=r.amounts.end())n+=it->second;}return n;
}
inline int64_t colonyAvailable(const ColonyStockpile& s,int64_t resource){return colonyStock(s,resource)-colonyReserved(s,resource);}
inline int64_t colonyTotal(const ColonyStockpile& s){int64_t n=0;for(auto [id,v]:s.stock){(void)id;n+=v;}return n;}
inline int64_t colonyStockAll(const ColonyState& s,int64_t resource){int64_t n=0;for(const auto& [id,p]:s.stockpiles){(void)id;n+=colonyStock(p,resource);}return n;}

inline bool valid(const ColonyColonist& c){
    if(c.id<=0||c.priorities.size()>kColonyMaxDefs||c.skills.size()>kColonyMaxDefs||c.needs.size()>kColonyMaxDefs)return false;
    for(auto [w,p]:c.priorities)if(w<=0||p<0||p>kColonyLowestPriority)return false;
    for(auto [w,s]:c.skills)if(w<=0||s<0||s>kColonySpeedLimit)return false;
    for(auto [n,v]:c.needs)if(n<=0||v<0||v>kColonyNeedMax)return false;
    for(auto b:c.schedule)if(uint8_t(b)>uint8_t(ColonyBlock::Leisure))return false;
    return c.mood>= -kColonyMoodLimit&&c.mood<=kColonyMoodLimit&&uint8_t(c.band)<=uint8_t(ColonyMoodBand::High)
        &&uint8_t(c.activity)<=uint8_t(ColonyActivity::Incapacitated)&&c.busyUntil>=0&&c.calmUntil>=0&&c.job>=0&&c.restoring>=0;
}
inline bool valid(const ColonyState& s){
    if(s.colonists.size()>kColonyMaxColonists||s.jobs.size()>kColonyMaxJobs||s.stockpiles.size()>kColonyMaxDefs||s.workstations.size()>kColonyMaxDefs||s.alerts.size()>kColonyMaxDefs)return false;
    if(s.nextJob<1||s.nextAlert<1||s.nextStructure<1||s.lastCommand<0||s.lastTick<0)return false;
    for(const auto& [id,c]:s.colonists)if(id!=c.id||!valid(c)||(c.job&&!s.jobs.contains(c.job)))return false;
    for(const auto& [id,p]:s.stockpiles){
        if(id<=0||p.id!=id||p.capacity<0||p.capacity>kColonyAmountLimit||!colonyQuantitiesValid(p.stock,true))return false;
        for(const auto& [t,r]:p.reserved.tickets)if(!s.jobs.contains(t)||r.owner<=0||!systems::validAmounts(r.amounts))return false;
    }
    for(const auto& [id,w]:s.workstations)if(id<=0||w.id!=id||w.type<=0||!s.stockpiles.contains(w.stockpile)||w.recipe<0||w.target<0||w.job<0)return false;
    for(const auto& [id,j]:s.jobs){
        if(id<=0||id>=s.nextJob||j.task.id!=id||uint8_t(j.kind)>uint8_t(ColonyJobKind::Response)||j.task.status==TaskStatus::Done)return false;
        if(j.work<1||j.work>kColonyAmountLimit||j.progress<0||j.progress>j.work*1000||j.amount<0||j.amount>kColonyAmountLimit)return false;
        if(j.task.status==TaskStatus::Active&&(!s.colonists.contains(j.task.worker)||s.colonists.at(j.task.worker).job!=id))return false;
        if(j.task.status==TaskStatus::Pending&&j.task.worker!=0)return false;
    }
    for(const auto& [id,a]:s.alerts)if(id<=0||id>=s.nextAlert||a.id!=id||a.kind<=0||a.severity<0||a.worked<0)return false;
    return true;
}
// Player/bot command shape checks (ownership and affordability are checked against state when applied).
inline bool colonyCommandValid(const ColonyCommand& c){
    switch(c.kind){
    case ColonyCommandKind::SetPriority:return c.colonist>0&&c.ref>0&&c.value>=0&&c.value<=kColonyLowestPriority;
    case ColonyCommandKind::SetSchedule:return c.colonist>0&&c.target>=0&&c.target<kColonyHours&&c.value>=0&&c.value<=int64_t(ColonyBlock::Leisure);
    case ColonyCommandKind::SetBill:return c.target>0&&c.ref>0&&c.value>=0&&c.value<=kColonyAmountLimit;
    case ColonyCommandKind::ClearBill:return c.target>0;
    case ColonyCommandKind::OrderBuild:return c.ref>0&&c.stockpile>0;
    case ColonyCommandKind::OrderGather:return c.ref>0&&c.target>0&&c.stockpile>0;
    case ColonyCommandKind::CancelJob:return c.target>0;
    }
    return false;
}
}
