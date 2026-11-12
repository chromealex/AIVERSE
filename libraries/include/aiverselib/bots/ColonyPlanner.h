#pragma once
// Colony / survival planner bot (settlement-production): decides what to build, which bills to run, what to
// gather and which work to prioritise from the colony's needs and stock. It never writes simulation state: it
// returns genre::ColonyCommand values, the same commands a player issues, which the owner passes to stepColony
// (validated there like any player command). Bounded (maxCommands per plan, one plan per intervalTicks) and
// deterministic: rules are evaluated in (priority desc, id asc) order, ties pick the lowest id. No RNG.
// State (ColonyPlannerState) is genre::save/load record kind 110.
#include "../genre/ColonyRunner.h"
#include "../genre/Persistence.h"
#include "engine/core/Hash.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::bots {
enum class ColonyPlanMetric:uint8_t {
    StockPerColonist, // ref = resource, value = stock across stockpiles * 1000 / colonists
    Stock,            // ref = resource
    PowerDeficit,     // facts.powerDeficit (demand not served, units per tick)
    ColdColonists,    // facts.cold
    NeedLow,          // ref = need: colonists at or below the need's lowAt
    Structures,       // ref = workstation type: count
    OpenJobs,         // ref = ColonyJobKind value: open jobs of that kind
    Alerts,           // active alerts
    Colonists,
    Custom            // ref = facts.custom key
};
struct ColonyPlanCondition {ColonyPlanMetric metric=ColonyPlanMetric::Stock;int64_t ref=0;bool below=true;int64_t threshold=0;}; // below: value < threshold, else value >= threshold
enum class ColonyPlanAction:uint8_t {Build,Bill,Gather,Priority};
struct ColonyPlanRule {
    int64_t id=0,priority=0;std::vector<ColonyPlanCondition> when; // all conditions must hold
    ColonyPlanAction action=ColonyPlanAction::Build;
    int64_t ref=0;          // blueprint / recipe / gather def / work type
    int64_t value=0;        // Bill: stock target (0 = repeat); Priority: work priority 0..4
    int64_t stockpile=0;    // Build / Gather stockpile
    int64_t maxOpen=1;      // Build / Gather: open jobs of this ref the rule keeps at most
    int64_t cooldownTicks=0;
};
struct ColonyPlannerConfig {int64_t intervalTicks=60,maxCommands=4;std::vector<ColonyPlanRule> rules;};
struct ColonyGatherOption {int64_t gather=0,node=0,available=0;};
// Owner-supplied facts the colony state does not contain (power, survival bodies, renewal nodes).
struct ColonyPlannerFacts {int64_t powerDeficit=0,cold=0;std::vector<ColonyGatherOption> gatherNodes;std::map<int64_t,int64_t> custom;};
struct ColonyPlannerState {int64_t lastPlan=0,nextSeq=1,lastTick=0;std::map<int64_t,int64_t> readyAt;bool planned=false,initialized=false;};
struct ColonyPlanReport {std::vector<genre::ColonyCommand> commands;std::vector<int64_t> fired;}; // fired: rule ids

