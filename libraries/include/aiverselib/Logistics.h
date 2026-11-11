#pragma once
#include <cstdint>
#include <cstddef>
#include <limits>
#include <span>
#include <vector>
namespace aiverse::lib {
struct Ingredient { std::size_t resource; std::int64_t amount; };
// Validate and aggregate before mutation, including duplicate ingredients.
inline bool consume(std::span<std::int64_t> stock,std::span<const Ingredient> recipe) {
    std::vector<std::int64_t> needed(stock.size());
    for (const auto& r:recipe) {
        if (r.resource>=stock.size() || r.amount<0 || r.amount>std::numeric_limits<std::int64_t>::max()-needed[r.resource]) return false;
        needed[r.resource]+=r.amount;
    }
    for (std::size_t i=0;i<stock.size();++i) if (needed[i]>0 && stock[i]<needed[i]) return false;
    for (std::size_t i=0;i<stock.size();++i) if (needed[i]>0) stock[i]-=needed[i];
    return true;
}
}
