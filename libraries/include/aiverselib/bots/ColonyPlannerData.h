#pragma once
// JSON for the colony planner bot (example: examples/libraries/genre/production-survival/planner.json):
// {"intervalTicks":100,"maxCommands":4,"rules":[{"id":1,"priority":50,"action":"bill","ref":1,"value":30,
//   "when":[{"metric":"stockPerColonist","ref":3,"below":true,"threshold":3000}],"cooldownTicks":0,"maxOpen":1,"stockpile":1}]}
// actions: build | bill | gather | priority; metrics: stockPerColonist | stock | powerDeficit | coldColonists | needLow |
// structures | openJobs | alerts | colonists | custom. Errors: JsonError("planner.<path>: ...").
#include "ColonyPlanner.h"
#include "../genre/SettlementJson.h"
namespace aiverse::lib::bots {
namespace detail_colony_planner_data {
using namespace genre::detail_settlement_json;
inline constexpr std::array<std::pair<std::string_view,ColonyPlanAction>,4> kActions{{{"build",ColonyPlanAction::Build},{"bill",ColonyPlanAction::Bill},{"gather",ColonyPlanAction::Gather},{"priority",ColonyPlanAction::Priority}}};
inline constexpr std::array<std::pair<std::string_view,ColonyPlanMetric>,10> kMetrics{{{"stockPerColonist",ColonyPlanMetric::StockPerColonist},{"stock",ColonyPlanMetric::Stock},
    {"powerDeficit",ColonyPlanMetric::PowerDeficit},{"coldColonists",ColonyPlanMetric::ColdColonists},{"needLow",ColonyPlanMetric::NeedLow},{"structures",ColonyPlanMetric::Structures},
    {"openJobs",ColonyPlanMetric::OpenJobs},{"alerts",ColonyPlanMetric::Alerts},{"colonists",ColonyPlanMetric::Colonists},{"custom",ColonyPlanMetric::Custom}}};
}
inline ColonyPlannerConfig readColonyPlanner(const Json& j,const genre::ColonyDefs& defs){
    using namespace detail_colony_planner_data;const std::string root="planner";
    keys(j,root,{"intervalTicks","maxCommands","rules"});ColonyPlannerConfig c;
    c.intervalTicks=optional(j,"intervalTicks",root,1,10000000,60);c.maxCommands=optional(j,"maxCommands",root,1,256,4);
    const auto& rules=list(j,"rules",root,1024);
    for(std::size_t i=0;i<rules.size();++i){
        const auto p=at(root+".rules",i);const auto& r=rules[i];keys(r,p,{"id","priority","when","action","ref","value","stockpile","maxOpen","cooldownTicks"});
        ColonyPlanRule rule;rule.id=required(r,"id",p,1,INT64_MAX);rule.priority=optional(r,"priority",p,-1000000,1000000,0);
        if(!r.has("action"))throw JsonError(p+".action: required");
        rule.action=choice(r["action"],p+".action",kActions);rule.ref=required(r,"ref",p,1,INT64_MAX);rule.value=optional(r,"value",p,0,genre::kColonyAmountLimit,0);
        rule.stockpile=optional(r,"stockpile",p,0,INT64_MAX,0);rule.maxOpen=optional(r,"maxOpen",p,1,1024,1);rule.cooldownTicks=optional(r,"cooldownTicks",p,0,100000000,0);
        const auto& when=list(r,"when",p,16);
        for(std::size_t k=0;k<when.size();++k){
            const auto q=at(p+".when",k);keys(when[k],q,{"metric","ref","below","threshold"});
            if(!when[k].has("metric"))throw JsonError(q+".metric: required");
            rule.when.push_back({choice(when[k]["metric"],q+".metric",kMetrics),optional(when[k],"ref",q,0,INT64_MAX,0),flag(when[k],"below",q,true),required(when[k],"threshold",q,INT64_MIN/2,INT64_MAX/2)});
        }
        c.rules.push_back(rule);
    }
    if(auto e=colonyPlannerError(c,defs);!e.empty())throw JsonError(e);
    return c;
}
}
