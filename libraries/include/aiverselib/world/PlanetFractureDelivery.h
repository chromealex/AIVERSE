// Planetary projection of the EXISTING durable FRACTURE outbox event.
// No debris spawn, acknowledgement, physical state or second fracture owner.
#pragma once
#include "FractureEcs.h"
#include "MorphPhysisGravity.h"
#include "engine/physis/PhysisFractureOutbox.h"

namespace aiverse::fracture {
struct PlanetCollapseDelivery {
    Event event{};
    lib::worldprocess::SurfaceFrame frame{};
    Vec3 gravity{};
};
inline std::optional<PlanetCollapseDelivery> projectPlanetCollapse(
        const World& world,const game::Surface& surface,const Event& event){
    const auto* member=world.tryGetConst<Member>(event.entity);
    const auto* box=world.tryGetConst<Collapse>(event.entity);
    if(!member||!member->detached||!box||box->delivery!=Delivery::Pending||
       !event.key||event.key!=box->eventKey||event.tick!=box->detachedTick)return {};
    // A PHYSIS primary fall is delivered through its own outbox. A host
    // must explicitly reconcile overlapping producers, not spawn twice.
    const auto* nature=world.tryGetConst<physis::NatureFractureOutboxComponent>(event.entity);
    if(nature&&nature->delivery!=physis::NatureFractureDelivery::Empty)return {};
    const auto frame=morph::gravity_detail::ownerFrame(world,&surface,event.entity);
    if(!frame)return {};
    return PlanetCollapseDelivery{event,*frame,physis::gravityAt(world,frame->origin)};
}
} // namespace aiverse::fracture
