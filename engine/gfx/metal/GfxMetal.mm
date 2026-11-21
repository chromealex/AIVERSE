// Metal backend (macOS, iOS). Compiled as Objective-C++ with ARC (-fobjc-arc).
#include "../Gfx.h"
#include "../ColorSpace.h"
#include "MetalShaderAssets.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <TargetConditionals.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <iterator>
#include <memory>
#include <limits>
#include <mutex>
#include <map>
#include <cstring>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace aiverse::gfx {
namespace {

constexpr int kFramesInFlight = 3;

// Snapshot allocations belong to independent GPU submissions and cannot be
// overwritten while in flight. Geometric growth avoids frequent MTLBuffer
// reallocations when a batch grows by a few indexed commands.
size_t growIndirectSnapshotCapacity(size_t oldCapacity, size_t required) {
    if (required <= oldCapacity) return oldCapacity;
    size_t capacity = std::max(oldCapacity, size_t{256});
    while (capacity < required) {
        if (capacity > SIZE_MAX / 2) return required;
        capacity *= 2;
    }
    return capacity;
}

template <class T> class Pool {
public:
    uint32_t add(T v) {
        if (!free_.empty()) { uint32_t i = free_.back(); free_.pop_back(); items_[i] = std::move(v); alive_[i] = 1; return i + 1; }
        items_.push_back(std::move(v)); alive_.push_back(1); return uint32_t(items_.size());
    }
    T* get(uint32_t id) { return id && id <= items_.size() && alive_[id - 1] ? &items_[id - 1] : nullptr; }
    const T* get(uint32_t id) const { return id && id <= items_.size() && alive_[id - 1] ? &items_[id - 1] : nullptr; }
    void remove(uint32_t id) { if (get(id)) { items_[id - 1] = T{}; alive_[id - 1] = 0; free_.push_back(id - 1); } }
    template <class F> void each(F&& fn) {
        for (size_t i = 0; i < items_.size(); ++i)
            if (alive_[i]) fn(uint32_t(i + 1), items_[i]);
    }
    template <class F> void each(F&& fn) const {
        for (size_t i = 0; i < items_.size(); ++i)
            if (alive_[i]) fn(uint32_t(i + 1), items_[i]);
    }
private:
    std::vector<T> items_; std::vector<uint8_t> alive_; std::vector<uint32_t> free_;
};

struct MTBuffer {
    id<MTLBuffer> bufs[kFramesInFlight];
    int count = 1;          // 1 for immutable, kFramesInFlight for dynamic (one copy per frame in flight)
    size_t size = 0;
    BufferType type = BufferType::Vertex;
    bool storage = false,snapshot = false;
    bool discardOnUpdate = false; // offset-zero writes replace readable prefix
    uint64_t updatedFrame = UINT64_MAX;
    // Only for dynamic indirect command snapshots: per-frame, per-submission
    // allocations retained until this frame slot is safe to recycle.
    std::vector<id<MTLBuffer>> indirectSnapshots[kFramesInFlight];
    // Number of valid bytes in each snapshot (per slot/submission).
    // Kept in lockstep with indirectSnapshots to avoid comparing uninitialized data.
    std::vector<size_t> indirectSnapshotValid[kFramesInFlight];
    size_t indirectCursor = 0;
    BufferUpdateStats transferStats{};
};
struct MTTexture {
    id<MTLTexture> tex;
    id<MTLSamplerState> sampler;
    // Preserve actual sampler semantics for graph source tex16.
    // Metal cannot dynamically bind sampler(16), so its ICB shader
    // uses a constexpr repeat/linear sampler, which is only correct
    // when these stored flags match the original texture descriptor.
    bool samplerRepeat = false;
    bool samplerLinear = false;
    bool samplerCompare = false;
    PixelFormat fmt = PixelFormat::RGBA8;
    TextureDimension dimension = TextureDimension::Texture2D;
    bool mips = false;
    bool storage = false;
};
struct MTTextureView { id<MTLTexture> tex; uint32_t parentId = 0; };
struct MTShader { id<MTLFunction> vs; id<MTLFunction> fs; std::string source; bool linear = false; };
struct MTShaderCompilation { MTShader value; std::string error; std::atomic<bool> done{false}; };
struct MTPipeline { id<MTLRenderPipelineState> state; id<MTLRenderPipelineState> icbPbrState; id<MTLArgumentEncoder> icbPbrEncoder; id<MTLDepthStencilState> depth; PipelineDesc desc; PipelineHandle hdr; ShaderHandle hdrShader; bool hdrFailed = false; bool icbDynamicGi = false; Device::ShaderJob hdrShaderJob; Device::PipelineJob hdrPipelineJob; };

MTLVertexFormat toMTL(VertexFormat f) {
    switch (f) {
        case VertexFormat::Float2: return MTLVertexFormatFloat2;
        case VertexFormat::Float3: return MTLVertexFormatFloat3;
        case VertexFormat::Float4: return MTLVertexFormatFloat4;
        case VertexFormat::UByte4N: return MTLVertexFormatUChar4Normalized;
        case VertexFormat::UByte4: return MTLVertexFormatUChar4;
    }
    return MTLVertexFormatFloat3;
}

class MetalDevice final : public Device {
public:
    explicit MetalDevice(CAMetalLayer* layer) : layer_(layer) {
        device_ = layer.device ? layer.device : MTLCreateSystemDefaultDevice();
        layer_.device = device_;
        layer_.pixelFormat = MTLPixelFormatBGRA8Unorm;
        layer_.framebufferOnly = YES;
        queue_ = [device_ newCommandQueue];
        frameSemaphore_ = dispatch_semaphore_create(kFramesInFlight);
        info_.backend = Backend::Metal;
        info_.clipDepthZeroToOne = true;
        info_.renderTargetFlipY = true;
        info_.name = std::string("Metal ") + device_.name.UTF8String;
        shaderAssets_=std::make_unique<MetalShaderAssets>(device_);
    }

    const DeviceInfo& info() const override { return info_; }
    int maxFragmentTextureSlots() const override { return 32; }
    std::vector<TextureResourceInfo> textureResources() const override {
        std::vector<TextureResourceInfo> result;
        textures_.each([&](uint32_t id, const MTTexture& tex) {
            if (!tex.tex) return;
            TextureResourceInfo item;
            item.handle = {id};
            item.dimension = tex.dimension;
            item.format = tex.fmt;
            item.width = int(tex.tex.width);
            item.height = int(tex.tex.height);
            item.depth = tex.dimension == TextureDimension::Texture3D ?
                         int(tex.tex.depth) : 1;
            item.arrayLayers = tex.dimension == TextureDimension::Cube ? 6 :
                tex.dimension == TextureDimension::CubeArray ?
                int(tex.tex.arrayLength)*6 :
                tex.dimension == TextureDimension::Texture2DArray ?
                int(tex.tex.arrayLength) : 1;
            item.mipLevels = int(tex.tex.mipmapLevelCount);
            item.sampleCount = int(tex.tex.sampleCount);
            item.renderTarget = (tex.tex.usage & MTLTextureUsageRenderTarget) != 0;
            item.storage = (tex.tex.usage & MTLTextureUsageShaderWrite) != 0;
            TextureDesc d;
            d.dimension = item.dimension; d.format = item.format;
            d.width = item.width; d.height = item.height; d.depth = item.depth;
            d.arrayLayers = item.arrayLayers; d.mipLevels = item.mipLevels;
            d.sampleCount = item.sampleCount;
            item.estimatedBytes = estimateTextureFootprint(d);
            result.push_back(item);
        });
        return result;
    }
    uint64_t frameSerial() const override { return frame_; }
    uint64_t completedFrameSerial() const override {
        return gpuHealthy_->load(std::memory_order_acquire)
            ? completedFrame_->load(std::memory_order_acquire) : 0;
    }
    uint64_t gpuSubmissionFence() const override {
        return cmd_ && gpuHealthy_->load(std::memory_order_acquire) ? frame_ : 0;
    }
    uint64_t gpuCompletedFence() const override {
        return gpuHealthy_->load(std::memory_order_acquire)
            ? completedGpuFrame_->load(std::memory_order_acquire) : 0;
    }
    bool waitForSubmittedGpuWork() override {
        if (!queue_ || !gpuHealthy_->load(std::memory_order_acquire) ||
            enc_ || compute_ || gpuIcbEncoder_)
            return false;
        // Do not create/commit a new barrier behind cmd_: cmd_ was already
        // created by beginFrame but is not committed yet, and Metal queues
        // may preserve command-buffer creation order. Waiting such a barrier
        // could deadlock. Instead wait for the last ACTUALLY committed frame.
        if (lastCommittedGpuFrameBuffer_) {
            [lastCommittedGpuFrameBuffer_ waitUntilCompleted];
            if (lastCommittedGpuFrameBuffer_.status !=
                MTLCommandBufferStatusCompleted) return false;
        }
        // Main graphics submissions are in-order on this queue; the last
        // committed frame covers all earlier frame viewport/ImGui uses.
        const uint64_t latest = lastCommittedGpuFrame_;
        uint64_t seen = completedGpuFrame_->load(std::memory_order_relaxed);
        while (seen < latest &&
               !completedGpuFrame_->compare_exchange_weak(
                    seen,latest,std::memory_order_release,
                    std::memory_order_relaxed)) {}
        return true;
    }
    bool waitForResourceIdle() override {
        // A separate command buffer on the same ordered queue completes
        // only AFTER all previously committed frames and uploads. It does
        // not flush currently recording draw commands; the host invokes
        // this before recording resource uses in its current frame.
        if(enc_ || compute_ || gpuIcbEncoder_ || !queue_)return false;
        id<MTLCommandBuffer> barrier=[queue_ commandBuffer];
        if(!barrier)return false;
        [barrier commit];
        [barrier waitUntilCompleted];
        if(barrier.status!=MTLCommandBufferStatusCompleted)return false;
        // A successfully completed queue-tail marker proves that all older
        // retirement markers have finished. Their completion-handler atomics
        // can still be waiting for callback dispatch on another thread.
        for(auto& [token,state]:retirementTokens_) {
            (void)token;
            state->store(1);
        }
        return true;
    }
    uint64_t beginResourceRetirement() override {
        // This marker is ordered after previously committed GPU work, but
        // NOT after the current frame's uncommitted command buffer.
        if(!queue_ || enc_ || compute_ || gpuIcbEncoder_ ||
           !gpuHealthy_->load())return 0;
        id<MTLCommandBuffer> marker=[queue_ commandBuffer];
        if(!marker)return 0;
        const auto state=std::make_shared<std::atomic<int>>(0);
        const uint64_t token=nextRetirementToken_++;
        retirementTokens_.emplace(token,state);
        [marker addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            state->store(completed.status==MTLCommandBufferStatusCompleted ? 1 : -1);
        }];
        [marker commit];
        return token;
    }
    bool resourceRetirementComplete(uint64_t token) override {
        const auto it=retirementTokens_.find(token);
        return it!=retirementTokens_.end() && it->second->load()==1;
    }
    ResourceRetirementStatus resourceRetirementStatus(uint64_t token) override {
        const auto it=retirementTokens_.find(token);
        if(it==retirementTokens_.end())return ResourceRetirementStatus::Failed;
        const int status=it->second->load();
        if(status>0)return ResourceRetirementStatus::Complete;
        if(status<0)return ResourceRetirementStatus::Failed;
        return ResourceRetirementStatus::Pending;
    }
    void endResourceRetirement(uint64_t token) override {
        const auto it=retirementTokens_.find(token);
        if(it!=retirementTokens_.end() && it->second->load()==1)
            retirementTokens_.erase(it);
    }
    void abandonRetirementTokenAfterIdle(uint64_t token) override {
        // Only after a successful ordered queue-tail idle marker.
        // The completion handler retains its own shared atomic reference.
        retirementTokens_.erase(token);
    }
    bool supports(PixelFormat f) const override {
        bool apple = false, bc = false;
#if TARGET_OS_IPHONE
        apple = true;
#else
        if (@available(macOS 11.0, *)) apple = [device_ supportsFamily:MTLGPUFamilyApple2];
        bc = true;
#endif
        if (@available(macOS 11.0, iOS 16.4, *)) bc = bc || device_.supportsBCTextureCompression;
        switch (f) {
            case PixelFormat::ETC2_RGB8: case PixelFormat::ETC2_RGBA8:
            case PixelFormat::EAC_R11: case PixelFormat::ASTC_4x4:
            case PixelFormat::ASTC_6x6:
            case PixelFormat::ETC2_RGB8_sRGB: case PixelFormat::ETC2_RGBA8_sRGB:
            case PixelFormat::ASTC_4x4_sRGB: case PixelFormat::ASTC_6x6_sRGB: return apple;
            case PixelFormat::BC1: case PixelFormat::BC3: case PixelFormat::BC4:
            case PixelFormat::BC5: case PixelFormat::BC7:
            case PixelFormat::BC6H_UFloat: case PixelFormat::BC6H_SFloat:
            case PixelFormat::BC1_sRGB: case PixelFormat::BC3_sRGB:
            case PixelFormat::BC7_sRGB:
                return bc && compressedFormat(f) != MTLPixelFormatInvalid;
            default: return true;
        }
    }
    bool supportsTextureDimension(TextureDimension) const override { return true; }
    bool supportsTextureViews() const override { return true; }
    bool supportsStorageTexture(PixelFormat format, TextureDimension dimension) const override {
        // Portable Metal writeable texture subset, avoiding compressed,
        // sRGB, multisample, depth and unverified typed storage formats.
        const bool sizeSupported = dimension == TextureDimension::Texture2D ||
                                   dimension == TextureDimension::Texture2DArray ||
                                   dimension == TextureDimension::Texture3D;
        // SpectraFX evolution writes RGBA32F positions into a 2D
        // storage texture. Other unverified float32 storage dimensions
        // remain unsupported.
        return supportsCompute() && sizeSupported &&
               (format == PixelFormat::RGBA8 ||
                format == PixelFormat::RGBA16F ||
                (format == PixelFormat::RGBA32F &&
                 dimension == TextureDimension::Texture2D));
    }

    static MTLSamplerAddressMode addressMode(SamplerAddressMode a) {
        switch (a) {
        case SamplerAddressMode::Repeat: return MTLSamplerAddressModeRepeat;
        case SamplerAddressMode::MirrorRepeat: return MTLSamplerAddressModeMirrorRepeat;
        case SamplerAddressMode::ClampToBorder: return MTLSamplerAddressModeClampToBorderColor;
        case SamplerAddressMode::ClampToEdge: return MTLSamplerAddressModeClampToEdge;
        }
        return MTLSamplerAddressModeClampToEdge;
    }
    static MTLCompareFunction compareFunction(CompareFunction f) {
        switch (f) {
        case CompareFunction::Never: return MTLCompareFunctionNever;
        case CompareFunction::Less: return MTLCompareFunctionLess;
        case CompareFunction::LessEqual: return MTLCompareFunctionLessEqual;
        case CompareFunction::Equal: return MTLCompareFunctionEqual;
        case CompareFunction::Greater: return MTLCompareFunctionGreater;
        case CompareFunction::GreaterEqual: return MTLCompareFunctionGreaterEqual;
        case CompareFunction::NotEqual: return MTLCompareFunctionNotEqual;
        case CompareFunction::Always: return MTLCompareFunctionAlways;
        }
        return MTLCompareFunctionLessEqual;
    }

    static MTLTextureType textureType(TextureDimension d) {
        switch (d) {
        case TextureDimension::Texture1D: return MTLTextureType1D;
        case TextureDimension::Texture2D: return MTLTextureType2D;
        case TextureDimension::Texture3D: return MTLTextureType3D;
        case TextureDimension::Texture2DArray: return MTLTextureType2DArray;
        case TextureDimension::Cube: return MTLTextureTypeCube;
        case TextureDimension::CubeArray: return MTLTextureTypeCubeArray;
        }
        return MTLTextureType2D;
    }

    static MTLPixelFormat compressedFormat(PixelFormat f) {
        switch (f) {
            case PixelFormat::ETC2_RGB8: return MTLPixelFormatETC2_RGB8;
            case PixelFormat::ETC2_RGBA8: return MTLPixelFormatEAC_RGBA8;
            case PixelFormat::EAC_R11: return MTLPixelFormatEAC_R11Unorm;
            case PixelFormat::ASTC_4x4: return MTLPixelFormatASTC_4x4_LDR;
            case PixelFormat::ASTC_6x6: return MTLPixelFormatASTC_6x6_LDR;
            case PixelFormat::ETC2_RGB8_sRGB: return MTLPixelFormatETC2_RGB8_sRGB;
            case PixelFormat::ETC2_RGBA8_sRGB: return MTLPixelFormatEAC_RGBA8_sRGB;
            case PixelFormat::ASTC_4x4_sRGB: return MTLPixelFormatASTC_4x4_sRGB;
            case PixelFormat::ASTC_6x6_sRGB: return MTLPixelFormatASTC_6x6_sRGB;
#if !TARGET_OS_IPHONE
            case PixelFormat::BC1: return MTLPixelFormatBC1_RGBA;
            case PixelFormat::BC3: return MTLPixelFormatBC3_RGBA;
            case PixelFormat::BC4: return MTLPixelFormatBC4_RUnorm;
            case PixelFormat::BC5: return MTLPixelFormatBC5_RGUnorm;
            case PixelFormat::BC7: return MTLPixelFormatBC7_RGBAUnorm;
            case PixelFormat::BC1_sRGB: return MTLPixelFormatBC1_RGBA_sRGB;
            case PixelFormat::BC3_sRGB: return MTLPixelFormatBC3_RGBA_sRGB;
            case PixelFormat::BC7_sRGB: return MTLPixelFormatBC7_RGBAUnorm_sRGB;
            case PixelFormat::BC6H_UFloat: return MTLPixelFormatBC6H_RGBUfloat;
            case PixelFormat::BC6H_SFloat: return MTLPixelFormatBC6H_RGBFloat;
#endif
            default: return MTLPixelFormatInvalid;
        }
    }
    const std::string& lastError() const override { return error_; }
    bool waitIdle() override {
        if(enc_ || compute_ || !queue_) return false;
        // A fresh command buffer waits for every previously submitted command
        // on this single queue. Never wait on cmd_ before endFrame commits it.
        id<MTLCommandBuffer> barrier=[queue_ commandBuffer];
        [barrier commit];
        [barrier waitUntilCompleted];
        if(barrier.status!=MTLCommandBufferStatusCompleted)return false;
        deliverBufferReads();
        deliverCapture();
        deliverTextureCaptures();
        return true;
    }

    // ---------- buffers ----------
    BufferHandle createBuffer(const BufferDesc& d) override {
        MTBuffer b;
        b.size = d.size ? d.size : 16;
        b.type = d.type;
        b.snapshot = d.snapshotUpdates;
        b.discardOnUpdate = d.discardOnUpdate;
        b.storage = d.type == BufferType::Storage;
        b.count = !b.storage && d.usage == Usage::Dynamic ? kFramesInFlight : 1;
        for (int i = 0; i < b.count; ++i) {
            b.bufs[i] = d.data ? [device_ newBufferWithBytes:d.data length:b.size options:MTLResourceStorageModeShared]
                               : [device_ newBufferWithLength:b.size options:MTLResourceStorageModeShared];
        }
        for(int i=0;i<b.count;++i)if(!b.bufs[i]){error_="buffer allocation failed";return {};}
        if (b.type == BufferType::Indirect && b.snapshot) {
            b.transferStats.available = true;
            b.transferStats.allocations = uint64_t(b.count);
            if (d.data) b.transferStats.mappedWriteBytes = uint64_t(d.size) * b.count;
        }
        return {buffers_.add(b)};
    }
    id<MTLBuffer> current(MTBuffer& b) { return b.bufs[b.count == 1 ? 0 : int(frame_ % kFramesInFlight)]; }
    bool uploadSharedStorage(BufferHandle h, const void* data, size_t size, size_t offset) {
        auto* b = buffers_.get(h.id);
        if (!b || !b->storage || !data || !size ||
            offset > b->size || size > b->size-offset || enc_ || compute_)
            return false;
        id<MTLBuffer> upload = [device_ newBufferWithBytes:data length:size options:MTLResourceStorageModeShared];
        if (!upload) { error_ = "storage upload allocation failed"; gpuHealthy_->store(false); return false; }
        id<MTLCommandBuffer> cb = cmd_ ? cmd_ : [queue_ commandBuffer];
        if (!cb) { error_ = "storage command buffer allocation failed"; gpuHealthy_->store(false); return false; }
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        if (!blit) { error_ = "storage blit encoder unavailable"; gpuHealthy_->store(false); return false; }
        [blit copyFromBuffer:upload sourceOffset:0 toBuffer:b->bufs[0]
          destinationOffset:offset size:size];
        [blit endEncoding];
        if (!cmd_) [cb commit];
        return true;
    }
    bool updateSharedBufferChecked(BufferHandle h, const void* data,
                                   size_t size, size_t offset) override {
        return uploadSharedStorage(h, data, size, offset);
    }
    bool updateStorageBufferChecked(BufferHandle h,const void* data,
                                    size_t size,size_t offset=0) override {
        auto* b=buffers_.get(h.id);
        if(!b || !b->storage || !cmd_ || enc_ || compute_ ||
           gpuIcbEncoder_ || !gpuHealthy_->load() || !data || size==0 ||
           offset>b->size || size>b->size-offset)return false;
        id<MTLBuffer> upload=[device_ newBufferWithBytes:data length:size
                                               options:MTLResourceStorageModeShared];
        if(!upload) {
            error_="checked SpectraFX storage upload allocation failed";
            gpuHealthy_->store(false);
            return false;
        }
        id<MTLBlitCommandEncoder> blit=[cmd_ blitCommandEncoder];
        if(!blit) {
            error_="checked SpectraFX storage upload blit encoder unavailable";
            return false;
        }
        [blit copyFromBuffer:upload sourceOffset:0
                   toBuffer:b->bufs[0] destinationOffset:offset size:size];
        [blit endEncoding];
        return true;
    }
    void updateBuffer(BufferHandle h, const void* data, size_t size, size_t offset) override {
        auto* b = buffers_.get(h.id);
        if (!b) return;
        if (b->storage) {
            (void)uploadSharedStorage(h, data, size, offset);
            return;
        }
        if (b->type == BufferType::Indirect && b->snapshot && b->count > 1) {
            // Metal executes commands asynchronously. A later submission must
            // never overwrite the buffer referenced by an earlier submission.
            // The queue and frame semaphore ensure that the same frame slot's
            // previous command buffers have finished before this slot is reused.
            if (!data || offset > SIZE_MAX - size) return;
            const size_t required = offset + size;
            const int slot = int(frame_ % kFramesInFlight);
            if (b->updatedFrame != frame_) b->indirectCursor = 0;
            auto& snapshots = b->indirectSnapshots[slot];
            auto& valid = b->indirectSnapshotValid[slot];
            const size_t cursor = b->indirectCursor;
            // The previous logical contents may be needed for general partial
            // updates, including cursor zero after this frame slot is recycled.
            // GPU fences guarantee the previous slot submission is finished.
            id<MTLBuffer> previous = b->bufs[slot];
            id<MTLBuffer> target = cursor < snapshots.size() ? snapshots[cursor] : nil;
            const bool discardPrefix = b->discardOnUpdate && offset == 0;
            const size_t capacity = discardPrefix
                ? growIndirectSnapshotCapacity(b->size, required)
                : std::max(b->size, required);
            const bool allocated = !target || target.length < capacity;
            if (allocated) {
                target = [device_ newBufferWithLength:capacity options:MTLResourceStorageModeShared];
                if (!target) {
                    error_ = "indirect snapshot allocation failed";
                    gpuHealthy_->store(false);
                    return;
                }
                if (cursor == snapshots.size()) {
                    snapshots.push_back(target);
                    valid.push_back(0);
                } else {
                    snapshots[cursor] = target;
                    valid[cursor] = 0;
                }
            }
            // The model indirect path replaces the readable command prefix.
            // No old tail is read by drawIndexedIndirect(drawCount), so skip
            // copying it from the previous submission. Non-opted-in buffers
            // retain their partial-update preservation semantics.
            size_t preserved = 0;
            if (!discardPrefix && previous && previous != target &&
                (offset != 0 || size < b->size)) {
                preserved = std::min(size_t(target.length), size_t(previous.length));
                std::memcpy(target.contents, previous.contents, preserved);
            }
            static const bool dirtyIndirectUpdates = [] {
                const char* setting = std::getenv("AIVERSE_METAL_ICB_DIRTY_UPDATES");
                return setting && std::strcmp(setting, "1") == 0;
            }();
            size_t written = size;
            size_t avoided = 0;
            size_t compared = 0;
            // Comparing a completed frame-slot snapshot is safe; comparing
            // snapshots from an earlier submission in *this* frame is not
            // permission to overwrite that submission. The cursor always
            // provides a distinct target for each new draw.
            if (dirtyIndirectUpdates && discardPrefix && valid[cursor] >= size) {
                // A changing camera often changes only a few indexed
                // commands. Copy only dirty 128-byte spans; keep the exact
                // 20-byte command ABI and the full current drawCount.
                constexpr size_t kDirtyChunk = 128;
                written = 0;
                auto* dst = static_cast<uint8_t*>(target.contents);
                const auto* src = static_cast<const uint8_t*>(data);
                for (size_t at = 0; at < size; at += kDirtyChunk) {
                    const size_t length = std::min(kDirtyChunk, size - at);
                    compared += length;
                    if (std::memcmp(dst + at, src + at, length) != 0) {
                        std::memcpy(dst + at, src + at, length);
                        written += length;
                    } else {
                        avoided += length;
                    }
                }
            } else if (dirtyIndirectUpdates && !discardPrefix &&
                       offset == 0 && size == b->size &&
                       valid[cursor] >= size) {
                compared += size;
                if (std::memcmp(target.contents, data, size) == 0) {
                    written = 0;
                    avoided = size;
                } else {
                    std::memcpy(target.contents, data, size);
                }
            } else {
                std::memcpy(static_cast<uint8_t*>(target.contents) + offset, data, size);
            }
            // When only the new prefix is valid, old tail data remains
            // explicitly undefined for discardOnUpdate clients.
            valid[cursor] = discardPrefix ? required : std::max(valid[cursor], required);
            b->bufs[slot] = target;
            b->size = capacity;
            b->indirectCursor = cursor + 1;
            b->updatedFrame = frame_;
            // Count only a completed snapshot update. These are observed CPU
            // copies, not the bytes fetched by Metal or discrete-GPU traffic.
            ++b->transferStats.updateCalls;
            b->transferStats.requestedBytes += size;
            b->transferStats.mappedWriteBytes += written + preserved;
            b->transferStats.preservedBytes += preserved;
            b->transferStats.comparedBytes += compared;
            b->transferStats.skippedWriteBytes += avoided;
            b->transferStats.allocations += allocated ? 1u : 0u;
            b->transferStats.reused += allocated ? 0u : 1u;
            if (std::getenv("AIVERSE_ICB_PROFILE")) {
                static uint64_t uploads = 0, copied = 0, saved = 0, growths = 0;
                copied += written;
                saved += avoided;
                growths += allocated ? 1u : 0u;
                if (++uploads == 600) {
                    std::fprintf(stderr,
                        "[metal-indirect-snapshots] /600 updates copied=%.1f KiB dirty-elided=%.1f KiB allocations=%llu (slot fenced)\n",
                        double(copied) / 1024.0, double(saved) / 1024.0,
                        (unsigned long long)growths);
                    uploads = copied = saved = growths = 0;
                }
            }
            return;
        }
        if (offset + size > b->size) {
            b->size = (offset + size) * 3 / 2;
            for (int i = 0; i < b->count; ++i) b->bufs[i] = [device_ newBufferWithLength:b->size options:MTLResourceStorageModeShared];
        }
        if(b->snapshot && b->count>1 && b->updatedFrame==frame_){
            // Multiple simulation ticks can be encoded before the GPU consumes the first.
            int slot=int(frame_%kFramesInFlight);id<MTLBuffer> old=b->bufs[slot];
            id<MTLBuffer> fresh=[device_ newBufferWithBytes:old.contents length:b->size options:MTLResourceStorageModeShared];
            if(!fresh){error_="dynamic snapshot allocation failed";gpuHealthy_->store(false);return;}
            b->bufs[slot]=fresh;
        }
        b->updatedFrame=frame_;
        if (b->count == 1 && frame_ > 0) {
            // Immutable buffer updated after creation: safest is a fresh allocation (GPU may still read the old one).
            id<MTLBuffer> nb = [device_ newBufferWithLength:b->size options:MTLResourceStorageModeShared];
            memcpy(nb.contents, b->bufs[0].contents, b->size);
            b->bufs[0] = nb;
        }
        memcpy(static_cast<uint8_t*>(current(*b).contents) + offset, data, size);
    }
    bool bufferInfo(BufferHandle h, BufferInfo& out) const override {
        const auto* b = buffers_.get(h.id);
        if (!b) return false;
        out = {b->size,uint32_t(b->count)};
        return true;
    }
    void destroyBuffer(BufferHandle h) override { buffers_.remove(h.id); }
    BufferUpdateStats bufferUpdateStats(BufferHandle h) override {
        auto* b = buffers_.get(h.id);
        if (!b || !b->transferStats.available) return {};
        auto stats = b->transferStats;
        // b->bufs[slot] can alias an entry in indirectSnapshots[slot].
        // Do not double-count those MTLBuffers in retained capacity.
        for (int slot = 0; slot < b->count; ++slot) {
            const auto& snapshots = b->indirectSnapshots[slot];
            for (id<MTLBuffer> snapshot : snapshots)
                if (snapshot) stats.retainedBufferBytes += size_t(snapshot.length);
            bool alias = false;
            for (id<MTLBuffer> snapshot : snapshots)
                if (snapshot == b->bufs[slot]) { alias = true; break; }
            if (b->bufs[slot] && !alias)
                stats.retainedBufferBytes += size_t(b->bufs[slot].length);
        }
        return stats;
    }
    bool canUpdateSharedBuffers() const override { return !enc_ && !compute_; }

