// Internal: the Vulkan gfx::Device. Split over GfxVulkan.cpp (instance, swapchain, frames),
// VkResources.cpp (memory, buffers, textures) and VkDraw.cpp (shaders, pipelines, passes, draws).
#pragma once
#include "Vulkan.h"
#include "VkApi.h"
#include "VkShaderCompiler.h"

#include <array>
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace aiverse::gfx::vk {

constexpr int kFramesInFlight = 2;
constexpr size_t kUniformRange = 4096;   // Device::maxUniformBytes: one descriptor range per uniform slot

template <class T> class Pool {
public:
    uint32_t add(T v) {
        if (!free_.empty()) { uint32_t i = free_.back(); free_.pop_back(); items_[i] = std::move(v); alive_[i] = 1; return i + 1; }
        items_.push_back(std::move(v)); alive_.push_back(1); return uint32_t(items_.size());
    }
    T* get(uint32_t id) { return id && id <= items_.size() && alive_[id - 1] ? &items_[id - 1] : nullptr; }
    const T* get(uint32_t id) const { return id && id <= items_.size() && alive_[id - 1] ? &items_[id - 1] : nullptr; }
    void remove(uint32_t id) { if (get(id)) { alive_[id - 1] = 0; items_[id - 1] = T{}; free_.push_back(id - 1); } }
    template <class F> void each(F&& f) { for (size_t i = 0; i < items_.size(); ++i) if (alive_[i]) f(items_[i]); }
    template <class F> void eachWithId(F&& f) {
        for (size_t i = 0; i < items_.size(); ++i)
            if (alive_[i]) f(uint32_t(i + 1), items_[i]);
    }
    template <class F> void eachWithId(F&& f) const {
        for (size_t i = 0; i < items_.size(); ++i)
            if (alive_[i]) f(uint32_t(i + 1), items_[i]);
    }
private:
    std::vector<T> items_; std::vector<uint8_t> alive_; std::vector<uint32_t> free_;
};

// Sub-allocation of device memory: 64 MB blocks per memory type, first fit. Linear (buffers) and
// optimal (images) resources never share a block, so bufferImageGranularity needs no care.
struct Allocation {
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkDeviceSize offset = 0, size = 0;
    void* mapped = nullptr;      // host-visible memory stays mapped
    int block = -1;              // -1: dedicated allocation
};

class MemoryAllocator {
public:
    void init(VkPhysicalDevice gpu, VkDevice device);
    void shutdown();
    bool allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags want, bool linear, Allocation& out);
    void release(Allocation& a);
private:
    struct Range { VkDeviceSize offset, size; };
    struct Block { VkDeviceMemory memory = VK_NULL_HANDLE; VkDeviceSize size = 0; uint32_t type = 0; bool linear = false; void* mapped = nullptr; std::vector<Range> free; };
    int findType(uint32_t bits, VkMemoryPropertyFlags want) const;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPhysicalDeviceMemoryProperties props_{};
    std::vector<Block> blocks_;
};

struct VBuffer {
    // syncedEpoch: the last shadow write this version contains (kUnsynced:
    // contents unknown, a full copy is needed before a partial write).
    static constexpr uint64_t kUnsynced = ~uint64_t(0);
    struct Version { VkBuffer buffer = VK_NULL_HANDLE; Allocation alloc; uint64_t usedSerial = 0;
                     uint64_t syncedEpoch = 0; };
    // Shadow writes since the oldest version's sync point, so switching to a
    // reused version copies only the ranges it missed instead of the whole
    // shadow. Bounded: dropped entries raise logBaseEpoch, and a version
    // synced before it falls back to a full copy.
    struct DirtyRange { size_t offset = 0, size = 0; uint64_t epoch = 0; };
    BufferType type = BufferType::Vertex;
    Usage usage = Usage::Immutable;
    size_t size = 0;
    bool host = false;                 // host-visible versions (Dynamic): a write never touches a version the GPU may read
    bool discardOnUpdate = false;      // offset-zero update replaces complete readable prefix
    BufferUpdateStats transferStats{};
    std::vector<Version> versions;
    int current = 0;
    std::vector<uint8_t> shadow;       // host buffers: the contents, copied into a fresh version
    uint64_t writeEpoch = 0, logBaseEpoch = 0;
    std::vector<DirtyRange> dirtyLog;
};

