// Planetary GRANULAR consumer boundary for a canonical terrain owner.
// No terrain store, height mutation, brush solver or scheduler is defined here.
#pragma once
#include "GranularSurfaceField.h"
#include "PlanetTerrainFootprint.h"
#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace aiverse::granular {
enum class PlanetSoilDirection : uint8_t { Dig, Fill };
struct PlanetSoilRequest {
    Entity field=kNullEntity;
    std::string expectedBody; // stable authored identity, not render/orbit position
    uint16_t column=0,row=0;
    int32_t cellSizeCm=100;
    PlanetSoilDirection direction=PlanetSoilDirection::Fill;
    uint64_t maximumVolumeCm3=0,expectedTerrainRevision=0;
};
struct PlanetSoilTarget {
    std::string bodyId;
    int body=-1,face=-1,x=-1,y=-1,surfaceCell=-1;
    Vec3 ground{},up{};
    uint32_t completedTick=0;
    uint64_t expectedTerrainRevision=0,volumeBudgetCm3=0;
    PlanetSoilDirection direction=PlanetSoilDirection::Fill;
    lib::worldprocess::PlanetTerrainFootprint footprint;
};
struct PlanetSoilOwnerChange {
    uint64_t beforeRevision=0,afterRevision=0,movedVolumeCm3=0;
};
struct PlanetSoilTransferResult {
    PlanetSoilTarget target;
    PlanetSoilOwnerChange terrain;
    I beforeMass=0,afterMass=0;
};

// Field mass is calibrated as 1 cm^3 per unit only for this opt-in API.
// Terrain owner computes actual conservative volume (including seam alias
// vertices, quantization, bedrock and derived-state invalidation). The
// bridge never estimates volume from the nominal surface cell area.
inline std::optional<PlanetSoilTarget> planPlanetSoilTransfer(
        const World& world,const game::Surface& surface,
        const PlanetSoilRequest& request,uint32_t completedTick){
    if(!world.alive(request.field)||request.expectedBody.empty()||
       request.maximumVolumeCm3==0||completedTick==UINT32_MAX||
       (request.direction!=PlanetSoilDirection::Dig&&
        request.direction!=PlanetSoilDirection::Fill))return {};
    const auto* field=world.tryGetConst<Field>(request.field);
    const auto* tr=world.tryGetConst<Transform>(request.field);
    if(!field||!tr||!field->enabled||!valid(*field)||
       field->sampledTick!=completedTick||request.column>=field->width||
       request.row!=field->height-1)return {};
    const auto i=size_t(request.row)*field->width+request.column;
    if(field->solid[i])return {};
    const auto frame=lib::worldprocess::surfaceFrame(&surface,tr->pos);
    if(!frame||surface.bodies[size_t(frame->body)].id!=request.expectedBody)return {};
    // Reject an airborne / underground field rather than silently projecting
    // its base to contact. Tolerance accommodates Fixed surface rounding.
    if(fxAbs(surface.altitude(tr->pos)-surface.heightAt(tr->pos))>
       Fixed::fromRatio(1,8))return {};
    const SurfaceFieldPlacement placement{tr->pos,request.cellSizeCm,0};
    const auto grain=surfaceGrainCell(surface,*field,placement,request.column,request.row);
    if(!grain||grain->body!=frame->body)return {};
    PlanetSoilTarget target;
    if(!surface.cellFace(grain->surfaceCell,target.body,target.face,target.x,target.y)||
       target.body!=frame->body)return {};
    const auto footprint=lib::worldprocess::planetTerrainFootprint(surface,target.body,grain->surfaceCell);
    if(!footprint||footprint->bodyId!=request.expectedBody)return {};
    target.footprint=*footprint;
    target.bodyId=request.expectedBody;target.surfaceCell=grain->surfaceCell;
    target.ground=grain->ground;target.up=grain->up;
    target.completedTick=completedTick;target.direction=request.direction;
    target.expectedTerrainRevision=request.expectedTerrainRevision;
    const I available=request.direction==PlanetSoilDirection::Fill?
        field->mass[i]:field->capacity-field->mass[i];
    target.volumeBudgetCm3=std::min(request.maximumVolumeCm3,uint64_t(available));
    if(!target.volumeBudgetCm3)return {};
    return target;
}

namespace planet_soil_detail {
inline bool sameField(const Field& a,const Field& b){
    return a.width==b.width&&a.height==b.height&&a.capacity==b.capacity&&
        a.mass==b.mass&&a.solid==b.solid&&a.enabled==b.enabled&&
        a.sampledTick==b.sampledTick;
}
}

// At a serial authoritative tick barrier ONLY, after settling and before
// publishing terrain. The owner callback must mutate only the staged World
// (including its persistent owner resources), never external objects, shared
// mutable resources, Surface through const_cast, or presentation. It must
// validate the terrain revision/body and compute exact moved volume <= budget.
// No callback on invalid input. Failure, exception or inconsistent receipt
// discards the staged World. A zero-volume receipt also discards staged writes.
// Successful commit invalidates outstanding World pointers/references.
// This is a bounded single-request transaction, not a per-grain hot path.
template<class TerrainOwner>
inline std::optional<PlanetSoilTransferResult> transferPlanetSoil(
        World& world,const game::Surface& surface,const PlanetSoilRequest& request,
        uint32_t completedTick,TerrainOwner&& owner){
    const auto target=planPlanetSoilTransfer(world,surface,request,completedTick);
    if(!target)return {};
    const World& read=world;
    const auto& field=*read.tryGetConst<Field>(request.field);
    const auto i=size_t(request.row)*field.width+request.column;
    const auto before=field.mass[i];
    const auto origin=read.tryGetConst<Transform>(request.field)->pos;
    World staged=read;
    const std::optional<PlanetSoilOwnerChange> receipt=
        std::forward<TerrainOwner>(owner)(staged,*target);
    if(!receipt||receipt->beforeRevision!=target->expectedTerrainRevision||
       receipt->movedVolumeCm3>target->volumeBudgetCm3)return {};
    const auto* next=static_cast<const World&>(staged).tryGetConst<Field>(request.field);
    const auto* nextTransform=static_cast<const World&>(staged).tryGetConst<Transform>(request.field);
    if(!next||!nextTransform||!planet_soil_detail::sameField(field,*next)||
       !(nextTransform->pos==origin))return {};
    const auto quantity=I(receipt->movedVolumeCm3);
    if(!quantity){
        if(receipt->afterRevision!=receipt->beforeRevision)return {};
        return PlanetSoilTransferResult{*target,*receipt,before,before};
    }
    if(receipt->afterRevision<=receipt->beforeRevision)return {};
    const auto after=request.direction==PlanetSoilDirection::Fill?
        before-quantity:before+quantity;
    // All potentially allocating work (including COW detachment and receipt
    // construction) happens before the final World move.
    staged.get<Field>(request.field).mass[i]=after;
    PlanetSoilTransferResult result{*target,*receipt,before,after};
    world=std::move(staged);
    return result;
}
} // namespace aiverse::granular
