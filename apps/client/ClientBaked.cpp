// Baked lighting of the started map (agent-lightbake): "maps/<map>.bake" next to the map file
// (engine/bake/LightmapData.h), uploaded and handed to the renderer. No folder: realtime lighting only.
#include <cstdio>
#include "GameClient.h"
#include "engine/platform/Platform.h"

namespace aiverse::client {

void GameClient::configureBakedLighting(const std::string& mapFile) {
    r3d_.setBakedLighting(nullptr);
    baked_.release();
    const std::string folder = bake::bakeFolderFor(mapFile);
    bake::LightmapData data;
    std::string err;
    auto read = [](const std::string& p, std::string& out) { return platform::readContentFile(p, out); };
    std::string probe;
    if (!read(folder + "/lightmap.json", probe)) return;   // not baked
    if (!bake::loadLightmapData(folder, read, data, err)) { std::fprintf(stderr, "baked lighting: %s\n", err.c_str()); return; }
    if (baked_.upload(*dev_, std::move(data))) r3d_.setBakedLighting(&baked_);
}

} // namespace aiverse::client
