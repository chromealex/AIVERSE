#include "engine/view/VisibilityField.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>
#include <string>
#include <stdexcept>

namespace aiverse::view {

using namespace render;

namespace {
// GLSL ES 3.00 / MSL. U0: viewProj; color (rgb, a = unexplored opacity); p0 (explored opacity, map w,
// map h, style); grid (rgb, alpha); p1 (noise, time, grid everywhere, unused).
const char* kFogVS = R"(
layout(std140) uniform U0 { mat4 u_viewProj; vec4 u_color; vec4 u_p0; vec4 u_grid; vec4 u_p1; };
layout(location = 0) in vec3 a_pos;
out vec3 v_wpos;
void main() { v_wpos = a_pos; gl_Position = u_viewProj * vec4(a_pos, 1.0); }
)";
const char* kFogFS = R"(
layout(std140) uniform U0 { mat4 u_viewProj; vec4 u_color; vec4 u_p0; vec4 u_grid; vec4 u_p1; };
uniform sampler2D tex0;
in vec3 v_wpos;
out vec4 o_color;
highp float hash2(highp vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }
float vnoise(highp vec2 p) {
    highp vec2 i = floor(p), f = fract(p);
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + vec2(1.0, 0.0)), u.x), mix(hash2(i + vec2(0.0, 1.0)), hash2(i + vec2(1.0, 1.0)), u.x), u.y);
}
void main() {
    vec2 size = u_p0.yz;
    vec2 uv = v_wpos.xz / size;
    int style = int(u_p0.w + 0.5);
    if (style == 1 || style == 2) uv = (floor(v_wpos.xz) + 0.5) / size;     // hard cells
    float v = texture(tex0, uv).r;
    if (style == 0) v = smoothstep(0.0, 1.0, v);                             // softer ramp between the cells
    if (u_p1.x > 0.0 && v > 0.01 && v < 0.99) {                              // cloudy edges
        float n = vnoise(v_wpos.xz * 0.7 + vec2(u_p1.y * 0.05, u_p1.y * 0.03)) + 0.5 * vnoise(v_wpos.xz * 1.9 - u_p1.y * 0.04);
        v = clamp(v + (n / 1.5 - 0.5) * u_p1.x * 0.6, 0.0, 1.0);
    }
    float a = v < 0.5 ? mix(u_color.a, u_p0.x, v * 2.0) : mix(u_p0.x, 0.0, (v - 0.5) * 2.0);
    if (style == 3) {                                                          // dithered edge, classic look
        ivec2 q = ivec2(mod(gl_FragCoord.xy, 4.0));
        int b = (q.x & 1) * 8 + (q.y & 1) * 4 + (q.x & 2) + (q.y & 2) / 2;
        float t = (float(b) + 0.5) / 16.0;
        a = (a / max(u_color.a, 0.001) > t) ? u_color.a : (a > u_p0.x * 0.5 && v < 0.75 ? u_p0.x : 0.0);
    }
    vec3 c = u_color.rgb;
    float g = 0.0;
    if (u_grid.a > 0.0 && (u_p1.z > 0.5 || a > 0.01)) {
        vec2 fw = fract(v_wpos.xz);
        float d = min(min(fw.x, 1.0 - fw.x), min(fw.y, 1.0 - fw.y));
        g = u_grid.a * (1.0 - smoothstep(0.015, 0.05, d));
    }
    if (style == 2 && a > 0.01) g = max(g, 0.18 * (1.0 - smoothstep(0.015, 0.05, min(min(fract(v_wpos.x), 1.0 - fract(v_wpos.x)), min(fract(v_wpos.z), 1.0 - fract(v_wpos.z))))));
    vec3 col = mix(c, u_grid.rgb, g / max(a + g, 0.001));
    float alpha = max(a, g);
    if (alpha < 0.003) discard;
    o_color = rtsDisplay(vec4(col, alpha));
}
)";
const char* kFogMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4x4 viewProj; float4 color; float4 p0; float4 grid; float4 p1; };
struct VIn { float3 pos [[attribute(0)]]; };
struct VOut { float4 pos [[position]]; float3 wpos; };
vertex VOut vs_main(VIn in [[stage_in]], constant U0& u [[buffer(1)]]) { VOut o; o.wpos = in.pos; o.pos = u.viewProj * float4(in.pos, 1.0); return o; }
static float hash2(float2 p) { return fract(sin(dot(p, float2(127.1, 311.7))) * 43758.5453); }
static float vnoise(float2 p) {
    float2 i = floor(p), f = fract(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(hash2(i), hash2(i + float2(1.0, 0.0)), u.x), mix(hash2(i + float2(0.0, 1.0)), hash2(i + float2(1.0, 1.0)), u.x), u.y);
}
fragment float4 fs_main(VOut in [[stage_in]], constant U0& u [[buffer(1)]], texture2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]]) {
    float2 size = u.p0.yz;
    float2 uv = in.wpos.xz / size;
    int style = int(u.p0.w + 0.5);
    if (style == 1 || style == 2) uv = (floor(in.wpos.xz) + 0.5) / size;
    float v = tex0.sample(s0, uv).r;
    if (style == 0) v = smoothstep(0.0, 1.0, v);
    if (u.p1.x > 0.0 && v > 0.01 && v < 0.99) {
        float n = vnoise(in.wpos.xz * 0.7 + float2(u.p1.y * 0.05, u.p1.y * 0.03)) + 0.5 * vnoise(in.wpos.xz * 1.9 - u.p1.y * 0.04);
        v = clamp(v + (n / 1.5 - 0.5) * u.p1.x * 0.6, 0.0, 1.0);
    }
    float a = v < 0.5 ? mix(u.color.a, u.p0.x, v * 2.0) : mix(u.p0.x, 0.0, (v - 0.5) * 2.0);
    if (style == 3) {
        int2 q = int2(fmod(in.pos.xy, 4.0));
        int b = (q.x & 1) * 8 + (q.y & 1) * 4 + (q.x & 2) + (q.y & 2) / 2;
        float t = (float(b) + 0.5) / 16.0;
        a = (a / max(u.color.a, 0.001) > t) ? u.color.a : (a > u.p0.x * 0.5 && v < 0.75 ? u.p0.x : 0.0);
    }
    float3 c = u.color.rgb;
    float g = 0.0;
    if (u.grid.a > 0.0 && (u.p1.z > 0.5 || a > 0.01)) {
        float2 fw = fract(in.wpos.xz);
        float d = min(min(fw.x, 1.0 - fw.x), min(fw.y, 1.0 - fw.y));
        g = u.grid.a * (1.0 - smoothstep(0.015, 0.05, d));
    }
    if (style == 2 && a > 0.01) {
        float2 fw = fract(in.wpos.xz);
        g = max(g, 0.18 * (1.0 - smoothstep(0.015, 0.05, min(min(fw.x, 1.0 - fw.x), min(fw.y, 1.0 - fw.y)))));
    }
    float3 col = mix(c, u.grid.rgb, g / max(a + g, 0.001));
    float alpha = max(a, g);
    if (alpha < 0.003) discard_fragment();
    return rtsDisplay(float4(col, alpha));
}
)";