    bool supportsCompute() const override {
        if (@available(macOS 10.14, iOS 12.0, *)) return true;
        return false;
    }
    ComputeHandle createCompute(const ComputeDesc& d) override {
        if (!supportsCompute() || !d.msl) return {};
        NSError* err=nil; MTLCompileOptions* options=[MTLCompileOptions new];
#if __MAC_OS_X_VERSION_MAX_ALLOWED >= 150000 || __IPHONE_OS_VERSION_MAX_ALLOWED >= 180000
        if (@available(macOS 15.0, iOS 18.0, *)) options.mathMode=MTLMathModeSafe;
        else
#endif
        {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            options.fastMathEnabled=NO;
#pragma clang diagnostic pop
        }
        id<MTLLibrary> lib=[device_ newLibraryWithSource:[NSString stringWithUTF8String:d.msl] options:options error:&err];
        if (!lib) { error_=err.localizedDescription.UTF8String;return {}; }
        id<MTLFunction> fn=[lib newFunctionWithName:@"cs_main"];
        if(!fn){error_="compute entry cs_main is missing";return {};}
        id<MTLComputePipelineState> state=[device_ newComputePipelineStateWithFunction:fn error:&err];
        if (!state || state.maxTotalThreadsPerThreadgroup<64) { error_=err ? err.localizedDescription.UTF8String : "compute group size unsupported";return {}; }
        return {computes_.add(state)};
    }
    void destroyCompute(ComputeHandle h) override { computes_.remove(h.id); }
    bool beginCompute(ComputeHandle h) override {
        computeDispatchSucceeded_=false;
        auto* state=computes_.get(h.id);
        if (!cmd_ || enc_ || compute_ || gpuIcbEncoder_ || !state) return false;
        compute_=[cmd_ computeCommandEncoder];[compute_ setComputePipelineState:*state];return compute_!=nil;
    }
    void setComputeBuffer(int slot,BufferHandle h) override {
        if (auto* b=buffers_.get(h.id)) [compute_ setBuffer:current(*b) offset:0 atIndex:NSUInteger(slot)];
    }
    void setComputeTexture(int slot,TextureHandle h) override {
        if (!compute_ || slot < 0 || slot >= 16) return;
        if (auto* t=textures_.get(h.id)) [compute_ setTexture:t->tex atIndex:NSUInteger(slot)];
    }
    bool bindComputeStorageTexture(int slot,TextureHandle h) override {
        if (!compute_ || slot < 0 || slot >= 16) {
            error_ = "Metal: a writable texture requires an active compute encoder and slot 0..15";
            return false;
        }
        const MTTexture* t = textures_.get(h.id);
        if (!t || !t->tex || !t->storage ||
            !(t->tex.usage & MTLTextureUsageShaderWrite) ||
            t->tex.sampleCount > 1 ||
            !supportsStorageTexture(t->fmt,t->dimension)) {
            error_ = "Metal: texture was not created for supported compute writes";
            return false;
        }
        [compute_ setTexture:t->tex atIndex:NSUInteger(slot)];
        return true;
    }
    void setComputeUniforms(const void* data,size_t size) override { if(size<=4096)[compute_ setBytes:data length:size atIndex:8]; }
    void dispatch(uint32_t x,uint32_t y) override {
        computeDispatchSucceeded_=false;
        if (!compute_ || !x || !y) return;
        [compute_ dispatchThreadgroups:MTLSizeMake((x+63)/64,y,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
        if (@available(macOS 10.14, iOS 12.0, *)) [compute_ memoryBarrierWithScope:MTLBarrierScopeBuffers|MTLBarrierScopeTextures];
        computeDispatchSucceeded_=true;
    }
    bool computeDispatchSucceeded() const override { return computeDispatchSucceeded_; }
    void endCompute() override { if(compute_){[compute_ endEncoding];compute_=nil;} }
    bool readBuffer(BufferHandle h,void* data,size_t size) override {
        auto* b=buffers_.get(h.id);if(cmd_||!b||!data||size>b->size)return false;
        id<MTLBuffer> dst=[device_ newBufferWithLength:size options:MTLResourceStorageModeShared];
        if(!dst)return false;
        id<MTLCommandBuffer> cb=[queue_ commandBuffer];id<MTLBlitCommandEncoder> blit=[cb blitCommandEncoder];
        [blit copyFromBuffer:current(*b) sourceOffset:0 toBuffer:dst destinationOffset:0 size:size];[blit endEncoding];
        [cb commit];[cb waitUntilCompleted];if(cb.status==MTLCommandBufferStatusError)return false;
        memcpy(data,dst.contents,size);return true;
    }
    struct BufferRead {std::vector<uint8_t> bytes;BufferReadFn callback;std::atomic<bool> ready{false};};
    std::vector<std::shared_ptr<BufferRead>> bufferReads_;
    struct TextureRead {
        TextureReadbackResult result;
        TextureReadFn callback;
        std::atomic<bool> ready{false};
    };
    std::vector<std::shared_ptr<TextureRead>> textureReads_;
    bool readTextureAsync(TextureHandle h, TextureReadbackDesc request,
                          TextureReadFn callback) override {
        MTTexture* t = textures_.get(h.id);
        if (!cmd_ || enc_ || compute_ || gpuIcbEncoder_ || !callback ||
            !t || !t->tex || t->tex.sampleCount != 1 ||
            request.mipLevel < 0 ||
            request.mipLevel >= int(t->tex.mipmapLevelCount) ||
            request.arrayLayer < 0 || textureReads_.size() >= 32)
            return false;
        const bool cube = t->dimension == TextureDimension::Cube ||
                          t->dimension == TextureDimension::CubeArray;
        const bool volume=t->dimension==TextureDimension::Texture3D;
        const int layers = cube ? int(t->tex.arrayLength)*6 :
                           volume ? int(std::max<NSUInteger>(1,
                                  t->tex.depth >> request.mipLevel)) :
                           t->dimension == TextureDimension::Texture2DArray ?
                           int(t->tex.arrayLength) : 1;
        if ((t->dimension != TextureDimension::Texture2D &&
             t->dimension != TextureDimension::Texture2DArray &&
             !cube && !volume) || request.arrayLayer >= layers) return false;
        const MTLPixelFormat pixelFormat = t->tex.pixelFormat;
        const bool isBGRA = pixelFormat == MTLPixelFormatBGRA8Unorm ||
                            pixelFormat == MTLPixelFormatBGRA8Unorm_sRGB;
        const bool isRGBA = pixelFormat == MTLPixelFormatRGBA8Unorm ||
                            pixelFormat == MTLPixelFormatRGBA8Unorm_sRGB;
        const bool isHalf = pixelFormat == MTLPixelFormatRGBA16Float;
        if ((!isBGRA && !isRGBA && !isHalf) ||
            (isHalf ? t->fmt != PixelFormat::RGBA16F :
             t->fmt != PixelFormat::RGBA8 && t->fmt != PixelFormat::RGBA8_sRGB))
            return false;
        const size_t width = size_t(std::max<NSUInteger>(1,
                              t->tex.width >> request.mipLevel));
        const size_t height = size_t(std::max<NSUInteger>(1,
                               t->tex.height >> request.mipLevel));
        const size_t bpp = isHalf ? 8u : 4u;
        // Align bytesPerRow for the Metal texture-to-buffer blit.
        if (width > SIZE_MAX / bpp) return false;
        const size_t packedRow = width*bpp;
        if (packedRow > SIZE_MAX-255u) return false;
        const size_t gpuRow = ((packedRow+255u)/256u)*256u;
        if (height == 0 || gpuRow > (size_t(256)<<20)/height)
            return false;
        const size_t gpuBytes = gpuRow*height;
        id<MTLBuffer> target = [device_ newBufferWithLength:gpuBytes
                                                options:MTLResourceStorageModeShared];
        if (!target) { error_ = "Metal: texture readback staging buffer allocation failed"; return false; }
        id<MTLBlitCommandEncoder> blit = [cmd_ blitCommandEncoder];
        if (!blit) { error_ = "Metal: cannot begin texture readback blit"; return false; }
        [blit copyFromTexture:t->tex sourceSlice:volume ? 0u : NSUInteger(request.arrayLayer)
                    sourceLevel:NSUInteger(request.mipLevel)
                   sourceOrigin:MTLOriginMake(0,0,volume ? NSUInteger(request.arrayLayer) : 0u)
                     sourceSize:MTLSizeMake(width,height,1)
                       toBuffer:target destinationOffset:0
         destinationBytesPerRow:gpuRow destinationBytesPerImage:gpuBytes];
        [blit endEncoding];
        auto read = std::make_shared<TextureRead>();
        read->callback = std::move(callback);
        read->result.format = t->fmt;
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            if (completed.status == MTLCommandBufferStatusCompleted) {
                read->result.width = int(width);
                read->result.height = int(height);
                read->result.pixels.resize(packedRow*height);
                const uint8_t* src = static_cast<const uint8_t*>(target.contents);
                uint8_t* dst = read->result.pixels.data();
                for (size_t y=0;y<height;++y) {
                    const uint8_t* input = src + y*gpuRow;
                    uint8_t* output = dst + y*packedRow;
                    if (isBGRA) {
                        for (size_t x=0;x<width;++x) {
                            output[x*4+0]=input[x*4+2];
                            output[x*4+1]=input[x*4+1];
                            output[x*4+2]=input[x*4+0];
                            output[x*4+3]=input[x*4+3];
                        }
                    } else std::memcpy(output,input,packedRow);
                }
            }
            read->ready.store(true,std::memory_order_release);
        }];
        textureReads_.push_back(std::move(read));
        return true;
    }
    bool readBufferAsync(BufferHandle h,size_t size,BufferReadFn callback) override {
        auto* b=buffers_.get(h.id);
        if(!cmd_||enc_||compute_||!b||!size||size>b->size||size>65536||!callback||bufferReads_.size()>=32)return false;
        id<MTLBuffer> dst=[device_ newBufferWithLength:size options:MTLResourceStorageModeShared];if(!dst)return false;
        auto read=std::make_shared<BufferRead>();read->bytes.resize(size);read->callback=std::move(callback);
        id<MTLBlitCommandEncoder> blit=[cmd_ blitCommandEncoder];
        [blit copyFromBuffer:current(*b) sourceOffset:0 toBuffer:dst destinationOffset:0 size:size];[blit endEncoding];
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> cb){
            if(cb.status==MTLCommandBufferStatusError)read->bytes.clear();else memcpy(read->bytes.data(),dst.contents,size);
            read->ready.store(true,std::memory_order_release);
        }];
        bufferReads_.push_back(std::move(read));return true;
    }
    bool readBufferRangeAsync(BufferHandle h,size_t offset,size_t size,
                              BufferReadFn callback) override {
        auto* b=buffers_.get(h.id);
        if(!cmd_||enc_||compute_||gpuIcbEncoder_||!gpuHealthy_->load()||!b||!size||offset>b->size||
           size>b->size-offset||size>65536||!callback||
           bufferReads_.size()>=32)return false;
        id<MTLBuffer> dst=[device_ newBufferWithLength:size
                             options:MTLResourceStorageModeShared];
        if(!dst)return false;
        auto read=std::make_shared<BufferRead>();
        read->bytes.resize(size);
        read->callback=std::move(callback);
        id<MTLBlitCommandEncoder> blit=[cmd_ blitCommandEncoder];
        if(!blit)return false;
        [blit copyFromBuffer:current(*b) sourceOffset:offset
                  toBuffer:dst destinationOffset:0 size:size];
        [blit endEncoding];
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> cb){
            if(cb.status!=MTLCommandBufferStatusCompleted)read->bytes.clear();
            else memcpy(read->bytes.data(),dst.contents,size);
            read->ready.store(true,std::memory_order_release);
        }];
        bufferReads_.push_back(std::move(read));
        return true;
    }
    bool readTexturePixelAsync(TextureHandle h,int x,int y,PixelReadFn callback) override {
        auto* source=textures_.get(h.id);
        if(!cmd_ || enc_ || compute_ || !source || !source->tex ||
           source->fmt!=PixelFormat::RGBA8 || !callback ||
           (source->tex.usage & MTLTextureUsageRenderTarget)==0 ||
           source->tex.sampleCount!=1 ||
           x<0 || y<0 || x>=int(source->tex.width) ||
           y>=int(source->tex.height) ||
           bufferReads_.size()>=32)return false;
        const MTLPixelFormat format=source->tex.pixelFormat;
        if(format!=MTLPixelFormatBGRA8Unorm &&
           format!=MTLPixelFormatRGBA8Unorm)return false;
        // Metal texture->buffer blits require a padded row on many devices.
        // Copy a single top-left-origin pixel; the command buffer keeps both
        // texture and staging buffer alive through GPU completion.
        constexpr NSUInteger rowBytes=256;
        id<MTLBuffer> staging=[device_ newBufferWithLength:rowBytes
                                options:MTLResourceStorageModeShared];
        if(!staging)return false;
        id<MTLBlitCommandEncoder> blit=[cmd_ blitCommandEncoder];
        if(!blit)return false;
        [blit copyFromTexture:source->tex sourceSlice:0 sourceLevel:0
                sourceOrigin:MTLOriginMake(NSUInteger(x),NSUInteger(y),0)
                  sourceSize:MTLSizeMake(1,1,1)
                    toBuffer:staging destinationOffset:0
      destinationBytesPerRow:rowBytes destinationBytesPerImage:rowBytes];
        [blit endEncoding];
        auto read=std::make_shared<BufferRead>();
        read->bytes.resize(4);
        read->callback=std::move(callback);
        const bool bgra=format==MTLPixelFormatBGRA8Unorm;
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            if(completed.status!=MTLCommandBufferStatusCompleted) {
                read->bytes.clear();
            }else{
                const auto* px=static_cast<const uint8_t*>(staging.contents);
                if(bgra) {
                    read->bytes[0]=px[2];read->bytes[1]=px[1];
                    read->bytes[2]=px[0];read->bytes[3]=px[3];
                }else{
                    std::copy_n(px,4,read->bytes.begin());
                }
            }
            read->ready.store(true,std::memory_order_release);
        }];
        bufferReads_.push_back(std::move(read));
        return true;
    }
    bool readTextureDepthPixelAsync(TextureHandle h,int x,int y,
                                    DepthPixelReadFn callback) override {
        // Valid handle must reference an explicit retained offscreen depth
        // attachment; invalid handle refers ONLY to the current screen
        // depth buffer. Never substitute a missing model/ID target.
        id<MTLTexture> source=nil;
        if(h.valid()) {
            auto* t=textures_.get(h.id);
            if(!t || t->fmt!=PixelFormat::Depth)return false;
            source=t->tex;
        }else source=depth_;
        if(!cmd_ || enc_ || compute_ || gpuIcbEncoder_ || !source ||
           !callback || !gpuHealthy_->load() ||
           source.pixelFormat!=MTLPixelFormatDepth32Float ||
           source.sampleCount!=1 ||
           x<0 || y<0 || x>=int(source.width) ||
           y>=int(source.height) ||
           bufferReads_.size()>=32)return false;
        // Depth32Float native bits are read back as one IEEE-754 float.
        // Metal texture-to-buffer blits require 256-byte row alignment.
        constexpr NSUInteger rowBytes=256;
        id<MTLBuffer> staging=[device_ newBufferWithLength:rowBytes
                                options:MTLResourceStorageModeShared];
        if(!staging)return false;
        id<MTLBlitCommandEncoder> blit=[cmd_ blitCommandEncoder];
        if(!blit)return false;
        [blit copyFromTexture:source sourceSlice:0 sourceLevel:0
                sourceOrigin:MTLOriginMake(NSUInteger(x),NSUInteger(y),0)
                  sourceSize:MTLSizeMake(1,1,1)
                    toBuffer:staging destinationOffset:0
      destinationBytesPerRow:rowBytes destinationBytesPerImage:rowBytes];
        [blit endEncoding];
        auto read=std::make_shared<BufferRead>();
        read->bytes.resize(sizeof(float));
        read->callback=[fn=std::move(callback)](std::vector<uint8_t> bytes) {
            float depth=0.f;
            if(bytes.size()!=sizeof(depth)) {
                fn(false,depth);
                return;
            }
            std::memcpy(&depth,bytes.data(),sizeof(depth));
            fn(std::isfinite(depth) && depth>=0.f && depth<=1.f,depth);
        };
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            if(completed.status!=MTLCommandBufferStatusCompleted)
                read->bytes.clear();
            else std::memcpy(read->bytes.data(),staging.contents,sizeof(float));
            read->ready.store(true,std::memory_order_release);
        }];
        bufferReads_.push_back(std::move(read));
        return true;
    }
    void deliverBufferReads(){
        // Move ready entries before callbacks, allowing callbacks to enqueue other work.
        std::vector<std::shared_ptr<BufferRead>> ready;
        for(auto it=bufferReads_.begin();it!=bufferReads_.end();){
            if((*it)->ready.load(std::memory_order_acquire)){ready.push_back(*it);it=bufferReads_.erase(it);}else ++it;
        }
        for(auto& read:ready)read->callback(std::move(read->bytes));
    }
    // A depth-only diagnostic readback, asynchronous and bounded. Metal's
    // Depth32Float shadow map is private; encode a blit into a temporary shared
    // buffer after the shadow render pass, never use getBytes on private memory.
    struct DepthRead {
        int width = 0, height = 0;
        std::vector<uint8_t> bytes;
        DepthReadFn callback;
        std::atomic<bool> ready{false};
    };
    std::vector<std::shared_ptr<DepthRead>> depthReads_;
    bool readDepthTextureAsync(TextureHandle h,DepthReadFn callback) override {
        auto* t = textures_.get(h.id);
        if (!cmd_ || enc_ || compute_ || !t || !callback ||
            t->fmt != PixelFormat::Depth ||
            t->tex.pixelFormat != MTLPixelFormatDepth32Float ||
            t->tex.textureType != MTLTextureType2D ||
            t->tex.sampleCount != 1 ||
            !t->tex.width || !t->tex.height ||
            t->tex.width > 4096 || t->tex.height > 4096 ||
            depthReads_.size() >= 8)
            return false;
        const NSUInteger w = t->tex.width, hgt = t->tex.height;
        constexpr NSUInteger kFloatBytes = sizeof(float);
        // Metal blit-to-buffer row stride must be padded. All user-facing
        // bytes are repacked to tightly packed float32 at callback delivery.
        const NSUInteger rowBytes =
            ((w * kFloatBytes + 255u) / 256u) * 256u;
        const NSUInteger allocation = rowBytes * hgt;
        if (allocation > 64u * 1024u * 1024u) return false;
        id<MTLBuffer> dst =
            [device_ newBufferWithLength:allocation
                                 options:MTLResourceStorageModeShared];
        if (!dst) return false;
        auto read = std::make_shared<DepthRead>();
        read->width = int(w);
        read->height = int(hgt);
        read->bytes.resize(size_t(w * hgt) * sizeof(float));
        read->callback = std::move(callback);
        id<MTLBlitCommandEncoder> blit = [cmd_ blitCommandEncoder];
        if (!blit) return false;
        [blit copyFromTexture:t->tex sourceSlice:0 sourceLevel:0
                sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(w, hgt, 1)
                    toBuffer:dst destinationOffset:0
        destinationBytesPerRow:rowBytes
      destinationBytesPerImage:allocation];
        [blit endEncoding];
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            if (completed.status != MTLCommandBufferStatusCompleted) {
                read->bytes.clear();
            } else {
                const uint8_t* src =
                    static_cast<const uint8_t*>(dst.contents);
                for (NSUInteger y = 0; y < hgt; ++y)
                    std::memcpy(read->bytes.data() + size_t(y*w)*sizeof(float),
                                src + size_t(y*rowBytes),
                                size_t(w)*sizeof(float));
            }
            read->ready.store(true, std::memory_order_release);
        }];
        depthReads_.push_back(std::move(read));
        return true;
    }
    void deliverDepthReads() {
        std::vector<std::shared_ptr<DepthRead>> ready;
        for (auto it = depthReads_.begin(); it != depthReads_.end();) {
            if ((*it)->ready.load(std::memory_order_acquire)) {
                ready.push_back(*it);
                it = depthReads_.erase(it);
            } else ++it;
        }
        for (auto& read : ready)
            read->callback(read->width, read->height,
                           std::move(read->bytes));
    }
    // Diagnostic only: copy the *actual offscreen reflection render target*
    // (not the presented drawable) as native RGBA8. Same GPU queue and
    // owner-thread completion rules as readDepthTextureAsync().
    struct ColorRead {
        int width = 0, height = 0;
        std::vector<uint8_t> bytes;
        ColorReadFn callback;
        std::atomic<bool> ready{false};
    };
    std::vector<std::shared_ptr<ColorRead>> colorReads_;
    bool readColorTextureAsync(TextureHandle h,ColorReadFn callback) override {
        auto* t = textures_.get(h.id);
        if (!cmd_ || enc_ || compute_ || !t || !callback ||
            t->fmt != PixelFormat::RGBA8 ||
            t->tex.pixelFormat != MTLPixelFormatBGRA8Unorm ||
            t->tex.textureType != MTLTextureType2D ||
            t->tex.sampleCount != 1 ||
            !(t->tex.usage & MTLTextureUsageRenderTarget) ||
            !t->tex.width || !t->tex.height ||
            t->tex.width > 4096 || t->tex.height > 4096 ||
            colorReads_.size() >= 8)
            return false;
        const NSUInteger w=t->tex.width, hgt=t->tex.height;
        const NSUInteger rowBytes=((w*4u+255u)/256u)*256u;
        const NSUInteger length=rowBytes*hgt;
        if (length > 64u*1024u*1024u) return false;
        id<MTLBuffer> dst=[device_ newBufferWithLength:length
                               options:MTLResourceStorageModeShared];
        if (!dst) return false;
        auto read=std::make_shared<ColorRead>();
        read->width=int(w);
        read->height=int(hgt);
        read->bytes.resize(size_t(w*hgt)*4u);
        read->callback=std::move(callback);
        id<MTLBlitCommandEncoder> blit=[cmd_ blitCommandEncoder];
        if (!blit) return false;
        [blit copyFromTexture:t->tex sourceSlice:0 sourceLevel:0
                 sourceOrigin:MTLOriginMake(0,0,0)
                   sourceSize:MTLSizeMake(w,hgt,1)
                     toBuffer:dst destinationOffset:0
         destinationBytesPerRow:rowBytes
       destinationBytesPerImage:length];
        [blit endEncoding];
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            if (completed.status != MTLCommandBufferStatusCompleted)
                read->bytes.clear();
            else {
                const uint8_t* src=static_cast<const uint8_t*>(dst.contents);
                for (NSUInteger y=0;y<hgt;++y)
                    for (NSUInteger x=0;x<w;++x) {
                        const size_t from=size_t(y*rowBytes+x*4u);
                        const size_t to=size_t((y*w+x)*4u);
                        read->bytes[to+0]=src[from+2];
                        read->bytes[to+1]=src[from+1];
                        read->bytes[to+2]=src[from+0];
                        read->bytes[to+3]=src[from+3];
                    }
            }
            read->ready.store(true,std::memory_order_release);
        }];
        colorReads_.push_back(std::move(read));
        return true;
    }
    void deliverColorReads() {
        std::vector<std::shared_ptr<ColorRead>> ready;
        for (auto it=colorReads_.begin();it!=colorReads_.end();) {
            if ((*it)->ready.load(std::memory_order_acquire)) {
                ready.push_back(*it);
                it=colorReads_.erase(it);
            } else ++it;
        }
        for (auto& read:ready)
            read->callback(read->width,read->height,std::move(read->bytes));
    }
    void deliverTextureReads() {
        // Never invoke app callbacks on the asynchronous Metal driver thread.
        std::vector<std::shared_ptr<TextureRead>> ready;
        for (auto it=textureReads_.begin();it!=textureReads_.end();) {
            if ((*it)->ready.load(std::memory_order_acquire)) {
                ready.push_back(*it);
                it=textureReads_.erase(it);
            } else ++it;
        }
        for (auto& read:ready) read->callback(std::move(read->result));
    }
    bool gpuHealthy() const override { return gpuHealthy_->load(); }
    double gpuFrameMs() const override { return gpuMs_->load(); }

    // ---------- textures ----------
    TextureHandle createTexture(const TextureDesc& d) override {
        if (const TextureDescError e = validateTextureDesc(d); e != TextureDescError::None) {
            error_ = textureDescErrorString(e); return {};
        }
        if (d.storage && !supportsStorageTexture(d.format,d.dimension)) {
            error_ = "Metal: unsupported writable texture format or dimension";
            return {};
        }
        if (d.renderTarget && (d.dimension == TextureDimension::Texture1D ||
                               d.dimension == TextureDimension::Texture3D)) {
            error_ = "Metal: 1D/3D render targets are unsupported";
            return {};
        }
        if (d.sampleCount > 1 && ![device_ supportsTextureSampleCount:NSUInteger(d.sampleCount)]) {
            error_ = "Metal: requested MSAA sample count is unsupported by the device";
            return {};
        }
        if (d.renderTarget && isExtendedDataTextureFormat(d.format)) {
            error_ = "Metal: typed R/RG render targets require pipeline format support";
            return {};
        }
        const MTLPixelFormat cf = compressedFormat(d.format);
        const bool compressed = cf != MTLPixelFormatInvalid;
        if (isCompressedTextureFormat(d.format) &&
            (!compressed || !supports(d.format))) {
            error_ = "Metal: requested compressed pixel format is unsupported on this platform";
            return {};
        }
        if (compressed && (d.dimension == TextureDimension::Texture1D ||
                           d.dimension == TextureDimension::Texture3D || d.storage ||
                           d.renderTarget || d.levelCount < 1 || !d.levelData || !d.levelSize)) {
            error_ = "Metal: compressed texture requires 2D/array/cube mip payloads";
            return {};
        }
        if (compressed && !supports(d.format)) { error_ = "Metal: compressed format unavailable"; return {}; }

        const MTLPixelFormat pf = compressed ? cf
            : d.format == PixelFormat::RG8 ? MTLPixelFormatRG8Unorm
            : d.format == PixelFormat::R16F ? MTLPixelFormatR16Float
            : d.format == PixelFormat::RG16F ? MTLPixelFormatRG16Float
            : d.format == PixelFormat::R32F ? MTLPixelFormatR32Float
            : d.format == PixelFormat::RG32F ? MTLPixelFormatRG32Float
            : d.format == PixelFormat::R8 ? MTLPixelFormatR8Unorm
            : d.format == PixelFormat::RGBA16F ? MTLPixelFormatRGBA16Float
            : d.format == PixelFormat::RGBA32F ? MTLPixelFormatRGBA32Float
            : d.format == PixelFormat::Depth ? MTLPixelFormatDepth32Float
            : d.format == PixelFormat::RGBA8_sRGB
                ? (d.renderTarget ? MTLPixelFormatBGRA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm_sRGB)
            : d.renderTarget ? MTLPixelFormatBGRA8Unorm
            : MTLPixelFormatRGBA8Unorm;
        const bool generate = d.mipmaps && !compressed && d.format != PixelFormat::Depth &&
                              d.format != PixelFormat::RGBA32F && !d.renderTarget;
        const int levels = compressed ? d.levelCount
            : d.mipLevels > 0 ? d.mipLevels
            : generate ? maxTextureMipLevels(d.width, d.height, d.depth) : 1;
        MTLTextureDescriptor* td = [MTLTextureDescriptor new];
        td.textureType = d.sampleCount > 1 ? MTLTextureType2DMultisample :
                         textureType(d.dimension);
        td.pixelFormat = pf;
        td.sampleCount = NSUInteger(d.sampleCount);
        td.width = NSUInteger(d.width);
        td.height = NSUInteger(d.height);
        td.depth = NSUInteger(d.dimension == TextureDimension::Texture3D ? d.depth : 1);
        td.arrayLength = NSUInteger(d.dimension == TextureDimension::Texture2DArray ? d.arrayLayers :
                                    d.dimension == TextureDimension::CubeArray ? d.arrayLayers / 6 : 1);
        td.mipmapLevelCount = NSUInteger(levels);
        td.usage = MTLTextureUsageShaderRead | (d.storage ? MTLTextureUsageShaderWrite : 0) |
                   (d.renderTarget || d.format == PixelFormat::Depth ? MTLTextureUsageRenderTarget : 0);
        if (d.format == PixelFormat::Depth || d.sampleCount > 1)
            td.storageMode = MTLStorageModePrivate;
        MTTexture t;
        t.fmt = d.format; t.dimension = d.dimension; t.storage = d.storage;
        t.tex = [device_ newTextureWithDescriptor:td];
        if (!t.tex) { error_ = "Metal: requested texture type, dimensions or pixel format unavailable"; return {}; }

        const int faces = d.arrayLayers;
        if (compressed) {
            const NSUInteger blockDim = NSUInteger(compressedTextureBlockSize(d.format));
            const NSUInteger blockBytes = NSUInteger(compressedTextureBlockBytes(d.format));
            for (int l = 0; l < d.levelCount; ++l) {
                const NSUInteger w = NSUInteger(std::max(1, d.width >> l));
                const NSUInteger h = NSUInteger(std::max(1, d.height >> l));
                const NSUInteger bytesPerRow = ((w + blockDim - 1) / blockDim) * blockBytes;
                const NSUInteger bytesPerImage = bytesPerRow * ((h + blockDim - 1) / blockDim);
                if (d.levelSize[l] < size_t(bytesPerImage) * size_t(faces) || !d.levelData[l]) {
                    error_ = "Metal: compressed mip payload does not cover all faces";
                    return {};
                }
                const auto* pixels = static_cast<const uint8_t*>(d.levelData[l]);
                if (d.dimension == TextureDimension::Texture2D) {
                    [t.tex replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:NSUInteger(l)
                               withBytes:pixels bytesPerRow:bytesPerRow];
                } else {
                    for (int face = 0; face < faces; ++face)
                        [t.tex replaceRegion:MTLRegionMake2D(0, 0, w, h)
                                 mipmapLevel:NSUInteger(l) slice:NSUInteger(face)
                                   withBytes:pixels + size_t(face) * size_t(bytesPerImage)
                                bytesPerRow:bytesPerRow bytesPerImage:bytesPerImage];
                }
            }
        } else if (d.data && d.format != PixelFormat::Depth) {
            const int bpp = uncompressedTexturePixelBytes(d.format);
            const NSUInteger row = NSUInteger(d.width) * NSUInteger(bpp);
            const NSUInteger sliceBytes = row * NSUInteger(d.height);
            const auto* pixels = static_cast<const uint8_t*>(d.data);
            if (d.dimension == TextureDimension::Texture1D) {
                [t.tex replaceRegion:MTLRegionMake1D(0, NSUInteger(d.width)) mipmapLevel:0
                           withBytes:pixels bytesPerRow:row];
            } else if (d.dimension == TextureDimension::Texture3D) {
                [t.tex replaceRegion:MTLRegionMake3D(0, 0, 0, NSUInteger(d.width), NSUInteger(d.height), NSUInteger(d.depth))
                          mipmapLevel:0 slice:0 withBytes:pixels bytesPerRow:row bytesPerImage:sliceBytes];
            } else if (d.dimension == TextureDimension::Texture2D) {
                [t.tex replaceRegion:MTLRegionMake2D(0, 0, NSUInteger(d.width), NSUInteger(d.height))
                          mipmapLevel:0 withBytes:pixels bytesPerRow:row];
            } else {
                for (int face = 0; face < faces; ++face)
                    [t.tex replaceRegion:MTLRegionMake2D(0, 0, NSUInteger(d.width), NSUInteger(d.height))
                             mipmapLevel:0 slice:NSUInteger(face)
                               withBytes:pixels + size_t(face) * size_t(sliceBytes)
                            bytesPerRow:row bytesPerImage:sliceBytes];
            }
            if (generate && levels > 1) generateMips(t.tex);
        }
        t.mips = generate && levels > 1;
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        const bool float32Data = d.format == PixelFormat::RGBA32F ||
                                  d.format == PixelFormat::R32F ||
                                  d.format == PixelFormat::RG32F;
        const bool linear = d.filter == Filter::Linear && !float32Data;
        sd.minFilter = sd.magFilter = linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        if (levels > 1) sd.mipFilter = linear ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
        sd.sAddressMode = sd.tAddressMode = sd.rAddressMode =
            d.repeat ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
        if (d.compare) sd.compareFunction = MTLCompareFunctionLessEqual;
        t.samplerRepeat = d.repeat;
        t.samplerLinear = linear;
        t.samplerCompare = d.compare;
        t.sampler = [device_ newSamplerStateWithDescriptor:sd];
        if (!t.sampler) { error_ = "Metal: sampler allocation failed"; return {}; }
        return {textures_.add(t)};
    }

    void updateTexture(TextureHandle h, int x, int y, int w, int hh, const void* pixels, int rowPitch) override {
        auto* t = textures_.get(h.id);
        if (!t || t->dimension != TextureDimension::Texture2D || t->fmt == PixelFormat::Depth) return;
        [t->tex replaceRegion:MTLRegionMake2D(NSUInteger(x), NSUInteger(y), NSUInteger(w), NSUInteger(hh)) mipmapLevel:0
                    withBytes:pixels bytesPerRow:NSUInteger(rowPitch)];
        if (t->mips) generateMips(t->tex);
    }

    bool updateTextureRegion(TextureHandle h, const TextureUploadDesc& u) override {
        auto* t = textures_.get(h.id);
        if (!t || !u.data || u.width < 1 || u.height < 1 || u.depth < 1 ||
            u.x < 0 || u.y < 0 || u.z < 0 || u.mipLevel < 0 || u.arrayLayer < 0 ||
            t->fmt == PixelFormat::Depth || compressedFormat(t->fmt) != MTLPixelFormatInvalid)
            return false;
        const int mw = std::max(1, int(t->tex.width) >> u.mipLevel);
        const int mh = std::max(1, int(t->tex.height) >> u.mipLevel);
        const int md = t->dimension == TextureDimension::Texture3D ?
                       std::max(1, int(t->tex.depth) >> u.mipLevel) : 1;
        const int slices = t->dimension == TextureDimension::Texture2DArray ? int(t->tex.arrayLength) :
                           t->dimension == TextureDimension::CubeArray ? int(t->tex.arrayLength) * 6 :
                           t->dimension == TextureDimension::Cube ? 6 : 1;
        if (u.mipLevel >= int(t->tex.mipmapLevelCount) ||
            u.x > mw - u.width || u.y > mh - u.height || u.z > md - u.depth ||
            u.arrayLayer >= slices ||
            (t->dimension != TextureDimension::Texture3D && (u.depth != 1 || u.z != 0)) ||
            (t->dimension == TextureDimension::Texture1D && (u.y != 0 || u.height != 1)))
            return false;
        const int bpp = uncompressedTexturePixelBytes(t->fmt);
        if (u.rowPitchBytes < u.width * bpp) return false;
        const int imagePitch = u.slicePitchBytes > 0 ? u.slicePitchBytes : u.rowPitchBytes * u.height;
        if (imagePitch < u.rowPitchBytes * u.height) return false;
        // CPU-side replaceRegion may race older in-flight GPU frames that
        // still sample the overwritten texels (VT atlas slices, streamed
        // 2D/cube/3D mips). Encode the write as a blit on the SAME Metal
        // queue instead, so previous readers retire before the new bytes
        // become visible. Auto-mip textures keep replaceRegion: their mip
        // regeneration runs on a separate, immediately committed buffer.
        // Texture2DArray (VT atlas) requires the ordered path; the other
        // dimensions keep the legacy replaceRegion when an encoder is open.
        const bool arrayUpload = t->dimension == TextureDimension::Texture2DArray;
        const bool encoderOpen = enc_ || compute_ || gpuIcbEncoder_;
        const bool orderedUpload = !t->mips &&
            (arrayUpload ||
             ((t->dimension == TextureDimension::Texture2D ||
               t->dimension == TextureDimension::Cube ||
               t->dimension == TextureDimension::CubeArray ||
               t->dimension == TextureDimension::Texture3D) && !encoderOpen));
        if (orderedUpload) {
            if (encoderOpen) {
                error_ = "Metal array texture upload requires no active GPU encoder";
                return false;
            }
            const bool volume = t->dimension == TextureDimension::Texture3D;
            const NSUInteger align = 256;
            const NSUInteger sourceRow = NSUInteger(u.width) * NSUInteger(bpp);
            if (sourceRow > NSUInteger(-1) - (align-1)) {
                error_ = "Metal array upload row overflow";
                return false;
            }
            const NSUInteger gpuRow = (sourceRow + align - 1) & ~(align-1);
            if (NSUInteger(u.height) > NSUInteger(-1) / gpuRow) {
                error_ = "Metal array upload buffer overflow";
                return false;
            }
            const NSUInteger gpuImage = gpuRow * NSUInteger(u.height);
            if (NSUInteger(u.depth) > NSUInteger(-1) / gpuImage) {
                error_ = "Metal array upload buffer overflow";
                return false;
            }
            const NSUInteger total = gpuImage * NSUInteger(u.depth);
            id<MTLBuffer> staging = [device_ newBufferWithLength:total
                                     options:MTLResourceStorageModeShared];
            if (!staging) {
                error_ = "Metal array texture upload staging allocation failed";
                return false;
            }
            auto* dst = static_cast<uint8_t*>(staging.contents);
            const auto* src = static_cast<const uint8_t*>(u.data);
            for (int z = 0; z < u.depth; ++z)
                for (int row = 0; row < u.height; ++row)
                    std::memcpy(dst + size_t(z)*size_t(gpuImage) + size_t(row)*size_t(gpuRow),
                                src + size_t(z)*size_t(imagePitch) +
                                      size_t(row)*size_t(u.rowPitchBytes),
                                size_t(sourceRow));
            id<MTLCommandBuffer> uploadCmd = cmd_ ? cmd_ : [queue_ commandBuffer];
            if (!uploadCmd) {
                error_ = "Metal array texture upload command allocation failed";
                return false;
            }
            id<MTLBlitCommandEncoder> blit = [uploadCmd blitCommandEncoder];
            if (!blit) {
                error_ = "Metal array texture upload blit encoder failed";
                return false;
            }
            // sourceBytesPerImage: image stride for 3D/array, 0 otherwise.
            // Cube faces / cube-array face-layers are destination slices.
            [blit copyFromBuffer:staging sourceOffset:0
                sourceBytesPerRow:gpuRow
                sourceBytesPerImage:(volume || arrayUpload) ? gpuImage : 0
                sourceSize:MTLSizeMake(NSUInteger(u.width),NSUInteger(u.height),
                                       NSUInteger(u.depth))
                toTexture:t->tex
                destinationSlice:volume ? 0 : NSUInteger(u.arrayLayer)
                destinationLevel:NSUInteger(u.mipLevel)
                destinationOrigin:MTLOriginMake(NSUInteger(u.x),NSUInteger(u.y),
                                                NSUInteger(volume ? u.z : 0))];
            [blit endEncoding];
            // The completion block keeps staging alive until the queued
            // transfer has retired, including for standalone command buffers.
            [uploadCmd addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                (void)staging;
                (void)completed;
            }];
            if (!cmd_) [uploadCmd commit];
            return true; // queued in order, NOT a GPU completion fence
        }
        if (t->dimension == TextureDimension::Texture1D) {
            [t->tex replaceRegion:MTLRegionMake1D(NSUInteger(u.x), NSUInteger(u.width))
                       mipmapLevel:NSUInteger(u.mipLevel) withBytes:u.data
                       bytesPerRow:NSUInteger(u.rowPitchBytes)];
        } else if (t->dimension == TextureDimension::Texture3D) {
            [t->tex replaceRegion:MTLRegionMake3D(NSUInteger(u.x), NSUInteger(u.y), NSUInteger(u.z),
                    NSUInteger(u.width), NSUInteger(u.height), NSUInteger(u.depth))
                       mipmapLevel:NSUInteger(u.mipLevel) slice:0 withBytes:u.data
                       bytesPerRow:NSUInteger(u.rowPitchBytes) bytesPerImage:NSUInteger(imagePitch)];
        } else if (t->dimension == TextureDimension::Texture2D) {
            [t->tex replaceRegion:MTLRegionMake2D(NSUInteger(u.x), NSUInteger(u.y), NSUInteger(u.width), NSUInteger(u.height))
                       mipmapLevel:NSUInteger(u.mipLevel) withBytes:u.data bytesPerRow:NSUInteger(u.rowPitchBytes)];
        } else {
            [t->tex replaceRegion:MTLRegionMake2D(NSUInteger(u.x), NSUInteger(u.y), NSUInteger(u.width), NSUInteger(u.height))
                       mipmapLevel:NSUInteger(u.mipLevel) slice:NSUInteger(u.arrayLayer)
                         withBytes:u.data bytesPerRow:NSUInteger(u.rowPitchBytes)
                      bytesPerImage:NSUInteger(imagePitch)];
        }
        if (t->mips && u.mipLevel == 0) generateMips(t->tex);
        return true;
    }
    TextureViewHandle createTextureView(TextureHandle h, const TextureViewDesc& view) override {
        MTTexture* t = textures_.get(h.id);
        if (!t || !t->tex || t->tex.sampleCount > 1) {
            error_ = "Metal texture view: invalid parent or multisampled resource";
            return {};
        }
        TextureDesc parent;
        parent.width = int(t->tex.width); parent.height = int(t->tex.height);
        parent.depth = t->dimension == TextureDimension::Texture3D ? int(t->tex.depth) : 1;
        parent.arrayLayers = t->dimension == TextureDimension::Cube ? 6 :
                             t->dimension == TextureDimension::CubeArray ? int(t->tex.arrayLength) * 6 :
                             t->dimension == TextureDimension::Texture2DArray ? int(t->tex.arrayLength) : 1;
        parent.dimension = t->dimension;
        parent.format = t->fmt;
        parent.mipLevels = int(t->tex.mipmapLevelCount);
        if (!validateTextureViewDesc(parent, view)) {
            error_ = "Metal texture view: mip, layer, aspect or view dimension invalid"; return {};
        }
        // Explicit views select a subresource and can reinterpret an array slice
        // as 2D or a cube array as one cube.
        const MTLTextureType type = textureType(view.dimension);
        id<MTLTexture> native = [t->tex newTextureViewWithPixelFormat:t->tex.pixelFormat
                                 textureType:type
                                 levels:NSMakeRange(NSUInteger(view.baseMipLevel), NSUInteger(view.mipLevelCount))
                                 slices:NSMakeRange(NSUInteger(view.baseArrayLayer), NSUInteger(view.arrayLayerCount))];
        if (!native) { error_ = "Metal texture view: native allocation rejected"; return {}; }
        return {views_.add({native, h.id})};
    }
    void destroyTextureView(TextureViewHandle h) override { views_.remove(h.id); }

    SamplerHandle createSampler(const SamplerDesc& d) override {
        if (!validateSamplerDesc(d) || d.lodBias != 0.0f) {
            error_ = "Metal sampler: invalid descriptor or unsupported LOD bias"; return {};
        }
        if (d.addressU == SamplerAddressMode::ClampToBorder ||
            d.addressV == SamplerAddressMode::ClampToBorder ||
            d.addressW == SamplerAddressMode::ClampToBorder) {
            // Border color is only available for newer Metal families.
            if (@available(macOS 10.12, iOS 14.0, *)) {}
            else { error_ = "Metal sampler: border clamp unavailable on this OS"; return {}; }
        }
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = d.minFilter == Filter::Linear ?
                       MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        sd.magFilter = d.magFilter == Filter::Linear ?
                       MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        sd.mipFilter = d.mipFilter == SamplerMipFilter::None ? MTLSamplerMipFilterNotMipmapped :
                       d.mipFilter == SamplerMipFilter::Linear ? MTLSamplerMipFilterLinear :
                       MTLSamplerMipFilterNearest;
        sd.sAddressMode = addressMode(d.addressU);
        sd.tAddressMode = addressMode(d.addressV);
        sd.rAddressMode = addressMode(d.addressW);
        sd.lodMinClamp = d.minLod; sd.lodMaxClamp = d.maxLod;
        sd.maxAnisotropy = NSUInteger(std::max(1, std::min(16, int(d.maxAnisotropy))));
        sd.compareFunction = d.compare ? compareFunction(d.comparison) : MTLCompareFunctionNever;
        id<MTLSamplerState> native = [device_ newSamplerStateWithDescriptor:sd];
        if (!native) { error_ = "Metal sampler: native allocation failed"; return {}; }
        return {samplers_.add(native)};
    }
    void destroySampler(SamplerHandle h) override { samplers_.remove(h.id); }

    bool bindTextureView(int slot, TextureViewHandle h, SamplerHandle sampler = {}) override {
        if (!enc_ || slot < 0 || slot >= 16) return false;
        MTTextureView* view = views_.get(h.id);
        if (!view || !view->tex) return false;
        MTTexture* parent = textures_.get(view->parentId);
        if (!parent) return false;
        id<MTLSamplerState> state = sampler.valid() ? (samplers_.get(sampler.id) ?
            *samplers_.get(sampler.id) : nil) : parent->sampler;
        if (!state) return false;
        icbFragmentTextures_[slot] = view->tex;
        icbFragmentSamplers_[slot] = state;
        if (bindingCacheEnabled_ && boundTextures_[slot] == view->tex &&
            boundSamplers_[slot] == state) return true;
        [enc_ setFragmentTexture:view->tex atIndex:NSUInteger(slot)];
        [enc_ setVertexTexture:view->tex atIndex:NSUInteger(slot)];
        [enc_ setFragmentSamplerState:state atIndex:NSUInteger(slot)];
        [enc_ setVertexSamplerState:state atIndex:NSUInteger(slot)];
        if (bindingCacheEnabled_) {
            boundTextures_[slot] = view->tex;
            boundSamplers_[slot] = state;
        }
        return true;
    }

    // Own command buffer: textures are usually created while loading, outside of a frame.
    void generateMips(id<MTLTexture> tex) {
        id<MTLCommandBuffer> cb = [queue_ commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit generateMipmapsForTexture:tex];
        [blit endEncoding];
        [cb commit];
    }
    bool textureInfo(TextureHandle h, TextureInfo& out) const override {
        const auto* t = textures_.get(h.id);
        if (!t || !t->tex) return false;
        out = {int(t->tex.width),int(t->tex.height),t->fmt};
        return true;
    }
    void destroyTexture(TextureHandle h) override {
        // Parent/view ownership: remove children before allowing texture handle reuse.
        std::vector<uint32_t> dependent;
        views_.each([&](uint32_t id, MTTextureView& v) {
            if (v.parentId == h.id) dependent.push_back(id);
        });
        for (uint32_t id : dependent) views_.remove(id);
        clearBindings();
        textures_.remove(h.id);
    }

    // ---------- shaders / pipelines ----------
    ShaderHandle createShader(const ShaderDesc& d) override {
        if (!d.msl) { error_ = "shader has no MSL source"; return {}; }
        NSError* err = nil;
        const auto source=colorShaderPrefix(true,d.linearTarget)+d.msl;
        id<MTLLibrary> lib=shaderAssets_->library(source);
        if(!lib && !shaderAssets_->strict()) lib=[device_ newLibraryWithSource:[NSString stringWithUTF8String:source.c_str()] options:nil error:&err];
        if (!lib) { error_ = std::string("MSL compile: ") + (err ? err.localizedDescription.UTF8String : "?"); return {}; }
        MTShader s;
        s.source = d.msl; s.linear = d.linearTarget;
        s.vs = [lib newFunctionWithName:@"vs_main"];
        s.fs = [lib newFunctionWithName:@"fs_main"]; // optional for depth-only shaders
        if (!s.vs) { error_ = "MSL: vs_main not found"; return {}; }
        return {shaders_.add(s)};
    }
    ShaderJob createShaderAsync(const ShaderDesc& d) override {
        if (!d.msl) return Device::createShaderAsync(d);
        // Blend/depth states share the same program. Submit one library compilation per
        // source/colour-space pair, even when many pipeline jobs request it concurrently.
        const auto key=std::make_pair(std::string(d.msl),d.linearTarget);
        auto& cached=asyncShaderCompiles_[key];
        const bool start=!cached;
        if (start) cached=std::make_shared<MTShaderCompilation>();
        auto result=cached;
        if (start) {
        result->value.source=d.msl; result->value.linear=d.linearTarget;
        const auto text=colorShaderPrefix(true,d.linearTarget)+d.msl;
        id<MTLLibrary> precompiled=shaderAssets_->library(text);
        if(precompiled) {
            result->value.vs=[precompiled newFunctionWithName:@"vs_main"];
            result->value.fs=[precompiled newFunctionWithName:@"fs_main"];
            if(!result->value.vs)result->error="precompiled library: vs_main not found";
            result->done.store(true,std::memory_order_release);
        } else if(shaderAssets_->strict()) {
            result->error="required precompiled Metal library missing";
            result->done.store(true,std::memory_order_release);
        } else {
        NSString* source=[NSString stringWithUTF8String:text.c_str()];
        [device_ newLibraryWithSource:source options:nil completionHandler:^(id<MTLLibrary> library, NSError* error) {
            if (library) {
                result->value.vs=[library newFunctionWithName:@"vs_main"];
                result->value.fs=[library newFunctionWithName:@"fs_main"];
            }
            if (!result->value.vs) result->error=error ? error.localizedDescription.UTF8String : "MSL: vs_main not found";
            result->done.store(true,std::memory_order_release);
        }];
        }
        }
        // Each caller owns a distinct handle; shared compiled Metal functions are retained.
        auto published=std::make_shared<ShaderHandle>();
        return [this,result,published](ShaderHandle& out) {
            if (!result->done.load(std::memory_order_acquire)) return false;
            if (!result->error.empty()) { error_=result->error; out={}; return true; }
            if (!published->valid()) *published={shaders_.add(result->value)};
            out=*published; return true;
        };
    }
    void destroyShader(ShaderHandle h) override { shaders_.remove(h.id); }

    MTLRenderPipelineDescriptor* pipelineDescriptor(const PipelineDesc& d, const MTShader* s) {
        MTLVertexDescriptor* vd = [MTLVertexDescriptor vertexDescriptor];
        for (int i = 0; i < d.attrCount; ++i) {
            vd.attributes[d.attrs[i].location].format = toMTL(d.attrs[i].format);
            vd.attributes[d.attrs[i].location].offset = d.attrs[i].offset;
            vd.attributes[d.attrs[i].location].bufferIndex = 0;
        }
        vd.layouts[0].stride = d.stride;
        vd.layouts[0].stepFunction = MTLVertexStepFunctionPerVertex;

        MTLRenderPipelineDescriptor* pd = [MTLRenderPipelineDescriptor new];
        pd.vertexFunction = s->vs;
        pd.fragmentFunction = s->fs; // depth-only shaders without fs_main get nil (no fragment stage)
        pd.vertexDescriptor = vd;
        pd.rasterSampleCount = NSUInteger(std::max(1, d.sampleCount));
        // Metal requires opt-in at pipeline creation for pipeline states
        // referenced by indirect render commands. Without this the driver may
        // silently discard ICB draws or fail an entire command buffer.
        // Classic ICB commands cannot encode direct fragment textures/samplers.
        // Only opt depth-only pipelines into ICB. PBR colour pipelines continue
        // through direct/instanced draws until shader textures use argument buffers.
        if (supportsMultiDrawIndirect() && s->fs == nil) pd.supportIndirectCommandBuffers = YES;
        // GPU-created ICBs inherit the regular PBR parent pipeline. The
        // feature is explicitly opt-in and never affects production shaders.
        if (std::getenv("AIVERSE_GPU_ENCODED_ICB") &&
            std::strcmp(std::getenv("AIVERSE_GPU_ENCODED_ICB"), "1") == 0 &&
            supportsMultiDrawIndirect())
            pd.supportIndirectCommandBuffers = YES;
        auto nativeColor = [&](PixelFormat format) {
            return format == PixelFormat::RGBA16F ? MTLPixelFormatRGBA16Float :
                   format == PixelFormat::RGBA8_sRGB ? MTLPixelFormatBGRA8Unorm_sRGB :
                   layer_.pixelFormat;
        };
        const int slots = d.depthOnly ? 0 : d.colorAttachmentCount;
        for (int colorSlot = 0; colorSlot < slots; ++colorSlot) {
            const PixelFormat color = colorSlot == 0 ? d.colorFormat :
                                      d.additionalColorFormats[colorSlot - 1];
            pd.colorAttachments[colorSlot].pixelFormat = nativeColor(color);
        }
        pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        for (int colorSlot = 0; colorSlot < slots; ++colorSlot) {
        auto* ca = pd.colorAttachments[colorSlot];
        if (!d.colorWrite) ca.writeMask = MTLColorWriteMaskNone;
        if (d.blend == BlendMode::Multiply) {          // dst * src colour, dst alpha kept
            ca.blendingEnabled = YES;
            ca.rgbBlendOperation = ca.alphaBlendOperation = MTLBlendOperationAdd;
            ca.sourceRGBBlendFactor = MTLBlendFactorDestinationColor;
            ca.destinationRGBBlendFactor = MTLBlendFactorZero;
            ca.sourceAlphaBlendFactor = MTLBlendFactorZero;
            ca.destinationAlphaBlendFactor = MTLBlendFactorOne;
        } else if (d.blend == BlendMode::Subtract) {   // dst - src * srcAlpha
            ca.blendingEnabled = YES;
            ca.rgbBlendOperation = MTLBlendOperationReverseSubtract;
            ca.alphaBlendOperation = MTLBlendOperationAdd;
            ca.sourceRGBBlendFactor = MTLBlendFactorSourceAlpha;
            ca.destinationRGBBlendFactor = MTLBlendFactorOne;
            ca.sourceAlphaBlendFactor = MTLBlendFactorZero;
            ca.destinationAlphaBlendFactor = MTLBlendFactorOne;
        } else if (d.blend == BlendMode::Screen) {     // src + dst * (1 - src colour), alpha premultiplied
            ca.blendingEnabled = YES;
            ca.rgbBlendOperation = ca.alphaBlendOperation = MTLBlendOperationAdd;
            ca.sourceRGBBlendFactor = MTLBlendFactorOne;
            ca.destinationRGBBlendFactor = MTLBlendFactorOneMinusSourceColor;
            ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
            ca.destinationAlphaBlendFactor = MTLBlendFactorOneMinusSourceAlpha;
        } else if (d.blend != BlendMode::Opaque) {
            ca.blendingEnabled = YES;
            ca.rgbBlendOperation = ca.alphaBlendOperation = MTLBlendOperationAdd;
            ca.sourceRGBBlendFactor = d.blend == BlendMode::Premultiplied ? MTLBlendFactorOne : MTLBlendFactorSourceAlpha;
            ca.destinationRGBBlendFactor = d.blend == BlendMode::Additive ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
            ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
            ca.destinationAlphaBlendFactor = d.blend == BlendMode::Additive ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
        }
        }
        shaderAssets_->prepare(pd);
        return pd;
    }
    void prepareIcbPbrPipeline(MTPipeline& pipeline, const MTShader* shader) {
        // ICB colour requires all sampled textures/samplers inside an argument
        // buffer. Preserve conventional pipelines; build this optional variant
        // only for rigid built-in model shaders, on explicit experimental opt-in.
        if (!supportsMultiDrawIndirect() || !shader || !shader->fs ||
            pipeline.desc.depthOnly || shaderAssets_->strict() ||
            device_.argumentBuffersSupport < MTLArgumentBuffersTier2) return;
        const auto& src = shader->source;
        if(src.find("#define DYNAMIC_GI 1")!=std::string::npos &&
           src.find("#define AIVERSE_MODEL_VT 1")!=std::string::npos) return;
        if (src.find("#define AIVERSE_MODEL_SSR 1") == std::string::npos ||
            src.find("#define LIGHTMAP 1") != std::string::npos ||
            src.find("#define PROBES 1") != std::string::npos ||
            src.find("#define SKINNED 1") != std::string::npos ||
            src.find("#define BAKED 1") != std::string::npos ||
            src.find("#define MORPH 1") != std::string::npos ||
            src.find("#define MATERIAL_PALETTE 1") != std::string::npos ||
            (src.find("#define SG_SAMPLE(") != std::string::npos &&
             src.find("#define AIVERSE_SG_ICB_GRAPH 1") == std::string::npos))
            return;

        auto& cached = icbPbrShaders_[{src, shader->linear}];
        if (!cached.attempted) {
            cached.attempted = true;
            const std::string variant = colorShaderPrefix(true,shader->linear) +
                                        "#define AIVERSE_ICB_PBR 1\n" + src;
            NSError* err = nil;
            id<MTLLibrary> library = [device_ newLibraryWithSource:
                [NSString stringWithUTF8String:variant.c_str()] options:nil error:&err];
            if (library) cached.fragment = [library newFunctionWithName:@"fs_main"];
            if (!cached.fragment && std::getenv("AIVERSE_ICB_TRACE"))
                std::fprintf(stderr,"[metal-icb] PBR variant failed: %s\n",
                             err ? err.localizedDescription.UTF8String : "missing fs_main");
        }
        if (!cached.fragment) return;
        MTLRenderPipelineDescriptor* pd = pipelineDescriptor(pipeline.desc,shader);
        pd.fragmentFunction = cached.fragment;
        pd.supportIndirectCommandBuffers = YES;
        NSError* error = nil;
        auto state = [device_ newRenderPipelineStateWithDescriptor:pd error:&error];
        if (!state) {
            if (std::getenv("AIVERSE_ICB_TRACE"))
                std::fprintf(stderr,"[metal-icb] PBR pipeline rejected: %s\n",
                             error ? error.localizedDescription.UTF8String : "unknown");
            return;
        }
        auto encoder = [cached.fragment newArgumentEncoderWithBufferIndex:8];
        if (!encoder) return;
        pipeline.icbDynamicGi = src.find("#define DYNAMIC_GI 1") != std::string::npos;
        pipeline.icbPbrState = state;
        pipeline.icbPbrEncoder = encoder;
    }
    PipelineHandle createPipeline(const PipelineDesc& d) override {
        if (d.colorAttachmentCount < 1 || d.colorAttachmentCount > kMaxRenderTargetColors) {
            error_ = "Metal pipeline: color attachment count must be 1..4";
            return {};
        }
        if (d.sampleCount < 1 || ![device_ supportsTextureSampleCount:NSUInteger(d.sampleCount)]) {
            error_ = "Metal pipeline: unsupported raster sample count";
            return {};
        }
        auto* s = shaders_.get(d.shader.id);
        if (!s) { error_ = "pipeline: invalid shader"; return {}; }
        auto* pd = pipelineDescriptor(d, s);
        NSError* err = nil;
        MTPipeline p;
        p.desc = d;
        p.state = [device_ newRenderPipelineStateWithDescriptor:pd options:shaderAssets_->options() reflection:nil error:&err];
        // Classic Metal ICB cannot bind direct fragment textures/samplers.
        // PBR material shaders use tex1..tex13, so Metal rejects an ICB-enabled
        // pipeline. Retry the normal pipeline rather than breaking the Scene.
        if (!p.state && pd.supportIndirectCommandBuffers) {
            pd.supportIndirectCommandBuffers = NO;
            err = nil;
            p.state = [device_ newRenderPipelineStateWithDescriptor:pd options:shaderAssets_->options() reflection:nil error:&err];
        }
        if (!p.state) { ++*shaderAssets_->failureCounter(); error_ = std::string("pipeline: ") + (err ? err.localizedDescription.UTF8String : "?"); std::fprintf(stderr,"[metal-assets] pipeline failed: %s\n",error_.c_str()); if(shaderAssets_->strict())MetalShaderAssets::requiredFailure("pipeline archive miss"); return {}; }
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = d.depthTest ? MTLCompareFunctionLessEqual : MTLCompareFunctionAlways;
        dd.depthWriteEnabled = d.depthWrite ? YES : NO;
        p.depth = [device_ newDepthStencilStateWithDescriptor:dd];
        prepareIcbPbrPipeline(p, s);
        return {pipelines_.add(p)};
    }
    PipelineJob createPipelineAsync(const PipelineDesc& d) override {
        if (d.colorAttachmentCount < 1 || d.colorAttachmentCount > kMaxRenderTargetColors) {
            return Device::createPipelineAsync(d);
        }
        if (d.sampleCount < 1 || ![device_ supportsTextureSampleCount:NSUInteger(d.sampleCount)]) {
            return Device::createPipelineAsync(d);
        }
        auto* shader=shaders_.get(d.shader.id);
        if (!shader) return Device::createPipelineAsync(d);
        struct Result { MTPipeline value; std::string error; std::atomic<bool> done{false}; PipelineHandle published; };
        auto result=std::make_shared<Result>();
        result->value.desc=d; result->value.desc.label=nullptr;
        MTLDepthStencilDescriptor* depth=[MTLDepthStencilDescriptor new];
        depth.depthCompareFunction=d.depthTest ? MTLCompareFunctionLessEqual : MTLCompareFunctionAlways;
        depth.depthWriteEnabled=d.depthWrite;
        result->value.depth=[device_ newDepthStencilStateWithDescriptor:depth];
        const auto failures=shaderAssets_->failureCounter();
        const bool strict=shaderAssets_->strict();
        // If the indirect-enabled PBR pipeline is rejected (fragment texture
        // arguments are not encodable in classic ICB), asynchronously compile
        // the ordinary pipeline instead. Report failure only if both fail.
        MTLRenderPipelineDescriptor* pd = pipelineDescriptor(d,shader);
        [device_ newRenderPipelineStateWithDescriptor:pd options:shaderAssets_->options()
            completionHandler:^(id<MTLRenderPipelineState> state, MTLRenderPipelineReflection*, NSError* firstError) {
            if (!state && pd.supportIndirectCommandBuffers) {
                pd.supportIndirectCommandBuffers = NO;
                [device_ newRenderPipelineStateWithDescriptor:pd options:shaderAssets_->options()
                    completionHandler:^(id<MTLRenderPipelineState> fallback, MTLRenderPipelineReflection*, NSError* fallbackError) {
                    result->value.state = fallback;
                    if (!fallback) {
                        ++*failures;
                        result->error = fallbackError ? fallbackError.localizedDescription.UTF8String : "Metal fallback pipeline compilation failed";
                        std::fprintf(stderr,"[metal-assets] pipeline failed: %s\\n",result->error.c_str());
                        if (strict) MetalShaderAssets::requiredFailure("pipeline archive miss");
                    }
                    result->done.store(true,std::memory_order_release);
                }];
                return;
            }
            result->value.state=state;
            if (!state) {
                ++*failures;
                result->error=firstError ? firstError.localizedDescription.UTF8String : "Metal pipeline compilation failed";
                std::fprintf(stderr,"[metal-assets] pipeline failed: %s\\n",result->error.c_str());
                if(strict)MetalShaderAssets::requiredFailure("pipeline archive miss");
            }
            result->done.store(true,std::memory_order_release);
        }];
        return [this,result](PipelineHandle& out) {
            if (!result->done.load(std::memory_order_acquire)) return false;
            if (!result->error.empty()) { error_=result->error; out={}; return true; }
            if (!result->published.valid()) {
                prepareIcbPbrPipeline(result->value, shaders_.get(result->value.desc.shader.id));
                result->published = {pipelines_.add(std::move(result->value))};
            }
            out=result->published; return true;
        };
    }
    void destroyPipeline(PipelineHandle h) override {
        if (auto* p = pipelines_.get(h.id)) {
            const auto variant = p->hdr; const auto shader = p->hdrShader;
            pipelines_.remove(h.id);
            if (variant.valid()) destroyPipeline(variant);
            if (shader.valid()) destroyShader(shader);
        }
    }
    // Old bool API retains terminal-completion semantics for existing clients;
    // use the explicit status API to distinguish a GPU compiler rejection.
    bool warmPipelineColor(PipelineHandle h, PixelFormat format) override {
        return warmPipelineColorStatus(h, format) != PipelineColorWarmStatus::Pending;
    }
    PipelineColorWarmStatus warmPipelineColorStatus(PipelineHandle h, PixelFormat format) override {
        auto* p=pipelines_.get(h.id);
        if (!p) { error_="HDR warmup: invalid pipeline handle"; return PipelineColorWarmStatus::Failed; }
        if (format!=PixelFormat::RGBA16F || p->desc.depthOnly ||
            p->desc.colorFormat==format) return PipelineColorWarmStatus::Ready;
        if (p->hdr.valid()) return PipelineColorWarmStatus::Ready;
        if (p->hdrFailed) return PipelineColorWarmStatus::Failed;
        if (!p->hdrShader.valid()) {
            if (!p->hdrShaderJob) {
                auto* shader=shaders_.get(p->desc.shader.id);
                if (!shader) {
                    error_="HDR warmup: missing source shader";
                    p->hdrFailed=true;
                    return PipelineColorWarmStatus::Failed;
                }
                ShaderDesc desc; desc.msl=shader->source.c_str(); desc.linearTarget=true;
                p->hdrShaderJob=createShaderAsync(desc);
            }
            if (!p->hdrShaderJob(p->hdrShader)) return PipelineColorWarmStatus::Pending;
            p->hdrShaderJob={};
            if (!p->hdrShader.valid()) {
                p->hdrFailed=true;
                std::fprintf(stderr,"[gfx] HDR warmup shader: %s\n",error_.c_str());
                return PipelineColorWarmStatus::Failed;
            }
        }
        if (!p->hdrPipelineJob) {
            auto desc=p->desc; desc.shader=p->hdrShader; desc.colorFormat=format;
            p->hdrPipelineJob=createPipelineAsync(desc);
        }
        // Publishing may reallocate the pool, including the job itself. Poll a local copy.
        auto job=p->hdrPipelineJob; PipelineHandle result;
        if (!job(result)) return PipelineColorWarmStatus::Pending;
        p=pipelines_.get(h.id);
        if (!p) {
            // A destroyed base pipeline cannot publish its asynchronous variant.
            if (result.valid()) destroyPipeline(result);
            error_="HDR warmup: pipeline retired before variant completion";
            return PipelineColorWarmStatus::Failed;
        }
        p->hdrPipelineJob={}; p->hdr=result; p->hdrFailed=!result.valid();
        if (p->hdrFailed)
            std::fprintf(stderr,"[gfx] HDR warmup pipeline: %s\n",error_.c_str());
        return p->hdrFailed ? PipelineColorWarmStatus::Failed
                            : PipelineColorWarmStatus::Ready;
    }
    PipelineHandle hdrPipeline(PipelineHandle h) {
        auto* p = pipelines_.get(h.id);
        if (!p) return {};
        if (p->hdr.valid()) return p->hdr;
        if (p->hdrFailed) return {};
        p->hdrFailed = true; // compile at most once; a broken variant must not stall every frame
        PipelineDesc pd = p->desc;
        auto* s = shaders_.get(pd.shader.id);
        if (!s) return {};
        ShaderDesc sd;
        const std::string source = s->source;
        sd.msl = source.c_str();
        sd.linearTarget = true;
        const auto shader = createShader(sd);
        if (!shader.valid()) { std::fprintf(stderr, "[gfx] HDR shader: %s\n", error_.c_str()); return {}; }
        pd.shader = shader; pd.colorFormat = PixelFormat::RGBA16F;
        const auto variant = createPipeline(pd);
        if (!variant.valid()) { std::fprintf(stderr, "[gfx] HDR pipeline: %s\n", error_.c_str()); destroyShader(shader); return {}; }
        p = pipelines_.get(h.id); // pool may have reallocated
        p->hdr = variant; p->hdrShader = shader; p->hdrFailed = false;
        return variant;
    }

    // ---------- frame ----------
    // ---------- capture (tools) ----------
    CaptureFn capture_;              // requested, waiting for a readable drawable
    int captureDelay_ = 0;
    struct Captured { int w = 0, h = 0; std::vector<uint8_t> px; std::atomic<bool> ready{false}; CaptureFn fn; };
    std::shared_ptr<Captured> captured_;
    std::vector<std::shared_ptr<Captured>> capturedTextures_;
    void captureNextFrame(CaptureFn fn) override {
        capture_ = std::move(fn);
        layer_.framebufferOnly = NO; // drawables acquired from now on can be blitted
        captureDelay_ = drawable_ && !drawable_.texture.framebufferOnly ? 0 : 1;
    }
    bool captureTexture(TextureHandle source,CaptureFn fn) override {
        auto* resource=textures_.get(source.id);
        if(!fn||!cmd_||enc_||!resource||!resource->tex)return false;
        id<MTLTexture> tex=resource->tex;
        const bool bgra=tex.pixelFormat==MTLPixelFormatBGRA8Unorm ||
                        tex.pixelFormat==MTLPixelFormatBGRA8Unorm_sRGB;
        const bool rgba=tex.pixelFormat==MTLPixelFormatRGBA8Unorm ||
                        tex.pixelFormat==MTLPixelFormatRGBA8Unorm_sRGB;
        if(!bgra&&!rgba)return false; // HDR requires explicit tonemap first
        const NSUInteger width=tex.width,height=tex.height,row=width*4;
        if(width==0||height==0||width>8192||height>8192)return false;
        id<MTLBuffer> buffer=[device_ newBufferWithLength:row*height
            options:MTLResourceStorageModeShared];
        if(!buffer)return false;
        id<MTLBlitCommandEncoder> blit=[cmd_ blitCommandEncoder];
        [blit copyFromTexture:tex sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
             sourceSize:MTLSizeMake(width,height,1) toBuffer:buffer destinationOffset:0
             destinationBytesPerRow:row destinationBytesPerImage:row*height];
        [blit endEncoding];
        auto request=std::make_shared<Captured>();
        request->w=int(width);request->h=int(height);request->fn=std::move(fn);
        capturedTextures_.push_back(request);
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> finished) {
            if(finished.status==MTLCommandBufferStatusCompleted) {
                const auto* bytes=static_cast<const uint8_t*>(buffer.contents);
                request->px.resize(size_t(row*height));
                for(size_t i=0;i<size_t(width*height);++i){
                    request->px[i*4]=bytes[i*4+(bgra?2:0)];
                    request->px[i*4+1]=bytes[i*4+1];
                    request->px[i*4+2]=bytes[i*4+(bgra?0:2)];
                    request->px[i*4+3]=bytes[i*4+3];
                }
            }else{request->w=0;request->h=0;}
            request->ready=true;
        }];
        return true;
    }
    void deliverTextureCaptures() {
        for(size_t i=0;i<capturedTextures_.size();) {
            auto capture=capturedTextures_[i];
            if(!capture->ready){++i;continue;}
            capturedTextures_.erase(capturedTextures_.begin()+std::ptrdiff_t(i));
            if(capture->fn)capture->fn(capture->w,capture->h,std::move(capture->px));
        }
    }
    void deliverCapture() {
        if (captured_ && captured_->ready) {
            auto c = captured_;
            captured_.reset();
            layer_.framebufferOnly = screenReadable_ ? NO : YES;
            if (c->fn) c->fn(c->w, c->h, std::move(c->px));
        }
    }

    void setScreenReadable(bool readable) override {
        screenReadable_ = readable;
        if (!capture_ && !captured_) layer_.framebufferOnly = readable ? NO : YES;
    }
    bool copyColor(TextureHandle from, TextureHandle to) override {
        auto* dst = textures_.get(to.id);
        if (!cmd_ || enc_ || !dst) return false;
        id<MTLTexture> src = nil;
        if (from.valid()) { if (auto* s = textures_.get(from.id)) src = s->tex; }
        else if (drawable_) src = drawable_.texture;
        if (!src || src.framebufferOnly || src.pixelFormat != dst->tex.pixelFormat ||
            src.width != dst->tex.width || src.height != dst->tex.height) return false;
        id<MTLBlitCommandEncoder> blit = [cmd_ blitCommandEncoder];
        [blit copyFromTexture:src sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(src.width, src.height, 1)
                    toTexture:dst->tex destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
        return true;
    }

    bool readColorRGBA8(TextureHandle h, std::vector<uint8_t>& rgba) override {
        rgba.clear();
        // Diagnostic-only blocking readback: never interrupt an active frame,
        // encoder, or the normal asynchronous particle render path.
        if (cmd_ || enc_ || compute_ || gpuIcbEncoder_ || !queue_) return false;
        const auto* item = textures_.get(h.id);
        if (!item || !item->tex) return false;
        const auto format = item->tex.pixelFormat;
        const bool bgra = format == MTLPixelFormatBGRA8Unorm ||
                          format == MTLPixelFormatBGRA8Unorm_sRGB;
        const bool rawRGBA = format == MTLPixelFormatRGBA8Unorm ||
                             format == MTLPixelFormatRGBA8Unorm_sRGB;
        if (!bgra && !rawRGBA) return false;
        const size_t w = item->tex.width, hgt = item->tex.height;
        if (!w || !hgt || w > SIZE_MAX / 4u || hgt > SIZE_MAX / (w * 4u))
            return false;
        // 256-byte rows satisfy Metal texture->buffer blit alignment for
        // arbitrary offscreen viewport widths, not just the test's 128 px.
        const size_t pitch = ((w * 4u + 255u) / 256u) * 256u;
        if (hgt > SIZE_MAX / pitch) return false;
        id<MTLBuffer> buffer = [device_ newBufferWithLength:pitch * hgt
                            options:MTLResourceStorageModeShared];
        if (!buffer) return false;
        id<MTLCommandBuffer> command = [queue_ commandBuffer];
        if (!command) return false;
        id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
        if (!blit) return false;
        [blit copyFromTexture:item->tex sourceSlice:0 sourceLevel:0
                  sourceOrigin:MTLOriginMake(0, 0, 0)
                    sourceSize:MTLSizeMake(w, hgt, 1)
                      toBuffer:buffer destinationOffset:0
         destinationBytesPerRow:pitch destinationBytesPerImage:pitch * hgt];
        [blit endEncoding];
        [command commit];
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted) {
            error_ = command.error ? command.error.localizedDescription.UTF8String
                                   : "diagnostic color readback incomplete";
            return false;
        }
        rgba.resize(w * hgt * 4u);
        const auto* src = static_cast<const uint8_t*>(buffer.contents);
        for (size_t y = 0; y < hgt; ++y) {
            for (size_t x = 0; x < w; ++x) {
                const size_t si = y * pitch + x * 4u;
                const size_t di = (y * w + x) * 4u;
                rgba[di] = src[si + (bgra ? 2 : 0)];
                rgba[di + 1] = src[si + 1];
                rgba[di + 2] = src[si + (bgra ? 0 : 2)];
                rgba[di + 3] = src[si + 3];
            }
        }
        return true;
    }

    void beginFrame(int w, int h) override {
        fbW_ = w; fbH_ = h;
        dispatch_semaphore_wait(frameSemaphore_, DISPATCH_TIME_FOREVER);
        deliverBufferReads();
        deliverDepthReads();
        deliverColorReads();
        deliverTextureReads();
        deliverCapture(); // the previous frame's GPU work is done here
        deliverTextureCaptures();
        ++frame_;
        // Programmatic .gputrace for one explicit frame. Capture must begin
        // BEFORE commandBuffer creation; beginning in the fixture callback
        // would be too late for the already-created command buffer.
        maybeStartGpuTrace();
        if (modelCounterEnabled_) {
            std::fill(std::begin(modelCounterMarked_), std::end(modelCounterMarked_), false);
            modelCounterCurrent_ = nil;
            if (@available(macOS 11.0, iOS 14.0, *)) {
                MTLCounterSampleBufferDescriptor* descriptor =
                    [MTLCounterSampleBufferDescriptor new];
                descriptor.counterSet = modelTimestampCounterSet_;
                descriptor.sampleCount = 4;
                descriptor.storageMode = MTLStorageModeShared;
                NSError* counterError = nil;
                modelCounterCurrent_ = [device_ newCounterSampleBufferWithDescriptor:descriptor
                                                                              error:&counterError];
            }
        }
        // The semaphore guarantees this slot is no longer in flight.
        // Reuse its ICB and uniform allocations, but never reuse one twice
        // within the same frame (different draws need different snapshots).
        indirectCursor_[frame_ % kFramesInFlight] = 0;
        gpuEncodedIcbCursor_[frame_ % kFramesInFlight] = 0;
        uniformPage_=uniformOffset_=0;
        gpuIcbInstanceUploadedBytes_ = 0;
        layer_.drawableSize = CGSizeMake(w, h);
        drawable_ = [layer_ nextDrawable];
        if (!depth_ || int(depth_.width) != w || int(depth_.height) != h) {
            MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatDepth32Float
                width:NSUInteger(w) height:NSUInteger(h) mipmapped:NO];
            td.usage = MTLTextureUsageRenderTarget;
            td.storageMode = MTLStorageModePrivate;
            depth_ = [device_ newTextureWithDescriptor:td];
        }
        cmd_ = [queue_ commandBuffer];
        __block dispatch_semaphore_t sem = frameSemaphore_;
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer>) { dispatch_semaphore_signal(sem); }];
        const uint64_t gpuToken = frame_;
        auto completed = completedGpuFrame_;
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> command) {
            // Callback runs after the GPU command buffer has actually
            // finished. Command buffers from this Metal queue complete in
            // submission order; never substitute frame_ on the CPU thread.
            if (command.status != MTLCommandBufferStatusCompleted) return;
            uint64_t seen = completed->load(std::memory_order_relaxed);
            while (seen < gpuToken &&
                   !completed->compare_exchange_weak(
                       seen,gpuToken,std::memory_order_release,
                       std::memory_order_relaxed)) {}
        }];
        auto timing=gpuMs_;auto health=gpuHealthy_;
        auto completedSerial=completedFrame_;
        const uint64_t submittedFrame=frame_;
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            if(completed.status==MTLCommandBufferStatusError) {
                health->store(false);
                const char* description = completed.error ? completed.error.localizedDescription.UTF8String : "unknown";
                std::fprintf(stderr, "[metal] command buffer failed: %s\\n", description);
            }
            if(completed.status==MTLCommandBufferStatusCompleted) {
                timing->store((completed.GPUEndTime-completed.GPUStartTime)*1000);
                // Only a completed queue submission may release quarantined
                // geometry spans. One Metal queue orders all model uploads
                // and draws; atomically publish the highest completed frame.
                uint64_t observed=completedSerial->load(std::memory_order_relaxed);
                while(observed<submittedFrame &&
                      !completedSerial->compare_exchange_weak(
                          observed, submittedFrame,
                          std::memory_order_release,
                          std::memory_order_relaxed)) {}
            }
        }];
        firstPass_ = true;
    }
    // Dedicated GPU-hardware timestamp scope, never whole-command-buffer
    // GPUEndTime minus GPUStartTime labeled as a viewport interval.
    // A start and end sample are recorded in the SAME command buffer,
    // resolved only after that command buffer reports GPU completion.
    struct ScopedCommand {
        uint64_t tag = 0;
        id<MTLCommandBuffer> command = nil;
        id<MTLCounterSampleBuffer> sample = nil;
        MTLTimestamp cpuStart = 0, gpuStart = 0;
        bool ended = false;
    };
    std::vector<ScopedCommand> scopedCommands_;
    bool gpuScopeActive_ = false;
    bool timestampScopeChecked_ = false;
    id<MTLCounterSet> timestampCounterSet_ = nil;
    // 1 = explicit blit-command samples, 2 = blit stage-boundary samples
    int timestampScopeMode_ = 0;

    bool encodeGpuScopeTimestamp(id<MTLCounterSampleBuffer> samples,
                                 NSUInteger index) {
        if (@available(macOS 11.0, iOS 14.0, *)) {
        if (!cmd_ || enc_ || compute_ || gpuIcbEncoder_ || !samples)
            return false;
        if (timestampScopeMode_ == 1) {
            id<MTLBlitCommandEncoder> blit = [cmd_ blitCommandEncoder];
            if (!blit) return false;
            [blit sampleCountersInBuffer:samples atSampleIndex:index withBarrier:YES];
            [blit endEncoding];
            return true;
        }
        if (timestampScopeMode_ == 2) {
            MTLBlitPassDescriptor* descriptor=[MTLBlitPassDescriptor blitPassDescriptor];
            auto* attachment=descriptor.sampleBufferAttachments[0];
            attachment.sampleBuffer=samples;
            attachment.startOfEncoderSampleIndex=index;
            attachment.endOfEncoderSampleIndex=MTLCounterDontSample;
            id<MTLBlitCommandEncoder> blit=[cmd_ blitCommandEncoderWithDescriptor:descriptor];
            if (!blit) return false;
            [blit endEncoding];
            return true;
        }
        }
        return false;
    }
    bool beginGpuScope(uint64_t tag) override {
        if (!cmd_ || enc_ || compute_ || gpuIcbEncoder_ ||
            gpuScopeActive_ || scopedCommands_.size()>=64 ||
            !gpuHealthy_->load(std::memory_order_acquire))
            return false;
        if (@available(macOS 11.0, iOS 14.0, *)) {
            if (!timestampScopeChecked_) {
                timestampScopeChecked_=true;
                // Do not create an unsupported counter buffer or assume a
                // driver provides nanosecond timestamps on every GPU.
                const bool blit=[device_ supportsCounterSampling:
                    MTLCounterSamplingPointAtBlitBoundary];
                const bool stage=[device_ supportsCounterSampling:
                    MTLCounterSamplingPointAtStageBoundary];
                if (blit || stage) {
                    for (id<MTLCounterSet> entry in device_.counterSets) {
                        if ([entry.name isEqualToString:MTLCommonCounterSetTimestamp]) {
                            timestampCounterSet_=entry;
                            timestampScopeMode_=blit ? 1 : 2;
                            break;
                        }
                    }
                }
            }
            if (!timestampCounterSet_ || !timestampScopeMode_) return false;
            MTLCounterSampleBufferDescriptor* desc=
                [[MTLCounterSampleBufferDescriptor alloc] init];
            desc.counterSet=timestampCounterSet_;
            desc.sampleCount=2;
            desc.storageMode=MTLStorageModeShared;
            desc.label=@"AIVERSE scoped GPU timestamps";
            NSError* error=nil;
            id<MTLCounterSampleBuffer> samples=
                [device_ newCounterSampleBufferWithDescriptor:desc error:&error];
            if (!samples) return false;
            ScopedCommand entry;
            entry.tag=tag;
            entry.command=cmd_;
            entry.sample=samples;
            [device_ sampleTimestamps:&entry.cpuStart gpuTimestamp:&entry.gpuStart];
            if (!entry.cpuStart || !entry.gpuStart ||
                !encodeGpuScopeTimestamp(samples,0))
                return false;
            scopedCommands_.push_back(entry);
            gpuScopeActive_=true;
            return true;
        }
        return false;
    }
    void endGpuScope() override {
        if (!gpuScopeActive_) return;
        gpuScopeActive_=false;
        if (scopedCommands_.empty()) return;
        auto& entry=scopedCommands_.back();
        // Encoder boundaries are a strict host contract: do not silently
        // end someone else's render/compute pass to acquire a timestamp.
        entry.ended=encodeGpuScopeTimestamp(entry.sample,1);
    }
    std::vector<GpuTiming> takeGpuScopeTimings() override {
        std::vector<GpuTiming> result;
        if (@available(macOS 11.0, iOS 14.0, *)) {
            for (size_t i=0;i<scopedCommands_.size();) {
                const auto& t=scopedCommands_[i];
                const auto status=t.command.status;
                if (status!=MTLCommandBufferStatusCompleted &&
                    status!=MTLCommandBufferStatusError) {++i;continue;}
                bool valid=false;
                double elapsedMs=0.0;
                if (t.ended && status==MTLCommandBufferStatusCompleted) {
                    NSData* data=[t.sample resolveCounterRange:NSMakeRange(0,2)];
                    if (data && data.length>=sizeof(MTLCounterResultTimestamp)*2) {
                        const auto* samples=
                            static_cast<const MTLCounterResultTimestamp*>(data.bytes);
                        MTLTimestamp cpuEnd=0,gpuEnd=0;
                        [device_ sampleTimestamps:&cpuEnd gpuTimestamp:&gpuEnd];
                        if (samples[0].timestamp>0 && samples[1].timestamp>samples[0].timestamp &&
                            samples[0].timestamp!=MTLCounterErrorValue &&
                            samples[1].timestamp!=MTLCounterErrorValue &&
                            gpuEnd>t.gpuStart && cpuEnd>t.cpuStart) {
                            // GPU clock ticks may be frequency-scaled; use
                            // TWO simultaneous CPU/GPU clock samples to
                            // convert the measured GPU delta to CPU ns.
                            const double ratio=double(cpuEnd-t.cpuStart)/
                                               double(gpuEnd-t.gpuStart);
                            elapsedMs=double(samples[1].timestamp-
                                samples[0].timestamp)*ratio/1000000.0;
                            valid=std::isfinite(elapsedMs) &&
                                elapsedMs>0.0 && elapsedMs<=250.0;
                        }
                    }
                }
                result.push_back({t.tag,valid?elapsedMs:0.0,valid});
                scopedCommands_.erase(scopedCommands_.begin()+i);
            }
        }
        return result;
    }

    // Shared lifetime across completed-handler callbacks: no blocking wait
    // and no callbacks dereferencing a destroyed MetalDevice.
    struct ModelGpuCounterResults {
        std::mutex lock;
        std::vector<ModelGpuCounter> ready;
    };
    std::shared_ptr<ModelGpuCounterResults> modelCounterResults_ =
        std::make_shared<ModelGpuCounterResults>();
    bool modelCounterEnabled_ = false;
    id<MTLCounterSet> modelTimestampCounterSet_ = nil;
    id<MTLCounterSampleBuffer> modelCounterCurrent_ = nil;
    bool modelCounterMarked_[4] = {};
    bool enableModelGpuCounters(bool enabled) override {
        modelCounterEnabled_ = false;
        modelCounterCurrent_ = nil;
        modelTimestampCounterSet_ = nil;
        if (!enabled) return true;
        if (@available(macOS 11.0, iOS 14.0, *)) {
            if (![device_ supportsCounterSampling:MTLCounterSamplingPointAtDrawBoundary])
                return false;
            for (id<MTLCounterSet> counterSet in device_.counterSets) {
                if ([counterSet.name isEqualToString:MTLCommonCounterSetTimestamp]) {
                    for (id<MTLCounter> counter in counterSet.counters)
                        if ([counter.name isEqualToString:MTLCommonCounterTimestamp]) {
                            modelTimestampCounterSet_ = counterSet;
                            modelCounterEnabled_ = true;
                            return true;
                        }
                }
            }
        }
        return false;
    }
    bool sampleModelGpuCounter(int phase, bool start) override {
        if (!modelCounterEnabled_ || !modelCounterCurrent_ || !enc_ ||
            (phase != 0 && phase != 1)) return false;
        const NSUInteger index = NSUInteger(phase * 2 + (start ? 0 : 1));
        if (modelCounterMarked_[index]) return false;
        // Draw-boundary samples are diagnostics; barrier=NO avoids an
        // intrusive GPU synchronization point that distorts the benchmark.
        [enc_ sampleCountersInBuffer:modelCounterCurrent_
                       atSampleIndex:index withBarrier:NO];
        modelCounterMarked_[index] = true;
        return true;
    }
    std::vector<ModelGpuCounter> takeModelGpuCounters() override {
        auto results = modelCounterResults_;
        std::lock_guard<std::mutex> hold(results->lock);
        std::vector<ModelGpuCounter> out;
        out.swap(results->ready);
        return out;
    }
    struct TimedCommand { uint64_t tag; id<MTLCommandBuffer> command; };
    std::vector<TimedCommand> timedCommands_;
    bool timeGpuFrame(uint64_t tag) override {
        if (!cmd_ || timedCommands_.size() >= 64) return false;
        timedCommands_.push_back({tag, cmd_});
        return true;
    }
    std::vector<GpuTiming> takeGpuTimings() override {
        std::vector<GpuTiming> out;
        for (size_t i = 0; i < timedCommands_.size();) {
            auto& t = timedCommands_[i];
            const auto status = t.command.status;
            if (status != MTLCommandBufferStatusCompleted && status != MTLCommandBufferStatusError) { ++i; continue; }
            const double start = t.command.GPUStartTime, end = t.command.GPUEndTime;
            out.push_back({t.tag, (end - start) * 1000.0, status == MTLCommandBufferStatusCompleted && start > 0 && end >= start});
            timedCommands_.erase(timedCommands_.begin() + i);
        }
        return out;
    }
    void endFrame() override {
        finishGpuEncodedIcbPreparation();
        endCompute();
        if (enc_) { [enc_ endEncoding]; enc_ = nil; }
        // Balanced even if a client omits its explicit endGpuScope.
        if (gpuScopeActive_) endGpuScope();
        if (capture_ && drawable_ && captureDelay_-- <= 0 && !drawable_.texture.framebufferOnly) {
            // copy the finished frame into a shared buffer before presenting it
            const NSUInteger w = drawable_.texture.width, h = drawable_.texture.height, row = w * 4;
            id<MTLBuffer> buf = [device_ newBufferWithLength:row * h options:MTLResourceStorageModeShared];
            id<MTLBlitCommandEncoder> blit = [cmd_ blitCommandEncoder];
            [blit copyFromTexture:drawable_.texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                       sourceSize:MTLSizeMake(w, h, 1) toBuffer:buf destinationOffset:0 destinationBytesPerRow:row destinationBytesPerImage:row * h];
            [blit endEncoding];
            auto c = std::make_shared<Captured>();
            c->w = int(w); c->h = int(h); c->fn = std::move(capture_);
            capture_ = nullptr;
            captured_ = c;
            [cmd_ addCompletedHandler:^(id<MTLCommandBuffer>) {
                const uint8_t* src = static_cast<const uint8_t*>(buf.contents);
                c->px.resize(size_t(row * h));
                for (size_t i = 0; i < size_t(w * h); ++i) { // BGRA -> RGBA
                    c->px[i * 4 + 0] = src[i * 4 + 2]; c->px[i * 4 + 1] = src[i * 4 + 1];
                    c->px[i * 4 + 2] = src[i * 4 + 0]; c->px[i * 4 + 3] = 255;
                }
                c->ready = true; // read on the main thread in the next beginFrame (after the semaphore)
            }];
        }
        if (modelCounterEnabled_ && modelCounterCurrent_) {
            // Counter buffers are resolved only after the GPU has finished
            // the command buffer; retain the sample buffer via the block.
            id<MTLCounterSampleBuffer> sampleBuffer = modelCounterCurrent_;
            auto resultQueue = modelCounterResults_;
            const uint64_t serial = frame_;
            const bool completeSet = std::all_of(
                std::begin(modelCounterMarked_), std::end(modelCounterMarked_),
                [](bool value) { return value; });
            [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> completed) {
                ModelGpuCounter output{};
                output.frame = serial;
                if (completeSet && completed.status == MTLCommandBufferStatusCompleted) {
                    NSData* resolved = [sampleBuffer resolveCounterRange:NSMakeRange(0, 4)];
                    if (resolved && resolved.length >= 4 * sizeof(MTLCounterResultTimestamp)) {
                        auto* times = static_cast<const MTLCounterResultTimestamp*>(resolved.bytes);
                        const uint64_t a = times[0].timestamp, b = times[1].timestamp;
                        const uint64_t c = times[2].timestamp, d = times[3].timestamp;
                        if (a && c && b > a && d > c &&
                            a != MTLCounterErrorValue && b != MTLCounterErrorValue &&
                            c != MTLCounterErrorValue && d != MTLCounterErrorValue) {
                            output.shadowTicks = b - a;
                            output.opaqueTicks = d - c;
                            output.valid = true;
                        }
                    }
                }
                std::lock_guard<std::mutex> hold(resultQueue->lock);
                resultQueue->ready.push_back(output);
            }];
            modelCounterCurrent_ = nil;
        }
        if (drawable_) [cmd_ presentDrawable:drawable_];
        lastCommittedGpuFrameBuffer_ = cmd_;
        lastCommittedGpuFrame_ = frame_;
        [cmd_ commit];
        if (gpuTraceActive_) {
            [[MTLCaptureManager sharedCaptureManager] stopCapture];
            gpuTraceActive_ = false;
            std::fprintf(stderr, "[metal-gputrace] STOP frame=%llu path=%s\n",
                         static_cast<unsigned long long>(frame_),
                         gpuTracePath_.c_str());
        }
        cmd_ = nil;
        drawable_ = nil;
    }

    bool bindingCacheEnabled_=true;
    id<MTLTexture> boundTextures_[32]={};
    id<MTLSamplerState> boundSamplers_[16]={};
    id<MTLBuffer> boundVertexBuffer_=nil;
    size_t boundVertexOffset_=0;
    void clearBindings(){for(auto& t:boundTextures_)t=nil;for(auto& s:boundSamplers_)s=nil;boundVertexBuffer_=nil;}
    void setBindingCacheEnabled(bool enabled) override {if(enabled!=bindingCacheEnabled_){bindingCacheEnabled_=enabled;clearBindings();}}
    bool supportsRenderTargetPasses() const override { return true; }

    bool beginRenderTargets(const RenderTargetPassDesc& desc) override {
        if (!validateRenderTargetPassDesc(desc) || !cmd_ || enc_ || compute_ || gpuIcbEncoder_) {
            error_ = "Metal: invalid render target pass or an encoder is already active";
            return false;
        }
        auto sliceCount = [](const MTTexture& t) {
            return t.dimension == TextureDimension::Cube ? 6 :
                   t.dimension == TextureDimension::CubeArray ? int(t.tex.arrayLength) * 6 :
                   t.dimension == TextureDimension::Texture2DArray ? int(t.tex.arrayLength) : 1;
        };
        auto validate = [&](const RenderTargetAttachment& a, bool wantDepth,
                            int& width, int& height) {
            const MTTexture* t = textures_.get(a.texture.id);
            if (!t || !t->tex || a.mipLevel < 0 || a.arrayLayer < 0 ||
                a.mipLevel >= int(t->tex.mipmapLevelCount) ||
                a.arrayLayer >= sliceCount(*t) ||
                !(t->tex.usage & MTLTextureUsageRenderTarget) ||
                (t->fmt == PixelFormat::Depth) != wantDepth ||
                t->dimension == TextureDimension::Texture1D ||
                t->dimension == TextureDimension::Texture3D)
                return false;
            if (!wantDepth && t->fmt != PixelFormat::RGBA8 &&
                t->fmt != PixelFormat::RGBA8_sRGB && t->fmt != PixelFormat::RGBA16F)
                return false;
            width = std::max(1, int(t->tex.width) >> a.mipLevel);
            height = std::max(1, int(t->tex.height) >> a.mipLevel);
            return width > 0 && height > 0;
        };
        int width = 0, height = 0;
        const MTTexture* colors[kMaxRenderTargetColors]{};
        const MTTexture* resolves[kMaxRenderTargetColors]{};
        const MTTexture* depth = nullptr;
        PixelFormat commonFormat = PixelFormat::RGBA8;
        int samples = 0;
        for (int i = 0; i < desc.colorCount; ++i) {
            const auto& a = desc.colors[size_t(i)];
            int w = 0, h = 0;
            if (!validate(a, false, w, h) ||
                (width && (width != w || height != h))) {
                error_ = "Metal: incompatible color attachment size/format/subresource";
                return false;
            }
            const MTTexture* color = textures_.get(a.texture.id);
            // Current HDR shader variants and blend policies are shared
            // across slots. Mixed format MRT needs explicit shader variants.
            if (i && color->fmt != commonFormat) {
                error_ = "Metal: mixed color formats are not supported by MRT shader policy";
                return false;
            }
            if (samples && samples != int(color->tex.sampleCount)) {
                error_ = "Metal: color attachment sample counts differ";
                return false;
            }
            width = w; height = h;
            samples = int(color->tex.sampleCount);
            commonFormat = color->fmt;
            colors[i] = color;
        }
        if (desc.depth.texture.valid()) {
            int w = 0, h = 0;
            if (!validate(desc.depth, true, w, h) ||
                (width && (width != w || height != h))) {
                error_ = "Metal: depth attachment size/format mismatch";
                return false;
            }
            depth = textures_.get(desc.depth.texture.id);
            if (samples && samples != int(depth->tex.sampleCount)) {
                error_ = "Metal: depth sample count differs from color";
                return false;
            }
            if (!width) { width = w; height = h; }
            if (!samples) samples = int(depth->tex.sampleCount);
        }
        if (width < 1 || height < 1 || samples < 1) return false;
        // Check every resolve before creating any render encoder, so a
        // malformed target cannot partially bind or clear the MRT pass.
        for (int i = 0; i < desc.colorCount; ++i) {
            const auto& a = desc.colors[size_t(i)];
            if (!a.resolveTexture.valid()) continue;
            const MTTexture* color = colors[i];
            const MTTexture* resolve = textures_.get(a.resolveTexture.id);
            if (samples <= 1 || !resolve || !resolve->tex ||
                resolve->tex.sampleCount != 1 ||
                resolve->tex.pixelFormat != color->tex.pixelFormat ||
                !(resolve->tex.usage & MTLTextureUsageRenderTarget) ||
                resolve->dimension == TextureDimension::Texture1D ||
                resolve->dimension == TextureDimension::Texture3D ||
                a.resolveMipLevel < 0 ||
                a.resolveMipLevel >= int(resolve->tex.mipmapLevelCount) ||
                a.resolveArrayLayer < 0 ||
                a.resolveArrayLayer >= sliceCount(*resolve) ||
                std::max(1, int(resolve->tex.width) >> a.resolveMipLevel) != width ||
                std::max(1, int(resolve->tex.height) >> a.resolveMipLevel) != height) {
                error_ = "Metal: incompatible color resolve target";
                return false;
            }
            resolves[i] = resolve;
        }
        finishGpuEncodedIcbPreparation();
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        for (int i = 0; i < desc.colorCount; ++i) {
            const auto& a = desc.colors[size_t(i)];
            auto* ca = rp.colorAttachments[i];
            ca.texture = colors[i]->tex;
            ca.level = NSUInteger(a.mipLevel);
            ca.slice = NSUInteger(a.arrayLayer);
            ca.loadAction = a.clear ? MTLLoadActionClear : MTLLoadActionLoad;
            ca.storeAction = resolves[i] ? MTLStoreActionMultisampleResolve :
                             MTLStoreActionStore;
            if (resolves[i]) {
                ca.resolveTexture = resolves[i]->tex;
                ca.resolveLevel = NSUInteger(a.resolveMipLevel);
                ca.resolveSlice = NSUInteger(a.resolveArrayLayer);
            }
            ca.clearColor = MTLClearColorMake(
                a.clearColor[0], a.clearColor[1], a.clearColor[2], a.clearColor[3]);
        }
        if (depth) {
            rp.depthAttachment.texture = depth->tex;
            rp.depthAttachment.level = NSUInteger(desc.depth.mipLevel);
            rp.depthAttachment.slice = NSUInteger(desc.depth.arrayLayer);
            rp.depthAttachment.loadAction = desc.depth.clear ? MTLLoadActionClear : MTLLoadActionLoad;
            rp.depthAttachment.storeAction = MTLStoreActionStore;
            rp.depthAttachment.clearDepth = desc.clearDepth;
        }
        id<MTLRenderCommandEncoder> encoder = [cmd_ renderCommandEncoderWithDescriptor:rp];
        if (!encoder) {
            error_ = "Metal: MRT render encoder creation failed";
            return false;
        }
        enc_ = encoder;
        passW_ = width; passH_ = height;
        explicitPassSamples_ = samples;
        explicitPassColors_ = desc.colorCount;
        explicitPassColorFormat_ = commonFormat;
        hdrPass_ = desc.colorCount && commonFormat == PixelFormat::RGBA16F;
        pipelineId_ = 0;
        clearBindings();
        [enc_ setFrontFacingWinding:MTLWindingCounterClockwise];
        return true;
    }

    void beginPass(const PassDesc& d) override {
        // Do not allow an unfinished GPU ICB compute pass to overlap a
        // render encoder, even if a future caller omits the explicit finish.
        finishGpuEncodedIcbPreparation();
        clearBindings();
        const auto* color = textures_.get(d.colorTarget.id);
        hdrPass_ = color && color->fmt == PixelFormat::RGBA16F;
        if (!cmd_) return;
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        if (modelCounterEnabled_ && modelCounterCurrent_) {
            if (@available(macOS 11.0, iOS 14.0, *))
                rp.sampleBufferAttachments[0].sampleBuffer = modelCounterCurrent_;
        }
        const bool offscreen = d.colorTarget.valid() || d.depthTarget.valid();
        passW_ = fbW_; passH_ = fbH_;
        if (offscreen) {
            auto* ct = textures_.get(d.colorTarget.id);
            auto* dt = textures_.get(d.depthTarget.id);
            id<MTLTexture> any = ct ? ct->tex : dt ? dt->tex : nil;
            if (any) { passW_ = int(any.width); passH_ = int(any.height); }
            if (ct) {
                rp.colorAttachments[0].texture = ct->tex;
                rp.colorAttachments[0].loadAction = d.clearColorEnabled ? MTLLoadActionClear : MTLLoadActionLoad;
                rp.colorAttachments[0].storeAction = MTLStoreActionStore;
                rp.colorAttachments[0].clearColor = MTLClearColorMake(d.clearColor[0], d.clearColor[1], d.clearColor[2], d.clearColor[3]);
            }
            rp.depthAttachment.texture = dt ? dt->tex : nil;
            rp.depthAttachment.loadAction = d.clearDepthEnabled ? MTLLoadActionClear : MTLLoadActionLoad;
            rp.depthAttachment.storeAction = MTLStoreActionStore;
            rp.depthAttachment.clearDepth = d.clearDepth;
            enc_ = [cmd_ renderCommandEncoderWithDescriptor:rp];
            [enc_ setFrontFacingWinding:MTLWindingCounterClockwise];
            pipelineId_ = 0;
            return;
        }
        if (!drawable_) return;
        rp.colorAttachments[0].texture = drawable_.texture;
        rp.colorAttachments[0].loadAction = d.clearColorEnabled ? MTLLoadActionClear : (firstPass_ ? MTLLoadActionDontCare : MTLLoadActionLoad);
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        rp.colorAttachments[0].clearColor = MTLClearColorMake(d.clearColor[0], d.clearColor[1], d.clearColor[2], d.clearColor[3]);
        rp.depthAttachment.texture = depth_;
        rp.depthAttachment.loadAction = d.clearDepthEnabled ? MTLLoadActionClear : MTLLoadActionLoad;
        rp.depthAttachment.storeAction = MTLStoreActionStore;
        rp.depthAttachment.clearDepth = d.clearDepth;
        enc_ = [cmd_ renderCommandEncoderWithDescriptor:rp];
        [enc_ setFrontFacingWinding:MTLWindingCounterClockwise];
        firstPass_ = false;
        pipelineId_ = 0;
    }
    void endPass() override {
        if (enc_) { [enc_ endEncoding]; enc_ = nil; }
        explicitPassSamples_ = 0;
        explicitPassColors_ = -1;
    }
    void pushGpuDebugGroup(const char* label) override {
        if (gpuTraceActive_ && enc_ && label) {
            NSString* text = [NSString stringWithUTF8String:label];
            if (text) [enc_ pushDebugGroup:text];
        }
    }
    void popGpuDebugGroup() override {
        if (gpuTraceActive_ && enc_) [enc_ popDebugGroup];
    }

    void setViewport(const Rect& r) override {
        if (enc_) [enc_ setViewport:MTLViewport{double(r.x), double(r.y), double(r.w), double(r.h), 0.0, 1.0}];
    }
    void setScissor(const Rect& r) override {
        if (!enc_) return;
        if (r.w <= 0 || r.h <= 0) { [enc_ setScissorRect:MTLScissorRect{0, 0, NSUInteger(passW_), NSUInteger(passH_)}]; return; }
        int x0 = std::max(0, r.x), y0 = std::max(0, r.y);
        int x1 = std::min(passW_, r.x + r.w), y1 = std::min(passH_, r.y + r.h);
        if (x1 <= x0 || y1 <= y0) { x0 = y0 = 0; x1 = y1 = 1; } // Metal forbids empty rects; 1px is invisible enough
        [enc_ setScissorRect:MTLScissorRect{NSUInteger(x0), NSUInteger(y0), NSUInteger(x1 - x0), NSUInteger(y1 - y0)}];
    }
    void setPipeline(PipelineHandle h) override {
        auto* p = pipelines_.get(h.id);
        if (!enc_ || !p) return;
        if (hdrPass_ && !p->desc.depthOnly && p->desc.colorFormat != PixelFormat::RGBA16F) {
            h = hdrPipeline(h); p = pipelines_.get(h.id); if (!p) { pipelineId_ = 0; return; }
        }
        if (explicitPassSamples_ && p->desc.sampleCount != explicitPassSamples_) {
            error_ = "Metal: pipeline sample count does not match active render target pass";
            pipelineId_ = 0; return;
        }
        if (explicitPassColors_ >= 0 &&
            (p->desc.depthOnly ? 0 : p->desc.colorAttachmentCount) != explicitPassColors_) {
            error_ = "Metal: pipeline color output count does not match MRT pass";
            pipelineId_ = 0; return;
        }
        if (explicitPassColors_ > 0 && p->desc.colorFormat != explicitPassColorFormat_) {
            error_ = "Metal: pipeline color format does not match MRT pass";
            pipelineId_ = 0; return;
        }
        if (explicitPassColors_ > 1) {
            bool match = true;
            for (int i = 1; i < explicitPassColors_; ++i)
                if (p->desc.additionalColorFormats[i - 1] != explicitPassColorFormat_)
                    match = false;
            if (!match) {
                error_ = "Metal: additional pipeline formats differ from MRT attachments";
                pipelineId_ = 0; return;
            }
        }
        pipelineId_ = h.id;
        [enc_ setRenderPipelineState:p->state];
        [enc_ setDepthStencilState:p->depth];
        [enc_ setCullMode:p->desc.cull == CullMode::None ? MTLCullModeNone : p->desc.cull == CullMode::Back ? MTLCullModeBack : MTLCullModeFront];
        [enc_ setDepthBias:p->desc.depthBiasConstant slopeScale:p->desc.depthBiasSlope clamp:0.0f];
    }
    void setVertexBuffer(BufferHandle h, size_t offset) override {
        vbId_ = h.id;
        vbOffset_ = offset;
        auto* b = buffers_.get(h.id);
        if(enc_&&b){auto buffer=current(*b);if(bindingCacheEnabled_&&boundVertexBuffer_==buffer&&boundVertexOffset_==offset)return;
            [enc_ setVertexBuffer:buffer offset:offset atIndex:0];if(bindingCacheEnabled_){boundVertexBuffer_=buffer;boundVertexOffset_=offset;}}
    }
    void setIndexBuffer(BufferHandle h, size_t offset) override { ibId_ = h.id; ibOffset_ = offset; }
    void setTexture(int slot, TextureHandle h) override {
        auto* t = textures_.get(h.id);
        if (!enc_ || !t) return;
        if (slot >= 0 && slot < 32) {
            icbFragmentTextures_[slot] = t->tex;
            icbFragmentSamplers_[slot] = t->sampler;
        }
        if(bindingCacheEnabled_&&slot>=0&&slot<32&&boundTextures_[slot]==t->tex&&(slot>=16||boundSamplers_[slot]==t->sampler))return;
        if(bindingCacheEnabled_&&slot>=0&&slot<32)boundTextures_[slot]=t->tex;
        if(bindingCacheEnabled_&&slot>=0&&slot<16)boundSamplers_[slot]=t->sampler;
        [enc_ setFragmentTexture:t->tex atIndex:NSUInteger(slot)];
        [enc_ setVertexTexture:t->tex atIndex:NSUInteger(slot)];   // bone / morph data textures are read in the vertex stage
        if (slot < 16) {   // sampler slots 0..15 per stage; shaders use constexpr samplers above that
            [enc_ setFragmentSamplerState:t->sampler atIndex:NSUInteger(slot)];
            [enc_ setVertexSamplerState:t->sampler atIndex:NSUInteger(slot)];
        }
    }
    size_t maxUniformBytes() const override { return 32768; }
    struct UniformPage { id<MTLBuffer> buffer; };
    std::vector<UniformPage> uniformPages_[kFramesInFlight];
    size_t uniformPage_=0, uniformOffset_=0;
    bool gpuIcbUniformBinding_ = false;
    size_t gpuIcbInstanceUploadedBytes_ = 0;
    void setGpuIcbUniformBinding(bool enabled) override { gpuIcbUniformBinding_ = enabled; }
    size_t gpuIcbInstanceUploadBytes() const override { return gpuIcbInstanceUploadedBytes_; }
    void setUniforms(int slot, const void* data, size_t size) override {
        if (!enc_ || !data || !size) return;
        // ICB commands cannot inherit setVertexBytes/setFragmentBytes when
        // inheritBuffers=NO. Capture the exact bytes to give each command a
        // stable MTLBuffer instead. This is used only in experimental ICB mode.
        if (supportsMultiDrawIndirect() && slot >= 0 && slot < 8)
            indirectUniformSnapshots_[slot].assign(static_cast<const uint8_t*>(data),
                                                   static_cast<const uint8_t*>(data) + size);
        if(size>4096 || gpuIcbUniformBinding_) {
            if(size>maxUniformBytes()) { error_="uniform block exceeds device limit"; return; }
            // Per-frame pages survive until this frame slot's GPU work has completed.
            constexpr size_t capacity=1024*1024;
            auto& pages=uniformPages_[frame_%kFramesInFlight];
            if(uniformOffset_+size>capacity){++uniformPage_;uniformOffset_=0;}
            if(uniformPage_>=pages.size())pages.push_back({[device_ newBufferWithLength:capacity options:MTLResourceStorageModeShared]});
            auto buffer=pages[uniformPage_].buffer;
            if(!buffer){error_="uniform allocation failed";gpuHealthy_->store(false);return;}
            std::memcpy(static_cast<uint8_t*>(buffer.contents)+uniformOffset_,data,size);
            [enc_ setVertexBuffer:buffer offset:uniformOffset_ atIndex:NSUInteger(1+slot)];
            [enc_ setFragmentBuffer:buffer offset:uniformOffset_ atIndex:NSUInteger(1+slot)];
            uniformOffset_=(uniformOffset_+size+255)&~size_t(255);
            return;
        }
        [enc_ setVertexBytes:data length:size atIndex:NSUInteger(1 + slot)];
        [enc_ setFragmentBytes:data length:size atIndex:NSUInteger(1 + slot)];
    }
    MTLPrimitiveType prim() {
        auto* p = pipelines_.get(pipelineId_);
        return p && p->desc.primitive == Primitive::Lines ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle;
    }
    void draw(uint32_t count, uint32_t first) override {
        if (!enc_ || !pipelines_.get(pipelineId_)) return;
        [enc_ drawPrimitives:prim() vertexStart:first vertexCount:count];
    }
    void drawIndexed(uint32_t count, uint32_t firstIndex, int32_t baseVertex, uint32_t instances) override {
        auto* p = pipelines_.get(pipelineId_);
        auto* ib = buffers_.get(ibId_);
        if (!enc_ || !p || !ib) return;
        const bool u32 = p->desc.indexType == IndexType::UInt32;
        [enc_ drawIndexedPrimitives:prim() indexCount:count indexType:u32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16
                        indexBuffer:current(*ib) indexBufferOffset:ibOffset_ + size_t(firstIndex) * (u32 ? 4 : 2)
                      instanceCount:std::max<uint32_t>(instances, 1) baseVertex:baseVertex baseInstance:0];
    }

    bool supportsMultiDrawIndirect() const override {
        // Independent opt-ins: explicit CPU MDI, legacy experimental ICB,
        // or the renderer's opt-in three-way AUTO policy. AUTO needs to query
        // CPU-MDI availability to train its CPU fallback; it must NOT enable
        // compute-authored GPU ICB (supportsGpuEncodedIcb has stricter flags).
        // No path is enabled for an ordinary non-AUTO production process.
        const char* cpu = std::getenv("AIVERSE_METAL_CPU_MDI");
        const char* experimental = std::getenv("AIVERSE_EXPERIMENTAL_METAL_ICB");
        const char* autoBatch = std::getenv("AIVERSE_BATCH_AUTO");
        if (!(cpu && std::strcmp(cpu, "1") == 0) &&
            !(experimental && std::strcmp(experimental, "1") == 0) &&
            !(autoBatch && std::strcmp(autoBatch, "1") == 0)) return false;
#if TARGET_OS_IPHONE
        if (@available(iOS 14.0, *)) return [device_ supportsFamily:MTLGPUFamilyApple3];
#else
        if (@available(macOS 11.0, *)) return [device_ supportsFamily:MTLGPUFamilyApple3] || [device_ supportsFamily:MTLGPUFamilyMac2];
#endif
        return false;
    }
    bool supportsGpuEncodedIcb() const override {
        // Do not let the CPU-only opt-in accidentally enable an unvalidated
        // compute-encoded ICB path on a driver with known AGX failures.
        const char* experimental = std::getenv("AIVERSE_EXPERIMENTAL_METAL_ICB");
        const char* encoded = std::getenv("AIVERSE_GPU_ENCODED_ICB");
        return experimental && std::strcmp(experimental, "1") == 0 &&
            encoded && std::strcmp(encoded, "1") == 0 &&
            supportsMultiDrawIndirect();
    }
    void declareIndirectRead(id<MTLResource> resource, MTLRenderStages stages) {
        if (@available(macOS 13.0, iOS 16.0, *))
            [enc_ useResource:resource usage:MTLResourceUsageRead stages:stages];
        else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
            [enc_ useResource:resource usage:MTLResourceUsageRead];
#pragma clang diagnostic pop
        }
    }
    // Encode a complete indexed ICB on the GPU, outside all render passes.
    // Commands inherit the current parent pipeline and buffer arguments when
    // executed later; the instance index maps to the caller's U0 array.
    uint32_t prepareGpuEncodedIndexedIcb(
        BufferHandle gpuArguments, BufferHandle vertexBuffer, BufferHandle indexBuffer,
        const uint32_t* sourceSlots, uint32_t count,
        const void* instanceData, size_t instanceBytes) override {
        return prepareGpuEncodedIndexedIcbInternal(
            gpuArguments,vertexBuffer,indexBuffer,sourceSlots,count,
            instanceData,instanceBytes,nullptr);
    }
    uint32_t prepareGpuEncodedIndexedIcbWithVirtualMaterials(
        BufferHandle gpuArguments, BufferHandle vertexBuffer, BufferHandle indexBuffer,
        const uint32_t* sourceSlots, uint32_t count,
        const void* instanceData, size_t instanceBytes,
        const GpuIcbVirtualMaterial* materials) override {
        static const bool optIn = [] {
            const char* flag=std::getenv("AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_VT");
            return flag && std::strcmp(flag,"1")==0;
        }();
        if (!optIn || !materials) return 0;
        return prepareGpuEncodedIndexedIcbInternal(
            gpuArguments,vertexBuffer,indexBuffer,sourceSlots,count,
            instanceData,instanceBytes,materials);
    }
    uint32_t prepareGpuEncodedIndexedIcbWithPbrMaterials(
        BufferHandle gpuArguments, BufferHandle vertexBuffer,
        BufferHandle indexBuffer, const uint32_t* sourceSlots, uint32_t count,
        const void* instanceData, size_t instanceBytes,
        const GpuIcbPbrMaterial* materials) override {
        static const bool enabled=[] {
            const char* flag=std::getenv("AIVERSE_EXPERIMENTAL_ICB_PER_COMMAND_PBR");
            return flag && std::strcmp(flag,"1")==0;
        }();
        if (!enabled || !materials || count<2 || count>256) return 0;
        std::vector<GpuIcbVirtualMaterial> entries(count);
        for (uint32_t i=0;i<count;++i) {
            if (!materials[i].valid()) return 0;
            auto& entry=entries[i];
            entry.hasVirtualTexture=false;
            entry.hasPbr=true;
            entry.pbrTextures=materials[i].textures;
            entry.pbrU2=materials[i].u2;
            entry.graphTextureCount=materials[i].graphTextureCount;
            entry.graphTextures=materials[i].graphTextures;
            if (!entry.valid()) return 0;
        }
        return prepareGpuEncodedIndexedIcbInternal(
            gpuArguments,vertexBuffer,indexBuffer,sourceSlots,count,
            instanceData,instanceBytes,entries.data());
    }
    uint32_t prepareGpuEncodedIndexedIcbInternal(
        BufferHandle gpuArguments, BufferHandle vertexBuffer, BufferHandle indexBuffer,
        const uint32_t* sourceSlots, uint32_t count,
        const void* instanceData, size_t instanceBytes,
        const GpuIcbVirtualMaterial* materials) {
        static const bool enabled = [] {
            const char* flag = std::getenv("AIVERSE_GPU_ENCODED_ICB");
            return flag && std::strcmp(flag, "1") == 0;
        }();
        if (!enabled || !supportsGpuEncodedIcb() || !cmd_ || enc_ || compute_ ||
            !sourceSlots || !instanceData || count < 2 || count > 256 ||
            instanceBytes != size_t(count) * 36u * sizeof(float)) return 0;
        auto* commandData = buffers_.get(gpuArguments.id);
        auto* vertexData = buffers_.get(vertexBuffer.id);
        auto* indexData = buffers_.get(indexBuffer.id);
        if (!commandData || !vertexData || !indexData ||
            commandData->type != BufferType::Storage ||
            (vertexData->type != BufferType::Vertex &&
             vertexData->type != BufferType::Storage) ||
            (indexData->type != BufferType::Index &&
             indexData->type != BufferType::Storage)) return 0;
        // CPU knows the input slots and the original command buffer capacity.
        // Reject malformed source offsets before launching the GPU kernel.
        const size_t availableCommands = commandData->size / sizeof(DrawIndexedIndirectCommand);
        for (uint32_t i = 0; i < count; ++i)
            if (sourceSlots[i] >= availableCommands) return 0;
        if (!gpuIcbPipeline_) {
            static const char* source = R"MSL(
#include <metal_stdlib>
#include <metal_command_buffer>
using namespace metal;
struct DrawArgs {
    uint indexCount, instanceCount, firstIndex;
    int baseVertex;
    uint firstInstance;
};
struct IcbContainer {
    command_buffer commands [[id(0)]];
};
struct Params { uint count, indexCapacity, materialStride, vtStride, pbrU2Stride,
                 vertexCapacity; };
kernel void cs_main(device const DrawArgs* generated [[buffer(0)]],
                    device uint* indices [[buffer(1)]],
                    device IcbContainer* container [[buffer(2)]],
                    device const uint* slots [[buffer(3)]],
                    device const uchar* vertices [[buffer(4)]],
                    device const uchar* instances [[buffer(5)]],
                    device const uchar* uniform1 [[buffer(6)]],
                    device const uchar* uniform2 [[buffer(7)]],
                    device const uchar* uniform3 [[buffer(8)]],
                    device const uchar* uniform4 [[buffer(9)]],
                    device const uchar* uniform5 [[buffer(10)]],
                    device const uchar* uniform6 [[buffer(11)]],
                    device const uchar* uniform7 [[buffer(12)]],
                    device const uchar* pbrArguments [[buffer(13)]],
                    constant Params& params [[buffer(17)]],
                    uint tid [[thread_position_in_grid]]) {
    if (tid >= params.count) return;
    const DrawArgs args = generated[slots[tid]];
    // Generated draw commands are untrusted until bounded. Each GPU slot
    // owns exactly one uniform record: multi-instance source commands would
    // overrun that record and must fall back to the CPU renderer.
    if (args.indexCount == 0 || args.instanceCount != 1 ||
        args.firstIndex > params.indexCapacity ||
        args.indexCount > params.indexCapacity - args.firstIndex) return;
    // baseVertex is signed and untrusted: a negative or out-of-buffer base
    // would make every fetched vertex address escape the vertex buffer.
    if (args.baseVertex < 0 || uint(args.baseVertex) >= params.vertexCapacity) return;
    render_command command(container->commands, tid);
    // Bind explicitly in the ICB. Inheriting buffers with a zero binding
    // capacity previously triggered an AGX executeCommandsInBuffer crash.
    command.set_vertex_buffer(vertices, 0);
    command.set_vertex_buffer(instances, 1);
    command.set_vertex_buffer(uniform1, 2);
    command.set_vertex_buffer(uniform2, 3);
    command.set_vertex_buffer(uniform3, 4);
    command.set_vertex_buffer(uniform4, 5);
    command.set_vertex_buffer(uniform5, 6);
    command.set_vertex_buffer(uniform6, 7);
    command.set_vertex_buffer(uniform7, 8);
    command.set_fragment_buffer(uniform1, 2);
    command.set_fragment_buffer(uniform2 + tid*params.pbrU2Stride, 3);
    command.set_fragment_buffer(uniform3, 4);
    command.set_fragment_buffer(uniform4, 5);
    command.set_fragment_buffer(uniform5, 6);
    // Only the opt-in per-command path changes U6 and the PBR resources.
    // Legacy commands use zero strides and the original shared buffers.
    command.set_fragment_buffer(uniform6 + tid*params.vtStride, 7);
    // Fragment slot 8 is reserved for the PBR argument-buffer variant.
    // U7 remains available at vertex slot 8.
    command.set_fragment_buffer(pbrArguments + tid*params.materialStride, 8);
    command.draw_indexed_primitives(primitive_type::triangle,
                                    args.indexCount,
                                    indices + args.firstIndex,
                                    args.instanceCount,
                                    args.baseVertex,
                                    tid);
}
)MSL";
            NSError* error = nil;
            id<MTLLibrary> library =
                [device_ newLibraryWithSource:[NSString stringWithUTF8String:source]
                                     options:nil error:&error];
            id<MTLFunction> kernel = library ? [library newFunctionWithName:@"cs_main"] : nil;
            if (!kernel) {
                error_ = error ? error.localizedDescription.UTF8String : "GPU ICB kernel missing";
                return 0;
            }
            gpuIcbPipeline_ = [device_ newComputePipelineStateWithFunction:kernel error:&error];
            gpuIcbArgumentEncoder_ = [kernel newArgumentEncoderWithBufferIndex:2];
            if (!gpuIcbPipeline_ || !gpuIcbArgumentEncoder_ ||
                gpuIcbPipeline_.maxTotalThreadsPerThreadgroup < 64) {
                error_ = error ? error.localizedDescription.UTF8String : "GPU ICB compute unsupported";
                gpuIcbPipeline_ = nil;
                return 0;
            }
        }
        const size_t frameSlot = frame_ % kFramesInFlight;
        auto& pool = gpuEncodedIcbPool_[frameSlot];
        const size_t slotIndex = gpuEncodedIcbCursor_[frameSlot]++;
        if (slotIndex >= 65535u) return 0;
        if (slotIndex == pool.size()) pool.emplace_back();
        auto& resources = pool[slotIndex];
        if (!resources.icb || resources.capacity < count) {
            MTLIndirectCommandBufferDescriptor* descriptor =
                [MTLIndirectCommandBufferDescriptor new];
            descriptor.commandTypes = MTLIndirectCommandTypeDrawIndexed;
            descriptor.inheritBuffers = NO;
            descriptor.inheritPipelineState = YES;
            descriptor.maxVertexBufferBindCount = 9;
            descriptor.maxFragmentBufferBindCount = 9;
            resources.icb =
                [device_ newIndirectCommandBufferWithDescriptor:descriptor
                                                maxCommandCount:count
                                                        options:MTLResourceStorageModeShared];
            if (!resources.icb) return 0;
            resources.capacity = count;
        }
        if (!resources.argBuffer || resources.argBuffer.length <
                    gpuIcbArgumentEncoder_.encodedLength)
            resources.argBuffer = [device_ newBufferWithLength:
                gpuIcbArgumentEncoder_.encodedLength options:MTLResourceStorageModeShared];
        if (!resources.slots || resources.slots.length < count * sizeof(uint32_t))
            resources.slots = [device_ newBufferWithLength:count * sizeof(uint32_t)
                                                  options:MTLResourceStorageModeShared];
        if (!resources.argBuffer || !resources.slots) return 0;
        if (!resources.instanceBuffer || resources.instanceBuffer.length < instanceBytes)
            resources.instanceBuffer = [device_ newBufferWithLength:instanceBytes
                                             options:MTLResourceStorageModeShared];
        if (!resources.instanceBuffer || !resources.instanceBuffer.contents) return 0;
        // Prepare the native VT descriptors BEFORE GPU encoding. Each native
        // texture is retained by this frame's resource pool and compared
        // with its original handle again at execute time.
        resources.virtualMaterials.clear();
        if (materials) {
            resources.virtualMaterials.reserve(count);
            for (uint32_t i=0;i<count;++i) {
                if (!materials[i].valid() ||
                    materials[i].hasPbr != materials[0].hasPbr ||
                    materials[i].hasVirtualTexture !=
                        materials[0].hasVirtualTexture) return 0;
                GpuIcbNativeVtMaterial native;
                native.hasVt=materials[i].hasVirtualTexture;
                if (native.hasVt) {
                    const auto* atlas=textures_.get(materials[i].physicalAtlas.id);
                    const auto* table=textures_.get(materials[i].pageTable.id);
                    const auto* alpha=materials[i].alphaCoverage.valid() ?
                        textures_.get(materials[i].alphaCoverage.id) : nullptr;
                    // VT uses RGBA8 2D arrays and optional R8 alpha.
                    if (!atlas || !table || !atlas->tex || !table->tex ||
                        atlas->dimension != TextureDimension::Texture2DArray ||
                        table->dimension != TextureDimension::Texture2DArray ||
                        atlas->fmt != PixelFormat::RGBA8 ||
                        table->fmt != PixelFormat::RGBA8 ||
                        !atlas->tex.arrayLength || !table->tex.arrayLength ||
                        (alpha && (!alpha->tex ||
                         alpha->dimension != TextureDimension::Texture2D ||
                         alpha->fmt != PixelFormat::R8)) ||
                        (materials[i].alphaCoverage.valid() && !alpha) ||
                        materials[i].params[0] >= float(table->tex.arrayLength) ||
                        2.0f*materials[i].params[1]+1.0f >=
                            float(std::min(atlas->tex.width,atlas->tex.height)))
                        return 0;
                    native.atlasHandle=materials[i].physicalAtlas;
                    native.tableHandle=materials[i].pageTable;
                    native.alphaHandle=materials[i].alphaCoverage;
                    native.atlas=atlas->tex;
                    native.table=table->tex;
                    native.alpha=alpha ? alpha->tex : nil;
                    std::memcpy(native.u6,materials[i].params,sizeof(native.u6));
                }
                native.hasPbr=materials[i].hasPbr;
                if (native.hasPbr) {
                    // The six ordinary built-in PBR sources are bound as
                    // texture2d<float> and require their own sampler states.
                    for (int slot=0;slot<6;++slot) {
                        const auto* source=textures_.get(
                            materials[i].pbrTextures[size_t(slot)].id);
                        if (!source || !source->tex || !source->sampler ||
                            source->dimension != TextureDimension::Texture2D ||
                            source->tex.sampleCount != 1)
                            return 0;
                        native.pbrHandles[size_t(slot)]=
                            materials[i].pbrTextures[size_t(slot)];
                        native.pbrTextures[size_t(slot)]=source->tex;
                        native.pbrSamplers[size_t(slot)]=source->sampler;
                    }
                    std::memcpy(native.u2,materials[i].pbrU2.data(),
                                sizeof(native.u2));
                }
                native.graphTextureCount=materials[i].graphTextureCount;
                // Graph sources use 13..16 and optional VT occupies the
                // next two fragment texture indices, not dynamic samplers.
                // Do not encode out-of-range Metal argument IDs even if
                // the caller bypassed the renderer's slot planner.
                if (native.graphTextureCount>4 ||
                    (native.graphTextureCount>0 && !native.hasPbr) ||
                    13+int(native.graphTextureCount)+(native.hasVt ? 2 : 0) >
                        maxFragmentTextureSlots())
                    return 0;
                for (uint32_t j=0;j<native.graphTextureCount;++j) {
                    const auto handle=materials[i].graphTextures[j];
                    const auto* source=textures_.get(handle.id);
                    if (!source || !source->tex || !source->sampler ||
                        source->dimension!=TextureDimension::Texture2D ||
                        source->tex.sampleCount!=1 ||
                        source->samplerCompare ||
                        // Graph source slot 16 has no dynamic Metal
                        // sampler. Reject instead of silently changing
                        // nearest/clamp sampling to repeat/linear.
                        (j==3 && (!source->samplerRepeat ||
                                  !source->samplerLinear)))
                        return 0;
                    native.graphHandles[j]=handle;
                    native.graphTextures[j]=source->tex;
                    native.graphSamplers[j]=source->sampler;
                }
                resources.virtualMaterials.push_back(std::move(native));
            }
        }
        // The GPU encoder references persistent uniform allocations, not
        // transient parent setBytes or parent buffer inheritance. The values
        // are filled from the correct render pass at execution time.
        constexpr size_t kGpuIcbUniformCapacity = 4096;
        constexpr size_t kPerCommandPbrStride = 4096;
        constexpr size_t kPerCommandVtStride = 256;
        constexpr size_t kPerCommandU2Stride = 256;
        const bool commandPbr = materials && materials[0].hasPbr;
        const bool commandVt = materials && materials[0].hasVirtualTexture;
        for (int slot = 1; slot < 8; ++slot) {
            const size_t bytes = (commandVt && slot==6) ?
                size_t(count)*kPerCommandVtStride :
                (commandPbr && slot==2) ?
                size_t(count)*kPerCommandU2Stride :
                kGpuIcbUniformCapacity;
            if (!resources.uniforms[slot] ||
                resources.uniforms[slot].length < bytes)
                resources.uniforms[slot] = [device_ newBufferWithLength:
                    bytes options:MTLResourceStorageModeShared];
            if (!resources.uniforms[slot]) return 0;
        }
        const size_t pbrBytes = materials ?
            size_t(count)*kPerCommandPbrStride : kGpuIcbUniformCapacity;
        if (!resources.pbrArgs || resources.pbrArgs.length < pbrBytes)
            resources.pbrArgs = [device_ newBufferWithLength:
                pbrBytes options:MTLResourceStorageModeShared];
        if (!resources.pbrArgs) return 0;
        if (materials) {
            if (commandVt) {
                auto* destination=static_cast<uint8_t*>(resources.uniforms[6].contents);
                if (!destination) return 0;
                for(uint32_t i=0;i<count;++i)
                    std::memcpy(destination+size_t(i)*kPerCommandVtStride,
                                resources.virtualMaterials[i].u6,16);
            }
            if (commandPbr) {
                auto* u2Destination=static_cast<uint8_t*>(
                    resources.uniforms[2].contents);
                if (!u2Destination) return 0;
                for (uint32_t i=0;i<count;++i)
                    std::memcpy(u2Destination+size_t(i)*kPerCommandU2Stride,
                                resources.virtualMaterials[i].u2,
                                sizeof(resources.virtualMaterials[i].u2));
            }
        }
        // The same submission index in a recycled frame slot typically
        // contains the same static instance payload. Avoid rewriting it.
        if (resources.instanceValidBytes != instanceBytes ||
            std::memcmp(resources.instanceBuffer.contents,
                        instanceData, instanceBytes) != 0) {
            std::memcpy(resources.instanceBuffer.contents, instanceData, instanceBytes);
            resources.instanceValidBytes = instanceBytes;
            gpuIcbInstanceUploadedBytes_ += instanceBytes;
        }
        std::memcpy(resources.slots.contents, sourceSlots, count * sizeof(uint32_t));
        [gpuIcbArgumentEncoder_ setArgumentBuffer:resources.argBuffer offset:0];
        [gpuIcbArgumentEncoder_ setIndirectCommandBuffer:resources.icb atIndex:0];

        // Reset before GPU encoding. This slot is reused only after the Metal
        // frame-semaphore fence and not twice within one frame.
        [resources.icb resetWithRange:NSMakeRange(0, count)];
        // Encode every compatible group into one Metal compute encoder.
        // The caller closes it once, before opening the render pass, reducing
        // per-group encoder switches and associated GPU scheduling overhead.
        if (!gpuIcbEncoder_) {
            gpuIcbEncoder_ = [cmd_ computeCommandEncoder];
            if (!gpuIcbEncoder_) return 0;
            [gpuIcbEncoder_ setComputePipelineState:gpuIcbPipeline_];
        }
        id<MTLComputeCommandEncoder> encoder = gpuIcbEncoder_;
        [encoder setBuffer:current(*commandData) offset:0 atIndex:0];
        [encoder setBuffer:current(*indexData) offset:0 atIndex:1];
        [encoder setBuffer:resources.argBuffer offset:0 atIndex:2];
        [encoder setBuffer:resources.slots offset:0 atIndex:3];
        // The model renderer supplies both geometry buffers from the same
        // pooled resource. Each indexed command selects its local firstIndex.
        [encoder setBuffer:current(*vertexData) offset:0 atIndex:4];
        [encoder setBuffer:resources.instanceBuffer offset:0 atIndex:5];
        for (int slot = 1; slot < 8; ++slot)
            [encoder setBuffer:resources.uniforms[slot] offset:0
                      atIndex:NSUInteger(slot + 5)];
        [encoder setBuffer:resources.pbrArgs offset:0 atIndex:13];
        struct Params { uint32_t count, indexCapacity, materialStride, vtStride,
                        pbrU2Stride, vertexCapacity; };
        const size_t indexCount = indexData->size / sizeof(uint32_t);
        if (indexCount > UINT32_MAX) return 0;
        // The vertex layout is owned by the bound pipeline, not known here.
        // Bound baseVertex by the smallest possible vertex (float3 position):
        // never rejects a valid command, always rejects negative or
        // beyond-buffer bases.
        constexpr size_t kMinVertexStride = 3u * sizeof(float);
        const size_t vertexCapacity = std::min<size_t>(vertexData->size / kMinVertexStride,
                                                       size_t(INT32_MAX));
        Params params{count,uint32_t(indexCount),
            materials ? uint32_t(kPerCommandPbrStride) : 0u,
            commandVt ? uint32_t(kPerCommandVtStride) : 0u,
            commandPbr ? uint32_t(kPerCommandU2Stride) : 0u,
            uint32_t(vertexCapacity)};
        [encoder setBytes:&params length:sizeof(params) atIndex:17];
        [encoder useResource:resources.icb usage:MTLResourceUsageWrite];
        [encoder dispatchThreadgroups:MTLSizeMake((count + 63u) / 64u, 1, 1)
                threadsPerThreadgroup:MTLSizeMake(64,1,1)];
        resources.frame = frame_;
        resources.count = count;
        return uint32_t(slotIndex + 1);
    }
    void finishGpuEncodedIcbPreparation() override {
        if (gpuIcbEncoder_) {
            [gpuIcbEncoder_ endEncoding];
            gpuIcbEncoder_ = nil;
        }
    }
    bool executeGpuEncodedIndexedIcb(uint32_t token) override {
        auto reject = [this](const char* cause) {
            error_ = std::string("GPU-encoded ICB execute rejected: ") + cause;
            if (std::getenv("AIVERSE_ICB_TRACE"))
                std::fprintf(stderr, "[metal-gpu-icb] %s\n", error_.c_str());
            return false;
        };
        if (!enc_ || token == 0) return reject("render encoder missing or token zero");
        auto* pipeline = pipelines_.get(pipelineId_);
        if (!pipeline || !pipeline->state)
            return reject("active render pipeline missing");
        // Regular PBR pipelines are deliberately compiled without ICB support
        // when their direct fragment textures/samplers are rejected by Metal.
        // The existing PBR ICB variant consumes these resources through a
        // fragment argument buffer at index 8 instead.
        const auto* boundShader=shaders_.get(pipeline->desc.shader.id);
        // Direct fragment texture bindings cannot be inherited reliably by
        // a GPU-encoded ICB. VT/R8 permutations MUST use an argument-buffer
        // variant even when the direct pipeline reports ICB support.
        const bool externalMaterialTextures = boundShader &&
            (boundShader->source.find("#define AIVERSE_MODEL_VT 1") != std::string::npos ||
             boundShader->source.find("#define VIRTUAL_ALPHA_MASK 1") != std::string::npos);
        const size_t frameSlot = frame_ % kFramesInFlight;
        auto& pool = gpuEncodedIcbPool_[frameSlot];
        if (size_t(token) > pool.size())
            return reject("ICB token outside current frame pool");
        auto& resources = pool[token - 1];
        if (resources.frame != frame_ || !resources.icb || resources.count == 0)
            return reject("ICB not encoded for current frame");
        const bool perCommandPbr = !resources.virtualMaterials.empty() &&
            resources.virtualMaterials.front().hasPbr;
        const bool perCommandVt = !resources.virtualMaterials.empty() &&
            resources.virtualMaterials.front().hasVt;
        // A non-VT PBR group still needs the PBR argument-buffer shader
        // even when the direct pipeline supports ordinary ICB.
        const bool usePbrVariant = perCommandPbr || externalMaterialTextures ||
            !pipeline->state.supportIndirectCommandBuffers;
        if (pipeline->desc.indexType != IndexType::UInt32)
            return reject("GPU ICB requires uint32 indices");
        id<MTLRenderPipelineState> icbPipeline =
            usePbrVariant ? pipeline->icbPbrState : pipeline->state;
        if (!icbPipeline || !icbPipeline.supportIndirectCommandBuffers)
            return reject("ICB-compatible pipeline unavailable (PBR variant not compiled)");
        if (!buffers_.get(ibId_) || !buffers_.get(vbId_))
            return reject("index or vertex resource is not bound");
        // The Metal render pipeline must retain all parent vertex/fragment
        // bindings; no transient bytes may be inherited from earlier draws.
        if (!gpuIcbUniformBinding_)
            return reject("persistent ICB uniform binding disabled");
        // ICB inherited parent bindings cannot rely on setBytes allocations
        // from earlier passes (especially U1 frame uniforms). Snapshot every
        // active uniform slot into resources owned by this frame/submission.
        // These buffers remain alive until the frame semaphore retires.
        // U0 is a device-indexed persistent GPU instance buffer. Material and
        // frame uniform blocks U1..U7 remain explicitly inherited snapshots.
        if (!resources.instanceBuffer) return reject("GPU instance buffer missing");
        declareIndirectRead(resources.instanceBuffer, MTLRenderStageVertex);
        for (int slot = 1; slot < 8; ++slot) {
            const auto& snapshot = indirectUniformSnapshots_[slot];
            id<MTLBuffer> stable = resources.uniforms[slot];
            const bool perCommandU2 = slot==2 &&
                !resources.virtualMaterials.empty() &&
                resources.virtualMaterials.front().hasPbr;
            const bool perCommandU6 = slot==6 &&
                !resources.virtualMaterials.empty() &&
                resources.virtualMaterials.front().hasVt;
            if (!stable || !stable.contents ||
                (!(perCommandU2 || perCommandU6) &&
                 snapshot.size() > stable.length))
                return reject("uniform snapshot exceeds encoded GPU ICB buffer");
            // Each command's U2/U6 is captured at prepare time in an
            // aligned persistent slice; never overwrite with the parent
            // material's uniform state.
            if (!(perCommandU2 || perCommandU6) && !snapshot.empty())
                std::memcpy(stable.contents, snapshot.data(), snapshot.size());
            declareIndirectRead(stable, MTLRenderStageVertex | MTLRenderStageFragment);
        }
        if (perCommandVt && !boundShader)
            return reject("per-command VT requires a model shader");
        if (!perCommandVt && perCommandPbr && boundShader &&
            (boundShader->source.find("#define AIVERSE_MODEL_VT 1") !=
                 std::string::npos ||
             boundShader->source.find("#define VIRTUAL_ALPHA_MASK 1") !=
                 std::string::npos))
            return reject("non-VT PBR ICB shader mode mismatch");
        if (usePbrVariant) {
            if (!pipeline->icbPbrEncoder)
                return reject("PBR ICB fragment argument encoder missing");
            constexpr int pbrSlots[] = {0,1,2,3,4,5,6,9,10,11,12};
            const auto* pbrShader = shaders_.get(pipeline->desc.shader.id);
            const bool virtualSampling = pbrShader &&
                pbrShader->source.find("#define AIVERSE_MODEL_VT 1") != std::string::npos;
            const bool alphaCoverage = pbrShader &&
                pbrShader->source.find("#define VIRTUAL_ALPHA_MASK 1") != std::string::npos;
            const bool graphShader = pbrShader &&
                pbrShader->source.find("#define AIVERSE_SG_ICB_GRAPH 1") != std::string::npos;
            int shaderGraphCount=0;
            if (graphShader) {
                const std::string_view source=pbrShader->source;
                constexpr std::string_view tag="#define AIVERSE_SG_TEXTURE_COUNT ";
                const auto at=source.find(tag);
                if (at==std::string_view::npos ||
                    at+tag.size()+1>=source.size() ||
                    source[at+tag.size()]<'0' ||
                    source[at+tag.size()]>'4' ||
                    source[at+tag.size()+1]!='\n')
                    return reject("invalid graph source count in ICB variant");
                shaderGraphCount=int(source[at+tag.size()]-'0');
            }
            const auto bound = [&](int slot) {
                return icbFragmentTextures_[slot] && icbFragmentSamplers_[slot];
            };
            for (int slot : pbrSlots)
                if (!(perCommandPbr && slot>=1 && slot<=6) && !bound(slot))
                    return reject("PBR ICB required texture or sampler not bound");
            if (resources.virtualMaterials.empty() &&
                ((virtualSampling &&
                  (!icbFragmentTextures_[13] || !icbFragmentTextures_[14])) ||
                 (alphaCoverage && !bound(15))))
                return reject("VT atlas/page table or R8 alpha ICB binding missing");
            const NSUInteger size = pipeline->icbPbrEncoder.encodedLength;
            // The compute kernel already recorded this exact buffer pointer.
            // Growing/replacing it here would make the encoded ICB stale.
            constexpr size_t kPerCommandPbrStride = 4096;
            if (!resources.pbrArgs || size > kPerCommandPbrStride ||
                resources.pbrArgs.length <
                    (resources.virtualMaterials.empty() ? size :
                     size_t(resources.count)*kPerCommandPbrStride))
                return reject("PBR ICB argument buffer exceeds encoded capacity");
            const auto bindPbrSlot = [&](int slot, bool sampler = true) {
                [pipeline->icbPbrEncoder setTexture:icbFragmentTextures_[slot]
                                            atIndex:NSUInteger(slot * 2)];
                if (sampler)
                    [pipeline->icbPbrEncoder setSamplerState:icbFragmentSamplers_[slot]
                                                     atIndex:NSUInteger(slot * 2 + 1)];
                declareIndirectRead(icbFragmentTextures_[slot], MTLRenderStageFragment);
            };
            if (resources.virtualMaterials.empty()) {
                [pipeline->icbPbrEncoder setArgumentBuffer:resources.pbrArgs offset:0];
                for (int slot : pbrSlots) bindPbrSlot(slot);
                // Atlas uses s1; page table uses read(). Never encode
                // unused argument indices 27/29.
                if (virtualSampling) { bindPbrSlot(13,false); bindPbrSlot(14,false); }
                if (alphaCoverage) bindPbrSlot(15);
            } else {
                if (perCommandVt != virtualSampling ||
                    resources.virtualMaterials.size()!=resources.count)
                    return reject("per-command material count/shader mode mismatch");
                for (uint32_t i=0;i<resources.count;++i) {
                    const auto& resource=resources.virtualMaterials[i];
                    if (resource.graphTextureCount!=uint32_t(shaderGraphCount))
                        return reject("ICB graph count mismatches shader layout");
                    if (perCommandVt) {
                        const auto* liveAtlas=textures_.get(resource.atlasHandle.id);
                        const auto* liveTable=textures_.get(resource.tableHandle.id);
                        const auto* liveAlpha=resource.alphaHandle.valid() ?
                            textures_.get(resource.alphaHandle.id) : nullptr;
                        if (!liveAtlas || !liveTable ||
                            liveAtlas->tex != resource.atlas ||
                            liveTable->tex != resource.table ||
                            (resource.alphaHandle.valid() &&
                             (!liveAlpha || liveAlpha->tex != resource.alpha)))
                            return reject("per-command VT texture changed after preparation");
                    }
                    [pipeline->icbPbrEncoder
                        setArgumentBuffer:resources.pbrArgs
                        offset:NSUInteger(size_t(i)*kPerCommandPbrStride)];
                    for (int slot : pbrSlots)
                        if (!(perCommandPbr && slot>=1 && slot<=6))
                            bindPbrSlot(slot);
                    if (perCommandPbr) {
                        for (int j=0;j<6;++j) {
                            const auto texture=resource.pbrTextures[size_t(j)];
                            const auto sampler=resource.pbrSamplers[size_t(j)];
                            const auto* live=textures_.get(
                                resource.pbrHandles[size_t(j)].id);
                            if (!live || live->tex!=texture ||
                                live->sampler!=sampler)
                                return reject("per-command PBR source changed after prepare");
                            const NSUInteger pbrSlot=NSUInteger(j+1);
                            [pipeline->icbPbrEncoder
                                setTexture:texture atIndex:pbrSlot*2u];
                            [pipeline->icbPbrEncoder
                                setSamplerState:sampler atIndex:pbrSlot*2u+1u];
                            declareIndirectRead(texture,MTLRenderStageFragment);
                        }
                    }
                    for(uint32_t j=0;j<resource.graphTextureCount;++j) {
                        const auto nativeTexture=resource.graphTextures[j];
                        const auto nativeSampler=resource.graphSamplers[j];
                        const auto* live=textures_.get(resource.graphHandles[j].id);
                        if (!live || live->tex!=nativeTexture ||
                            live->sampler!=nativeSampler)
                            return reject("ICB graph source changed after prepare");
                        const int slot=13+int(j);
                        [pipeline->icbPbrEncoder setTexture:nativeTexture
                                                   atIndex:NSUInteger(slot*2)];
                        if (slot<16)
                            [pipeline->icbPbrEncoder setSamplerState:nativeSampler
                                                           atIndex:NSUInteger(slot*2+1)];
                        // Graph source tex16 uses constexpr sampler s16.
                        declareIndirectRead(nativeTexture,MTLRenderStageFragment);
                    }
                    if (perCommandVt) {
                        const int slot=13+shaderGraphCount;
                        [pipeline->icbPbrEncoder setTexture:resource.atlas
                                                   atIndex:NSUInteger(slot*2)];
                        [pipeline->icbPbrEncoder setTexture:resource.table
                                                   atIndex:NSUInteger((slot+1)*2)];
                        declareIndirectRead(resource.atlas, MTLRenderStageFragment);
                        declareIndirectRead(resource.table, MTLRenderStageFragment);
                    }
                    if (alphaCoverage) {
                        if (!resource.alpha || !icbFragmentSamplers_[15])
                            return reject("per-command VT alpha mask missing");
                        [pipeline->icbPbrEncoder
                            setTexture:resource.alpha atIndex:30];
                        [pipeline->icbPbrEncoder
                            setSamplerState:icbFragmentSamplers_[15] atIndex:31];
                        declareIndirectRead(resource.alpha, MTLRenderStageFragment);
                    }
                }
            }
            if(pipeline->icbDynamicGi) {
                if(!icbFragmentTextures_[13])return reject("GI ICB atlas not bound");
                [pipeline->icbPbrEncoder setTexture:icbFragmentTextures_[13] atIndex:26];
                declareIndirectRead(icbFragmentTextures_[13],MTLRenderStageFragment);
            }
            declareIndirectRead(resources.pbrArgs, MTLRenderStageFragment);
            [enc_ setRenderPipelineState:icbPipeline];
        }
        declareIndirectRead(current(*buffers_.get(ibId_)), MTLRenderStageVertex);
        declareIndirectRead(current(*buffers_.get(vbId_)), MTLRenderStageVertex);
        [enc_ executeCommandsInBuffer:resources.icb
                            withRange:NSMakeRange(0, resources.count)];
        if (usePbrVariant)
            [enc_ setRenderPipelineState:pipeline->state];
        return true;
    }
    bool supportsGpuAuthoredIndirectDraw() const override {
        // This is the plain Metal indirect-buffer path, not GPU-encoded ICB.
        // No unstable ICB feature flags or render-pass inheritance required.
        return supportsCompute();
    }
    bool drawIndexedGpuIndirect(BufferHandle arguments, size_t offset) override {
        if (!enc_ || !pipelines_.get(pipelineId_) || !buffers_.get(ibId_)) return false;
        auto* args = buffers_.get(arguments.id);
        auto* index = buffers_.get(ibId_);
        if (!args || !index || args->type != BufferType::Storage ||
            (offset & 3u) || offset > args->size ||
            sizeof(DrawIndexedIndirectCommand) > args->size - offset ||
            ibOffset_ > index->size) return false;
        const id<MTLBuffer> argumentBuffer = current(*args);
        const id<MTLBuffer> indexBuffer = current(*index);
        if (!argumentBuffer || !indexBuffer) return false;
        const auto* p = pipelines_.get(pipelineId_);
        const bool u32 = p->desc.indexType == IndexType::UInt32;
        [enc_ drawIndexedPrimitives:prim()
                         indexType:u32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16
                       indexBuffer:indexBuffer
                 indexBufferOffset:ibOffset_
                    indirectBuffer:argumentBuffer
              indirectBufferOffset:offset];
        return true;
    }
    bool drawIndexedIndirect(BufferHandle commands, size_t offset, uint32_t drawCount, uint32_t stride) override {
        auto* p = pipelines_.get(pipelineId_);
        if (!supportsMultiDrawIndirect() || !enc_ || !p || !drawCount ||
            stride < sizeof(DrawIndexedIndirectCommand) || (stride & 3u) || (offset & 3u)) return false;
        auto* cmdBuffer = buffers_.get(commands.id);
        auto* ib = buffers_.get(ibId_);
        auto* vb = buffers_.get(vbId_);
        if (!cmdBuffer || !ib || !vb || cmdBuffer->type != BufferType::Indirect || cmdBuffer->storage ||
            offset > cmdBuffer->size ||
            size_t(drawCount - 1) * stride + sizeof(DrawIndexedIndirectCommand) > cmdBuffer->size - offset)
            return false;

        const id<MTLBuffer> indirectData = current(*cmdBuffer);
        const id<MTLBuffer> indices = current(*ib);
        const id<MTLBuffer> vertices = current(*vb);
        const bool pbr = p->icbPbrState != nil;
        const auto* sourceShader=shaders_.get(p->desc.shader.id);
        const bool externalMaterialTextures = sourceShader &&
            (sourceShader->source.find("#define AIVERSE_MODEL_VT 1") != std::string::npos ||
             sourceShader->source.find("#define VIRTUAL_ALPHA_MASK 1") != std::string::npos);
        if (externalMaterialTextures && !pbr) return false;
        id<MTLRenderPipelineState> indirectPipeline = pbr ? p->icbPbrState : p->state;
        if (!indirectData || !indirectData.contents || !indices || !vertices || !indirectPipeline ||
            !indirectPipeline.supportIndirectCommandBuffers) return false;

        const bool traceIcb = std::getenv("AIVERSE_ICB_PROFILE") != nullptr;
        const auto profileStart = std::chrono::steady_clock::now();
        const size_t frameSlot = frame_ % kFramesInFlight;
        auto& pooled = indirectPool_[frameSlot];
        const size_t submissionIndex = indirectCursor_[frameSlot]++;
        if (submissionIndex == pooled.size()) pooled.emplace_back();
        auto& resources = pooled[submissionIndex];
        // Build one fragment argument buffer containing all PBR texture and
        // sampler descriptors. The ICB can bind this buffer, whereas it cannot
        // encode the individual texture/sampler bindings used by direct draws.
        id<MTLBuffer> pbrArgs = nil;
        constexpr int pbrSlots[] = {0,1,2,3,4,5,6,9,10,11,12};
        const auto* indirectShader = shaders_.get(p->desc.shader.id);
        const bool virtualSampling = indirectShader &&
            indirectShader->source.find("#define AIVERSE_MODEL_VT 1") != std::string::npos;
        const bool alphaCoverage = indirectShader &&
            indirectShader->source.find("#define VIRTUAL_ALPHA_MASK 1") != std::string::npos;
        if (pbr) {
            if (!p->icbPbrEncoder) return false;
            const auto bound = [&](int slot) {
                return icbFragmentTextures_[slot] && icbFragmentSamplers_[slot];
            };
            for (int slot : pbrSlots)
                if (!bound(slot)) return false;
            if ((virtualSampling &&
                 (!icbFragmentTextures_[13] || !icbFragmentTextures_[14])) ||
                (alphaCoverage && !bound(15))) return false;
            const NSUInteger bytes = p->icbPbrEncoder.encodedLength;
            if (!resources.pbrArgs || resources.pbrArgs.length < bytes)
                resources.pbrArgs = [device_ newBufferWithLength:bytes options:MTLResourceStorageModeShared];
            pbrArgs = resources.pbrArgs;
            if (!pbrArgs) return false;
            [p->icbPbrEncoder setArgumentBuffer:pbrArgs offset:0];
            const auto bindPbrSlot = [&](int slot, bool sampler = true) {
                [p->icbPbrEncoder setTexture:icbFragmentTextures_[slot]
                                    atIndex:NSUInteger(slot * 2)];
                if (sampler)
                    [p->icbPbrEncoder setSamplerState:icbFragmentSamplers_[slot]
                                         atIndex:NSUInteger(slot * 2 + 1)];
            };
            for (int slot : pbrSlots) bindPbrSlot(slot);
            if (virtualSampling) { bindPbrSlot(13,false); bindPbrSlot(14,false); }
            if (alphaCoverage) bindPbrSlot(15);
        }

        if(pbr && p->icbDynamicGi) {
            if(!icbFragmentTextures_[13])return false;
            [p->icbPbrEncoder setTexture:icbFragmentTextures_[13] atIndex:26];
            declareIndirectRead(icbFragmentTextures_[13],MTLRenderStageFragment);
        }
        const auto afterArgs = std::chrono::steady_clock::now();
        // Controlled experiment: inherit only the shared pipeline from the
        // parent encoder. Per-command vertex/fragment buffers remain explicit:
        // their inheritance previously caused AGX G15 driver faults.
        // Process-level opt-in is captured before the immutable descriptor is
        // first allocated; never mix descriptor settings in one process.
        static const bool inheritCpuIcbPipeline = [] {
            const char* flag = std::getenv("AIVERSE_METAL_CPU_MDI_INHERIT_PIPELINE");
            return flag && std::strcmp(flag, "1") == 0;
        }();
        // Driver crash fix: explicit vertex/fragment buffer bindings.
        // Inheriting buffers with maxVertexBufferBindCount=0 caused AGX G15 to
        // fault inside executeCommandsInBufferCommon on Apple Silicon.
        // Descriptor configuration is identical for every model ICB. Keep one
        // immutable descriptor rather than allocating an Obj-C object per draw.
        if (!modelIcbDescriptor_) {
            modelIcbDescriptor_ = [MTLIndirectCommandBufferDescriptor new];
            modelIcbDescriptor_.commandTypes = MTLIndirectCommandTypeDrawIndexed;
            modelIcbDescriptor_.inheritBuffers = NO;
            modelIcbDescriptor_.inheritPipelineState = inheritCpuIcbPipeline ? YES : NO;
            modelIcbDescriptor_.maxVertexBufferBindCount = 9;
            modelIcbDescriptor_.maxFragmentBufferBindCount = 9;
        }
        // Stage141, diagnostic-only ICB playback control: both 0 and 128
        // deliberately allocate and reset the SAME extra 128 empty commands.
        // Only the executed ICB range differs between control/experiment.
        // When unset, all allocation/reset/execute behavior is unchanged.
        // Empty commands are never encoded; reset clears stale pooled slots.
        struct EmptyTailExperiment {
            bool active = false;
            uint32_t executedTail = 0;
        };
        static const EmptyTailExperiment emptyTail = [] {
            const char* flag = std::getenv("AIVERSE_METAL_CPU_MDI_EMPTY_TAIL");
            if (!flag) return EmptyTailExperiment{};
            if (std::strcmp(flag, "0") == 0) return EmptyTailExperiment{true, 0};
            if (std::strcmp(flag, "128") == 0) return EmptyTailExperiment{true, 128};
            return EmptyTailExperiment{}; // invalid controls never enable padding
        }();
        const uint32_t resetTail = emptyTail.active ? 128u : 0u;
        if (drawCount > std::numeric_limits<uint32_t>::max() - resetTail) return false;
        const uint32_t allocatedCount = drawCount + resetTail;
        const uint32_t executeCount = drawCount + emptyTail.executedTail;
        MTLIndirectCommandBufferDescriptor* descriptor = modelIcbDescriptor_;
        if (!resources.icb || resources.icbCapacity < allocatedCount) {
            resources.icb = [device_ newIndirectCommandBufferWithDescriptor:descriptor
                maxCommandCount:allocatedCount options:MTLResourceStorageModeShared];
            if (!resources.icb) return false;
            resources.icbCapacity = allocatedCount;
        }
        id<MTLIndirectCommandBuffer> icb = resources.icb;
        // Both Stage141 modes reset all padded slots; only the range passed
        // to executeCommandsInBuffer differs. In the default path the range
        // is exactly the original drawCount, with no extra allocation.
        [icb resetWithRange:NSMakeRange(0, allocatedCount)];
        const auto afterIcbReset = std::chrono::steady_clock::now();

        // Uniform buffers are private to this submission. Preserve their lifetime
        // until the frame's GPU fence has completed; never reference setBytes
        // allocations made by a parent render encoder from an ICB.
        id<MTLBuffer> uniforms[8] = {};
        for (int slot = 0; slot < 8; ++slot) {
            const auto& bytes = indirectUniformSnapshots_[slot];
            if (bytes.empty()) continue;
            if (!resources.uniforms[slot] || resources.uniforms[slot].length < bytes.size())
                resources.uniforms[slot] = [device_ newBufferWithLength:bytes.size()
                                                options:MTLResourceStorageModeShared];
            uniforms[slot] = resources.uniforms[slot];
            if (!uniforms[slot] || !uniforms[slot].contents) return false;
            std::memcpy(uniforms[slot].contents, bytes.data(), bytes.size());
        }
        // Each ICB command uses the same uniform slots. Compact the active
        // bindings once per submission instead of checking 8 empty slots for
        // every command (potentially hundreds per submission).
        struct ActiveUniform {
            id<MTLBuffer> buffer = nil;
            NSUInteger index = 0;
        };
        ActiveUniform uniformBindings[8]{};
        int uniformBindingCount = 0;
        for (int slot = 0; slot < 8; ++slot) {
            if (!uniforms[slot]) continue;
            uniformBindings[uniformBindingCount++] = {uniforms[slot], NSUInteger(1 + slot)};
        }
        const auto afterAlloc = std::chrono::steady_clock::now();
        const bool u32 = p->desc.indexType == IndexType::UInt32;
        const size_t indexBytes = u32 ? 4u : 2u;
        const MTLIndexType icbIndexType = u32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16;
        // All indirect commands share the same validated pipeline. Avoid
        // re-querying the pipeline table in prim() for each encoded command.
        const MTLPrimitiveType icbPrimitive =
            p->desc.primitive == Primitive::Lines
                ? MTLPrimitiveTypeLine : MTLPrimitiveTypeTriangle;
        // Stage139: reversible, process-level ICB experiment. A pipeline
        // without fs_main has no fragment stage, so a fragment uniform binding
        // cannot be consumed. Keep EVERY vertex binding and residency
        // declaration, regardless of this option: inherited buffers previously
        // triggered AGX faults. Missing shader metadata fails closed.
        struct DepthFragmentUniformExperiment {
            bool audit = false;
            bool skip = false;
        };
        static const DepthFragmentUniformExperiment depthFragmentUniforms = [] {
            const char* flag =
                std::getenv("AIVERSE_METAL_CPU_MDI_SKIP_DEPTH_FRAGMENT_UNIFORMS");
            const bool audit = flag && (std::strcmp(flag, "0") == 0 ||
                                        std::strcmp(flag, "1") == 0);
            return DepthFragmentUniformExperiment{
                audit, audit && std::strcmp(flag, "1") == 0};
        }();
        const MTShader* icbShader = depthFragmentUniforms.audit
            ? shaders_.get(p->desc.shader.id) : nullptr;
        const bool eligibleDepthNoFragment =
            depthFragmentUniforms.audit && p->desc.depthOnly && !pbr &&
            icbShader && icbShader->fs == nil;
        const bool skipDepthFragmentUniforms =
            eligibleDepthNoFragment && depthFragmentUniforms.skip;
        if (eligibleDepthNoFragment) {
            // One line per process (rather than per frame/command) confirms
            // that an experiment actually reached a fragmentless depth pass.
            static bool reported = false;
            if (!reported) {
                reported = true;
                std::fprintf(stderr,
                    "[metal-icb-depth-uniforms] mode=%s eligible=1 slots=%d\n",
                    skipDepthFragmentUniforms ? "on" : "off",
                    uniformBindingCount);
            }
        }
        const auto& modelUniforms = indirectUniformSnapshots_[0];
        constexpr size_t kModelInstanceBytes = 36u * sizeof(float);
        const size_t instanceCapacity = modelUniforms.size() / kModelInstanceBytes;
        if (modelUniforms.empty() || ibOffset_ > ib->size) return false;
        const size_t indexCapacity = (ib->size - ibOffset_) / indexBytes;
        for (uint32_t i = 0; i < drawCount; ++i) {
            DrawIndexedIndirectCommand args{};
            std::memcpy(&args, static_cast<const uint8_t*>(indirectData.contents) +
                               offset + size_t(i) * stride, sizeof(args));
            if (!args.indexCount || !args.instanceCount) continue;
            // Instance IDs index the model uniform array. Reject malformed commands
            // before encoding an ICB that could access beyond its snapshot.
            if (uint64_t(args.firstInstance) + args.instanceCount > instanceCapacity)
                return false;
            if (size_t(args.firstIndex) > indexCapacity ||
                size_t(args.indexCount) > indexCapacity - args.firstIndex)
                return false;
            id<MTLIndirectRenderCommand> command = [icb indirectRenderCommandAtIndex:i];
            if (!command) return false;
            if (!inheritCpuIcbPipeline)
                [command setRenderPipelineState:indirectPipeline];
            if (pbr) [command setFragmentBuffer:pbrArgs offset:0 atIndex:8];
            [command setVertexBuffer:vertices offset:vbOffset_ atIndex:0];
            for (int k = 0; k < uniformBindingCount; ++k) {
                const auto& binding = uniformBindings[k];
                [command setVertexBuffer:binding.buffer offset:0 atIndex:binding.index];
                // No fragment function means no consumer of fragment uniforms.
                // All other shaders preserve the original explicit bindings.
                if (!skipDepthFragmentUniforms)
                    [command setFragmentBuffer:binding.buffer offset:0 atIndex:binding.index];
            }
            [command drawIndexedPrimitives:icbPrimitive indexCount:args.indexCount
                indexType:icbIndexType
                indexBuffer:indices indexBufferOffset:ibOffset_ + size_t(args.firstIndex) * indexBytes
                instanceCount:args.instanceCount baseVertex:args.baseVertex baseInstance:args.firstInstance];
        }
        const auto afterEncode = std::chrono::steady_clock::now();
        // ICB resources are not automatically resident. Explicitly declare every
        // resource accessed by commands, even when the parent encoder also uses it.
        declareIndirectRead(vertices, MTLRenderStageVertex);
        declareIndirectRead(indices, MTLRenderStageVertex);
        if (pbr) {
            declareIndirectRead(pbrArgs, MTLRenderStageFragment);
            for (int slot : pbrSlots)
                declareIndirectRead(icbFragmentTextures_[slot], MTLRenderStageFragment);
        }
        for (int slot = 0; slot < 8; ++slot) if (uniforms[slot]) {
            declareIndirectRead(uniforms[slot], MTLRenderStageVertex | MTLRenderStageFragment);
        }
        // With inherited pipeline state, each ICB command sees exactly the
        // same per-batch compatible pipeline; restore the ordinary state so
        // subsequent direct fallback draws behave identically.
        if (inheritCpuIcbPipeline)
            [enc_ setRenderPipelineState:indirectPipeline];
        [enc_ executeCommandsInBuffer:icb withRange:NSMakeRange(0, executeCount)];
        if (emptyTail.active) {
            static bool reported = false;
            if (!reported) {
                reported = true;
                std::fprintf(stderr,
                    "[metal-icb-empty-tail] mode=%s valid=%u reset=%u execute=%u\n",
                    emptyTail.executedTail ? "on" : "off",
                    unsigned(drawCount), unsigned(allocatedCount),
                    unsigned(executeCount));
            }
        }
        if (inheritCpuIcbPipeline)
            [enc_ setRenderPipelineState:p->state];
        // Diagnostic only. Sample the four existing CPU ICB stage boundaries
        // after a submission, never inside the per-command encode loop.
        // Limit printing to avoid turning ordinary frames into a log benchmark.
        static const bool traceSlowIcb = [] {
            const char* flag = std::getenv("AIVERSE_ICB_SLOW_TRACE");
            return flag && std::strcmp(flag, "1") == 0;
        }();
        if (traceSlowIcb) {
            const auto afterExecute = std::chrono::steady_clock::now();
            const auto ms = [](auto begin, auto end) {
                return std::chrono::duration<double, std::milli>(end - begin).count();
            };
            const double argsMs = ms(profileStart, afterArgs);
            const double allocMs = ms(afterArgs, afterAlloc);
            const double icbResetMs = ms(afterArgs, afterIcbReset);
            const double uniformsMs = ms(afterIcbReset, afterAlloc);
            const double encodeMs = ms(afterAlloc, afterEncode);
            const double executeMs = ms(afterEncode, afterExecute);
            const double totalMs = argsMs + allocMs + encodeMs + executeMs;
            static const double slowMs = [] {
                const char* raw = std::getenv("AIVERSE_ICB_SLOW_TRACE_MS");
                if (!raw || !*raw) return 0.15;
                char* end = nullptr;
                const double v = std::strtod(raw, &end);
                return end != raw && *end == '\0' && std::isfinite(v) &&
                    v >= 0.01 && v <= 100.0 ? v : 0.15;
            }();
            // Select a warmed timeline window so the 24-event budget is
            // not exhausted by initial Metal ICB allocations. Optional slot
            // filtering samples equivalent command groups across frames.
            static const uint64_t fromFrame = [] {
                const char* raw = std::getenv("AIVERSE_ICB_SLOW_TRACE_FROM_FRAME");
                if (!raw || !*raw) return uint64_t(0);
                char* end = nullptr;
                const unsigned long n = std::strtoul(raw, &end, 10);
                return end != raw && *end == '\0' && n <= 1000000ul
                    ? uint64_t(n) : uint64_t(0);
            }();
            static const long selectedSlot = [] {
                const char* raw = std::getenv("AIVERSE_ICB_SLOW_TRACE_SLOT");
                if (!raw || !*raw) return -1l;
                char* end = nullptr;
                const long n = std::strtol(raw, &end, 10);
                return end != raw && *end == '\0' && n >= 0 && n <= 64
                    ? n : -1l;
            }();
            static uint32_t printed = 0;
            if (uint64_t(frame_) >= fromFrame &&
                (selectedSlot < 0 || submissionIndex == size_t(selectedSlot)) &&
                totalMs >= slowMs && printed < 24) {
                ++printed;
                std::fprintf(stderr,
                    "[metal-icb-slow] frame=%llu slot=%zu commands=%u total=%.5fms "
                    "args=%.5f alloc=%.5f icb_reset=%.5f uniforms=%.5f "
                    "encode=%.5f residency_execute=%.5f "
                    "threshold=%.3fms sample=%u/24\n",
                    static_cast<unsigned long long>(frame_), submissionIndex,
                    drawCount, totalMs, argsMs, allocMs, icbResetMs, uniformsMs,
                    encodeMs, executeMs, slowMs, printed);
            }
        }
        if (traceIcb) {
            static uint32_t submissions = 0;
            static uint64_t commandsEncoded = 0;
            static double totals[4] = {};
            const auto ms = [](auto a, auto b) {
                return std::chrono::duration<double, std::milli>(b - a).count();
            };
            totals[0] += ms(profileStart, afterArgs);
            totals[1] += ms(afterArgs, afterAlloc);
            totals[2] += ms(afterAlloc, afterEncode);
            totals[3] += ms(afterEncode, std::chrono::steady_clock::now());
            commandsEncoded += drawCount;
            // Override only the reporting interval, never a GPU/CPU path.
            // Small AUTO probes may submit far fewer than 600 ICBs.
            static const uint32_t reportEvery = [] {
                const char* raw = std::getenv("AIVERSE_ICB_PROFILE_REPORT_EVERY");
                if (!raw || !*raw) return 600u;
                char* end = nullptr;
                const long n = std::strtol(raw, &end, 10);
                return end != raw && *end == '\0' && n >= 1 && n <= 10000
                    ? uint32_t(n) : 600u;
            }();
            if (++submissions == reportEvery) {
                std::fprintf(stderr,
                    "[metal-icb] avg/%u submissions args=%.4f alloc=%.4f encode=%.4f residency-execute=%.4f ms commands/submission=%.1f\n",
                    reportEvery,
                    totals[0] / submissions, totals[1] / submissions,
                    totals[2] / submissions, totals[3] / submissions,
                    double(commandsEncoded) / submissions);
                submissions = 0;
                commandsEncoded = 0;
                for (double& value : totals) value = 0.0;
            }
        }
        return true;
    }

