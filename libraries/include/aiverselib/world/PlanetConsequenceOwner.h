// Optional durable consequence owner for native projects. Fixed-only
// records; native physics handles and floating point never enter persistence.
#pragma once
#include "PlanetPhysicalDelivery.h"
#include "engine/core/FixedQuaternion.h"
namespace aiverse::lib::worldprocess {
enum class ConsequenceKind:uint8_t {Structure,Plant};
enum class ConsequenceShape:uint8_t {Box,Sphere,Capsule};
struct ConsequenceKey {
    ConsequenceKind kind=ConsequenceKind::Structure;
    uint64_t source=0,event=0,revision=0;
    uint32_t species=0,tick=0;
    auto operator<=>(const ConsequenceKey&)const=default;
};
inline ConsequenceKey consequenceKey(const fracture::Event& e){
    return {ConsequenceKind::Structure,e.entity.packed(),e.key,0,0,e.tick};
}
inline ConsequenceKey consequenceKey(const evolve::PlanetPlantFall& f){
    return {ConsequenceKind::Plant,f.event.id.key,0,f.event.sourceSoilRevision,f.event.id.speciesId,uint32_t(f.event.tick)};
}
struct ConsequenceMotion {
    Vec3 position{},linear{},angular{};
    FixedQuaternion rotation{};
    bool awake=true;
};
struct ConsequenceBody {
    ConsequenceKey key;
    std::string body;
    ConsequenceShape shape=ConsequenceShape::Box;
    Vec3 dimensions{1_fx,1_fx,1_fx}; // box extents; sphere x radius; capsule x radius/y half-height
    Fixed density=1000_fx,friction=Fixed::fromRatio(1,2);
    ConsequenceMotion motion;
    uint32_t lastMotionTick=UINT32_MAX;
};
struct PlanetConsequenceResource {std::vector<ConsequenceBody> bodies;};
inline constexpr size_t kMaxConsequences=4096;
inline bool validConsequence(const ConsequenceBody& b){
    const auto bounded=[](Vec3 v,Fixed n){return fxAbs(v.x)<=n&&fxAbs(v.y)<=n&&fxAbs(v.z)<=n;};
    const auto& k=b.key;
    const auto& q=b.motion.rotation;
    if(k.tick==UINT32_MAX||b.body.empty()||b.body.size()>128||
       (k.kind==ConsequenceKind::Structure?(!Entity::fromPacked(k.source).valid()||!k.event||k.species||k.revision):
        (k.kind!=ConsequenceKind::Plant||!k.source||k.event||!k.species||!k.revision))||
       b.shape>ConsequenceShape::Capsule||b.density<1_fx||b.density>Fixed::fromInt(1000000)||
       b.friction<Fixed{}||b.friction>10_fx||
       !bounded(b.motion.position,Fixed::fromInt(1000000))||
       !bounded(b.motion.linear,1000_fx)||!bounded(b.motion.angular,1000_fx)||
       !fxQuaternionBounded(q)||fxAbs(q.x*q.x+q.y*q.y+q.z*q.z+q.w*q.w-1_fx)>Fixed::fromRatio(1,100)||
       (b.lastMotionTick!=UINT32_MAX&&b.lastMotionTick<k.tick))return false;
    for(auto v:{b.dimensions.x,b.dimensions.y,b.dimensions.z})
        if(v<Fixed::fromRatio(1,50)||v>1000_fx)return false;
    return true;
}
inline void io(Archive& a,ConsequenceBody& b){
    auto& k=b.key;auto& m=b.motion;
    a(k.kind,k.source,k.event,k.revision,k.species,k.tick,b.body,b.shape,b.dimensions,b.density,b.friction,
      m.position,m.linear,m.angular,m.rotation.x,m.rotation.y,m.rotation.z,m.rotation.w,m.awake,b.lastMotionTick);
    if(!validConsequence(b))a.fail();
}
inline void io(Archive& a,PlanetConsequenceResource& r){
    const auto n=a.count(r.bodies.size(),80);
    if(!a.ok()||n>kMaxConsequences){a.fail();return;}
    if(!a.writing())r.bodies.resize(n);
    for(auto& b:r.bodies)a(b);
    for(size_t i=1;i<r.bodies.size();++i)if(!(r.bodies[i-1].key<r.bodies[i].key))a.fail();
}
inline void registerPlanetConsequencePersistence(Simulation& sim){
    sim.registerPersistentResource<PlanetConsequenceResource>("worldprocess.planet_consequences",1);
    if(!sim.world().resourceConst<PlanetConsequenceResource>())sim.world().setResource<PlanetConsequenceResource>();
}
inline bool stageConsequence(World& world,const game::Surface& surface,ConsequenceBody body){
    const auto* resource=world.resourceConst<PlanetConsequenceResource>();
    const auto frame=surfaceFrame(&surface,body.motion.position);
    if(!resource||resource->bodies.size()>=kMaxConsequences||!validConsequence(body)||
       !frame||surface.bodies[size_t(frame->body)].id!=body.body)return false;
    auto next=*resource;
    auto it=std::lower_bound(next.bodies.begin(),next.bodies.end(),body.key,
        [](const auto& b,const auto& key){return b.key<key;});
    if(it!=next.bodies.end()&&it->key==body.key)return false;
    next.bodies.insert(it,std::move(body));world.setResource<PlanetConsequenceResource>(std::move(next));return true;
}
// Explicit owner callbacks supply real shape and remove the original
// authoritative support/visual representation in staged ECS. Returning
// false aborts both creation and original outbox acknowledgement.
template<class StructureOwner,class PlantOwner>
inline bool deliverPlanetConsequences(World& world,const game::Surface& surface,
        std::string_view body,uint32_t tick,StructureOwner&& structure,PlantOwner&& plant){
    return commitPlanetPhysicalDelivery(world,surface,body,tick,
        [&](World& staged,const fracture::PlanetCollapseDelivery& event){
            auto result=structure(staged,event);
            if(!result)return false;
            result->key=consequenceKey(event.event);result->body=std::string(body);
            return stageConsequence(staged,surface,std::move(*result));
        },[&](World& staged,const evolve::PlanetPlantFall& event){
            auto result=plant(staged,event);
            if(!result)return false;
            result->key=consequenceKey(event);result->body=std::string(body);
            return stageConsequence(staged,surface,std::move(*result));
        });
}
struct ConsequenceCheckpoint {ConsequenceKey key;ConsequenceMotion motion;};
inline bool commitConsequenceMotion(World& world,const game::Surface& surface,
        std::string_view body,uint32_t tick,std::span<const ConsequenceCheckpoint> states){
    const auto* source=world.resourceConst<PlanetConsequenceResource>();
    if(!source||tick==UINT32_MAX||states.size()>kMaxConsequences)return false;
    auto next=*source;
    std::map<ConsequenceKey,ConsequenceMotion> motion;
    for(const auto& state:states)if(!motion.emplace(state.key,state.motion).second)return false;
    for(auto& b:next.bodies){
        if(b.body!=body)continue;
        auto it=motion.find(b.key);
        if(it==motion.end()||(b.lastMotionTick!=UINT32_MAX&&b.lastMotionTick>=tick)||tick<b.key.tick)return false;
        b.motion=it->second;b.lastMotionTick=tick;motion.erase(it);
        const auto frame=surfaceFrame(&surface,b.motion.position);
        if(!validConsequence(b)||!frame||surface.bodies[size_t(frame->body)].id!=body)return false;
    }
    if(!motion.empty())return false;
    world.setResource<PlanetConsequenceResource>(std::move(next));return true;
}
} // namespace aiverse::lib::worldprocess
