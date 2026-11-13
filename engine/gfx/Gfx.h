// RHI: a thin, explicit rendering interface implemented directly on top of native APIs.
//
//   Backends:  Metal        -> macOS, iOS
//              OpenGL ES 3  -> Android, Web (WebGL2), Linux (dev/CI via EGL)
//              OpenGL 3.3   -> Windows (WGL)  [D3D11 backend planned]
//
// Conventions shared by every backend:
//   * pixel coordinates (viewport/scissor) have their origin in the TOP-LEFT corner;
//   * clip-space depth range is reported in DeviceInfo::clipDepthZeroToOne, build projection
//     matrices with it (Metal: [0,1], GL: [-1,1]);
//   * matrices are column-major, shaders multiply as  M * v;
//   * uniform data is pushed per draw with setUniforms(slot, ptr, size) (<= 4 KB, std140 layout);
//     GLSL: uniform block "U<slot>", MSL: [[buffer(1 + slot)]] in both stages;
//   * textures: GLSL sampler2D "tex<slot>" (sampler2DShadow for compare textures), MSL texture(slot) +
//     sampler(slot) (depth2d + sample_compare for compare textures); bound for both stages;
//   * instancing: drawIndexed(..., instanceCount); GLSL gl_InstanceID, MSL [[instance_id]];
//   * render-to-texture: when sampling a target in a shader, Metal needs v flipped (DeviceInfo::renderTargetFlipY);
//   * vertex buffer: one interleaved stream; GLSL attribute location = VertexAttr::location,
//     MSL [[attribute(location)]] via [[stage_in]] at buffer(0).
#pragma once
#include <cstddef>
#include <cstdint>
#include <memory>
#include <functional>
#include <string>
#include <vector>

namespace aiverse::gfx {

template <class Tag> struct Handle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
    bool operator==(const Handle&) const = default;
};
using BufferHandle = Handle<struct BufferTag>;
using TextureHandle = Handle<struct TextureTag>;
using ShaderHandle = Handle<struct ShaderTag>;
using ComputeHandle = Handle<struct ComputeTag>;
using PipelineHandle = Handle<struct PipelineTag>;

enum class Backend { Metal, GLES3, GL33 };
enum class BufferType { Vertex, Index, Storage };
enum class Usage { Immutable, Dynamic };          // Dynamic: updated (whole or partially) every frame
enum class PixelFormat { RGBA8, R8, Depth, RGBA32F,
                         ETC2_RGB8, ETC2_RGBA8, BC1, BC3, BC7, ASTC_4x4, ASTC_6x6,   // block-compressed: upload levels, check supports()   // Depth: 24/32-bit depth render target; RGBA32F: data textures
                                                        // (bone matrices, morph deltas), nearest only, read with texelFetch / read()
                         RGBA8_sRGB, // sampling decodes sRGB; render targets encode on write and blend in linear; alpha stays linear
                         RGBA16F }; // linear HDR radiance, 8 bytes/pixel; check supports()
enum class Filter { Nearest, Linear };
enum class VertexFormat { Float2, Float3, Float4, UByte4N, UByte4 }; // UByte4: integers 0..255 (GLSL float / MSL uchar4)
enum class IndexType { None, UInt16, UInt32 };
enum class Primitive { Triangles, Lines };
enum class CullMode { None, Back, Front };
enum class BlendMode { Opaque, Alpha, Additive, Premultiplied, Multiply, Subtract, Screen };
// Premultiplied: ONE, 1-srcAlpha. Multiply: dst * src colour. Subtract: dst - src * srcAlpha (reverse subtract). (particles)
// Screen: src + dst * (1 - src colour) for premultiplied colour, alpha as Premultiplied (Spine "screen" slots)

struct BufferDesc {
    BufferType type = BufferType::Vertex;
    Usage usage = Usage::Immutable;
    size_t size = 0;
    const void* data = nullptr;
    const char* label = nullptr;
    bool snapshotUpdates = false; // Dynamic: preserve earlier updates encoded in this frame
};

struct TextureDesc {
    int width = 0, height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    Filter filter = Filter::Linear;
    bool repeat = false;
    bool renderTarget = false;  // can be attached to a pass (color or depth)
    bool compare = false;       // depth textures: sampled with hardware depth comparison (shadow maps)
    bool mipmaps = false;       // RGBA8/R8: full mip chain generated from level 0 (trilinear when Linear)
    const void* data = nullptr; // may be null (contents undefined until updated)
    // Block-compressed formats: every mip level as given (no generation); level i is levelSize[i] bytes.
    int levelCount = 0;
    const void* const* levelData = nullptr;
    const size_t* levelSize = nullptr;
    const char* label = nullptr;
    bool storage = false; // compute writable; requires supportsCompute()
};

