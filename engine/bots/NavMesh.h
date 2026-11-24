#pragma once
#include "engine/bots/Navigation.h"
#include "engine/core/Hash.h"
#include "engine/nav/NavMesh.h"
#include <array>
#include <deque>
#include <map>
#include <set>
#include <string>
namespace aiverse::lib::bots {
// Static AABB collision is the current FPS geometry source. Polygons are conservative raster cells,
// with separate layers for stacked floors; no floating point or unordered traversal is used.
struct NavBuildConfig {
    world::Bounds bounds{{-64_fx,-4_fx,-64_fx},{64_fx,16_fx,64_fx}};
    Fixed cellSize=2_fx,radius=Fixed::fromRatio(3,10),height=Fixed::fromRatio(18,10),stepHeight=Fixed::fromRatio(3,10);
    uint32_t maxCells=65536,maxPolygons=65536,maxCandidatesPerCell=128;
};
// Walkable rectangles merged by the engine navigation mesh (engine/nav): every cell of a polygon is a supported,
// body-clear surface at one height; neighbours share a portal the motor can cross.
struct NavPolygon {std::array<Vec3,4> vertices;Vec3 center{};uint32_t component=UINT32_MAX;std::vector<size_t> neighbors;};
struct NavMesh {
    NavBuildConfig config;uint64_t collisionHash=0;
    std::vector<NavPolygon> polygons;            // index = core rectangle id
    std::vector<std::vector<size_t>> components;
    nav::Mesh core;                              // genre-independent mesh: rectangles, portals, A* + funnel
};
struct NavBuildResult {std::optional<NavMesh> mesh;std::string error;};
inline uint64_t collisionHash(const world::SpatialWorld& world){
    Hasher hash;for(const auto& [id,c]:world.colliders()){hash.u32(id);hash.v3(c.bounds.min);hash.v3(c.bounds.max);}return hash.value();
}
inline bool valid(const NavBuildConfig& c){
    for(auto p:{c.bounds.min,c.bounds.max})if(p.x< -100000_fx||p.x>100000_fx||p.y< -100000_fx||p.y>100000_fx||p.z< -100000_fx||p.z>100000_fx)return false;
    return c.bounds.min.x<c.bounds.max.x&&c.bounds.min.y<c.bounds.max.y&&c.bounds.min.z<c.bounds.max.z&&
        c.cellSize>=Fixed::fromRatio(1,4)&&c.cellSize<=32_fx&&c.radius>=Fixed::fromRatio(1,10)&&c.radius<=2_fx&&
        c.height>=c.radius*2&&c.height<=4_fx&&c.stepHeight>=Fixed{}&&c.stepHeight<=1_fx&&
        c.maxCells>0&&c.maxCells<=262144&&c.maxPolygons>0&&c.maxPolygons<=262144&&c.maxCandidatesPerCell>0&&c.maxCandidatesPerCell<=1024;
}
namespace navDetail {
constexpr auto skin=Fixed::fromRatio(1,100);
// Exact union coverage of the expanded foot rectangle. Sampling corners alone misses holes.
inline bool supported(const std::vector<world::Bounds>& floors,world::Bounds foot){
    std::vector<Fixed> xs{foot.min.x,foot.max.x};
    for(const auto& b:floors){xs.push_back(fxClamp(b.min.x,foot.min.x,foot.max.x));xs.push_back(fxClamp(b.max.x,foot.min.x,foot.max.x));}
    std::sort(xs.begin(),xs.end());xs.erase(std::unique(xs.begin(),xs.end()),xs.end());
    for(size_t i=1;i<xs.size();++i){
        std::vector<std::pair<Fixed,Fixed>> spans;
        for(const auto& b:floors)if(b.min.x<=xs[i-1]&&b.max.x>=xs[i])spans.push_back({b.min.z,b.max.z});
        std::sort(spans.begin(),spans.end());auto end=foot.min.z;
        for(auto [lo,hi]:spans){if(lo>end)break;end=fxMax(end,hi);}
        if(end<foot.max.z)return false;
    }return true;
}
inline bool connection(const world::SpatialWorld& world,Vec3 from,Vec3 to,const NavBuildConfig& c){
    if(fxAbs(from.y-to.y)>c.stepHeight+skin)return false;
    // Lift at the lower side, sweep horizontally, then land; matches the motor's step-up semantics.
    auto height=fxMax(from.y,to.y);Vec3 a=from,b=to;a.y=b.y=height;
    return clearPath(world,from,a,c.radius,c.height)&&clearPath(world,a,b,c.radius,c.height)&&clearPath(world,b,to,c.radius,c.height);
}
}
// Spans of the sampled columns: supported floors under a clear body, as nav::Source for the engine mesh.
struct NavColumns final : nav::Source {
    const world::SpatialWorld& world;const NavBuildConfig& c;int32_t nx=0,nz=0;std::vector<std::vector<Fixed>> levels;
    NavColumns(const world::SpatialWorld& w,const NavBuildConfig& config):world(w),c(config){}
    Vec3 center(int32_t x,int32_t z,uint32_t i) const {
        return {c.bounds.min.x+c.cellSize*x+c.cellSize/2,levels[size_t(z*nx+x)][i]+navDetail::skin,c.bounds.min.z+c.cellSize*z+c.cellSize/2};
    }
    void spans(int32_t x,int32_t z,std::vector<nav::Span>& out) const override {
        for(auto level:levels[size_t(z*nx+x)])out.push_back({level+navDetail::skin,0});
    }
    bool link(int32_t x0,int32_t z0,uint32_t i,int32_t x1,int32_t z1,uint32_t j) const override {
        return navDetail::connection(world,center(x0,z0,i),center(x1,z1,j),c);
    }
};
inline NavBuildResult buildNavMesh(const world::SpatialWorld& world,const NavBuildConfig& c){
    if(!valid(c))return {{},"navmesh: invalid agent, bounds or limits"};
    const auto nx=(c.bounds.max.x.raw-c.bounds.min.x.raw)/c.cellSize.raw;
    const auto nz=(c.bounds.max.z.raw-c.bounds.min.z.raw)/c.cellSize.raw;
    if(nx<=0||nz<=0||nx>c.maxCells||nz>c.maxCells||nx*nz>c.maxCells)return {{},"navmesh: cell budget exceeded; use a smaller region or larger cells"};
    NavMesh mesh;mesh.config=c;mesh.collisionHash=collisionHash(world);
    NavColumns columns(world,mesh.config);columns.nx=int32_t(nx);columns.nz=int32_t(nz);columns.levels.resize(size_t(nx*nz));
    size_t spans=0;
    for(int32_t z=0;z<nz;++z)for(int32_t x=0;x<nx;++x){
        auto x0=c.bounds.min.x+c.cellSize*x,z0=c.bounds.min.z+c.cellSize*z;
        Vec3 center{x0+c.cellSize/2,{},z0+c.cellSize/2};
        world::Bounds column{{x0-c.radius,c.bounds.min.y-c.stepHeight,z0-c.radius},{x0+c.cellSize+c.radius,c.bounds.max.y+c.height,z0+c.cellSize+c.radius}};
        auto ids=world.query(column);if(ids.size()>c.maxCandidatesPerCell)return {{},"navmesh: candidate budget exceeded in a cell"};
        std::set<Fixed> levels;
        for(auto id:ids){const auto& b=world.colliders().at(id).bounds;
            if(b.max.y>=c.bounds.min.y&&b.max.y<=c.bounds.max.y&&center.x>=b.min.x&&center.x<=b.max.x&&center.z>=b.min.z&&center.z<=b.max.z)levels.insert(b.max.y);
        }
        for(auto level:levels){
            center.y=level+navDetail::skin;bool blocked=false;std::vector<world::Bounds> floors;
            for(auto id:ids){const auto& b=world.colliders().at(id).bounds;
                if(b.min.y<=level+navDetail::skin&&b.max.y>level&&center.x>b.min.x&&center.x<b.max.x&&center.z>b.min.z&&center.z<b.max.z){blocked=true;break;}
                if(fxAbs(b.max.y-level)<=c.stepHeight)floors.push_back(b);
            }
            if(blocked||!navDetail::supported(floors,column))continue;
            // Step-sized lips may overlap the cell boundary, but ceilings/walls must clear the body.
            auto body=column;body.min.y=level+c.stepHeight+navDetail::skin;body.max.y=level+c.height+navDetail::skin;
            if(!world.query(body).empty())continue;
            if(++spans>c.maxPolygons)return {{},"navmesh: polygon budget exceeded"};
            columns.levels[size_t(z*nx+x)].push_back(level);
        }
    }
    nav::Config core;core.width=int32_t(nx);core.depth=int32_t(nz);core.originX=c.bounds.min.x;core.originZ=c.bounds.min.z;core.cell=c.cellSize;
    std::string error;if(!mesh.core.build(columns,core,&error))return {{},error};
    // Inspection / gameplay view of the merged rectangles; ids are the core rectangle ids.
    mesh.polygons.resize(mesh.core.rectCount());
    for(uint32_t i=0;i<mesh.polygons.size();++i){
        const auto& r=mesh.core.rect(i);auto& polygon=mesh.polygons[i];const auto y=r.y-navDetail::skin;
        const auto x0=mesh.core.worldX(r.x0),x1=mesh.core.worldX(r.x1+1),z0=mesh.core.worldZ(r.z0),z1=mesh.core.worldZ(r.z1+1);
        polygon.vertices={Vec3{x0,y,z0},Vec3{x1,y,z0},Vec3{x1,y,z1},Vec3{x0,y,z1}};
        polygon.center={(x0+x1)/2,r.y,(z0+z1)/2};
        for(auto n:mesh.core.neighbors(i))polygon.neighbors.push_back(n);
    }
    for(size_t i=0;i<mesh.polygons.size();++i)if(mesh.polygons[i].component==UINT32_MAX){
        const auto id=uint32_t(mesh.components.size());std::vector<size_t> members{i};mesh.polygons[i].component=id;
        for(size_t head=0;head<members.size();++head)for(auto next:mesh.polygons[members[head]].neighbors)if(mesh.polygons[next].component==UINT32_MAX){mesh.polygons[next].component=id;members.push_back(next);}
        std::sort(members.begin(),members.end());mesh.components.push_back(std::move(members));
    }
    return {std::move(mesh),{}};
}
inline Fixed navTolerance(const NavMesh& mesh){return mesh.config.stepHeight+Fixed::fromRatio(1,5);}
inline std::optional<size_t> navPolygonAt(const NavMesh& mesh,Vec3 point){
    const auto& c=mesh.config;if(!valid(c)||point.x<c.bounds.min.x||point.z<c.bounds.min.z||point.x>=c.bounds.max.x||point.z>=c.bounds.max.z||point.y<c.bounds.min.y-1_fx||point.y>c.bounds.max.y+1_fx)return {};
    auto id=mesh.core.locate(point,navTolerance(mesh));if(!id||*id>=mesh.polygons.size())return {};
    return size_t(*id);
}
// Centre of the sampled cell under a point on polygon `index` (the motor's reference for joining the mesh).
inline Vec3 navCellCenter(const NavMesh& mesh,size_t index,Vec3 point){
    const auto& c=mesh.config;const auto cell=c.cellSize;
    const auto x=(point.x.raw-c.bounds.min.x.raw)/cell.raw,z=(point.z.raw-c.bounds.min.z.raw)/cell.raw;
    return {c.bounds.min.x+cell*x+cell/2,mesh.polygons[index].center.y,c.bounds.min.z+cell*z+cell/2};
}
// Any-angle route: from, corners on the polygon corridor, goal; empty means unreachable/stale/budget exhausted.
inline std::optional<std::vector<Vec3>> navPath(const NavMesh& mesh,const world::SpatialWorld& world,Vec3 from,Vec3 goal,size_t maxVisits=8192){
    if(mesh.collisionHash!=collisionHash(world))return {};
    auto start=navPolygonAt(mesh,from),end=navPolygonAt(mesh,goal);if(!start||!end)return {};
    const auto& c=mesh.config;
    if(!navDetail::connection(world,from,navCellCenter(mesh,*start,from),c)||!navDetail::connection(world,navCellCenter(mesh,*end,goal),goal,c))return {};
    nav::Query q;q.margin=fxMin(c.cellSize/4,Fixed::fromRatio(1,4));q.yTolerance=navTolerance(mesh);q.maxVisits=maxVisits;
    std::vector<Vec3> points;if(!mesh.core.findPath(from,goal,q,points))return {};
    return points;
}
inline std::optional<Vec3> navStep(const NavMesh& mesh,const world::SpatialWorld& world,Vec3 from,Vec3 goal,size_t maxVisits=8192){
    auto path=navPath(mesh,world,from,goal,maxVisits);if(!path)return {};
    // Corners stay on the polygon corridor; never shortcut across holes or disconnected floors.
    const auto near=fxMin(mesh.config.cellSize/4,Fixed::fromRatio(1,4));
    for(size_t i=1;i<path->size();++i)if(((*path)[i]-from).xz().length()>near){
        if(navDetail::connection(world,from,(*path)[i],mesh.config))return (*path)[i];
        return navCellCenter(mesh,*navPolygonAt(mesh,from),from);
    }
    return goal;
}
}
