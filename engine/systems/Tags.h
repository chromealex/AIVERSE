#pragma once
#include <cstdint>
#include <optional>
#include <span>
namespace aiverse::lib::systems {
struct TagQuery {std::uint64_t all=0,any=0,none=0;};
inline bool matches(std::uint64_t tags,TagQuery q){return (tags&q.all)==q.all&&(!q.any||(tags&q.any))&&!(tags&q.none);}
// Resolver returns optional mask; unknown names must not silently turn into unconstrained filters.
template<class Name,class Resolve>
std::optional<std::uint64_t> resolveTags(std::span<const Name> names,Resolve resolve){
    std::uint64_t mask=0;for(const auto& name:names){auto found=resolve(name);if(!found||!*found)return {};mask|=*found;}return mask;
}
struct Condition {std::int64_t key=0,minimum=0,maximum=0;};
template<class Read>
bool conditions(std::span<const Condition> rows,Read read){
    for(const auto& row:rows){if(row.minimum>row.maximum)return false;auto v=read(row.key);if(!v||*v<row.minimum||*v>row.maximum)return false;}return true;
}
}