// Each backend picks the sources it understands. A shader may provide all of them.
struct ShaderDesc {
    const char* glslVertex = nullptr;   // GLSL ES 3.00 body WITHOUT #version line (added by backend)
    const char* glslFragment = nullptr;
    const char* msl = nullptr;          // one MSL source with entry points "vs_main"/"fs_main"
    int textureSlots = 0;               // number of sampled textures (tex0..)
    int uniformSlots = 1;               // number of uniform blocks (U0..)
    const char* label = nullptr;
    bool linearTarget = false; // internal variant: AIVERSE_HDR=1, selected for RGBA16F passes
};

struct ComputeDesc { const char* msl=nullptr; const char* glsl=nullptr; const char* label=nullptr; };

struct VertexAttr {
    uint32_t location = 0;
    VertexFormat format = VertexFormat::Float3;
    uint32_t offset = 0;
};

struct PipelineDesc {
    ShaderHandle shader;
    VertexAttr attrs[8];
    int attrCount = 0;
    uint32_t stride = 0;
    Primitive primitive = Primitive::Triangles;
    IndexType indexType = IndexType::UInt16;
    CullMode cull = CullMode::Back;
    BlendMode blend = BlendMode::Opaque;
    bool depthTest = true;
    bool depthWrite = true;
    bool colorWrite = true;          // false: depth only into a pass with a colour target (colour mask off)
    bool depthOnly = false;          // no color output (shadow maps); the pass must have no color target.
                                     // A fragment stage, when the shader has one, still runs (alpha-tested casters).
    float depthBiasConstant = 0;     // polygon offset, used by shadow casters
    float depthBiasSlope = 0;
    PixelFormat colorFormat = PixelFormat::RGBA8;   // colour target it draws into: RGBA8 (screen, RGBA8 targets) or RGBA8_sRGB
                                                    // RGBA16F passes select a cached linear-target variant on both backends
    const char* label = nullptr;
};

// Default: render to the screen. With targets: render into textures (both must match in size).
struct PassDesc {
    TextureHandle colorTarget;       // invalid = screen (or none when depthTarget is set)
    TextureHandle depthTarget;       // invalid = screen depth
    float clearColor[4] = {0.1f, 0.1f, 0.12f, 1.0f};
    bool clearColorEnabled = true;
    bool clearDepthEnabled = true;
    float clearDepth = 1.0f;
};

struct DeviceInfo {
    Backend backend = Backend::GLES3;
    bool clipDepthZeroToOne = false;
    bool renderTargetFlipY = false;  // texture v of a render target grows downward (Metal)
    std::string name;
};

struct Rect { int x = 0, y = 0, w = 0, h = 0; };

class Device {
public:
    virtual ~Device() = default;
    virtual const DeviceInfo& info() const = 0;

    // Resources
    virtual BufferHandle createBuffer(const BufferDesc& d) = 0;
    virtual void updateBuffer(BufferHandle h, const void* data, size_t size, size_t offset = 0) = 0; // grows Dynamic buffers if needed
    virtual void destroyBuffer(BufferHandle h) = 0;
    virtual TextureHandle createTexture(const TextureDesc& d) = 0;
    virtual void updateTexture(TextureHandle h, int x, int y, int w, int h2, const void* pixels, int rowPitchBytes) = 0;
    virtual void destroyTexture(TextureHandle h) = 0;
    virtual ShaderHandle createShader(const ShaderDesc& d) = 0;   // returns invalid handle on failure (see lastError())
    // Start on the device thread, then poll on that same thread. Jobs must not outlive
    // the device. Dropping a job cancels publication; backend callbacks own their data.
    using ShaderJob = std::function<bool(ShaderHandle&)>;
    using PipelineJob = std::function<bool(PipelineHandle&)>;
    virtual ShaderJob createShaderAsync(const ShaderDesc& d) {
        const auto h = createShader(d); return [h](ShaderHandle& out) { out=h; return true; };
    }
    virtual PipelineJob createPipelineAsync(const PipelineDesc& d) {
        const auto h = createPipeline(d); return [h](PipelineHandle& out) { out=h; return true; };
    }
    virtual void destroyShader(ShaderHandle h) = 0;
    virtual PipelineHandle createPipeline(const PipelineDesc& d) = 0;
    // Poll preparation of a backend-selected colour-target variant. Device thread only.
    virtual bool warmPipelineColor(PipelineHandle, PixelFormat) { return true; }
    virtual void destroyPipeline(PipelineHandle h) = 0;
    virtual const std::string& lastError() const = 0;
    // Formats the GPU samples natively (block-compressed ones vary by GPU and API).
    virtual bool supports(PixelFormat f) const { return f == PixelFormat::RGBA8 || f == PixelFormat::R8 || f == PixelFormat::Depth || f == PixelFormat::RGBA32F; }
    // Copies the colour of a render target (invalid = the screen as drawn so far this frame) into `to`: an RGBA8
    // render target of the same size. Only between passes. False when the backend can't (the screen is not
    // readable: see setScreenReadable, sizes differ); the caller then takes another path.
    virtual bool copyColor(TextureHandle from, TextureHandle to) { (void)from; (void)to; return false; }
    // The screen may be a copyColor source (Metal: drawables stop being framebuffer-only from the next frame).
    virtual void setScreenReadable(bool readable) { (void)readable; }

