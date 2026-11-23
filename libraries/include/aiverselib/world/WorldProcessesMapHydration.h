// Authoritative map -> ECS hydration for opt-in World Processes components.
// The authoritative map/Entity pairing comes from the host, not from a
// parallel entity registry or any genre-specific physics/renderer objects.
#pragma once
#include "WorldProcessesAuthoring.h"
#include <array>
#include <cstddef>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::lib::worldprocess::authoring {

struct MapHydration {
    std::size_t zones=0, members=0, gasCells=0;
};
// Call exactly once DURING GameModule::setup after spawning the map entities,
// but BEFORE the Simulation persistence registry is sealed.
// A host passes its exact, ordered map["entities"] -> Entity mapping. Invalid
// schemas or mappings fail before component registration or ECS mutations.
inline MapHydration hydrateMap(Simulation& sim,const Json& map,
                               const std::vector<Entity>& placed) {
    const Json& entities=map["entities"];
    if(!entities.isArray()) {
        if(!placed.empty())throw std::invalid_argument("World Processes: map.entities missing");
        return {};
    }
    if(entities.size()!=placed.size())
        throw std::invalid_argument("World Processes: map ECS placement count mismatch");
    struct Pending {
        Entity entity=kNullEntity;
        const Json* settings=nullptr;
        std::string_view component;
    };
    std::vector<Pending> changes;
    MapHydration totals;
    // Fixed order is part of the save manifest and simulation checksum;
    // it cannot depend on JSON member insertion or ECS sparse-set order.
    constexpr std::array<std::string_view,3> kinds{
        "EchoZone","FractureMember","AetherGasCell"};
    for(std::size_t i=0;i<entities.size();++i){
        const auto& components=entities[i]["components"];
        if(!components.isObject())continue;
        for(auto kind:kinds){
            const auto key=std::string(kind);
            if(!components.has(key))continue;
            if(!sim.world().alive(placed[i]))
                throw std::invalid_argument("World Processes: authoring data on an unspawned entity");
            std::string error;
            if(!validate(kind,components[key],&error))
                throw std::invalid_argument("World Processes "+key+": "+error);
            changes.push_back({placed[i],&components[key],kind});
            if(kind=="EchoZone")++totals.zones;
            else if(kind=="FractureMember")++totals.members;
            else ++totals.gasCells;
        }
    }
    // Enforce bounded runtime policy counts even if the map is huge.
    if(totals.zones>echo::kMaxZones ||
       totals.members>fracture::kMaxMembers ||
       totals.gasCells>aether::kMaxCells)
        throw std::invalid_argument("World Processes: map component budget exceeded");
    // Schema registrations are opt-in. Maps with no World Processes ECS remain
    // byte-compatible with v9 legacy gameplay and do not install new systems.
    if(totals.zones) echo::install(sim);
    if(totals.members) fracture::install(sim);
    if(totals.gasCells) aether::install(sim);
    for(const auto& edit:changes){
        std::string error;
        if(!apply(sim.world(),edit.entity,edit.component,*edit.settings,&error))
            throw std::invalid_argument(
                "World Processes: duplicate or invalid map component: "+
                std::string(edit.component)+" "+error);
    }
    return totals;
}
} // namespace aiverse::lib::worldprocess::authoring
