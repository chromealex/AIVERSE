// Spine rendering through the RHI (gfx::Device): draws a Skeleton's DrawList with premultiplied
// alpha, one pipeline per blend family, GLSL + MSL (SpineShaders.h). Presentation only.
//
// Blend modes (premultiplied, as the Spine runtimes): normal = ONE, 1-srcA; additive = the same
// pipeline with alpha written as 0; multiply = dst * (src + 1 - srcA) through BlendMode::Multiply;
// screen = ONE, 1-srcColor through BlendMode::Screen (exactly the Spine runtimes' screen blend).
//
// SpinePlayer bundles everything one animated skeleton needs (data, pose, state, page textures).
// Content (skeleton JSON + atlas + page images) is read through platform::readContentFile, so packs,
// Android assets and the web preload work.
#pragma once
#include <memory>
#include <string>
#include <vector>
#include "../gfx/Gfx.h"
#include "SpineSkeleton.h"

namespace aiverse::render { class TextureCache; }

namespace aiverse::spine {

class SpineRenderer {
public:
    bool init(gfx::Device& dev);
    void shutdown();
    // Inside an open pass. `mvp` (column-major) maps skeleton space (x right, y up) to clip space.
    // pages[i]: texture of atlas page i; pma[i]: the page is premultiplied (atlas "pma: true").
    void draw(gfx::Device& dev, const DrawList& list, const std::vector<gfx::TextureHandle>& pages,
              const std::vector<uint8_t>& pma, const float mvp[16], bool depthTest = false, float alpha = 1);
    uint32_t drawCalls() const { return drawCalls_; }

private:
    gfx::Device* dev_ = nullptr;
    gfx::ShaderHandle shader_;
    gfx::PipelineHandle pipes_[2][3];   // [depthTest][premultiplied / multiply / screen]
    gfx::BufferHandle vb_, ib_;
    size_t vbSize_ = 0, ibSize_ = 0;
    uint32_t drawCalls_ = 0;
};

struct SpineAsset {
    std::shared_ptr<SkeletonData> data;
    std::vector<std::string> pagePaths;    // content paths of the page images
};

// Loads `skeleton` (.json, content path) with `atlas` (default: the same name with .atlas, then
// .atlas.txt). Throws std::runtime_error with a readable message.
SpineAsset loadSpineAsset(const std::string& skeleton, const std::string& atlas = "", float scale = 1);

class SpinePlayer {
public:
    explicit SpinePlayer(const SpineAsset& asset);
    Skeleton skeleton;
    AnimationState state;
    DrawList list;
    // update(dt) + apply + world transform + draw list
    void update(float dt);
    // Resolves the page textures once (TextureCache keeps them), then draws.
    void draw(gfx::Device& dev, SpineRenderer& r, render::TextureCache& textures, const float mvp[16], bool depthTest = false, float alpha = 1);

private:
    std::vector<std::string> pagePaths_;
    std::vector<gfx::TextureHandle> pages_;
    std::vector<uint8_t> pma_;
};

// Column-major orthographic mvp that puts skeleton-space `bounds` (minX, minY, maxX, maxY; with
// `pad` pixels) into a width x height viewport, keeping the aspect (UI portraits, previews).
// flipY: draw upside down (GL render targets shown by the UI).
void fitMatrix(const float bounds[4], int width, int height, float pad, bool flipY, float out[16]);
// Bounds to fit: the posed attachments now (exported header bounds are often stale).
void poseBounds(const Skeleton& sk, float out[4]);

} // namespace aiverse::spine
