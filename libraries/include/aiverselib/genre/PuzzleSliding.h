#pragma once
// Sliding and packing puzzles (puzzle-solvers): N-puzzle (15-puzzle) parity, seeded shuffles that are always
// solvable and an IDA* solver; sliding blocks of arbitrary rectangle sizes (Klotski / rush-hour style) with a BFS
// solver; polyomino packing with rotations/flips by bounded exact-cover backtracking and solution counting.
#include "PuzzleCommon.h"
#include "engine/core/Random.h"
#include <map>
namespace aiverse::lib::genre {
// ------------------------------------------------------------------------------------------------ N-puzzle
// Tiles 1..n-1 plus 0 = blank; the goal is ascending tiles with the blank last. A move slides the tile at `cell`
// (orthogonally next to the blank) into the blank.
struct PuzzleTilesState {int32_t width=0,height=0;std::vector<uint16_t> tiles;uint32_t moves=0,lastMove=0;bool solved=false;bool operator==(const PuzzleTilesState&)const=default;};
struct PuzzleTilesSlide {uint32_t move=0;int32_t cell=-1;};
struct PuzzleTilesSolution {PuzzleSearch status=PuzzleSearch::Invalid;std::vector<int32_t> cells;uint64_t nodes=0;};
inline bool puzzleTilesPermutation(int32_t w,int32_t h,const std::vector<uint16_t>& t){
    if(w<2||h<2||w>8||h>8||t.size()!=size_t(w*h))return false;
    std::vector<uint8_t> seen(t.size(),0);
    for(auto v:t){if(v>=t.size()||seen[v])return false;seen[v]=1;}return true;
}
inline bool valid(const PuzzleTilesState& s){return puzzleTilesPermutation(s.width,s.height,s.tiles)&&s.lastMove>=s.moves;}
inline bool puzzleTilesGoal(const std::vector<uint16_t>& t){for(size_t i=0;i+1<t.size();++i)if(t[i]!=i+1)return false;return !t.empty()&&t.back()==0;}
// Parity rule: odd width -> inversions even; even width -> inversions + blank row counted from the bottom (1-based) odd.
inline bool puzzleTilesSolvable(int32_t w,int32_t h,const std::vector<uint16_t>& t){
    if(!puzzleTilesPermutation(w,h,t))return false;
    int64_t inversions=0;int32_t blank=0;
    for(size_t i=0;i<t.size();++i){if(!t[i]){blank=int32_t(i);continue;}for(size_t j=i+1;j<t.size();++j)if(t[j]&&t[j]<t[i])++inversions;}
    if(w%2)return inversions%2==0;
    return (inversions+(h-blank/w))%2==1;
}
// Uniform seeded permutation; when its parity is unsolvable the first two non-blank cells swap, which flips it.
inline std::vector<uint16_t> puzzleTilesShuffle(int32_t w,int32_t h,Random& rng){
    std::vector<uint16_t> t;if(w<2||h<2||w>8||h>8)return t;t.resize(size_t(w*h));
    for(size_t i=0;i<t.size();++i)t[i]=uint16_t((i+1)%t.size());
    for(size_t i=t.size();i>1;--i)std::swap(t[i-1],t[rng.below(uint32_t(i))]);
    if(!puzzleTilesSolvable(w,h,t)){size_t a=t[0]?0:1,b=t[a+1]?a+1:a+2;std::swap(t[a],t[b]);}
    return t;
}
inline std::optional<PuzzleTilesState> puzzleTilesBegin(int32_t w,int32_t h,std::vector<uint16_t> tiles){
    if(!puzzleTilesSolvable(w,h,tiles))return std::nullopt;
    PuzzleTilesState s{w,h,std::move(tiles),0,0,false};s.solved=puzzleTilesGoal(s.tiles);return s;
}
inline int32_t puzzleTilesBlank(const PuzzleTilesState& s){return int32_t(std::find(s.tiles.begin(),s.tiles.end(),uint16_t(0))-s.tiles.begin());}
inline bool puzzleTilesApply(PuzzleTilesState& s,const PuzzleTilesSlide& m){
    if(s.solved||m.move<=s.lastMove||!valid(s))return false;
    const auto blank=puzzleTilesBlank(s);
    bool adjacent=false;for(int32_t d=0;d<4;++d)adjacent=adjacent||puzzleStep(s.width,s.height,blank,d)==m.cell;if(!adjacent)return false;
    std::swap(s.tiles[size_t(blank)],s.tiles[size_t(m.cell)]);++s.moves;s.lastMove=m.move;s.solved=puzzleTilesGoal(s.tiles);return true;
}
namespace detail_tiles {
inline int32_t manhattan(const PuzzleTilesState& s){
    int32_t h=0;for(int32_t c=0;c<int32_t(s.tiles.size());++c)if(s.tiles[size_t(c)])h+=puzzleManhattan(s.width,c,s.tiles[size_t(c)]-1);return h;
}
inline int32_t search(PuzzleTilesState& s,int32_t blank,int32_t g,int32_t limit,int32_t h,int32_t previous,PuzzleBudget& budget,std::vector<int32_t>& path,int32_t& next){
    const int32_t f=g+h;if(f>limit){next=std::min(next,f);return 0;}
    if(h==0)return 1;
    if(!budget.take())return -1;
    for(int32_t d=0;d<4;++d){
        const auto cell=puzzleStep(s.width,s.height,blank,d);if(cell<0||cell==previous)continue;
        const auto tile=s.tiles[size_t(cell)];const int32_t goal=tile-1;
        const int32_t h2=h-puzzleManhattan(s.width,cell,goal)+puzzleManhattan(s.width,blank,goal);
        std::swap(s.tiles[size_t(blank)],s.tiles[size_t(cell)]);path.push_back(cell);
        const int32_t r=search(s,cell,g+1,limit,h2,blank,budget,path,next);
        std::swap(s.tiles[size_t(blank)],s.tiles[size_t(cell)]);if(r!=0)return r;path.pop_back();
    }
    return 0;
}
}
// IDA* with the Manhattan bound; `cells` are the tiles to slide in order. Unsolvable states are rejected by parity
// (status Unsolvable) without searching; Unknown = node budget exhausted.
inline PuzzleTilesSolution puzzleTilesSolve(const PuzzleTilesState& start,PuzzleBudget budget={}){
    PuzzleTilesSolution out;if(!valid(start)||!puzzleValidBudget(budget))return out;
    if(!puzzleTilesSolvable(start.width,start.height,start.tiles)){out.status=PuzzleSearch::Unsolvable;return out;}
    auto s=start;const int32_t h=detail_tiles::manhattan(s);int32_t limit=h;
    while(true){
        std::vector<int32_t> path;int32_t next=INT32_MAX;const int32_t r=detail_tiles::search(s,puzzleTilesBlank(s),0,limit,h,-1,budget,path,next);
        if(r>0){out.status=PuzzleSearch::Solved;out.cells=std::move(path);break;}
        if(r<0||next==INT32_MAX){out.status=PuzzleSearch::Unknown;break;}
        limit=next;
    }
    out.nodes=budget.used;return out;
}
// Hint: first slide of an IDA* solution, else the slide that lowers the Manhattan sum most (lowest cell on ties).
inline std::optional<int32_t> puzzleTilesHint(const PuzzleTilesState& s,PuzzleBudget budget={}){
    if(s.solved||!valid(s))return std::nullopt;
    auto solution=puzzleTilesSolve(s,budget);
    if(solution.status==PuzzleSearch::Solved&&!solution.cells.empty())return solution.cells.front();
    const auto blank=puzzleTilesBlank(s);std::optional<int32_t> best;int32_t bestH=INT32_MAX;
    for(int32_t d=0;d<4;++d){auto c=puzzleStep(s.width,s.height,blank,d);if(c<0)continue;auto copy=s;std::swap(copy.tiles[size_t(blank)],copy.tiles[size_t(c)]);auto h=detail_tiles::manhattan(copy);if(h<bestH||(h==bestH&&c<*best)){bestH=h;best=c;}}
    return best;
}
// ------------------------------------------------------------------------------------------ sliding blocks
// Rectangular blocks move one cell at a time; `positions` holds each block's top-left cell. Blocks with the same
// size and `interchangeable` set are identical for the solver's visited set (the target block never is).
struct PuzzleBlockDef {int32_t width=1,height=1;bool interchangeable=true;};
struct PuzzleBlocksLevel {int32_t width=0,height=0;std::vector<uint8_t> walls;std::vector<PuzzleBlockDef> blocks;std::vector<int32_t> start;int32_t target=0,goal=0;};
struct PuzzleBlocksState {std::vector<int32_t> positions;uint32_t moves=0,lastMove=0;bool solved=false;bool operator==(const PuzzleBlocksState&)const=default;};
struct PuzzleBlocksMove {uint32_t move=0;int32_t block=-1,direction=-1;};
struct PuzzleBlocksSolution {PuzzleSearch status=PuzzleSearch::Invalid;std::vector<std::pair<int32_t,int32_t>> moves;uint64_t nodes=0;};
namespace detail_blocks {
// Occupancy (block index + 1 per cell) or nullopt when blocks overlap, leave the grid or cover walls.
inline std::optional<std::vector<int16_t>> occupancy(const PuzzleBlocksLevel& l,const std::vector<int32_t>& p){
    std::vector<int16_t> o(size_t(l.width*l.height),0);
    for(size_t b=0;b<l.blocks.size();++b){
        const int32_t x0=p[b]%l.width,y0=p[b]/l.width;if(p[b]<0||x0+l.blocks[b].width>l.width||y0+l.blocks[b].height>l.height)return std::nullopt;
        for(int32_t y=y0;y<y0+l.blocks[b].height;++y)for(int32_t x=x0;x<x0+l.blocks[b].width;++x){const int32_t c=y*l.width+x;if(l.walls[size_t(c)]||o[size_t(c)])return std::nullopt;o[size_t(c)]=int16_t(b+1);}
    }
    return o;
}
inline std::vector<int32_t> key(const PuzzleBlocksLevel& l,const std::vector<int32_t>& p){
    std::vector<int32_t> k=p;std::map<std::pair<int32_t,int32_t>,std::vector<size_t>> groups;
    for(size_t b=0;b<l.blocks.size();++b)if(l.blocks[b].interchangeable&&int32_t(b)!=l.target)groups[{l.blocks[b].width,l.blocks[b].height}].push_back(b);
    for(auto& [size,members]:groups){(void)size;std::vector<int32_t> v;for(auto b:members)v.push_back(p[b]);std::sort(v.begin(),v.end());for(size_t i=0;i<members.size();++i)k[members[i]]=v[i];}
    return k;
}
}
inline bool valid(const PuzzleBlocksLevel& l){
    if(!puzzleGridSize(l.width,l.height)||l.walls.size()!=size_t(l.width*l.height)||l.blocks.empty()||l.blocks.size()>64||l.start.size()!=l.blocks.size()
        ||l.target<0||l.target>=int32_t(l.blocks.size())||l.goal<0||l.goal>=l.width*l.height)return false;
    for(const auto& b:l.blocks)if(b.width<1||b.height<1||b.width>l.width||b.height>l.height)return false;
    return detail_blocks::occupancy(l,l.start).has_value();
}
inline bool valid(const PuzzleBlocksState& s){return !s.positions.empty()&&s.positions.size()<=64&&s.lastMove>=s.moves;}
inline bool puzzleBlocksGoal(const PuzzleBlocksLevel& l,const std::vector<int32_t>& p){return p[size_t(l.target)]==l.goal;}
inline PuzzleBlocksState puzzleBlocksBegin(const PuzzleBlocksLevel& l){PuzzleBlocksState s;if(!valid(l))return s;s.positions=l.start;s.solved=puzzleBlocksGoal(l,s.positions);return s;}
// Moves block b one cell in direction d when the destination is inside, wall-free and unoccupied.
inline bool puzzleBlocksShift(const PuzzleBlocksLevel& l,std::vector<int32_t>& p,int32_t b,int32_t d){
    if(b<0||b>=int32_t(l.blocks.size())||d<0||d>3)return false;
    const int32_t x=p[size_t(b)]%l.width+kPuzzleDx[size_t(d)],y=p[size_t(b)]/l.width+kPuzzleDy[size_t(d)];
    if(x<0||y<0||x+l.blocks[size_t(b)].width>l.width||y+l.blocks[size_t(b)].height>l.height)return false;
    auto copy=p;copy[size_t(b)]=y*l.width+x;if(!detail_blocks::occupancy(l,copy))return false;p=std::move(copy);return true;
}
inline bool puzzleBlocksApply(const PuzzleBlocksLevel& l,PuzzleBlocksState& s,const PuzzleBlocksMove& m){
    if(s.solved||m.move<=s.lastMove||!valid(l)||s.positions.size()!=l.blocks.size()||!detail_blocks::occupancy(l,s.positions))return false;
    if(!puzzleBlocksShift(l,s.positions,m.block,m.direction))return false;
    ++s.moves;s.lastMove=m.move;s.solved=puzzleBlocksGoal(l,s.positions);return true;
}
// Breadth-first (fewest single-cell moves); children in block order then direction 0..3. Budget = expanded states.
inline PuzzleBlocksSolution puzzleBlocksSolve(const PuzzleBlocksLevel& l,const std::vector<int32_t>& start,PuzzleBudget budget={}){
    PuzzleBlocksSolution out;if(!valid(l)||start.size()!=l.blocks.size()||!detail_blocks::occupancy(l,start)||!puzzleValidBudget(budget))return out;
    struct Node {std::vector<int32_t> p;size_t parent;int32_t block,dir;};
    std::vector<Node> nodes{{start,0,-1,-1}};std::map<std::vector<int32_t>,size_t> seen{{detail_blocks::key(l,start),0}};
    for(size_t at=0;at<nodes.size();++at){
        if(puzzleBlocksGoal(l,nodes[at].p)){
            out.status=PuzzleSearch::Solved;for(size_t i=at;nodes[i].block>=0;i=nodes[i].parent)out.moves.push_back({nodes[i].block,nodes[i].dir});
            std::reverse(out.moves.begin(),out.moves.end());out.nodes=budget.used;return out;
        }
        if(!budget.take()){out.status=PuzzleSearch::Unknown;out.nodes=budget.used;return out;}
        for(int32_t b=0;b<int32_t(l.blocks.size());++b)for(int32_t d=0;d<4;++d){
            auto p=nodes[at].p;if(!puzzleBlocksShift(l,p,b,d))continue;auto k=detail_blocks::key(l,p);
            if(seen.emplace(std::move(k),nodes.size()).second)nodes.push_back({std::move(p),at,b,d});
        }
    }
    out.status=PuzzleSearch::Unsolvable;out.nodes=budget.used;return out;
}
// ------------------------------------------------------------------------------------------------ packing
// Pieces are cell offsets {x,y}; orientations are the distinct normalized images under rotation (and flips when
// allowed) in a fixed transform order. Pieces exactly cover the target cells, each piece used at most once
// (`useAll`: exactly once).
struct PuzzlePieceDef {std::vector<std::array<int32_t,2>> cells;bool rotate=true,flip=false;};
struct PuzzlePackLevel {int32_t width=0,height=0;std::vector<uint8_t> target;std::vector<PuzzlePieceDef> pieces;bool useAll=true;};
struct PuzzlePlacement {int32_t piece=-1,orientation=0,x=0,y=0;bool operator==(const PuzzlePlacement&)const=default;};
struct PuzzlePackState {std::vector<PuzzlePlacement> placed;uint32_t moves=0,lastMove=0;bool solved=false;bool operator==(const PuzzlePackState&)const=default;};
enum class PuzzlePackAction:uint8_t {Place,Remove};
struct PuzzlePackCommand {uint32_t move=0;PuzzlePackAction action=PuzzlePackAction::Place;PuzzlePlacement placement;};
struct PuzzlePackSolution {PuzzleSearch status=PuzzleSearch::Invalid;std::vector<PuzzlePlacement> placements;uint32_t count=0;uint64_t nodes=0;};
inline std::vector<std::vector<std::array<int32_t,2>>> puzzlePieceOrientations(const PuzzlePieceDef& p){
    std::vector<std::vector<std::array<int32_t,2>>> out;
    for(int t=0;t<8;++t){
        if((t%4&&!p.rotate)||(t>=4&&!p.flip))continue;
        std::vector<std::array<int32_t,2>> cells;
        for(auto [x,y]:p.cells){int32_t a=t>=4?-x:x,b=y;for(int r=0;r<t%4;++r){const int32_t na=-b,nb=a;a=na;b=nb;}cells.push_back({a,b});}
        int32_t mx=INT32_MAX,my=INT32_MAX;for(auto [x,y]:cells){mx=std::min(mx,x);my=std::min(my,y);}
        for(auto& c:cells){c[0]-=mx;c[1]-=my;}
        std::sort(cells.begin(),cells.end(),[](auto a,auto b){return a[1]!=b[1]?a[1]<b[1]:a[0]<b[0];});
        if(std::find(out.begin(),out.end(),cells)==out.end())out.push_back(std::move(cells));
    }
    return out;
}
inline bool valid(const PuzzlePackLevel& l){
    if(!puzzleGridSize(l.width,l.height)||l.target.size()!=size_t(l.width*l.height)||l.pieces.empty()||l.pieces.size()>64)return false;
    int64_t area=0,cover=0;for(auto t:l.target){if(t>1)return false;cover+=t;}
    for(const auto& p:l.pieces){
        if(p.cells.empty()||p.cells.size()>64)return false;
        auto cells=p.cells;std::sort(cells.begin(),cells.end());
        if(std::adjacent_find(cells.begin(),cells.end())!=cells.end())return false;
        for(auto [x,y]:cells)if(x<0||y<0||x>=kPuzzleMaxSide||y>=kPuzzleMaxSide)return false;
        area+=int64_t(cells.size());
    }
    return cover>0&&(l.useAll?area==cover:area>=cover);
}
inline bool valid(const PuzzlePackState& s){if(s.placed.size()>64||s.lastMove<s.moves)return false;for(size_t i=1;i<s.placed.size();++i)if(s.placed[i-1].piece>=s.placed[i].piece)return false;return true;}
namespace detail_pack {
// Cells covered by a placement, or empty when it leaves the grid / target or overlaps `cover`.
inline std::vector<int32_t> cells(const PuzzlePackLevel& l,const PuzzlePlacement& p,const std::vector<uint8_t>& cover){
    std::vector<int32_t> out;if(p.piece<0||p.piece>=int32_t(l.pieces.size()))return out;
    auto orientations=puzzlePieceOrientations(l.pieces[size_t(p.piece)]);if(p.orientation<0||p.orientation>=int32_t(orientations.size()))return out;
    for(auto [dx,dy]:orientations[size_t(p.orientation)]){
        const int32_t x=p.x+dx,y=p.y+dy;if(!puzzleInside(l.width,l.height,x,y)){out.clear();return out;}
        const int32_t c=y*l.width+x;if(!l.target[size_t(c)]||cover[size_t(c)]){out.clear();return out;}out.push_back(c);
    }
    return out;
}
inline std::optional<std::vector<uint8_t>> cover(const PuzzlePackLevel& l,const std::vector<PuzzlePlacement>& placed){
    std::vector<uint8_t> c(l.target.size(),0);
    for(const auto& p:placed){auto cells=detail_pack::cells(l,p,c);if(cells.empty())return std::nullopt;for(auto q:cells)c[size_t(q)]=1;}
    return c;
}
inline bool complete(const PuzzlePackLevel& l,const std::vector<uint8_t>& c,size_t used){
    for(size_t i=0;i<c.size();++i)if(l.target[i]&&!c[i])return false;
    return !l.useAll||used==l.pieces.size();
}
struct Search {const PuzzlePackLevel& l;std::vector<std::vector<std::vector<std::array<int32_t,2>>>> orient;PuzzleBudget& budget;uint32_t limit;uint32_t count=0;std::vector<PuzzlePlacement> current,first;bool out=false;};
// Exact cover: the first uncovered target cell (row-major) must be covered by the first cell of some orientation.
inline void run(Search& s,std::vector<uint8_t>& c,std::vector<uint8_t>& used){
    if(s.count>=s.limit||s.out)return;
    int32_t anchor=-1;for(int32_t i=0;i<int32_t(c.size());++i)if(s.l.target[size_t(i)]&&!c[size_t(i)]){anchor=i;break;}
    if(anchor<0){if(complete(s.l,c,s.current.size())){if(!s.count)s.first=s.current;++s.count;}return;}
    if(!s.budget.take()){s.out=true;return;}
    for(int32_t p=0;p<int32_t(s.l.pieces.size());++p){
        if(used[size_t(p)])continue;
        for(int32_t o=0;o<int32_t(s.orient[size_t(p)].size());++o){
            const auto& shape=s.orient[size_t(p)][size_t(o)];
            PuzzlePlacement pl{p,o,anchor%s.l.width-shape[0][0],anchor/s.l.width-shape[0][1]};
            auto cells=detail_pack::cells(s.l,pl,c);if(cells.empty())continue;
            for(auto q:cells)c[size_t(q)]=1;
            used[size_t(p)]=1;s.current.push_back(pl);
            run(s,c,used);
            s.current.pop_back();used[size_t(p)]=0;for(auto q:cells)c[size_t(q)]=0;
            if(s.count>=s.limit||s.out)return;
        }
    }
}
}
// Counts solutions up to `limit` (2 is a uniqueness check) extending `fixed` placements. Solved: count >= 1
// (placements = first solution in search order, `fixed` excluded); Unsolvable: proven none; Unknown: budget ran out
// before the count was settled (a found solution is still returned).
inline PuzzlePackSolution puzzlePackSolve(const PuzzlePackLevel& l,const std::vector<PuzzlePlacement>& fixed={},uint32_t limit=1,PuzzleBudget budget={}){
    PuzzlePackSolution out;if(!valid(l)||!puzzleValidBudget(budget)||limit<1)return out;
    auto c=detail_pack::cover(l,fixed);if(!c)return out;std::vector<uint8_t> used(l.pieces.size(),0);for(const auto& p:fixed){if(used[size_t(p.piece)])return out;used[size_t(p.piece)]=1;}
    detail_pack::Search s{l,{},budget,limit,0,{},{},false};for(const auto& p:l.pieces)s.orient.push_back(puzzlePieceOrientations(p));
    s.current=fixed;detail_pack::run(s,*c,used);
    out.count=s.count;out.nodes=budget.used;
    if(s.count){out.placements.assign(s.first.begin()+std::ptrdiff_t(fixed.size()),s.first.end());}
    out.status=s.out?PuzzleSearch::Unknown:s.count?PuzzleSearch::Solved:PuzzleSearch::Unsolvable;
    return out;
}
inline bool puzzlePackApply(const PuzzlePackLevel& l,PuzzlePackState& s,const PuzzlePackCommand& cmd){
    if(s.solved||cmd.move<=s.lastMove||!valid(l)||!valid(s))return false;
    auto placed=s.placed;
    auto it=std::find_if(placed.begin(),placed.end(),[&](const PuzzlePlacement& p){return p.piece==cmd.placement.piece;});
    if(cmd.action==PuzzlePackAction::Remove){if(it==placed.end())return false;placed.erase(it);}
    else{
        if(it!=placed.end())return false;
        placed.push_back(cmd.placement);
        std::sort(placed.begin(),placed.end(),[](const PuzzlePlacement& a,const PuzzlePlacement& b){return a.piece<b.piece;});
    }
    auto c=detail_pack::cover(l,placed);if(!c)return false;
    s.placed=std::move(placed);++s.moves;s.lastMove=cmd.move;s.solved=detail_pack::complete(l,*c,s.placed.size());return true;
}
// Hint: extend the current placements when possible; otherwise remove the lowest placed piece that disagrees with a
// from-scratch solution (or the lowest placed piece when none is known within the budget).
inline std::optional<PuzzlePackCommand> puzzlePackHint(const PuzzlePackLevel& l,const PuzzlePackState& s,PuzzleBudget budget={}){
    if(s.solved||!valid(l)||!valid(s))return std::nullopt;
    auto extend=puzzlePackSolve(l,s.placed,1,budget);
    if(extend.status==PuzzleSearch::Solved&&!extend.placements.empty())return PuzzlePackCommand{s.lastMove+1,PuzzlePackAction::Place,extend.placements.front()};
    if(s.placed.empty())return std::nullopt;
    auto fresh=puzzlePackSolve(l,{},1,budget);
    for(const auto& p:s.placed)if(fresh.status!=PuzzleSearch::Solved||std::find(fresh.placements.begin(),fresh.placements.end(),p)==fresh.placements.end())return PuzzlePackCommand{s.lastMove+1,PuzzlePackAction::Remove,p};
    return std::nullopt;
}
}