    // Opt-in diagnostic samples. Call after beginFrame and before endFrame; poll later without stalling.
    // GPU timeline interval, not busy-time: GL may include idle gaps waiting for CPU submission.
    // Unsupported backends return false.
    struct GpuTiming { uint64_t tag = 0; double milliseconds = 0; bool valid = false; };
    virtual bool timeGpuFrame(uint64_t tag) { (void)tag; return false; }
    virtual std::vector<GpuTiming> takeGpuTimings() { return {}; }
    // Optional compute. Buffer slots 0..7/9; uniform bytes at slot 8 (<=4 KB).
    // Storage buffers persist across frames; updateBuffer uploads in queue order.
    // Commands are between beginFrame/endFrame and outside render passes.
    virtual bool supportsCompute() const { return false; }
    virtual ComputeHandle createCompute(const ComputeDesc&) { return {}; }
    virtual void destroyCompute(ComputeHandle) {}
    virtual bool beginCompute(ComputeHandle) { return false; }
    virtual void setComputeBuffer(int,BufferHandle) {}
    virtual void setComputeTexture(int,TextureHandle) {}
    virtual void setComputeUniforms(const void*,size_t) {}
    // 64 threads per X group; dispatch provides total logical threads. Bounds-check in shader.
    virtual void dispatch(uint32_t,uint32_t=1) {}
    virtual void endCompute() {}
    // Debug/readback only, between frames. Never used in normal simulation/rendering.
    virtual bool readBuffer(BufferHandle,void*,size_t) { return false; }
    // Small asynchronous status readback, inside a frame and outside encoders. Callback on
    // owner thread at a later beginFrame; empty data on GPU error. Unsupported returns false.
    using BufferReadFn=std::function<void(std::vector<uint8_t>)>;
    virtual bool readBufferAsync(BufferHandle,size_t,BufferReadFn) { return false; }
    virtual bool gpuHealthy() const { return true; }
    virtual double gpuFrameMs() const { return 0; } // last completed frame; asynchronous

    // Frame (driven by the platform layer)
    virtual void beginFrame(int framebufferWidth, int framebufferHeight) = 0;
    virtual void endFrame() = 0; // presents
    // Tools (editor screenshots, MCP): deliver the next presented frame as top-down RGBA8.
    // The callback may run one or two frames later; it always runs on the main thread.
    using CaptureFn = std::function<void(int w, int h, std::vector<uint8_t> rgba)>;
    virtual void captureNextFrame(CaptureFn fn) { if (fn) fn(0, 0, {}); }
    int framebufferWidth() const { return fbW_; }
    int framebufferHeight() const { return fbH_; }

    // Commands (only valid between beginPass/endPass)
    virtual void beginPass(const PassDesc& d) = 0;
    virtual void endPass() = 0;
    virtual void setViewport(const Rect& r) = 0;
    virtual void setScissor(const Rect& r) = 0;   // w==0 disables
    virtual void setPipeline(PipelineHandle p) = 0;
    virtual void setVertexBuffer(BufferHandle b, size_t offset = 0) = 0;
    virtual void setIndexBuffer(BufferHandle b, size_t offset = 0) = 0;
    virtual void setTexture(int slot, TextureHandle t) = 0;
    // Larger blocks require explicit backend capability; portable minimum remains 4 KB.
    virtual size_t maxUniformBytes() const { return 4096; }
    virtual void setBindingCacheEnabled(bool enabled) { (void)enabled; }
    virtual void setUniforms(int slot, const void* data, size_t size) = 0;
    virtual void draw(uint32_t vertexCount, uint32_t firstVertex = 0) = 0;
    virtual void drawIndexed(uint32_t indexCount, uint32_t firstIndex = 0, int32_t baseVertex = 0, uint32_t instanceCount = 1) = 0;

protected:
    int fbW_ = 0, fbH_ = 0;
};

// Backend factories. `nativeSurface` is backend specific:
//   Metal: CAMetalLayer*   GL: nullptr (context must be current on the calling thread)
std::unique_ptr<Device> createMetalDevice(void* nativeSurface);
std::unique_ptr<Device> createGLDevice(Backend glFlavor);

} // namespace aiverse::gfx
