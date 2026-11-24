#pragma once
#include "engine/mechanics/Namespace.h"
// tactics-levels: multi-storey boards for turn tactics (XCOM-style buildings, rooftops, cliffs) on top of tactics-grid.
// A board is a stack of TacticsGrid levels of the same size plus connectors (stairs, ladders, ramps, drops, grapple
// points) between cells of different levels with their own AP cost. Each level marks which cells have a floor slab that
// blocks vertical sight. Movement: deterministic Dijkstra over (level, cell) with tactics-grid step costs and connector
// costs. Sight across levels: the line is traced on the higher level's grid and every level between must be open
// (no floor) above the lower cell. Height advantage adds hit chance per level up and lowers the target's cover by one
// step. Planning: a move plan of tentative steps that can be undone one by one or cancelled before it is committed,
// so previews and confirmations stay consistent. Integer math; record kind 216 (TacticsPlan).
#include "TacticsGrid.h"
#include "PolicyCommon.h"
namespace aiverse::lib::mechanics {
struct LevelCoord {int32_t level=0,index=0;auto operator<=>(const LevelCoord&)const=default;};
struct LevelConnector {LevelCoord a{},b{};int32_t cost=4;bool bidirectional=true;};       // cost in tactics half-units (2 = one orthogonal step)
struct LevelBoard {std::vector<TacticsGrid> levels;std::vector<std::vector<uint8_t>> floor;std::vector<LevelConnector> connectors;int32_t heightBonusMilli=100;};
inline bool valid(const LevelBoard& b){
    if(b.levels.empty()||b.levels.size()>16||b.floor.size()!=b.levels.size())return false;
    for(std::size_t i=0;i<b.levels.size();++i){if(!valid(b.levels[i])||b.levels[i].width!=b.levels[0].width||b.levels[i].height!=b.levels[0].height||b.levels[i].shape!=b.levels[0].shape
        ||b.floor[i].size()!=b.levels[i].cells.size())return false;}
    for(auto& c:b.connectors)for(auto x:{c.a,c.b})if(x.level<0||x.level>=int32_t(b.levels.size())||!tacticsInside(b.levels[0],x.index)||c.cost<1||c.cost>1000)return false;
    return b.heightBonusMilli>=0&&b.heightBonusMilli<=1000;
}
struct LevelReach {std::map<LevelCoord,int32_t> cost;std::map<LevelCoord,LevelCoord> parent;};
// occupied(level, index) -> bool: cells blocked by units.
template<class Occupied> std::optional<LevelReach> levelReachable(const LevelBoard& b,LevelCoord start,int32_t budget,Occupied&& occupied){
    if(!valid(b)||start.level<0||start.level>=int32_t(b.levels.size())||!tacticsInside(b.levels[0],start.index)||budget<0)return std::nullopt;
    LevelReach r;using Node=std::tuple<int32_t,int32_t,int32_t>;std::priority_queue<Node,std::vector<Node>,std::greater<Node>> open;
    r.cost[start]=0;open.push({0,start.level,start.index});
    std::map<LevelCoord,std::vector<std::pair<LevelCoord,int32_t>>> links;
    for(auto& c:b.connectors){links[c.a].push_back({c.b,c.cost});if(c.bidirectional)links[c.b].push_back({c.a,c.cost});}
    while(!open.empty()){auto [d,l,i]=open.top();open.pop();LevelCoord u{l,i};if(r.cost[u]!=d)continue;
        auto relax=[&](LevelCoord v,int32_t step){if(step<0)return;auto nd=d+step;if(nd>budget||occupied(v.level,v.index))return;auto it=r.cost.find(v);
            if(it==r.cost.end()||nd<it->second){r.cost[v]=nd;r.parent[v]=u;open.push({nd,v.level,v.index});}};
        const auto& g=b.levels[std::size_t(l)];
        for(auto n:tacticsNeighbors(g,i))relax({l,n},tacticsStepCost(g,i,n));
        if(auto it=links.find(u);it!=links.end())for(auto& [v,c]:it->second)relax(v,c);}
    return r;
}
inline std::vector<LevelCoord> levelPath(const LevelReach& r,LevelCoord target){std::vector<LevelCoord> p;if(!r.cost.contains(target))return p;
    for(auto c=target;;){p.push_back(c);auto it=r.parent.find(c);if(it==r.parent.end())break;c=it->second;}std::reverse(p.begin(),p.end());return p;}
inline bool levelCanSee(const LevelBoard& b,LevelCoord from,LevelCoord to,int32_t sight){
    if(!valid(b))return false;auto hi=std::max(from.level,to.level),lo=std::min(from.level,to.level);
    if(!tacticsCanSee(b.levels[std::size_t(hi)],from.index,to.index,sight))return false;
    auto lowCell=from.level<to.level?from.index:to.index;
    for(int32_t l=lo+1;l<=hi;++l)if(b.floor[std::size_t(l)][std::size_t(lowCell)])return false;
    return true;
}
// Shot preview across levels: tactics-grid preview on the attacker's level plus height advantage.
inline TacticsShot levelPreviewShot(const LevelBoard& b,const TacticsFireRules& rules,LevelCoord from,LevelCoord to,TacticsWeapon w,bool overwatch=false){
    TacticsShot s{};if(!valid(b)||!levelCanSee(b,from,to,1000))return s;
    auto& g=b.levels[std::size_t(std::max(from.level,to.level))];s=tacticsPreviewShot(g,rules,from.index,to.index,w,overwatch);
    auto up=from.level-to.level;
    if(up>0&&s.clear&&s.inRange){int64_t hit=s.hitMilli+int64_t(up)*b.heightBonusMilli;             // height: bonus per level and one cover step less
        if(s.cover.cover==TacticsCover::Half)hit+=rules.halfCoverMilli;else if(s.cover.cover==TacticsCover::Full)hit+=rules.fullCoverMilli-rules.halfCoverMilli;
        s.hitMilli=int32_t(std::clamp<int64_t>(hit,rules.minHitMilli,rules.maxHitMilli));}
    return s;
}
// ---- Move planning with undo.
struct TacticsPlan {int64_t unit=0;LevelCoord start{};std::vector<LevelCoord> steps;std::vector<int32_t> costs;int32_t budget=0;bool committed=false;};
inline int32_t planSpent(const TacticsPlan& p){int32_t s=0;for(auto c:p.costs)s+=c;return s;}
inline LevelCoord planEnd(const TacticsPlan& p){return p.steps.empty()?p.start:p.steps.back();}
template<class Occupied> bool planStep(const LevelBoard& b,TacticsPlan& p,LevelCoord target,Occupied&& occupied){
    if(p.committed)return false;auto r=levelReachable(b,planEnd(p),p.budget-planSpent(p),occupied);if(!r||!r->cost.contains(target)||target==planEnd(p))return false;
    p.steps.push_back(target);p.costs.push_back(r->cost.at(target));return true;}
inline bool planUndo(TacticsPlan& p){if(p.committed||p.steps.empty())return false;p.steps.pop_back();p.costs.pop_back();return true;}
inline void planCancel(TacticsPlan& p){if(!p.committed){p.steps.clear();p.costs.clear();}}
inline std::optional<LevelCoord> planCommit(TacticsPlan& p){if(p.committed||p.steps.empty())return std::nullopt;p.committed=true;return planEnd(p);}
inline void io(Archive& a,LevelCoord& c){a(c.level,c.index);}
inline void io(Archive& a,TacticsPlan& p){a(p.unit,p.start,p.steps,p.costs,p.budget,p.committed);}
inline uint16_t stateKind(const TacticsPlan&){return 216;}
inline bool valid(const TacticsPlan& p){return p.steps.size()==p.costs.size()&&p.budget>=0&&planSpent(p)<=p.budget;}
}
