// Native consumer for the future canonical planetary rooted flora owner.
// No plant state, recruitment algorithm, weather grid or render cache here.
#pragma once
#include "EvolveSurfaceEcology.h"
#include "engine/physis/Physis.h"
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace aiverse::evolve {
inline constexpr size_t kMaxPlanetEcologySamples=4096;

struct PlanetWeatherSource {
    Entity cell=kNullEntity;
    Vec3 ground{}; // canonical simulation-space location from PHYSIS/host owner
    Fixed maxGroundDistance{};
    // Explicit gameplay approximation. PHYSIS surface wetness is NOT soil
    // water content. Only opt-in owners may use it as an upper bound.
    bool capWaterBySurfaceWetness=false;
};

struct PlanetEcologyRequest {
    std::string expectedBody;
    Vec3 ground{}; // actual plant/site ground position, not an XZ anchor
    terraweave::ForestEcologyClimate baseline{}; // from flora/water owner
    std::optional<PlanetWeatherSource> weather;
};
struct PlanetWeatherObservation {
    Entity cell=kNullEntity;
    Fixed temperature{},humidity{},rain{},wetness{},snowpack{},ice{};
};
struct PlanetEcologySample {
    int body=-1,face=-1,x=-1,y=-1,surfaceCell=-1;
    terraweave::ForestEcologyClimate climate{};
    std::optional<PlanetWeatherObservation> weather;
    size_t contributingRegions=0;
};
namespace planet_ecology_detail {
inline bool validClimate(const terraweave::ForestEcologyClimate& c){
    return c.sunPermille<=1000&&c.floodedPermille<=1000&&
        c.waterPermille<=1000&&c.nutrientsPermille<=1000;
}
inline bool normalized(Fixed v){return v>=Fixed{}&&v<=Fixed::fromInt(1);}
inline bool grounded(const game::Surface& surface,Vec3 position){
    return fxAbs(surface.altitude(position)-surface.heightAt(position))<=
        Fixed::fromRatio(1,8);
}
}

// Synchronous owner-barrier batch, output in input order. Build the canonical
// region index once. No caching across frames or rollbacks. Source positions
// must come from the owner mapping: the existing planar PHYSIS grid must
// never be automatically projected onto a planet. Missing optional weather
// leaves the owner baseline unchanged. Invalid supplied weather fails closed.
// Caller must keep region membership, positions and weather stable during
// sampling; a failure yields no partial result and never mutates World.
inline std::optional<std::vector<PlanetEcologySample>> samplePlanetEcology(
        const World& world,const game::Surface& surface,
        std::span<const PlanetEcologyRequest> requests){
    if(!surface.sphere()||!surface.tab||
       requests.size()>kMaxPlanetEcologySamples)return {};
    SurfaceRegionIndex index;
    if(!index.build(world,surface))return {};
    std::vector<PlanetEcologySample> output;
    output.reserve(requests.size());
    for(const auto& request:requests){
        if(request.expectedBody.empty()||
           !planet_ecology_detail::validClimate(request.baseline))return {};
        const auto point=lib::worldprocess::surfaceFrame(&surface,request.ground);
        if(!point||surface.bodies[size_t(point->body)].id!=request.expectedBody||
           !planet_ecology_detail::grounded(surface,request.ground))return {};
        PlanetEcologySample sample;
        sample.surfaceCell=point->cell;
        if(!surface.cellFace(point->cell,sample.body,sample.face,sample.x,sample.y)||
           sample.body!=point->body)return {};
        sample.climate=request.baseline;
        if(request.weather){
            const auto& source=*request.weather;
            const auto weatherPoint=lib::worldprocess::surfaceFrame(&surface,source.ground);
            if(!weatherPoint||weatherPoint->body!=point->body||
               !planet_ecology_detail::grounded(surface,source.ground)||
               source.maxGroundDistance<Fixed{}||
               source.maxGroundDistance>Fixed::fromInt(4096))return {};
            const auto distance=lib::worldprocess::surfaceGroundDistance(&surface,*point,*weatherPoint);
            if(!distance||*distance>source.maxGroundDistance)return {};
            const auto* air=world.tryGetConst<physis::Atmosphere>(source.cell);
            const auto* soil=world.tryGetConst<physis::Surface>(source.cell);
            if(!air||!soil||!planet_ecology_detail::normalized(air->humidity)||
               !planet_ecology_detail::normalized(air->rain)||
               !planet_ecology_detail::normalized(soil->wetness)||
               !planet_ecology_detail::normalized(soil->ice)||soil->snowpack<Fixed{})return {};
            sample.weather=PlanetWeatherObservation{source.cell,air->temperature,
                air->humidity,air->rain,soil->wetness,soil->snowpack,soil->ice};
            if(source.capWaterBySurfaceWetness){
                const auto wet=uint32_t(soil->wetness.raw*1000/Fixed::kOne);
                sample.climate.waterPermille=std::min(sample.climate.waterPermille,wet);
            }
        }
        index.query(world,surface,request.ground,[&](Entity,const Region& region){
            sample.climate=projectEvolutionClimate(region,sample.climate);
            ++sample.contributingRegions;
        });
        output.push_back(std::move(sample));
    }
    return output;
}
} // namespace aiverse::evolve
