#pragma once
// Match-3 rules (puzzle-solvers): swap validation, line / L / T match detection, special pieces from data rules,
// gravity + refill from the caller's seeded Random, cascades with a fixed resolution order, no-move detection,
// a shuffle that always leaves a legal move, goal tracking and hints. Players and bots submit the same Match3Swap.
//
// Resolution order of one cascade step (chain n = 1, 2, ...):
//  1. runs of >= 3 equal colors are found row by row, then column by column; runs sharing a cell form one group;
//     groups are ordered by their lowest cell;
//  2. each group may spawn a special (first rule whose shape matches: Line5 = a run of 5+, Corner = a horizontal
//     and a vertical run sharing a cell (L/T), Line4 = a run of 4) at the crossing cell (Corner), else at the swap
//     destination, else the swap source, else the group's lowest cell;
//  3. cleared specials activate in ascending cell order (chains of specials are queued in discovery order);
//  4. cleared tiles add `tileScore * n` each and count toward collect goals; spawned specials stay in place;
//  5. gravity compacts every column downward past holes; refill fills empty cells column by column (left to
//     right), top to bottom, color = 1 + rng.below(colors).
// Cascades stop after kMatch3MaxCascades steps (refill keeps playing; the cap only bounds per-move work).
#include "PuzzleCommon.h"
#include "engine/core/Random.h"
#include <utility>
namespace aiverse::lib::genre {
inline constexpr uint8_t kMatch3Blocked=255;       // hole: never holds a tile; gravity passes it
inline constexpr uint8_t kMatch3MaxColors=8;
inline constexpr uint32_t kMatch3MaxCascades=64;
enum class Match3Special:uint8_t {None,StripeRow,StripeColumn,Bomb,ColorBomb};
enum class Match3Shape:uint8_t {None,Line4,Line5,Corner};
struct Match3Tile {uint8_t color=0;Match3Special special=Match3Special::None;bool operator==(const Match3Tile&)const=default;};
// `perpendicular` stripes clear across the run that made them (a horizontal run makes a StripeColumn).
struct Match3SpecialRule {Match3Shape shape=Match3Shape::Line4;Match3Special special=Match3Special::StripeRow;bool perpendicular=false;};
struct Match3Rules {
    int32_t width=8,height=8;
    uint8_t colors=5;                         // 3..8
    std::vector<uint8_t> holes;               // empty or width*height flags (1 = hole)
    std::vector<Match3SpecialRule> specials;  // at most 8; first rule with the group's shape wins
    bool colorBombSwap=true;                  // a ColorBomb may swap with any tile: clears that tile's color
    int64_t tileScore=10;
    uint32_t shuffleAttempts=32;              // random permutations tried before the guaranteed pattern fallback
    bool autoShuffle=true;                    // false: no legal move left ends the level as Lost
};
struct Match3Goals {uint32_t moveLimit=20;int64_t targetScore=0;std::vector<std::pair<uint8_t,uint32_t>> collect;};
struct Match3Level {Match3Rules rules;Match3Goals goals;std::vector<Match3Tile> start;}; // start empty: generated
enum class Match3Status:uint8_t {Playing,Won,Lost};
struct Match3State {
    std::vector<Match3Tile> tiles;
    std::vector<uint32_t> collected;          // per color 0..colors (index 0 unused)
    int64_t score=0;
    uint32_t moves=0,lastMove=0,shuffles=0,maxChain=0;
    Match3Status status=Match3Status::Playing;
    bool operator==(const Match3State&)const=default;
};
struct Match3Swap {uint32_t move=0;int32_t from=-1,to=-1;};   // move numbers strictly increase per state
struct Match3Report {uint32_t chains=0,cleared=0;int64_t score=0;std::vector<std::pair<int32_t,Match3Special>> created;bool shuffled=false;Match3Status status=Match3Status::Playing;};
struct Match3Group {uint8_t color=0;std::vector<int32_t> cells;Match3Shape shape=Match3Shape::None;bool horizontal=false;int32_t crossing=-1;};

inline bool match3Hole(const Match3Rules& r,int32_t c){return !r.holes.empty()&&r.holes[size_t(c)]!=0;}
inline bool match3Colored(const Match3Tile& t){return t.color!=0&&t.color!=kMatch3Blocked;}
namespace detail_match3 {
inline bool sameAt(const Match3Rules& r,const std::vector<Match3Tile>& t,int32_t x,int32_t y,uint8_t color){
    return puzzleInside(r.width,r.height,x,y)&&t[size_t(y*r.width+x)].color==color;
}
// Cell `c` is inside a horizontal or vertical run of 3+.
inline bool inRun(const Match3Rules& r,const std::vector<Match3Tile>& t,int32_t c){
    const auto color=t[size_t(c)].color;if(color==0||color==kMatch3Blocked)return false;
    const int32_t x=c%r.width,y=c/r.width;
    for(int axis=0;axis<2;++axis){
        const int32_t ax=axis==0?1:0,ay=axis==0?0:1;int32_t n=1;
        for(int32_t k=1;sameAt(r,t,x-ax*k,y-ay*k,color);++k)++n;
        for(int32_t k=1;sameAt(r,t,x+ax*k,y+ay*k,color);++k)++n;
        if(n>=3)return true;
    }
    return false;
}
inline int32_t root(std::vector<int32_t>& p,int32_t i){while(p[size_t(i)]!=i){p[size_t(i)]=p[size_t(p[size_t(i)])];i=p[size_t(i)];}return i;}
inline std::optional<std::vector<Match3Tile>> pattern(const Match3Rules& r,bool transposed,int32_t offset,uint8_t base);
}
inline bool match3HasMove(const Match3Rules& r,const std::vector<Match3Tile>& tiles);
inline bool valid(const Match3Rules& r){
    if(!puzzleGridSize(r.width,r.height)||r.colors<3||r.colors>kMatch3MaxColors||(!r.holes.empty()&&r.holes.size()!=size_t(r.width*r.height))
        ||r.specials.size()>8||r.tileScore<0||r.tileScore>1000000||r.shuffleAttempts>1024)return false;
    for(const auto& s:r.specials)if(s.shape==Match3Shape::None||s.shape>Match3Shape::Corner||s.special==Match3Special::None||s.special>Match3Special::ColorBomb)return false;
    for(auto h:r.holes)if(h>1)return false;
    // The shuffle guarantee needs a board shape where the fallback pattern has a legal move.
    for(int t=0;t<2;++t)for(int32_t o=0;o<2;++o)if(detail_match3::pattern(r,t!=0,o,0))return true;
    return false;
}
inline bool valid(const Match3Goals& g,const Match3Rules& r){
    if(g.moveLimit<1||g.moveLimit>100000||g.targetScore<0||g.collect.size()>kMatch3MaxColors)return false;
    for(size_t i=0;i<g.collect.size();++i)if(g.collect[i].first<1||g.collect[i].first>r.colors||g.collect[i].second>1000000||(i&&g.collect[i-1].first>=g.collect[i].first))return false;
    return true;
}
inline bool valid(const Match3Level& l){return valid(l.rules)&&valid(l.goals,l.rules)&&(l.start.empty()||l.start.size()==size_t(l.rules.width*l.rules.height));}
// State-only checks (used by save/load); match3Compatible adds the board shape.
inline bool valid(const Match3State& s){
    if(s.tiles.empty()||s.tiles.size()>size_t(kPuzzleMaxCells)||s.collected.size()<4||s.collected.size()>size_t(kMatch3MaxColors)+1||s.score<0
        ||s.status>Match3Status::Lost||s.lastMove<s.moves||s.maxChain>kMatch3MaxCascades)return false;
    for(const auto& t:s.tiles)if((t.color>kMatch3MaxColors&&t.color!=kMatch3Blocked)||t.special>Match3Special::ColorBomb||(t.color==kMatch3Blocked&&t.special!=Match3Special::None))return false;
    return true;
}
inline bool match3Compatible(const Match3Rules& r,const Match3State& s){
    if(!valid(r)||!valid(s)||s.tiles.size()!=size_t(r.width*r.height)||s.collected.size()!=size_t(r.colors)+1)return false;
    for(int32_t c=0;c<r.width*r.height;++c){const auto& t=s.tiles[size_t(c)];if(match3Hole(r,c)!=(t.color==kMatch3Blocked)||(t.color>r.colors&&t.color!=kMatch3Blocked))return false;}
    return true;
}

// Groups of runs (3+ equal colors), ordered by lowest cell; each group's cells are sorted.
inline std::vector<Match3Group> match3Groups(const Match3Rules& r,const std::vector<Match3Tile>& t){
    struct Run {std::vector<int32_t> cells;bool horizontal;};std::vector<Run> runs;
    for(int axis=0;axis<2;++axis){
        const int32_t outer=axis==0?r.height:r.width,inner=axis==0?r.width:r.height;
        for(int32_t o=0;o<outer;++o)for(int32_t i=0;i<inner;){
            auto cell=[&](int32_t k){return axis==0?o*r.width+k:k*r.width+o;};
            const auto color=t[size_t(cell(i))].color;int32_t j=i+1;
            while(j<inner&&t[size_t(cell(j))].color==color)++j;
            if(color!=0&&color!=kMatch3Blocked&&j-i>=3){Run run{{},axis==0};for(int32_t k=i;k<j;++k)run.cells.push_back(cell(k));runs.push_back(std::move(run));}
            i=j;
        }
    }
    std::vector<int32_t> parent(runs.size());for(size_t i=0;i<runs.size();++i)parent[i]=int32_t(i);
    std::vector<int32_t> owner(t.size(),-1);
    for(size_t i=0;i<runs.size();++i)for(auto c:runs[i].cells){
        if(owner[size_t(c)]>=0){auto a=detail_match3::root(parent,owner[size_t(c)]),b=detail_match3::root(parent,int32_t(i));if(a!=b)parent[size_t(std::max(a,b))]=std::min(a,b);}
        else owner[size_t(c)]=int32_t(i);
    }
    std::vector<Match3Group> groups;std::vector<int32_t> index(runs.size(),-1);
    std::vector<uint8_t> inH(t.size(),0),inV(t.size(),0);
    for(const auto& run:runs)for(auto c:run.cells)(run.horizontal?inH:inV)[size_t(c)]=1;
    for(size_t i=0;i<runs.size();++i){
        const auto g=detail_match3::root(parent,int32_t(i));
        if(index[size_t(g)]<0){index[size_t(g)]=int32_t(groups.size());groups.push_back({t[size_t(runs[i].cells[0])].color,{},Match3Shape::None,false,-1});}
        auto& group=groups[size_t(index[size_t(g)])];
        for(auto c:runs[i].cells)group.cells.push_back(c);
    }
    for(auto& g:groups){
        std::sort(g.cells.begin(),g.cells.end());g.cells.erase(std::unique(g.cells.begin(),g.cells.end()),g.cells.end());
        int32_t longest=0;bool h=false,v=false;
        for(const auto& run:runs)if(std::binary_search(g.cells.begin(),g.cells.end(),run.cells[0])&&t[size_t(run.cells[0])].color==g.color){
            if(int32_t(run.cells.size())>longest){longest=int32_t(run.cells.size());g.horizontal=run.horizontal;}
            (run.horizontal?h:v)=true;
        }
        for(auto c:g.cells)if(inH[size_t(c)]&&inV[size_t(c)]){g.crossing=c;break;}
        g.shape=longest>=5?Match3Shape::Line5:(h&&v)?Match3Shape::Corner:longest==4?Match3Shape::Line4:Match3Shape::None;
    }
    std::sort(groups.begin(),groups.end(),[](const Match3Group& a,const Match3Group& b){return a.cells.front()<b.cells.front();});
    return groups;
}
inline bool match3Adjacent(const Match3Rules& r,int32_t a,int32_t b){
    if(a<0||b<0||a>=r.width*r.height||b>=r.width*r.height)return false;
    for(int32_t d=0;d<4;++d)if(puzzleStep(r.width,r.height,a,d)==b)return true;
    return false;
}
// A swap is legal when both cells hold tiles, they are orthogonal neighbours and the swap creates a run through
// either cell (or a ColorBomb takes part and colorBombSwap is on). Never mutates `tiles`.
inline bool match3Legal(const Match3Rules& r,const std::vector<Match3Tile>& tiles,int32_t from,int32_t to){
    if(tiles.size()!=size_t(r.width*r.height)||!match3Adjacent(r,from,to))return false;
    const auto& a=tiles[size_t(from)];const auto& b=tiles[size_t(to)];
    if(!match3Colored(a)||!match3Colored(b))return false;
    if(r.colorBombSwap&&(a.special==Match3Special::ColorBomb||b.special==Match3Special::ColorBomb))return true;
    auto copy=tiles;std::swap(copy[size_t(from)],copy[size_t(to)]);
    return detail_match3::inRun(r,copy,from)||detail_match3::inRun(r,copy,to);
}
// Legal swaps in stable order: ascending source cell, then right before down.
inline std::vector<std::pair<int32_t,int32_t>> match3Moves(const Match3Rules& r,const std::vector<Match3Tile>& tiles){
    std::vector<std::pair<int32_t,int32_t>> out;if(tiles.size()!=size_t(r.width*r.height))return out;
    for(int32_t c=0;c<r.width*r.height;++c)for(int32_t d:{1,3}){auto q=puzzleStep(r.width,r.height,c,d);if(q>=0&&match3Legal(r,tiles,c,q))out.push_back({c,q});}
    return out;
}
inline bool match3HasMove(const Match3Rules& r,const std::vector<Match3Tile>& tiles){
    if(tiles.size()!=size_t(r.width*r.height))return false;
    for(int32_t c=0;c<r.width*r.height;++c)for(int32_t d:{1,3}){auto q=puzzleStep(r.width,r.height,c,d);if(q>=0&&match3Legal(r,tiles,c,q))return true;}
    return false;
}
namespace detail_match3 {
// Fallback pattern ((u + offset) / 2 + v) % 3 over three palette colors: rows/columns never hold three equal
// tiles (holes only break runs), and swapping the third cell of a pair row with the cell above it completes a run.
inline std::optional<std::vector<Match3Tile>> pattern(const Match3Rules& r,bool transposed,int32_t offset,uint8_t base){
    if(!puzzleGridSize(r.width,r.height)||r.colors<3)return std::nullopt;
    std::vector<Match3Tile> t(size_t(r.width*r.height));
    for(int32_t y=0;y<r.height;++y)for(int32_t x=0;x<r.width;++x){
        const int32_t c=y*r.width+x;if(match3Hole(r,c)){t[size_t(c)]={kMatch3Blocked,Match3Special::None};continue;}
        const int32_t u=transposed?y:x,v=transposed?x:y;t[size_t(c)]={uint8_t((base+((u+offset)/2+v)%3)%r.colors+1),Match3Special::None};
    }
    if(!match3Groups(r,t).empty()||!match3HasMove(r,t))return std::nullopt;
    return t;
}
inline void gravity(const Match3Rules& r,std::vector<Match3Tile>& t){
    for(int32_t x=0;x<r.width;++x){
        int32_t write=r.height-1;
        for(int32_t y=r.height-1;y>=0;--y){
            const int32_t c=y*r.width+x;if(match3Hole(r,c))continue;
            if(t[size_t(c)].color==0)continue;
            while(match3Hole(r,write*r.width+x))--write;
            if(write!=y){t[size_t(write*r.width+x)]=t[size_t(c)];t[size_t(c)]={};}
            --write;
        }
    }
}
inline void refill(const Match3Rules& r,std::vector<Match3Tile>& t,Random& rng){
    for(int32_t x=0;x<r.width;++x)for(int32_t y=0;y<r.height;++y){
        const int32_t c=y*r.width+x;if(!match3Hole(r,c)&&t[size_t(c)].color==0)t[size_t(c)]={uint8_t(1+rng.below(r.colors)),Match3Special::None};
    }
}
inline uint8_t commonest(const Match3Rules& r,const std::vector<Match3Tile>& t,const std::vector<uint8_t>& clear){
    std::array<uint32_t,kMatch3MaxColors+1> count{};
    for(size_t c=0;c<t.size();++c)if(match3Colored(t[c])&&!clear[c])++count[t[c].color];
    uint8_t best=0;for(uint8_t k=1;k<=r.colors;++k)if(count[k]>count[best])best=k;return best;
}
// Marks the cells a special at `p` clears; newly reached specials are queued.
inline void activate(const Match3Rules& r,const std::vector<Match3Tile>& t,int32_t p,uint8_t colorHint,std::vector<uint8_t>& clear,std::vector<int32_t>& queue){
    auto mark=[&](int32_t q){if(q<0||!match3Colored(t[size_t(q)])||clear[size_t(q)])return;clear[size_t(q)]=1;if(t[size_t(q)].special!=Match3Special::None)queue.push_back(q);};
    const int32_t x=p%r.width,y=p/r.width;
    switch(t[size_t(p)].special){
    case Match3Special::StripeRow:for(int32_t k=0;k<r.width;++k)mark(y*r.width+k);break;
    case Match3Special::StripeColumn:for(int32_t k=0;k<r.height;++k)mark(k*r.width+x);break;
    case Match3Special::Bomb:for(int32_t dy=-1;dy<=1;++dy)for(int32_t dx=-1;dx<=1;++dx)if(puzzleInside(r.width,r.height,x+dx,y+dy))mark((y+dy)*r.width+x+dx);break;
    case Match3Special::ColorBomb:{
        const uint8_t color=colorHint?colorHint:commonest(r,t,clear);
        for(int32_t q=0;q<r.width*r.height;++q)if(t[size_t(q)].color==color)mark(q);
        break;
    }
    case Match3Special::None:break;
    }
}
}
// Runs cascades until the board is stable. `rng == nullptr` simulates without refill (hints, previews).
// `swapCells` = {to, from} of the triggering swap (or {-1,-1}); `extra` = cells cleared by a ColorBomb swap.
inline void match3Resolve(const Match3Rules& r,Match3State& s,std::pair<int32_t,int32_t> swapCells,std::vector<uint8_t> extra,std::pair<int32_t,uint8_t> bombHint,Random* rng,Match3Report& report){
    const size_t n=s.tiles.size();if(extra.size()!=n)extra.assign(n,0);
    for(uint32_t chain=1;chain<=kMatch3MaxCascades;++chain){
        auto groups=match3Groups(r,s.tiles);
        std::vector<uint8_t> clear=std::move(extra);extra.assign(n,0);
        bool any=false;for(auto c:clear)any=any||c;
        if(groups.empty()&&!any)break;
        std::vector<std::pair<int32_t,Match3Tile>> spawns;
        for(const auto& g:groups){
            for(auto c:g.cells)clear[size_t(c)]=1;
            for(const auto& rule:r.specials){
                if(rule.shape!=g.shape)continue;
                int32_t at=g.crossing;
                if(g.shape!=Match3Shape::Corner||at<0){
                    at=g.cells.front();
                    for(int32_t c:{swapCells.first,swapCells.second})if(c>=0&&std::binary_search(g.cells.begin(),g.cells.end(),c)){at=c;break;}
                }
                auto special=rule.special;
                if(rule.perpendicular&&(special==Match3Special::StripeRow||special==Match3Special::StripeColumn))special=g.horizontal?Match3Special::StripeColumn:Match3Special::StripeRow;
                spawns.push_back({at,{g.color,special}});break;
            }
        }
        std::vector<int32_t> queue;
        for(size_t c=0;c<n;++c)if(clear[c]&&s.tiles[c].special!=Match3Special::None)queue.push_back(int32_t(c));
        for(size_t at=0;at<queue.size();++at){
            const auto p=queue[at];const uint8_t hint=(p==bombHint.first)?bombHint.second:uint8_t(0);
            detail_match3::activate(r,s.tiles,p,hint,clear,queue);
        }
        for(const auto& [cell,tile]:spawns){(void)tile;clear[size_t(cell)]=0;}
        uint32_t cleared=0;
        for(size_t c=0;c<n;++c)if(clear[c]&&match3Colored(s.tiles[c])){++s.collected[s.tiles[c].color];s.tiles[c]={};++cleared;}
        for(const auto& [cell,tile]:spawns){s.tiles[size_t(cell)]=tile;report.created.push_back({cell,tile.special});}
        const int64_t gained=int64_t(cleared)*r.tileScore*chain;
        s.score=std::min<int64_t>(s.score+gained,INT64_MAX/4);report.score+=gained;report.cleared+=cleared;report.chains=chain;
        s.maxChain=std::max(s.maxChain,chain);swapCells={-1,-1};bombHint={-1,0};
        detail_match3::gravity(r,s.tiles);if(rng)detail_match3::refill(r,s.tiles,*rng);
    }
}
// Rearranges the playable tiles until there is no run and at least one legal move. Tries `shuffleAttempts`
// rng permutations (keeping the tile multiset and specials), then falls back to the three-color pattern, which
// valid(Match3Rules) guarantees to exist. Returns false only for invalid input.
inline bool match3Shuffle(const Match3Rules& r,std::vector<Match3Tile>& tiles,Random& rng){
    if(!valid(r)||tiles.size()!=size_t(r.width*r.height))return false;
    std::vector<int32_t> cells;for(int32_t c=0;c<r.width*r.height;++c)if(!match3Hole(r,c))cells.push_back(c);
    for(uint32_t attempt=0;attempt<r.shuffleAttempts;++attempt){
        auto copy=tiles;std::vector<Match3Tile> pool;for(auto c:cells)pool.push_back(copy[size_t(c)]);
        for(size_t i=pool.size();i>1;--i)std::swap(pool[i-1],pool[rng.below(uint32_t(i))]);
        bool empty=false;for(size_t i=0;i<cells.size();++i){copy[size_t(cells[i])]=pool[i];empty=empty||pool[i].color==0;}
        if(empty)break;
        if(match3Groups(r,copy).empty()&&match3HasMove(r,copy)){tiles=std::move(copy);return true;}
    }
    const auto base=uint8_t(rng.below(r.colors));
    for(int t=0;t<2;++t)for(int32_t o=0;o<2;++o)if(auto p=detail_match3::pattern(r,t!=0,o,base)){tiles=std::move(*p);return true;}
    return false;
}
// A fresh board with no runs and at least one legal move: row-major, each cell takes 1 + rng.below(colors) and steps
// to the next color while it would complete a run with the two cells to its left or above.
inline std::vector<Match3Tile> match3Generate(const Match3Rules& r,Random& rng){
    std::vector<Match3Tile> t;if(!valid(r))return t;t.resize(size_t(r.width*r.height));
    for(int32_t c=0;c<r.width*r.height;++c){
        if(match3Hole(r,c)){t[size_t(c)]={kMatch3Blocked,Match3Special::None};continue;}
        const int32_t x=c%r.width,y=c/r.width;uint8_t color=uint8_t(1+rng.below(r.colors));
        for(int k=0;k<r.colors;++k){
            const bool h=x>=2&&t[size_t(c-1)].color==color&&t[size_t(c-2)].color==color;
            const bool v=y>=2&&t[size_t(c-r.width)].color==color&&t[size_t(c-2*r.width)].color==color;
            if(!h&&!v)break;
            color=uint8_t(color%r.colors+1);
        }
        t[size_t(c)]={color,Match3Special::None};
    }
    if(!match3HasMove(r,t))match3Shuffle(r,t,rng);
    return t;
}
inline bool match3GoalsMet(const Match3Goals& g,const Match3State& s){
    if(s.score<g.targetScore)return false;
    for(auto [color,count]:g.collect)if(size_t(color)>=s.collected.size()||s.collected[color]<count)return false;
    return true;
}
// Starts a level: authored tiles (must be compatible, run-free) or a generated board; shuffles if stuck.
inline std::optional<Match3State> match3Begin(const Match3Level& level,Random& rng){
    if(!valid(level))return std::nullopt;
    const auto& r=level.rules;Match3State s;
    s.collected.assign(size_t(r.colors)+1,0);s.tiles=level.start.empty()?match3Generate(r,rng):level.start;
    if(!match3Compatible(r,s)||!match3Groups(r,s.tiles).empty())return std::nullopt;
    for(const auto& t:s.tiles)if(t.color==0)return std::nullopt;
    if(!match3HasMove(r,s.tiles)){if(!r.autoShuffle||!match3Shuffle(r,s.tiles,rng))return std::nullopt;++s.shuffles;}
    return s;
}
// Applies one player/bot swap. Invalid commands (stale move number, illegal swap, finished level) return nullopt
// and leave the state untouched.
inline std::optional<Match3Report> match3Apply(const Match3Level& level,Match3State& s,const Match3Swap& cmd,Random& rng){
    const auto& r=level.rules;
    if(s.status!=Match3Status::Playing||cmd.move<=s.lastMove||!match3Compatible(r,s)||!match3Legal(r,s.tiles,cmd.from,cmd.to))return std::nullopt;
    Match3Report report;std::vector<uint8_t> extra(s.tiles.size(),0);std::pair<int32_t,uint8_t> hint{-1,0};
    auto a=s.tiles[size_t(cmd.from)],b=s.tiles[size_t(cmd.to)];
    std::swap(s.tiles[size_t(cmd.from)],s.tiles[size_t(cmd.to)]);
    if(r.colorBombSwap&&(a.special==Match3Special::ColorBomb||b.special==Match3Special::ColorBomb)){
        // After the swap the bomb sits on the other cell; it clears the partner's color (both bombs: the whole board).
        const int32_t bombCell=a.special==Match3Special::ColorBomb?cmd.to:cmd.from;const auto& partner=a.special==Match3Special::ColorBomb?b:a;
        extra[size_t(bombCell)]=1;
        if(partner.special==Match3Special::ColorBomb){for(size_t c=0;c<extra.size();++c)if(match3Colored(s.tiles[c]))extra[c]=1;}
        else hint={bombCell,partner.color};
    }
    match3Resolve(r,s,{cmd.to,cmd.from},std::move(extra),hint,&rng,report);
    ++s.moves;s.lastMove=cmd.move;
    if(match3GoalsMet(level.goals,s))s.status=Match3Status::Won;
    else if(s.moves>=level.goals.moveLimit)s.status=Match3Status::Lost;
    else if(!match3HasMove(r,s.tiles)){
        if(r.autoShuffle&&match3Shuffle(r,s.tiles,rng)){report.shuffled=true;++s.shuffles;}else s.status=Match3Status::Lost;
    }
    report.status=s.status;return report;
}
enum class Match3HintMode:uint8_t {First,Best};
// Hint: First = the first legal swap in match3Moves order; Best = highest preview value (score of the
// refill-free cascade + 4 per still-needed goal tile + 25 per special created), ties to the earlier swap.
inline std::optional<std::pair<int32_t,int32_t>> match3Hint(const Match3Level& level,const Match3State& s,Match3HintMode mode=Match3HintMode::Best){
    const auto& r=level.rules;if(s.status!=Match3Status::Playing||!match3Compatible(r,s))return std::nullopt;
    auto moves=match3Moves(r,s.tiles);if(moves.empty())return std::nullopt;if(mode==Match3HintMode::First)return moves.front();
    std::optional<std::pair<int32_t,int32_t>> best;int64_t bestValue=-1;
    for(auto [from,to]:moves){
        auto copy=s;auto& t=copy.tiles;Match3Report rep;std::vector<uint8_t> extra(t.size(),0);std::pair<int32_t,uint8_t> hint{-1,0};
        auto a=t[size_t(from)],b=t[size_t(to)];std::swap(t[size_t(from)],t[size_t(to)]);
        if(r.colorBombSwap&&(a.special==Match3Special::ColorBomb||b.special==Match3Special::ColorBomb)){
            const int32_t bomb=a.special==Match3Special::ColorBomb?to:from;const auto& partner=a.special==Match3Special::ColorBomb?b:a;extra[size_t(bomb)]=1;
            if(partner.special==Match3Special::ColorBomb){for(size_t c=0;c<extra.size();++c)if(match3Colored(t[c]))extra[c]=1;}else hint={bomb,partner.color};
        }
        match3Resolve(r,copy,{to,from},std::move(extra),hint,nullptr,rep);
        int64_t value=rep.score+25*int64_t(rep.created.size());
        for(auto [color,count]:level.goals.collect){const auto before=s.collected[color],after=copy.collected[color];if(before<count)value+=4*int64_t(std::min(after,count)-before);}
        if(value>bestValue){bestValue=value;best=std::pair{from,to};}
    }
    return best;
}
}