struct VTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    Allocation alloc;
    PixelFormat format = PixelFormat::RGBA8;
    VkFormat vkFormat = VK_FORMAT_UNDEFINED;
    int width = 0, height = 0, volumeDepth = 1, layers = 1, levels = 1;
    int samples = 1; // multisampled render-only images cannot be bound as sampled textures
    TextureDimension dimension = TextureDimension::Texture2D;
    bool depth = false, renderTarget = false, generateMips = false, compressed = false, storage = false;
};

struct VCompute {
    VkShaderModule shader = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    bool bufferPushConstants = false;
    uint32_t bufferBindings = 4;
};
struct VTextureView { VkImageView native = VK_NULL_HANDLE; uint32_t parentId = 0; };
struct VSampler { VkSampler native = VK_NULL_HANDLE; };

struct VShader {
    std::string vs, fs;                          // GLSL bodies (HDR variant compiled from them)
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    VkShaderModule hdrVert = VK_NULL_HANDLE, hdrFrag = VK_NULL_HANDLE;
    bool linear = false, hdrFailed = false;
    uint32_t shadowSlots = 0;
    // Vertex/fragment modules with UByte4 inputs rescaled (no USCALED fetch),
    // per (location mask << 1 | hdr); a null pair records a failed compile.
    std::vector<std::pair<uint64_t, std::pair<VkShaderModule, VkShaderModule>>> scaled;
};

struct VPipeline {
    PipelineDesc desc;
    struct Variant {
        uint64_t key;
        VkPipeline pipeline;
        int colorCount = 1;
        std::array<VkFormat, kMaxRenderTargetColors> formats{};
        int samples = 1;
        uint32_t resolveMask = 0;
    };
    std::vector<Variant> variants;               // per render pass format (and HDR shader)
};

struct FrameSlot {
    // Offscreen RT readbacks are owned by the submitting frame slot until its
    // fence signals. Never recycle staging storage while a copy is in flight.
    struct TextureCapture {
        VkBuffer buffer = VK_NULL_HANDLE;
        Allocation allocation{};
        int width=0, height=0;
        Device::CaptureFn callback;
    };
    std::vector<TextureCapture> textureCaptures;
    VkCommandPool pool = VK_NULL_HANDLE;
    VkCommandBuffer pre = VK_NULL_HANDLE, main = VK_NULL_HANDLE;
    bool preRecording = false;
    VkFence fence = VK_NULL_HANDLE;
    VkSemaphore acquired = VK_NULL_HANDLE;
    uint64_t serial = 0;                         // frame submitted with the fence
    std::vector<VkDescriptorPool> descriptorPools;
    size_t descriptorPool = 0;
    VkBuffer uniforms = VK_NULL_HANDLE;
    Allocation uniformAlloc;
    size_t uniformSize = 0, uniformOffset = 0;
    // AIVERSE_VK_GPU_TIMING: GPU timestamps of this slot's frame (labels per query)
    VkQueryPool timing = VK_NULL_HANDLE;
    uint32_t timingCount = 0;
    std::vector<std::string> timingLabels;   // one per query, "" = frame start
};

class VulkanDevice final : public Device {
public:
    VulkanDevice() = default;
    ~VulkanDevice() override;
    bool init(const VulkanWindow& window, std::string& error);
    bool setWindow(const VulkanWindow* window);

