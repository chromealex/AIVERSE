#pragma once
// Shared pieces of the puzzle-solvers package: grid limits, the tri-state search result and the explicit node
// budget every solver takes. Cells are row-major (index = y*width + x, y grows downward); neighbours are visited
// in the fixed order left, right, up, down. Integer-only, no hidden randomness: callers pass aiverse::Random&.
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstdint>
#include <optional>
#include <vector>
namespace aiverse::lib::genre {
inline constexpr int32_t kPuzzleMaxSide=32;
inline constexpr int32_t kPuzzleMaxCells=1024;
inline constexpr uint64_t kPuzzleMaxBudget=2000000;
inline constexpr std::array<int32_t,4> kPuzzleDx{-1,1,0,0};
inline constexpr std::array<int32_t,4> kPuzzleDy{0,0,-1,1};
// Directions use the same numbering as grid-puzzles (strategy/Puzzles.h): 0 left, 1 right, 2 up, 3 down.
enum class PuzzleDir:uint8_t {Left,Right,Up,Down};
// Solved: a solution was found. Unsolvable: the search was exhaustive (a proof). Unknown: the node budget ran
// out first (never a proof either way). Invalid: inputs failed validation.
enum class PuzzleSearch:uint8_t {Solved,Unsolvable,Unknown,Invalid};
// Every solver/generator consumes at most `nodes` search nodes and reports how many it used.
struct PuzzleBudget {
    uint64_t nodes=20000,used=0;
    bool take(){if(used>=nodes)return false;++used;return true;}
    bool exhausted()const{return used>=nodes;}
};
inline bool puzzleValidBudget(const PuzzleBudget& b){return b.nodes>=1&&b.nodes<=kPuzzleMaxBudget&&b.used<=b.nodes;}
inline bool puzzleGridSize(int32_t width,int32_t height){return width>=1&&height>=1&&width<=kPuzzleMaxSide&&height<=kPuzzleMaxSide;}
inline bool puzzleInside(int32_t width,int32_t height,int32_t x,int32_t y){return x>=0&&y>=0&&x<width&&y<height;}
// Neighbour of `cell` in direction d (0..3), or -1 outside the grid. Never wraps across rows.
inline int32_t puzzleStep(int32_t width,int32_t height,int32_t cell,int32_t d){
    if(d<0||d>3||cell<0||cell>=width*height)return -1;
    const int32_t x=cell%width+kPuzzleDx[size_t(d)],y=cell/width+kPuzzleDy[size_t(d)];
    return puzzleInside(width,height,x,y)?y*width+x:-1;
}
inline int32_t puzzleOpposite(int32_t d){return d^1;}
inline int32_t puzzleManhattan(int32_t width,int32_t a,int32_t b){return std::abs(a%width-b%width)+std::abs(a/width-b/width);}
// Generic 4-connected flood fill from `start` over cells accepted by `same(cell)`; returns cells in BFS order
// (stable: queue order, neighbours left/right/up/down). Empty when start is outside or rejected.
template<class Same> std::vector<int32_t> puzzleFloodFill(int32_t width,int32_t height,int32_t start,Same&& same){
    std::vector<int32_t> out;if(!puzzleGridSize(width,height)||start<0||start>=width*height||!same(start))return out;
    std::vector<uint8_t> seen(size_t(width*height),0);seen[size_t(start)]=1;out.push_back(start);
    for(size_t at=0;at<out.size();++at)for(int32_t d=0;d<4;++d){
        const int32_t q=puzzleStep(width,height,out[at],d);
        if(q>=0&&!seen[size_t(q)]&&same(q)){seen[size_t(q)]=1;out.push_back(q);}
    }
    return out;
}
// Connected components of equal, nonzero-label cells (label 0 = not part of any region). Region ids are 1..count
// in row-major order of each region's first cell; `regions[cell]` is 0 for unlabeled cells.
struct PuzzleRegions {int32_t count=0;std::vector<int32_t> regions;std::vector<int32_t> sizes;};
template<class Label> PuzzleRegions puzzleRegions(int32_t width,int32_t height,const std::vector<Label>& labels){
    PuzzleRegions out;if(!puzzleGridSize(width,height)||labels.size()!=size_t(width*height))return out;
    out.regions.assign(labels.size(),0);
    for(int32_t c=0;c<width*height;++c){
        if(labels[size_t(c)]==Label{}||out.regions[size_t(c)])continue;
        const auto label=labels[size_t(c)];++out.count;
        auto cells=puzzleFloodFill(width,height,c,[&](int32_t q){return labels[size_t(q)]==label&&!out.regions[size_t(q)];});
        for(auto q:cells)out.regions[size_t(q)]=out.count;
        out.sizes.push_back(int32_t(cells.size()));
    }
    return out;
}
}
