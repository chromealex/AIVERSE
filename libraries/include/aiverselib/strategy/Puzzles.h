#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <queue>
#include <set>
#include <tuple>
#include <cstdlib>
#include <vector>

namespace aiverse::lib::strategy {
struct PushBoard {int32_t width=0,height=0;std::vector<uint8_t> walls;std::vector<int32_t> goals;};
struct PushState {int32_t player=0;std::vector<int32_t> boxes;bool operator==(const PushState&)const=default;};
inline bool valid(const PushBoard& b,const PushState& s) {
    if(b.width<1||b.width>64||b.height<1||b.height>64||b.walls.size()!=size_t(b.width*b.height)||s.boxes.size()!=b.goals.size()||s.boxes.empty()||s.boxes.size()>256)return false;
    auto free=[&](int32_t p){return p>=0&&size_t(p)<b.walls.size()&&!b.walls[size_t(p)];};
    if(!free(s.player)||!std::is_sorted(s.boxes.begin(),s.boxes.end())||std::adjacent_find(s.boxes.begin(),s.boxes.end())!=s.boxes.end()||std::binary_search(s.boxes.begin(),s.boxes.end(),s.player))return false;
    for(auto p:s.boxes)if(!free(p))return false;
    for(auto p:b.goals)if(!free(p))return false;
    auto goals=b.goals;std::sort(goals.begin(),goals.end());return std::adjacent_find(goals.begin(),goals.end())==goals.end();
}
inline bool pushSolved(const PushBoard& b,const PushState& s){if(!valid(b,s))return false;auto goals=b.goals;std::sort(goals.begin(),goals.end());return goals==s.boxes;}
// Directions are left, right, up, down. No wrapping across row boundaries.
inline bool pushMove(const PushBoard& b,PushState& s,int32_t direction) {
    if(!valid(b,s)||direction<0||direction>3)return false;
    constexpr int32_t dx[]{-1,1,0,0},dy[]{0,0,-1,1};
    auto next=[&](int32_t p)->std::optional<int32_t>{int32_t x=p%b.width+dx[direction],y=p/b.width+dy[direction];if(x<0||x>=b.width||y<0||y>=b.height)return {};int32_t q=y*b.width+x;if(b.walls[size_t(q)])return {};return q;};
    auto target=next(s.player);if(!target)return false;
    auto box=std::lower_bound(s.boxes.begin(),s.boxes.end(),*target);
    if(box!=s.boxes.end()&&*box==*target){auto dest=next(*target);if(!dest||std::binary_search(s.boxes.begin(),s.boxes.end(),*dest))return false;*box=*dest;std::sort(s.boxes.begin(),s.boxes.end());}
    s.player=*target;return true;
}
struct PushHistory {PushState current;std::vector<PushState> past,future;};
inline bool pushMove(const PushBoard& b,PushHistory& s,int32_t direction,size_t capacity=256) {
    if(capacity==0||capacity>4096||s.past.size()>capacity||s.future.size()>capacity)return false;
    auto next=s.current;if(!pushMove(b,next,direction))return false;
    if(s.past.size()==capacity)s.past.erase(s.past.begin());
    s.past.push_back(s.current);s.current=std::move(next);s.future.clear();return true;
}
inline bool pushUndo(const PushBoard& b,PushHistory& s) {
    if(s.past.empty()||s.future.size()>=4096||!valid(b,s.current)||!valid(b,s.past.back()))return false;
    s.future.push_back(s.current);s.current=std::move(s.past.back());s.past.pop_back();return true;
}
inline bool pushRedo(const PushBoard& b,PushHistory& s) {
    if(s.future.empty()||s.past.size()>=4096||!valid(b,s.current)||!valid(b,s.future.back()))return false;
    s.past.push_back(s.current);s.current=std::move(s.future.back());s.future.pop_back();return true;
}
enum class SolveStatus {Solved,Unsolvable,BudgetExceeded,Invalid};
struct PushSolution {SolveStatus status=SolveStatus::Invalid;std::vector<int32_t> moves;size_t expanded=0;};
inline PushSolution solvePush(const PushBoard& b,const PushState& start,size_t budget=10000) {
    PushSolution out;if(!valid(b,start)||b.walls.size()>256||start.boxes.size()>16||budget==0||budget>100000)return out;
    struct Entry {PushState state;size_t parent;int32_t move;};
    auto key=[](const PushState& s){auto k=s.boxes;k.push_back(s.player);return k;};
    std::vector<Entry> entries{{start,0,-1}};std::map<std::vector<int32_t>,size_t> seen{{key(start),0}};size_t cursor=0;bool exhausted=false;
    while(cursor<entries.size()){
        const auto current=entries[cursor];
        if(pushSolved(b,current.state)){
            out.status=SolveStatus::Solved;size_t at=cursor;
            while(entries[at].move!=-1){out.moves.push_back(entries[at].move);at=entries[at].parent;}
            std::reverse(out.moves.begin(),out.moves.end());return out;
        }
        ++out.expanded;
        for(int32_t d=0;d<4;++d){
            auto s=current.state;if(!pushMove(b,s,d))continue;auto k=key(s);if(seen.contains(k))continue;
            if(entries.size()==budget){exhausted=true;continue;}
            seen.emplace(std::move(k),entries.size());entries.push_back({std::move(s),cursor,d});
        }
        ++cursor;
    }
    out.status=exhausted?SolveStatus::BudgetExceeded:SolveStatus::Unsolvable;return out;
}
// ---- 0.2.0: deadlock detection, A* search with a node budget and hints.
// Simple deadlocks: free cells from which no push sequence (ignoring other boxes) brings a box onto any goal.
// Computed by reverse pushes from every goal; corners and dead wall runs are included. Empty on an invalid board.
inline std::vector<uint8_t> pushDeadCells(const PushBoard& b) {
    std::vector<uint8_t> dead;
    if(b.width<1||b.width>64||b.height<1||b.height>64||b.walls.size()!=size_t(b.width*b.height))return dead;
    const size_t n=b.walls.size();std::vector<uint8_t> live(n,0);std::vector<int32_t> queue;
    auto free=[&](int32_t x,int32_t y){return x>=0&&y>=0&&x<b.width&&y<b.height&&!b.walls[size_t(y*b.width+x)];};
    for(auto g:b.goals)if(g>=0&&size_t(g)<n&&!b.walls[size_t(g)]&&!live[size_t(g)]){live[size_t(g)]=1;queue.push_back(g);}
    constexpr int32_t dx[]{-1,1,0,0},dy[]{0,0,-1,1};
    for(size_t at=0;at<queue.size();++at){
        const int32_t x=queue[at]%b.width,y=queue[at]/b.width;
        // The box came from (x-dx,y-dy), pushed by a player standing at (x-2dx,y-2dy).
        for(int d=0;d<4;++d){
            const int32_t px=x-dx[d],py=y-dy[d];if(!free(px,py)||!free(px-dx[d],py-dy[d]))continue;
            const int32_t q=py*b.width+px;if(!live[size_t(q)]){live[size_t(q)]=1;queue.push_back(q);}
        }
    }
    dead.resize(n);for(size_t i=0;i<n;++i)dead[i]=uint8_t(!b.walls[i]&&!live[i]);return dead;
}
namespace detail_push {
struct FreezeProbe {const PushBoard& b;const std::vector<uint8_t>& dead;const std::vector<int32_t>& boxes;std::vector<uint8_t> asWall;uint32_t calls=0;};
inline bool wallish(FreezeProbe& f,int32_t x,int32_t y){return x<0||y<0||x>=f.b.width||y>=f.b.height||f.b.walls[size_t(y*f.b.width+x)]||f.asWall[size_t(y*f.b.width+x)];}
// Box p cannot move along axis (0 horizontal, 1 vertical): a wall on one side, dead cells on both sides,
// or a neighbouring box that is itself blocked on the other axis while p is treated as a wall.
inline bool blocked(FreezeProbe& f,int32_t p,int axis) {
    if(++f.calls>4096)return false; // conservative: never claim a deadlock we did not prove
    const int32_t x=p%f.b.width,y=p/f.b.width,ax=axis==0?1:0,ay=axis==0?0:1;
    if(wallish(f,x-ax,y-ay)||wallish(f,x+ax,y+ay))return true;
    const int32_t q1=(y-ay)*f.b.width+x-ax,q2=(y+ay)*f.b.width+x+ax;
    if(f.dead[size_t(q1)]&&f.dead[size_t(q2)])return true;
    f.asWall[size_t(p)]=1;bool out=false;
    for(int32_t q:{q1,q2})if(std::binary_search(f.boxes.begin(),f.boxes.end(),q)&&blocked(f,q,1-axis)){out=true;break;}
    f.asWall[size_t(p)]=0;return out;
}
}
// True when the position provably cannot be solved: a box off-goal on a simple-deadlock cell, or a frozen
// box (blocked on both axes, including clusters of boxes) that is not on a goal. Sound, not complete.
inline bool pushDeadlocked(const PushBoard& b,const PushState& s,const std::vector<uint8_t>& dead) {
    if(!valid(b,s)||dead.size()!=b.walls.size())return false;
    auto goals=b.goals;std::sort(goals.begin(),goals.end());
    detail_push::FreezeProbe f{b,dead,s.boxes,std::vector<uint8_t>(b.walls.size(),0)};
    for(auto p:s.boxes){
        if(std::binary_search(goals.begin(),goals.end(),p))continue;
        if(dead[size_t(p)])return true;
        f.calls=0;if(detail_push::blocked(f,p,0)&&detail_push::blocked(f,p,1))return true;
    }
    return false;
}
inline bool pushDeadlocked(const PushBoard& b,const PushState& s){return pushDeadlocked(b,s,pushDeadCells(b));}
// Shortest move sequence by A* (heuristic: sum of each box's Manhattan distance to its nearest goal; consistent)
// with deadlock pruning. `budget` caps expanded nodes: BudgetExceeded is "unknown", never a proof.
// Ties: lower f, then lower h, then earlier insertion; directions are tried 0..3. Up to 1024 cells / 32 boxes.
inline PushSolution solvePushAStar(const PushBoard& b,const PushState& start,size_t budget=100000) {
    PushSolution out;if(!valid(b,start)||b.walls.size()>1024||start.boxes.size()>32||budget==0||budget>2000000)return out;
    const auto dead=pushDeadCells(b);
    auto h=[&](const PushState& s)->int64_t{
        int64_t total=0;
        for(auto p:s.boxes){int64_t best=INT32_MAX;for(auto g:b.goals){int64_t d=std::abs(p%b.width-g%b.width)+std::abs(p/b.width-g/b.width);best=std::min(best,d);}total+=best;}
        return total;
    };
    if(pushDeadlocked(b,start,dead)){out.status=SolveStatus::Unsolvable;return out;}
    struct Node {PushState state;size_t parent;int32_t move;uint32_t g;};
    auto key=[](const PushState& s){auto k=s.boxes;k.push_back(s.player);return k;};
    std::vector<Node> nodes{{start,0,-1,0}};std::map<std::vector<int32_t>,uint32_t> best{{key(start),0}};
    std::set<std::tuple<int64_t,int64_t,size_t>> open{{h(start),h(start),0}};
    while(!open.empty()){
        const auto [f,hv,index]=*open.begin();(void)f;(void)hv;open.erase(open.begin());
        const Node current=nodes[index];
        if(best[key(current.state)]<current.g)continue;
        if(pushSolved(b,current.state)){
            out.status=SolveStatus::Solved;size_t at=index;
            while(nodes[at].move!=-1){out.moves.push_back(nodes[at].move);at=nodes[at].parent;}
            std::reverse(out.moves.begin(),out.moves.end());return out;
        }
        if(out.expanded==budget){out.status=SolveStatus::BudgetExceeded;return out;}
        ++out.expanded;
        for(int32_t d=0;d<4;++d){
            auto s=current.state;if(!pushMove(b,s,d))continue;
            if(s.boxes!=current.state.boxes&&pushDeadlocked(b,s,dead))continue;
            auto k=key(s);const uint32_t g=current.g+1;auto it=best.find(k);if(it!=best.end()&&it->second<=g)continue;
            best[k]=g;const auto hs=h(s);nodes.push_back({std::move(s),index,d,g});open.insert({int64_t(g)+hs,hs,nodes.size()-1});
        }
    }
    out.status=SolveStatus::Unsolvable;return out;
}
// The first move of an A* solution; nullopt when solved, invalid, unsolvable or unknown within the budget.
inline std::optional<int32_t> pushHint(const PushBoard& b,const PushState& s,size_t budget=100000) {
    if(pushSolved(b,s))return std::nullopt;
    auto solution=solvePushAStar(b,s,budget);
    if(solution.status!=SolveStatus::Solved||solution.moves.empty())return std::nullopt;
    return solution.moves.front();
}
}