    const DeviceInfo& info() const override { return info_; }
    // Descriptor limits are device-dependent, not necessarily 16.
    int maxFragmentTextureSlots() const override { return textureSlots_; }
    uint64_t frameSerial() const override { return healthy_ ? serial_ : 0; }
    uint64_t completedFrameSerial() const override { return healthy_ ? completed_ : 0; }
    const std::string& lastError() const override { return error_; }
    bool supports(PixelFormat f) const override;
    std::vector<TextureResourceInfo> textureResources() const override;
    bool supportsCompute() const override { return device_ && healthy_ && computeQueue_ &&
        gpuProps_.limits.maxComputeWorkGroupInvocations >= 64 &&
        gpuProps_.limits.maxComputeWorkGroupSize[0] >= 64; }
    bool supportsStorageTexture(PixelFormat,TextureDimension) const override;
    ComputeHandle createCompute(const ComputeDesc&) override;
    void destroyCompute(ComputeHandle) override;
    bool beginCompute(ComputeHandle) override;
    void setComputeBuffer(int,BufferHandle) override;
    void setComputeTexture(int,TextureHandle) override;
    bool bindComputeStorageTexture(int,TextureHandle) override;
    void setComputeUniforms(const void*,size_t) override;
    void dispatch(uint32_t,uint32_t) override;
    bool dispatchChecked(uint32_t,uint32_t=1) override;
    bool readBufferAsync(BufferHandle,size_t,BufferReadFn) override;
    bool readBufferRangeAsync(BufferHandle,size_t,size_t,BufferReadFn) override;
    void endCompute() override;
    bool supportsTextureViews() const override { return true; }
    bool supportsTextureDimension(TextureDimension d) const override {
        return d != TextureDimension::CubeArray || cubeArray_;
    }

    BufferHandle createBuffer(const BufferDesc& d) override;
    void updateBuffer(BufferHandle h, const void* data, size_t size, size_t offset) override;
    bool updateSharedBufferChecked(BufferHandle h, const void* data,
                                   size_t size, size_t offset) override;
    bool bufferInfo(BufferHandle h, BufferInfo& out) const override;
    void destroyBuffer(BufferHandle h) override;
    BufferUpdateStats bufferUpdateStats(BufferHandle h) override;
    TextureHandle createTexture(const TextureDesc& d) override;
    void updateTexture(TextureHandle h, int x, int y, int w, int h2, const void* pixels, int rowPitchBytes) override;
    bool updateTextureRegion(TextureHandle h, const TextureUploadDesc& u) override;
    bool supportsStorageBufferTextureCopy() const override { return supportsCompute() && gpuHealthy(); }
    bool copyStorageBufferToTexture2D(BufferHandle,TextureHandle,const BufferTextureCopy2D&) override;
    bool textureInfo(TextureHandle h, TextureInfo& out) const override;
    void destroyTexture(TextureHandle h) override;
    TextureViewHandle createTextureView(TextureHandle texture, const TextureViewDesc& desc) override;
    void destroyTextureView(TextureViewHandle h) override;
    SamplerHandle createSampler(const SamplerDesc& desc) override;
    void destroySampler(SamplerHandle h) override;
    bool bindTextureView(int slot, TextureViewHandle view, SamplerHandle sampler = {}) override;
    ShaderHandle createShader(const ShaderDesc& d) override;
    void destroyShader(ShaderHandle h) override;
    PipelineHandle createPipeline(const PipelineDesc& d) override;
    void destroyPipeline(PipelineHandle h) override;
    bool copyColor(TextureHandle from, TextureHandle to) override;
    bool readTextureAsync(TextureHandle, TextureReadbackDesc, TextureReadFn) override;
    bool gpuHealthy() const override { return healthy_; }
    bool waitIdle() override;
    // serial_ names the currently recording VkFence-backed frame. This is
    // NOT evidence of completion. completed_ advances only after a real
    // vkWaitForFences return of VK_SUCCESS for its recorded frame slot.
    uint64_t gpuSubmissionFence() const override {
        return recording_ && healthy_ ? serial_ : 0;
    }
    uint64_t gpuCompletedFence() const override {
        return healthy_ ? completed_ : 0;
    }
    bool waitForSubmittedGpuWork() override;
    bool waitForResourceIdle() override;
    uint64_t beginResourceRetirement() override;
    bool resourceRetirementComplete(uint64_t token) override;
    ResourceRetirementStatus resourceRetirementStatus(uint64_t token) override;
    void endResourceRetirement(uint64_t token) override;
    void abandonRetirementTokenAfterIdle(uint64_t token) override;

