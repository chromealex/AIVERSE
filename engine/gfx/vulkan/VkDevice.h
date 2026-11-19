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
    void remove(uint32_t id) { if (get(id)) { alive_[id - 1] = 0; items_[id - 1] = T{}; free_.push_back(id - 1); } }
    template <class F> void each(F&& f) { for (size_t i = 0; i < items_.size(); ++i) if (alive_[i]) f(items_[i]); }
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
    struct Version { VkBuffer buffer = VK_NULL_HANDLE; Allocation alloc; uint64_t usedSerial = 0; };
    BufferType type = BufferType::Vertex;
    Usage usage = Usage::Immutable;
    size_t size = 0;
    bool host = false;                 // host-visible versions (Dynamic): a write never touches a version the GPU may read
    std::vector<Version> versions;
    int current = 0;
    std::vector<uint8_t> shadow;       // host buffers: the contents, copied into a fresh version
};

struct VTexture {
    VkImage image = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkSampler sampler = VK_NULL_HANDLE;
    Allocation alloc;
    PixelFormat format = PixelFormat::RGBA8;
    VkFormat vkFormat = VK_FORMAT_UNDEFINED;
    int width = 0, height = 0, levels = 1;
    bool depth = false, renderTarget = false, generateMips = false, compressed = false;
};

struct VShader {
    std::string vs, fs;                          // GLSL bodies (HDR variant compiled from them)
    VkShaderModule vert = VK_NULL_HANDLE, frag = VK_NULL_HANDLE;
    VkShaderModule hdrVert = VK_NULL_HANDLE, hdrFrag = VK_NULL_HANDLE;
    bool linear = false, hdrFailed = false;
    uint32_t shadowSlots = 0;
};

struct VPipeline {
    PipelineDesc desc;
    struct Variant { uint64_t key; VkPipeline pipeline; };
    std::vector<Variant> variants;               // per render pass format (and HDR shader)
};

struct FrameSlot {
    // Offscreen RT readbacks are owned by the submitting frame slot until its
    // fence signals. Never recycle staging storage while a copy is in flight.
    struct TextureCapture {
        VkBuffer buffer = VK_NULL_HANDLE;
        Allocation allocation{};
        int width=0, height=0;
        CaptureFn callback;
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
};

class VulkanDevice final : public Device {
public:
    VulkanDevice() = default;
    ~VulkanDevice() override;
    bool init(const VulkanWindow& window, std::string& error);
    bool setWindow(const VulkanWindow* window);

    const DeviceInfo& info() const override { return info_; }
    const std::string& lastError() const override { return error_; }
    bool supports(PixelFormat f) const override;

    BufferHandle createBuffer(const BufferDesc& d) override;
    void updateBuffer(BufferHandle h, const void* data, size_t size, size_t offset) override;
    void destroyBuffer(BufferHandle h) override;
    TextureHandle createTexture(const TextureDesc& d) override;
    void updateTexture(TextureHandle h, int x, int y, int w, int h2, const void* pixels, int rowPitchBytes) override;
    void destroyTexture(TextureHandle h) override;
    ShaderHandle createShader(const ShaderDesc& d) override;
    void destroyShader(ShaderHandle h) override;
    PipelineHandle createPipeline(const PipelineDesc& d) override;
    void destroyPipeline(PipelineHandle h) override;
    bool copyColor(TextureHandle from, TextureHandle to) override;
    bool gpuHealthy() const override { return healthy_; }
    bool waitIdle() override;
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

