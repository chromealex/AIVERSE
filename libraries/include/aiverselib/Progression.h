#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <limits>
#include <type_traits>
#include <utility>
namespace aiverse::lib {
struct Weighted { std::int64_t id,weight; };
// draw(total) supplies simulation RNG, integer in [1,total]; zero total is empty.
template<class Draw>
std::optional<std::int64_t> weighted(std::span<const Weighted> rows,Draw draw) {
    std::int64_t total=0;
    for (const auto& r:rows) {
        if (r.weight<0 || r.weight>std::numeric_limits<std::int64_t>::max()-total) return std::nullopt;
        total+=r.weight;
    }
    if (!total) return std::nullopt;
    const auto sampled=draw(total);
    static_assert(std::is_integral_v<decltype(sampled)>,"draw must return an integer");
    if (sampled<1 || std::cmp_greater(sampled,total)) return std::nullopt;
    auto n=static_cast<std::int64_t>(sampled);
    for (const auto& r:rows) { if (n<=r.weight) return r.id; n-=r.weight; }
    return std::nullopt;
}
inline std::optional<std::size_t> level(std::int64_t xp,std::span<const std::int64_t> thresholds) {
    if (xp<0) return std::nullopt;
    std::int64_t previous=-1; std::size_t result=0;
    for (auto value:thresholds) { if (value<=previous) return std::nullopt; previous=value; if (xp>=value) ++result; }
    return result;
}
}
