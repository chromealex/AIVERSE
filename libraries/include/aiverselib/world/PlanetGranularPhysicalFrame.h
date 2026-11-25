// Explicit native presentation/physics placement for planetary GRANULAR.
// No ECS state, no terrain mutation and no independent gravity solver.
#pragma once
#include "PlanetProcessGeometry.h"
#include <string>
#include <string_view>

namespace aiverse::lib::worldprocess {
struct PlanetGranularScale {
    int32_t cellSizeCm=100,depthCm=100,bottomClearanceCm=0;
};
inline bool valid(const PlanetGranularScale& s){
    return s.cellSizeCm>=10&&s.cellSizeCm<=10000&&s.depthCm>=10&&s.depthCm<=10000&&
        s.bottomClearanceCm>=0&&s.bottomClearanceCm<=20000;
}
struct PlanetGranularPhysicalFrame {
    int body=-1;
    std::string bodyId;
    granular::SurfaceFieldPlacement placement;
    int32_t depthCm=0;
    Vec3 publicationOffset{}; // subtract from simulation coordinates BEFORE float conversion
    Vec3 patchOrigin{}; // simulation origin minus publicationOffset
};
// The field Transform identifies the ground anchor. Clearance is explicit;
// an airborne/underground anchor is rejected instead of silently snapped.
// Empty localBody preserves legacy canonical simulation-space publication.
// A nonempty localBody selects a single body-local physics/render frame.
inline std::optional<PlanetGranularPhysicalFrame> resolvePlanetGranularPhysicalFrame(
        const World& world,const game::Surface& surface,Entity e,
        std::string_view expectedBody,const PlanetGranularScale& scale,
        uint32_t completedTick,std::string_view localBody={}){
    if(!world.alive(e)||!valid(scale)||expectedBody.empty()||completedTick==UINT32_MAX)return {};
    const auto* field=world.tryGetConst<granular::Field>(e);
    const auto* tr=world.tryGetConst<Transform>(e);
    if(!field||!tr||!field->enabled||!granular::valid(*field)||field->sampledTick!=completedTick)return {};
    const auto frame=surfaceFrame(&surface,tr->pos);
    if(!frame||surface.bodies[size_t(frame->body)].id!=expectedBody||
       (!localBody.empty()&&localBody!=expectedBody))return {};
    const auto radius=surface.bodies[size_t(frame->body)].radius;
    const auto circumference=radius*game::kFxTwoPi;
    if(radius.raw<=0||Fixed::fromRatio(int64_t(field->width)*scale.cellSizeCm,200)>=circumference/4||
       Fixed::fromRatio(scale.depthCm,200)>=circumference/4||
       fxAbs(surface.altitude(tr->pos)-surface.heightAt(tr->pos))>Fixed::fromRatio(1,8))return {};
    PlanetGranularPhysicalFrame result;
    result.body=frame->body;result.bodyId=std::string(expectedBody);
    result.placement={tr->pos,scale.cellSizeCm,scale.bottomClearanceCm};
    result.depthCm=scale.depthCm;
    if(!localBody.empty())result.publicationOffset=game::Surface::anchor(frame->body);
    result.patchOrigin=tr->pos-result.publicationOffset;
    return result;
}
} // namespace aiverse::lib::worldprocess
