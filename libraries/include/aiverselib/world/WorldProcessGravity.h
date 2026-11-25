// Opt-in PHYSIS consumers. The owning host supplies authoritative positions,
// frame, and unit conversion; this adapter never creates gravity fields.
#pragma once
#include "WorldProcessSurfaceFrame.h"
#include "GranularEcs.h"
#include "MorphEcs.h"
#include "engine/physis/PhysisGravity.h"
#include <optional>
#include <utility>

namespace aiverse::lib::worldprocess {
struct GravitySample {
    Vec3 acceleration{}; // PHYSIS coordinate frame, m/s^2
    Vec3 local{};        // east/up/south axes of the supplied local frame
};
// physisPositionMeters must be expressed in exactly the same coordinates
// as the PHYSIS GravityField centers. Do not use a render/orbit position.
inline GravitySample sampleGravity(const World& world,const SurfaceFrame& frame,
                                    Vec3 physisPositionMeters){
    const auto gravity=physis::gravityAt(world,physisPositionMeters);
    return {gravity,frame.localVector(gravity)};
}
inline bool validLocalGravity(Vec3 g){
    // PHYSIS caps components at 200; any unit-basis projection is <= 600.
    constexpr auto cap=Fixed::fromInt(600);
    return g.x>=-cap&&g.x<=cap&&g.y>=-cap&&g.y<=cap&&g.z>=-cap&&g.z<=cap;
}
// MORPH is a local quantized deformation solver, not Newtonian integration.
// Host provides the conversion from m/s^2 to authored impulse per tick,
// accounting for its units and tick rate; never double-apply rigid gravity.
// For rotated rigid bodies use their actual local frame, not terrain axes.
inline std::optional<morph::Force> morphGravityForce(Entity node,Vec3 local,
                                                     Fixed impulseScale){
    if(!node.valid()||!validLocalGravity(local)||impulseScale<Fixed{}||
       impulseScale>Fixed::fromInt(1000))return {};
    morph::Force force{node,(local.x*impulseScale).toInt(),
        (local.y*impulseScale).toInt(),(local.z*impulseScale).toInt(),true};
    if(!morph::valid(force))return {};
    return force;
}
// GRANULAR's legacy field is a 2D vertical section: +x east, +row down.
// Quantize the in-plane acceleration to the dominant cardinal direction.
// No motion below the dead zone. The out-of-plane component is not simulated.
enum class GravityAxis {None,Down,Up,Left,Right};
inline std::optional<GravityAxis> granularGravityAxis(Vec3 local,
        Fixed deadZone=Fixed::fromRatio(1,100)){
    if(!validLocalGravity(local)||deadZone<Fixed{})return {};
    const auto x=fxAbs(local.x),y=fxAbs(local.y);
    if(fxMax(x,y)<=deadZone)return GravityAxis::None;
    if(y>=x)return local.y.raw<0?GravityAxis::Down:GravityAxis::Up;
    return local.x.raw<0?GravityAxis::Left:GravityAxis::Right;
}
// Reuse the canonical one-row settlement kernel. Remap the grid so its
// positive row direction follows the selected axis, then map mass back.
// The original field's dimensions, obstacles and source coordinates stay
// unchanged. This is directional 2D settlement, not continuous acceleration.
inline bool settleWithGravity(granular::Field& field,uint32_t tick,Vec3 local){
    if(!granular::valid(field))return false;
    if(!field.enabled){field.sampledTick=tick;return true;}
    const auto axis=granularGravityAxis(local);
    if(!axis)return false;
    if(*axis==GravityAxis::None){field.sampledTick=tick;return true;}
    if(*axis==GravityAxis::Down)return granular::settle(field,tick);
    const size_t w=field.width,h=field.height;
    const bool horizontal=*axis==GravityAxis::Left||*axis==GravityAxis::Right;
    granular::Field oriented;
    oriented.width=horizontal?field.height:field.width;
    oriented.height=horizontal?field.width:field.height;
    oriented.capacity=field.capacity;
    oriented.mass.resize(field.mass.size());
    oriented.solid.resize(field.solid.size());
    const auto map=[&](size_t u,size_t v){
        switch(*axis){
            case GravityAxis::Up:return (h-1-v)*w+u;
            case GravityAxis::Left:return u*w+(w-1-v);
            case GravityAxis::Right:return u*w+v;
            default:return v*w+u;
        }
    };
    for(size_t v=0;v<oriented.height;++v)for(size_t u=0;u<oriented.width;++u){
        const auto i=v*oriented.width+u,j=map(u,v);
        oriented.mass[i]=field.mass[j];oriented.solid[i]=field.solid[j];
    }
    if(!granular::settle(oriented,tick))return false;
    for(size_t v=0;v<oriented.height;++v)for(size_t u=0;u<oriented.width;++u)
        field.mass[map(u,v)]=oriented.mass[v*oriented.width+u];
    field.sampledTick=tick;
    return true;
}
// Resolver: optional<Vec3>(const World&, Entity), in the field's local frame.
// Unknown placement fails the entire staged tick without committing sources.
template<class Resolver>
inline bool advanceGranularWithGravity(World& world,uint32_t tick,
                                       const Resolver& resolver){
    return granular::advanceWithSettler(world,tick,
        [&](const World& read,Entity e,granular::Field& field,uint32_t now){
            if(!field.enabled)return granular::settle(field,now);
            const auto local=resolver(read,e);
            return local&&settleWithGravity(field,now,*local);
        });
}
// Register INSTEAD OF the legacy granular::install; never install both.
template<class Resolver>
inline void installGranularWithGravity(Simulation& sim,Resolver resolver,
                                       std::string name="granular.settle"){
    granular::registerPersistence(sim);
    sim.addSystem(std::move(name),[resolver=std::move(resolver)](Simulation& s){
        (void)advanceGranularWithGravity(s.world(),s.tick(),resolver);
    });
}
} // namespace aiverse::lib::worldprocess
