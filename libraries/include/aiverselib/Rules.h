#pragma once
#include <algorithm>
#include <span>
namespace aiverse::lib {
// Pure predicates in declared order, with short-circuit evaluation.
template<class Predicate,class Context>
bool all(std::span<const Predicate> predicates,const Context& context) {
    return std::all_of(predicates.begin(),predicates.end(),[&](const auto& p){return p(context);});
}
template<class Predicate,class Context>
bool any(std::span<const Predicate> predicates,const Context& context) {
    return std::any_of(predicates.begin(),predicates.end(),[&](const auto& p){return p(context);});
}
}
