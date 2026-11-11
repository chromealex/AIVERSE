#pragma once
#include <cstdint>
#include <cstddef>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib {
struct Goal { std::int64_t id,event,target; };
// Progress indexed like goals. Invalid input leaves progress unchanged.
inline std::optional<std::vector<std::int64_t>> objectiveEvent(
    std::span<std::int64_t> progress,std::span<const Goal> goals,std::int64_t event) {
    if (progress.size()!=goals.size()) return std::nullopt;
    for (std::size_t i=0;i<goals.size();++i) if (goals[i].target<=0 || progress[i]<0) return std::nullopt;
    std::vector<std::int64_t> completed;
    for (std::size_t i=0;i<goals.size();++i) if (goals[i].event==event && progress[i]<goals[i].target) {
        if (++progress[i]==goals[i].target) completed.push_back(goals[i].id);
    }
    return completed;
}
}