    void beginPass(const PassDesc& d) override;
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
    bool supportsMultiDrawIndirect() const override { return multiDrawIndirect_; }
    bool drawIndexedIndirect(BufferHandle commands, size_t offset,
                             uint32_t drawCount, uint32_t stride) override;

private:
    // GfxVulkan.cpp
    bool createInstance(bool wantSurface, std::string& error);
    bool pickDevice(std::string& error);
    bool createSwapchain();
    void destroySwapchain();
    void destroyDeviceObjects();
    void ensureBackBuffer(int w, int h);
    void prepareSlot();                          // waits for the slot's previous frame, resets its pools
    void collectGarbage();
    void present();
    void readBackBuffer();
    void completeTextureCaptures(FrameSlot& slot,bool succeeded);
    // VkResources.cpp
    bool createBufferObject(size_t size, VkBufferUsageFlags usage, bool host, VkBuffer& buf, Allocation& alloc);
    VkBufferUsageFlags bufferUsage(BufferType t) const;
    VBuffer::Version& writableVersion(VBuffer& b, bool keepContents);
    VkBuffer resolveBuffer(uint32_t id, uint64_t serialUse);
    bool createImage(VTexture& t, VkImageUsageFlags usage);
    VkSampler samplerFor(const TextureDesc& d, const VTexture& t);
    void* stagingWrite(size_t size, VkBuffer& buf, VkDeviceSize& offset);
    void uploadImage(VTexture& t, int level, int x, int y, int w, int h, const void* data, size_t bytes, VkCommandBuffer cb);
    void generateMips(VTexture& t, VkCommandBuffer cb);
    void initImageLayout(VTexture& t, VkCommandBuffer cb, bool clear);
    // VkDraw.cpp
    VkRenderPass renderPassFor(VkFormat color, VkFormat depth, bool clearColor, bool clearDepth);
    VkFramebuffer framebufferFor(VkImageView color, VkImageView depth, VkRenderPass rp, int w, int h, uint32_t colorId, uint32_t depthId);
    VkPipeline pipelineFor(VPipeline& p, VkFormat color, VkFormat depth, bool hdr);
    VkShaderModule createModule(const std::vector<uint32_t>& code);
    bool prepareDraw();

public:   // helpers shared by the translation units
    VkCommandBuffer transferCmd();               // main command buffer outside passes, else the pre-frame one
    void barrierImage(VkCommandBuffer cb, VkImage image, bool depth, VkImageLayout from, VkImageLayout to, uint32_t levels = VK_REMAINING_MIP_LEVELS,
                      uint32_t baseLevel = 0);
    void deferDestroy(std::function<void()> fn) { garbage_.push_back({serial_, std::move(fn)}); }
    VkFormat vkFormat(PixelFormat f) const;

private:
    DeviceInfo info_;
    std::string error_;
    bool multiDrawIndirect_ = false;
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
    std::vector<VkImage> swapImages_;
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

    // resources
    Pool<VBuffer> buffers_;
    Pool<VTexture> textures_;
    Pool<VShader> shaders_;
    Pool<VPipeline> pipelines_;
    VTexture dummy_;          // bound to unset texture slots
    VTexture dummyDepth_;     // ... of compare samplers
    struct SamplerKey { uint32_t key; VkSampler sampler; };
    std::vector<SamplerKey> samplers_;
    struct RenderPassKey { uint64_t key; VkRenderPass pass; };
    std::vector<RenderPassKey> renderPasses_;
    struct FramebufferKey { uint32_t color, depth; VkRenderPass pass; int w, h; VkFramebuffer fb; };
    std::vector<FramebufferKey> framebuffers_;
    struct StagingChunk { VkBuffer buffer = VK_NULL_HANDLE; Allocation alloc; size_t size = 0, used = 0; uint64_t serial = 0; };
    std::vector<StagingChunk> staging_;

    // pass state
    bool inPass_ = false, passSkipped_ = false, hdrPass_ = false;
    int passW_ = 0, passH_ = 0;
    VkFormat passColor_ = VK_FORMAT_UNDEFINED, passDepth_ = VK_FORMAT_UNDEFINED;
    uint32_t pipelineId_ = 0;
    VkPipeline boundPipeline_ = VK_NULL_HANDLE;
    uint32_t vbId_ = 0, ibId_ = 0;
    size_t vbOffset_ = 0, ibOffset_ = 0;
    std::array<uint32_t, kMaxTextureSlots> textures_bound_{};
    std::array<uint32_t, kUniformSlots> uniformOffsets_{};
    bool descriptorsDirty_ = true;
    VkDescriptorSet currentSet_ = VK_NULL_HANDLE;
};

} // namespace aiverse::gfx::vk