    void beginFrame(int framebufferWidth, int framebufferHeight) override;
    void endFrame() override;
    void captureNextFrame(CaptureFn fn) override { capture_ = std::move(fn); }
    bool captureTexture(TextureHandle source,CaptureFn callback) override;
    bool readTexturePixelAsync(TextureHandle,int,int,PixelReadFn) override;

    void beginPass(const PassDesc& d) override;
    bool supportsRenderTargetPasses() const override { return true; }
    bool beginRenderTargets(const RenderTargetPassDesc& desc) override;
    void endPass() override;
    void setViewport(const Rect& r) override;
    void setScissor(const Rect& r) override;
    void setPipeline(PipelineHandle p) override;
    void setVertexBuffer(BufferHandle b, size_t offset) override;
    void setIndexBuffer(BufferHandle b, size_t offset) override;
    void setTexture(int slot, TextureHandle t) override;
    void setUniforms(int slot, const void* data, size_t size) override;
    void draw(uint32_t vertexCount, uint32_t firstVertex) override;
    void drawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t baseVertex, uint32_t instanceCount) override;
    void drawIndexedCanonical(uint32_t indexCount, uint32_t firstIndex,
                              int32_t baseVertex, uint32_t instanceCount) override;
    bool supportsMultiDrawIndirect() const override { return multiDrawIndirect_; }
    bool drawIndexedIndirect(BufferHandle commands, size_t offset,
                             uint32_t drawCount, uint32_t stride) override;

    // Single compute-authored draw: Indirect or Storage argument buffers
    // (both carry INDIRECT + STORAGE usage), no multiDrawIndirect needed.
    bool supportsGpuAuthoredIndirectDraw() const override {
        return drawIndirectFirstInstance_ && supportsCompute();
    }
    bool drawIndexedGpuIndirect(BufferHandle arguments, size_t offset) override;

private:
    // Shared encoder of validated indexed-indirect draws (VkDraw.cpp).
    bool encodeIndexedIndirect(BufferHandle commands, size_t offset,
                               uint32_t drawCount, uint32_t stride);
    // GfxVulkan.cpp
    bool createInstance(bool wantSurface, std::string& error);
    bool pickDevice(std::string& error);
    bool createSwapchain();
    void destroySwapchain();
    void destroyDeviceObjects();
    void ensureBackBuffer(int w, int h);
    void prepareSlot();                          // waits for the slot's previous frame, resets its pools
    void collectGarbage();
    void deliverPixelReads();  // called on owner thread at next beginFrame
    void present();
    void readBackBuffer();
    void completeTextureCaptures(FrameSlot& slot,bool succeeded);
    void deliverTextureReads();
    void deliverComputeReadbacks();
    // After device loss no fence signals again: complete every accepted
    // texture / buffer readback exactly once with an empty failure result.
    void failPendingReadsAfterDeviceLoss();
    void shutdownCompute();
    // VkResources.cpp
    bool createBufferObject(size_t size, VkBufferUsageFlags usage, bool host, VkBuffer& buf, Allocation& alloc);
    VkBufferUsageFlags bufferUsage(BufferType t) const;
    // Null on allocation failure. Never return an in-flight version as a
    // fallback: writing it corrupts commands already submitted to the GPU.
    VBuffer::Version* writableVersion(VBuffer& b, bool keepContents);
    void recordShadowWrite(VBuffer& b, VBuffer::Version& written,
                           size_t offset, size_t size, bool staleTail);
    bool updateBufferChecked(BufferHandle h, const void* data,
                             size_t size, size_t offset);
    VkBuffer resolveBuffer(uint32_t id, uint64_t serialUse);
    bool createImage(VTexture& t, VkImageUsageFlags usage);
    VkSampler samplerFor(const TextureDesc& d, const VTexture& t);
    void* stagingWrite(size_t size, VkBuffer& buf, VkDeviceSize& offset);
    bool uploadImage(VTexture& t, int level, int x, int y, int w, int h, const void* data, size_t bytes, VkCommandBuffer cb,
                     int z = 0, int depth = 1, int baseArrayLayer = 0, int layerCount = 1);
    void generateMips(VTexture& t, VkCommandBuffer cb);
    void initImageLayout(VTexture& t, VkCommandBuffer cb, bool clear);
    // VkDraw.cpp
    VkRenderPass renderPassFor(VkFormat color, VkFormat depth, bool clearColor, bool clearDepth);
    VkRenderPass renderPassForMrt(
        const std::array<VkFormat, kMaxRenderTargetColors>& formats,
        VkFormat depth, int colors, uint32_t clearMask, bool clearDepth,
        int samples = 1, uint32_t resolveMask = 0);
    VkFramebuffer framebufferFor(VkImageView color, VkImageView depth, VkRenderPass rp, int w, int h, uint32_t colorId, uint32_t depthId);
    VkPipeline pipelineFor(VPipeline& p,
                           const std::array<VkFormat, kMaxRenderTargetColors>& formats,
                           VkFormat depth, bool hdr, int colorCount = 1,
                           int samples = 1, uint32_t resolveMask = 0);
    VkShaderModule createModule(const std::vector<uint32_t>& code);
    bool prepareDraw();

