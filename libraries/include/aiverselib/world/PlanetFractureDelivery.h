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
// At a serial authoritative barrier. The existing gameplay owner must
// persist its consequence (shape/pose/identity and original support removal)
// in staged World only. A temporary Box3D body is NOT a durable consequence.
// On rejection or exception the original World and outbox remain untouched.
template<class StageOwner>
inline bool commitPlanetCollapse(World& world,const game::Surface& surface,
        const Event& event,uint32_t completedTick,StageOwner&& stageOwner){
    if(event.tick>completedTick||completedTick==UINT32_MAX)return false;
    const auto projected=projectPlanetCollapse(static_cast<const World&>(world),surface,event);
    if(!projected)return false;
    World staged=world;
    if(!std::forward<StageOwner>(stageOwner)(staged,*projected))return false;
    // Revalidate identity and PHYSIS primary producer after owner staging.
    if(!projectPlanetCollapse(static_cast<const World&>(staged),surface,event)||
       !acknowledge(staged,event))return false;
    world=std::move(staged);
    return true;
}

// Acknowledged support event is a necessary receipt for presentation.
// Host must also resolve its own durable consequence (including restored pose).
inline bool planetCollapseReceipt(const World& world,const Event& event){
    if(!world.alive(event.entity)||!event.key)return false;
    const auto* member=world.tryGetConst<Member>(event.entity);
    const auto* box=world.tryGetConst<Collapse>(event.entity);
    const auto* nature=world.tryGetConst<physis::NatureFractureOutboxComponent>(event.entity);
    return member&&member->detached&&box&&box->delivery==Delivery::Acknowledged&&
        box->eventKey==event.key&&box->detachedTick==event.tick&&
        (!nature||nature->delivery==physis::NatureFractureDelivery::Empty);
}
} // namespace aiverse::fracture
