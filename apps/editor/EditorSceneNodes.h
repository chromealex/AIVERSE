// Genre-independent authoring scene node enumeration.
// These are document sources (which may not have runtime ECS counterparts).
// Never fabricate ECS components from this representation.
#pragma once
#include "EditorEntityAdapter.h"
#include <string>
#include <utility>
#include <vector>

namespace aiverse::editor {

struct EditorSceneNode {
    DocumentEntityRef ref;
    std::string group;
    std::string label;
    DocumentTransform position{};
    bool hasPosition = false;
};

inline std::vector<EditorSceneNode> enumerateDocumentSceneNodes(
    const MapDoc& map, bool fpsProject) {
    std::vector<EditorSceneNode> nodes;
    auto add = [&](DocumentEntityRef ref, const char* group, std::string label) {
        EditorSceneNode node;
        node.ref = ref;
        node.group = group;
        node.label = std::move(label);
        node.hasPosition = readDocumentTransform(map, ref, node.position);
        nodes.push_back(std::move(node));
    };
    const auto& lights=(fpsProject?map.data.fps:map.data.extra)["lights"];
    if(lights.isArray())for(size_t i=0;i<lights.size();++i) {
        if(!lights[i].isObject())continue;
        const auto name=lights[i].getStr("name");
        add(DocumentEntityRef::light(i,fpsProject),"Lights",name.empty()?"Light #"+std::to_string(i+1):name);
    }
    if (fpsProject) {
        const Json& fps = map.data.fps;
        const Json& colliders = fps["colliders"];
        if (colliders.isArray())
            for (size_t i = 0; i < colliders.size(); ++i)
                add(DocumentEntityRef::fpsCollider(i), "Colliders",
                    "Box Collider #" + std::to_string(i + 1));
        const Json& spawns = fps["spawns"];
        if (spawns.isArray())
            for (size_t i = 0; i < spawns.size(); ++i)
                add(DocumentEntityRef::fpsSpawn(i), "Spawn Points",
                    "Spawn #" + std::to_string(i + 1));
        return nodes;
    }
    nodes.reserve(map.data.entities.size());
    for (size_t i = 0; i < map.data.entities.size(); ++i) {
        const DocEntity& entity = map.data.entities[i];
        const std::string display = entity.extra.getStr("displayName", "");
        add(DocumentEntityRef::mapEntity(i), "Map Entities",
            display.empty() ? (entity.id.empty() ? entity.type : entity.type + " #" + entity.id)
                            : display);
    }
    return nodes;
}

} // namespace aiverse::editor
