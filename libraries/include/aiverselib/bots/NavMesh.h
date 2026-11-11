#pragma once
#include "Navigation.h"
#include "engine/core/Hash.h"
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
struct NavPolygon {std::array<Vec3,4> vertices;Vec3 center{};uint32_t component=UINT32_MAX;std::vector<size_t> neighbors;};
struct NavMesh {
    NavBuildConfig config;uint64_t collisionHash=0;
    std::vector<NavPolygon> polygons;
    std::vector<std::vector<size_t>> components;
    std::map<std::pair<int32_t,int32_t>,std::vector<size_t>> cells;
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
inline NavBuildResult buildNavMesh(const world::SpatialWorld& world,const NavBuildConfig& c){
    if(!valid(c))return {{},"navmesh: invalid agent, bounds or limits"};
    const auto nx=(c.bounds.max.x.raw-c.bounds.min.x.raw)/c.cellSize.raw;
    const auto nz=(c.bounds.max.z.raw-c.bounds.min.z.raw)/c.cellSize.raw;
    if(nx<=0||nz<=0||nx>c.maxCells||nz>c.maxCells||nx*nz>c.maxCells)return {{},"navmesh: cell budget exceeded; use a smaller region or larger cells"};
    NavMesh mesh;mesh.config=c;mesh.collisionHash=collisionHash(world);
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
            if(mesh.polygons.size()>=c.maxPolygons)return {{},"navmesh: polygon budget exceeded"};
            NavPolygon polygon;polygon.center=center;
            polygon.vertices={Vec3{x0,level,z0},Vec3{x0+c.cellSize,level,z0},Vec3{x0+c.cellSize,level,z0+c.cellSize},Vec3{x0,level,z0+c.cellSize}};
            mesh.cells[{x,z}].push_back(mesh.polygons.size());mesh.polygons.push_back(std::move(polygon));
        }
    }
    for(const auto& [cell,indices]:mesh.cells)for(auto i:indices){
        for(auto offset:{std::pair{0,-1},std::pair{-1,0},std::pair{1,0},std::pair{0,1}}){
            auto other=mesh.cells.find({cell.first+offset.first,cell.second+offset.second});if(other==mesh.cells.end())continue;
            for(auto j:other->second)if(navDetail::connection(world,mesh.polygons[i].center,mesh.polygons[j].center,c))mesh.polygons[i].neighbors.push_back(j);
        }
        auto& links=mesh.polygons[i].neighbors;std::sort(links.begin(),links.end());
    }
    for(size_t i=0;i<mesh.polygons.size();++i)if(mesh.polygons[i].component==UINT32_MAX){
        const auto id=uint32_t(mesh.components.size());std::vector<size_t> members{i};mesh.polygons[i].component=id;
        for(size_t head=0;head<members.size();++head)for(auto next:mesh.polygons[members[head]].neighbors)if(mesh.polygons[next].component==UINT32_MAX){mesh.polygons[next].component=id;members.push_back(next);}
        std::sort(members.begin(),members.end());mesh.components.push_back(std::move(members));
    }
    return {std::move(mesh),{}};
}
inline std::optional<size_t> navPolygonAt(const NavMesh& mesh,Vec3 point){
    const auto& c=mesh.config;if(!valid(c)||point.x<c.bounds.min.x||point.z<c.bounds.min.z||point.x>=c.bounds.max.x||point.z>=c.bounds.max.z||point.y<c.bounds.min.y-1_fx||point.y>c.bounds.max.y+1_fx)return {};
    auto x=(point.x.raw-c.bounds.min.x.raw)/c.cellSize.raw,z=(point.z.raw-c.bounds.min.z.raw)/c.cellSize.raw;
    if(x>INT32_MAX||z>INT32_MAX)return {};
    auto cell=mesh.cells.find({int32_t(x),int32_t(z)});if(cell==mesh.cells.end())return {};
    std::optional<size_t> best;Fixed delta=c.stepHeight+Fixed::fromRatio(1,5);
    for(auto index:cell->second){if(index>=mesh.polygons.size())return {};auto d=fxAbs(mesh.polygons[index].center.y-point.y);if(d<=delta){best=index;delta=d;}}
    return best;
}
// Full cell-center corridor, with exact endpoints; empty means unreachable/stale/budget exhausted.
inline std::optional<std::vector<Vec3>> navPath(const NavMesh& mesh,const world::SpatialWorld& world,Vec3 from,Vec3 goal,size_t maxVisits=8192){
    if(mesh.collisionHash!=collisionHash(world))return {};
    auto start=navPolygonAt(mesh,from),end=navPolygonAt(mesh,goal);if(!start||!end)return {};
    const auto& c=mesh.config;
    if(!navDetail::connection(world,from,mesh.polygons[*start].center,c)||!navDetail::connection(world,mesh.polygons[*end].center,goal,c))return {};
    std::vector<size_t> parents(mesh.polygons.size(),kUnreachable);std::deque<size_t> pending{*start};parents[*start]=*start;
    bool found=false;
    while(!pending.empty()&&maxVisits){--maxVisits;auto node=pending.front();pending.pop_front();if(node==*end){found=true;break;}
        for(auto next:mesh.polygons[node].neighbors){if(next>=parents.size())return {};if(parents[next]!=kUnreachable)continue;parents[next]=node;pending.push_back(next);}
    }
    if(!found)return {};
    std::vector<Vec3> points{goal};for(auto node=*end;node!=*start;node=parents[node])points.push_back(mesh.polygons[node].center);
    points.push_back(mesh.polygons[*start].center);points.push_back(from);std::reverse(points.begin(),points.end());return points;
}
inline std::optional<Vec3> navStep(const NavMesh& mesh,const world::SpatialWorld& world,Vec3 from,Vec3 goal,size_t maxVisits=8192){
    auto path=navPath(mesh,world,from,goal,maxVisits);if(!path)return {};
    // Stay on the polygon corridor; never shortcut across holes or disconnected floors.
    const auto near=fxMin(mesh.config.cellSize/4,Fixed::fromRatio(1,4));
    for(size_t i=2;i<path->size();++i)if(((*path)[i]-from).xz().length()>near){
        if(navDetail::connection(world,from,(*path)[i],mesh.config))return (*path)[i];
        return (*path)[1];
    }
    return goal;
}
}
