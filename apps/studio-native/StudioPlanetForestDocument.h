// Studio map-owned forest documents. Native recipe validation is shared;
// never mutate live ECS, rules files, or an independent undo stack.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include "aiverselib/world/WorldProcessesPlanetForestPipeline.h"
#include "engine/world/WorldAssets.h"
#include <set>
namespace aiverse::studio {
inline const Json* planetForestModuleConfig(const Json& rules){
    for(const auto& entry:rules["nativeModules"].items())
        if(entry.getStr("id")=="world_processes")return &entry["config"];
    return nullptr;
}
inline Json studioPlanetForestDocument(const editor::MapDoc& doc,const Json& rules){
    Json out=Json::object(),markers=Json::array();
    out.set("available",Json::fromBool(doc.data.planet()));
    out.set("mapPath",Json::string(doc.path));
    out.set("revision",Json::string(std::to_string(doc.revision)));
    const auto* module=planetForestModuleConfig(rules);
    out.set("moduleEnabled",Json::fromBool(module!=nullptr));
    out.set("physicsRequested",Json::fromBool(module&&module->getBool("planetForestPhysics",false)));
    out.set("override",Json::fromBool(doc.data.worldExtra.has("planetForest")));
    out.set("forest",doc.data.worldExtra.has("planetForest")?doc.data.worldExtra["planetForest"]:
        module?(*module)["planetForest"]:Json{});
    out.set("catalog",doc.data.worldExtra["terraweaveForest"]);
    for(const auto& e:doc.data.entities){
        if(e.id.empty()||!e.at.isObject())continue;
        const int bi=doc.data.findBody(e.at.getStr("body"));
        if(bi<0||!doc.data.bodies[size_t(bi)].surface())continue;
        Json row=Json::object();row.set("id",Json::string(e.id));row.set("type",Json::string(e.type));
        row.set("placement",e.at);markers.push(std::move(row));
    }
    out.set("markers",std::move(markers));return out;
}
inline void validateStudioPlanetForest(const editor::MapDoc& doc,const Json& rules,
        const Json& forest,const Json& catalog){
    namespace wp=lib::worldprocess;
    if(!doc.data.planet())throw std::invalid_argument("Planet forest requires a planetary map");
    Json map=doc.toJson();
    if(!catalog.isNull()){
        if(!catalog.isObject()||!catalog["species"].isArray()||catalog["species"].size()==0||catalog["species"].size()>64)
            throw std::invalid_argument("ARBOR catalog requires 1..64 species");
        if(catalog.has("botanicalTextures")&&(!catalog["botanicalTextures"].isString()||
           (!catalog.getStr("botanicalTextures").empty()&&!world::safeAssetPath(catalog.getStr("botanicalTextures")))))
            throw std::invalid_argument("Invalid botanical texture content directory");
        std::set<uint32_t> ids;
        for(const auto& entry:catalog["species"].items()){
            const auto recipe=world::parseForestSpeciesProfile(entry);
            if(!ids.insert(recipe.profile.speciesId).second)throw std::invalid_argument("Duplicate ARBOR species");
            (void)wp::planetArborTrunk(recipe); // exact generator validation
        }
        map.member("world").set("terraweaveForest",catalog);
    }else map.member("world").erase("terraweaveForest");
    const auto* module=planetForestModuleConfig(rules);
    const auto& effective=forest.isNull()?(module?(*module)["planetForest"]:forest):forest;
    if(effective.isNull())return;
    if(!module)throw std::invalid_argument("Enable the project's world_processes native module first");
    if(module->has("planetBanks")&&!(*module)["planetBanks"].getBool("rootFalls"))
        throw std::invalid_argument("planetBanks.rootFalls must be enabled with forest");
    const auto cfg=wp::parsePlanetForestConfig(effective,map);
    const auto integer=[](const Json& j,const char* k,int64_t d,int64_t lo,int64_t hi){return wp::planetBankInteger(j,k,d,lo,hi);};
    const auto marker=[&](const Json& item){
        if(!item.isObject()||!item["entity"].isString())throw std::invalid_argument("Missing plant/weather marker");
        const int i=doc.findEntity(item.getStr("entity"));
        if(i<0)throw std::invalid_argument("Unresolved plant/weather marker: "+item.getStr("entity"));
        const auto& at=doc.data.entities[size_t(i)].at;
        const int bi=doc.data.findBody(at.getStr("body"));
        if(!at.isObject()||bi<0||!doc.data.bodies[size_t(bi)].surface())
            throw std::invalid_argument("Marker must be placed on a planetary surface");
    };
    std::set<std::pair<uint32_t,uint64_t>> ids;
    std::set<uint32_t> species;
    for(const auto& s:cfg.species)species.insert(s.speciesId);
    if(effective["plants"].size()>cfg.ecology.maxTrackedPlants)throw std::invalid_argument("Forest plant budget");
    for(const auto& item:effective["plants"].items()){
        marker(item);
        const auto id=uint32_t(integer(item,"species",0,1,1000000));
        const auto key=uint64_t(integer(item,"key",0,1,1000000000));
        if(!key||!species.contains(id)||!ids.emplace(id,key).second)throw std::invalid_argument("Invalid or duplicate plant identity");
        const auto habit=item.getStr("habit","tree");
        if(habit!="tree"&&habit!="shrub")throw std::invalid_argument("Plant habit must be tree or shrub");
        (void)integer(item,"ageTicks",0,0,1000000000);
    }
    if(effective.has("weather")&&(!effective["weather"].isArray()||effective["weather"].size()>evolve::kMaxPlanetWeatherMappings))
        throw std::invalid_argument("Forest weather mapping budget");
    std::set<std::pair<int64_t,int64_t>> weather;
    for(const auto& item:effective["weather"].items()){
        marker(item);
        const auto x=integer(item,"x",0,-1000000,1000000),z=integer(item,"z",0,-1000000,1000000);
        if(!weather.emplace(x,z).second)throw std::invalid_argument("Duplicate PHYSIS cell mapping");
        (void)integer(item,"coverageMetres",32,1,4096);
    }
}
inline bool editStudioPlanetForest(editor::MapDoc& doc,const Json& rules,const Json& params){
    if(!params["mapPath"].isString()||params.getStr("mapPath")!=doc.path||
       !params["expectedRevision"].isString()||params.getStr("expectedRevision")!=std::to_string(doc.revision))
        throw std::invalid_argument("Stale planet forest document; reload before editing");
    if(!params.has("forest")||!params.has("catalog"))throw std::invalid_argument("Complete forest and catalog required");
    const auto& forest=params["forest"];const auto& catalog=params["catalog"];
    validateStudioPlanetForest(doc,rules,forest,catalog);
    auto next=doc.data.worldExtra;
    if(forest.isNull())next.erase("planetForest");else next.set("planetForest",forest);
    if(catalog.isNull())next.erase("terraweaveForest");else next.set("terraweaveForest",catalog);
    if(next==doc.data.worldExtra)return false;
    doc.beginEdit();doc.data.worldExtra=std::move(next);doc.touched();return true;
}
} // namespace aiverse::studio
