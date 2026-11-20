// Vulkan RHI backend on an offscreen device: GL conventions (orientation, depth range, render targets),
// buffer versioning inside a frame, textures, shadow compare, GLSL translation. Skips without a driver.
#include "engine/gfx/vulkan/Vulkan.h"
#include "engine/gfx/vulkan/VkShaderCompiler.h"
#include "engine/render/GiGpuVulkanShaders.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace aiverse::gfx;

namespace {
int g_errors = 0;
#define CHECK(c) do { if (!(c)) { ++g_errors; std::printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

constexpr int W = 64, H = 64;

struct Image {
    int w = 0, h = 0;
    std::vector<uint8_t> px;   // top-down RGBA
    const uint8_t* at(int x, int y) const { return px.data() + (size_t(y) * size_t(w) + size_t(x)) * 4; }
};
bool near(const uint8_t* p, int r, int g, int b) { return std::abs(p[0] - r) <= 3 && std::abs(p[1] - g) <= 3 && std::abs(p[2] - b) <= 3; }

// One frame drawn by `draw`, read back.
template <class F> Image frame(Device& d, F&& draw) {
    Image img;
    d.captureNextFrame([&](int w, int h, std::vector<uint8_t> px) { img.w = w; img.h = h; img.px = std::move(px); });
    d.beginFrame(W, H);
    draw();
    d.endFrame();
    return img;
}

const char* kVs = R"(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(std140) uniform U0 { vec4 u_color; vec4 u_offset; };
out vec2 v_uv;
void main() { v_uv = a_uv; gl_Position = vec4(a_pos.xy + u_offset.xy, a_pos.z, 1.0); }
)";
const char* kColorFs = R"(
in vec2 v_uv;
layout(std140) uniform U0 { vec4 u_color; vec4 u_offset; };
out vec4 o_color;
void main() { o_color = u_color; }
)";
const char* kTexFs = R"(
in vec2 v_uv;
uniform sampler2D tex0;
out vec4 o_color;
void main() { o_color = texture(tex0, v_uv); }
)";
const char* kShadowFs = R"(
in vec2 v_uv;
uniform highp sampler2DShadow tex1;
layout(std140) uniform U0 { vec4 u_color; vec4 u_offset; };
out vec4 o_color;
void main() { float lit = texture(tex1, vec3(v_uv, u_color.x)); o_color = vec4(lit, lit, lit, 1.0); }
)";

// A tiny *real* compute -> storage RGBA16F -> fragment sampler path.
// Unlike source-only shader-string tests this is exercised on any Vulkan
// offscreen driver whose graphics queue reports compute+storage availability.
// Its resource descriptor shape matches GiGpuSmallScene: four SSBOs, one
// storage atlas, 80-byte push constants, 64 logical threads.
const char* kGiStorageCompute = R"GLSL(
#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
layout(set=0,binding=0,std430) readonly buffer Triangles { vec4 tris[]; };
layout(set=0,binding=1,std430) readonly buffer Lamps { vec4 lamps[]; };
layout(set=0,binding=2,std430) readonly buffer Jobs { vec4 jobs[]; };
layout(set=0,binding=3,std430) readonly buffer Nodes { vec4 nodes[]; };
layout(set=0,binding=4,rgba16f) uniform writeonly image2D giAtlas;
layout(push_constant) uniform GIParams {
    uvec4 counts;
    vec4 misc;
    vec4 sun;
    vec4 sky;
    vec4 color;
} parameters;
void main() {
    uint index=gl_GlobalInvocationID.x;
    if(index>=64u)return;
    imageStore(giAtlas,ivec2(int(index%8u),int(index/8u)),parameters.color);
}
)GLSL";

struct Vtx { float x, y, z, u, v; };
BufferDesc bufferDesc(Usage usage, size_t size, const void* data) { BufferDesc b; b.usage = usage; b.size = size; b.data = data; return b; }
std::vector<Vtx> quad(float x0, float y0, float x1, float y1, float z) {
    return {{x0, y0, z, 0, 0}, {x1, y0, z, 1, 0}, {x1, y1, z, 1, 1}, {x0, y0, z, 0, 0}, {x1, y1, z, 1, 1}, {x0, y1, z, 0, 1}};
}

