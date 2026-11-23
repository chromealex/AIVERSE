#include "UiModelView.h"
#include "UiLayer.h"
#include "engine/render/Animation.h"

namespace aiverse::ui {

bool UiModelView::init(gfx::Device& device, UiLayer& ui, const std::string& name,
                       int width, int height) {
    if (device_ || name.empty() || name.size()>64 || width<16 || height<16 ||
        width>2048 || height>2048) return false;
    device_=&device; ui_=&ui; name_=name; width_=width; height_=height;
    if (!renderer_.init(device)) { shutdown(); return false; }
    renderer_.setShadowQuality(render::ShadowQuality::Low);
    gfx::TextureDesc desc;
    desc.width=width; desc.height=height; desc.renderTarget=true;
    desc.label="ui-model-color";
    color_=device.createTexture(desc);
    desc.format=gfx::PixelFormat::Depth;
    desc.filter=gfx::Filter::Nearest;
    desc.label="ui-model-depth";
    depth_=device.createTexture(desc);
    if (!color_.valid() || !depth_.valid()) { shutdown(); return false; }
    ui.setDynamicTexture("model:"+name_,color_,width_,height_);
    return true;
}

bool UiModelView::renderDraw(const render::ModelDraw& draw,
                             const render::Camera& camera,
                             render::float4 background) {
    if (!device_ || !color_.valid() || !depth_.valid() || !draw.model) return false;
    renderer_.beginScene(camera,width_,height_);
    renderer_.drawModel(draw);
    gfx::PassDesc pass;
    pass.colorTarget=color_; pass.depthTarget=depth_;
    pass.clearColor[0]=background.x; pass.clearColor[1]=background.y;
    pass.clearColor[2]=background.z; pass.clearColor[3]=background.w;
    renderer_.render(pass);
    device_->endPass();
    return true;
}

bool UiModelView::renderPose(const render::Model& model,
                             const render::Pose& pose,
                             const render::Camera& camera,
                             render::float4 background) {
    if (pose.size()!=model.nodes.size()) return false;
    std::vector<render::mat4> nodes;
    render::worldMatrices(model,pose,nodes);
    render::ModelDraw draw;
    draw.model=&model; draw.nodeWorld=&nodes; draw.pose=&pose;
    return renderDraw(draw,camera,background);
}

bool UiModelView::renderAnimator(const render::Model& model,
                                 const render::Animator& animator,
                                 const render::Camera& camera,
                                 render::float4 background) {
    render::Pose pose;
    animator.evaluate(pose);
    return renderPose(model,pose,camera,background);
}

void UiModelView::shutdown() {
    if (!device_) return;
    if (ui_ && !name_.empty()) ui_->setDynamicTexture("model:"+name_,{},0,0);
    renderer_.shutdown();
    if (color_.valid()) device_->destroyTexture(color_);
    if (depth_.valid()) device_->destroyTexture(depth_);
    color_=depth_={}; device_=nullptr; ui_=nullptr;
    name_.clear(); width_=height_=0;
}
} // namespace aiverse::ui
