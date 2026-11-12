// Metal backend (macOS, iOS). Compiled as Objective-C++ with ARC (-fobjc-arc).
#include "../Gfx.h"
#include "../ColorSpace.h"

#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include <TargetConditionals.h>

#include <algorithm>
#include <atomic>
#include <memory>
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
    bool storage = false,snapshot = false;
    uint64_t updatedFrame = UINT64_MAX;
};
struct MTTexture { id<MTLTexture> tex; id<MTLSamplerState> sampler; PixelFormat fmt = PixelFormat::RGBA8; bool mips = false; };
struct MTShader { id<MTLFunction> vs; id<MTLFunction> fs; std::string source; bool linear = false; };
struct MTPipeline { id<MTLRenderPipelineState> state; id<MTLDepthStencilState> depth; PipelineDesc desc; PipelineHandle hdr; ShaderHandle hdrShader; bool hdrFailed = false; };

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
    }

    const DeviceInfo& info() const override { return info_; }
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
            case PixelFormat::ETC2_RGB8: case PixelFormat::ETC2_RGBA8: case PixelFormat::ASTC_4x4: return apple;
            case PixelFormat::BC1: case PixelFormat::BC3: case PixelFormat::BC7: return bc;
            default: return true;
        }
    }
    static MTLPixelFormat compressedFormat(PixelFormat f) {
        switch (f) {
            case PixelFormat::ETC2_RGB8: return MTLPixelFormatETC2_RGB8;
            case PixelFormat::ETC2_RGBA8: return MTLPixelFormatEAC_RGBA8;
            case PixelFormat::ASTC_4x4: return MTLPixelFormatASTC_4x4_LDR;
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
        if (!cmd_ || enc_ || compute_ || !state) return false;
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
            for (int l = 0; l < d.levelCount; ++l) {
                const NSUInteger w = NSUInteger(std::max(1, d.width >> l)), h = NSUInteger(std::max(1, d.height >> l));
                [t.tex replaceRegion:MTLRegionMake2D(0, 0, w, h) mipmapLevel:NSUInteger(l)
                           withBytes:d.levelData[l] bytesPerRow:((w + 3) / 4) * blockBytes];
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
        id<MTLLibrary> lib = [device_ newLibraryWithSource:[NSString stringWithUTF8String:(colorShaderPrefix(true, d.linearTarget) + d.msl).c_str()] options:nil error:&err];
        if (!lib) { error_ = std::string("MSL compile: ") + (err ? err.localizedDescription.UTF8String : "?"); return {}; }
        MTShader s;
        s.source = d.msl; s.linear = d.linearTarget;
        s.vs = [lib newFunctionWithName:@"vs_main"];
        s.fs = [lib newFunctionWithName:@"fs_main"]; // optional for depth-only shaders
        if (!s.vs) { error_ = "MSL: vs_main not found"; return {}; }
        return {shaders_.add(s)};
    }
    void destroyShader(ShaderHandle h) override { shaders_.remove(h.id); }

    PipelineHandle createPipeline(const PipelineDesc& d) override {
        auto* s = shaders_.get(d.shader.id);
        if (!s) { error_ = "pipeline: invalid shader"; return {}; }
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
        } else if (d.blend != BlendMode::Opaque) {
            ca.blendingEnabled = YES;
            ca.rgbBlendOperation = ca.alphaBlendOperation = MTLBlendOperationAdd;
            ca.sourceRGBBlendFactor = d.blend == BlendMode::Premultiplied ? MTLBlendFactorOne : MTLBlendFactorSourceAlpha;
            ca.destinationRGBBlendFactor = d.blend == BlendMode::Additive ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
            ca.sourceAlphaBlendFactor = MTLBlendFactorOne;
            ca.destinationAlphaBlendFactor = d.blend == BlendMode::Additive ? MTLBlendFactorOne : MTLBlendFactorOneMinusSourceAlpha;
        }
        NSError* err = nil;
        MTPipeline p;
        p.desc = d;
        p.state = [device_ newRenderPipelineStateWithDescriptor:pd error:&err];
        if (!p.state) { error_ = std::string("pipeline: ") + (err ? err.localizedDescription.UTF8String : "?"); return {}; }
        MTLDepthStencilDescriptor* dd = [MTLDepthStencilDescriptor new];
        dd.depthCompareFunction = d.depthTest ? MTLCompareFunctionLessEqual : MTLCompareFunctionAlways;
        dd.depthWriteEnabled = d.depthWrite ? YES : NO;
        p.depth = [device_ newDepthStencilStateWithDescriptor:dd];
        return {pipelines_.add(p)};
    }
    void destroyPipeline(PipelineHandle h) override {
        if (auto* p = pipelines_.get(h.id)) {
            const auto variant = p->hdr; const auto shader = p->hdrShader;
            pipelines_.remove(h.id);
            if (variant.valid()) destroyPipeline(variant);
            if (shader.valid()) destroyShader(shader);
        }
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
        uniformPage_=uniformOffset_=0;
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
            if(completed.status==MTLCommandBufferStatusError)health->store(false);
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
        auto* b = buffers_.get(h.id);
        if(enc_&&b){auto buffer=current(*b);if(bindingCacheEnabled_&&boundVertexBuffer_==buffer&&boundVertexOffset_==offset)return;
            [enc_ setVertexBuffer:buffer offset:offset atIndex:0];if(bindingCacheEnabled_){boundVertexBuffer_=buffer;boundVertexOffset_=offset;}}
    }
    void setIndexBuffer(BufferHandle h, size_t offset) override { ibId_ = h.id; ibOffset_ = offset; }
    void setTexture(int slot, TextureHandle h) override {
        auto* t = textures_.get(h.id);
        if (!enc_ || !t) return;
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
    void setUniforms(int slot, const void* data, size_t size) override {
        if (!enc_) return;
        if(size>4096) {
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

private:
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
    bool firstPass_ = true;
    bool screenReadable_ = false;   // setScreenReadable: drawables stay copyable (copyColor)
    bool hdrPass_ = false;
    DeviceInfo info_;
    std::string error_;
    Pool<MTBuffer> buffers_;
    Pool<MTTexture> textures_;
    Pool<MTShader> shaders_;
    Pool<MTPipeline> pipelines_;
    uint32_t pipelineId_ = 0, ibId_ = 0;
    size_t ibOffset_ = 0;
    int passW_ = 0, passH_ = 0;
};

} // namespace

std::unique_ptr<Device> createMetalDevice(void* nativeSurface) {
    return std::make_unique<MetalDevice>((__bridge CAMetalLayer*)nativeSurface);
}

} // namespace aiverse::gfx
