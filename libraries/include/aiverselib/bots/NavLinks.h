#pragma once
// nav-links: off-mesh links on top of any navigation mesh (engine/nav, bots/NavMesh.h, voxel paths): jumps, drops,
// ladders, climbs, doors and teleports with costs, one-way or two-way use and enable/disable at runtime. planWithLinks
// combines the owner's walking query (mesh distance between two points, or nothing when unreachable) with link edges
// in one deterministic Dijkstra (cost, then node index) and returns legs: walk to a point / traverse a link.
// stepLinkTraversal moves an agent along a link tick by tick (jump arcs, ladder and door timing, waiting at closed
// doors). generateCover samples cover spots around box obstacles for ShooterBot (map GeneratedCover to
// bots::CoverCandidate). Fixed math; persistence of LinkTraversal: mechanics::save/load record kind 201.
#include <aiverselib/mechanics/PolicyCommon.h>
#include "engine/world/SpatialWorld.h"
namespace aiverse::lib::bots {
using namespace aiverse::lib::mechanics;
enum class LinkKind:uint8_t {Jump,Drop,Ladder,Climb,Door,Teleport};
struct NavLink {int64_t id=0;Vec3 from{},to{};LinkKind kind=LinkKind::Jump;Fixed cost{};   // cost 0 = straight distance
    bool bidirectional=false,enabled=true;uint32_t traverseTicks=30;Fixed arcHeight=1_fx;};
using NavLinkSet=std::map<int64_t,NavLink>;
inline constexpr std::size_t kMaxNavLinks=512;
inline bool valid(const NavLink& l){return l.id>0&&policyBounded(l.from)&&policyBounded(l.to)&&!(l.from==l.to)&&uint8_t(l.kind)<=uint8_t(LinkKind::Teleport)
    &&l.cost>=Fixed{}&&l.cost<=100000_fx&&(l.kind==LinkKind::Teleport||l.traverseTicks>0)&&l.traverseTicks<=36000&&l.arcHeight>=Fixed{}&&l.arcHeight<=100_fx;}
inline bool valid(const NavLinkSet& s){if(s.size()>kMaxNavLinks)return false;for(auto& [id,l]:s)if(id!=l.id||!valid(l))return false;return true;}
inline Fixed linkCost(const NavLink& l){return l.cost>Fixed{}?l.cost:(l.to-l.from).length();}

enum class LegKind:uint8_t {Walk,Link};
struct PathLeg {LegKind kind=LegKind::Walk;Vec3 target{};int64_t link=0;bool reverse=false;};
struct LinkPath {std::vector<PathLeg> legs;Fixed cost{};};
// meshCost(a,b) -> std::optional<Fixed>: walking distance on the owner's mesh, nullopt when b cannot be reached from a.
template<class MeshCost> std::optional<LinkPath> planWithLinks(const NavLinkSet& links,Vec3 start,Vec3 goal,MeshCost&& meshCost){
    if(!valid(links)||!policyBounded(start)||!policyBounded(goal))return std::nullopt;
    std::vector<Vec3> pos{start,goal};std::vector<const NavLink*> owner{nullptr,nullptr};
    for(auto& [id,l]:links){pos.push_back(l.from);pos.push_back(l.to);owner.push_back(&l);owner.push_back(&l);}
    const std::size_t n=pos.size();const Fixed inf=Fixed::fromInt(int64_t(1)<<40);
    std::vector<Fixed> dist(n,inf);std::vector<int64_t> prev(n,-1);std::vector<uint8_t> via(n,0),done(n,0);  // via: 0 walk, 1 link forward, 2 link reverse
    std::map<std::pair<std::size_t,std::size_t>,std::optional<Fixed>> cache;
    auto walk=[&](std::size_t a,std::size_t b)->std::optional<Fixed>{auto k=std::pair{a,b};auto it=cache.find(k);if(it!=cache.end())return it->second;
        std::optional<Fixed> c=std::optional<Fixed>(meshCost(pos[a],pos[b]));if(c&&*c<Fixed{})c.reset();cache[k]=c;return c;};
    dist[0]={};
    for(;;){
        std::size_t u=n;for(std::size_t i=0;i<n;++i)if(!done[i]&&dist[i]<inf&&(u==n||dist[i]<dist[u]))u=i;
        if(u==n||u==1)break;done[u]=1;
        for(std::size_t v=0;v<n;++v){if(done[v]||v==u)continue;if(auto c=walk(u,v)){auto d=dist[u]+*c;if(d<dist[v]){dist[v]=d;prev[v]=int64_t(u);via[v]=0;}}}
        if(u>=2){const auto* l=owner[u];if(l->enabled){bool atFrom=(u-2)%2==0;std::size_t other=atFrom?u+1:u-1;
            if((atFrom||l->bidirectional)&&!done[other]){auto d=dist[u]+linkCost(*l);if(d<dist[other]){dist[other]=d;prev[other]=int64_t(u);via[other]=atFrom?1:2;}}}}
    }
    if(dist[1]>=inf)return std::nullopt;
    LinkPath path;path.cost=dist[1];
    for(int64_t v=1;v!=0;v=prev[std::size_t(v)]){auto i=std::size_t(v);
        if(via[i]==0)path.legs.push_back({LegKind::Walk,pos[i],0,false});else path.legs.push_back({LegKind::Link,pos[i],owner[i]->id,via[i]==2});}
    std::reverse(path.legs.begin(),path.legs.end());
    // Merge consecutive walk legs (walk to a link start, then walk again never happens, but keep the output canonical).
    std::vector<PathLeg> merged;for(auto& leg:path.legs){if(!merged.empty()&&leg.kind==LegKind::Walk&&merged.back().kind==LegKind::Walk)merged.back()=leg;else merged.push_back(leg);}
    path.legs=std::move(merged);return path;
}

struct LinkTraversal {int64_t link=0;uint32_t start=0,lastTick=0,waited=0;bool reverse=false,active=false,initialized=false;};
struct LinkTraversalStep {Vec3 position{};bool finished=false,waiting=false;};
inline bool beginLink(LinkTraversal& t,const NavLinkSet& links,int64_t link,bool reverse,uint32_t tick){
    auto it=links.find(link);if(it==links.end()||!it->second.enabled||t.active||(reverse&&!it->second.bidirectional)||!policyTickOk(t.initialized,t.lastTick,tick))return false;
    t={link,tick,tick,0,reverse,true,true};return true;}
// doorOpen: owner's door state for Door links (ignored for other kinds). Position is the body's feet.
inline std::optional<LinkTraversalStep> stepLinkTraversal(const NavLinkSet& links,LinkTraversal& t,uint32_t tick,bool doorOpen=true){
    if(!t.active||tick<=t.lastTick)return std::nullopt;auto it=links.find(t.link);if(it==links.end())return std::nullopt;
    const auto& l=it->second;auto a=t.reverse?l.to:l.from,b=t.reverse?l.from:l.to;LinkTraversalStep r;auto s=t;s.lastTick=tick;
    if(l.kind==LinkKind::Teleport){r.position=b;r.finished=true;s.active=false;t=s;return r;}
    if(l.kind==LinkKind::Door&&!doorOpen){++s.waited;s.start=s.start+1;r.position=a;r.waiting=true;t=s;return r;}
    uint32_t age=tick-s.start;if(age>=l.traverseTicks){r.position=b;r.finished=true;s.active=false;t=s;return r;}
    Fixed f=Fixed::fromRatio(age,l.traverseTicks);
    if(l.kind==LinkKind::Ladder||l.kind==LinkKind::Climb){             // vertical first, then step off
        Fixed rise=Fixed::fromRatio(4,5);
        if(f<=rise){auto k=f/rise;r.position={a.x,a.y+(b.y-a.y)*k,a.z};}
        else{auto k=(f-rise)/(1_fx-rise);r.position={a.x+(b.x-a.x)*k,b.y,a.z+(b.z-a.z)*k};}
    }else{r.position=a+(b-a)*f;if(l.kind==LinkKind::Jump||l.kind==LinkKind::Drop)r.position.y+=l.arcHeight*f*(1_fx-f)*4;}
    t=s;return r;
}
inline void io(Archive& a,LinkTraversal& t){a(t.link,t.start,t.lastTick,t.waited,t.reverse,t.active,t.initialized);}
inline uint16_t stateKind(const LinkTraversal&){return 201;}
inline bool valid(const LinkTraversal& t){return t.link>=0&&(!t.active||t.link>0);}

// ---- Cover spots around box obstacles.
struct CoverGenConfig {Fixed agentRadius=Fixed::fromRatio(2,5),spacing=Fixed::fromRatio(3,2),crouchHeight=1_fx,standHeight=Fixed::fromRatio(17,10);
    int64_t halfMilli=500,fullMilli=1000;std::size_t maxSpots=4096;};
struct GeneratedCover {int64_t id=0,box=0;Vec3 position{},normal{};int64_t protectionMilli=0;};
inline bool pointInBox(const world::Bounds& b,Vec3 p,Fixed pad){return p.x>b.min.x-pad&&p.x<b.max.x+pad&&p.z>b.min.z-pad&&p.z<b.max.z+pad&&p.y>=b.min.y&&p.y<b.max.y;}
// standable(point) -> bool: owner check that the spot is on walkable ground (e.g. navmesh polygon lookup). Boxes are indexed from 1.
template<class Standable> std::vector<GeneratedCover> generateCover(std::span<const world::Bounds> boxes,const CoverGenConfig& c,Standable&& standable){
    std::vector<GeneratedCover> out;if(c.agentRadius<=Fixed{}||c.spacing<=Fixed{}||c.crouchHeight<=Fixed{}||c.standHeight<c.crouchHeight)return out;
    int64_t next=1;
    for(std::size_t bi=0;bi<boxes.size();++bi){const auto& b=boxes[bi];auto h=b.max.y-b.min.y;
        if(h<c.crouchHeight*Fixed::fromRatio(3,5))continue;int64_t prot=h>=c.standHeight?c.fullMilli:c.halfMilli;
        struct Side {Vec3 a,b,n;};
        Side sides[4]={{{b.min.x,b.min.y,b.min.z-c.agentRadius},{b.max.x,b.min.y,b.min.z-c.agentRadius},{{},{},-1_fx}},
                       {{b.min.x,b.min.y,b.max.z+c.agentRadius},{b.max.x,b.min.y,b.max.z+c.agentRadius},{{},{},1_fx}},
                       {{b.min.x-c.agentRadius,b.min.y,b.min.z},{b.min.x-c.agentRadius,b.min.y,b.max.z},{-1_fx,{},{}}},
                       {{b.max.x+c.agentRadius,b.min.y,b.min.z},{b.max.x+c.agentRadius,b.min.y,b.max.z},{1_fx,{},{}}}};
        for(auto& s:sides){auto len=(s.b-s.a).length();int64_t count=std::max<int64_t>(1,(len/c.spacing).toInt());
            for(int64_t i=0;i<count;++i){auto f=Fixed::fromRatio(2*i+1,2*count);Vec3 p=s.a+(s.b-s.a)*f;
                bool blocked=false;for(std::size_t o=0;o<boxes.size()&&!blocked;++o)if(o!=bi&&pointInBox(boxes[o],p+Vec3{{},Fixed::fromRatio(1,10),{}},c.agentRadius/2))blocked=true;
                if(blocked||!standable(p))continue;if(out.size()>=c.maxSpots)return out;
                out.push_back({next++,int64_t(bi+1),p,s.n,prot});}}
    }
    return out;
}
}
