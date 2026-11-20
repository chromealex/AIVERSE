// Dynamic indirect GI admission for ModelDraw batching, independent of game
// genres and model types. CPU-authored indexed/MDI material palettes retain
// per-instance PBR factors in U3; indirect GI uses U6+/tex13+, so the two
// resource sets do not overlap. GPU-authored ICB instance-buffer paths are
// intentionally excluded until their separate texture binding ABI is tested.
#pragma once

namespace aiverse::render {

// True for ordinary ModelDraw and CPU-built MDI material palette batches.
// Does not by itself guarantee GI: callers must still check quality, live
// probe atlas, shader graph texture slots, phase and material shading model.
constexpr bool giModelBatchMaySample(bool gpuAuthoredInstances,
                                     bool perInstancePbrPalette) {
    (void)perInstancePbrPalette; // U3 is independent of GI U6+ resources.
    return !gpuAuthoredInstances;
}

} // namespace aiverse::render
