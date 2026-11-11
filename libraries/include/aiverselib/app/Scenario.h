#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace aiverse::lib::app {
struct ScenarioStep { std::uint64_t tick=0; std::string action,payload; };
struct ScenarioCheck { std::uint64_t tick=0; std::string metric;std::int64_t minimum=0,maximum=0; };
struct Scenario { std::vector<ScenarioStep> steps; std::vector<ScenarioCheck> checks; };
inline bool validScenario(const Scenario& s) {
    std::uint64_t previous=0;
    for(const auto& x:s.steps){if(x.action.empty()||x.tick<previous)return false;previous=x.tick;}
    previous=0;
    for(const auto& x:s.checks){if(x.metric.empty()||x.minimum>x.maximum||x.tick<previous)return false;previous=x.tick;}
    return true;
}
struct ScenarioRunner {
    std::size_t step=0,check=0;
    std::uint64_t lastTick=0;
    bool started=false,failed=false;
    std::string error;
    // Call at each exact sim tick. Host emits normal Commands; metric callback returns optional<int64_t>.
    template<class Emit,class Read>
    bool advance(const Scenario& s,std::uint64_t tick,Emit emit,Read read) {
        if(failed)return false;
        if(!validScenario(s)||(started&&tick<=lastTick)){failed=true;error="invalid scenario or tick order";return false;}
        lastTick=tick;started=true;
        while(step<s.steps.size()&&s.steps[step].tick<=tick){
            if(s.steps[step].tick!=tick||!emit(s.steps[step])){failed=true;error="missed or rejected action";return false;}++step;
        }
        while(check<s.checks.size()&&s.checks[check].tick<=tick){
            const auto& c=s.checks[check];auto value=read(c.metric);
            if(c.tick!=tick||!value||*value<c.minimum||*value>c.maximum){failed=true;error="assertion failed: "+c.metric;return false;}++check;
        }
        return true;
    }
    bool complete(const Scenario& s)const{return !failed&&step==s.steps.size()&&check==s.checks.size();}
};
}
