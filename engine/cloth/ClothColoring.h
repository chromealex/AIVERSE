#pragma once
#include "Cloth.h"
#include <array>
#include <bit>
#include <stdexcept>
namespace aiverse::cloth {
// Edges within a color never share a particle: GPU writes need no float atomics.
struct ColoredGrid {
    std::vector<Edge> edges;
    std::vector<size_t> offsets{0};
    explicit ColoredGrid(const Grid& grid){
        std::vector<uint32_t> used(grid.rest.size());std::array<std::vector<Edge>,32> colors;
        for(auto e:grid.edges){uint32_t free=~(used.at(e.a)|used.at(e.b));
            if(!free)throw std::runtime_error("cloth constraint coloring exceeds 32 colors");
            unsigned c=std::countr_zero(free);used[e.a]|=1u<<c;used[e.b]|=1u<<c;colors[c].push_back(e);
        }
        for(auto& color:colors)if(!color.empty()){edges.insert(edges.end(),color.begin(),color.end());offsets.push_back(edges.size());}
    }
};
}
