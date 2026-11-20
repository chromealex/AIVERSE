// Vulkan RHI backend on an offscreen device: GL conventions (orientation, depth range, render targets),
// buffer versioning inside a frame, textures, shadow compare, GLSL translation. Skips without a driver.
#include "engine/gfx/vulkan/Vulkan.h"
#include "engine/gfx/vulkan/VkShaderCompiler.h"
#include "engine/render/LumaWeaveVulkanDirectShader.h"
#include "engine/render/LumaWeaveVulkanScreenShader.h"

#include <array>
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
            TextureDesc hd; hd.width = W; hd.height = H; hd.format = PixelFormat::RGBA16F; hd.renderTarget = true;
            const TextureHandle hdr = d.createTexture(hd);
            frame(d, [&] { PassDesc p; p.colorTarget = hdr; d.beginPass(p); d.setPipeline(pColor); d.setVertexBuffer(vbFull); uniforms(1, 1, 1, 1); d.draw(6); d.endPass(); });
            d.destroyTexture(hdr);
        }
    }
    // Real Vulkan RHI compute->fragment regression (lavapipe or physical GPU).
    // A direct red point light must illuminate the receiver; the SAME ray
    // behind a single y=0.5 BVH triangle must be fully shadowed.
    // This checks actual descriptor layout, buffer transfer visibility,
    // storage-image layout transitions, and GPU ray-pixel output.
    if(d.supportsCompute()) {
        struct LightPacket {
            float pos[4],col[4],dir[4],settings[4];
            uint32_t identity[4];
        };
        struct ReceiverPacket {float position[4],normal[4];};
        struct TriPacket {float a[4],b[4],c[4],color[4];};
        struct NodePacket {float lo[4],hi[4];uint32_t meta[4];};
        struct alignas(16) Params {
            uint32_t counts[4],control[4];
            float reserved0[4],reserved1[4],reserved2[4];
        };
        static_assert(sizeof(LightPacket)==80 && sizeof(ReceiverPacket)==32);
        static_assert(sizeof(TriPacket)==64 && sizeof(NodePacket)==48 &&
                      sizeof(Params)==80);
        const LightPacket l{{0,1,0,3},{1,0,0,100},{0,-1,0,-2},
                            {1,1,1,0},{0xabc,0,0,0}};
        const ReceiverPacket r{{0,0,0,1},{0,1,0,0}};
        // Horizontal occluder spanning the central receiver-to-light ray.
        const TriPacket triangle{{-2,.5f,-2,0},{2,.5f,-2,0},
                                 {0,.5f,2,0},{1,1,1,0}};
        const NodePacket node{{-2,.5f,-2,0},{2,.5f,2,0},{0,0,0,1}};
        auto storage=[&](const void* bytes,size_t count) {
            BufferDesc desc;desc.type=BufferType::Storage;
            desc.usage=Usage::Immutable;desc.data=bytes;desc.size=count;
            return d.createBuffer(desc);
        };
        const auto lightBuffer=storage(&l,sizeof(l));
        const auto receiverBuffer=storage(&r,sizeof(r));
        const auto triBuffer=storage(&triangle,sizeof(triangle));
        const auto nodeBuffer=storage(&node,sizeof(node));
        TextureDesc outDesc;
        outDesc.width=1;outDesc.height=1;
        outDesc.format=PixelFormat::RGBA16F;
        outDesc.filter=Filter::Nearest;outDesc.storage=true;
        const auto out=d.createTexture(outDesc);
        const auto kernel=d.createCompute({
            nullptr,aiverse::render::luma_vk::kReceiverDirectCompute,
            "LumaWeave Vulkan direct pixel/occlusion fixture"});
        CHECK(lightBuffer.valid() && receiverBuffer.valid() &&
              triBuffer.valid() && nodeBuffer.valid() && out.valid() &&
              kernel.valid());
        if(out.valid() && kernel.valid() && lightBuffer.valid() &&
           receiverBuffer.valid() && triBuffer.valid() && nodeBuffer.valid()) {
            auto directFrame=[&](uint32_t nodes)->Image {
                return frame(d,[&]{
                    Params params{};
                    params.counts[0]=1;params.counts[1]=1;
                    params.counts[2]=nodes;params.counts[3]=1;
                    params.control[0]=27;params.control[1]=1;
                    params.control[2]=1;
                    CHECK(d.beginCompute(kernel));
                    d.setComputeBuffer(0,lightBuffer);
                    d.setComputeBuffer(1,receiverBuffer);
                    d.setComputeBuffer(2,triBuffer);
                    d.setComputeBuffer(3,nodeBuffer);
                    d.setComputeTexture(0,out);
                    d.setComputeUniforms(&params,sizeof(params));
                    d.dispatch(1,1);
                    CHECK(d.computeDispatchSucceeded());
                    d.endCompute();
                    d.beginPass(screen);
                    d.setPipeline(pTex);
                    d.setTexture(0,out);
                    d.setVertexBuffer(vbFull);
                    d.draw(6);
                    d.endPass();
                });
            };
            const Image lit=directFrame(0);
            CHECK(lit.w==W && lit.h==H);
            if(lit.w==W && lit.h==H)
                CHECK(near(lit.at(W/2,H/2),255,0,0));
            const Image blocked=directFrame(1);
            CHECK(blocked.w==W && blocked.h==H);
            if(blocked.w==W && blocked.h==H)
                CHECK(near(blocked.at(W/2,H/2),0,0,0));
        }
        if(kernel.valid())d.destroyCompute(kernel);
        if(out.valid())d.destroyTexture(out);
        for(auto b:{lightBuffer,receiverBuffer,triBuffer,nodeBuffer})
            if(b.valid())d.destroyBuffer(b);
    }
    // Stage14: real Vulkan sampled-texture compute descriptor fixture.
    // A fragment-rendered material/depth target later feeds this same ABI.
    if(d.supportsCompute()) {
        const char* sampledCs=R"(
#version 450
layout(local_size_x=64) in;
layout(set=0,binding=0,std430) readonly buffer B0 {float a[];};
layout(set=0,binding=1,std430) readonly buffer B1 {float b[];};
layout(set=0,binding=2,std430) readonly buffer B2 {float c[];};
layout(set=0,binding=3,std430) readonly buffer B3 {float d[];};
layout(rgba16f,set=0,binding=4) writeonly uniform image2D outputImage;
layout(set=0,binding=5) uniform sampler2D sampled0;
layout(set=0,binding=6) uniform sampler2D sampled1;
layout(set=0,binding=7) uniform sampler2D sampled2;
layout(set=0,binding=8) uniform sampler2D sampled3;
layout(set=0,binding=9) uniform sampler2D sampled4;
layout(push_constant) uniform P {uvec4 counts;uvec4 control;vec4 x;vec4 y;vec4 z;} p;
void main() {
    if(gl_GlobalInvocationID.x>=p.counts.x)return;
    vec4 value=(texelFetch(sampled0,ivec2(0),0)+
                texelFetch(sampled1,ivec2(0),0)+
                texelFetch(sampled2,ivec2(0),0)+
                texelFetch(sampled3,ivec2(0),0)+
                texelFetch(sampled4,ivec2(0),0))*.2;
    imageStore(outputImage,ivec2(0),value);
}
)";
        const float dummy=0.0f;
        BufferDesc b; b.type=BufferType::Storage;
        b.size=sizeof(dummy);b.data=&dummy;
        std::array<BufferHandle,4> buffers{};
        for(auto& h:buffers)h=d.createBuffer(b);
        const uint8_t rgba[4]={64,128,192,255};
        TextureDesc inputDesc;inputDesc.width=1;inputDesc.height=1;
        inputDesc.format=PixelFormat::RGBA8;inputDesc.filter=Filter::Nearest;
        inputDesc.data=rgba;
        const auto input=d.createTexture(inputDesc);
        TextureDesc outputDesc;outputDesc.width=1;outputDesc.height=1;
        outputDesc.format=PixelFormat::RGBA16F;outputDesc.storage=true;
        outputDesc.filter=Filter::Nearest;
        const auto output=d.createTexture(outputDesc);
        ComputeDesc cd;cd.glsl=sampledCs;cd.sampledTextureSlots=5;
        const auto kernel=d.createCompute(cd);
        CHECK(kernel.valid() && input.valid() && output.valid());
        if(kernel.valid() && input.valid() && output.valid()) {
            Image image=frame(d,[&] {
                std::array<uint32_t,20> push{};
                push[0]=1u;
                CHECK(d.beginCompute(kernel));
                for(int i=0;i<4;++i)d.setComputeBuffer(i,buffers[size_t(i)]);
                d.setComputeTexture(0,output);
                d.setComputeUniforms(push.data(),sizeof(push));
                d.dispatch(1);
                CHECK(!d.computeDispatchSucceeded()); // missing all sampled inputs
                for(int i=0;i<5;++i)d.setComputeSampledTexture(i,input);
                d.dispatch(1);
                CHECK(d.computeDispatchSucceeded());
                d.endCompute();
                d.beginPass(screen);
                d.setPipeline(pTex);d.setTexture(0,output);
                d.setVertexBuffer(vbFull);d.draw(6);
                d.endPass();
            });
            CHECK(image.w==W && image.h==H);
            if(image.w==W && image.h==H)
                CHECK(near(image.at(W/2,H/2),64,128,192));
        }
        if(kernel.valid())d.destroyCompute(kernel);
        if(input.valid())d.destroyTexture(input);
        if(output.valid())d.destroyTexture(output);
        for(auto h:buffers)if(h.valid())d.destroyBuffer(h);
    }
    // Stage16: real Vulkan PBR screen-space direct compute from independent
    // material/normal/depth passes. Verifies the actual depth-to-world mapping
    // and BVH wall shadow as *GPU pixels*, not a MockDevice approximation.
    if(d.supportsCompute()) {
        struct PackedLight {float position[4],color[4],direction[4],settings[4];
                            uint32_t identity[4];};
        struct PackedTri {float a[4],b[4],c[4],albedo[4];};
        struct PackedNode {float lo[4],hi[4];uint32_t meta[4];};
        struct ScreenUniforms {float inv[16],eye[4],resolution[4],options[4];};
        static_assert(sizeof(PackedLight)==80 && sizeof(PackedTri)==64 &&
                      sizeof(PackedNode)==48 && sizeof(ScreenUniforms)==112);
        PackedLight light{{0,0,2,4},{1,0,0,100},{0,-1,0,-2},
                          {1,1,1,0},{31,0,0,0}};
        PackedTri tri{{-2,-2,1,0},{2,-2,1,0},{0,2,1,0},{1,1,1,0}};
        PackedNode node{{-2,-2,1,0},{2,2,1,0},{0,0,0,1}};
        ScreenUniforms view{};
        view.inv[0]=view.inv[5]=view.inv[10]=view.inv[15]=1;
        view.eye[2]=5;
        view.resolution[0]=view.resolution[1]=1;
        view.resolution[2]=W;view.resolution[3]=H;
        view.options[0]=0.008f;
        auto storage=[&](const void* bytes,size_t n){
            BufferDesc desc;desc.type=BufferType::Storage;
            desc.usage=Usage::Immutable;desc.size=n;desc.data=bytes;
            return d.createBuffer(desc);
        };
        const BufferHandle lightBuf=storage(&light,sizeof(light));
        const BufferHandle viewBuf=storage(&view,sizeof(view));
        const BufferHandle triBuf=storage(&tri,sizeof(tri));
        const BufferHandle nodeBuf=storage(&node,sizeof(node));
        TextureDesc depthDesc;depthDesc.width=W;depthDesc.height=H;
        depthDesc.format=PixelFormat::Depth;depthDesc.renderTarget=true;
        depthDesc.filter=Filter::Nearest;
        const auto sceneZ=d.createTexture(depthDesc);
        const auto materialZ=d.createTexture(depthDesc);
        const auto surfaceZ=d.createTexture(depthDesc);
        TextureDesc colorDesc=depthDesc;colorDesc.format=PixelFormat::RGBA16F;
        const auto material=d.createTexture(colorDesc);
        const auto surface=d.createTexture(colorDesc);
        TextureDesc outDesc;outDesc.width=outDesc.height=1;
        outDesc.format=PixelFormat::RGBA16F;
        outDesc.storage=true;outDesc.filter=Filter::Nearest;
        const auto out=d.createTexture(outDesc);
        ComputeDesc cd;cd.glsl=aiverse::render::luma_vk::kScreenDirectCompute;
        cd.sampledTextureSlots=5;
        const auto kernel=d.createCompute(cd);
        CHECK(kernel.valid() && sceneZ.valid() && material.valid() &&
              surface.valid() && out.valid());
        if(kernel.valid() && sceneZ.valid() && materialZ.valid() &&
           surfaceZ.valid() && material.valid() && surface.valid() &&
           lightBuf.valid() && viewBuf.valid() &&
           triBuf.valid() && nodeBuf.valid() && out.valid()) {
            auto drawScreen=[&](uint32_t n)->Image {
                return frame(d,[&]{
                    PassDesc zPass;zPass.depthTarget=sceneZ;
                    zPass.clearColorEnabled=false;
                    d.beginPass(zPass);
                    d.setPipeline(pCaster);d.setVertexBuffer(vbFull);
                    uniforms(1,1,1,1);d.draw(6);d.endPass();

                    PassDesc materialPass;
                    materialPass.colorTarget=material;
                    materialPass.depthTarget=materialZ;
                    d.beginPass(materialPass);
                    d.setPipeline(pDepth);d.setVertexBuffer(vbFull);
                    uniforms(1,0,0,0);d.draw(6);d.endPass();

                    PassDesc normalPass;
                    normalPass.colorTarget=surface;
                    normalPass.depthTarget=surfaceZ;
                    d.beginPass(normalPass);
                    d.setPipeline(pDepth);d.setVertexBuffer(vbFull);
                    uniforms(0,0,1,.5f);d.draw(6);d.endPass();

                    std::array<uint32_t,20> params{};
                    params[0]=1;params[1]=1;params[2]=n;params[3]=1;
                    params[4]=19;params[5]=1;params[6]=1;
                    CHECK(d.beginCompute(kernel));
                    d.setComputeBuffer(0,lightBuf);
                    d.setComputeBuffer(1,viewBuf);
                    d.setComputeBuffer(2,triBuf);
                    d.setComputeBuffer(3,nodeBuf);
                    d.setComputeTexture(0,out);
                    d.setComputeSampledTexture(0,sceneZ);
                    d.setComputeSampledTexture(1,material);
                    d.setComputeSampledTexture(2,materialZ);
                    d.setComputeSampledTexture(3,surface);
                    d.setComputeSampledTexture(4,surfaceZ);
                    d.setComputeUniforms(params.data(),sizeof(params));
                    d.dispatch(1);
                    CHECK(d.computeDispatchSucceeded());
                    d.endCompute();
                    d.beginPass(screen);
                    d.setPipeline(pTex);d.setTexture(0,out);
                    d.setVertexBuffer(vbFull);d.draw(6);
                    d.endPass();
                });
            };
            const auto lit=drawScreen(0);
            CHECK(lit.w==W && lit.h==H);
            if(lit.w==W && lit.h==H)
                CHECK(near(lit.at(W/2,H/2),255,0,0));
            const auto blocked=drawScreen(1);
            CHECK(blocked.w==W && blocked.h==H);
            if(blocked.w==W && blocked.h==H)
                CHECK(near(blocked.at(W/2,H/2),0,0,0));
        }
        if(kernel.valid())d.destroyCompute(kernel);
        for(auto handle:{sceneZ,materialZ,surfaceZ,material,surface,out})
            if(handle.valid())d.destroyTexture(handle);
        for(auto handle:{lightBuf,viewBuf,triBuf,nodeBuf})
            if(handle.valid())d.destroyBuffer(handle);
    }
    CHECK(d.gpuHealthy());
    d.destroyPipeline(pColor); d.destroyPipeline(pDepth); d.destroyPipeline(pTex); d.destroyPipeline(pShadow); d.destroyPipeline(pCaster); d.destroyPipeline(pAlpha);
    d.destroyShader(color); d.destroyShader(tex); d.destroyShader(shadow);
    d.destroyBuffer(vbTop); d.destroyBuffer(vbFull);
    std::printf("Vulkan backend: %d errors\n", g_errors);
    return g_errors ? 1 : 0;
}