struct Uniforms { float viewProj[16]; float color[4]; float p0[4]; float grid[4]; float p1[4]; };

void readColor(const Json& c, float (&out)[3]) {
    if (c.size() < 3) return;
    for (int i = 0; i < 3; ++i) {
        double v = c[size_t(i)].toDouble();
        out[i] = float(v > 1.0 ? v / 255.0 : v);       // [8, 10, 14] or [0.03, 0.04, 0.05]
    }
}
void apply(VisibilityLook& L, const Json& j) {
    if (!j.isObject()) return;
    if (j.has("show")) L.show = j.getBool("show", true);
    if (j.has("mode") && (j.getStr("mode") == "none" || j.getStr("mode") == "off")) L.show = false;
    readColor(j["color"], L.color);
    if (j.has("unexplored")) L.unexplored = std::clamp(float(j["unexplored"].toDouble()), 0.0f, 1.0f);
    if (j.has("explored")) L.explored = std::clamp(float(j["explored"].toDouble()), 0.0f, 1.0f);
    if (j.has("style")) {
        std::string s = j.getStr("style");
        L.style = s == "cells" ? VisibilityLook::Cells : s == "grid" ? VisibilityLook::Grid : s == "pattern" ? VisibilityLook::Pattern : VisibilityLook::Smooth;
    }
    if (j.has("softness")) L.softness = std::clamp(int(j["softness"].integer()), 0, 6);
    if (j.has("fade")) L.fade = std::clamp(float(j["fade"].toDouble()), 0.0f, 5.0f);
    if (j.has("noise")) L.noise = std::clamp(float(j["noise"].toDouble()), 0.0f, 1.0f);
    if (j.has("height")) L.height = float(j["height"].toDouble());
    const Json& g = j["grid"];
    if (g.isObject()) {
        std::string show = g.getStr("show", "fog");
        L.gridEverywhere = show == "all" || show == "always";
        L.gridAlpha = show == "none" ? 0.0f : std::clamp(float(g["alpha"].toDouble(0.12)), 0.0f, 1.0f);
        readColor(g["color"], L.gridColor);
    } else if (!g.isNull()) {
        L.gridAlpha = g.boolean(false) ? 0.12f : 0.0f;   // "grid": true
    }
}

