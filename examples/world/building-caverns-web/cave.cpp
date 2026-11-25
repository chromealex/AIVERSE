#include "engine/world/TerraWeaveCavernsVolumeSnapshot.h"
#include <emscripten/emscripten.h>
#include <vector>

using namespace aiverse::terraweave::caverns;
namespace {
std::vector<float> vertices;
}

extern "C" EMSCRIPTEN_KEEPALIVE int rebuild(int mode, int radius) {
    if (mode < 0 || mode > 2 || radius < 50 || radius > 200) return 0;
    CaveVolumeSnapshot volume;
    volume.settings.tunnelChance = 0;
    volume.settings.chamberChance = 0;
    volume.settings.minYcm = -790;
    volume.settings.maxYcm = -100;
    volume.stamps.push_back({1, {0,-500,0}, 220, CaveStampMode::Carve});
    volume.entrances.push_back({1,0,0,0,-400,125});
    if (mode != 0) {
        volume.stamps.push_back({2, {160,-500,0}, radius,
            mode == 1 ? CaveStampMode::Carve : CaveStampMode::Fill});
    }
    canonicalizeCaveSnapshot(volume);
    vertices.clear();
    for (int x : {-1,0}) for (int z : {-1,0}) {
        const auto mesh = generateCaveSnapshotMesh(volume, {x,-1,z}, 32, 25);
        for (const auto index : mesh.indices) {
            const auto& v = mesh.vertices[index];
            vertices.insert(vertices.end(), {v.x+x*8.f,v.y-8.f,v.z+z*8.f,v.nx,v.ny,v.nz});
        }
    }
    return static_cast<int>(vertices.size()/6);
}
extern "C" EMSCRIPTEN_KEEPALIVE const float* vertex_data() { return vertices.data(); }
