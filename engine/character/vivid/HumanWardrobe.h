#pragma once
#include "HumanAppearance.h"
#include "HumanFaceShape.h"
namespace aiverse::vivid {
// Content-relative asset keys are resolved exclusively by the host FileReader.
// Recipe v1: hiddenMeshes:string[], items:[{id,asset,slot,enabled,bone,translation,
// rotation,scale,hideMeshes:string[],masks:[{material,texture}],tint?:[r,g,b]}] (tint multiplies the item colour).
// Optional sockets:[{name,bone,translation,rotation,scale}]; items may select
// socket instead of bone and use independentRig + attachmentRule keep_world/snap.
// Masks: red=visibility (black hides), UV0, multiplied with original alpha.
void bakeHumanBody(render::Model& model,const HumanAppearance& look);
void validateHumanWardrobe(const Json& recipe);
Json inspectHumanWardrobe(const render::Model& model);
std::unique_ptr<render::Model> assembleHumanCharacter(const std::string& base,
    const render::FileReader& read,const Json& wardrobe,const HumanAppearance& look,
    int textureSize=1024,const HumanFaceShape* face=nullptr);
}
