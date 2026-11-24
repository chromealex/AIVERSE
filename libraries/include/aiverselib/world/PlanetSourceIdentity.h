// Stable source identity derived from existing durable consequence records.
// Suppression survives consequence retirement through the original receipt.
// Only explicitly registered project-owned representations are switched.
#pragma once
#include "PlanetConsequenceOwner.h"

namespace aiverse::lib::worldprocess {
struct PlanetSourceKey {
    ConsequenceKind kind=ConsequenceKind::Structure;
    uint64_t source=0;
    uint32_t species=0;
    auto operator<=>(const PlanetSourceKey&)const=default;
};
inline PlanetSourceKey consequenceSource(const ConsequenceKey& k){return {k.kind,k.source,k.species};}
inline bool validPlanetSource(PlanetSourceKey k){
    return k.source&&(k.kind==ConsequenceKind::Structure?!k.species:
        k.kind==ConsequenceKind::Plant&&k.species);
}
inline bool planetSourceRetired(const World& world,PlanetSourceKey key,std::string_view body){
    if(!validPlanetSource(key))return false;
    if(const auto* records=world.resourceConst<PlanetConsequenceResource>())
        for(const auto& record:records->bodies)
            if(record.body==body&&consequenceSource(record.key)==key)return true;
    if(key.kind==ConsequenceKind::Structure){
        const auto e=Entity::fromPacked(key.source);
        const auto* m=world.tryGetConst<fracture::Member>(e);
        const auto* c=world.tryGetConst<fracture::Collapse>(e);
        return world.alive(e)&&m&&m->detached&&c&&c->delivery==fracture::Delivery::Acknowledged;
    }
    // Replanting must use a new stable plant identity; this tombstone
    // must not revive a previously delivered source after debris retirement.
    if(const auto* delivery=world.resourceConst<evolve::PlanetRootDeliveryResource>())
        for(const auto& record:delivery->events)
            if(record.acknowledged&&record.fall.bodyId==body&&
               record.fall.event.id.speciesId==key.species&&record.fall.event.id.key==key.source)return true;
    return false;
}
inline bool validPlanetSourceBinding(const World& world,const game::Surface& surface,
        PlanetSourceKey key,std::string_view body){
    if(!validPlanetSource(key))return false;
    if(key.kind==ConsequenceKind::Structure){
        const auto e=Entity::fromPacked(key.source);
        if(!world.alive(e)||!world.has<fracture::Member>(e))return false;
        const auto frame=morph::gravity_detail::ownerFrame(world,&surface,e);
        return frame&&surface.bodies[size_t(frame->body)].id==body;
    }
    const auto* r=world.resourceConst<evolve::PlanetRootDeliveryResource>();
    if(!r)return false;
    for(const auto& b:r->bindings)
        if(b.id.key==key.source&&b.id.speciesId==key.species)return b.bodyId==body;
    return false;
}
} // namespace aiverse::lib::worldprocess
