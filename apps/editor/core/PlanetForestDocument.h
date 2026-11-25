// Shared editor/MCP map-owned forest documents. Native recipe validation is shared;
// never mutate live ECS, rules files, or an independent undo stack.
#pragma once
#include "apps/editor/core/MapDoc.h"
#include "aiverselib/world/WorldProcessesPlanetForestPipeline.h"
#include "engine/world/WorldAssets.h"
#include "engine/world/TerraWeaveForestSpeciesProfile.h"
#include <algorithm>
#include <set>
namespace aiverse::editor {
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
    Json presets=Json::array();for(const auto& preset:terraweave::botanicalTreePresets())presets.push(Json::string(std::string(preset.name)));
    Json bodies=Json::array();for(const auto& b:doc.data.bodies)if(b.surface())bodies.push(Json::string(b.id));
    out.set("bodies",std::move(bodies));out.set("presets",std::move(presets));out.set("markers",std::move(markers));return out;
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
        if(item.has("at")){
            const auto& at=item["at"];const int bi=doc.data.findBody(at.getStr("body"));
            const auto face=at["face"].integer(-1);
            if(item.has("entity")||!at.isObject()||bi<0||!doc.data.bodies[size_t(bi)].surface()||
               !at["face"].isNumber()||face<0||face>5||at["face"].fx()!=Fixed::fromInt(face)||
               !at["x"].isNumber()||!at["y"].isNumber())throw std::invalid_argument("Invalid forest surface placement");
            const auto n=Fixed::fromInt(doc.data.bodies[size_t(bi)].faceSize);
            if(at["x"].fx()<Fixed{}||at["y"].fx()<Fixed{}||at["x"].fx()>n||at["y"].fx()>n)
                throw std::invalid_argument("Forest surface coordinate out of range");
            return;
        }
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
// One click = one complete validated MapDoc transaction. No synthetic entity.
inline void placeStudioPlanetForest(editor::MapDoc& doc,const Json& rules,const Json& params){
    if(!params["mapPath"].isString()||!params["expectedRevision"].isString()||params.getStr("mapPath")!=doc.path||params.getStr("expectedRevision")!=std::to_string(doc.revision))
        throw std::invalid_argument("Stale forest planting request");
    const auto state=studioPlanetForestDocument(doc,rules);
    auto forest=state["forest"];
    if(!forest.isObject())throw std::invalid_argument("Configure forest species first");
    const auto species=lib::worldprocess::planetBankInteger(params,"species",0,1,1000000);
    if(!species)throw std::invalid_argument("Choose species");
    const auto habit=params.getStr("habit","tree");
    if(habit!="tree"&&habit!="shrub")throw std::invalid_argument("Invalid habit");
    std::set<uint64_t> used;
    for(const auto& row:forest["plants"].items())used.insert(uint64_t(row.getInt("key")));
    uint64_t key=1;while(used.contains(key))++key;
    Json plant=Json::object();plant.set("species",Json::number(species));plant.set("key",Json::number(int64_t(key)));
    plant.set("habit",Json::string(habit));
    if(params.has("at")==params.has("entity"))throw std::invalid_argument("Give exactly one of at or entity");
    if(params.has("at"))plant.set("at",params["at"]);else plant.set("entity",params["entity"]);
    if(params.has("ageTicks"))plant.set("ageTicks",params["ageTicks"]);
    auto plants=forest["plants"];if(!plants.isArray())throw std::invalid_argument("Invalid plants array");
    plants.push(std::move(plant));forest.set("plants",std::move(plants));
    Json patch=params;patch.set("forest",forest);patch.set("catalog",state["catalog"]);
    editStudioPlanetForest(doc,rules,patch);
}
// Stable identity, not row index. Moving a shared marker is deliberately forbidden.
inline bool moveStudioPlanetForest(editor::MapDoc& doc,const Json& rules,const Json& params){
    if(!params["mapPath"].isString()||!params["expectedRevision"].isString()||
       params.getStr("mapPath")!=doc.path||params.getStr("expectedRevision")!=std::to_string(doc.revision))
        throw std::invalid_argument("Stale forest relocation request");
    const auto state=studioPlanetForestDocument(doc,rules);
    auto forest=state["forest"];
    const auto species=lib::worldprocess::planetBankInteger(params,"species",0,1,1000000);
    const auto key=lib::worldprocess::planetBankInteger(params,"key",0,1,1000000000);
    if(!species||!key||!forest.isObject()||!forest["plants"].isArray())
        throw std::invalid_argument("Invalid forest relocation identity");
    auto plants=forest["plants"];
    Json* target=nullptr;
    for(auto& row:plants.itemsMut()){
        if(row.getInt("species")!=species||row.getInt("key")!=key)continue;
        if(target)throw std::invalid_argument("Ambiguous forest identity");
        target=&row;
    }
    if(!target)throw std::invalid_argument("Plant no longer exists");
    if(target->has("entity")||!target->has("at"))
        throw std::invalid_argument("Convert marker-bound plant to direct coordinates first");
    if(!params["at"].isObject())throw std::invalid_argument("Missing destination surface");
    if((*target)["at"]==params["at"])return false;
    target->set("at",params["at"]);forest.set("plants",std::move(plants));
    Json patch=params;patch.set("forest",forest);patch.set("catalog",state["catalog"]);
    return editStudioPlanetForest(doc,rules,patch);
}
inline bool removeStudioPlanetForest(MapDoc& doc,const Json& rules,const Json& params){
    if(!params["mapPath"].isString()||!params["expectedRevision"].isString()||
       params.getStr("mapPath")!=doc.path||params.getStr("expectedRevision")!=std::to_string(doc.revision))
        throw std::invalid_argument("Stale forest removal request");
    const auto state=studioPlanetForestDocument(doc,rules);auto forest=state["forest"];
    const auto species=lib::worldprocess::planetBankInteger(params,"species",0,1,1000000);
    const auto key=lib::worldprocess::planetBankInteger(params,"key",0,1,1000000000);
    if(!species||!key||!forest.isObject()||!forest["plants"].isArray())
        throw std::invalid_argument("Invalid forest removal identity");
    Json plants=Json::array();size_t matches=0;
    for(const auto& row:forest["plants"].items()){
        if(row.getInt("species")==species&&row.getInt("key")==key){++matches;continue;}
        plants.push(row);
    }
    if(matches!=1)throw std::invalid_argument("Missing or ambiguous forest plant");
    forest.set("plants",std::move(plants));
    Json patch=params;patch.set("forest",forest);patch.set("catalog",state["catalog"]);
    return editStudioPlanetForest(doc,rules,patch);
}
} // namespace aiverse::editor
