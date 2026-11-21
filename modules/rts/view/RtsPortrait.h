// Unit portraits: one type rendered by the game view into a texture (HUD portrait, production
// buttons, editor previews). Owns its renderer and render targets; call render() outside of an
// open render pass (before the frame's scene), then sample texture(). On Metal the texture's v
// grows downward (DeviceInfo::renderTargetFlipY): flip the UVs when drawing it.
#pragma once
#include <memory>
#include "RtsView.h"
#include "engine/view/View.h"
#include "engine/render/ModelResourceCache.h"
#include "engine/render/ModelAssetProvider.h"

namespace aiverse::game {

class Portrait {
public:
    bool init(gfx::Device& dev, int size = 256);
    void shutdown();
    // Renders `type` of `owner` at animation time `time` (idle clip, slowly turning when yaw < 0).
    void render(RtsView& view, uint16_t type, uint8_t owner, double time, float yaw = -1);
    // A material (library name or override object, docs/models.md) on a sphere (shape 0) or a box
    // (shape 1), slowly turning; textures resolve like in rules.json.
    void renderPose(const render::Model&, const render::Pose&, float yaw=0.5f);
    // Draw a source model provided by any native project, without RtsView,
    // gameplay types or an extra MotionWeave animation evaluation.
    bool renderStaticModel(render::ModelAssetProvider& provider,
                           const std::string& source,float yaw=0.5f);
    // Generic fully-authored offscreen ModelDraw path. The host owns model,
    // material override vectors, node assignments, variant and transform.
    // No RtsView, gameplay ECS, second animation evaluation or file import.
    bool renderAppearance(const render::ModelDraw& draw,
                          const render::RTSCamera& camera,
                          render::float4 background={0.16f,0.18f,0.22f,1.f});
    // Borrowed resource-owner view for cross-host GPU accounting.
    const render::ModelResourceCache& modelResources() const noexcept {
        return previewModels_;
    }
    void renderMaterial(RtsView& view, const Json& material, int shape, double time);
    gfx::TextureHandle texture() const { return color_; }
    int size() const { return size_; }
    const std::string& lastError() const { return r3d_.modelError(); }

private:
    gfx::Device* dev_ = nullptr;
    render::Renderer3D r3d_;
    // Portraits use the SAME engine-level View frame and model paths
    // as the main viewport; the off-screen render targets remain owned here.
    ::aiverse::View sceneView_;
    gfx::TextureHandle color_, depth_;
    // Fully portrait-owned generated material preview models; no borrowing
    // and no model-map ownership through the legacy RTS View.
    render::ModelResourceCache previewModels_;
    std::vector<render::Material> mats_;
    int size_ = 0;
};

} // namespace aiverse::game