PipelineHandle pipeline(Device& d, ShaderHandle s, BlendMode blend = BlendMode::Opaque, bool depth = false, bool depthOnly = false) {
    PipelineDesc p;
    p.shader = s;
    p.attrs[0] = {0, VertexFormat::Float3, 0};
    p.attrs[1] = {1, VertexFormat::Float2, 12};
    p.attrCount = 2;
    p.stride = sizeof(Vtx);
    p.indexType = IndexType::None;
    p.cull = CullMode::Back;
    p.blend = blend;
    p.depthTest = p.depthWrite = depth;
    p.depthOnly = depthOnly;
    return d.createPipeline(p);
}

void translation() {
    const std::string vs = aiverse::gfx::vk::translateGlsl(kVs, true, false);
    CHECK(vs.find("binding = 0) uniform U0") != std::string::npos);
    CHECK(vs.find("aiverse_user_main") != std::string::npos);
    CHECK(vs.find("gl_Position.z = (gl_Position.z + gl_Position.w) * 0.5") != std::string::npos);
    uint32_t shadow = 0;
    const std::string fs = aiverse::gfx::vk::translateGlsl(kShadowFs, false, false, &shadow);
    CHECK(fs.find("binding = 9) uniform highp sampler2DShadow tex1") != std::string::npos);
    CHECK(shadow == 2u);
    const std::string ids = aiverse::gfx::vk::translateGlsl("void main() { int a = gl_InstanceID + gl_VertexID; } // uniform sampler2D tex3;", true, false);
    CHECK(ids.find("gl_InstanceIndex") != std::string::npos && ids.find("gl_VertexIndex") != std::string::npos);
    CHECK(ids.find("tex3") == std::string::npos);   // comments are dropped, not bound
    aiverse::gfx::vk::SpirvProgram p;
    CHECK(aiverse::gfx::vk::compileProgram(kVs, kColorFs, false, p) && !p.vertex.empty() && !p.fragment.empty());
    CHECK(!aiverse::gfx::vk::compileProgram(kVs, "void main() { undefined_call(); }", false, p) && !p.error.empty());
    // Compile the REAL Vulkan GI BVH kernel even without a Vulkan driver.
    // This is glslang SPIR-V compilation, not a source-text substring check.
    std::vector<uint32_t> computeSpirv;
    std::string computeError;
    const bool computeOk=aiverse::gfx::vk::compileComputeProgram(
        aiverse::render::gi_gpu::kVulkanSmallScene,computeSpirv,computeError);
    if(!computeOk)std::printf("Vulkan GI SPIR-V: %s\\n",computeError.c_str());
    CHECK(computeOk && !computeSpirv.empty());
}
} // namespace

