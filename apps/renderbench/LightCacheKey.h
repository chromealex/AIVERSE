#pragma once
#include "engine/core/Hash.h"
#include "engine/data/Json.h"
namespace aiverse::bench {
// Recipe hash of the baked light cache (.avgic): everything that changes baked light, nothing that
// is presentation only (cameras, post, look, volumetric haze, mesh AO, roughness overrides,
// models marked "bake": false).
inline uint64_t lightCacheKey(const Json& config) {
    Json keyConfig=config;keyConfig.erase("cameras");keyConfig.erase("postfx");keyConfig.erase("id");keyConfig.erase("sunStudy");keyConfig.erase("meshOcclusion");keyConfig.erase("volumetricLight");keyConfig.erase("look");
    // Per-model mesh AO and roughness overrides do not change the baked light (metallic does: albedo).
    Json keyModels=Json::array();bool dropped=false;
    for(auto entry:keyConfig["models"].items()){if(!entry.getBool("bake",true)){dropped=true;continue;}   // draw-only models
        entry.erase("meshOcclusion");
        if(entry.has("materials")){Json materials=Json::object();
            for(auto [name,value]:entry["materials"].members()){for(const char* key:{"roughness"})value.erase(key);if(!value.members().empty())materials.set(name,value);}
            if(materials.members().empty())entry.erase("materials");else entry.set("materials",materials);}
        keyModels.push(entry);}
    if(dropped&&keyModels.size()==0)keyConfig.erase("models");else if(keyConfig.has("models"))keyConfig.set("models",keyModels);
    Json keyGi=keyConfig["probeGI"];for(const char* key:{"quality","points","sun","sky"})keyGi.erase(key);keyConfig.set("probeGI",keyGi);
    Hasher keyHash;keyHash.str(keyConfig.dump());return keyHash.value();
}
}
