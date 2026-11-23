// RHI: a thin, explicit rendering interface implemented directly on top of native APIs.
//
//   Backends:  Metal        -> macOS, iOS
//              OpenGL ES 3  -> Android, Web (WebGL2), Linux (dev/CI via EGL)
//              OpenGL 3.3   -> Windows (WGL)
//              Vulkan       -> Windows, Linux, Android on request (AIVERSE_GFX / game.json "graphics"),
//                              same GL conventions and GLSL sources (vulkan/Vulkan.h, docs/vulkan.md)
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
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <limits>
#include <memory>
#include <functional>
#include <string>
#include <vector>
#include "engine/gfx/GpuTimingRouter.h"

namespace aiverse::gfx {

template <class Tag> struct Handle {
    uint32_t id = 0;
    bool valid() const { return id != 0; }
    bool operator==(const Handle&) const = default;
};
using BufferHandle = Handle<struct BufferTag>;
using TextureHandle = Handle<struct TextureTag>;
using TextureViewHandle = Handle<struct TextureViewTag>;
using SamplerHandle = Handle<struct SamplerTag>;
using ShaderHandle = Handle<struct ShaderTag>;
using ComputeHandle = Handle<struct ComputeTag>;
using PipelineHandle = Handle<struct PipelineTag>;

enum class Backend { Metal, GLES3, GL33, Vulkan };
// Pending != failed: a renderer must never infer GPU fence failure
// from normal in-flight work or elapsed frame counters.
enum class ResourceRetirementStatus : uint8_t {
    Pending, Complete, Failed
};
enum class BufferType { Vertex, Index, Storage, Indirect };
enum class Usage { Immutable, Dynamic };          // Dynamic: updated (whole or partially) every frame
enum class PixelFormat { RGBA8, R8, Depth, RGBA32F,
                         ETC2_RGB8, ETC2_RGBA8, BC1, BC3, BC7, ASTC_4x4, ASTC_6x6,   // block-compressed: upload levels, check supports()   // Depth: 24/32-bit depth render target; RGBA32F: data textures
                                                        // (bone matrices, morph deltas), nearest only, read with texelFetch / read()
                         RGBA8_sRGB, // sampling decodes sRGB; render targets encode on write and blend in linear; alpha stays linear
                         RGBA16F, // linear HDR radiance, 8 bytes/pixel; check supports()
                         BC4, BC5, EAC_R11, // one/two-channel compressed data maps; check supports()
                         RG8, R16F, RG16F, R32F, RG32F,
                         BC6H_UFloat, BC6H_SFloat,
                         BC1_sRGB, BC3_sRGB, BC7_sRGB,
                         ETC2_RGB8_sRGB, ETC2_RGBA8_sRGB,
                         ASTC_4x4_sRGB, ASTC_6x6_sRGB }; // HDR GPU block compression // single/dual-channel sampled data textures
// Shared compression metadata used by all texture backends. A requested
// compressed format must NEVER be allocated as RGBA8 when unsupported.
constexpr bool isCompressedTextureFormat(PixelFormat f) {
    switch (f) {
    case PixelFormat::ETC2_RGB8: case PixelFormat::ETC2_RGBA8:
    case PixelFormat::EAC_R11: case PixelFormat::BC1:
    case PixelFormat::BC3: case PixelFormat::BC4:
    case PixelFormat::BC5: case PixelFormat::BC7:
    case PixelFormat::BC6H_UFloat: case PixelFormat::BC6H_SFloat:
    case PixelFormat::BC1_sRGB: case PixelFormat::BC3_sRGB: case PixelFormat::BC7_sRGB:
    case PixelFormat::ETC2_RGB8_sRGB: case PixelFormat::ETC2_RGBA8_sRGB:
    case PixelFormat::ASTC_4x4_sRGB: case PixelFormat::ASTC_6x6_sRGB:
    case PixelFormat::ASTC_4x4: case PixelFormat::ASTC_6x6:
        return true;
    default:
        return false;
    }
}
constexpr int compressedTextureBlockBytes(PixelFormat f) {
    if (!isCompressedTextureFormat(f)) return 0;
    return f == PixelFormat::ETC2_RGB8 || f == PixelFormat::EAC_R11 ||
           f == PixelFormat::BC1 || f == PixelFormat::BC1_sRGB ||
           f == PixelFormat::ETC2_RGB8_sRGB ||
           f == PixelFormat::BC4 ? 8 : 16;
}
constexpr int compressedTextureBlockSize(PixelFormat f) {
    return !isCompressedTextureFormat(f) ? 0 :
           f == PixelFormat::ASTC_6x6 || f == PixelFormat::ASTC_6x6_sRGB ? 6 : 4;
}
constexpr bool isExtendedDataTextureFormat(PixelFormat f) {
    return f == PixelFormat::RG8 || f == PixelFormat::R16F ||
           f == PixelFormat::RG16F || f == PixelFormat::R32F ||
           f == PixelFormat::RG32F;
}
constexpr int uncompressedTexturePixelBytes(PixelFormat f) {
    switch (f) {
    case PixelFormat::R8: return 1;
    case PixelFormat::RG8: case PixelFormat::R16F: return 2;
    case PixelFormat::RG16F: case PixelFormat::R32F: return 4;
    case PixelFormat::RG32F: case PixelFormat::RGBA16F: return 8;
    case PixelFormat::RGBA32F: return 16;
    case PixelFormat::RGBA8: case PixelFormat::RGBA8_sRGB: return 4;
    default: return 0; // depth/compressed formats are not CPU-interleaved pixel data
    }
}
enum class Filter { Nearest, Linear };
// Resource dimension is separate from the sampled view dimension. Array layers are physical
// slices: Cube requires six, CubeArray requires a multiple of six.
enum class TextureDimension : uint8_t { Texture1D, Texture2D, Texture3D, Texture2DArray, Cube, CubeArray };
enum class SamplerAddressMode : uint8_t { ClampToEdge, Repeat, MirrorRepeat, ClampToBorder };
enum class SamplerMipFilter : uint8_t { None, Nearest, Linear };
enum class CompareFunction : uint8_t { Never, Less, LessEqual, Equal, Greater, GreaterEqual, NotEqual, Always };
enum class TextureAspect : uint8_t { Color, Depth, Stencil };
enum class VertexFormat { Float2, Float3, Float4, UByte4N, UByte4 }; // UByte4: integers 0..255 (GLSL float / MSL uchar4)
enum class IndexType { None, UInt16, UInt32 };
enum class Primitive { Triangles, Lines };
enum class CullMode { None, Back, Front };
enum class BlendMode { Opaque, Alpha, Additive, Premultiplied, Multiply, Subtract, Screen };
// Premultiplied: ONE, 1-srcAlpha. Multiply: dst * src colour. Subtract: dst - src * srcAlpha (reverse subtract). (particles)
// Screen: src + dst * (1 - src colour) for premultiplied colour, alpha as Premultiplied (Spine "screen" slots)

// Cross-backend layout of indexed indirect commands (Vulkan/Metal compatible).
// All commands in one submission share vertex/index buffers, pipeline, resources
// and uniforms. Material state or transform changes require a separate group.
struct DrawIndexedIndirectCommand {
    uint32_t indexCount = 0, instanceCount = 1, firstIndex = 0;
    int32_t baseVertex = 0;
    uint32_t firstInstance = 0;
};
static_assert(sizeof(DrawIndexedIndirectCommand) == 20);

// Experimental *per-command* VT fragment resources for Metal GPU ICB.
// The pipeline and non-VT PBR resources remain shared per command group.
// This is a descriptor only; regular backends reject the opt-in path.
// Both physical atlas and page table must be 2D-array textures.
// params = (virtualLayer, gutterPixels, addressU, addressV).
struct GpuIcbVirtualMaterial {
    TextureHandle physicalAtlas, pageTable, alphaCoverage;
    float params[4] = {};
    bool hasVirtualTexture = true; // false only for per-command opaque PBR
    // Optional experimental per-command material U2 and fragment source
    // textures 1..6. All six must be bound (fallback 1x1 textures are valid).
    // The nine float4 rows match the existing std140/Metal MU2 layout.
    bool hasPbr = false;
    std::array<TextureHandle,6> pbrTextures{};
    std::array<float,36> pbrU2{};
    // Fragment-only Shader Graph source slots 13..16. Slot 16 uses
    // a constexpr Metal sampler because dynamic sampler slots stop at 15.
    uint32_t graphTextureCount = 0;
    std::array<TextureHandle,4> graphTextures{};
    bool valid() const {
        if (graphTextureCount>4 || (graphTextureCount && !hasPbr))
            return false;
        for(uint32_t i=0;i<graphTextureCount;++i)
            if(!graphTextures[i].valid()) return false;
        for(uint32_t i=graphTextureCount;i<4;++i)
            if(graphTextures[i].valid()) return false;
        if (hasPbr) {
            for (const auto texture : pbrTextures)
                if (!texture.valid()) return false;
            for (const float value : pbrU2)
                if (!std::isfinite(value)) return false;
        } else {
            for (const auto texture : pbrTextures)
                if (texture.valid()) return false;
            for (const float value : pbrU2)
                if (value != 0.0f) return false;
        }
        if (!hasVirtualTexture) {
            if (!hasPbr || physicalAtlas.valid() || pageTable.valid() ||
                alphaCoverage.valid()) return false;
            for (float x : params) if (x != 0.0f) return false;
            return true;
        }
        return physicalAtlas.valid() && pageTable.valid() &&
               std::isfinite(params[0]) && params[0] >= 0 &&
               params[0] < 16 && std::floor(params[0]) == params[0] &&
               std::isfinite(params[1]) && params[1] >= 0 &&
               std::isfinite(params[2]) && params[2] >= 0 &&
               params[2] <= 2 && std::floor(params[2]) == params[2] &&
               std::isfinite(params[3]) && params[3] >= 0 &&
               params[3] <= 2 && std::floor(params[3]) == params[3];
    }
};

// Opt-in per-command PBR resources without a virtual texture.
// Slots 1..6 and U2 are fully specified; default backends refuse it.
struct GpuIcbPbrMaterial {
    std::array<TextureHandle,6> textures{};
    std::array<float,36> u2{};
    uint32_t graphTextureCount = 0;
    std::array<TextureHandle,4> graphTextures{};
    bool valid() const {
        for (auto t : textures) if (!t.valid()) return false;
        for (float x : u2) if (!std::isfinite(x)) return false;
        if (graphTextureCount>4) return false;
        for(uint32_t i=0;i<graphTextureCount;++i)
            if(!graphTextures[i].valid()) return false;
        for(uint32_t i=graphTextureCount;i<4;++i)
            if(graphTextures[i].valid()) return false;
        return true;
    }
};

struct BufferInfo {
    size_t logicalBytes = 0; // lower bound: excludes ring snapshots/driver padding
    uint32_t versions = 1;   // dynamic GPU versions may occupy more VRAM
};

struct BufferDesc {
    BufferType type = BufferType::Vertex;
    Usage usage = Usage::Immutable;
    size_t size = 0;
    const void* data = nullptr;
    const char* label = nullptr;
    bool snapshotUpdates = false; // Dynamic: preserve earlier updates encoded in this frame
    // Opt-in for dynamic buffers whose next offset-zero update replaces the
    // entire *readable* payload. Old tail bytes need not be copied to a new
    // GPU-safe version. Clients must only read the freshly written prefix.
    // This does NOT relax snapshotUpdates or in-flight GPU lifetime rules.
    bool discardOnUpdate = false;
};

// Read-only diagnostics for one dynamic indexed-indirect buffer, accumulated
// since its creation. These are actual CPU memcpy/memcmp operations observed
// in the backend; not GPU execution, bus transfers or driver allocations.
// Backends without this instrumentation return available=false.
struct BufferUpdateStats {
    bool available = false;
    uint64_t updateCalls = 0;
    uint64_t requestedBytes = 0;
    uint64_t mappedWriteBytes = 0;  // CPU memcpy into mapped GPU-visible memory
    uint64_t cpuShadowWriteBytes = 0; // separate CPU-side shadow memcpy
    uint64_t comparedBytes = 0;     // per memcmp length (one input span)
    uint64_t skippedWriteBytes = 0; // verified equal spans not copied
    uint64_t preservedBytes = 0;    // old GPU-visible data copied to new version
    uint64_t allocations = 0;       // buffer versions/snapshots incl. initial
    uint64_t reused = 0;            // existing safe snapshots/versions reused
    size_t retainedBufferBytes = 0; // sum of live allocation logical capacities
    size_t retainedCpuShadowBytes = 0;
};

// Compute only monotonic lifetime-counter deltas. Retained capacities are
// point-in-time gauges, not additive and deliberately remain zero here.
// An unsupported backend, destroyed handle or counter reset is unavailable.
inline BufferUpdateStats bufferUpdateCounterDelta(
    const BufferUpdateStats& before, const BufferUpdateStats& after) {
    if (!after.available) return {};
    const BufferUpdateStats empty{};
    const BufferUpdateStats& previous = before.available ? before : empty;
    if (after.updateCalls < previous.updateCalls ||
        after.requestedBytes < previous.requestedBytes ||
        after.mappedWriteBytes < previous.mappedWriteBytes ||
        after.cpuShadowWriteBytes < previous.cpuShadowWriteBytes ||
        after.comparedBytes < previous.comparedBytes ||
        after.skippedWriteBytes < previous.skippedWriteBytes ||
        after.preservedBytes < previous.preservedBytes ||
        after.allocations < previous.allocations ||
        after.reused < previous.reused) return {};
    BufferUpdateStats delta{};
    delta.available = true;
    delta.updateCalls = after.updateCalls - previous.updateCalls;
    delta.requestedBytes = after.requestedBytes - previous.requestedBytes;
    delta.mappedWriteBytes = after.mappedWriteBytes - previous.mappedWriteBytes;
    delta.cpuShadowWriteBytes = after.cpuShadowWriteBytes - previous.cpuShadowWriteBytes;
    delta.comparedBytes = after.comparedBytes - previous.comparedBytes;
    delta.skippedWriteBytes = after.skippedWriteBytes - previous.skippedWriteBytes;
    delta.preservedBytes = after.preservedBytes - previous.preservedBytes;
    delta.allocations = after.allocations - previous.allocations;
    delta.reused = after.reused - previous.reused;
    return delta;
}

// Sum deltas from repeated renderModels invocations for a single beginScene.
// Do not sum retainedBufferBytes/retainedCpuShadowBytes or reinterpret the
// counters as asynchronous GPU measurements.
inline void accumulateBufferUpdateCounters(BufferUpdateStats& total,
                                           const BufferUpdateStats& delta) {
    if (!delta.available) return;
    total.available = true;
    total.updateCalls += delta.updateCalls;
    total.requestedBytes += delta.requestedBytes;
    total.mappedWriteBytes += delta.mappedWriteBytes;
    total.cpuShadowWriteBytes += delta.cpuShadowWriteBytes;
    total.comparedBytes += delta.comparedBytes;
    total.skippedWriteBytes += delta.skippedWriteBytes;
    total.preservedBytes += delta.preservedBytes;
    total.allocations += delta.allocations;
    total.reused += delta.reused;
}

struct TextureInfo {
    int width = 0, height = 0;
    PixelFormat format = PixelFormat::RGBA8;
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
    // Extended descriptor contract. Defaults preserve all existing Texture2D callers.
    // Backends MUST reject unsupported dimensions/features instead of silently making Texture2D.
    TextureDimension dimension = TextureDimension::Texture2D;
    int depth = 1;          // texel depth of Texture3D; 1 otherwise
    int arrayLayers = 1;    // 2DArray slices, or faces (6 for Cube, 6*N for CubeArray)
    int mipLevels = 0;      // 0 = legacy automatic policy; >0 = explicit allocation request
    int sampleCount = 1;    // multisampled textures require renderTarget and no initial data
};


struct TextureViewDesc {
    TextureDimension dimension = TextureDimension::Texture2D;
    TextureAspect aspect = TextureAspect::Color;
    int baseMipLevel = 0, mipLevelCount = 1;
    int baseArrayLayer = 0, arrayLayerCount = 1; // cube views: multiples of six
};

struct SamplerDesc {
    Filter minFilter = Filter::Linear, magFilter = Filter::Linear;
    SamplerMipFilter mipFilter = SamplerMipFilter::Linear;
    SamplerAddressMode addressU = SamplerAddressMode::ClampToEdge;
    SamplerAddressMode addressV = SamplerAddressMode::ClampToEdge;
    SamplerAddressMode addressW = SamplerAddressMode::ClampToEdge;
    float minLod = 0.0f, maxLod = 1000.0f, lodBias = 0.0f, maxAnisotropy = 1.0f;
    bool compare = false;
    CompareFunction comparison = CompareFunction::LessEqual;
};

struct TextureUploadDesc {
    int mipLevel = 0, arrayLayer = 0;
    int x = 0, y = 0, z = 0;
    int width = 0, height = 1, depth = 1;
    int rowPitchBytes = 0, slicePitchBytes = 0;
    const void* data = nullptr;
};

// Request a single 2D mip/face/layer from an existing sampled or render-target
// texture. Readback is asynchronous only where implemented by the backend;
// unsupported backends return false and never invoke the callback.
struct TextureReadbackDesc {
    int mipLevel = 0;
    int arrayLayer = 0;
};
struct TextureReadbackResult {
    int width = 0, height = 0;
    PixelFormat format = PixelFormat::RGBA8;
    // Tightly packed rows in output format, row 0 = top of the image on
    // every backend: uploaded textures return rows in upload order, render
    // targets return the rendered top row first (GL/Vulkan flip their
    // bottom-up render targets; Metal render targets are already top-down).
    std::vector<uint8_t> pixels;
    bool success() const { return width > 0 && height > 0 && !pixels.empty(); }
};

// CPU-only validation of dimension/layer/mip invariants. It does NOT imply GPU support.
enum class TextureDescError : uint8_t {
    None, InvalidExtent, InvalidLayers, InvalidMipCount, InvalidSampleCount,
    InvalidDimension, InvalidUsage
};
constexpr int maxTextureMipLevels(int w, int h, int d = 1) {
    int n = 0;
    for (int s = w > h ? (w > d ? w : d) : (h > d ? h : d); s > 0; s >>= 1) ++n;
    return n;
}
constexpr TextureDescError validateTextureDesc(const TextureDesc& d) {
    if (d.width < 1 || d.height < 1 || d.depth < 1)
        return TextureDescError::InvalidExtent;
    if (d.arrayLayers < 1) return TextureDescError::InvalidLayers;
    switch (d.dimension) {
    case TextureDimension::Texture1D:
        if (d.height != 1 || d.depth != 1 || d.arrayLayers != 1) return TextureDescError::InvalidDimension;
        break;
    case TextureDimension::Texture2D:
        if (d.depth != 1 || d.arrayLayers != 1) return TextureDescError::InvalidDimension;
        break;
    case TextureDimension::Texture3D:
        if (d.arrayLayers != 1) return TextureDescError::InvalidDimension;
        break;
    case TextureDimension::Texture2DArray:
        if (d.depth != 1) return TextureDescError::InvalidDimension;
        break;
    case TextureDimension::Cube:
        if (d.width != d.height || d.depth != 1 || d.arrayLayers != 6) return TextureDescError::InvalidDimension;
        break;
    case TextureDimension::CubeArray:
        if (d.width != d.height || d.depth != 1 || d.arrayLayers % 6 != 0) return TextureDescError::InvalidDimension;
        break;
    }
    if (d.mipLevels < 0 || d.levelCount < 0 || d.mipLevels > maxTextureMipLevels(d.width, d.height, d.depth) ||
        d.levelCount > maxTextureMipLevels(d.width, d.height, d.depth) ||
        (d.mipLevels > 0 && d.levelCount > 0 && d.mipLevels != d.levelCount))
        return TextureDescError::InvalidMipCount;
    if (d.sampleCount != 1 && d.sampleCount != 2 && d.sampleCount != 4 &&
        d.sampleCount != 8 && d.sampleCount != 16)
        return TextureDescError::InvalidSampleCount;
    const bool blockCompressed = isCompressedTextureFormat(d.format);
    if (d.sampleCount > 1 && (d.dimension != TextureDimension::Texture2D ||
        !d.renderTarget || d.data || d.mipmaps || d.mipLevels > 1 ||
        d.levelCount > 0 || d.storage || d.compare || blockCompressed))
        return TextureDescError::InvalidUsage;
    if (d.compare && d.format != PixelFormat::Depth) return TextureDescError::InvalidUsage;
    if (d.storage && d.format == PixelFormat::Depth) return TextureDescError::InvalidUsage;
    // Until each backend has a validated float/RG mip generator, use explicit
    // mipLevels + updateTextureRegion. Do not silently allocate one mip.
    if (isExtendedDataTextureFormat(d.format) && d.mipmaps)
        return TextureDescError::InvalidUsage;
    if (blockCompressed) {
        // A block-compressed GPU image requires a complete mip chain of
        // levelData/levelSize entries; random data and render/storage targets
        // must not be misinterpreted as compressed texture payloads.
        if (d.levelCount < 1 || !d.levelData || !d.levelSize || d.data ||
            d.mipmaps || d.renderTarget || d.storage || d.sampleCount != 1)
            return TextureDescError::InvalidUsage;
        const size_t byteCount = size_t(compressedTextureBlockBytes(d.format));
        const size_t block = size_t(compressedTextureBlockSize(d.format));
        if (!byteCount || !block) return TextureDescError::InvalidUsage;
        for (int mip = 0; mip < d.levelCount; ++mip) {
            const size_t w = size_t((d.width >> mip) > 0 ? (d.width >> mip) : 1);
            const size_t h = size_t((d.height >> mip) > 0 ? (d.height >> mip) : 1);
            const size_t z = d.dimension == TextureDimension::Texture3D ?
                size_t((d.depth >> mip) > 0 ? (d.depth >> mip) : 1) : 1;
            const size_t nx = (w + block - 1) / block, ny = (h + block - 1) / block;
            size_t need = byteCount;
            const size_t factors[4] = {nx, ny, z, size_t(d.arrayLayers)};
            for (size_t f : factors) {
                if (need > std::numeric_limits<size_t>::max() / f)
                    return TextureDescError::InvalidUsage;
                need *= f;
            }
            if (!d.levelData[mip] || d.levelSize[mip] < need)
                return TextureDescError::InvalidUsage;
        }
    }
    return TextureDescError::None;
}
constexpr const char* textureDescErrorString(TextureDescError e) {
    switch (e) {
    case TextureDescError::None: return "valid";
    case TextureDescError::InvalidExtent: return "texture extent must be positive";
    case TextureDescError::InvalidLayers: return "texture arrayLayers must be positive";
    case TextureDescError::InvalidMipCount: return "texture mip count is invalid";
    case TextureDescError::InvalidSampleCount: return "texture sample count is invalid";
    case TextureDescError::InvalidDimension: return "texture dimensions/layers do not match its dimension";
    case TextureDescError::InvalidUsage: return "texture usage combinations are invalid";
    }
    return "invalid texture descriptor";
}


inline int textureEffectiveMipLevels(const TextureDesc& d) {
    if (d.levelCount > 0) return d.levelCount;
    if (d.mipLevels > 0) return d.mipLevels;
    return d.mipmaps ? maxTextureMipLevels(d.width, d.height, d.depth) : 1;
}

// Validate a view against the *requested* parent descriptor. Backends must also validate
// the actual GPU allocation (which can have fewer mips if generation is unsupported).
inline bool validateTextureViewDesc(const TextureDesc& image, const TextureViewDesc& view) {
    if (validateTextureDesc(image) != TextureDescError::None ||
        view.baseMipLevel < 0 || view.mipLevelCount <= 0 ||
        view.baseArrayLayer < 0 || view.arrayLayerCount <= 0)
        return false;
    const int mips = textureEffectiveMipLevels(image);
    if (view.baseMipLevel >= mips || view.mipLevelCount > mips - view.baseMipLevel ||
        view.baseArrayLayer >= image.arrayLayers ||
        view.arrayLayerCount > image.arrayLayers - view.baseArrayLayer)
        return false;
    if (image.format == PixelFormat::Depth) {
        if (view.aspect != TextureAspect::Depth) return false;
    } else if (view.aspect != TextureAspect::Color) return false;
    switch (image.dimension) {
    case TextureDimension::Texture1D:
    case TextureDimension::Texture2D:
    case TextureDimension::Texture3D:
    case TextureDimension::Cube:
        return view.dimension == image.dimension &&
               view.baseArrayLayer == 0 &&
               view.arrayLayerCount == image.arrayLayers;
    case TextureDimension::Texture2DArray:
        return (view.dimension == TextureDimension::Texture2DArray) ||
               (view.dimension == TextureDimension::Texture2D &&
                view.arrayLayerCount == 1);
    case TextureDimension::CubeArray:
        return (view.dimension == TextureDimension::CubeArray &&
                view.baseArrayLayer % 6 == 0 && view.arrayLayerCount % 6 == 0) ||
               (view.dimension == TextureDimension::Cube &&
                view.baseArrayLayer % 6 == 0 && view.arrayLayerCount == 6);
    }
    return false;
}

inline bool validateSamplerDesc(const SamplerDesc& s) {
    return std::isfinite(s.minLod) && std::isfinite(s.maxLod) &&
           std::isfinite(s.lodBias) && std::isfinite(s.maxAnisotropy) &&
           s.minLod >= 0.0f && s.maxLod >= s.minLod &&
           s.maxAnisotropy >= 1.0f;
}


struct TextureResourceInfo {
    TextureHandle handle;
    TextureDimension dimension = TextureDimension::Texture2D;
    PixelFormat format = PixelFormat::RGBA8;
    int width = 0, height = 0, depth = 1;
    int arrayLayers = 1, mipLevels = 1, sampleCount = 1;
    bool renderTarget = false, storage = false;
    size_t estimatedBytes = 0; // format-based estimate, not a driver memory query
};

// Conservative GPU storage estimate for editor inspection and budget planning.
// Assumes tightly packed logical storage; real drivers may pad allocations.
inline size_t estimateTextureFootprint(const TextureDesc& d) {
    if (d.width < 1 || d.height < 1 || d.depth < 1 ||
        d.arrayLayers < 1 || d.sampleCount < 1) return 0;
    const int mips = textureEffectiveMipLevels(d);
    if (mips < 1 || mips > maxTextureMipLevels(d.width,d.height,d.depth) ||
        (d.sampleCount > 1 && mips != 1)) return 0;
    const bool compressed = isCompressedTextureFormat(d.format);
    const size_t bpp = compressed ?
        size_t(compressedTextureBlockBytes(d.format)) :
        size_t(d.format == PixelFormat::Depth ? 4 :
               uncompressedTexturePixelBytes(d.format));
    const size_t block = compressed ?
        size_t(compressedTextureBlockSize(d.format)) : 1;
    if (!bpp || !block) return 0;
    size_t total = 0;
    for (int mip = 0; mip < mips; ++mip) {
        const size_t w = size_t(std::max(1,d.width >> mip));
        const size_t h = size_t(std::max(1,d.height >> mip));
        const size_t z = d.dimension == TextureDimension::Texture3D ?
                         size_t(std::max(1,d.depth >> mip)) : 1;
        const size_t factors[4] = {
            (w + block - 1) / block, (h + block - 1) / block,
            z, size_t(d.arrayLayers)
        };
        size_t bytes = bpp;
        for (size_t factor : factors) {
            if (bytes > std::numeric_limits<size_t>::max() / factor) return 0;
            bytes *= factor;
        }
        if (bytes > std::numeric_limits<size_t>::max() /
                         size_t(d.sampleCount)) return 0;
        bytes *= size_t(d.sampleCount);
        if (total > std::numeric_limits<size_t>::max() - bytes) return 0;
        total += bytes;
    }
    return total;
}

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

// Vulkan shader resource ABI. Other backends keep their native slot contracts.
// TextureResources: storage images 0..3, sampled images 4..11, buffers 12..15,
// uniform buffer 16. BufferPushConstants: buffers 0..7, <=128 push bytes.
enum class VulkanComputeLayout : uint8_t { TextureResources, BufferPushConstants };
struct ComputeDesc {
    const char* msl=nullptr;
    const char* glsl=nullptr;
    const char* label=nullptr;
    VulkanComputeLayout vulkanLayout=VulkanComputeLayout::TextureResources;
};

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
    int sampleCount = 1; // 1 legacy; MSAA pipelines must explicitly match render-pass samples
    // Color slot 0 uses colorFormat above; additional slots are opt-in.
    // Fragment shaders must write explicit outputs 0..colorAttachmentCount-1.
    int colorAttachmentCount = 1;
    PixelFormat additionalColorFormats[3] = {
        PixelFormat::RGBA8, PixelFormat::RGBA8, PixelFormat::RGBA8
    };
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


constexpr int kMaxRenderTargetColors = 4;

// The new pass API is explicit and offscreen-only. It never implicitly binds
// the swapchain: use the existing PassDesc for screen/legacy single-target paths.
// Each attachment selects a single mip and layer/face. All active attachments
// must have identical mip-sized extents and sample counts.
// A Texture3D attachment uses arrayLayer as a depth plane at the selected mip.
struct RenderTargetAttachment {
    TextureHandle texture;
    int mipLevel = 0;
    int arrayLayer = 0;
    bool clear = true;
    float clearColor[4] = {0, 0, 0, 0}; // color attachments only
    // Optional resolve destination: must be single-sampled with identical
    // pixel format and mip-sized extent. Source must be multisampled.
    TextureHandle resolveTexture;
    int resolveMipLevel = 0;
    int resolveArrayLayer = 0;
};
struct RenderTargetPassDesc {
    std::array<RenderTargetAttachment, kMaxRenderTargetColors> colors{};
    int colorCount = 0;
    RenderTargetAttachment depth; // texture invalid = no depth attachment
    float clearDepth = 1.0f;
};

// CPU-only structural validation; actual image formats, flags, extents, subresources
// and capability limits must additionally be verified by the device implementation.
inline bool validateRenderTargetPassDesc(const RenderTargetPassDesc& desc) {
    if (desc.colorCount < 0 || desc.colorCount > kMaxRenderTargetColors ||
        (desc.colorCount == 0 && !desc.depth.texture.valid()))
        return false;
    for (int i = 0; i < desc.colorCount; ++i) {
        const auto& a = desc.colors[size_t(i)];
        if (!a.texture.valid() || a.mipLevel < 0 || a.arrayLayer < 0) return false;
        if (a.resolveTexture.valid() &&
            (a.resolveTexture == a.texture || a.resolveMipLevel < 0 ||
             a.resolveArrayLayer < 0)) return false;
        for (int k = 0; k < 4; ++k)
            if (!std::isfinite(a.clearColor[k])) return false;
        for (int j = 0; j < i; ++j) {
            const auto& b = desc.colors[size_t(j)];
            if (a.texture == b.texture && a.mipLevel == b.mipLevel &&
                a.arrayLayer == b.arrayLayer) return false; // simultaneous write alias
        }
    }
    if (desc.depth.resolveTexture.valid()) return false; // depth resolve is not portable
    if (desc.depth.texture.valid()) {
        if (desc.depth.mipLevel < 0 || desc.depth.arrayLayer < 0 ||
            !std::isfinite(desc.clearDepth) || desc.clearDepth < 0.0f ||
            desc.clearDepth > 1.0f) return false;
        for (int i = 0; i < desc.colorCount; ++i) {
            const auto& a = desc.colors[size_t(i)];
            if (a.texture == desc.depth.texture) return false;
        }
    }
    return true;
}

struct DeviceInfo {
    Backend backend = Backend::GLES3;
    bool clipDepthZeroToOne = false;
    bool renderTargetFlipY = false;  // texture v of a render target grows downward (Metal)
    std::string name;
};

struct Rect { int x = 0, y = 0, w = 0, h = 0; };

// Native access for vendor libraries that record into the engine's own command stream
// (NVIDIA NGX / DLSS through the PRISMA external upscaler). Opaque pointers keep the API
// headers out of this file: VkInstance, VkPhysicalDevice, VkDevice, VkCommandBuffer,
// PFN_vkGetInstanceProcAddr, VkImage, VkImageView, VkFormat. Vulkan backend only.
struct NativeVulkanContext {
    void* instance = nullptr;
    void* physicalDevice = nullptr;
    void* device = nullptr;
    void* commandBuffer = nullptr;      // current frame command buffer, outside any render pass
    void* getInstanceProcAddr = nullptr;
    uint32_t queueFamily = 0;
};
struct NativeVulkanImage {
    void* image = nullptr;
    void* view = nullptr;
    uint32_t format = 0;                // VkFormat
    int width = 0, height = 0;
    bool depth = false, storage = false;
};
// Extensions a vendor library needs; the Vulkan backend enables the available ones when it
// creates its instance / device. Register before the device is created (e.g. at startup).
struct VulkanExtensionRequests { std::vector<std::string> instance, device; };
inline VulkanExtensionRequests& vulkanExtensionRequests() { static VulkanExtensionRequests r; return r; }

class Device {
public:
    virtual ~Device() = default;
    // One shared timing collection owner per graphics Device. Components
    // subscribe instead of independently draining takeGpuTimings().
    // All router calls remain on this Device's render owner thread.
    GpuTimingRouter& gpuTimingRouter() {return gpuTimingRouter_;}
    const GpuTimingRouter& gpuTimingRouter() const {return gpuTimingRouter_;}
    virtual const DeviceInfo& info() const = 0;
    // Number of simultaneously addressable fragment texture *resources*
    // via setTexture(), not dynamic sampler state objects. Legacy GL and
    // portable Vulkan default to 16. Backends with larger binding tables
    // override this (Metal textures 0..31, samplers only 0..15).
    virtual int maxFragmentTextureSlots() const { return 16; }