int main() {
    translation();
    std::string err;
    auto dev = createVulkanDevice(VulkanWindow{}, &err);
    if (!dev) {
        std::printf("Vulkan backend: skipped (%s), %d translation errors\n", err.c_str(), g_errors);
        return g_errors ? 1 : 0;
    }
    Device& d = *dev;
    std::printf("%s\n", d.info().name.c_str());
    CHECK(d.info().backend == Backend::Vulkan && !d.info().clipDepthZeroToOne && !d.info().renderTargetFlipY);

    ShaderDesc sd;
    sd.glslVertex = kVs; sd.glslFragment = kColorFs; sd.uniformSlots = 1;
    const ShaderHandle color = d.createShader(sd);
    sd.glslFragment = kTexFs; sd.textureSlots = 1;
    const ShaderHandle tex = d.createShader(sd);
    sd.glslFragment = kShadowFs; sd.textureSlots = 2;
    const ShaderHandle shadow = d.createShader(sd);
    CHECK(color.valid() && tex.valid() && shadow.valid());
    const PipelineHandle pColor = pipeline(d, color), pDepth = pipeline(d, color, BlendMode::Opaque, true);
    const PipelineHandle pTex = pipeline(d, tex), pShadow = pipeline(d, shadow), pCaster = pipeline(d, color, BlendMode::Opaque, true, true);
    const PipelineHandle pAlpha = pipeline(d, color, BlendMode::Alpha);

    const auto top = quad(-1, 0, 1, 1, 0), full = quad(-1, -1, 1, 1, 0);
    const BufferHandle vbTop = d.createBuffer(bufferDesc(Usage::Immutable, top.size() * sizeof(Vtx), top.data()));
    const BufferHandle vbFull = d.createBuffer(bufferDesc(Usage::Immutable, full.size() * sizeof(Vtx), full.data()));
    struct U { float color[4], offset[4]; };
    auto uniforms = [&](float r, float g, float b, float a, float ox = 0, float oy = 0) { U u{{r, g, b, a}, {ox, oy, 0, 0}}; d.setUniforms(0, &u, sizeof u); };
    PassDesc screen;
    screen.clearColor[0] = 0; screen.clearColor[1] = 0; screen.clearColor[2] = 1; screen.clearColor[3] = 1;

    {   // clear + orientation: clip y > 0 is the top of the screen, captures are top-down
        Image img = frame(d, [&] {
            d.beginPass(screen);
            d.setPipeline(pColor); d.setVertexBuffer(vbTop); uniforms(1, 0, 0, 1); d.draw(6);
            d.endPass();
        });
        CHECK(img.w == W && img.h == H);
        CHECK(near(img.at(W / 2, 4), 255, 0, 0));
        CHECK(near(img.at(W / 2, H - 4), 0, 0, 255));
    }
    {   // viewport / scissor rects are top-left based
        Image img = frame(d, [&] {
            d.beginPass(screen);
            d.setPipeline(pColor); d.setVertexBuffer(vbFull);
            d.setViewport({0, 0, W / 2, H / 2}); uniforms(0, 1, 0, 1); d.draw(6);
            d.setViewport({0, 0, W, H}); d.setScissor({W / 2, H / 2, W / 2, H / 2}); uniforms(1, 0, 0, 1); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(4, 4), 0, 255, 0));
        CHECK(near(img.at(W - 4, H - 4), 255, 0, 0));
        CHECK(near(img.at(W - 4, 4), 0, 0, 255));
    }
    {   // depth: GL range, LEQUAL, nearer (smaller z) wins regardless of order
        const auto nearQuad = quad(-1, -1, 1, 1, -0.5f), farQuad = quad(-1, -1, 1, 1, 0.5f);
        const BufferHandle vbNear = d.createBuffer(bufferDesc(Usage::Immutable, nearQuad.size() * sizeof(Vtx), nearQuad.data()));
        const BufferHandle vbFar = d.createBuffer(bufferDesc(Usage::Immutable, farQuad.size() * sizeof(Vtx), farQuad.data()));
        Image img = frame(d, [&] {
            d.beginPass(screen);
            d.setPipeline(pDepth);
            d.setVertexBuffer(vbNear); uniforms(1, 0, 0, 1); d.draw(6);
            d.setVertexBuffer(vbFar); uniforms(0, 1, 0, 1); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(W / 2, H / 2), 255, 0, 0));
        d.destroyBuffer(vbNear); d.destroyBuffer(vbFar);
    }
    {   // render target: not flipped (texture v = 1 is the top, as GL), sampled in the next pass
        TextureDesc td; td.width = W; td.height = H; td.renderTarget = true;
        const TextureHandle rt = d.createTexture(td);
        Image img = frame(d, [&] {
            PassDesc p; p.colorTarget = rt; p.clearColor[0] = 0; p.clearColor[1] = 0; p.clearColor[2] = 0; p.clearColor[3] = 1;
            d.beginPass(p);
            d.setPipeline(pColor); d.setVertexBuffer(vbTop); uniforms(1, 1, 0, 1); d.draw(6);
            d.endPass();
            d.beginPass(screen);
            d.setPipeline(pTex); d.setTexture(0, rt); d.setVertexBuffer(vbFull); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(W / 2, 4), 255, 255, 0));
        CHECK(near(img.at(W / 2, H - 4), 0, 0, 0));
        // copyColor from the screen: same orientation
        TextureDesc cd; cd.width = W; cd.height = H; cd.renderTarget = true;
        const TextureHandle copy = d.createTexture(cd);
        Image img2 = frame(d, [&] {
            d.beginPass(screen); d.setPipeline(pColor); d.setVertexBuffer(vbTop); uniforms(0, 1, 0, 1); d.draw(6); d.endPass();
            CHECK(d.copyColor({}, copy));
            PassDesc p; p.clearColorEnabled = false;
            d.beginPass(p);
            d.setPipeline(pTex); d.setTexture(0, copy); d.setVertexBuffer(vbFull); d.setViewport({0, 0, W / 2, H}); d.draw(6);
            d.endPass();
        });
        CHECK(near(img2.at(4, 4), 0, 255, 0));
        CHECK(near(img2.at(4, H - 4), 0, 0, 255));
        d.destroyTexture(rt); d.destroyTexture(copy);
    }
    {   // a dynamic buffer rewritten between draws of one frame: each draw keeps its data
        const BufferHandle dyn = d.createBuffer(bufferDesc(Usage::Dynamic, top.size() * sizeof(Vtx), nullptr));
        const auto bottom = quad(-1, -1, 1, 0, 0);
        for (int pass = 0; pass < 3; ++pass) {   // several frames: versions recycle
            Image img = frame(d, [&] {
                d.beginPass(screen);
                d.setPipeline(pColor); d.setVertexBuffer(dyn);
                d.updateBuffer(dyn, top.data(), top.size() * sizeof(Vtx)); uniforms(1, 0, 0, 1); d.draw(6);
                d.updateBuffer(dyn, bottom.data(), bottom.size() * sizeof(Vtx)); uniforms(0, 1, 0, 1); d.draw(6);
                d.endPass();
            });
            CHECK(near(img.at(W / 2, 4), 255, 0, 0));
            CHECK(near(img.at(W / 2, H - 4), 0, 255, 0));
        }
        d.destroyBuffer(dyn);
    }
    {   // textures: upload with row pitch, sub-rect update, R8 reads as (r, 0, 0, 1), mipmaps
        std::vector<uint8_t> px(size_t(4) * 4 * 8, 0);   // 4x4 RGBA with an 8-pixel row pitch
        for (int y = 0; y < 4; ++y) for (int x = 0; x < 4; ++x) { uint8_t* p = px.data() + (size_t(y) * 8 + size_t(x)) * 4; p[0] = 200; p[3] = 255; }
        TextureDesc td; td.width = 4; td.height = 4; td.filter = Filter::Nearest; td.mipmaps = true;
        const TextureHandle t = d.createTexture(td);
        d.updateTexture(t, 0, 0, 4, 4, px.data(), 8 * 4);
        TextureDesc rd; rd.width = 2; rd.height = 2; rd.format = PixelFormat::R8; rd.filter = Filter::Nearest;
        const uint8_t r8[4] = {100, 100, 100, 100};
        rd.data = r8;
        const TextureHandle tr = d.createTexture(rd);
        Image img = frame(d, [&] {
            d.beginPass(screen);
            d.setPipeline(pTex); d.setVertexBuffer(vbFull);
            d.setTexture(0, t); d.setViewport({0, 0, W / 2, H}); d.draw(6);
            d.setTexture(0, tr); d.setViewport({W / 2, 0, W / 2, H}); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(4, H / 2), 200, 0, 0));
        CHECK(near(img.at(W - 4, H / 2), 100, 0, 0));
        d.destroyTexture(t); d.destroyTexture(tr);
    }
    {   // Actual Vulkan compute to sampleable RGBA16F GI atlas, not just GLSL source.
        if (d.supportsCompute()) {
            TextureDesc td;
            td.width=8;td.height=8;td.format=PixelFormat::RGBA16F;
            td.storage=true;td.filter=Filter::Nearest;
            const TextureHandle atlas=d.createTexture(td);
            const TextureHandle secondAtlas=d.createTexture(td);
            CHECK(atlas.valid() && secondAtlas.valid());
            const ComputeHandle compute=d.createCompute({nullptr,kGiStorageCompute,
                                                          "offscreen GI storage parity"});
            CHECK(compute.valid());
            const uint32_t storageData[4]={0,0,0,0};
            BufferHandle storage[4]{};
            for(auto& h:storage) {
                BufferDesc bd;
                bd.type=BufferType::Storage;
                bd.usage=Usage::Immutable;
                bd.size=sizeof(storageData);
                bd.data=storageData;
                h=d.createBuffer(bd);
                CHECK(h.valid());
            }
            if(atlas.valid() && secondAtlas.valid() && compute.valid() &&
               storage[0].valid() && storage[1].valid() &&
               storage[2].valid() && storage[3].valid()) {
                struct Parameters {
                    uint32_t counts[4]{};
                    float misc[4]{},sun[4]{},sky[4]{};
                    float color[4]{.25f,.5f,.75f,1.0f};
                } params;
                static_assert(sizeof(Parameters)==80);
                Image img=frame(d,[&] {
                    const bool begun=d.beginCompute(compute);
                    CHECK(begun);
                    if(begun) {
                        for(int i=0;i<4;++i)d.setComputeBuffer(i,storage[i]);
                        d.setComputeTexture(0,atlas);
                        params.color[0]=.9f;params.color[1]=.1f;params.color[2]=.1f;
                        d.setComputeUniforms(&params,sizeof(params));
                        d.dispatch(64);  // first write
                        CHECK(d.computeDispatchSucceeded());
                        params.color[0]=.25f;params.color[1]=.5f;params.color[2]=.75f;
                        d.setComputeUniforms(&params,sizeof(params));
                        d.dispatch(64);  // same image: compute write->write barrier
                        CHECK(d.computeDispatchSucceeded());
                        d.setComputeTexture(0,secondAtlas); // close old GENERAL layout
                        params.color[0]=.75f;params.color[1]=.25f;params.color[2]=.5f;
                        d.setComputeUniforms(&params,sizeof(params));
                        d.dispatch(64);
                        CHECK(d.computeDispatchSucceeded());
                        d.endCompute();
                    }
                    d.beginPass(screen);
                    d.setPipeline(pTex);d.setVertexBuffer(vbFull);
                    d.setViewport({0,0,W/2,H});
                    d.setTexture(0,atlas);
                    d.draw(6);
                    d.setViewport({W/2,0,W/2,H});
                    d.setTexture(0,secondAtlas);
                    d.draw(6);
                    d.endPass();
                });
                CHECK(img.w==W && img.h==H);
                if(img.w==W && img.h==H) {
                    CHECK(near(img.at(8,H/2),64,128,191));
                    CHECK(near(img.at(W-8,H/2),191,64,128));
                }
            }
            for(auto h:storage)if(h.valid())d.destroyBuffer(h);
            if(compute.valid())d.destroyCompute(compute);
            if(atlas.valid())d.destroyTexture(atlas);
            if(secondAtlas.valid())d.destroyTexture(secondAtlas);
        }
    }
    {   // shadow map: depth-only pass, compare sampler (LEQUAL), GL depth values
        TextureDesc sd2; sd2.width = W; sd2.height = H; sd2.format = PixelFormat::Depth; sd2.renderTarget = true; sd2.compare = true;
        const TextureHandle sm = d.createTexture(sd2);
        Image img = frame(d, [&] {
            PassDesc p; p.depthTarget = sm; p.clearColorEnabled = false;
            d.beginPass(p);
            d.setPipeline(pCaster); d.setVertexBuffer(vbTop); uniforms(0, 0, 0, 0); d.draw(6);   // z 0 -> depth 0.5 in the top half
            d.endPass();
            d.beginPass(screen);
            d.setPipeline(pShadow); d.setTexture(1, sm); d.setVertexBuffer(vbFull); uniforms(0.75f, 0, 0, 0); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(W / 2, 4), 0, 0, 0));          // 0.75 <= 0.5 fails: shadowed
        CHECK(near(img.at(W / 2, H - 4), 255, 255, 255)); // 0.75 <= 1.0 (cleared): lit
        d.destroyTexture(sm);
    }
    {   // alpha blending and an HDR (RGBA16F) pass with an LDR pipeline (HDR shader variant)
        Image img = frame(d, [&] {
            d.beginPass(screen);
            d.setPipeline(pAlpha); d.setVertexBuffer(vbFull); uniforms(1, 0, 0, 0.5f); d.draw(6);
            d.endPass();
        });
        CHECK(near(img.at(W / 2, H / 2), 128, 0, 127));
        if (d.supports(PixelFormat::RGBA16F)) {
            // General format support is sampled-texture support. A Vulkan
            // device can support sampled+storage RGBA16F for compute GI without
            // COLOR_ATTACHMENT support; rendering to FP16 is a separate cap.
            TextureDesc hd; hd.width = W; hd.height = H; hd.format = PixelFormat::RGBA16F; hd.renderTarget = true;
            const TextureHandle hdr = d.createTexture(hd);
            if (hdr.valid()) {
                frame(d, [&] { PassDesc p; p.colorTarget = hdr; d.beginPass(p); d.setPipeline(pColor); d.setVertexBuffer(vbFull); uniforms(1, 1, 1, 1); d.draw(6); d.endPass(); });
                d.destroyTexture(hdr);
            } else std::printf("Vulkan: sampled RGBA16F supported, FP16 color attachment unavailable\\n");
        }
    }
    CHECK(d.gpuHealthy());
    d.destroyPipeline(pColor); d.destroyPipeline(pDepth); d.destroyPipeline(pTex); d.destroyPipeline(pShadow); d.destroyPipeline(pCaster); d.destroyPipeline(pAlpha);
    d.destroyShader(color); d.destroyShader(tex); d.destroyShader(shadow);
    d.destroyBuffer(vbTop); d.destroyBuffer(vbFull);
    std::printf("Vulkan backend: %d errors\n", g_errors);
    return g_errors ? 1 : 0;
}
