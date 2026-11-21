#include "RtsPortrait.h"

#include <algorithm>
#include <cmath>

namespace aiverse::game {

using namespace render;

bool Portrait::init(gfx::Device& dev, int size) {
    previewModels_.bindResourceDevice(dev);
    dev_ = &dev;
    size_ = size;
    if (!r3d_.init(dev)) return false;
    r3d_.setShadowQuality(ShadowQuality::Low);
    gfx::TextureDesc td;
    td.width = td.height = size;
    td.renderTarget = true;
    td.label = "portrait";
    color_ = dev.createTexture(td);
    td.format = gfx::PixelFormat::Depth;
    td.filter = gfx::Filter::Nearest;
    depth_ = dev.createTexture(td);
    return color_.valid() && depth_.valid();
}

namespace {
// a unit sphere / box with UVs that show a texture the way it tiles (u around, v top to bottom)
std::unique_ptr<Model> previewShape(int shape) {
    auto m = std::make_unique<Model>();
    m->path = shape == 0 ? "preview:sphere" : "preview:box";
    auto add = [&](float3 p, float3 n, float2 uv) { ModelVertex v; v.pos = p; v.nrm = n; v.uv = uv; m->vertices.push_back(v); };
    if (shape == 0) {
        const int rings = 32, segs = 64;
        for (int r = 0; r <= rings; ++r)
            for (int sg = 0; sg <= segs; ++sg) {
                const float th = float(r) / float(rings) * 3.14159265f, ph = float(sg) / float(segs) * 6.2831853f;
                const float3 n{std::sin(th) * std::cos(ph), std::cos(th), -std::sin(th) * std::sin(ph)};
                add(n * 0.5f + float3{0, 0.5f, 0}, n, {float(sg) / float(segs) * 2, float(r) / float(rings)});
            }
        for (int r = 0; r < rings; ++r)
            for (int sg = 0; sg < segs; ++sg) {
                const uint32_t a = uint32_t(r * (segs + 1) + sg), b = a + uint32_t(segs + 1);
                for (uint32_t i : {a, b, a + 1, a + 1, b, b + 1}) m->indices.push_back(i);
            }
    } else {
        const float3 dirs[6] = {{1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}};
        for (const float3& n : dirs) {
            const float3 up = std::fabs(n.y) > 0.5f ? float3{0, 0, -1} : float3{0, 1, 0};
            const float3 right = cross(up, n);
            const uint32_t base = uint32_t(m->vertices.size());
            for (int k = 0; k < 4; ++k) {
                const float su = (k == 1 || k == 2) ? 1.0f : -1.0f, sv = k >= 2 ? 1.0f : -1.0f;
                add((n + right * su + up * sv) * 0.4f + float3{0, 0.4f, 0}, n, {su * 0.5f + 0.5f, 0.5f - sv * 0.5f});
            }
            for (uint32_t i : {0u, 2u, 1u, 0u, 3u, 2u}) m->indices.push_back(base + i);
        }
    }
    generateTangents(m->vertices, m->indices, 0, uint32_t(m->indices.size()));
    Primitive p;
    p.indexCount = uint32_t(m->indices.size());
    p.vertexCount = uint32_t(m->vertices.size());
    MeshDef md;
    md.name = "preview";
    md.primitives.push_back(p);
    m->meshes.push_back(md);
    Node n;
    n.name = "preview";
    n.mesh = 0;
    m->nodes.push_back(n);
    Material mt;
    mt.name = "preview";
    m->materials.push_back(mt);
    m->finalize();
    return m;
}
} // namespace

void Portrait::renderMaterial(RtsView& view, const Json& material, int shape, double time) {
    if (!dev_ || !color_.valid()) return;
    shape = shape == 1 ? 1 : 0;
    const std::string key = shape ? "shape:preview-box" : "shape:preview-sphere";
    Model* resident = previewModels_.find(key);
    if(!resident) {
        auto generated=previewShape(shape);
        if(!uploadModel(*dev_,*generated,true)) {
            // Failed publication has no consumers; release any partially
            // created GPU allocations before dropping this temporary Model.
            releaseModel(*dev_,*generated);
            return;
        }
        resident=previewModels_.insertIfAbsent(key,std::move(generated));
    }
    if(!resident)return;
    Model& m=*resident;
    static UnitType previewType;   // warnings read "type 'material preview': ..."
    previewType.id = "material preview";
    mats_ = m.materials;
    view.applyMaterial(*dev_, previewType, m, mats_[0], material);
    RTSCamera cam;
    cam.target = {0, 0.45f, 0};
    cam.distance = 2.1f;
    cam.minDistance = 0.1f;
    cam.pitch = 0.42f;
    cam.yaw = 0.6f;
    cam.fovY = 0.6f;
    ModelDraw draw;
    draw.model=&m;
    draw.world=mat4::rotateY(float(time)*0.4f);
    draw.materials=&mats_;
    draw.team={0.8f,0.1f,0.08f};
    // Material authoring remains with the old host, while the GPU
    // submission and offscreen target live in one generic View path.
    (void)renderAppearance(draw,cam);
}

// Canonical View portrait consumer for any native host's fully-authored,
// already-resolved ModelDraw (including materials, variant, pose and tint).
// All borrowed pointers are consumed synchronously before returning.
bool Portrait::renderAppearance(const ModelDraw& draw,
                                const RTSCamera& camera,float4 background) {
    if(!dev_ || !color_.valid() || !draw.model)return false;
    sceneView_.beginScene(r3d_,camera,size_,size_);
    ::aiverse::View::submitModel(r3d_,draw);
    gfx::PassDesc pass;
    pass.colorTarget=color_;
    pass.depthTarget=depth_;
    pass.clearColor[0]=background.x;
    pass.clearColor[1]=background.y;
    pass.clearColor[2]=background.z;
    pass.clearColor[3]=background.w;
    r3d_.render(pass);
    dev_->endPass();
    return true;
}

bool Portrait::renderStaticModel(render::ModelAssetProvider& provider,
                                const std::string& source,float yaw) {
    if(!dev_ || !color_.valid())return false;
    // The provider retains the Model; this portrait only borrows it for a
    // synchronous packet and does not create a second animation World.
    const Model* model=provider.load(*dev_,source);
    if(!model)return false;
    RTSCamera cam;
    cam.target=(model->boundsMin+model->boundsMax)*0.5f;
    const auto extent=model->boundsMax-model->boundsMin;
    cam.distance=std::max({extent.x,extent.y,extent.z,0.3f})*2.1f;
    cam.minDistance=0.01f;
    cam.pitch=0.15f;
    cam.yaw=yaw;
    cam.fovY=0.6f;
    ModelDraw draw;
    draw.model=model;
    draw.world=mat4::identity();
    return renderAppearance(draw,cam,{0.12f,0.14f,0.18f,1.f});
}

void Portrait::renderPose(const Model& model,const Pose& pose,float yaw) {
    if(!dev_||!color_.valid()||pose.size()!=model.nodes.size())return;
    std::vector<mat4> world;worldMatrices(model,pose,world);
    RTSCamera cam;cam.target=(model.boundsMin+model.boundsMax)*.5f;
    auto e=model.boundsMax-model.boundsMin;cam.distance=std::max({e.x,e.y,e.z,.3f})*2.1f;
    cam.minDistance=.01f;cam.pitch=.15f;cam.yaw=yaw;cam.fovY=.6f;
    r3d_.lighting.skyColor={.8f,.85f,.9f};r3d_.lighting.groundColor={.5f,.5f,.5f};
    ModelDraw draw;
    draw.model=&model;draw.nodeWorld=&world;
    draw.pose=&pose;draw.team={.1f,.4f,.9f};
    (void)renderAppearance(draw,cam,{.12f,.14f,.18f,1.f});
}

void Portrait::shutdown() {
    if (!dev_) return;
    // Generic per-host cache owns all preview mesh GPU handles, and its
    // shutdown waits for prior submitted commands before releasing them.
    previewModels_.shutdown(*dev_);
    r3d_.shutdown();
    if (color_.valid()) dev_->destroyTexture(color_);
    if (depth_.valid()) dev_->destroyTexture(depth_);
    color_ = depth_ = {};
    dev_ = nullptr;
}

void Portrait::render(RtsView& view, uint16_t type, uint8_t owner, double time, float yaw) {
    if (!dev_ || !color_.valid()) return;
    // frame the bind-pose bounds (look scale and fit are applied by the view; approximate with the model)
    float radius = 0.8f, height = 1.0f;
    if (const Model* m = view.modelOf(type)) {
        const float3 e = m->boundsMax - m->boundsMin;
        height = std::max(0.3f, std::min(e.y, 4.0f));
        radius = std::max({e.x, e.y, e.z, 0.3f}) * 0.6f;
    }
    const float s = view.selectionRadius(type);
    radius = std::max(radius, s);
    RTSCamera cam;
    cam.target = {0, std::min(height, radius * 1.4f) * 0.45f, 0};
    cam.distance = radius * 3.2f;
    cam.minDistance = 0.1f;
    cam.pitch = 0.32f;
    cam.yaw = 0.5f;
    cam.fovY = 0.6f;
    const float previewYaw=yaw<0 ? float(time)*.6f : yaw;
    // Preserve the full authored appearance (material overrides, variant,
    // geometry policy and tint) without routing this static packet through
    // any legacy animation evaluation or a second graph owner.
    view_adapter::StaticPreviewModel resolved;
    ::aiverse::view::Object packet;
    if(view.resolveStatic(type,owner,resolved) &&
       view_adapter::buildStaticPreviewPacket(resolved,previewYaw,packet)) {
        (void)renderAppearance(packet.draw,cam);
        return;
    }
    // Complex or animated models continue using the established host
    // authoring path; do not double-evaluate their MotionWeave graphs.
    sceneView_.beginScene(r3d_,cam,size_,size_);
    view.drawPreview(r3d_,type,owner,time,previewYaw);
    gfx::PassDesc pass;
    pass.colorTarget=color_;
    pass.depthTarget=depth_;
    pass.clearColor[0]=.16f;
    pass.clearColor[1]=.18f;
    pass.clearColor[2]=.22f;
    pass.clearColor[3]=1.f;
    r3d_.render(pass);
    dev_->endPass();
}

} // namespace aiverse::game
