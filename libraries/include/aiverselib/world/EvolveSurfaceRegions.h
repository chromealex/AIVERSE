// Planetary EVOLVE coverage over the canonical immutable Surface.
// Region remains the sole ecological state; no duplicate pressure solver.
#pragma once
#include "EvolveArea.h"
#include "WorldProcessSurfaceFrame.h"
#include <array>
#include <optional>

namespace aiverse::evolve {
struct SurfaceArea {
    Vec3 origin{}; // actual simulation-space position at authoring time
    int32_t body=-1;
    Area local{}; // east/south offsets and half extents, centimetres
};
inline bool valid(const SurfaceArea& a){
    return a.body>=0&&a.body<game::Surface::kMaxBodies&&valid(a.local);
}
inline void io(Archive& archive,SurfaceArea& a){
    archive(a.origin.x,a.origin.y,a.origin.z,a.body);
    io(archive,a.local);
    if(!archive.writing()&&!valid(a))archive.fail();
}
inline void registerSurfaceAreaPersistence(Simulation& sim){
    sim.registerPersistentComponent<SurfaceArea>("evolve.surface_area",1);
}
inline bool setSurfaceArea(World& world,const game::Surface& surface,
        Entity region,Vec3 origin,Area local){
    if(!world.alive(region)||!world.has<Region>(region)||
       world.has<Area>(region)||!valid(local))return false;
    const auto frame=lib::worldprocess::surfaceFrame(&surface,origin);
    if(!frame)return false;
    const SurfaceArea area{origin,frame->body,local};
    if(world.has<SurfaceArea>(region))world.get<SurfaceArea>(region)=area;
    else world.add<SurfaceArea>(region,area);
    return true;
}
// A region is a rectangle in the origin's azimuthal equidistant chart:
// shortest surface arc length resolved along canonical local east/south.
// Face seams do not change the chart. Exact antipodes choose east as a
// deterministic chart cut; use multiple overlapping regions for global
// coverage. Height is ignored (surface ecology). No second spherical math.
inline std::optional<Vec2> surfaceRegionOffset(const game::Surface& surface,
        const lib::worldprocess::SurfaceFrame& origin,
        const lib::worldprocess::SurfaceFrame& point){
    if(origin.body<0||origin.body!=point.body||
       size_t(origin.body)>=surface.bodies.size())return {};
    const auto turns=game::sphereArcTurns(origin.up,point.up);
    if(turns.raw==0)return Vec2{};
    const auto arc=turns*game::kFxTwoPi*surface.bodies[size_t(origin.body)].radius;
    auto direction=game::sphereTangentToward(origin.up,point.up);
    if(direction.lengthSq().raw==0)direction=origin.east;
    return Vec2{direction.dot(origin.east)*arc,direction.dot(origin.south)*arc};
}
// Inverse of surfaceRegionOffset on the open azimuthal chart. The
// canonical face axes are independently projected and not orthogonal.
// Do not invent an inverse at the antipodal cut.
inline std::optional<Vec3> surfaceRegionDirection(Vec3 up,Vec3 east,Vec3 south,
        Fixed radius,Fixed x,Fixed z){
    if(radius.raw<=0)return {};
    const auto circumference=radius*game::kFxTwoPi;
    if(circumference.raw<=0||fxAbs(x)>=circumference/2||fxAbs(z)>=circumference/2)return {};
    const auto d=east.dot(south),den=Fixed::fromInt(1)-d*d;
    if(den<Fixed::fromRatio(1,100))return {};
    const auto tangent=east*((x-d*z)/den)+south*((z-d*x)/den);
    const auto distance=tangent.length();
    const auto turns=distance/circumference;
    if(turns>=Fixed::fromRatio(1,2))return {};
    if(distance.raw==0)return up;
    return fxNormalize(up*fxCosTurns(turns)+(tangent/distance)*fxSinTurns(turns));
}
inline bool containsSurfacePoint(const game::Surface& surface,const SurfaceArea& area,
        const lib::worldprocess::SurfaceFrame& point){
    if(!valid(area)||!area.local.enabled||area.body!=point.body)return false;
    const auto origin=lib::worldprocess::surfaceFrame(&surface,area.origin);
    if(!origin||origin->body!=area.body)return false;
    const auto offset=surfaceRegionOffset(surface,*origin,point);
    if(!offset)return false;
    const auto& a=area.local;
    return offset->x>=Fixed::fromRatio(a.centerXcm-a.halfWidthCm,100)&&
           offset->x< Fixed::fromRatio(a.centerXcm+a.halfWidthCm,100)&&
           offset->y>=Fixed::fromRatio(a.centerZcm-a.halfDepthCm,100)&&
           offset->y< Fixed::fromRatio(a.centerZcm+a.halfDepthCm,100);
}
// Derived body buckets, immutable for one owner sampling step. Bounds and
// membership may not change until the sampler completes. Region values
// are read live from the same ECS at query time. Stable ID ordering.
class SurfaceRegionIndex {
    std::array<std::vector<std::pair<Entity,SurfaceArea>>,game::Surface::kMaxBodies> bodies_;
public:
    bool build(const World& world,const game::Surface& surface){
        if(world.countExisting<SurfaceArea>()>kMaxRegions)return false;
        decltype(bodies_) next{};
        bool ok=true;
        world.eachExistingConst<SurfaceArea>([&](Entity e,const SurfaceArea& a){
            if(!world.alive(e))return;
            if(!valid(a)||!world.has<Region>(e)||world.has<Area>(e)){ok=false;return;}
            if(!a.local.enabled)return;
            const auto frame=lib::worldprocess::surfaceFrame(&surface,a.origin);
            if(!frame||frame->body!=a.body){ok=false;return;}
            next[size_t(a.body)].push_back({e,a});
        });
        if(!ok)return false;
        for(auto& bucket:next)std::sort(bucket.begin(),bucket.end(),
            [](const auto& a,const auto& b){return a.first.packed()<b.first.packed();});
        bodies_=std::move(next);return true;
    }
    template<class Visit>
    void query(const World& world,const game::Surface& surface,Vec3 position,
               const Visit& visit)const{
        const auto point=lib::worldprocess::surfaceFrame(&surface,position);
        if(!point)return;
        for(const auto& [id,area]:bodies_[size_t(point->body)]){
            if(!containsSurfacePoint(surface,area,*point))continue;
            const auto* region=world.tryGetConst<Region>(id);
            if(region&&region->enabled&&valid(*region))visit(id,*region);
        }
    }
};
} // namespace aiverse::evolve