public:   // native access for vendor libraries (Gfx.h NativeVulkanContext)
    bool nativeVulkanContext(NativeVulkanContext& out) override {
        if (!device_ || !recording_ || inPass_ || !healthy_) return false;
        out.instance = instance_;
        out.physicalDevice = gpu_;
        out.device = device_;
        out.commandBuffer = transferCmd();
        out.getInstanceProcAddr = reinterpret_cast<void*>(vkGetInstanceProcAddr);
        out.queueFamily = queueFamily_;
        return true;
    }
    bool nativeVulkanImage(TextureHandle h, NativeVulkanImage& out) const override {
        const VTexture* t = textures_.get(h.id);
        if (!t || !t->image) return false;
        out.image = t->image;
        out.view = t->view;
        out.format = uint32_t(t->vkFormat);
        out.width = t->width; out.height = t->height;
        out.depth = t->depth; out.storage = t->storage;
        return true;
    }
    bool nativeVulkanImageBarrier(TextureHandle h, uint32_t fromLayout, uint32_t toLayout) override {
        const VTexture* t = textures_.get(h.id);
        if (!t || !t->image || !recording_ || inPass_) return false;
        barrierImage(transferCmd(), t->image, t->depth, VkImageLayout(fromLayout), VkImageLayout(toLayout));
        return true;
    }

public:   // helpers shared by the translation units
    VkCommandBuffer transferCmd();               // main command buffer outside passes, else the pre-frame one
    void barrierImage(VkCommandBuffer cb, VkImage image, bool depth, VkImageLayout from, VkImageLayout to, uint32_t levels = VK_REMAINING_MIP_LEVELS,
                      uint32_t baseLevel = 0, uint32_t arrayLayers = 1);
    void deferDestroy(std::function<void()> fn) { garbage_.push_back({serial_, std::move(fn)}); }
    VkFormat vkFormat(PixelFormat f) const;