// Box blur of radius r over a byte image (separable, integer: same result everywhere).
void boxBlur(std::vector<uint8_t>& img, std::vector<uint16_t>& tmp, int w, int h, int r) {
    if (r <= 0) return;
    tmp.assign(img.size(), 0);
    const int n = 2 * r + 1;
    for (int y = 0; y < h; ++y) {
        int sum = 0;
        const uint8_t* row = &img[size_t(y) * size_t(w)];
        for (int x = -r; x <= r; ++x) sum += row[std::clamp(x, 0, w - 1)];
        for (int x = 0; x < w; ++x) {
            tmp[size_t(y) * size_t(w) + size_t(x)] = uint16_t(sum);
            sum += row[std::min(x + r + 1, w - 1)] - row[std::max(x - r, 0)];
        }
    }
    for (int x = 0; x < w; ++x) {
        int sum = 0;
        for (int y = -r; y <= r; ++y) sum += tmp[size_t(std::clamp(y, 0, h - 1)) * size_t(w) + size_t(x)];
        for (int y = 0; y < h; ++y) {
            img[size_t(y) * size_t(w) + size_t(x)] = uint8_t((sum + n * n / 2) / (n * n));
            sum += tmp[size_t(std::min(y + r + 1, h - 1)) * size_t(w) + size_t(x)] - tmp[size_t(std::max(y - r, 0)) * size_t(w) + size_t(x)];
        }
    }
}
} // namespace

VisibilityLook parseVisibilityLook(const Json& baseConfig, const Json& overrides) {
    VisibilityLook L;
    apply(L, baseConfig);
    apply(L, overrides);
    return L;
}

