// Canonical GameCore spherical terrain-aware NEREID query.
// The shell is an existing prescribed-level sea, not a finite-volume solver.
// No water body edits or independent hydrology state.
#pragma once
#include "engine/game/Surface.h"
#include "engine/world/nereid/NereidSphereWater.h"
#include "engine/world/nereid/NereidErosion.h"
#include "engine/core/FixedMath.h"
#include <optional>

namespace aiverse::game {
inline int planetWaterBody(const Surface& surface,Vec3 position){
    if(!surface.tab||!Surface::onSphere(position))return -1;
    constexpr int64_t spacing=int64_t(Surface::kBodySpacing)*Fixed::kOne;
    const auto b=(position.x.raw-Surface::kSphereMinRaw)/spacing;
    if(b<0||size_t(b)>=surface.bodies.size()||!surface.bodies[size_t(b)].hasSurface())return -1;
    const auto local=position-Surface::anchor(int(b));
    constexpr auto bound=Fixed::fromInt(2048);
    if(fxAbs(local.x)>bound||fxAbs(local.y)>bound||fxAbs(local.z)>bound||local==Vec3{})return -1;
    return int(b);
}
// Use existing NEREID priority, wave and tangential current policy, but
// constrain candidates to the selected body's centre and actual ground.
inline lib::nereid::SphereWaterSample samplePlanetTerrainWater(
        const World& world,const Surface& surface,Vec3 position,Fixed seconds={}){
    using namespace lib::nereid;
    SphereWaterSample best;
    const int body=planetWaterBody(surface,position);
    if(body<0)return best;
    const auto center=Surface::anchor(body);
    const auto dist=(position-center).length();
    const auto ground=surface.bodies[size_t(body)].radius+surface.heightAt(position);
    world.eachExistingConst<WaterSphereComponent>([&](Entity e,const WaterSphereComponent& sea){
        if(!world.alive(e)||sea.center!=center)return;
        auto sample=sampleWaterSphere(e,sea,position,
            world.tryGetConst<WaterSphereWavesComponent>(e),seconds);
        if(!sample.found)return;
        sample.floorRadius=fxMax(sample.floorRadius,ground);
        if(sample.floorRadius>=sample.surfaceRadius)return;
        sample.submerged=dist<sample.surfaceRadius&&dist>=sample.floorRadius;
        sample.depth=sample.submerged?sample.surfaceRadius-dist:Fixed{};
        if(dist<sample.floorRadius)return; // below solid terrain is not a water contact
        const bool better=!best.found||sample.priority>best.priority||
            (sample.priority==best.priority&&sample.submerged&&!best.submerged)||
            (sample.priority==best.priority&&sample.submerged==best.submerged&&
             (sample.surfaceRadius>best.surfaceRadius||
              (sample.surfaceRadius==best.surfaceRadius&&e.packed()<best.body.packed())));
        if(better)best=sample;
    });
    return best;
}

struct PlanetShoreSample {
    lib::nereid::SphereWaterSample water{};
    lib::nereid::ShoreErosionForcing forcing{};
    Vec3 ground{},up{};
};
// Read-only input for NEREID's material policy. Flow is expressed in the
// local east/south axes; the existing planar speed metric ignores radial Y.
// No sediment update here: its equal-area cm-equivalent storage cannot be
// safely exchanged with the unequal-area spherical mesh without a volume
// conversion/receipt owned by the terrain and sediment authorities.
inline std::optional<PlanetShoreSample> samplePlanetShore(
        const World& world,const Surface& surface,Vec3 position,Fixed seconds,
        uint32_t waveEnergyPermille=0,uint32_t bankSlopePermille=0,
        uint32_t surfaceShieldPermille=0){
    if(planetWaterBody(surface,position)<0||waveEnergyPermille>1000||
       bankSlopePermille>8000||surfaceShieldPermille>1000)return {};
    PlanetShoreSample out;
    out.ground=surface.project(position);out.up=surface.up(out.ground);
    // Move one Fixed quantum above the ground to avoid classifying the
    // rounded radial projection as a point inside solid terrain.
    const auto probe=out.ground+out.up*Fixed::fromRatio(1,256);
    out.water=samplePlanetTerrainWater(world,surface,probe,seconds);
    Vec3 east{},south{};surface.frame(out.ground,east,south);
    out.forcing.flow={out.water.flow.dot(east),Fixed{},out.water.flow.dot(south)};
    out.forcing.wetContactPermille=out.water.submerged?1000u:0u;
    out.forcing.waveEnergyPermille=waveEnergyPermille;
    out.forcing.bankSlopePermille=bankSlopePermille;
    out.forcing.surfaceShieldPermille=surfaceShieldPermille;
    return out;
}
} // namespace aiverse::game
