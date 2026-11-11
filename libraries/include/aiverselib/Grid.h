#pragma once
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>
namespace aiverse::lib {
// Row-major, zero-based IDs. Four neighbors; -1 means blocked/unreachable.
inline std::optional<std::vector<std::int32_t>> gridDistance(
    std::size_t width,std::size_t height,std::span<const std::uint8_t> passable,
    std::span<const std::size_t> seeds) {
    if (!width || !height || width>4096 || height>4096 || width*height>262144 || passable.size()!=width*height) return std::nullopt;
    const auto count=width*height;
    for (auto seed:seeds) if (seed>=count) return std::nullopt;
    std::vector<std::int32_t> distance(count,-1); std::vector<std::size_t> queue;
    for (auto seed:seeds) if (passable[seed] && distance[seed]<0) { distance[seed]=0; queue.push_back(seed); }
    for (std::size_t head=0;head<queue.size();++head) {
        auto u=queue[head];
        auto visit=[&](std::size_t v){ if (passable[v] && distance[v]<0) { distance[v]=distance[u]+1; queue.push_back(v); } };
        if (u%width>0) visit(u-1);
        if (u%width+1<width) visit(u+1);
        if (u>=width) visit(u-width);
        if (u<count-width) visit(u+width);
    }
    return distance;
}
}
