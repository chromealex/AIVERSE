#pragma once
// JSON for settlement-production (example: examples/libraries/genre/production-survival/colony.json):
// {"restNeed":2,
//  "needs":[{"id":1,"decayPerTick":30,"initial":80000,"lowAt":35000,"criticalAt":10000,"recoveredAt":90000,"mood":[{"below":10000,"mood":-40000}]}],
//  "restores":[{"activity":"sleeping","need":2,"perTick":120}],          activity: sleeping | leisure | working
//  "meals":[{"resource":3,"need":1,"amount":50000,"ticks":20}],
//  "workTypes":[{"id":1,"defaultPriority":3}],
//  "recipes":[{"id":1,"workstationType":1,"workType":2,"work":80,"inputs":[[1,2]],"outputs":[[3,1]]}],
//  "blueprints":[{"id":1,"workType":3,"work":300,"workstationType":2,"needsPower":true,"cost":[[2,20]]}],
//  "gathers":[{"id":1,"workType":4,"resource":1,"yield":5,"work":60}],
//  "hauls":[{"id":1,"from":1,"to":2,"resource":3,"keep":10,"batch":5,"work":30,"workType":5}],
//  "alerts":[{"id":1,"workType":6,"priority":1000,"workPerSeverity":40,"mood":-5000,"preempt":true,"ignoreSchedule":true}]}
// Power grid: {"restoreDelayTicks":50,"nodes":[{"id":1,"medium":1,"kind":"producer","output":100}, {"id":2,"kind":"consumer","demand":40,"priority":5},
//   {"id":3,"kind":"storage","capacity":5000,"rate":50,"charge":0}, {"id":4,"kind":"conduit"}],"links":[[1,4],[4,2]]}
// Config (tick rate, mood bands, speeds) comes from the optional `colony` settings section (ColonyConfigured.h).
// Errors: JsonError("colony.<path>: ...") with index paths for syntax and id paths for references.
#include "ColonyPower.h"
#include "ColonyTypes.h"
#include "SettlementJson.h"
namespace aiverse::lib::genre {
namespace detail_colony_data {
using namespace detail_settlement_json;
inline constexpr std::array<std::pair<std::string_view,ColonyActivity>,3> kActivities{{{"sleeping",ColonyActivity::Sleeping},{"leisure",ColonyActivity::Leisure},{"working",ColonyActivity::Working}}};
inline constexpr std::array<std::pair<std::string_view,PowerNodeKind>,4> kPowerKinds{{{"producer",PowerNodeKind::Producer},{"consumer",PowerNodeKind::Consumer},{"storage",PowerNodeKind::Storage},{"conduit",PowerNodeKind::Conduit}}};
constexpr int64_t kAmount=kColonyAmountLimit,kNeed=kColonyNeedMax,kMood=kColonyMoodLimit;
inline void need(const ColonyDefs& d,int64_t id,const std::string& path){if(!d.needs.contains(id))throw JsonError(path+": unknown need "+std::to_string(id));}
inline void work(const ColonyDefs& d,int64_t id,const std::string& path){if(!d.workTypes.contains(id))throw JsonError(path+": unknown work type "+std::to_string(id));}
}
inline ColonyDefs readColonyDefinitions(const Json& j,const ColonyConfig& config={}){
    using namespace detail_colony_data;const std::string root="colony";
    keys(j,root,{"restNeed","needs","restores","meals","workTypes","recipes","blueprints","gathers","hauls","alerts"});
    ColonyDefs d;d.config=config;
    const auto& needs=list(j,"needs",root,kColonyMaxDefs);
    for(std::size_t i=0;i<needs.size();++i){
        const auto p=at(root+".needs",i);const auto& n=needs[i];keys(n,p,{"id","decayPerTick","initial","lowAt","criticalAt","recoveredAt","mood"});
        ColonyNeedDef def;def.id=required(n,"id",p,1,INT64_MAX);def.decayPerTick=required(n,"decayPerTick",p,0,kNeed);def.initial=optional(n,"initial",p,0,kNeed,kNeed);
        def.lowAt=required(n,"lowAt",p,0,kNeed);def.criticalAt=required(n,"criticalAt",p,0,kNeed);def.recoveredAt=optional(n,"recoveredAt",p,0,kNeed,kNeed);
        if(!(def.criticalAt<=def.lowAt&&def.lowAt<=def.recoveredAt))throw JsonError(p+": criticalAt <= lowAt <= recoveredAt required");
        const auto& mood=list(n,"mood",p,32);int64_t prev=-1;
        for(std::size_t k=0;k<mood.size();++k){
            const auto q=at(p+".mood",k);keys(mood[k],q,{"below","mood"});
            ColonyMoodStep st{required(mood[k],"below",q,0,kNeed+1),required(mood[k],"mood",q,-kMood,kMood)};
            if(st.below<=prev)throw JsonError(q+".below: must be ascending");
            prev=st.below;def.mood.push_back(st);
        }
        insertUnique(d.needs,def.id,def,p);
    }
    d.restNeed=optional(j,"restNeed",root,0,INT64_MAX,0);
    if(d.restNeed)need(d,d.restNeed,root+".restNeed");
    const auto& types=list(j,"workTypes",root,kColonyMaxDefs);
    for(std::size_t i=0;i<types.size();++i){
        const auto p=at(root+".workTypes",i);keys(types[i],p,{"id","defaultPriority"});
        ColonyWorkTypeDef w{required(types[i],"id",p,1,INT64_MAX),int(optional(types[i],"defaultPriority",p,0,kColonyLowestPriority,3))};insertUnique(d.workTypes,w.id,w,p);
    }
    const auto& restores=list(j,"restores",root,256);
    for(std::size_t i=0;i<restores.size();++i){
        const auto p=at(root+".restores",i);keys(restores[i],p,{"activity","need","perTick"});
        if(!restores[i].has("activity"))throw JsonError(p+".activity: required");
        ColonyRestoreDef r{choice(restores[i]["activity"],p+".activity",kActivities),required(restores[i],"need",p,1,INT64_MAX),required(restores[i],"perTick",p,1,kNeed)};
        need(d,r.need,p+".need");d.restores.push_back(r);
    }
    const auto& meals=list(j,"meals",root,256);
    for(std::size_t i=0;i<meals.size();++i){
        const auto p=at(root+".meals",i);keys(meals[i],p,{"resource","need","amount","ticks"});
        ColonyMealDef m{required(meals[i],"resource",p,1,INT64_MAX),required(meals[i],"need",p,1,INT64_MAX),required(meals[i],"amount",p,1,kNeed),optional(meals[i],"ticks",p,1,1000000,1)};
        need(d,m.need,p+".need");d.meals.push_back(m);
    }
    const auto& recipes=list(j,"recipes",root,kColonyMaxDefs);
    for(std::size_t i=0;i<recipes.size();++i){
        const auto p=at(root+".recipes",i);const auto& r=recipes[i];keys(r,p,{"id","workstationType","workType","work","inputs","outputs"});
        ColonyRecipeDef def;def.id=required(r,"id",p,1,INT64_MAX);def.workstationType=required(r,"workstationType",p,1,INT64_MAX);def.workType=required(r,"workType",p,1,INT64_MAX);
        def.work=required(r,"work",p,1,kAmount);def.inputs=pairs(r,"inputs",p,kAmount);def.outputs=pairs(r,"outputs",p,kAmount);
        if(def.outputs.empty())throw JsonError(p+".outputs: at least one output");
        work(d,def.workType,"colony.recipe "+std::to_string(def.id)+".workType");insertUnique(d.recipes,def.id,def,p);
    }
    const auto& blueprints=list(j,"blueprints",root,kColonyMaxDefs);
    for(std::size_t i=0;i<blueprints.size();++i){
        const auto p=at(root+".blueprints",i);const auto& b=blueprints[i];keys(b,p,{"id","workType","work","workstationType","needsPower","cost"});
        ColonyBlueprintDef def;def.id=required(b,"id",p,1,INT64_MAX);def.workType=required(b,"workType",p,1,INT64_MAX);def.work=required(b,"work",p,1,kAmount);
        def.workstationType=optional(b,"workstationType",p,0,INT64_MAX,0);def.needsPower=flag(b,"needsPower",p,false);def.cost=pairs(b,"cost",p,kAmount);
        work(d,def.workType,"colony.blueprint "+std::to_string(def.id)+".workType");insertUnique(d.blueprints,def.id,def,p);
    }
    const auto& gathers=list(j,"gathers",root,kColonyMaxDefs);
    for(std::size_t i=0;i<gathers.size();++i){
        const auto p=at(root+".gathers",i);const auto& g=gathers[i];keys(g,p,{"id","workType","resource","yield","work"});
        ColonyGatherDef def{required(g,"id",p,1,INT64_MAX),required(g,"workType",p,1,INT64_MAX),required(g,"resource",p,1,INT64_MAX),required(g,"yield",p,1,kAmount),required(g,"work",p,1,kAmount)};
        work(d,def.workType,"colony.gather "+std::to_string(def.id)+".workType");insertUnique(d.gathers,def.id,def,p);
    }
    const auto& hauls=list(j,"hauls",root,kColonyMaxDefs);
    for(std::size_t i=0;i<hauls.size();++i){
        const auto p=at(root+".hauls",i);const auto& h=hauls[i];keys(h,p,{"id","from","to","resource","keep","batch","work","workType"});
        ColonyHaulDef def{required(h,"id",p,1,INT64_MAX),required(h,"from",p,1,INT64_MAX),required(h,"to",p,1,INT64_MAX),required(h,"resource",p,1,INT64_MAX),
            required(h,"keep",p,1,kAmount),required(h,"batch",p,1,kAmount),required(h,"work",p,1,kAmount),required(h,"workType",p,1,INT64_MAX)};
        if(def.from==def.to)throw JsonError(p+".to: must differ from 'from'");
        work(d,def.workType,"colony.haul "+std::to_string(def.id)+".workType");insertUnique(d.hauls,def.id,def,p);
    }
    const auto& alerts=list(j,"alerts",root,kColonyMaxDefs);
    for(std::size_t i=0;i<alerts.size();++i){
        const auto p=at(root+".alerts",i);const auto& a=alerts[i];keys(a,p,{"id","workType","priority","workPerSeverity","mood","preempt","ignoreSchedule"});
        ColonyAlertDef def;def.id=required(a,"id",p,1,INT64_MAX);def.workType=required(a,"workType",p,1,INT64_MAX);def.priority=optional(a,"priority",p,-1000000,1000000,100);
        def.workPerSeverity=required(a,"workPerSeverity",p,1,kAmount);def.mood=optional(a,"mood",p,-kMood,kMood,0);def.preempt=flag(a,"preempt",p,true);def.ignoreSchedule=flag(a,"ignoreSchedule",p,true);
        work(d,def.workType,"colony.alert "+std::to_string(def.id)+".workType");insertUnique(d.alerts,def.id,def,p);
    }
    if(auto e=colonyDefsError(d);!e.empty())throw JsonError("colony."+e);
    return d;
}
inline PowerGridState readPowerGrid(const Json& j){
    using namespace detail_colony_data;const std::string root="power";
    keys(j,root,{"nodes","links","restoreDelayTicks"});PowerGridState s;s.restoreDelayTicks=optional(j,"restoreDelayTicks",root,0,100000000,0);
    const auto& nodes=list(j,"nodes",root,kPowerMaxNodes);
    for(std::size_t i=0;i<nodes.size();++i){
        const auto p=at(root+".nodes",i);const auto& n=nodes[i];keys(n,p,{"id","medium","kind","output","demand","priority","capacity","rate","charge","enabled"});
        PowerNode node;node.id=required(n,"id",p,1,INT64_MAX);node.medium=optional(n,"medium",p,1,INT64_MAX,1);
        if(!n.has("kind"))throw JsonError(p+".kind: required");
        node.kind=choice(n["kind"],p+".kind",kPowerKinds);node.output=optional(n,"output",p,0,kPowerAmountLimit,0);node.demand=optional(n,"demand",p,0,kPowerAmountLimit,0);
        node.priority=optional(n,"priority",p,-1000000,1000000,0);node.capacity=optional(n,"capacity",p,0,kPowerAmountLimit,0);node.rate=optional(n,"rate",p,0,kPowerAmountLimit,0);
        node.charge=optional(n,"charge",p,0,node.capacity,0);node.enabled=flag(n,"enabled",p,true);
        if(!powerAddNode(s,node))throw JsonError(p+".id: duplicate or invalid node "+std::to_string(node.id));
    }
    const auto& links=list(j,"links",root,kPowerMaxLinks);
    for(std::size_t i=0;i<links.size();++i){
        const auto p=at(root+".links",i);
        if(!links[i].isArray()||links[i].size()!=2)throw JsonError(p+": expected [a, b]");
        auto a=integer(links[i][0],p+"[0]",1,INT64_MAX),b=integer(links[i][1],p+"[1]",1,INT64_MAX);
        if(!powerLink(s,a,b))throw JsonError(p+": unknown node, self link, medium mismatch or duplicate link");
    }
    return s;
}
}
