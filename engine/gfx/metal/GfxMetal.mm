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
#include <memory>
#include <map>
#include <cstring>
#include <cstdio>
#include <string>
#include <vector>

namespace aiverse::gfx {
namespace {

constexpr int kFramesInFlight = 3;

template <class T> class Pool {
public:
    uint32_t add(T v) {
        if (!free_.empty()) { uint32_t i = free_.back(); free_.pop_back(); items_[i] = std::move(v); alive_[i] = 1; return i + 1; }
        items_.push_back(std::move(v)); alive_.push_back(1); return uint32_t(items_.size());
    }
    T* get(uint32_t id) { return id && id <= items_.size() && alive_[id - 1] ? &items_[id - 1] : nullptr; }
    void remove(uint32_t id) { if (get(id)) { items_[id - 1] = T{}; alive_[id - 1] = 0; free_.push_back(id - 1); } }
private:
    std::vector<T> items_; std::vector<uint8_t> alive_; std::vector<uint32_t> free_;
};

struct MTBuffer {
    id<MTLBuffer> bufs[kFramesInFlight];
    int count = 1;          // 1 for immutable, kFramesInFlight for dynamic (one copy per frame in flight)
    size_t size = 0;
    BufferType type = BufferType::Vertex;
    bool storage = false,snapshot = false;
    uint64_t updatedFrame = UINT64_MAX;
    // Only for dynamic indirect command snapshots: per-frame, per-submission
    // allocations retained until this frame slot is safe to recycle.
    std::vector<id<MTLBuffer>> indirectSnapshots[kFramesInFlight];
    // Number of valid bytes in each snapshot (per slot/submission).
    // Kept in lockstep with indirectSnapshots to avoid comparing uninitialized data.
    std::vector<size_t> indirectSnapshotValid[kFramesInFlight];
    size_t indirectCursor = 0;
};
struct MTTexture { id<MTLTexture> tex; id<MTLSamplerState> sampler; PixelFormat fmt = PixelFormat::RGBA8; bool mips = false; };
struct MTShader { id<MTLFunction> vs; id<MTLFunction> fs; std::string source; bool linear = false; };
struct MTShaderCompilation { MTShader value; std::string error; std::atomic<bool> done{false}; };
struct MTPipeline { id<MTLRenderPipelineState> state; id<MTLRenderPipelineState> icbPbrState; id<MTLArgumentEncoder> icbPbrEncoder; id<MTLDepthStencilState> depth; PipelineDesc desc; PipelineHandle hdr; ShaderHandle hdrShader; bool hdrFailed = false; Device::ShaderJob hdrShaderJob; Device::PipelineJob hdrPipelineJob; };

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
    int maxFragmentTextureSlots() const override { return 31; }
    uint64_t frameSerial() const override { return frame_; }
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
            case PixelFormat::ETC2_RGB8: case PixelFormat::ETC2_RGBA8: case PixelFormat::ASTC_4x4: case PixelFormat::ASTC_6x6: return apple;
            case PixelFormat::BC1: case PixelFormat::BC3: case PixelFormat::BC7: return bc;
            default: return true;
        }
    }
    static MTLPixelFormat compressedFormat(PixelFormat f) {
        switch (f) {
            case PixelFormat::ETC2_RGB8: return MTLPixelFormatETC2_RGB8;
            case PixelFormat::ETC2_RGBA8: return MTLPixelFormatEAC_RGBA8;
            case PixelFormat::ASTC_4x4: return MTLPixelFormatASTC_4x4_LDR;
            case PixelFormat::ASTC_6x6: return MTLPixelFormatASTC_6x6_LDR;
#if !TARGET_OS_IPHONE
            case PixelFormat::BC1: return MTLPixelFormatBC1_RGBA;
            case PixelFormat::BC3: return MTLPixelFormatBC3_RGBA;
            case PixelFormat::BC7: return MTLPixelFormatBC7_RGBAUnorm;
#endif
            default: return MTLPixelFormatInvalid;
        }
    }
    const std::string& lastError() const override { return error_; }

    // ---------- buffers ----------
    BufferHandle createBuffer(const BufferDesc& d) override {
        MTBuffer b;
        b.size = d.size ? d.size : 16;
        b.type = d.type;
        b.snapshot = d.snapshotUpdates;
        b.storage = d.type == BufferType::Storage;
        b.count = !b.storage && d.usage == Usage::Dynamic ? kFramesInFlight : 1;
        for (int i = 0; i < b.count; ++i) {
            b.bufs[i] = d.data ? [device_ newBufferWithBytes:d.data length:b.size options:MTLResourceStorageModeShared]
                               : [device_ newBufferWithLength:b.size options:MTLResourceStorageModeShared];
        }
        for(int i=0;i<b.count;++i)if(!b.bufs[i]){error_="buffer allocation failed";return {};}
        return {buffers_.add(b)};
    }
    id<MTLBuffer> current(MTBuffer& b) { return b.bufs[b.count == 1 ? 0 : int(frame_ % kFramesInFlight)]; }
    void updateBuffer(BufferHandle h, const void* data, size_t size, size_t offset) override {
        auto* b = buffers_.get(h.id);
        if (!b) return;
        if (b->storage) {
            if (!data || offset > b->size || size > b->size-offset || enc_ || compute_) return;
            id<MTLBuffer> upload = [device_ newBufferWithBytes:data length:size options:MTLResourceStorageModeShared];
            if(!upload){error_="storage upload allocation failed";gpuHealthy_->store(false);return;}
            id<MTLCommandBuffer> cb = cmd_ ? cmd_ : [queue_ commandBuffer];
            id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
            [blit copyFromBuffer:upload sourceOffset:0 toBuffer:b->bufs[0] destinationOffset:offset size:size];
            [blit endEncoding];
            if (!cmd_) [cb commit];
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
            id<MTLBuffer> previous = cursor ? b->bufs[slot] : nil;
            id<MTLBuffer> target = cursor < snapshots.size() ? snapshots[cursor] : nil;
            const size_t capacity = std::max(b->size, required);
            if (!target || target.length < capacity) {
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
            // Full updates are the common model indirect path. If this
            // frame-slot/submission's commands have not changed, keep the
            // previous contents. The slot is safe after the GPU fence.
            // Partial updates preserve the previous submission's bytes.
            const bool fullUpdate = offset == 0 && size == required;
            static const bool dirtyIndirectUpdates = [] {
                const char* setting = std::getenv("AIVERSE_METAL_ICB_DIRTY_UPDATES");
                return setting && std::strcmp(setting, "1") == 0;
            }();
            const bool unchanged = dirtyIndirectUpdates && fullUpdate &&
                valid[cursor] >= size && std::memcmp(target.contents, data, size) == 0;
            if (!unchanged) {
                if (previous && (offset != 0 || size < b->size))
                    std::memcpy(target.contents, previous.contents,
                                std::min(size_t(target.length), size_t(previous.length)));
                std::memcpy(static_cast<uint8_t*>(target.contents) + offset, data, size);
            }
            valid[cursor] = std::max(valid[cursor], required);
            b->bufs[slot] = target;
            b->size = capacity;
            b->indirectCursor = cursor + 1;
            b->updatedFrame = frame_;
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
    void destroyBuffer(BufferHandle h) override { buffers_.remove(h.id); }
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
        auto* state=computes_.get(h.id);
        if (!cmd_ || enc_ || compute_ || gpuIcbEncoder_ || !state) return false;
        compute_=[cmd_ computeCommandEncoder];[compute_ setComputePipelineState:*state];return compute_!=nil;
    }
    void setComputeBuffer(int slot,BufferHandle h) override {
        if (auto* b=buffers_.get(h.id)) [compute_ setBuffer:current(*b) offset:0 atIndex:NSUInteger(slot)];
    }
    void setComputeTexture(int slot,TextureHandle h) override {
        if (auto* t=textures_.get(h.id)) [compute_ setTexture:t->tex atIndex:NSUInteger(slot)];
    }
    void setComputeUniforms(const void* data,size_t size) override { if(size<=4096)[compute_ setBytes:data length:size atIndex:8]; }
    void dispatch(uint32_t x,uint32_t y) override {
        if (!compute_ || !x || !y) return;
        [compute_ dispatchThreadgroups:MTLSizeMake((x+63)/64,y,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
        if (@available(macOS 10.14, iOS 12.0, *)) [compute_ memoryBarrierWithScope:MTLBarrierScopeBuffers|MTLBarrierScopeTextures];
    }
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
    void deliverBufferReads(){
        // Move ready entries before callbacks, allowing callbacks to enqueue other work.
        std::vector<std::shared_ptr<BufferRead>> ready;
        for(auto it=bufferReads_.begin();it!=bufferReads_.end();){
            if((*it)->ready.load(std::memory_order_acquire)){ready.push_back(*it);it=bufferReads_.erase(it);}else ++it;
        }
        for(auto& read:ready)read->callback(std::move(read->bytes));
    }
    bool gpuHealthy() const override { return gpuHealthy_->load(); }
    double gpuFrameMs() const override { return gpuMs_->load(); }

    // ---------- textures ----------
    TextureHandle createTexture(const TextureDesc& d) override {
        if (MTLPixelFormat cf = compressedFormat(d.format); cf != MTLPixelFormatInvalid) {
            MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:cf
                width:NSUInteger(d.width) height:NSUInteger(d.height) mipmapped:d.levelCount > 1 ? YES : NO];
            td.mipmapLevelCount = NSUInteger(std::max(1, d.levelCount));
            td.usage = MTLTextureUsageShaderRead;
            MTTexture t;
            t.fmt = d.format;
            t.tex = [device_ newTextureWithDescriptor:td];
            if (!t.tex) { error_ = "compressed texture not supported by this GPU"; return {}; }
            const bool bc = d.format == PixelFormat::BC1 || d.format == PixelFormat::BC3 || d.format == PixelFormat::BC7;
            const NSUInteger blockBytes = d.format == PixelFormat::ETC2_RGB8 || d.format == PixelFormat::BC1 ? 8 : 16;
            const NSUInteger blockDim = d.format == PixelFormat::ASTC_6x6 ? 6 : 4;
            for (int l = 0; l < d.levelCount; ++l) {
                const NSUInteger w = NSUInteger(std::max(1, d.width >> l)), h = NSUInteger(std::max(1, d.height >> l));
                [t.tex replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:NSUInteger(l)
                           withBytes:d.levelData[l] bytesPerRow:((w + blockDim - 1) / blockDim) * blockBytes];
            }
            (void)bc;
            MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
            sd.minFilter = sd.magFilter = d.filter == Filter::Linear ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
            if (d.levelCount > 1) sd.mipFilter = MTLSamplerMipFilterLinear;
            sd.sAddressMode = sd.tAddressMode = d.repeat ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
            t.sampler = [device_ newSamplerStateWithDescriptor:sd];
            return {textures_.add(t)};
        }
        MTLPixelFormat pf = d.format == PixelFormat::R8 ? MTLPixelFormatR8Unorm
                          : d.format == PixelFormat::RGBA16F ? MTLPixelFormatRGBA16Float
                          : d.format == PixelFormat::RGBA32F ? MTLPixelFormatRGBA32Float
                          : d.format == PixelFormat::Depth ? MTLPixelFormatDepth32Float
                          : d.format == PixelFormat::RGBA8_sRGB ? (d.renderTarget ? MTLPixelFormatBGRA8Unorm_sRGB : MTLPixelFormatRGBA8Unorm_sRGB)
                          : d.renderTarget ? MTLPixelFormatBGRA8Unorm   // same format as the screen: pipelines are shared
                          : MTLPixelFormatRGBA8Unorm;
        const bool mips = d.mipmaps && d.format != PixelFormat::Depth && d.format != PixelFormat::RGBA32F && !d.renderTarget;
        MTLTextureDescriptor* td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:pf
            width:NSUInteger(d.width) height:NSUInteger(d.height) mipmapped:mips ? YES : NO];
        td.usage = MTLTextureUsageShaderRead | (d.storage ? MTLTextureUsageShaderWrite : 0) | (d.renderTarget || d.format == PixelFormat::Depth ? MTLTextureUsageRenderTarget : 0);
        if (d.format == PixelFormat::Depth) td.storageMode = MTLStorageModePrivate;
        MTTexture t;
        t.fmt = d.format;
        t.tex = [device_ newTextureWithDescriptor:td];
        if(!t.tex){error_="texture allocation failed";return {};}
        if (d.data && d.format != PixelFormat::Depth) {
            int bpp = d.format == PixelFormat::R8 ? 1 : d.format == PixelFormat::RGBA32F ? 16 : d.format == PixelFormat::RGBA16F ? 8 : 4;
            [t.tex replaceRegion:MTLRegionMake2D(0, 0, NSUInteger(d.width), NSUInteger(d.height)) mipmapLevel:0
                       withBytes:d.data bytesPerRow:NSUInteger(d.width * bpp)];
            if (mips) generateMips(t.tex);
        }
        t.mips = mips;
        MTLSamplerDescriptor* sd = [MTLSamplerDescriptor new];
        sd.minFilter = sd.magFilter = d.filter == Filter::Linear && d.format != PixelFormat::RGBA32F ? MTLSamplerMinMagFilterLinear : MTLSamplerMinMagFilterNearest;
        if (mips) sd.mipFilter = d.filter == Filter::Linear ? MTLSamplerMipFilterLinear : MTLSamplerMipFilterNearest;
        sd.sAddressMode = sd.tAddressMode = d.repeat ? MTLSamplerAddressModeRepeat : MTLSamplerAddressModeClampToEdge;
        if (d.compare) sd.compareFunction = MTLCompareFunctionLessEqual;
        t.sampler = [device_ newSamplerStateWithDescriptor:sd];
        return {textures_.add(t)};
    }
    void updateTexture(TextureHandle h, int x, int y, int w, int hh, const void* pixels, int rowPitch) override {
        auto* t = textures_.get(h.id);
        if (!t) return;
        [t->tex replaceRegion:MTLRegionMake2D(NSUInteger(x), NSUInteger(y), NSUInteger(w), NSUInteger(hh)) mipmapLevel:0
                    withBytes:pixels bytesPerRow:NSUInteger(rowPitch)];
        if (t->mips) generateMips(t->tex);
    }
    // Own command buffer: textures are usually created while loading, outside of a frame.
    void generateMips(id<MTLTexture> tex) {
        id<MTLCommandBuffer> cb = [queue_ commandBuffer];
        id<MTLBlitCommandEncoder> blit = [cb blitCommandEncoder];
        [blit generateMipmapsForTexture:tex];
        [blit endEncoding];
        [cb commit];
    }
    void destroyTexture(TextureHandle h) override { textures_.remove(h.id); }

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
        pd.colorAttachments[0].pixelFormat = d.depthOnly ? MTLPixelFormatInvalid
                                           : d.colorFormat == PixelFormat::RGBA16F ? MTLPixelFormatRGBA16Float
                                           : d.colorFormat == PixelFormat::RGBA8_sRGB ? MTLPixelFormatBGRA8Unorm_sRGB   // sRGB render targets
                                           : layer_.pixelFormat;
        pd.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float;
        auto* ca = pd.colorAttachments[0];
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
        if (src.find("#define AIVERSE_MODEL_SSR 1") == std::string::npos ||
            src.find("#define LIGHTMAP 1") != std::string::npos ||
            src.find("#define PROBES 1") != std::string::npos ||
            src.find("#define SKINNED 1") != std::string::npos ||
            src.find("#define BAKED 1") != std::string::npos ||
            src.find("#define MORPH 1") != std::string::npos ||
            src.find("#define MATERIAL_PALETTE 1") != std::string::npos ||
            src.find("#define SG_SAMPLE(") != std::string::npos) return;

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
        pipeline.icbPbrState = state;
        pipeline.icbPbrEncoder = encoder;
    }
    PipelineHandle createPipeline(const PipelineDesc& d) override {
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
    bool warmPipelineColor(PipelineHandle h, PixelFormat format) override {
        auto* p=pipelines_.get(h.id);
        if (!p || format!=PixelFormat::RGBA16F || p->desc.depthOnly || p->desc.colorFormat==format || p->hdr.valid() || p->hdrFailed) return true;
        if (!p->hdrShader.valid()) {
            if (!p->hdrShaderJob) {
                auto* shader=shaders_.get(p->desc.shader.id);
                if (!shader) { p->hdrFailed=true; return true; }
                ShaderDesc desc; desc.msl=shader->source.c_str(); desc.linearTarget=true;
                p->hdrShaderJob=createShaderAsync(desc);
            }
            if (!p->hdrShaderJob(p->hdrShader)) return false;
            p->hdrShaderJob={};
            if (!p->hdrShader.valid()) { p->hdrFailed=true; std::fprintf(stderr,"[gfx] HDR warmup shader: %s\n",error_.c_str()); return true; }
        }
        if (!p->hdrPipelineJob) {
            auto desc=p->desc; desc.shader=p->hdrShader; desc.colorFormat=format;
            p->hdrPipelineJob=createPipelineAsync(desc);
        }
        // Publishing may reallocate the pool, including the job itself. Poll a local copy.
        auto job=p->hdrPipelineJob; PipelineHandle result;
        if (!job(result)) return false;
        p=pipelines_.get(h.id);
        p->hdrPipelineJob={}; p->hdr=result; p->hdrFailed=!result.valid();
        if (p->hdrFailed) std::fprintf(stderr,"[gfx] HDR warmup pipeline: %s\n",error_.c_str());
        return true;
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
    void captureNextFrame(CaptureFn fn) override {
        capture_ = std::move(fn);
        layer_.framebufferOnly = NO; // drawables acquired from now on can be blitted
        captureDelay_ = drawable_ && !drawable_.texture.framebufferOnly ? 0 : 1;
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

    void beginFrame(int w, int h) override {
        fbW_ = w; fbH_ = h;
        dispatch_semaphore_wait(frameSemaphore_, DISPATCH_TIME_FOREVER);
        deliverBufferReads();
        deliverCapture(); // the previous frame's GPU work is done here
        ++frame_;
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
        auto timing=gpuMs_;auto health=gpuHealthy_;
        [cmd_ addCompletedHandler:^(id<MTLCommandBuffer> completed) {
            if(completed.status==MTLCommandBufferStatusError) {
                health->store(false);
                const char* description = completed.error ? completed.error.localizedDescription.UTF8String : "unknown";
                std::fprintf(stderr, "[metal] command buffer failed: %s\\n", description);
            }
            if(completed.status==MTLCommandBufferStatusCompleted)timing->store((completed.GPUEndTime-completed.GPUStartTime)*1000);
        }];
        firstPass_ = true;
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
        if (drawable_) [cmd_ presentDrawable:drawable_];
        [cmd_ commit];
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
    void beginPass(const PassDesc& d) override {
        // Do not allow an unfinished GPU ICB compute pass to overlap a
        // render encoder, even if a future caller omits the explicit finish.
        finishGpuEncodedIcbPreparation();
        clearBindings();
        const auto* color = textures_.get(d.colorTarget.id);
        hdrPass_ = color && color->fmt == PixelFormat::RGBA16F;
        if (!cmd_) return;
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
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
        // Both Metal ICB paths are deliberately opt-in. CPU Multi-Draw is
        // independently testable without enabling GPU-authored commands.
        // Historic AIVERSE_EXPERIMENTAL_METAL_ICB continues to enable the
        // CPU path for old A/B scripts; neither flag is a production default.
        const char* cpu = std::getenv("AIVERSE_METAL_CPU_MDI");
        const char* experimental = std::getenv("AIVERSE_EXPERIMENTAL_METAL_ICB");
        if (!(cpu && std::strcmp(cpu, "1") == 0) &&
            !(experimental && std::strcmp(experimental, "1") == 0)) return false;
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
struct Params { uint count, indexCapacity, pad0, pad1; };
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
    command.set_fragment_buffer(uniform2, 3);
    command.set_fragment_buffer(uniform3, 4);
    command.set_fragment_buffer(uniform4, 5);
    command.set_fragment_buffer(uniform5, 6);
    command.set_fragment_buffer(uniform6, 7);
    // Fragment slot 8 is reserved for the PBR argument-buffer variant.
    // U7 is still available at vertex slot 8.
    command.set_fragment_buffer(pbrArguments, 8);
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
        // The GPU encoder references persistent uniform allocations, not
        // transient parent setBytes or parent buffer inheritance. The values
        // are filled from the correct render pass at execution time.
        constexpr size_t kGpuIcbUniformCapacity = 4096;
        for (int slot = 1; slot < 8; ++slot) {
            if (!resources.uniforms[slot] ||
                resources.uniforms[slot].length < kGpuIcbUniformCapacity)
                resources.uniforms[slot] = [device_ newBufferWithLength:
                    kGpuIcbUniformCapacity options:MTLResourceStorageModeShared];
            if (!resources.uniforms[slot]) return 0;
        }
        if (!resources.pbrArgs || resources.pbrArgs.length < kGpuIcbUniformCapacity)
            resources.pbrArgs = [device_ newBufferWithLength:
                kGpuIcbUniformCapacity options:MTLResourceStorageModeShared];
        if (!resources.pbrArgs) return 0;
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
        struct Params { uint32_t count, indexCapacity, padding[2]; };
        const size_t indexCount = indexData->size / sizeof(uint32_t);
        if (indexCount > UINT32_MAX) return 0;
        Params params{count, uint32_t(indexCount), {0,0}};
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
        const bool usePbrVariant = !pipeline->state.supportIndirectCommandBuffers;
        // GPU command encoder uses uint32 index addresses. A future uint16
        // specialization must be explicit rather than reinterpret indices.
        if (pipeline->desc.indexType != IndexType::UInt32)
            return reject("GPU ICB requires uint32 indices");
        id<MTLRenderPipelineState> icbPipeline =
            usePbrVariant ? pipeline->icbPbrState : pipeline->state;
        if (!icbPipeline || !icbPipeline.supportIndirectCommandBuffers)
            return reject("ICB-compatible pipeline unavailable (PBR variant not compiled)");
        const size_t frameSlot = frame_ % kFramesInFlight;
        auto& pool = gpuEncodedIcbPool_[frameSlot];
        if (size_t(token) > pool.size())
            return reject("ICB token outside current frame pool");
        auto& resources = pool[token - 1];
        if (resources.frame != frame_ || !resources.icb || resources.count == 0)
            return reject("ICB not encoded for current frame");
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
            if (!stable || !stable.contents || snapshot.size() > stable.length)
                return reject("uniform snapshot exceeds encoded GPU ICB buffer");
            // The command was GPU-encoded to refer to this buffer. Never
            // replace it after encoding: only update the existing contents.
            if (!snapshot.empty())
                std::memcpy(stable.contents, snapshot.data(), snapshot.size());
            declareIndirectRead(stable, MTLRenderStageVertex | MTLRenderStageFragment);
        }
        if (usePbrVariant) {
            if (!pipeline->icbPbrEncoder)
                return reject("PBR ICB fragment argument encoder missing");
            constexpr int pbrSlots[] = {0,1,2,3,4,5,6,9,10,11,12};
            for (int slot : pbrSlots)
                if (!icbFragmentTextures_[slot] || !icbFragmentSamplers_[slot])
                    return reject("PBR ICB required texture or sampler not bound");
            const NSUInteger size = pipeline->icbPbrEncoder.encodedLength;
            // The compute kernel already recorded this exact buffer pointer.
            // Growing/replacing it here would make the encoded ICB stale.
            if (!resources.pbrArgs || resources.pbrArgs.length < size)
                return reject("PBR ICB argument buffer exceeds encoded capacity");
            [pipeline->icbPbrEncoder setArgumentBuffer:resources.pbrArgs offset:0];
            for (int slot : pbrSlots) {
                [pipeline->icbPbrEncoder setTexture:icbFragmentTextures_[slot]
                                            atIndex:NSUInteger(slot * 2)];
                [pipeline->icbPbrEncoder setSamplerState:icbFragmentSamplers_[slot]
                                                 atIndex:NSUInteger(slot * 2 + 1)];
                declareIndirectRead(icbFragmentTextures_[slot], MTLRenderStageFragment);
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
        if (pbr) {
            if (!p->icbPbrEncoder) return false;
            for (int slot : pbrSlots)
                if (!icbFragmentTextures_[slot] || !icbFragmentSamplers_[slot]) return false;
            const NSUInteger bytes = p->icbPbrEncoder.encodedLength;
            if (!resources.pbrArgs || resources.pbrArgs.length < bytes)
                resources.pbrArgs = [device_ newBufferWithLength:bytes options:MTLResourceStorageModeShared];
            pbrArgs = resources.pbrArgs;
            if (!pbrArgs) return false;
            [p->icbPbrEncoder setArgumentBuffer:pbrArgs offset:0];
            for (int slot : pbrSlots) {
                [p->icbPbrEncoder setTexture:icbFragmentTextures_[slot] atIndex:NSUInteger(slot * 2)];
                [p->icbPbrEncoder setSamplerState:icbFragmentSamplers_[slot] atIndex:NSUInteger(slot * 2 + 1)];
            }
        }

        const auto afterArgs = std::chrono::steady_clock::now();
        // Driver crash fix: explicit pipeline and vertex/fragment buffer bindings.
        // Inheriting buffers with maxVertexBufferBindCount=0 caused AGX G15 to
        // fault inside executeCommandsInBufferCommon on Apple Silicon.
        // Descriptor configuration is identical for every model ICB. Keep one
        // immutable descriptor rather than allocating an Obj-C object per draw.
        if (!modelIcbDescriptor_) {
            modelIcbDescriptor_ = [MTLIndirectCommandBufferDescriptor new];
            modelIcbDescriptor_.commandTypes = MTLIndirectCommandTypeDrawIndexed;
            modelIcbDescriptor_.inheritBuffers = NO;
            modelIcbDescriptor_.inheritPipelineState = NO;
            modelIcbDescriptor_.maxVertexBufferBindCount = 9;
            modelIcbDescriptor_.maxFragmentBufferBindCount = 9;
        }
        MTLIndirectCommandBufferDescriptor* descriptor = modelIcbDescriptor_;
        if (!resources.icb || resources.icbCapacity < drawCount) {
            resources.icb = [device_ newIndirectCommandBufferWithDescriptor:descriptor
                maxCommandCount:drawCount options:MTLResourceStorageModeShared];
            if (!resources.icb) return false;
            resources.icbCapacity = drawCount;
        }
        id<MTLIndirectCommandBuffer> icb = resources.icb;
        // Only commands used by this submission need resetting. Larger pooled
        // capacity is untouched and cannot execute (execute range is drawCount).
        [icb resetWithRange:NSMakeRange(0, drawCount)];

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
        const auto afterAlloc = std::chrono::steady_clock::now();
        const bool u32 = p->desc.indexType == IndexType::UInt32;
        const size_t indexBytes = u32 ? 4u : 2u;
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
            [command setRenderPipelineState:indirectPipeline];
            if (pbr) [command setFragmentBuffer:pbrArgs offset:0 atIndex:8];
            [command setVertexBuffer:vertices offset:vbOffset_ atIndex:0];
            for (int slot = 0; slot < 8; ++slot) if (uniforms[slot]) {
                [command setVertexBuffer:uniforms[slot] offset:0 atIndex:NSUInteger(1 + slot)];
                [command setFragmentBuffer:uniforms[slot] offset:0 atIndex:NSUInteger(1 + slot)];
            }
            [command drawIndexedPrimitives:prim() indexCount:args.indexCount
                indexType:u32 ? MTLIndexTypeUInt32 : MTLIndexTypeUInt16
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
        [enc_ executeCommandsInBuffer:icb withRange:NSMakeRange(0, drawCount)];
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
            if (++submissions == 600) {
                std::fprintf(stderr,
                    "[metal-icb] avg/600 submissions args=%.4f alloc=%.4f encode=%.4f residency-execute=%.4f ms commands/submission=%.1f\n",
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
    std::unique_ptr<MetalShaderAssets> shaderAssets_;
    CAMetalLayer* layer_;
    id<MTLDevice> device_;
    id<MTLCommandQueue> queue_;
    id<MTLCommandBuffer> cmd_;
    id<MTLRenderCommandEncoder> enc_;
    id<MTLComputeCommandEncoder> compute_;
    Pool<id<MTLComputePipelineState>> computes_;
    std::shared_ptr<std::atomic<bool>> gpuHealthy_=std::make_shared<std::atomic<bool>>(true);
    std::shared_ptr<std::atomic<double>> gpuMs_=std::make_shared<std::atomic<double>>(0);
    id<CAMetalDrawable> drawable_;
    id<MTLTexture> depth_;
    dispatch_semaphore_t frameSemaphore_;
    uint64_t frame_ = 0;
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
    struct GpuEncodedIcbResources {
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
    DeviceInfo info_;
    std::string error_;
    Pool<MTBuffer> buffers_;
    Pool<MTTexture> textures_;
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
