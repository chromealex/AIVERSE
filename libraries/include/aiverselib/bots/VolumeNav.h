#pragma once
// volume-nav: navigation in free 3D space for flyers, ships, drones and swimmers. A bounded occupancy grid built from
// box obstacles (inflated by the agent radius), deterministic A* over 26 neighbours with integer costs (10/14/17),
// line-of-sight smoothing by a 3D grid walk, boids-style local avoidance (separation, alignment, cohesion, goal seek,
// obstacle push) and 3D formation slots rotated by the leader's yaw and pitch. Fixed math, ascending ids.
// The grid is static data owned by the caller; rebuild it (or the affected boxes) when obstacles change.
#include <aiverselib/mechanics/PolicyCommon.h>
#include "engine/world/SpatialWorld.h"
#include <array>
#include <queue>
namespace aiverse::lib::bots {
using namespace aiverse::lib::mechanics;
struct VolumeGrid {Vec3 origin{};Fixed cell=2_fx;int32_t nx=0,ny=0,nz=0;std::vector<uint8_t> blocked;   // index = x + nx*(y + ny*z)
    std::size_t index(int32_t x,int32_t y,int32_t z)const{return std::size_t(x)+std::size_t(nx)*(std::size_t(y)+std::size_t(ny)*std::size_t(z));}
    bool inside(int32_t x,int32_t y,int32_t z)const{return x>=0&&y>=0&&z>=0&&x<nx&&y<ny&&z<nz;}
    bool solid(int32_t x,int32_t y,int32_t z)const{return !inside(x,y,z)||blocked[index(x,y,z)];}};
struct VolumeCell {int32_t x=0,y=0,z=0;auto operator<=>(const VolumeCell&)const=default;};
inline constexpr std::size_t kVolumeMaxCells=1u<<22;
inline std::optional<VolumeCell> volumeCellOf(const VolumeGrid& g,Vec3 p){auto r=(p-g.origin)/g.cell;VolumeCell c{int32_t(r.x.floorInt()),int32_t(r.y.floorInt()),int32_t(r.z.floorInt())};
    if(!g.inside(c.x,c.y,c.z))return std::nullopt;return c;}
inline Vec3 volumeCenter(const VolumeGrid& g,VolumeCell c){return g.origin+Vec3{g.cell*c.x,g.cell*c.y,g.cell*c.z}+Vec3{g.cell/2,g.cell/2,g.cell/2};}
// Build from obstacle boxes inflated by `radius`. Returns nullopt for invalid sizes.
inline std::optional<VolumeGrid> buildVolumeGrid(const world::Bounds& area,Fixed cell,Fixed radius,std::span<const world::Bounds> obstacles){
    if(cell<=Fixed{}||radius<Fixed{}||!policyBounded(area.min)||!policyBounded(area.max))return std::nullopt;
    VolumeGrid g;g.origin=area.min;g.cell=cell;auto size=area.max-area.min;
    g.nx=int32_t((size.x/cell).toInt());g.ny=int32_t((size.y/cell).toInt());g.nz=int32_t((size.z/cell).toInt());
    if(g.nx<1||g.ny<1||g.nz<1||std::size_t(g.nx)*std::size_t(g.ny)*std::size_t(g.nz)>kVolumeMaxCells)return std::nullopt;
    g.blocked.assign(std::size_t(g.nx)*g.ny*g.nz,0);
    for(const auto& b:obstacles){Vec3 lo=b.min-Vec3{radius,radius,radius},hi=b.max+Vec3{radius,radius,radius};
        auto c0=(lo-g.origin)/cell,c1=(hi-g.origin)/cell;
        int32_t x0=std::max<int32_t>(0,int32_t(c0.x.floorInt())),y0=std::max<int32_t>(0,int32_t(c0.y.floorInt())),z0=std::max<int32_t>(0,int32_t(c0.z.floorInt()));
        int32_t x1=std::min<int32_t>(g.nx-1,int32_t(c1.x.floorInt())),y1=std::min<int32_t>(g.ny-1,int32_t(c1.y.floorInt())),z1=std::min<int32_t>(g.nz-1,int32_t(c1.z.floorInt()));
        for(int32_t z=z0;z<=z1;++z)for(int32_t y=y0;y<=y1;++y)for(int32_t x=x0;x<=x1;++x)g.blocked[g.index(x,y,z)]=1;}
    return g;
}
// Straight segment through free cells only (supercover walk sampled at quarter cells).
inline bool volumeLineClear(const VolumeGrid& g,Vec3 a,Vec3 b){
    auto d=b-a;auto len=d.length();int64_t steps=std::max<int64_t>(1,(len/(g.cell/4)).toInt()+1);
    for(int64_t i=0;i<=steps;++i){auto p=a+d*Fixed::fromRatio(i,steps);auto c=volumeCellOf(g,p);if(!c||g.solid(c->x,c->y,c->z))return false;}
    return true;
}
struct VolumePath {std::vector<Vec3> points;uint32_t expanded=0;bool partial=false;};
// A* from start to goal (cell centres, then the exact goal). maxExpanded bounds the work; when exhausted the path to the
// expanded cell nearest to the goal is returned with partial=true.
inline std::optional<VolumePath> findVolumePath(const VolumeGrid& g,Vec3 start,Vec3 goal,uint32_t maxExpanded=200000,bool smooth=true){
    auto s=volumeCellOf(g,start),t=volumeCellOf(g,goal);if(!s||!t||g.solid(s->x,s->y,s->z)||g.solid(t->x,t->y,t->z))return std::nullopt;
    auto h=[&](VolumeCell c){int64_t dx=std::abs(c.x-t->x),dy=std::abs(c.y-t->y),dz=std::abs(c.z-t->z);
        int64_t a=std::max({dx,dy,dz}),cc=std::min({dx,dy,dz}),b=dx+dy+dz-a-cc;return 17*cc+14*(b-cc)+10*(a-b);};
    using Node=std::tuple<int64_t,int64_t,std::size_t>;   // f, g, index (deterministic tie-break by index)
    std::priority_queue<Node,std::vector<Node>,std::greater<Node>> open;std::map<std::size_t,int64_t> best;std::map<std::size_t,std::size_t> parent;
    auto si=g.index(s->x,s->y,s->z),ti=g.index(t->x,t->y,t->z);best[si]=0;open.push({h(*s),0,si});
    auto cellOf=[&](std::size_t i){VolumeCell c;c.x=int32_t(i%std::size_t(g.nx));c.y=int32_t((i/std::size_t(g.nx))%std::size_t(g.ny));c.z=int32_t(i/(std::size_t(g.nx)*g.ny));return c;};
    VolumePath out;std::size_t nearest=si;int64_t nearestH=h(*s);bool found=false;
    while(!open.empty()){auto [f,gc,i]=open.top();open.pop();if(best[i]!=gc)continue;
        if(i==ti){found=true;break;}if(++out.expanded>maxExpanded)break;
        auto c=cellOf(i);auto hc=h(c);if(hc<nearestH||(hc==nearestH&&i<nearest)){nearest=i;nearestH=hc;}
        for(int dz=-1;dz<=1;++dz)for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){if(!dx&&!dy&&!dz)continue;
            VolumeCell n{c.x+dx,c.y+dy,c.z+dz};if(g.solid(n.x,n.y,n.z))continue;
            int axes=(dx!=0)+(dy!=0)+(dz!=0);
            if(axes>1&&((dx&&g.solid(c.x+dx,c.y,c.z))||(dy&&g.solid(c.x,c.y+dy,c.z))||(dz&&g.solid(c.x,c.y,c.z+dz))))continue;   // no corner cutting
            int64_t cost=gc+(axes==1?10:axes==2?14:17);auto ni=g.index(n.x,n.y,n.z);auto it=best.find(ni);
            if(it==best.end()||cost<it->second){best[ni]=cost;parent[ni]=i;open.push({cost+h(n),cost,ni});}}
    }
    auto end=found?ti:nearest;out.partial=!found;
    std::vector<Vec3> raw;for(auto i=end;;){raw.push_back(volumeCenter(g,cellOf(i)));if(i==si)break;i=parent.at(i);}
    std::reverse(raw.begin(),raw.end());raw.front()=start;if(found)raw.back()=goal;
    if(smooth&&raw.size()>2){std::vector<Vec3> sm{raw.front()};std::size_t k=0;
        while(k+1<raw.size()){std::size_t j=raw.size()-1;while(j>k+1&&!volumeLineClear(g,raw[k],raw[j]))--j;sm.push_back(raw[j]);k=j;}raw=std::move(sm);}
    out.points=std::move(raw);return out;
}