bool VisibilityField::init(gfx::Device& dev) {
    if (dev_ && dev_ != &dev)
        throw std::logic_error("VisibilityField init: GPU device mismatch");
    if (ready()) return true;

    // These resources have never been published to a renderer. A failed
    // allocation can be destroyed immediately without waiting for old draws:
    // no in-flight frame can reference either temporary handle.
    gfx::ShaderHandle shader{};
    gfx::PipelineHandle pipe{};
    try {
        gfx::ShaderDesc sd;
        sd.glslVertex = kFogVS; sd.glslFragment = kFogFS; sd.msl = kFogMSL;
        sd.textureSlots = 1; sd.uniformSlots = 1; sd.label = "fog of war";
        shader = dev.createShader(sd);
        if (!shader.valid()) return false;

        gfx::PipelineDesc pd;
        pd.shader = shader;
        pd.attrs[0] = {0, gfx::VertexFormat::Float3, 0};
        pd.attrCount = 1; pd.stride = 12;
        pd.indexType = gfx::IndexType::UInt32;
        pd.cull = gfx::CullMode::None;
        pd.blend = gfx::BlendMode::Alpha;
        pd.depthTest = true;
        pd.depthWrite = false;
        pd.label = "fog of war";
        pipe = dev.createPipeline(pd);
        if (!pipe.valid()) {
            // Clear the local owner before destruction; if a backend throws
            // on cleanup, the exception handler must not destroy twice.
            const auto failedShader=shader;
            shader={};
            dev.destroyShader(failedShader);
            return false;
        }
    } catch (...) {
        if (pipe.valid()) dev.destroyPipeline(pipe);
        if (shader.valid()) dev.destroyShader(shader);
        throw;
    }
    shader_ = shader;
    pipe_ = pipe;
    dev_ = &dev;
    return true;
}

bool VisibilityField::shutdownAfterGpuIdle() {
    if (!dev_) return true;
    if (!dev_->waitForResourceIdle()) return false;
    shutdown();
    return true;
}

void VisibilityField::shutdown() {
    if (!dev_) return;
    if(!retired_.completeForShutdown(*dev_))
        throw std::runtime_error("VisibilityField: GPU retirement idle proof failed");
    for (auto* b : {&vb_, &ib_}) if (b->valid()) { dev_->destroyBuffer(*b); *b = {}; }
    if (tex_.valid()) { dev_->destroyTexture(tex_); tex_ = {}; }
    if (pipe_.valid()) dev_->destroyPipeline(pipe_);
    if (shader_.valid()) dev_->destroyShader(shader_);
    pipe_ = {}; shader_ = {};
    dev_ = nullptr;
    w_ = h_ = 0;
    ++revision_;
}