    // Resources
    virtual BufferHandle createBuffer(const BufferDesc& d) = 0;
    virtual void updateBuffer(BufferHandle h, const void* data, size_t size, size_t offset = 0) = 0; // grows Dynamic buffers if needed
    // Checked shared geometry write. Metal/Vulkan report true only when the
    // complete copy was encoded/mapped; other backends retain their existing
    // updateBuffer contract. True is NOT a GPU completion fence.
    virtual bool updateSharedBufferChecked(BufferHandle h, const void* data,
                                           size_t size, size_t offset = 0) {
        if (!h.valid() || !data || !size) return false;
        updateBuffer(h, data, size, offset);
        return true;
    }
    // Optional error-reporting storage upload, used for GPU source kills.
    // Accepting means the copy was encoded in the device's current ordered
    // frame command buffer, NOT completed. Unsupported backends return false.
    virtual bool updateStorageBufferChecked(BufferHandle,const void*,size_t,size_t=0) {
        return false;
    }
    virtual void destroyBuffer(BufferHandle h) = 0;
    // Optional per-buffer diagnostics. No GPU stall/readback, and never
    // authoritatively measures GPU memory bandwidth or execution.
    virtual BufferUpdateStats bufferUpdateStats(BufferHandle) { return {}; }
    // CPU-only live RHI buffer metadata, same contract as textureInfo.
    // No GPU readback or synchronization; false if unsupported/stale.
    virtual bool bufferInfo(BufferHandle h, BufferInfo& out) const {
        (void)h; (void)out; return false;
    }
    // Safe point for shared geometry writes (Metal cannot blit into storage during an encoder).
    virtual bool canUpdateSharedBuffers() const { return true; }
    virtual TextureHandle createTexture(const TextureDesc& d) = 0;
    virtual void updateTexture(TextureHandle h, int x, int y, int w, int h2, const void* pixels, int rowPitchBytes) = 0;
    virtual void destroyTexture(TextureHandle h) = 0;
    // New resource APIs are opt-in. A backend returning an invalid view/sampler or false
    // MUST NOT create a reduced 2D resource in its place.
    virtual bool supportsTextureDimension(TextureDimension dim) const { return dim == TextureDimension::Texture2D; }
    virtual bool supportsTextureViews() const { return false; }
    // Read/write images are optional. A backend must reject unsupported
    // format/dimension combinations rather than silently allocating an
    // ordinary sampled-only texture.
    virtual bool supportsStorageTexture(PixelFormat, TextureDimension) const { return false; }
    virtual TextureViewHandle createTextureView(TextureHandle, const TextureViewDesc&) { return {}; }
    virtual void destroyTextureView(TextureViewHandle) {}
    virtual SamplerHandle createSampler(const SamplerDesc&) { return {}; }
    virtual void destroySampler(SamplerHandle) {}
    virtual bool updateTextureRegion(TextureHandle, const TextureUploadDesc&) { return false; }
    virtual bool bindTextureView(int, TextureViewHandle, SamplerHandle = {}) { return false; }
    // CPU-only RHI metadata lookup; no readback, synchronization or texture
    // sampling. Unsupported providers return false and leave out unchanged.
    virtual bool textureInfo(TextureHandle h, TextureInfo& out) const {
        (void)h; (void)out; return false;
    }
    // Vulkan only (others return false). Context: only while a frame records and no render
    // pass is open. Sampled images rest in VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL; a library
    // that needs another layout transitions with nativeVulkanImageBarrier and restores it.
    virtual bool nativeVulkanContext(NativeVulkanContext& out) { (void)out; return false; }
    virtual bool nativeVulkanImage(TextureHandle h, NativeVulkanImage& out) const { (void)h; (void)out; return false; }
    virtual bool nativeVulkanImageBarrier(TextureHandle h, uint32_t fromLayout, uint32_t toLayout) {
        (void)h; (void)fromLayout; (void)toLayout; return false;
    }
    // GLSL defines put after #version / precision of every GL shader compiled from now on (GL backend only;
    // others ignore it). Set before the renderers create their pipelines.
    virtual void setGlslPrelude(const std::string& defines) { (void)defines; }
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
    // Legacy bool means "no longer pending", not "compiled successfully".
    // Implementations may override the status API to distinguish permanent
    // compilation failures from asynchronous Pending. Default preserves the
    // behavior of older backends and third-party gfx::Device implementations.
    enum class PipelineColorWarmStatus : uint8_t { Ready, Pending, Failed };
    virtual bool warmPipelineColor(PipelineHandle, PixelFormat) { return true; }
    virtual PipelineColorWarmStatus warmPipelineColorStatus(PipelineHandle h, PixelFormat format) {
        return warmPipelineColor(h, format) ? PipelineColorWarmStatus::Ready
                                            : PipelineColorWarmStatus::Pending;
    }
    virtual void destroyPipeline(PipelineHandle h) = 0;
    virtual const std::string& lastError() const = 0;
    // Formats the GPU samples natively (block-compressed ones vary by GPU and API).
    virtual bool supports(PixelFormat f) const { return f == PixelFormat::RGBA8 || f == PixelFormat::R8 || f == PixelFormat::Depth || f == PixelFormat::RGBA32F; }
    // Optional inspection snapshot for the editor. Enumerates user-created
    // live TextureHandles only (not swapchain internals); estimate is not a
    // driver allocation query. Query on the graphics device owner thread.
    virtual std::vector<TextureResourceInfo> textureResources() const { return {}; }
    // Copies the colour of a render target (invalid = the screen as drawn so far this frame) into `to`: an RGBA8
    // render target of the same size. Only between passes. False when the backend can't (the screen is not
    // readable: see setScreenReadable, sizes differ); the caller then takes another path.
    virtual bool copyColor(TextureHandle from, TextureHandle to) { (void)from; (void)to; return false; }
    // Optional diagnostics only: synchronously read a completed RGBA8 render
    // target into top-down RGBA bytes. Must be called BETWEEN frames (never
    // during gameplay rendering); unsupported backends return false.
    // No CPU readback is involved in normal SpectraFX GPU rendering.
    virtual bool readColorRGBA8(TextureHandle target, std::vector<uint8_t>& rgba) {
        (void)target; rgba.clear(); return false;
    }
    // The screen may be a copyColor source (Metal: drawables stop being framebuffer-only from the next frame).
    virtual void setScreenReadable(bool readable) { (void)readable; }