inline std::string colonyPlannerError(const ColonyPlannerConfig& c,const genre::ColonyDefs& d){
    if(c.intervalTicks<1||c.intervalTicks>10000000)return "planner.intervalTicks: 1..10000000";
    if(c.maxCommands<1||c.maxCommands>256)return "planner.maxCommands: 1..256";
    if(c.rules.size()>1024)return "planner.rules: at most 1024";
    std::vector<int64_t> ids;
    for(const auto& r:c.rules){
        auto p="planner.rule "+std::to_string(r.id);ids.push_back(r.id);
        if(r.id<=0||r.when.size()>16||r.maxOpen<1||r.maxOpen>1024||r.cooldownTicks<0||r.cooldownTicks>100000000||r.priority< -1000000||r.priority>1000000)return p+": invalid id/limits";
        for(const auto& w:r.when)if(uint8_t(w.metric)>uint8_t(ColonyPlanMetric::Custom))return p+": unknown metric";
        switch(r.action){
        case ColonyPlanAction::Build:if(!d.blueprints.contains(r.ref)||r.stockpile<=0)return p+": unknown blueprint or missing stockpile";break;
        case ColonyPlanAction::Bill:if(!d.recipes.contains(r.ref)||r.value<0)return p+": unknown recipe";break;
        case ColonyPlanAction::Gather:if(!d.gathers.contains(r.ref)||r.stockpile<=0)return p+": unknown gather or missing stockpile";break;
        case ColonyPlanAction::Priority:if(!d.workTypes.contains(r.ref)||r.value<0||r.value>genre::kColonyLowestPriority)return p+": unknown work type or priority";break;
        default:return p+": unknown action";
        }
    }
    std::sort(ids.begin(),ids.end());if(std::adjacent_find(ids.begin(),ids.end())!=ids.end())return "planner.rules: duplicate id";
    return {};
}
inline bool valid(const ColonyPlannerState& s){
    if(s.nextSeq<1||s.lastPlan<0||s.lastTick<0||s.readyAt.size()>1024)return false;
    for(auto [id,t]:s.readyAt)if(id<=0||t<0)return false;
    return true;
}
inline int64_t colonyPlanMetric(const genre::ColonyDefs& d,const genre::ColonyState& s,const ColonyPlannerFacts& f,const ColonyPlanCondition& c){
    using namespace genre;
    switch(c.metric){
    case ColonyPlanMetric::StockPerColonist:return colonyStockAll(s,c.ref)*1000/std::max<int64_t>(1,int64_t(s.colonists.size()));
    case ColonyPlanMetric::Stock:return colonyStockAll(s,c.ref);
    case ColonyPlanMetric::PowerDeficit:return f.powerDeficit;
    case ColonyPlanMetric::ColdColonists:return f.cold;
    case ColonyPlanMetric::NeedLow:{auto n=d.needs.find(c.ref);if(n==d.needs.end())return 0;int64_t k=0;
        for(const auto& [id,col]:s.colonists){(void)id;auto v=col.needs.find(c.ref);if(v!=col.needs.end()&&v->second<=n->second.lowAt)++k;}return k;}
    case ColonyPlanMetric::Structures:{int64_t k=0;for(const auto& [id,w]:s.workstations){(void)id;if(w.type==c.ref)++k;}return k;}
    case ColonyPlanMetric::OpenJobs:{int64_t k=0;for(const auto& [id,j]:s.jobs){(void)id;if(int64_t(j.kind)==c.ref)++k;}return k;}
    case ColonyPlanMetric::Alerts:return int64_t(s.alerts.size());
    case ColonyPlanMetric::Colonists:return int64_t(s.colonists.size());
    case ColonyPlanMetric::Custom:{auto it=f.custom.find(c.ref);return it==f.custom.end()?0:it->second;}
    }
    return 0;
}
namespace detail_colony_planner {
inline int64_t openJobs(const genre::ColonyState& s,genre::ColonyJobKind kind,int64_t ref){int64_t k=0;for(const auto& [id,j]:s.jobs){(void)id;if(j.kind==kind&&j.ref==ref)++k;}return k;}
inline bool affordable(const genre::ColonyState& s,int64_t stockpile,const systems::Quantities& cost){
    auto p=s.stockpiles.find(stockpile);if(p==s.stockpiles.end())return false;
    for(auto [res,n]:cost)if(genre::colonyAvailable(p->second,res)<n)return false;
    return true;
}
}
// Plans at most maxCommands commands once per intervalTicks; strictly increasing ticks (nullopt otherwise).
inline std::optional<ColonyPlanReport> planColony(const ColonyPlannerConfig& c,const genre::ColonyDefs& d,const genre::ColonyState& colony,
                                                  const ColonyPlannerFacts& facts,ColonyPlannerState& state,int64_t tick){
    using namespace genre;using namespace detail_colony_planner;
    if(tick<0||(state.initialized&&tick<=state.lastTick)||!valid(state)||!colonyPlannerError(c,d).empty())return std::nullopt;
    auto s=state;s.lastTick=tick;s.initialized=true;ColonyPlanReport rep;
    if(s.planned&&tick<s.lastPlan+c.intervalTicks){state=std::move(s);return rep;}
    s.planned=true;s.lastPlan=tick;
    std::vector<const ColonyPlanRule*> order;for(const auto& r:c.rules)order.push_back(&r);
    std::stable_sort(order.begin(),order.end(),[](auto* a,auto* b){return a->priority!=b->priority?a->priority>b->priority:a->id<b->id;});
    auto push=[&](ColonyCommand cmd){cmd.seq=s.nextSeq++;rep.commands.push_back(cmd);};
    for(const auto* r:order){
        if(int64_t(rep.commands.size())>=c.maxCommands)break;
        if(auto it=s.readyAt.find(r->id);it!=s.readyAt.end()&&tick<it->second)continue;
        bool holds=true;for(const auto& w:r->when){auto v=colonyPlanMetric(d,colony,facts,w);if(w.below?!(v<w.threshold):!(v>=w.threshold)){holds=false;break;}}
        if(!holds)continue;
        const auto before=rep.commands.size();
        switch(r->action){
        case ColonyPlanAction::Build:
            if(openJobs(colony,ColonyJobKind::Build,r->ref)<r->maxOpen&&affordable(colony,r->stockpile,d.blueprints.at(r->ref).cost))
                push({0,ColonyCommandKind::OrderBuild,0,0,r->ref,0,r->stockpile});
            break;
        case ColonyPlanAction::Bill:{
            const auto type=d.recipes.at(r->ref).workstationType;const ColonyWorkstation* pick=nullptr;
            for(const auto& [id,w]:colony.workstations){(void)id;if(w.type!=type)continue;if(w.recipe==r->ref&&w.target==r->value){pick=nullptr;break;}if(!pick&&(w.recipe==0||w.recipe==r->ref))pick=&w;}
            if(pick)push({0,ColonyCommandKind::SetBill,0,pick->id,r->ref,r->value,0});
            break;}
        case ColonyPlanAction::Gather:{
            if(openJobs(colony,ColonyJobKind::Gather,r->ref)>=r->maxOpen)break;
            const ColonyGatherOption* best=nullptr;
            for(const auto& o:facts.gatherNodes)if(o.gather==r->ref&&o.available>0&&(!best||o.available>best->available||(o.available==best->available&&o.node<best->node)))best=&o;
            if(best)push({0,ColonyCommandKind::OrderGather,0,best->node,r->ref,0,r->stockpile});
            break;}
        case ColonyPlanAction::Priority:
            for(const auto& [id,col]:colony.colonists){
                if(int64_t(rep.commands.size())>=c.maxCommands)break;
                if(colonyPriority(d,col,r->ref)!=r->value)push({0,ColonyCommandKind::SetPriority,id,0,r->ref,r->value,0});
            }
            break;
        }
        if(rep.commands.size()>before){rep.fired.push_back(r->id);if(r->cooldownTicks)s.readyAt[r->id]=tick+r->cooldownTicks;}
    }
    state=std::move(s);return rep;
}
// Persistence (genre::save/load record kind 110) and checksum hashing.
inline void io(Archive& a,ColonyPlannerState& s){a(s.lastPlan,s.nextSeq,s.lastTick,s.readyAt,s.planned,s.initialized);}
inline uint16_t stateKind(const ColonyPlannerState&){return 110;}
inline uint64_t colonyPlannerHash(const ColonyPlannerState& s){std::string bytes;Archive a(&bytes);auto copy=s;uint16_t kind=110;a(kind,copy);Hasher h;h.str(bytes);return h.value();}
}
