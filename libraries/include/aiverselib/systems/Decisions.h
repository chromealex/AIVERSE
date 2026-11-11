#pragma once
#include "../gameplay/Diagnostics.h"
#include <span>
#include <string>
#include <vector>
namespace aiverse::lib::systems {
struct DecisionFact {std::string key;std::int64_t subject=0,required=0,actual=0;gameplay::Reason reason=gameplay::Reason::Ok;};
struct DecisionReport {gameplay::Decision result;std::vector<DecisionFact> facts;bool truncated=false;};
// Facts must come from the authoritative calculation; this helper never reconstructs an approximate formula.
inline DecisionReport explain(std::span<const DecisionFact> facts,std::size_t maximum=32){
    DecisionReport report;
    for(const auto& fact:facts){
        if(report.result&&fact.reason!=gameplay::Reason::Ok)report.result={fact.reason,fact.subject,fact.required,fact.actual};
        if(report.facts.size()<maximum)report.facts.push_back(fact);else report.truncated=true;
    }
    return report;
}
}
