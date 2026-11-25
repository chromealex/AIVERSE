// Tick-scoped weather routing for the existing planetary EVOLVE ecology consumer.
// Explicit owner-supplied mapping only: never reinterpret PHYSIS planar X/Z
// as spherical coordinates, and never create plant or climate authority.
#pragma once
#include "EvolvePlanetEcologySampling.h"
#include <algorithm>
#include <array>
#include <set>

namespace aiverse::evolve {
inline constexpr size_t kMaxPlanetWeatherMappings=4096;
inline constexpr size_t kMaxPlanetWeatherComparisons=1048576;
struct PlanetWeatherMapping {
    Entity cell=kNullEntity;
    std::string bodyId;
    Vec3 ground{};
    Fixed coverage{}; // maximum same-body chord distance in metres
};
struct PlanetRootClimateSite {
    std::string bodyId;
    Vec3 ground{};
    terraweave::ForestEcologyClimate baseline{};
    bool requireWeather=false;
    bool capWaterBySurfaceWetness=false;
};
struct PlanetRootClimateBatch {
    uint64_t climateTick=0; // PHYSIS State::tick, not Simulation tick
    std::vector<PlanetEcologySample> sites; // same order as supplied sites
};

// Owner barrier: caller freezes World, terrain and owner mapping during this
// synchronous read. The mapping is ephemeral input, not a new weather grid.
// Each actual PHYSIS cell maps to exactly one body/ground point. Source
// choice: shortest eligible same-body chord; ties use packed Entity ID.
// No extrapolation beyond coverage. Optional uncovered sites use baseline.
// A stale/malformed source, insufficient budget or required missing weather
// rejects the entire batch; never publishes partial vegetation state.
inline std::optional<PlanetRootClimateBatch> samplePlanetRootClimate(
        const World& world,const game::Surface& surface,
        std::span<const PlanetWeatherMapping> mappings,
        std::span<const PlanetRootClimateSite> sites,
        uint64_t expectedClimateTick){
    namespace wp=lib::worldprocess;
    if(!surface.sphere()||!surface.tab||mappings.size()>kMaxPlanetWeatherMappings||
       sites.size()>kMaxPlanetEcologySamples)return {};
    const auto* state=world.resourceConst<physis::State>();
    if(!state||state->tick!=expectedClimateTick||state->settings.width<=0||
       state->settings.height<=0||
       uint64_t(state->settings.width)*uint64_t(state->settings.height)!=state->cells.size())return {};
    struct Located {
        PlanetWeatherMapping mapping;
        wp::SurfaceFrame frame;
    };
    std::array<std::vector<Located>,game::Surface::kMaxBodies> byBody;
    std::set<uint64_t> seen;
    for(const auto& mapping:mappings){
        if(mapping.bodyId.empty()||mapping.coverage<Fixed{}||
           mapping.coverage>Fixed::fromInt(4096)||!world.alive(mapping.cell)||
           !seen.insert(mapping.cell.packed()).second)return {};
        const auto frame=wp::surfaceFrame(&surface,mapping.ground);
        if(!frame||surface.bodies[size_t(frame->body)].id!=mapping.bodyId||
           !planet_ecology_detail::grounded(surface,mapping.ground))return {};
        physis::Sample sample;
        if(!physis::read(world,mapping.cell,sample)||
           physis::cellAt(world,sample.cell.x,sample.cell.z)!=mapping.cell||
           !planet_ecology_detail::normalized(sample.atmosphere.humidity)||
           !planet_ecology_detail::normalized(sample.atmosphere.rain)||
           !planet_ecology_detail::normalized(sample.surface.wetness)||
           !planet_ecology_detail::normalized(sample.surface.ice)||sample.surface.snowpack<Fixed{})return {};
        byBody[size_t(frame->body)].push_back({mapping,*frame});
    }
    size_t comparisons=0;
    std::vector<PlanetEcologyRequest> requests;
    requests.reserve(sites.size());
    for(const auto& site:sites){
        const auto frame=wp::surfaceFrame(&surface,site.ground);
        if(!frame||surface.bodies[size_t(frame->body)].id!=site.bodyId||
           !planet_ecology_detail::grounded(surface,site.ground)||
           !planet_ecology_detail::validClimate(site.baseline))return {};
        const auto& candidates=byBody[size_t(frame->body)];
        if(candidates.size()>kMaxPlanetWeatherComparisons-comparisons)return {};
        comparisons+=candidates.size();
        const Located* best=nullptr;
        Fixed bestDistance{};
        for(const auto& candidate:candidates){
            const auto distance=wp::surfaceGroundDistance(&surface,*frame,candidate.frame);
            if(!distance||*distance>candidate.mapping.coverage)continue;
            if(!best||*distance<bestDistance||
               (*distance==bestDistance&&candidate.mapping.cell.packed()<best->mapping.cell.packed())){
                best=&candidate;bestDistance=*distance;
            }
        }
        if(!best&&site.requireWeather)return {};
        PlanetEcologyRequest request{site.bodyId,site.ground,site.baseline,{}};
        if(best)request.weather=PlanetWeatherSource{best->mapping.cell,best->mapping.ground,
            best->mapping.coverage,site.capWaterBySurfaceWetness};
        requests.push_back(std::move(request));
    }
    auto result=samplePlanetEcology(world,surface,requests);
    if(!result)return {};
    return PlanetRootClimateBatch{expectedClimateTick,std::move(*result)};
}
} // namespace aiverse::evolve
