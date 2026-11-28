#include "CapturedBake.h"
#include "SceneBuilder.h"
#include <algorithm>
#include <cstring>
#include <set>

namespace aiverse::bake {
void markStaticNodes(CapturedBake& input, std::vector<CapturedBake::Draw>& previous) {
    // procedural shapes ("shape:...") have no file to load: they are not part of the bake
    std::erase_if(input.draws, [](const CapturedBake::Draw& d) { return d.path.rfind("shape:", 0) == 0; });
    std::erase_if(previous, [](const CapturedBake::Draw& d) { return d.path.rfind("shape:", 0) == 0; });
    const bool compare = previous.size() == input.draws.size();
    for (size_t i = 0; i < input.draws.size(); ++i) {
        auto& d = input.draws[i];
        d.still.assign(d.nodeWorld.size(), 1);
        if (!compare || previous[i].path != d.path || previous[i].nodeWorld.size() != d.nodeWorld.size()) continue;
        for (size_t n = 0; n < d.nodeWorld.size(); ++n)
            d.still[n] = std::memcmp(d.nodeWorld[n].m, previous[i].nodeWorld[n].m, sizeof d.nodeWorld[n].m) == 0;
    }
    previous.clear();
}

CapturedBakeResult runCapturedBake(const CapturedBake& in, const ReadFn& read,
                                   const WriteFn& write, const ProgressFn& progress) {
    CapturedBakeResult result;
    if ((in.nx || in.nz) && (in.nx < 2 || in.nz < 2 || !(in.step > 0) ||
        size_t(in.nx) > in.heights.size() / size_t(in.nz) ||
        size_t(in.nx) * size_t(in.nz) != in.heights.size())) {
        result.error = "Invalid captured terrain grid";
        return result;
    }
    BakeScene scene;
    SceneBuilder sb(scene, read, write, in.settings.texelsPerUnit);
    std::string err;
    std::set<std::string> failed;
    for (const auto& d : in.draws)
        if (!failed.count(d.path) && !sb.addModel(d.path, d.nodeWorld, d.materials.empty() ? nullptr : &d.materials, true, 1, err, &d.still)) {
            result.warnings += err + "\n";   // once per model
            failed.insert(d.path);
        }
    const int nx = in.nx, nz = in.nz;
    auto height = [&in, nx, nz](float x, float z) {
        const float fx = std::clamp(x / in.step, 0.0f, float(nx - 1)), fz = std::clamp(z / in.step, 0.0f, float(nz - 1));
        const int x0 = std::min(int(fx), nx - 2), z0 = std::min(int(fz), nz - 2);
        const float tx = fx - float(x0), tz = fz - float(z0);
        auto h = [&](int a, int b) { return in.heights[size_t(b) * size_t(nx) + size_t(a)]; };
        return (h(x0, z0) * (1 - tx) + h(x0 + 1, z0) * tx) * (1 - tz) + (h(x0, z0 + 1) * (1 - tx) + h(x0 + 1, z0 + 1) * tx) * tz;
    };
    if (nx >= 2 && nz >= 2) sb.addTerrain(height, 0, 0, in.ex, in.ez, in.step, {0.30f, 0.28f, 0.24f});
    scene.sun = in.sun; scene.sky = in.sky; scene.lights = in.lights;
    result.generatedUvs = sb.unwrapped != 0;
    for (const auto& w : sb.warnings) result.warnings += w + "\n";
    result.hash = hashBakeInput(scene, in.settings);
    if (!in.force && result.hash == in.oldHash) {
        result.unchanged = true;
        return result;
    }
    LightmapData data;
    if (bakeLighting(scene, in.settings, data, result.error, progress, &result.stats))
        saveLightmapData(in.folder, data, write, result.error);
    return result;
}
} // namespace aiverse::bake
