// Stable source KIND identity for authoring document objects; ordinals are
// still provisional until the editor's documents gain stable IDs.
#pragma once
#include "apps/editor/EditorEntityAdapter.h"
#include <string>

namespace aiverse::editor {
inline std::string documentEntityId(DocumentEntityRef ref) {
    switch (ref.source) {
        case DocumentEntityRef::Source::MapLight:
        case DocumentEntityRef::Source::FpsLight:
            return "light:legacy:"+std::to_string(ref.index);
        case DocumentEntityRef::Source::MapEntity:
            return "entity:" + std::to_string(ref.index);
        case DocumentEntityRef::Source::FpsCollider:
            return "collider:" + std::to_string(ref.index);
        case DocumentEntityRef::Source::FpsSpawn:
            return "spawn:" + std::to_string(ref.index);
    }
    return {};
}
inline std::string documentEntityId(const MapDoc& map,DocumentEntityRef ref) {
    if(const auto* light=documentLight(map,ref)) {
        const auto id=light->getStr("entityId");if(!id.empty())return "light:"+id;
    }
    if(const auto* entity=documentMapEntity(map,ref);entity && authoredEntity(*entity) && !entity->id.empty())return "entity:@"+entity->id;
    return documentEntityId(ref);
}
} // namespace aiverse::editor
