#include "UiLinear.h"

namespace aiverse::ui {

namespace {

// tex0: copy of the scene (plain RGBA8: sRGB-encoded bytes, decoded here with the exact curve);
// tex1: the UI layer (RGBA8_sRGB: the sampler returns linear premultiplied colour).
// U0.p: x = 1 flips v (Metal stores render targets top-down), y = 1 composites over the scene copy,
// 0 = no copy (fallback): the layer is encoded back to sRGB premultiplied and blended by the pipeline.
const char* kVS = R"(
layout(location = 0) in vec2 a_pos;
layout(std140) uniform U0 { vec4 u_p; };
out vec2 v_uv;
void main() {
    v_uv = a_pos * 0.5 + 0.5;
    if (u_p.x > 0.5) v_uv.y = 1.0 - v_uv.y;
    gl_Position = vec4(a_pos, 0.0, 1.0);
}
)";
const char* kFS = R"(
uniform sampler2D tex0;
uniform sampler2D tex1;
layout(std140) uniform U0 { vec4 u_p; };
in vec2 v_uv;
out vec4 o_color;
vec3 toLinear(vec3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, vec3(2.4)), step(vec3(0.04045), c)); }
vec3 toSrgb(vec3 l) {
    l = clamp(l, 0.0, 1.0);
    return mix(l * 12.92, 1.055 * pow(l, vec3(1.0 / 2.4)) - 0.055, step(vec3(0.0031308), l));
}
void main() {
    vec4 ui = texture(tex1, v_uv);
    if (u_p.y > 0.5) {
        vec4 s = texture(tex0, v_uv);
        o_color = vec4(toSrgb(toLinear(s.rgb) * (1.0 - ui.a) + ui.rgb), ui.a + s.a * (1.0 - ui.a));
    } else {
        o_color = ui.a > 0.0 ? vec4(toSrgb(ui.rgb / ui.a) * ui.a, ui.a) : vec4(0.0);
    }
}
)";
const char* kMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4 p; };
struct VIn { float2 pos [[attribute(0)]]; };
struct VOut { float4 pos [[position]]; float2 uv; };
static float3 toLinear(float3 c) { return mix(c / 12.92, pow((c + 0.055) / 1.055, float3(2.4)), step(float3(0.04045), c)); }
static float3 toSrgb(float3 l) {
    l = clamp(l, 0.0, 1.0);
    return mix(l * 12.92, 1.055 * pow(l, float3(1.0 / 2.4)) - 0.055, step(float3(0.0031308), l));
}
vertex VOut vs_main(VIn in [[stage_in]], constant U0& u [[buffer(1)]]) {
    VOut o;
    o.uv = in.pos * 0.5 + 0.5;
    if (u.p.x > 0.5) o.uv.y = 1.0 - o.uv.y;
    o.pos = float4(in.pos, 0.0, 1.0);
    return o;
}
fragment float4 fs_main(VOut in [[stage_in]], constant U0& u [[buffer(1)]],
                        texture2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]],
                        texture2d<float> tex1 [[texture(1)]], sampler s1 [[sampler(1)]]) {
    float4 ui = tex1.sample(s1, in.uv);
    if (u.p.y > 0.5) {
        float4 s = tex0.sample(s0, in.uv);
        return float4(toSrgb(toLinear(s.rgb) * (1.0 - ui.a) + ui.rgb), ui.a + s.a * (1.0 - ui.a));
    }
    return ui.a > 0.0 ? float4(toSrgb(ui.rgb / ui.a) * ui.a, ui.a) : float4(0.0);
}
)";

} // namespace

