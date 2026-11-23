// Authoritative map -> ECS hydration for opt-in World Processes components.
// Map IDs resolve through actual, generation-safe ECS Entities from the host;
// no persistent parallel entity registry, no genre-specific physics or renderer.
#pragma once
#include "WorldProcessesLinkedAuthoring.h"
#include "WorldProcessesExtendedAuthoring.h"
#include <array>
#include <cstddef>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::lib::worldprocess::authoring {

struct MapHydration {
    std::size_t zones=0,members=0,gasCells=0;
    std::size_t portals=0,emitters=0,receivers=0;
    std::size_t bonds=0,ducts=0,fluxes=0;
    SuiteFlags extra{};
};

// Call exactly once DURING GameModule::setup after spawning the map entities,
// but BEFORE the Simulation persistence registry is sealed.
// A host supplies its exact, ordered map["entities"] -> Entity pairing.
// All documents, references, budgets and existing component ownership are
// checked first; only then are schemas registered and ECS components created.
inline MapHydration hydrateMap(Simulation& sim,const Json& map,
                               const std::vector<Entity>& placed,
                               SuiteFlags requested={}) {
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
    std::vector<Pending> basic,linkedParts,extendedParts;
    std::map<std::string,Entity> ids;
    bool repeatedId=false;
    MapHydration totals;
    constexpr std::array<std::string_view,3> basicTypes{
        "EchoZone","FractureMember","AetherGasCell"};
    constexpr std::array<std::string_view,6> linkedTypes{
        "EchoPortal","EchoEmitter","EchoReceiver",
        "FractureBond","AetherDuct","AetherFlux"};
    constexpr std::array<std::string_view,7> extendedTypes{
        "MorphNode","CrowdAgent","ImprintCell","CircuitNode",
        "GranularField","EvolveRegion","SocietyPerson"};

    for(std::size_t i=0;i<entities.size();++i){
        const auto id=entities[i].getStr("id");
        if(!id.empty() && !ids.emplace(id,placed[i]).second)repeatedId=true;
        const auto& components=entities[i]["components"];
        if(!components.isObject())continue;
        for(const auto kind:basicTypes){
            const auto name=std::string(kind);
            if(!components.has(name))continue;
            if(!sim.world().alive(placed[i]))
                throw std::invalid_argument("World Processes: component on unspawned entity");
            std::string error;
            if(!validate(kind,components[name],&error))
                throw std::invalid_argument("World Processes "+name+": "+error);
            if((kind=="EchoZone"&&sim.world().has<echo::AcousticZone>(placed[i]))||
               (kind=="FractureMember"&&sim.world().has<fracture::Member>(placed[i]))||
               (kind=="AetherGasCell"&&sim.world().has<aether::GasCell>(placed[i])))
                throw std::invalid_argument("World Processes: duplicate component owner");
            basic.push_back({placed[i],&components[name],kind});
            if(kind=="EchoZone")++totals.zones;
            else if(kind=="FractureMember")++totals.members;
            else ++totals.gasCells;
        }
        for(const auto kind:extendedTypes){
            const auto name=std::string(kind);
            if(!components.has(name))continue;
            if(!sim.world().alive(placed[i]))
                throw std::invalid_argument("World Processes: unspawned extended entity");
            std::string error;
            if(!extended::validate(kind,components[name],&error))
                throw std::invalid_argument("World Processes "+name+": "+error);
            if(extended::hasExisting(sim.world(),placed[i],kind))
                throw std::invalid_argument("World Processes: duplicate extended ECS component");
            extendedParts.push_back({placed[i],&components[name],kind});
            if(kind=="MorphNode")totals.extra.morph=true;
            else if(kind=="CrowdAgent")totals.extra.crowd=true;
            else if(kind=="ImprintCell")totals.extra.imprint=true;
            else if(kind=="CircuitNode")totals.extra.circuit=true;
            else if(kind=="GranularField")totals.extra.granular=true;
            else if(kind=="EvolveRegion")totals.extra.evolve=true;
            else if(kind=="SocietyPerson")totals.extra.society=true;
        }
        for(const auto kind:linkedTypes){
            const auto name=std::string(kind);
            if(!components.has(name))continue;
            if(!sim.world().alive(placed[i]))
                throw std::invalid_argument("World Processes: linked component on unspawned entity");
            std::string error;
            if(!linked::validate(kind,components[name],&error))
                throw std::invalid_argument("World Processes "+name+": "+error);
            const auto entity=placed[i];
            if((kind=="EchoPortal"&&sim.world().has<echo::AcousticPortal>(entity))||
               (kind=="EchoEmitter"&&sim.world().has<echo::AcousticEmitter>(entity))||
               (kind=="EchoReceiver"&&sim.world().has<echo::AcousticReceiver>(entity))||
               (kind=="FractureBond"&&sim.world().has<fracture::Bond>(entity))||
               (kind=="AetherDuct"&&sim.world().has<aether::GasDuct>(entity))||
               (kind=="AetherFlux"&&sim.world().has<aether::GasFlux>(entity)))
                throw std::invalid_argument("World Processes: duplicate linked component owner");
            linkedParts.push_back({entity,&components[name],kind});
            if(kind=="EchoPortal")++totals.portals;
            else if(kind=="EchoEmitter")++totals.emitters;
            else if(kind=="EchoReceiver")++totals.receivers;
            else if(kind=="FractureBond")++totals.bonds;
            else if(kind=="AetherDuct")++totals.ducts;
            else ++totals.fluxes;
        }
    }
    if(totals.zones>echo::kMaxZones || totals.portals>echo::kMaxPortals ||
       totals.emitters>echo::kMaxEmitters || totals.receivers>echo::kMaxZones ||
       totals.members>fracture::kMaxMembers ||
       totals.bonds>fracture::kMaxBonds ||
       totals.gasCells>aether::kMaxCells ||
       totals.ducts>aether::kMaxDucts ||
       totals.fluxes>aether::kMaxFluxes)
        throw std::invalid_argument("World Processes: map component budget exceeded");

    if(!linkedParts.empty()&&repeatedId)
        throw std::invalid_argument("World Processes: ambiguous duplicate map entity ID");
    // Preflight every nonempty stable ID, even when a link is disabled:
    // disabling a stale link does not magically rebind it to a recycled ECS id.
    for(const auto& item:linkedParts){
        for(const auto& [key,value]:item.settings->members()){
            if(!linked::isReference(item.component,key)||value.str().empty())continue;
            auto it=ids.find(value.str());
            if(it==ids.end()||!sim.world().alive(it->second))
                throw std::invalid_argument("World Processes: stale map entity ref "+value.str());
            const auto targetType=linked::targetType(item.component);
            // A target must have this canonical ECS component already, or a
            // validated authored config in the same map that will hydrate
            // before any linked components are applied.
            bool ready=false;
            if(targetType=="EchoZone")ready=sim.world().has<echo::AcousticZone>(it->second);
            if(targetType=="FractureMember")ready=sim.world().has<fracture::Member>(it->second);
            if(targetType=="AetherGasCell")ready=sim.world().has<aether::GasCell>(it->second);
            if(!ready){
                for(const auto& basicPart:basic)
                    if(basicPart.entity==it->second&&basicPart.component==targetType){
                        ready=true;break;
                    }
            }
            if(!ready)throw std::invalid_argument(
                "World Processes: incompatible target for "+std::string(item.component));
        }
    }
    // Explicit native-module flags may also enable an empty category: its
    // producer can create ECS components after setup. An authored category
    // always enables the same system, without duplicate registration.
    totals.extra.morph |= requested.morph;
    totals.extra.crowd |= requested.crowd;
    totals.extra.imprint |= requested.imprint;
    totals.extra.circuit |= requested.circuit;
    totals.extra.granular |= requested.granular;
    totals.extra.evolve |= requested.evolve;
    totals.extra.society |= requested.society;
    // Schema registrations are deterministic and opt-in. Unconfigured maps
    // do not alter the legacy manifest. Only the host owns spawn timing.
    if(totals.zones||totals.portals||totals.emitters||totals.receivers)
        echo::install(sim);
    if(totals.members||totals.bonds)fracture::install(sim);
    if(totals.gasCells||totals.ducts||totals.fluxes)aether::install(sim);

    for(const auto& part:basic){
        std::string error;
        if(!apply(sim.world(),part.entity,part.component,*part.settings,&error))
            throw std::invalid_argument(
                "World Processes: failed basic component "+std::string(part.component)+" "+error);
    }
    // Install all seven systems in one well-defined order. The map stores no
    // duplicated runtime state; opt-in categories use portable ECS schemas.
    installSuite(sim,totals.extra);
    for(const auto& part:extendedParts){
        std::string error;
        if(!extended::apply(sim.world(),part.entity,part.component,*part.settings,&error))
            throw std::invalid_argument(
                "World Processes: failed extended component "+std::string(part.component)+" "+error);
    }
    for(const auto& part:linkedParts){
        std::string error;
        if(!linked::apply(sim.world(),part.entity,part.component,*part.settings,ids,&error))
            throw std::invalid_argument(
                "World Processes: failed linked component "+std::string(part.component)+" "+error);
        // A listener needs the authoritative hearing output pool to receive
        // the shared ECHO propagation result; it is never authored as map data.
        if(part.component=="EchoReceiver" &&
           !sim.world().has<echo::AcousticHearing>(part.entity))
            sim.world().add<echo::AcousticHearing>(part.entity);
    }
    return totals;
}
} // namespace aiverse::lib::worldprocess::authoring
