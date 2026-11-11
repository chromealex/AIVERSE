#pragma once
#include <cstdint>
#include <optional>
#include <span>
#include <type_traits>
#include <utility>
namespace aiverse::lib {
struct Transition { std::int64_t from,event,to; };
// Definitions must have unique (from,event); declared first match wins.
inline bool transition(std::int64_t& current,std::span<const Transition> definitions,std::int64_t event) {
    for (const auto& t:definitions) if (t.from==current && t.event==event) { current=t.to; return true; }
    return false;
}
template<class Score>
std::optional<std::int64_t> choose(std::span<const std::int64_t> candidates,Score score) {
    using Value=std::remove_cvref_t<decltype(score(std::int64_t{}))>;
    static_assert(!std::is_floating_point_v<Value>,"simulation scores must be integer or Fixed");
    std::optional<std::int64_t> best; std::optional<Value> bestScore;
    for (auto id:candidates) {
        auto value=score(id);
        if (!best || value>*bestScore || (value==*bestScore && id<*best)) { best=id; bestScore=value; }
    }
    return best;
}
}