void VisibilityField::build(gfx::Device& dev, int w, int h, const std::function<float(float, float)>& height) {
    if (!dev_ || w <= 0 || h <= 0) return;
    if (dev_ != &dev)
        throw std::logic_error("VisibilityField build: GPU device mismatch");
    // draped sheet: one vertex per cell corner at the highest neighbouring cell centre, so plateaus
    // never poke through; coarser on big maps (the texture carries the detail)
    const int step = std::max(1, std::max(w, h) / 256);
    const int cw = (w + step - 1) / step, ch = (h + step - 1) / step;
    std::vector<float> v;
    v.reserve(size_t(cw + 1) * size_t(ch + 1) * 3);
    for (int j = 0; j <= ch; ++j)
        for (int i = 0; i <= cw; ++i) {
            const float x = float(std::min(i * step, w)), z = float(std::min(j * step, h));
            float g = -1e9f;
            for (int dz = -1; dz <= 0; ++dz)
                for (int dx = -1; dx <= 0; ++dx) {
                    float cx = std::clamp(x + dx * float(step) + float(step) * 0.5f, 0.5f, float(w) - 0.5f);
                    float cz = std::clamp(z + dz * float(step) + float(step) * 0.5f, 0.5f, float(h) - 0.5f);
                    g = std::max(g, height(cx, cz));
                }
            v.push_back(x); v.push_back(g + look_.height); v.push_back(z);
        }
    std::vector<uint32_t> idx;
    idx.reserve(size_t(cw) * size_t(ch) * 6);
    for (int j = 0; j < ch; ++j)
        for (int i = 0; i < cw; ++i) {
            uint32_t a = uint32_t(j * (cw + 1) + i), b = a + 1, c = a + uint32_t(cw + 1), d = c + 1;
            idx.insert(idx.end(), {a, c, d, a, d, b});
        }
    // All CPU preparation happens before modifying the current published
    // view. The GPU handles below are private staging objects until commit.
    const bool resize=(w!=w_ || h!=h_ || !tex_.valid());
    std::vector<uint8_t> nextTarget,nextCur,nextField;
    if (resize) {
        nextTarget.assign(size_t(w)*size_t(h),0);
        nextCur=nextTarget;
        nextField=nextTarget;
    }
    gfx::BufferHandle nextVb{},nextIb{};
    gfx::TextureHandle nextTex{};
    try {
        gfx::BufferDesc vb; vb.size=v.size()*sizeof(float); vb.data=v.data(); vb.label="fog sheet";
        nextVb=dev.createBuffer(vb);
        if(!nextVb.valid())
            throw std::runtime_error("VisibilityField build: vertex buffer allocation failed");
        gfx::BufferDesc ib; ib.type=gfx::BufferType::Index;
        ib.size=idx.size()*sizeof(uint32_t); ib.data=idx.data(); ib.label="fog sheet idx";
        nextIb=dev.createBuffer(ib);
        if(!nextIb.valid())
            throw std::runtime_error("VisibilityField build: index buffer allocation failed");
        if (resize) {
            gfx::TextureDesc td;
            td.width=w; td.height=h; td.format=gfx::PixelFormat::R8;
            td.filter=gfx::Filter::Linear; td.data=nextField.data(); td.label="fog of war";
            nextTex=dev.createTexture(td);
            if(!nextTex.valid())
                throw std::runtime_error("VisibilityField build: field texture allocation failed");
        }
        // New staging handles were never submitted. Earlier camera passes
        // may still reference the *old* mesh and texture. A failed barrier
        // must preserve them, without publishing the replacement.
        if ((vb_.valid() || ib_.valid() || (resize && tex_.valid())) &&
            !dev.waitForResourceIdle())
            throw std::runtime_error("VisibilityField build: GPU idle barrier failed");
    } catch (...) {
        if(nextTex.valid())dev.destroyTexture(nextTex);
        if(nextIb.valid())dev.destroyBuffer(nextIb);
        if(nextVb.valid())dev.destroyBuffer(nextVb);
        throw;
    }
    if(vb_.valid())dev.destroyBuffer(vb_);
    if(ib_.valid())dev.destroyBuffer(ib_);
    vb_=nextVb;
    ib_=nextIb;
    indexCount_=uint32_t(idx.size());
    if(resize) {
        if(tex_.valid())dev.destroyTexture(tex_);
        tex_=nextTex;
        w_=w; h_=h;
        target_.swap(nextTarget);
        cur_.swap(nextCur);
        field_.swap(nextField);
        lastTime_=-1;
        dirty_=true;
        fading_=false;
    }
    ++revision_;
}