bool UiLinearComposite::init(gfx::Device& dev) {
    dev_ = &dev;
    if (!dev.supports(gfx::PixelFormat::RGBA8_sRGB)) return false;
    gfx::ShaderDesc sd;
    sd.glslVertex = kVS; sd.glslFragment = kFS; sd.msl = kMSL;
    sd.textureSlots = 2; sd.label = "ui-linear-composite";
    shader_ = dev.createShader(sd);
    if (!shader_.valid()) return false;
    gfx::PipelineDesc pd;
    pd.shader = shader_;
    pd.attrs[0] = {0, gfx::VertexFormat::Float2, 0};
    pd.attrCount = 1;
    pd.stride = 8;
    pd.indexType = gfx::IndexType::None;
    pd.cull = gfx::CullMode::None;
    pd.depthTest = false; pd.depthWrite = false;
    pd.blend = gfx::BlendMode::Opaque;
    pd.label = "ui-linear-composite";
    composite_ = dev.createPipeline(pd);
    pd.blend = gfx::BlendMode::Premultiplied;
    pd.label = "ui-linear-over";
    over_ = dev.createPipeline(pd);
    const float tri[6] = {-1, -1, 3, -1, -1, 3};
    gfx::BufferDesc bd; bd.data = tri; bd.size = sizeof tri; bd.label = "ui-linear-triangle";
    triangle_ = dev.createBuffer(bd);
    if (!composite_.valid() || !over_.valid() || !triangle_.valid()) { shutdown(); return false; }
    dev.setScreenReadable(true);   // Metal: the drawable must be a copy source
    return true;
}

void UiLinearComposite::shutdown() {
    if (!dev_) return;
    resize(0, 0);
    if (triangle_.valid()) dev_->destroyBuffer(triangle_);
    if (composite_.valid()) dev_->destroyPipeline(composite_);
    if (over_.valid()) dev_->destroyPipeline(over_);
    if (shader_.valid()) dev_->destroyShader(shader_);
    triangle_ = {}; composite_ = over_ = {}; shader_ = {};
    dev_->setScreenReadable(false);
    dev_ = nullptr;
}

void UiLinearComposite::resize(int w, int h) {
    if (w == w_ && h == h_ && (w == 0 || layer_.valid())) return;
    for (gfx::TextureHandle* t : {&layer_, &layerDepth_, &scene_})
        if (t->valid()) { dev_->destroyTexture(*t); *t = {}; }
    w_ = w; h_ = h;
    if (w <= 0 || h <= 0) return;
    gfx::TextureDesc td;
    td.width = w; td.height = h; td.renderTarget = true;
    td.format = gfx::PixelFormat::RGBA8_sRGB; td.filter = gfx::Filter::Nearest; td.label = "ui-linear-layer";
    layer_ = dev_->createTexture(td);
    td.format = gfx::PixelFormat::RGBA8; td.label = "ui-linear-scene";   // render target: the screen's format (Metal copies)
    scene_ = dev_->createTexture(td);
    if (dev_->info().backend == gfx::Backend::Metal) {   // Metal pipelines always carry a depth attachment format
        td.format = gfx::PixelFormat::Depth; td.label = "ui-linear-depth";
        layerDepth_ = dev_->createTexture(td);
    }
}

bool UiLinearComposite::beginLayer(int w, int h) {
    if (!ready() || w <= 0 || h <= 0) return false;
    resize(w, h);
    if (!layer_.valid() || !scene_.valid()) return false;
    gfx::PassDesc p;
    p.colorTarget = layer_;
    p.depthTarget = layerDepth_;
    p.clearColor[0] = p.clearColor[1] = p.clearColor[2] = p.clearColor[3] = 0;
    dev_->beginPass(p);
    return true;
}

void UiLinearComposite::composite(gfx::TextureHandle color, gfx::TextureHandle depth, int w, int h, bool drew) {
    // the copy runs between passes (Metal: a blit encoder); a failed copy (screen not readable yet,
    // size mismatch) still shows the UI, blended over the scene in sRGB like the default path
    const bool copied = drew && dev_->copyColor(color, scene_);
    gfx::PassDesc p;
    p.colorTarget = color;
    p.depthTarget = depth;
    p.clearColorEnabled = false;
    p.clearDepthEnabled = false;
    dev_->beginPass(p);
    dev_->setViewport({0, 0, w, h});
    if (!drew) return;
    dev_->setScissor({});
    const float u[4] = {dev_->info().renderTargetFlipY ? 1.0f : 0.0f, copied ? 1.0f : 0.0f, 0, 0};
    dev_->setPipeline(copied ? composite_ : over_);
    dev_->setVertexBuffer(triangle_);
    dev_->setUniforms(0, u, sizeof u);
    dev_->setTexture(0, scene_);
    dev_->setTexture(1, layer_);
    dev_->draw(3);
}

} // namespace aiverse::ui
