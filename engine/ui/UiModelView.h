#pragma once

#include <string>
#include "engine/gfx/Gfx.h"
#include "engine/render/Animation.h"
#include "engine/render/Renderer3D.h"

namespace aiverse::ui {
class UiLayer;

// One 3D image in RmlUi. Render before the main scene pass, then use
// <img src="#model:NAME"/>. The caller owns scene entities, models and poses.
class UiModelView {
public:
    bool init(gfx::Device& device, UiLayer& ui, const std::string& name,
              int width=256, int height=256);
    void shutdown();
    // A scene entity can supply its already prepared ModelDraw. A separate
    // presentation animation source can supply its current pose each frame.
    bool renderDraw(const render::ModelDraw& draw, const render::Camera& camera,
                    render::float4 background={0.12f,0.14f,0.18f,1.f});
    bool renderPose(const render::Model& model, const render::Pose& pose,
                    const render::Camera& camera,
                    render::float4 background={0.12f,0.14f,0.18f,1.f});
    // The caller advances its dedicated Animator once per frame; this only
    // reads the resulting pose, so the UI does not become a second pose owner.
    bool renderAnimator(const render::Model& model, const render::Animator& animator,
                        const render::Camera& camera,
                        render::float4 background={0.12f,0.14f,0.18f,1.f});
    gfx::TextureHandle texture() const noexcept { return color_; }
    const std::string& name() const noexcept { return name_; }
private:
    gfx::Device* device_=nullptr;
    UiLayer* ui_=nullptr;
    std::string name_;
    int width_=0, height_=0;
    render::Renderer3D renderer_;
    gfx::TextureHandle color_, depth_;
};
} // namespace aiverse::ui
