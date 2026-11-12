// Baked lighting on the GPU (agent-lightbake): the lightmap pages, shadow masks and light probes of
// one map (engine/bake/LightmapData.h) uploaded for Renderer3D::setBakedLighting.
//
// Static models find their lightmap region by model path + node + world matrix (the key the baker
// stored), so a game view needs no changes: whatever it draws at the baked placement is lightmapped,
// everything else (units, moved or new objects) is lit by the probes. The terrain lightmap is bound by
// the terrain renderer through Renderer3D::bindBakedTerrain.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../bake/LightmapData.h"
#include "../gfx/Gfx.h"
#include "Math3D.h"

namespace aiverse::render {

class BakedLighting {
public:
    ~BakedLighting();
    // Uploads `data` (CPU pixels are dropped afterwards). Replaces what was loaded before.
    bool upload(gfx::Device& dev, bake::LightmapData data);
    void release();
    bool empty() const { return pages_.empty() && !probeTex_.valid(); }

    const bake::LightmapData& data() const { return data_; }
    // Region of a static model node, null when that placement was not baked.
    const bake::LightmapRegion* find(const std::string& modelPath, int node, const mat4& world) const;
    struct Page { gfx::TextureHandle lightmap, mask; };
    const std::vector<Page>& pages() const { return pages_; }
    gfx::TextureHandle probeTexture() const { return probeTex_; }
    int probeTextureWidth() const { return probeWidth_; }
    // Shader GI block (BakedShaders.h): 16 floats.
    void giBlock(float out[16]) const;
    // Terrain block (3 x vec4) for bindBakedTerrain; false without a terrain lightmap.
    bool terrainBlock(float out[12]) const;
    int modeIndex() const { return int(data_.mode) + 1; }   // 1 indirect, 2 shadowmask, 3 subtractive

private:
    gfx::Device* dev_ = nullptr;
    bake::LightmapData data_;   // header, regions, probes (pixels released after upload)
    std::vector<Page> pages_;
    gfx::TextureHandle probeTex_;
    int probeWidth_ = 0;
};

} // namespace aiverse::render
