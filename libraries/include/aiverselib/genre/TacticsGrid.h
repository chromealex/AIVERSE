#pragma once
// tactics-grid: square and hex boards for turn strategy. Weighted movement with a deterministic reachable set and
// paths, half/full cover from the owner grid with a flank angle, integer line of fire and shot previews (hit chance
// in integer milli). Hex boards use axial coordinates (q = x, r = y) stored as a width x height parallelogram.
// Square boards move in 8 directions (orthogonal step = 2 half-units x cell cost, diagonal = 3, no corner cutting);
// hex steps cost 2 x cell cost. Integer math only.
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <queue>
#include <span>
#include <vector>
namespace aiverse::lib::genre {
enum class TacticsShape:uint8_t{Square,Hex};
enum class TacticsCover:uint8_t{None,Half,Full};
inline constexpr int32_t kTacticsMaxSide=256,kTacticsStep=2,kTacticsMaxCost=50,kTacticsUnreachable=-1;
struct TacticsCell{
    uint8_t cost=1;                          // movement cost multiplier; 0 = impassable
    TacticsCover cover=TacticsCover::None;   // cover this cell gives to units standing next to it
    bool blocksSight=false;                  // blocks line of fire/sight through it
    bool operator==(const TacticsCell&)const=default;
};
struct TacticsGrid{TacticsShape shape=TacticsShape::Square;int32_t width=0,height=0;std::vector<TacticsCell> cells;bool operator==(const TacticsGrid&)const=default;};
struct TacticsCoord{int32_t x=0,y=0;bool operator==(const TacticsCoord&)const=default;};
inline bool valid(const TacticsGrid& g){
    if(g.width<1||g.height<1||g.width>kTacticsMaxSide||g.height>kTacticsMaxSide||uint8_t(g.shape)>uint8_t(TacticsShape::Hex)||g.cells.size()!=std::size_t(g.width)*std::size_t(g.height))return false;
    for(const auto& c:g.cells)if(c.cost>kTacticsMaxCost||uint8_t(c.cover)>uint8_t(TacticsCover::Full))return false;
    return true;
}
inline int32_t tacticsCellCount(const TacticsGrid& g){return g.width*g.height;}
inline int32_t tacticsIndex(const TacticsGrid& g,TacticsCoord c){return c.x<0||c.y<0||c.x>=g.width||c.y>=g.height?-1:c.y*g.width+c.x;}
inline TacticsCoord tacticsCoord(const TacticsGrid& g,int32_t index){return {index%g.width,index/g.width};}
inline bool tacticsInside(const TacticsGrid& g,int32_t index){return index>=0&&index<tacticsCellCount(g);}

namespace detail_tactics {
inline constexpr std::array<TacticsCoord,8> kSquareDirs{{{1,0},{0,1},{-1,0},{0,-1},{1,1},{-1,1},{-1,-1},{1,-1}}};
inline constexpr std::array<TacticsCoord,6> kHexDirs{{{1,0},{1,-1},{0,-1},{-1,0},{-1,1},{0,1}}};
inline int64_t iabs(int64_t v){return v<0?-v:v;}
inline uint64_t isqrt(uint64_t v){uint64_t r=0,bit=uint64_t(1)<<62;while(bit>v)bit>>=2;while(bit){if(v>=r+bit){v-=r+bit;r=(r>>1)+bit;}else r>>=1;bit>>=2;}return r;}
// Rounded integer division (ties away from zero); callers add a nudge so exact ties never occur on lines.
inline int64_t roundDiv(int64_t a,int64_t b){return a>=0?(a+b/2)/b:-((-a+b/2)/b);}
}
// Planar centre in milli-units (square: 1000 per cell; hex: neighbouring centres are 2000 apart).
inline std::array<int64_t,2> tacticsPlanar(const TacticsGrid& g,int32_t index){
    auto c=tacticsCoord(g,index);
    if(g.shape==TacticsShape::Square)return {int64_t(c.x)*1000,int64_t(c.y)*1000};
    return {int64_t(2*c.x+c.y)*1000,int64_t(c.y)*1732};
}
// Grid distance in steps: Chebyshev on squares, hex distance on hexes.
inline int32_t tacticsDistance(const TacticsGrid& g,int32_t a,int32_t b){
    auto p=tacticsCoord(g,a),q=tacticsCoord(g,b);int32_t dx=q.x-p.x,dy=q.y-p.y;
    if(g.shape==TacticsShape::Square)return std::max(dx<0?-dx:dx,dy<0?-dy:dy);
    int32_t dz=-dx-dy;return (std::abs(dx)+std::abs(dy)+std::abs(dz))/2;
}
// Neighbours in a fixed direction order; `orthogonalOnly` drops square diagonals (used for cover).
inline std::vector<int32_t> tacticsNeighbors(const TacticsGrid& g,int32_t index,bool orthogonalOnly=false){
    std::vector<int32_t> out;auto c=tacticsCoord(g,index);
    if(g.shape==TacticsShape::Hex){for(auto d:detail_tactics::kHexDirs){auto n=tacticsIndex(g,{c.x+d.x,c.y+d.y});if(n>=0)out.push_back(n);}return out;}
    for(std::size_t i=0;i<(orthogonalOnly?4u:8u);++i){auto d=detail_tactics::kSquareDirs[i];auto n=tacticsIndex(g,{c.x+d.x,c.y+d.y});if(n>=0)out.push_back(n);}
    return out;
}
// Step cost in half-units between neighbours, or -1 if the step is not allowed (impassable or cutting a corner).
inline int32_t tacticsStepCost(const TacticsGrid& g,int32_t from,int32_t to){
    const auto& cell=g.cells[std::size_t(to)];if(!cell.cost)return -1;
    if(g.shape==TacticsShape::Hex)return kTacticsStep*cell.cost;
    auto a=tacticsCoord(g,from),b=tacticsCoord(g,to);
    if(a.x!=b.x&&a.y!=b.y){
        if(!g.cells[std::size_t(tacticsIndex(g,{b.x,a.y}))].cost||!g.cells[std::size_t(tacticsIndex(g,{a.x,b.y}))].cost)return -1;
        return 3*cell.cost;
    }
    return kTacticsStep*cell.cost;
}

// Occupancy for movement: 0 free, 1 pass-through only (allies), 2 blocked (enemies, objects).
inline constexpr uint8_t kTacticsFree=0,kTacticsPassOnly=1,kTacticsBlocked=2;
struct TacticsReach{
    int32_t start=-1;std::vector<int32_t> cost,parent; // cost in half-units, -1 unreachable
    bool canStop(int32_t cell,std::span<const uint8_t> occupancy)const{return cell>=0&&std::size_t(cell)<cost.size()&&cost[std::size_t(cell)]>=0&&cell!=start&&occupancy[std::size_t(cell)]==kTacticsFree;}
};
// Dijkstra within `budget` half-units. Nodes settle in (cost, cell index) order and a parent is replaced only by a
// strictly cheaper route, so equal-cost ties always resolve to the same path on every platform.
inline std::optional<TacticsReach> tacticsReachable(const TacticsGrid& g,int32_t start,int32_t budget,std::span<const uint8_t> occupancy){
    if(!valid(g)||!tacticsInside(g,start)||budget<0||occupancy.size()!=g.cells.size())return std::nullopt;
    TacticsReach r;r.start=start;r.cost.assign(g.cells.size(),-1);r.parent.assign(g.cells.size(),-1);
    using Node=std::pair<int32_t,int32_t>;std::priority_queue<Node,std::vector<Node>,std::greater<Node>> open;
    r.cost[std::size_t(start)]=0;open.push({0,start});std::vector<uint8_t> done(g.cells.size(),0);
    while(!open.empty()){
        auto [c,u]=open.top();open.pop();if(done[std::size_t(u)])continue;done[std::size_t(u)]=1;
        for(auto v:tacticsNeighbors(g,u)){
            if(occupancy[std::size_t(v)]==kTacticsBlocked)continue;
            auto step=tacticsStepCost(g,u,v);if(step<0)continue;auto nc=c+step;
            if(nc>budget)continue;
            auto& best=r.cost[std::size_t(v)];if(best>=0&&best<=nc)continue;
            best=nc;r.parent[std::size_t(v)]=u;open.push({nc,v});
        }
    }
    return r;
}
// Cells from the step after start to `target` (empty if unreachable or target == start).
inline std::vector<int32_t> tacticsPath(const TacticsReach& r,int32_t target){
    std::vector<int32_t> out;if(target<0||std::size_t(target)>=r.cost.size()||r.cost[std::size_t(target)]<0||target==r.start)return out;
    for(int32_t c=target;c!=r.start&&c>=0;c=r.parent[std::size_t(c)])out.push_back(c);
    std::reverse(out.begin(),out.end());return out;
}

// ---- Line of fire: cells sampled along the centre-to-centre segment, one per grid step, with a fixed nudge
// (deterministic at exact ties). Intermediate cells that block sight block the line; the end cells never do.
struct TacticsLine{std::vector<int32_t> cells;bool blocked=false;int32_t blocker=-1;};
inline TacticsLine tacticsLine(const TacticsGrid& g,int32_t from,int32_t to){
    using detail_tactics::roundDiv;TacticsLine out;
    auto n=tacticsDistance(g,from,to);if(!n)return out;
    auto a=tacticsCoord(g,from),b=tacticsCoord(g,to);constexpr int64_t K=1000;
    for(int32_t i=1;i<=n;++i){
        int64_t sx=(int64_t(a.x)*(n-i)+int64_t(b.x)*i)*K+1,sy=(int64_t(a.y)*(n-i)+int64_t(b.y)*i)*K+2,d=int64_t(n)*K;
        TacticsCoord c;
        if(g.shape==TacticsShape::Square)c={int32_t(roundDiv(sx,d)),int32_t(roundDiv(sy,d))};
        else{ // cube rounding: fix the component with the largest rounding error
            int64_t sz=-(sx-1)-(sy-2)-3;auto rx=roundDiv(sx,d),ry=roundDiv(sy,d),rz=roundDiv(sz,d);
            auto ex=detail_tactics::iabs(rx*d-sx),ey=detail_tactics::iabs(ry*d-sy),ez=detail_tactics::iabs(rz*d-sz);
            if(ex>ey&&ex>ez)rx=-ry-rz;else if(ey>ez)ry=-rx-rz;
            c={int32_t(rx),int32_t(ry)};
        }
        auto idx=tacticsIndex(g,c);if(idx<0)idx=to;
        if(out.cells.empty()||out.cells.back()!=idx)out.cells.push_back(idx);
    }
    if(out.cells.empty()||out.cells.back()!=to)out.cells.push_back(to);
    for(std::size_t i=0;i+1<out.cells.size();++i)if(g.cells[std::size_t(out.cells[i])].blocksSight){out.blocked=true;out.blocker=out.cells[i];break;}
    return out;
}

// ---- Cover and flanking. A cover cell next to the target protects against attackers within `coverCosMilli`
// (cosine of the half-angle, 707 = 45 degrees) of the direction from the target to that cell. A target next to
// cover but outside every arc is flanked; a target with no adjacent cover is exposed.
struct TacticsCoverInfo{TacticsCover cover=TacticsCover::None;bool flanked=false,exposed=true;bool operator==(const TacticsCoverInfo&)const=default;};
inline TacticsCoverInfo tacticsCoverAgainst(const TacticsGrid& g,int32_t target,int32_t attacker,int32_t coverCosMilli=707){
    using namespace detail_tactics;TacticsCoverInfo out;
    if(!tacticsInside(g,target)||!tacticsInside(g,attacker)||target==attacker)return out;
    auto t=tacticsPlanar(g,target),a=tacticsPlanar(g,attacker);int64_t vx=a[0]-t[0],vy=a[1]-t[1];
    auto vl=int64_t(isqrt(uint64_t(vx*vx+vy*vy)));
    for(auto n:tacticsNeighbors(g,target,true)){
        auto level=g.cells[std::size_t(n)].cover;if(level==TacticsCover::None)continue;
        out.exposed=false;auto p=tacticsPlanar(g,n);int64_t ux=p[0]-t[0],uy=p[1]-t[1];
        auto ul=int64_t(isqrt(uint64_t(ux*ux+uy*uy)));auto dot=ux*vx+uy*vy;
        if(dot>0&&dot*1000>=int64_t(coverCosMilli)*ul*vl&&uint8_t(level)>uint8_t(out.cover))out.cover=level;
    }
    out.flanked=!out.exposed&&out.cover==TacticsCover::None;
    return out;
}
struct TacticsFireRules{
    int32_t halfCoverMilli=200,fullCoverMilli=400,flankBonusMilli=150,rangePenaltyMilli=40,overwatchPenaltyMilli=150;
    int32_t minHitMilli=50,maxHitMilli=950,coverCosMilli=707;
    bool operator==(const TacticsFireRules&)const=default;
};
inline bool valid(const TacticsFireRules& r){
    auto m=[](int32_t v){return v>=0&&v<=1000;};
    return m(r.halfCoverMilli)&&m(r.fullCoverMilli)&&r.halfCoverMilli<=r.fullCoverMilli&&m(r.flankBonusMilli)&&m(r.rangePenaltyMilli)&&m(r.overwatchPenaltyMilli)
        &&m(r.minHitMilli)&&m(r.maxHitMilli)&&r.minHitMilli<=r.maxHitMilli&&r.coverCosMilli>=0&&r.coverCosMilli<=1000;
}
struct TacticsWeapon{int32_t aimMilli=700,optimalRange=5,maxRange=8;};
struct TacticsShot{TacticsLine line;TacticsCoverInfo cover;int32_t distance=0,hitMilli=0;bool inRange=false,clear=false;};
// What the attacker would see in a preview; hitMilli is 0 when the line is blocked or out of range.
inline TacticsShot tacticsPreviewShot(const TacticsGrid& g,const TacticsFireRules& r,int32_t from,int32_t to,TacticsWeapon w,bool overwatch=false){
    TacticsShot s;if(!tacticsInside(g,from)||!tacticsInside(g,to)||from==to)return s;
    s.line=tacticsLine(g,from,to);s.cover=tacticsCoverAgainst(g,to,from,r.coverCosMilli);s.distance=tacticsDistance(g,from,to);
    s.inRange=s.distance<=w.maxRange;s.clear=!s.line.blocked;if(!s.inRange||!s.clear)return s;
    int64_t hit=w.aimMilli;
    if(s.cover.cover==TacticsCover::Half)hit-=r.halfCoverMilli;else if(s.cover.cover==TacticsCover::Full)hit-=r.fullCoverMilli;
    if(s.cover.flanked)hit+=r.flankBonusMilli;
    if(s.distance>w.optimalRange)hit-=int64_t(r.rangePenaltyMilli)*(s.distance-w.optimalRange);
    if(overwatch)hit-=r.overwatchPenaltyMilli;
    s.hitMilli=int32_t(std::clamp<int64_t>(hit,r.minHitMilli,r.maxHitMilli));return s;
}
inline bool tacticsCanSee(const TacticsGrid& g,int32_t from,int32_t to,int32_t sight){
    if(!tacticsInside(g,from)||!tacticsInside(g,to))return false;
    if(from==to)return true;
    return tacticsDistance(g,from,to)<=sight&&!tacticsLine(g,from,to).blocked;
}
}
