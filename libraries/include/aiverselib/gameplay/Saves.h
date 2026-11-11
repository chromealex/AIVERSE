#pragma once
#include "Diagnostics.h"
#include <cstdint>
#include <functional>
#include <span>
namespace aiverse::lib::gameplay {
template<class State> struct Migration { std::int64_t from=0,to=0;std::function<bool(State&)> apply; };
// Run at load/import, before installing a state. Failure never modifies the live state.
// Engine nativeModules VERSION compatibility is separate; this does not bypass it.
template<class State>
Decision migrate(State& state,std::int64_t& version,std::int64_t target,std::span<const Migration<State>> migrations) {
    if(version<0||target<version)return {Reason::Version,0,target,version};
    auto next=state;auto current=version;
    while(current<target){const Migration<State>* step=nullptr;
        for(const auto& m:migrations)if(m.from==current){if(step)return {Reason::Invalid,current};step=&m;}
        if(!step||step->to<=current||step->to>target||!step->apply)return {Reason::Version,0,target,current};
        if(!step->apply(next))return {Reason::Invalid,current};current=step->to;
    }
    state=std::move(next);version=current;return {};
}
}