std::unique_ptr<VisibilityField::PreparedSheet> VisibilityField::prepareSheet(
        gfx::Device& dev,int w,int h,
        const std::function<float(float,float)>& height) {
    if(!dev_ || w<=0 || h<=0)return {};
    if(dev_!=&dev)
        throw std::logic_error("VisibilityField sheet staging: wrong device");
    auto next=std::make_unique<PreparedSheet>();
    next->owner_=this;
    next->dev_=&dev;
    next->revision_=revision_;
    next->width_=w;
    next->height_=h;
    next->resize_=(w!=w_ || h!=h_ || !tex_.valid());
    if(next->resize_) {
        next->target_.assign(size_t(w)*size_t(h),0);
        next->cur_=next->target_;
        next->field_=next->target_;
    }
    const int step=std::max(1,std::max(w,h)/256);
    const int cw=(w+step-1)/step,ch=(h+step-1)/step;
    std::vector<float> vertices;
    vertices.reserve(size_t(cw+1)*size_t(ch+1)*3);
    for(int j=0;j<=ch;++j)
        for(int i=0;i<=cw;++i) {
            const float x=float(std::min(i*step,w)),z=float(std::min(j*step,h));
            float ground=-1e9f;
            for(int dz=-1;dz<=0;++dz)
                for(int dx=-1;dx<=0;++dx) {
                    const float cx=std::clamp(x+dx*float(step)+float(step)*.5f,
                                              .5f,float(w)-.5f);
                    const float cz=std::clamp(z+dz*float(step)+float(step)*.5f,
                                              .5f,float(h)-.5f);
                    ground=std::max(ground,height(cx,cz));
                }
            vertices.push_back(x);
            vertices.push_back(ground+look_.height);
            vertices.push_back(z);
        }
    std::vector<uint32_t> indices;
    indices.reserve(size_t(cw)*size_t(ch)*6);
    for(int j=0;j<ch;++j)
        for(int i=0;i<cw;++i) {
            const uint32_t a=uint32_t(j*(cw+1)+i),b=a+1;
            const uint32_t cc=a+uint32_t(cw+1),d=cc+1;
            indices.insert(indices.end(),{a,cc,d,a,d,b});
        }
    gfx::BufferDesc vb;
    vb.size=vertices.size()*sizeof(float);
    vb.data=vertices.data();
    vb.label="fog sheet staged";
    next->vb_=dev.createBuffer(vb);
    if(!next->vb_.valid())
        throw std::runtime_error("VisibilityField sheet: staged vertex buffer failed");
    gfx::BufferDesc ib;
    ib.type=gfx::BufferType::Index;
    ib.size=indices.size()*sizeof(uint32_t);
    ib.data=indices.data();
    ib.label="fog sheet indices staged";
    next->ib_=dev.createBuffer(ib);
    if(!next->ib_.valid())
        throw std::runtime_error("VisibilityField sheet: staged index buffer failed");
    next->indexCount_=uint32_t(indices.size());
    return next;
}

std::unique_ptr<VisibilityField::PreparedUpdate> VisibilityField::prepareUpdate(
        std::span<const uint8_t> vision,int w,int h,double time,
        const PreparedSheet* stagedSheet) {
    if(stagedSheet && (stagedSheet->owner_!=this ||
       stagedSheet->dev_!=dev_ || stagedSheet->revision_!=revision_ ||
       stagedSheet->width_!=w || stagedSheet->height_!=h))
        throw std::logic_error("VisibilityField: stale prepared sheet");
    const bool resize=stagedSheet && stagedSheet->resize_;
    const auto& sourceTarget=resize?stagedSheet->target_:target_;
    const auto& sourceCur=resize?stagedSheet->cur_:cur_;
    const auto& sourceField=resize?stagedSheet->field_:field_;
    const double sourceLast=resize?-1.0:lastTime_;
    const bool sourceDirty=resize?true:dirty_;
    const bool sourceFading=resize?false:fading_;
    if(!dev_ || w!=(stagedSheet?stagedSheet->width_:w_) ||
       h!=(stagedSheet?stagedSheet->height_:h_) ||
       vision.size()!=sourceTarget.size())
        return {};
    auto next=std::make_unique<PreparedUpdate>();
    next->owner_=this;
    next->dev_=dev_;
    next->revision_=revision_;
    next->lastTime_=time;
    // The common unchanged frame only advances its clock. Avoid copying
    // and reallocating the full CPU visibility field in that case.
    if(!sourceDirty && !sourceFading) {
        bool identical=true;
        for(size_t i=0;i<vision.size();++i) {
            const uint8_t target=vision[i]>=2?255:vision[i]==1?128:0;
            if(sourceTarget[i]!=target) {identical=false;break;}
        }
        if(identical) {
            next->unchangedOnly_=true;
            return next;
        }
    }
    next->target_=sourceTarget;
    next->cur_=sourceCur;
    next->field_=sourceField;
    next->tmp_=resize?std::vector<uint16_t>{}:tmp_;
    next->lastTime_=time;
    next->dirty_=sourceDirty;
    next->fading_=sourceFading;
    const float dt=sourceLast<0 ? 1e6f :
        float(std::max(0.0,time-sourceLast));
    bool changed=sourceDirty;
    for(size_t i=0;i<vision.size();++i) {
        const uint8_t t=vision[i]>=2?255:vision[i]==1?128:0;
        if(next->target_[i]!=t) {
            next->target_[i]=t;
            changed=true;
            next->fading_=true;
        }
    }
    if(next->fading_ || next->dirty_) {
        const int stepPx=look_.fade<=0?255:
            std::max(1,int(dt*255.0f/look_.fade));
        bool any=false;
        for(size_t i=0;i<next->cur_.size();++i) {
            int c=next->cur_[i],t=next->target_[i];
            if(c==t)continue;
            c=c<t?std::min(t,c+stepPx):std::max(t,c-stepPx);
            next->cur_[i]=uint8_t(c);
            any=any || c!=t;
            changed=true;
        }
        next->fading_=any;
    }
    next->changed_=changed;
    if(changed) {
        next->field_=next->cur_;
        if(look_.style==VisibilityLook::Smooth ||
           look_.style==VisibilityLook::Pattern) {
            if(blur_)blur_(next->field_,look_.softness);
            else boxBlur(next->field_,next->tmp_,w,h,look_.softness);
        }
        // Copy-on-write GPU texture generation. Never mutate a texture
        // referenced by an already queued primary or RT camera.
        gfx::TextureDesc td;
        td.width=w;td.height=h;
        td.format=gfx::PixelFormat::R8;
        td.filter=gfx::Filter::Linear;
        td.data=nullptr;
        td.label="fog of war staged";
        next->tex_=dev_->createTexture(td);
        if(!next->tex_.valid())
            throw std::runtime_error("VisibilityField: staged texture allocation failed");
        // An injected/real upload exception destroys only this private texture.
        dev_->updateTexture(next->tex_,0,0,w,h,next->field_.data(),w);
    }
    return next;
}

