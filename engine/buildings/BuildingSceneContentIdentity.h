#pragma once
#include "engine/buildings/BuildingApertureOwner.h"
#include "engine/core/Hash.h"
#include <map>
#include <string>
#include <vector>
namespace aiverse::building {
// Identical authored geometry/instance contract on authority and replica.
inline uint64_t buildingSceneContentIdentity(uint64_t sceneId,const std::string& instanceKey,
    const std::string& styleId,Vec3 origin,uint32_t yaw,uint32_t domains,uint16_t stepPermille,
    const RuntimeBuildingResult& generated,const std::vector<BuildingAperturePose>& poses,
    const std::map<uint32_t,uint32_t>& liftLayout){
    Hasher hash;hash.str("aiverse.building.scene.v1");
    hash.u64(sceneId);hash.str(instanceKey);
    hash.str(styleId);hash.v3(origin);hash.u32(yaw);
    hash.u32(domains);hash.u32(stepPermille);
    for(const auto& box:generated.building.boxes){hash.v3(box.min);hash.v3(box.max);}
    for(const auto& pose:poses){
        hash.str(pose.socketId);hash.u32(pose.featureIndex);
        hash.u32(uint32_t(pose.kind));hash.u32(pose.physicalPermille);
        hash.v3(pose.bounds.min);hash.v3(pose.bounds.max);
    }
    for(const auto& room:generated.structureGraph.rooms){
        hash.str(room.id);hash.u32(room.floor);
    }
    for(const auto& edge:generated.structureGraph.connections){
        hash.str(edge.id);hash.str(edge.from);hash.str(edge.to);hash.u32(uint32_t(edge.kind));
    }
    for(const auto& [feature,floors]:liftLayout){
        hash.u32(feature);hash.u32(floors);
    }
    return hash.value();
}
}
