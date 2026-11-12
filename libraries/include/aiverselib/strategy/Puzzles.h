#pragma once
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <queue>
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
}