    // Opt-in diagnostic samples. Call after beginFrame and before endFrame; poll later without stalling.
    // GPU timeline interval, not busy-time: GL may include idle gaps waiting for CPU submission.
    // Unsupported backends return false.
    struct GpuTiming { uint64_t tag = 0; double milliseconds = 0; bool valid = false; };
    virtual bool timeGpuFrame(uint64_t tag) { (void)tag; return false; }
    virtual std::vector<GpuTiming> takeGpuTimings() { return {}; }
    // Optional exact GPU timeline of a bounded portion of ONE graphics frame.
    // Begin outside a render pass, end after its last encoder/pass. These
    // samples are asynchronous and kept separate from full-frame benchmark
    // timing consumers. Unsupported backends must return false/empty, NEVER
    // return whole-command-buffer timings falsely labeled as per-view.
    // No nested scopes: overlapping queries are rejected by the backend.
    virtual bool beginGpuScope(uint64_t tag) { (void)tag; return false; }
    virtual void endGpuScope() {}
    virtual std::vector<GpuTiming> takeGpuScopeTimings() { return {}; }
    // Diagnostic, opt-in: GPU hardware counter timestamp deltas across
    // renderModels shadow/opaque spans. Deltas are raw GPU counter ticks,
    // NOT milliseconds without a validated device clock calibration.
    // No counter availability is represented by false/empty, never fake zero.
    struct ModelGpuCounter {
        uint64_t frame = 0;
        uint64_t shadowTicks = 0, opaqueTicks = 0;
        bool valid = false;
    };
    virtual bool enableModelGpuCounters(bool enabled) { (void)enabled; return false; }
    virtual bool sampleModelGpuCounter(int phase, bool start) {
        (void)phase; (void)start; return false;
    }
    virtual std::vector<ModelGpuCounter> takeModelGpuCounters() { return {}; }
    // Optional compute. Buffer slots 0..7/9; uniform bytes at slot 8 (<=4 KB).
    // Storage buffers persist across frames; updateBuffer uploads in queue order.
    // Commands are between beginFrame/endFrame and outside render passes.
    virtual bool supportsCompute() const { return false; }
    virtual ComputeHandle createCompute(const ComputeDesc&) { return {}; }
    virtual void destroyCompute(ComputeHandle) {}
    virtual bool beginCompute(ComputeHandle) { return false; }
    virtual void setComputeBuffer(int,BufferHandle) {}
    virtual void setComputeTexture(int,TextureHandle) {}
    // Explicit writable binding: false unless a compute encoder is active
    // and the texture was created with TextureDesc::storage=true.
    // setComputeTexture remains backward-compatible for sampled input images.
    virtual bool bindComputeStorageTexture(int,TextureHandle) { return false; }
    virtual void setComputeUniforms(const void*,size_t) {}
    // 64 threads per X group; dispatch provides total logical threads. Bounds-check in shader.
    virtual void dispatch(uint32_t,uint32_t=1) {}
    // Unlike the legacy void dispatch(), this reports a CPU-side encode
    // rejection (missing descriptor, invalid pipeline, unsupported limits).
    // GPU execution errors still require health/readback/fence validation.
    // Backends that implement only void dispatch retain conservative
    // device-health-based behavior.
    virtual bool dispatchChecked(uint32_t x,uint32_t y=1) {
        if (!supportsCompute() || !gpuHealthy()) return false;
        dispatch(x,y);
        return gpuHealthy();
    }
    virtual void endCompute() {}
    // True only when the latest dispatch was successfully encoded; not GPU completion.
    virtual bool computeDispatchSucceeded() const { return false; }
    // Debug/readback only, between frames. Never used in normal simulation/rendering.
    virtual bool readBuffer(BufferHandle,void*,size_t) { return false; }
    // Small asynchronous status readback, inside a frame and outside encoders. Callback on
    // owner thread at a later beginFrame; empty data on GPU error. Unsupported returns false.
    using BufferReadFn=std::function<void(std::vector<uint8_t>)>;
    virtual bool readBufferAsync(BufferHandle,size_t,BufferReadFn) { return false; }
    // Diagnostic only: asynchronous readback of an offscreen Depth32Float
    // texture AFTER the depth pass has ended. Packed row-major native float32
    // samples, exactly width*height*sizeof(float) bytes. No GPU wait; the
    // callback runs on the owning thread at a later beginFrame. Empty bytes
    // indicate GPU failure. Unsupported backends return false.
    // Resource must remain alive until the queued GPU blit completes.
    using DepthReadFn = std::function<void(int width,int height,std::vector<uint8_t> depthBytes)>;
    virtual bool readDepthTextureAsync(TextureHandle,DepthReadFn) { return false; }
    // Diagnostic-only async readback of an OFFSCREEN, non-MSAA RGBA8
    // render target. Returns tightly packed top-down RGBA8 bytes after GPU
    // completion on the device owner thread. Empty payload signals GPU error.
    // Request after the writer pass is closed. Other backends may return false;
    // do not fall back to reading the presented screen as a fake RTT result.
    using ColorReadFn=std::function<void(int width,int height,std::vector<uint8_t> rgba)>;
    virtual bool readColorTextureAsync(TextureHandle,ColorReadFn) { return false; }
    // The callback is invoked on the graphics-device owner thread at a later
    // beginFrame, after GPU completion, never from a driver completion thread.
    // An accepted request completes with data or an empty failure result.
    // Destruction of the parent texture after submission must not invalidate
    // the readback's own staging bytes. Implementations must reject MSAA and
    // unsupported dimensions/formats instead of returning altered pixels.
    using TextureReadFn = std::function<void(TextureReadbackResult)>;
    virtual bool readTextureAsync(TextureHandle, TextureReadbackDesc,
                                  TextureReadFn) { return false; }
    // Optional ranged async readback for bounded large storage snapshots.
    // Offset and size are in bytes; unsupported backends return false.
    // A completed callback gets exactly size bytes or an empty vector on GPU
    // error. Multiple accepted requests are delivered on the device thread.
    virtual bool readBufferRangeAsync(BufferHandle,size_t,size_t,BufferReadFn) {
        return false;
    }
    // Small, true asynchronous offscreen RGBA8 *linear* pixel readback.
    // x/y are top-left source pixel coordinates, unlike native GL readPixels.
    // Call on the owner thread after endPass and before endFrame. Completion
    // is delivered on a later owner beginFrame, never inside a GPU encoder.
    // Returns false on unsupported backends; callers must use CPU fallback.
    // No colour-space conversion, tone mapping or depth values in this API.
    using PixelReadFn=std::function<void(std::vector<uint8_t>)>; // 4 RGBA8 bytes; empty on failure
    virtual bool readTexturePixelAsync(TextureHandle,int,int,PixelReadFn) { return false; }
    // Optional native depth readback for real GPU depth diagnostics.
    // Invalid handle selects the current screen depth attachment; a valid
    // handle MUST be a Depth render target (never silently use screen).
    // Top-left coordinates, outside passes, same frame, async owner-thread
    // callback on a later beginFrame. 'valid' is false on GPU failure;
    // result is actual hardware depth in [0,1], not CPU-projected depth.
    // Unsupported RHI backends reject without calling the callback.
    using DepthPixelReadFn=std::function<void(bool valid,float depth01)>;
    virtual bool readTextureDepthPixelAsync(TextureHandle,int,int,
                                            DepthPixelReadFn) { return false; }
    virtual bool gpuHealthy() const { return true; }
    // Shutdown-only GPU barrier. Unsupported devices return false.
    virtual bool waitIdle() { return false; }
    virtual double gpuFrameMs() const { return 0; } // last completed frame; asynchronous
    // Zero if backend does not expose a completed-slot-safe frame serial.
    virtual uint64_t frameSerial() const { return 0; }
    // Highest fully completed queue serial. Zero means unknown/unsupported.
    // A buffer range retired at frameSerial() may be overwritten ONLY when
    // this value is nonzero and >= that retirement serial. This does not
    // imply any safety for races on a live Model or unsubmitted GPU work.
    virtual uint64_t completedFrameSerial() const { return 0; }
    // GPU-completion timeline for safe nonblocking resource retirement.
    // gpuSubmissionFence() returns a token covering commands encoded in
    // the CURRENT GPU frame, including commands still to be submitted.
    // It is valid only during a GPU frame. Zero means unsupported.
    // gpuCompletedFence() is the greatest *contiguous* GPU-completed
    // token observed by this device. It must never use a CPU frame index
    // as a proxy for completion; zero means no verified completion yet.
    virtual uint64_t gpuSubmissionFence() const { return 0; }
    virtual uint64_t gpuCompletedFence() const { return 0; }
    // Explicit memory-pressure synchronization. Call only on the graphics
    // owner thread at a PRE-RENDER frame boundary, before encoding any work
    // that could reference textures awaiting destruction. The backend waits
    // for already SUBMITTED GPU commands; it must not require current-frame
    // unsubmitted commands to finish. May block; never use every frame.
    virtual bool waitForSubmittedGpuWork() { return false; }
    // Explicit retirement barrier for old model/texture GPU allocations.
    // Call on the device owner thread BEFORE recording any draw/transfer
    // referring to retired resources in the CURRENT frame, outside passes.
    // Blocks until all previously SUBMITTED GPU commands have completed.
    // A false result MUST prevent hot-reload destruction; frameSerial()
    // alone does not prove completion. May stall; never call per draw.
    virtual bool waitForResourceIdle() { return false; }
    // Nonblocking retirement token: enqueue an ordered GPU completion marker
    // after all PREVIOUSLY SUBMITTED work. Call at a frame boundary before
    // current-frame commands can reference retired objects.
    // 0 means unsupported/failed: hosts may use waitForResourceIdle().
    virtual uint64_t beginResourceRetirement() { return 0; }
    // Poll on the device thread. False means pending OR failed; NEVER
    // destroy resources based solely on frameSerial() or elapsed time.
    virtual bool resourceRetirementComplete(uint64_t token) {
        (void)token; return false;
    }
    // Backend-specific implementations can report an actual failed
    // completion marker. The legacy default conservatively treats false
    // as Pending; old Device subclasses remain source-compatible.
    virtual ResourceRetirementStatus resourceRetirementStatus(uint64_t token) {
        return resourceRetirementComplete(token) ?
            ResourceRetirementStatus::Complete:
            ResourceRetirementStatus::Pending;
    }
    // Release a COMPLETED token after checking resourceRetirementComplete().
    // No backend may free a still-in-flight native fence here.
    virtual void endResourceRetirement(uint64_t token) { (void)token; }
    // Call ONLY after a successful waitForResourceIdle() barrier. This
    // disposes native marker bookkeeping even when its status callback
    // failed. Never use it to justify deleting an in-flight GPU resource.
    virtual void abandonRetirementTokenAfterIdle(uint64_t token) {
        (void)token;
    }