bool VisibilityField::commitPreparedUpdate(
        std::unique_ptr<PreparedUpdate> next) {
    if(!next || next->owner_!=this || next->dev_!=dev_ ||
       next->revision_!=revision_)
        throw std::logic_error("VisibilityField: stale prepared update");
    if(next->unchangedOnly_) {
        lastTime_=next->lastTime_;
        ++revision_;
        return false;
    }
    const bool changed=next->changed_;
    if(changed) {
        retired_.retireTexture(*dev_,tex_);
        tex_=std::exchange(next->tex_,gfx::TextureHandle{});
    }
    target_.swap(next->target_);
    cur_.swap(next->cur_);
    field_.swap(next->field_);
    tmp_.swap(next->tmp_);
    lastTime_=next->lastTime_;
    dirty_=false;
    fading_=next->fading_;
    ++revision_;
    return changed;
}

bool VisibilityField::commitPreparedSheet(
        std::unique_ptr<PreparedSheet> sheet) noexcept {
    if(!sheet)return false;
    // This path is ONLY for unchanged map dimensions. It must never
    // publish an RHI mesh with a texture of another size, nor mutate
    // the user's explored visibility field or fade clock.
    if(!dev_ || sheet->owner_!=this || sheet->dev_!=dev_ ||
       sheet->revision_!=revision_ || sheet->resize_ ||
       sheet->width_!=w_ || sheet->height_!=h_ || !tex_.valid())
        std::terminate();
    retired_.retireBuffer(*dev_,vb_);
    retired_.retireBuffer(*dev_,ib_);
    vb_=std::exchange(sheet->vb_,gfx::BufferHandle{});
    ib_=std::exchange(sheet->ib_,gfx::BufferHandle{});
    indexCount_=sheet->indexCount_;
    ++revision_;
    return true;
}

