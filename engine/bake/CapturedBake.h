#pragma once
#include "BakeScene.h"

namespace aiverse::bake {
// Everything a bake needs, copied on the main thread (the job never touches the editor).
struct CapturedBake {
    struct Draw {
        std::string path;
        std::vector<mat4> nodeWorld;
        std::vector<render::Material> materials;
        std::vector<uint8_t> still;
    };
    std::vector<Draw> draws;
    std::vector<float> heights;   // terrain samples, (nx x nz) at `step`
    int nx = 0, nz = 0;
    float step = 0.5f, ex = 0, ez = 0;
    BakeScene::Sun sun;
    BakeScene::Sky sky;
    std::vector<BakeLight> lights;
    BakeSettings settings;
    std::string folder;   // folder relative to the project
    uint64_t oldHash = 0;
    bool force = false;
};

// Drop procedural draws and mask animated nodes using the preceding frame.
void markStaticNodes(CapturedBake& input, std::vector<CapturedBake::Draw>& previous);

struct CapturedBakeResult {
    BakeStats stats;
    uint64_t hash = 0;
    bool unchanged = false, generatedUvs = false;
    std::string error, warnings;
};

CapturedBakeResult runCapturedBake(const CapturedBake& input, const ReadFn& read,
                                   const WriteFn& write, const ProgressFn& progress);
} // namespace aiverse::bake
