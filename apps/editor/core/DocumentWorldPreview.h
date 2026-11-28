#pragma once
#include "Workspace.h"
#include "WorldInspect.h"
#include "SimTools.h"

namespace aiverse::editor {
// The preview worker owns copies, including unsaved map and rules edits.
inline void startDocumentWorldPreview(WorldPreview& preview, const MapDoc& doc,
                                      const Project& project, const WorldConfig& config) {
    auto data = std::make_shared<MapData>(doc.data);
    preview.start([data, path=doc.path, dir=project.dir, rules=project.rulesJson](Json& simRules, Json& simMap) {
        MapDoc map;
        map.data=std::move(*data);
        map.path=path;
        simMap=mapForSim(map.toJson(),dir);
        Project source;
        source.dir=dir;
        source.rulesJson=rules;
        simRules=source.simRules(simMap);
    },doc.data,config);
}
}