// ---- Local avoidance for flocks / fleets (no collision response; the owner integrates the returned acceleration).
struct FlockConfig {Fixed neighbourRadius=8_fx,separationRadius=3_fx,separation=3_fx,alignment=1_fx,cohesion=Fixed::fromRatio(1,2),seek=2_fx,obstacle=6_fx,maxAccel=20_fx;};
struct FlockAgent {int64_t id=0;Vec3 position{},velocity{};};
// obstaclePush(position) -> Vec3: owner repulsion from nearby geometry (zero when clear), e.g. from VolumeGrid lookups.
template<class ObstaclePush> Vec3 flockSteer(const FlockConfig& c,const FlockAgent& self,std::span<const FlockAgent> others,Vec3 goal,ObstaclePush&& push){
    Vec3 sep{},ali{},coh{};int64_t n=0;
    for(const auto& o:others){if(o.id==self.id)continue;auto d=self.position-o.position;auto dist=d.length();if(dist>c.neighbourRadius)continue;
        ++n;ali+=o.velocity;coh+=o.position;if(dist<c.separationRadius){auto k=dist>Fixed{}?(c.separationRadius-dist)/c.separationRadius:1_fx;
            sep+=(dist>Fixed{}?d/dist:Vec3{Fixed::fromRatio(o.id<self.id?1:-1,1),{},{}})*k;}}
    Vec3 acc=sep*c.separation+fxNormalize(goal-self.position)*c.seek+Vec3(push(self.position))*c.obstacle;
    if(n){ali=ali/Fixed::fromInt(n);coh=coh/Fixed::fromInt(n);acc+=(ali-self.velocity)*c.alignment*Fixed::fromRatio(1,4)+fxNormalize(coh-self.position)*c.cohesion;}
    auto len=acc.length();if(len>c.maxAccel)acc=acc*(c.maxAccel/len);return acc;
}
// Formation slot `offset` (x right, y up, z forward in the leader frame) rotated by leader yaw (turns about +Y) and pitch.
inline Vec3 formationSlot3D(Vec3 leader,Fixed yawTurns,Fixed pitchTurns,Vec3 offset){
    auto cy=fxCosTurns(yawTurns),sy=fxSinTurns(yawTurns),cp=fxCosTurns(pitchTurns),sp=fxSinTurns(pitchTurns);
    Vec3 p{offset.x,offset.y*cp+offset.z*sp,offset.z*cp-offset.y*sp};            // pitch about local x (nose up = positive)
    return leader+Vec3{p.x*cy+p.z*sy,p.y,p.z*cy-p.x*sy};
}
}