private:
    DeviceInfo info_;
    std::string error_;
    bool multiDrawIndirect_ = false;
    bool drawIndirectFirstInstance_ = false;
    bool cubeArray_ = false;
    bool computeQueue_ = false;
    bool samplerAnisotropy_ = false;
    bool healthy_ = true;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice gpu_ = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties gpuProps_{};
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    uint32_t queueFamily_ = 0;
    bool swapchainExt_ = false, surfaceInstance_ = false;
    MemoryAllocator memory_;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    int textureSlots_ = kMaxTextureSlots;
    VkFormat depthFormat_ = VK_FORMAT_D32_SFLOAT;
    bool depthLinear_ = true, uscaled_ = true;

    // window / swapchain
    VulkanWindow window_;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkFormat swapFormat_ = VK_FORMAT_B8G8R8A8_UNORM;
    VkExtent2D swapExtent_{};
    VkSurfaceTransformFlagBitsKHR swapTransform_ = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;   // caps.currentTransform at creation
    VkExtent2D swapCapsExtent_{};   // caps.currentExtent at creation (the swapchain may be smaller: Android)
    // VK_SUBOPTIMAL_KHR means "recreate" only when the surface really changed: Android reports it on every
    // present while preTransform (IDENTITY) differs from the display rotation (any landscape game).
    bool swapchainChanged();
    std::vector<VkImage> swapImages_;
    // Android pre-rotation: when the display is rotated (landscape on a
    // portrait panel) the swapchain is created in the panel's transform and
    // present() rotates the frame with a compute pass, so the compositor
    // does not rotate every frame. 0 = off, 1 = ROTATE_90, 2 = ROTATE_270.
    int rotatePresent_ = 0;
    // Backend experiment switches (read once at device creation):
    // AIVERSE_VK_GPU_TIMING=1  logs GPU time per render pass / present every 120 frames
    // AIVERSE_VK_DEPS=narrow   render pass dependencies limited to the stages that really touch attachments
    // AIVERSE_VK_DEPTH_STORE=0 depth/stencil are not written back to memory after a pass
    bool gpuTiming_ = false, narrowDeps_ = false, depthStore_ = true;
    bool preUploadAllowed_ = false;   // updateTexture() inside a pass: upload via the pre-frame buffer
    double timestampNs_ = 1.0;
    uint64_t timestampMask_ = ~0ull;
    std::map<std::string, std::pair<double, int>> timingAcc_;
    int timingFrames_ = 0;
    double timingFrameMs_ = 0.0;
    void timestamp(const std::string& label, VkPipelineStageFlagBits stage);
    void collectTiming(FrameSlot& s);
    std::vector<VkImageView> swapViews_;
    VkDescriptorSetLayout rotLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout rotPipeLayout_ = VK_NULL_HANDLE;
    VkPipeline rotPipe_ = VK_NULL_HANDLE;
    VkDescriptorPool rotPool_ = VK_NULL_HANDLE;
    VkDescriptorSet rotSets_[2] = {VK_NULL_HANDLE, VK_NULL_HANDLE};
    VkSampler rotSampler_ = VK_NULL_HANDLE;
    bool rotFailed_ = false;
    bool ensureRotatePipeline();
    std::vector<VkSemaphore> swapRendered_;
    uint32_t swapIndex_ = 0;
    int swapForW_ = 0, swapForH_ = 0;   // framebuffer size the swapchain was made for
    bool swapAcquired_ = false, swapDirty_ = false;

    // back buffer: the "screen" of the RHI, GL orientation (row 0 = bottom)
    VTexture back_, backDepth_;

    // frames
    std::array<FrameSlot, kFramesInFlight> slots_;
    int slot_ = 0;
    uint64_t serial_ = 1, completed_ = 0;
    uint64_t nextRetirementToken_ = 1;
    std::map<uint64_t,VkFence> retirementFences_;
    bool recording_ = false;
    struct Garbage { uint64_t serial; std::function<void()> fn; };
    std::vector<Garbage> garbage_;
    CaptureFn capture_;
    struct PixelRead {
        VkBuffer buffer = VK_NULL_HANDLE;
        Allocation alloc{};
        uint64_t serial = 0;
        PixelReadFn callback;
    };
    std::vector<PixelRead> pixelReads_;

    // resources
    Pool<VBuffer> buffers_;
    Pool<VTexture> textures_;
    Pool<VTextureView> views_;
    Pool<VSampler> customSamplers_;
    Pool<VShader> shaders_;
    Pool<VPipeline> pipelines_;
    Pool<VCompute> computes_;
    struct PendingComputeRead {
        uint64_t serial=0;
        VkBuffer buffer=VK_NULL_HANDLE;
        Allocation allocation{};
        size_t bytes=0;
        BufferReadFn callback;
    };
    std::vector<PendingComputeRead> computeReads_;

    VTexture dummy_;          // bound to unset texture slots
    VTexture dummyDepth_;     // ... of compare samplers
    struct SamplerKey { uint32_t key; VkSampler sampler; };
    std::vector<SamplerKey> samplers_;
    struct RenderPassKey { uint64_t key; VkRenderPass pass; };
    std::vector<RenderPassKey> renderPasses_;
    struct MrtRenderPass {
        std::array<VkFormat, kMaxRenderTargetColors> formats{};
        VkFormat depth = VK_FORMAT_UNDEFINED;
        int colorCount = 0;
        uint32_t clearMask = 0;
        bool clearDepth = false;
        int samples = 1;
        uint32_t resolveMask = 0;
        VkRenderPass pass = VK_NULL_HANDLE;
    };
    std::vector<MrtRenderPass> mrtRenderPasses_;
    struct FramebufferKey { uint32_t color, depth; VkRenderPass pass; int w, h; VkFramebuffer fb; };
    std::vector<FramebufferKey> framebuffers_;
    struct StagingChunk { VkBuffer buffer = VK_NULL_HANDLE; Allocation alloc; size_t size = 0, used = 0; uint64_t serial = 0; };
    std::vector<StagingChunk> staging_;
    struct TextureRead {
        VkBuffer buffer = VK_NULL_HANDLE;
        Allocation alloc;
        size_t bytes = 0;
        int width = 0, height = 0;
        PixelFormat format = PixelFormat::RGBA8;
        uint64_t serial = 0;
        TextureReadFn callback;
        // RHI render targets are stored bottom-up (GL convention); flip
        // them to the documented top-left row order. Uploaded/storage
        // images already have CPU row order.
        bool flipRows = false;
    };
    std::vector<TextureRead> textureReads_;

    // pass state
    bool inPass_ = false, passSkipped_ = false, hdrPass_ = false;
    // Explicit mip/layer passes allocate temporary 2D views + framebuffer.
    // Deferred destruction happens only after vkCmdEndRenderPass and GPU completion.
    VkFramebuffer scopedPassFb_ = VK_NULL_HANDLE;
    std::array<VkImageView, kMaxRenderTargetColors> scopedPassColors_{};
    std::array<VkImageView, kMaxRenderTargetColors> scopedPassResolves_{};
    VkImageView scopedPassDepth_ = VK_NULL_HANDLE;
    int passColors_ = 1, passSamples_ = 1;
    uint32_t passResolveMask_ = 0;
    bool explicitMrtPass_ = false;
    int passW_ = 0, passH_ = 0;
    VkFormat passColor_ = VK_FORMAT_UNDEFINED, passDepth_ = VK_FORMAT_UNDEFINED;
    std::array<VkFormat, kMaxRenderTargetColors> passFormats_{};
    uint32_t pipelineId_ = 0;
    VkPipeline boundPipeline_ = VK_NULL_HANDLE;
    uint32_t vbId_ = 0, ibId_ = 0;
    size_t vbOffset_ = 0, ibOffset_ = 0;
    std::array<uint32_t, kMaxTextureSlots> textures_bound_{};
    std::array<uint32_t, kMaxTextureSlots> views_bound_{};
    std::array<uint32_t, kMaxTextureSlots> samplers_bound_{};
    std::array<uint32_t, kUniformSlots> uniformOffsets_{};
    bool descriptorsDirty_ = true;
    VkDescriptorSet currentSet_ = VK_NULL_HANDLE;
    uint32_t activeComputeId_ = 0;
    std::array<TextureHandle,4> computeStorage_{};
    std::array<TextureHandle,8> computeSampled_{};
    std::array<BufferHandle,8> computeBuffers_{};
    bool computeStateValid_ = false;
    std::vector<uint8_t> computeUniforms_;
};

} // namespace aiverse::gfx::vk
