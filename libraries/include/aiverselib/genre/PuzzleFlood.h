#pragma once
// Flood fill and connectivity puzzles (puzzle-solvers): the color-flood game (recolor the origin region until the
// board is one color) with an IDA* solver, and rotatable pipe/wire grids with source -> sink connectivity, leak
// checks and a bounded backtracking solver. Region counting is puzzleRegions (PuzzleCommon.h).
#include "PuzzleCommon.h"
#include <bit>
#include <functional>
namespace aiverse::lib::genre {
// ---------------------------------------------------------------------------------------------- color flood
struct PuzzleFloodLevel {int32_t width=0,height=0;uint8_t colors=0;std::vector<uint8_t> cells;int32_t origin=0;uint32_t moveLimit=0;};
enum class PuzzleFloodStatus:uint8_t {Playing,Won,Lost};
struct PuzzleFloodState {std::vector<uint8_t> cells;uint32_t moves=0,lastMove=0;PuzzleFloodStatus status=PuzzleFloodStatus::Playing;bool operator==(const PuzzleFloodState&)const=default;};
struct PuzzleFloodPick {uint32_t move=0;uint8_t color=0;};
struct PuzzleFloodSolution {PuzzleSearch status=PuzzleSearch::Invalid;std::vector<uint8_t> colors;uint64_t nodes=0;};
inline bool valid(const PuzzleFloodLevel& l){
    if(!puzzleGridSize(l.width,l.height)||l.colors<2||l.colors>16||l.cells.size()!=size_t(l.width*l.height)||l.origin<0||l.origin>=l.width*l.height||l.moveLimit<1||l.moveLimit>1000)return false;
    for(auto c:l.cells)if(c<1||c>l.colors)return false;
    return true;
}
inline bool valid(const PuzzleFloodState& s){
    if(s.cells.empty()||s.cells.size()>size_t(kPuzzleMaxCells)||s.lastMove<s.moves||s.status>PuzzleFloodStatus::Lost)return false;
    for(auto c:s.cells)if(c<1||c>16)return false;
    return true;
}
inline bool puzzleFloodCompatible(const PuzzleFloodLevel& l,const PuzzleFloodState& s){
    if(!valid(l)||!valid(s)||s.cells.size()!=l.cells.size()||s.moves>l.moveLimit)return false;
    for(auto c:s.cells)if(c>l.colors)return false;
    return true;
}
inline std::vector<int32_t> puzzleFloodRegion(const PuzzleFloodLevel& l,const std::vector<uint8_t>& cells){
    const auto color=cells[size_t(l.origin)];return puzzleFloodFill(l.width,l.height,l.origin,[&](int32_t q){return cells[size_t(q)]==color;});
}
inline PuzzleFloodState puzzleFloodBegin(const PuzzleFloodLevel& l){PuzzleFloodState s;if(valid(l))s.cells=l.cells;return s;}
namespace detail_flood {
inline void recolor(const PuzzleFloodLevel& l,std::vector<uint8_t>& cells,uint8_t color){for(auto c:puzzleFloodRegion(l,cells))cells[size_t(c)]=color;}
inline bool uniform(const std::vector<uint8_t>& cells){return std::adjacent_find(cells.begin(),cells.end(),std::not_equal_to<>{})==cells.end();}
// Admissible bound: every color still present outside the origin region needs at least one pick.
inline uint32_t bound(const PuzzleFloodLevel& l,const std::vector<uint8_t>& cells){
    std::vector<uint8_t> in(cells.size(),0);for(auto c:puzzleFloodRegion(l,cells))in[size_t(c)]=1;
    uint32_t seen=0;for(size_t c=0;c<cells.size();++c)if(!in[c])seen|=1u<<cells[c];
    return uint32_t(std::popcount(seen));
}
// Colors touching the origin region, ascending: the only picks that change anything.
inline std::vector<uint8_t> frontier(const PuzzleFloodLevel& l,const std::vector<uint8_t>& cells){
    auto region=puzzleFloodRegion(l,cells);std::vector<uint8_t> in(cells.size(),0);for(auto c:region)in[size_t(c)]=1;
    uint32_t mask=0;for(auto c:region)for(int32_t d=0;d<4;++d){auto q=puzzleStep(l.width,l.height,c,d);if(q>=0&&!in[size_t(q)])mask|=1u<<cells[size_t(q)];}
    std::vector<uint8_t> out;for(uint8_t k=1;k<=l.colors;++k)if(mask&(1u<<k))out.push_back(k);return out;
}
inline int search(const PuzzleFloodLevel& l,std::vector<uint8_t>& cells,uint32_t depth,uint32_t limit,PuzzleBudget& budget,std::vector<uint8_t>& path){
    if(uniform(cells))return 1;
    if(depth+bound(l,cells)>limit)return 0;
    if(!budget.take())return -1;
    for(auto color:frontier(l,cells)){
        auto copy=cells;recolor(l,copy,color);path.push_back(color);
        const int r=search(l,copy,depth+1,limit,budget,path);if(r!=0)return r;path.pop_back();
    }
    return 0;
}
}
// Iterative-deepening A* over picks, never deeper than the remaining moves. Unsolvable means no solution within the
// move limit; Unknown means the node budget ran out. Children are tried in ascending color order.
inline PuzzleFloodSolution puzzleFloodSolve(const PuzzleFloodLevel& l,const PuzzleFloodState& s,PuzzleBudget budget={}){
    PuzzleFloodSolution out;if(!puzzleFloodCompatible(l,s)||!puzzleValidBudget(budget))return out;
    auto cells=s.cells;const uint32_t remaining=l.moveLimit-s.moves;
    for(uint32_t limit=detail_flood::bound(l,cells);limit<=remaining;++limit){
        std::vector<uint8_t> path;auto copy=cells;const int r=detail_flood::search(l,copy,0,limit,budget,path);
        if(r==1){out.status=PuzzleSearch::Solved;out.colors=std::move(path);out.nodes=budget.used;return out;}
        if(r<0){out.status=PuzzleSearch::Unknown;out.nodes=budget.used;return out;}
    }
    out.status=PuzzleSearch::Unsolvable;out.nodes=budget.used;return out;
}
inline std::optional<uint32_t> puzzleFloodApply(const PuzzleFloodLevel& l,PuzzleFloodState& s,const PuzzleFloodPick& p){
    if(s.status!=PuzzleFloodStatus::Playing||p.move<=s.lastMove||!puzzleFloodCompatible(l,s)||p.color<1||p.color>l.colors||p.color==s.cells[size_t(l.origin)])return std::nullopt;
    const auto before=puzzleFloodRegion(l,s.cells).size();detail_flood::recolor(l,s.cells,p.color);++s.moves;s.lastMove=p.move;
    if(detail_flood::uniform(s.cells))s.status=PuzzleFloodStatus::Won;else if(s.moves>=l.moveLimit)s.status=PuzzleFloodStatus::Lost;
    return uint32_t(puzzleFloodRegion(l,s.cells).size()-before);
}
// Hint: the first pick of a solution within the budget, else the greedy pick absorbing most cells (lowest color on ties).
inline std::optional<uint8_t> puzzleFloodHint(const PuzzleFloodLevel& l,const PuzzleFloodState& s,PuzzleBudget budget={}){
    if(s.status!=PuzzleFloodStatus::Playing||!puzzleFloodCompatible(l,s))return std::nullopt;
    auto solution=puzzleFloodSolve(l,s,budget);if(solution.status==PuzzleSearch::Solved&&!solution.colors.empty())return solution.colors.front();
    std::optional<uint8_t> best;size_t bestSize=0;
    for(auto color:detail_flood::frontier(l,s.cells)){auto copy=s.cells;detail_flood::recolor(l,copy,color);auto n=puzzleFloodRegion(l,copy).size();if(!best||n>bestSize){best=color;bestSize=n;}}
    return best;
}
// --------------------------------------------------------------------------------------------- pipes / wires
// Openings are bits 1<<d for directions d (0 left, 1 right, 2 up, 3 down). Rotation turns clockwise in quarter turns.
inline constexpr uint8_t kPuzzlePipeRotatable=1,kPuzzlePipeSource=2,kPuzzlePipeSink=4;
struct PuzzlePipeLevel {int32_t width=0,height=0;std::vector<uint8_t> masks,flags;bool noLeaks=false;};
struct PuzzlePipeState {std::vector<uint8_t> rotation;uint32_t moves=0,lastMove=0;bool solved=false;bool operator==(const PuzzlePipeState&)const=default;};
struct PuzzlePipeRotate {uint32_t move=0;int32_t cell=-1;uint8_t turns=1;};
struct PuzzlePipeSolution {PuzzleSearch status=PuzzleSearch::Invalid;std::vector<uint8_t> rotation;uint64_t nodes=0;};
inline uint8_t puzzlePipeTurn(uint8_t mask,uint32_t turns){
    for(uint32_t t=0;t<turns%4;++t){
        // clockwise: left -> up, up -> right, right -> down, down -> left
        uint8_t out=0;if(mask&1)out|=4;if(mask&4)out|=2;if(mask&2)out|=8;if(mask&8)out|=1;mask=out;
    }
    return mask;
}
inline bool valid(const PuzzlePipeLevel& l){
    if(!puzzleGridSize(l.width,l.height)||l.masks.size()!=size_t(l.width*l.height)||l.flags.size()!=l.masks.size())return false;
    bool source=false,sink=false;for(size_t c=0;c<l.masks.size();++c){if(l.masks[c]>15||l.flags[c]>7)return false;source=source||(l.flags[c]&kPuzzlePipeSource);sink=sink||(l.flags[c]&kPuzzlePipeSink);}
    return source&&sink;
}
inline bool valid(const PuzzlePipeState& s){if(s.rotation.empty()||s.rotation.size()>size_t(kPuzzleMaxCells)||s.lastMove<s.moves)return false;for(auto r:s.rotation)if(r>3)return false;return true;}
inline bool puzzlePipeCompatible(const PuzzlePipeLevel& l,const PuzzlePipeState& s){
    if(!valid(l)||!valid(s)||s.rotation.size()!=l.masks.size())return false;
    for(size_t c=0;c<l.masks.size();++c)if(!(l.flags[c]&kPuzzlePipeRotatable)&&s.rotation[c])return false;
    return true;
}
inline uint8_t puzzlePipeMask(const PuzzlePipeLevel& l,const std::vector<uint8_t>& rotation,int32_t c){return puzzlePipeTurn(l.masks[size_t(c)],rotation[size_t(c)]);}
// Cells connected to any source through mutually open edges (BFS from sources in cell order).
inline std::vector<uint8_t> puzzlePipeReach(const PuzzlePipeLevel& l,const std::vector<uint8_t>& rotation){
    std::vector<uint8_t> seen(l.masks.size(),0);std::vector<int32_t> queue;
    for(int32_t c=0;c<int32_t(l.masks.size());++c)if(l.flags[size_t(c)]&kPuzzlePipeSource){seen[size_t(c)]=1;queue.push_back(c);}
    for(size_t at=0;at<queue.size();++at){
        const auto c=queue[at];const auto m=puzzlePipeMask(l,rotation,c);
        for(int32_t d=0;d<4;++d){
            if(!(m&(1<<d)))continue;
            const auto q=puzzleStep(l.width,l.height,c,d);
            if(q<0||seen[size_t(q)]||!(puzzlePipeMask(l,rotation,q)&(1<<puzzleOpposite(d))))continue;
            seen[size_t(q)]=1;queue.push_back(q);
        }
    }
    return seen;
}
// Connected: every sink reached. noLeaks additionally requires every opening on the grid to meet a partner (no open
// ends anywhere, as in net/plumber puzzles); the solver enforces the same rule edge by edge.
inline bool puzzlePipeSolved(const PuzzlePipeLevel& l,const std::vector<uint8_t>& rotation){
    if(rotation.size()!=l.masks.size())return false;
    const auto reach=puzzlePipeReach(l,rotation);
    for(int32_t c=0;c<int32_t(l.masks.size());++c){
        if((l.flags[size_t(c)]&kPuzzlePipeSink)&&!reach[size_t(c)])return false;
        if(!l.noLeaks)continue;
        const auto m=puzzlePipeMask(l,rotation,c);
        for(int32_t d=0;d<4;++d)if(m&(1<<d)){const auto q=puzzleStep(l.width,l.height,c,d);if(q<0||!(puzzlePipeMask(l,rotation,q)&(1<<puzzleOpposite(d))))return false;}
    }
    return true;
}
inline PuzzlePipeState puzzlePipeBegin(const PuzzlePipeLevel& l,std::vector<uint8_t> rotation={}){
    PuzzlePipeState s;if(!valid(l))return s;if(rotation.empty())rotation.assign(l.masks.size(),0);s.rotation=std::move(rotation);s.solved=puzzlePipeSolved(l,s.rotation);return s;
}
inline bool puzzlePipeApply(const PuzzlePipeLevel& l,PuzzlePipeState& s,const PuzzlePipeRotate& r){
    if(s.solved||r.move<=s.lastMove||!puzzlePipeCompatible(l,s)||r.cell<0||r.cell>=int32_t(l.masks.size())||!(l.flags[size_t(r.cell)]&kPuzzlePipeRotatable)||r.turns<1||r.turns>3)return false;
    s.rotation[size_t(r.cell)]=uint8_t((s.rotation[size_t(r.cell)]+r.turns)%4);++s.moves;s.lastMove=r.move;s.solved=puzzlePipeSolved(l,s.rotation);return true;
}
namespace detail_pipe {
inline int search(const PuzzlePipeLevel& l,std::vector<uint8_t>& rot,int32_t c,PuzzleBudget& budget){
    const int32_t n=int32_t(l.masks.size());
    if(c==n)return puzzlePipeSolved(l,rot)?1:0;
    if(!budget.take())return -1;
    const bool rotatable=l.flags[size_t(c)]&kPuzzlePipeRotatable;
    for(uint8_t t=0;t<(rotatable?4:1);++t){
        const auto m=puzzlePipeTurn(l.masks[size_t(c)],t);
        bool seenMask=false;for(uint8_t u=0;u<t;++u)if(puzzlePipeTurn(l.masks[size_t(c)],u)==m)seenMask=true;if(seenMask)continue;
        rot[size_t(c)]=t;
        if(l.noLeaks){
            // Edge consistency with the already-fixed left/up neighbours and the border.
            const int32_t x=c%l.width,y=c/l.width;bool ok=true;
            if(x==0&&(m&1))ok=false;
            if(x==l.width-1&&(m&2))ok=false;
            if(y==0&&(m&4))ok=false;
            if(y==l.height-1&&(m&8))ok=false;
            if(ok&&x>0)ok=bool(m&1)==bool(puzzlePipeMask(l,rot,c-1)&2);
            if(ok&&y>0)ok=bool(m&4)==bool(puzzlePipeMask(l,rot,c-l.width)&8);
            if(!ok)continue;
        }
        const int r=search(l,rot,c+1,budget);if(r!=0)return r;
    }
    rot[size_t(c)]=0;return 0;
}
}
// Bounded backtracking over rotations in cell order (rotations 0..3, duplicate masks skipped). With noLeaks the
// search keeps every edge consistent, so Unsolvable is a proof for both modes; Unknown = budget exhausted.
inline PuzzlePipeSolution puzzlePipeSolve(const PuzzlePipeLevel& l,PuzzleBudget budget={}){
    PuzzlePipeSolution out;if(!valid(l)||!puzzleValidBudget(budget))return out;
    std::vector<uint8_t> rot(l.masks.size(),0);const int r=detail_pipe::search(l,rot,0,budget);out.nodes=budget.used;
    out.status=r>0?PuzzleSearch::Solved:r<0?PuzzleSearch::Unknown:PuzzleSearch::Unsolvable;if(r>0)out.rotation=std::move(rot);return out;
}
// Hint: the first cell (row-major) whose opening mask differs from the solver's, with the turns that fix it.
inline std::optional<PuzzlePipeRotate> puzzlePipeHint(const PuzzlePipeLevel& l,const PuzzlePipeState& s,PuzzleBudget budget={}){
    if(s.solved||!puzzlePipeCompatible(l,s))return std::nullopt;
    auto solution=puzzlePipeSolve(l,budget);if(solution.status!=PuzzleSearch::Solved)return std::nullopt;
    for(int32_t c=0;c<int32_t(l.masks.size());++c){
        const auto want=puzzlePipeMask(l,solution.rotation,c);if(puzzlePipeMask(l,s.rotation,c)==want)continue;
        for(uint8_t t=1;t<4;++t)if(puzzlePipeTurn(puzzlePipeMask(l,s.rotation,c),t)==want)return PuzzlePipeRotate{s.lastMove+1,c,t};
    }
    return std::nullopt;
}
}