    // Frame (driven by the platform layer)
    virtual void beginFrame(int framebufferWidth, int framebufferHeight) = 0;
    virtual void endFrame() = 0; // presents
    // Tools (editor screenshots, MCP): deliver the next presented frame as top-down RGBA8.
    // The callback may run one or two frames later; it always runs on the main thread.
    using CaptureFn = std::function<void(int w, int h, std::vector<uint8_t> rgba)>;
    using TaggedCaptureFn = std::function<void(uint64_t frame,int w,int h,std::vector<uint8_t> rgba)>;
    virtual void captureNextFrameTagged(TaggedCaptureFn fn) {
        captureNextFrame([fn=std::move(fn)](int w,int h,std::vector<uint8_t> rgba){if(fn)fn(0,w,h,std::move(rgba));});
    }
    virtual void captureNextFrame(CaptureFn fn) { if (fn) fn(0, 0, {}); }
    // Optional offscreen RGBA8 readback inside a frame but outside a pass.
    virtual bool captureTexture(TextureHandle source,CaptureFn callback) {
        (void)source;(void)callback;return false;
    }
    int framebufferWidth() const { return fbW_; }
    int framebufferHeight() const { return fbH_; }

    // Commands (only valid between beginPass/endPass)
    virtual void beginPass(const PassDesc& d) = 0;
    // Offscreen multi-attachment pass with explicit mip/array layer selection.
    // Success opens a regular render pass; finish with endPass(). Failure must
    // leave the active pass unchanged and never begin a reduced/silent fallback.
    virtual bool supportsRenderTargetPasses() const { return false; }
    virtual bool beginRenderTargets(const RenderTargetPassDesc&) { return false; }
    virtual void endPass() = 0;
    // Diagnostic GPU frame-capture grouping. Other backends are harmless
    // no-ops; Metal emits labels only while an explicit trace is active.
    virtual void pushGpuDebugGroup(const char* label) { (void)label; }
    virtual void popGpuDebugGroup() {}
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
    // Canonical indexed base vertex for pooled rigid models: the vertex
    // offset is added to the fetched index (and shader-visible vertex ID).
    // Vulkan differs from its historical direct drawIndexed() convention;
    // unsupported RHI implementations preserve their usual direct path.
    virtual void drawIndexedCanonical(uint32_t indexCount, uint32_t firstIndex,
                                      int32_t baseVertex, uint32_t instanceCount) {
        drawIndexed(indexCount, firstIndex, baseVertex, instanceCount);
    }
    // One API render submission for several indexed GPU commands. Command buffer
    // must use BufferType::Indirect; count/stride are validated by backend.
    // Unsupported (ES3/WebGL2/legacy GL) returns false and callers use drawIndexed.
    // GPU-authored single indexed draw arguments. Unlike drawIndexedIndirect,
    // command bytes are produced by an earlier compute pass and MUST NOT be
    // read back or validated on the CPU. Unsupported backends return false.
    // This is intentionally one draw per call, not multi-draw batching.
    // Experimental opt-in compute-authored draw arguments. Feature queries
    // are explicit: the mere presence of BufferType::Storage or compute does
    // not guarantee safe GPU-authored indirect indexed rendering.
    virtual bool supportsGpuAuthoredIndirectDraw() const { return false; }
    virtual bool drawIndexedGpuIndirect(BufferHandle arguments, size_t offset) {
        (void)arguments; (void)offset; return false;
    }
    // Experimental Metal compute-encoded indexed ICB. Called outside render
    // passes after GPU-authored arguments have been produced. Each source slot
    // selects one GPU DrawIndexedIndirectCommand; baseInstance becomes its
    // ordinal in the accompanying parent-encoder instanced uniform block.
    // Returns zero if unsupported; nonzero token is valid this GPU frame only.
    // Force persistent buffer-backed per-draw uniforms while a GPU-encoded
    // ICB inherits parent bindings; setBytes memory is not a safe ICB input.
    virtual void setGpuIcbUniformBinding(bool enabled) { (void)enabled; }
    // Amount of host instance bytes copied into persistent GPU ICB memory
    // so far in the current GPU frame (includes all scene viewports).
    virtual size_t gpuIcbInstanceUploadBytes() const { return 0; }
    virtual uint32_t prepareGpuEncodedIndexedIcb(
        BufferHandle gpuArguments, BufferHandle vertexBuffer, BufferHandle indexBuffer,
        const uint32_t* sourceSlots, uint32_t count,
        const void* instanceData, size_t instanceBytes) {
        (void)gpuArguments; (void)vertexBuffer; (void)indexBuffer; (void)sourceSlots; (void)count;
        (void)instanceData; (void)instanceBytes;
        return 0;
    }
    // Optional GPU ICB material bindings. Unlike the legacy homogeneous
    // path, this API may assign each draw its own VT atlas/page table and
    // per-command U6. Only supported backends implement this; failed
    // submissions return zero and the caller uses the ordinary draw path.
    virtual uint32_t prepareGpuEncodedIndexedIcbWithVirtualMaterials(
        BufferHandle gpuArguments, BufferHandle vertexBuffer,
        BufferHandle indexBuffer, const uint32_t* sourceSlots, uint32_t count,
        const void* instanceData, size_t instanceBytes,
        const GpuIcbVirtualMaterial* materials) {
        (void)gpuArguments; (void)vertexBuffer; (void)indexBuffer;
        (void)sourceSlots; (void)count; (void)instanceData;
        (void)instanceBytes; (void)materials;
        return 0;
    }
    // Separate optional non-VT PBR material pathway. A renderer may
    // mix compatible opaque built-in PBR source textures/U2 while using
    // the same geometry/shader pipeline. Failure returns token zero.
    virtual uint32_t prepareGpuEncodedIndexedIcbWithPbrMaterials(
        BufferHandle gpuArguments, BufferHandle vertexBuffer,
        BufferHandle indexBuffer, const uint32_t* sourceSlots, uint32_t count,
        const void* instanceData, size_t instanceBytes,
        const GpuIcbPbrMaterial* materials) {
        (void)gpuArguments; (void)vertexBuffer; (void)indexBuffer;
        (void)sourceSlots; (void)count; (void)instanceData;
        (void)instanceBytes; (void)materials;
        return 0;
    }
    // Finish batched GPU command encoding before opening a render pass.
    // Backends may keep one compute encoder across multiple ICB groups.
    virtual void finishGpuEncodedIcbPreparation() {}
    // Execute pre-encoded ICB with parent pipeline/buffers currently bound.
    virtual bool executeGpuEncodedIndexedIcb(uint32_t token) {
        (void)token; return false;
    }
    virtual bool supportsMultiDrawIndirect() const { return false; }
    // GPU-authored ICB is a separate, higher-risk feature. A backend may
    // support CPU-encoded multi-draw without allowing GPU command encoding.
    virtual bool supportsGpuEncodedIcb() const { return false; }
    virtual bool drawIndexedIndirect(BufferHandle commands, size_t offset,
                                     uint32_t drawCount, uint32_t stride = sizeof(DrawIndexedIndirectCommand)) {
        (void)commands; (void)offset; (void)drawCount; (void)stride; return false;
    }

protected:
    int fbW_ = 0, fbH_ = 0;
private:
    GpuTimingRouter gpuTimingRouter_{};
};

// Backend factories. `nativeSurface` is backend specific:
//   Metal: CAMetalLayer*   GL: nullptr (context must be current on the calling thread)
std::unique_ptr<Device> createMetalDevice(void* nativeSurface);
std::unique_ptr<Device> createGLDevice(Backend glFlavor);

} // namespace aiverse::gfx