private:
    // The trace is strictly opt-in and separate from GPU timestamp counters,
    // which may be unavailable on this device. No implicit capture in runtime.
    bool gpuTraceConfigured_ = false;
    bool gpuTraceAttempted_ = false;
    bool gpuTraceActive_ = false;
    uint64_t gpuTraceFrame_ = 0;
    std::string gpuTracePath_;
    void maybeStartGpuTrace() {
#if !TARGET_OS_IPHONE
        if (gpuTraceAttempted_) return;
        if (!gpuTraceConfigured_) {
            gpuTraceConfigured_ = true;
            const char* rawFrame = std::getenv("AIVERSE_METAL_TRACE_FRAME");
            const char* rawPath = std::getenv("AIVERSE_METAL_TRACE_PATH");
            if (!rawFrame || !rawPath || !*rawPath) return;
            char* end = nullptr;
            const unsigned long long parsed = std::strtoull(rawFrame, &end, 10);
            if (end == rawFrame || *end != '\0' || parsed < 1 || parsed > 1000000ull) {
                std::fprintf(stderr, "[metal-gputrace] INVALID frame number\n");
                gpuTraceAttempted_ = true;
                return;
            }
            gpuTraceFrame_ = uint64_t(parsed);
            gpuTracePath_ = rawPath;
            if (gpuTracePath_.size() < 9 ||
                gpuTracePath_.substr(gpuTracePath_.size() - 9) != ".gputrace") {
                std::fprintf(stderr, "[metal-gputrace] INVALID path (need .gputrace)\n");
                gpuTraceAttempted_ = true;
                return;
            }
        }
        if (gpuTraceFrame_ != frame_) return;
        gpuTraceAttempted_ = true;
        MTLCaptureManager* manager = [MTLCaptureManager sharedCaptureManager];
        if (![manager supportsDestination:MTLCaptureDestinationGPUTraceDocument]) {
            std::fprintf(stderr, "[metal-gputrace] UNSUPPORTED: GPUTraceDocument destination\n");
            return;
        }
        MTLCaptureDescriptor* descriptor = [MTLCaptureDescriptor new];
        descriptor.captureObject = queue_;
        descriptor.destination = MTLCaptureDestinationGPUTraceDocument;
        NSString* filePath = [NSString stringWithUTF8String:gpuTracePath_.c_str()];
        descriptor.outputURL = [NSURL fileURLWithPath:filePath];
        NSError* error = nil;
        if (![manager startCaptureWithDescriptor:descriptor error:&error]) {
            const char* reason = error ? error.localizedDescription.UTF8String : "unknown";
            std::fprintf(stderr, "[metal-gputrace] FAILED frame=%llu reason=%s\n",
                         static_cast<unsigned long long>(frame_), reason);
            return;
        }
        gpuTraceActive_ = true;
        std::fprintf(stderr, "[metal-gputrace] START frame=%llu path=%s\n",
                     static_cast<unsigned long long>(frame_), gpuTracePath_.c_str());
#endif
    }
    std::unique_ptr<MetalShaderAssets> shaderAssets_;
    CAMetalLayer* layer_;
    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
    id<MTLCommandBuffer> cmd_;
    id<MTLCommandBuffer> lastCommittedGpuFrameBuffer_ = nil;
    uint64_t lastCommittedGpuFrame_ = 0;
    id<MTLRenderCommandEncoder> enc_;
    id<MTLComputeCommandEncoder> compute_;
    bool computeDispatchSucceeded_=false;
    Pool<id<MTLComputePipelineState>> computes_;
    std::shared_ptr<std::atomic<bool>> gpuHealthy_=std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<std::atomic<double>> gpuMs_=std::make_shared<std::atomic<double>>(0);
    // Shared with the completion callback: never capture a destroyed device.
    std::shared_ptr<std::atomic<uint64_t>> completedFrame_=
        std::make_shared<std::atomic<uint64_t>>(0);
    id<CAMetalDrawable> drawable_;
    id<MTLTexture> depth_;
    dispatch_semaphore_t frameSemaphore_;
    uint64_t frame_ = 0;
    // Shared with asynchronous MTL completion handlers; safe even if the
    // command buffer finishes after the MetalDevice owner has been released.
    std::shared_ptr<std::atomic<uint64_t>> completedGpuFrame_ =
        std::make_shared<std::atomic<uint64_t>>(0);
    uint64_t nextRetirementToken_ = 1;
    // Only the GPU callback modifies the atomic status. The token map is
    // accessed exclusively on the Metal device owner thread.
    std::map<uint64_t,std::shared_ptr<std::atomic<int>>> retirementTokens_;
    // Each frame slot is recycled only after its completion semaphore.
    // Each submission gets its own reusable resources to avoid overwriting
    // buffers referenced by earlier commands in the same frame.
    MTLIndirectCommandBufferDescriptor* modelIcbDescriptor_ = nil;
    struct IndirectResources {
        id<MTLIndirectCommandBuffer> icb = nil;
        uint32_t icbCapacity = 0;
        id<MTLBuffer> uniforms[8] = {};
        id<MTLBuffer> pbrArgs = nil;
    };
    struct GpuIcbNativeVtMaterial {
        TextureHandle atlasHandle, tableHandle, alphaHandle;
        id<MTLTexture> atlas = nil, table = nil, alpha = nil;
        float u6[4] = {};
        bool hasVt = true, hasPbr = false;
        std::array<TextureHandle,6> pbrHandles{};
        std::array<id<MTLTexture>,6> pbrTextures{};
        std::array<id<MTLSamplerState>,6> pbrSamplers{};
        float u2[36] = {};
        uint32_t graphTextureCount = 0;
        std::array<TextureHandle,4> graphHandles{};
        std::array<id<MTLTexture>,4> graphTextures{};
        std::array<id<MTLSamplerState>,4> graphSamplers{};
    };
    struct GpuEncodedIcbResources {
        std::vector<GpuIcbNativeVtMaterial> virtualMaterials;
        id<MTLIndirectCommandBuffer> icb = nil;
        id<MTLBuffer> argBuffer = nil;
        id<MTLBuffer> pbrArgs = nil;
        id<MTLBuffer> slots = nil;
        id<MTLBuffer> instanceBuffer = nil;
        size_t instanceValidBytes = 0;
        id<MTLBuffer> uniforms[8] = {};
        uint32_t capacity = 0, count = 0;
        uint64_t frame = 0;
    };
    id<MTLComputePipelineState> gpuIcbPipeline_ = nil;
    id<MTLComputeCommandEncoder> gpuIcbEncoder_ = nil;
    id<MTLArgumentEncoder> gpuIcbArgumentEncoder_ = nil;
    std::vector<GpuEncodedIcbResources> gpuEncodedIcbPool_[kFramesInFlight];
    size_t gpuEncodedIcbCursor_[kFramesInFlight] = {};
    std::vector<IndirectResources> indirectPool_[kFramesInFlight];
    size_t indirectCursor_[kFramesInFlight] = {};
    bool firstPass_ = true;
    bool screenReadable_ = false;   // setScreenReadable: drawables stay copyable (copyColor)
    bool hdrPass_ = false;
    int explicitPassSamples_ = 0;
    int explicitPassColors_ = -1; // -1: legacy beginPass (no MRT slot validation)
    PixelFormat explicitPassColorFormat_ = PixelFormat::RGBA8;
    DeviceInfo info_;
    std::string error_;
    Pool<MTBuffer> buffers_;
    Pool<MTTexture> textures_;
    Pool<MTTextureView> views_;
    Pool<id<MTLSamplerState>> samplers_;
    std::map<std::pair<std::string,bool>,std::shared_ptr<MTShaderCompilation>> asyncShaderCompiles_;
    struct IcbPbrShaderCache { id<MTLFunction> fragment; bool attempted = false; };
    std::map<std::pair<std::string,bool>, IcbPbrShaderCache> icbPbrShaders_;
    id<MTLTexture> icbFragmentTextures_[32] = {};
    id<MTLSamplerState> icbFragmentSamplers_[32] = {};
    Pool<MTShader> shaders_;
    Pool<MTPipeline> pipelines_;
    uint32_t pipelineId_ = 0, vbId_ = 0, ibId_ = 0;
    size_t vbOffset_ = 0, ibOffset_ = 0;
    std::vector<uint8_t> indirectUniformSnapshots_[8];
    int passW_ = 0, passH_ = 0;
};

} // namespace

std::unique_ptr<Device> createMetalDevice(void* nativeSurface) {
    return std::make_unique<MetalDevice>((__bridge CAMetalLayer*)nativeSurface);
}

} // namespace aiverse::gfx