bool VisibilityField::commitPreparedFrame(
        std::unique_ptr<PreparedSheet> sheet,
        std::unique_ptr<PreparedUpdate> field) noexcept {
    if(!sheet) {
        if(!field)return false;
        try {return commitPreparedUpdate(std::move(field));}
        catch(...) {std::terminate();}
    }
    // All assertions happen before publishing any resource. No other thread
    // may mutate the owner between prepareAll and publish on the render thread.
    if(!dev_ || sheet->owner_!=this || sheet->dev_!=dev_ ||
       sheet->revision_!=revision_ || !field ||
       field->owner_!=this || field->dev_!=dev_ ||
       field->revision_!=revision_ ||
       (sheet->resize_ && (!field->changed_ || field->unchangedOnly_)))
        std::terminate();
    retired_.retireBuffer(*dev_,vb_);
    retired_.retireBuffer(*dev_,ib_);
    vb_=std::exchange(sheet->vb_,gfx::BufferHandle{});
    ib_=std::exchange(sheet->ib_,gfx::BufferHandle{});
    indexCount_=sheet->indexCount_;
    if(sheet->resize_) {
        // Commit the fresh map-sized CPU baseline without invalidating the
        // prepared field's source revision. The field publishes immediately
        // afterwards, including its matching resized R8 texture.
        w_=sheet->width_;
        h_=sheet->height_;
        target_.swap(sheet->target_);
        cur_.swap(sheet->cur_);
        field_.swap(sheet->field_);
        lastTime_=-1;
        dirty_=true;
        fading_=false;
    }
    try {return commitPreparedUpdate(std::move(field));}
    catch(...) {std::terminate();}
}

bool VisibilityField::update(std::span<const uint8_t> vision,int w,int h,double time) {
    auto candidate=prepareUpdate(vision,w,h,time);
    return candidate?commitPreparedUpdate(std::move(candidate)):false;
}

void VisibilityField::drawCandidate(PreparedSheet* sheet,PreparedUpdate* field,
        Renderer3D& r,double time,bool low) {
    if((sheet && (sheet->owner_!=this || sheet->revision_!=revision_)) ||
       (field && (field->owner_!=this || field->revision_!=revision_)))
        throw std::logic_error("VisibilityField: stale candidate draw");
    const bool texture=field && field->tex_.valid();
    const auto exchange=[&]() noexcept {
        using std::swap;
        if(sheet){swap(vb_,sheet->vb_);swap(ib_,sheet->ib_);swap(indexCount_,sheet->indexCount_);
            swap(w_,sheet->width_);swap(h_,sheet->height_);}
        if(texture)swap(tex_,field->tex_);
    };
    exchange();try {draw(r,time,low);}catch(...) {exchange();throw;}exchange();
}

void VisibilityField::draw(Renderer3D& r, double time, bool lowTier) {
    if (!dev_ || !pipe_.valid() || !vb_.valid() || !tex_.valid() || !look_.show) return;
    Uniforms u{};
    std::memcpy(u.viewProj, r.viewProj().m, sizeof u.viewProj);
    u.color[0] = look_.color[0]; u.color[1] = look_.color[1]; u.color[2] = look_.color[2]; u.color[3] = look_.unexplored;
    const int style = lowTier && look_.style == VisibilityLook::Smooth ? VisibilityLook::Cells : look_.style;
    u.p0[0] = look_.explored; u.p0[1] = float(w_); u.p0[2] = float(h_); u.p0[3] = float(style);
    u.grid[0] = look_.gridColor[0]; u.grid[1] = look_.gridColor[1]; u.grid[2] = look_.gridColor[2]; u.grid[3] = look_.gridAlpha;
    u.p1[0] = lowTier ? 0.0f : look_.noise; u.p1[1] = float(std::fmod(time, 10000.0)); u.p1[2] = look_.gridEverywhere ? 1.0f : 0.0f;
    gfx::PipelineHandle pipe = pipe_;
    gfx::BufferHandle vb = vb_, ib = ib_;
    gfx::TextureHandle tex = tex_;
    const uint32_t count = indexCount_;
    r.drawCustom([pipe, vb, ib, tex, count, u](gfx::Device& dev) {
        dev.setPipeline(pipe);
        dev.setUniforms(0, &u, sizeof u);
        dev.setTexture(0, tex);
        dev.setVertexBuffer(vb);
        dev.setIndexBuffer(ib);
        dev.drawIndexed(count, 0, 0);
    });
}

} // namespace aiverse::game
