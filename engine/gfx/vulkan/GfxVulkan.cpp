// Vulkan backend: instance, device, swapchain and the frame loop. See Vulkan.h for the conventions.
#include "VkDevice.h"
#include "VkSurface.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace aiverse::gfx::vk {
namespace {

constexpr uint32_t kBackColorId = 0xFFFFFFF0u, kBackDepthId = 0xFFFFFFF1u;   // framebuffer cache keys of the back buffer

bool envOn(const char* name) { const char* v = std::getenv(name); return v && v[0] && v[0] != '0'; }

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT severity, VkDebugUtilsMessageTypeFlagsEXT,
                                             const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) std::fprintf(stderr, "[vulkan] %s\n", data->pMessage);
    return VK_FALSE;
}
VkDebugUtilsMessengerEXT g_messenger = VK_NULL_HANDLE;

bool hasExtension(const std::vector<VkExtensionProperties>& list, const char* name) {
    for (const auto& e : list) if (std::strcmp(e.extensionName, name) == 0) return true;
    return false;
}

} // namespace

bool VulkanDevice::createInstance(bool wantSurface, std::string& error) {
    uint32_t api = VK_API_VERSION_1_0;
    if (vkEnumerateInstanceVersion) vkEnumerateInstanceVersion(&api);
    uint32_t n = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &n, nullptr);
    std::vector<VkExtensionProperties> exts(n);
    vkEnumerateInstanceExtensionProperties(nullptr, &n, exts.data());
    std::vector<const char*> want;
    if (wantSurface) {
        const char* platformExt = surfaceExtension(window_.kind);
        if (!platformExt || !hasExtension(exts, VK_KHR_SURFACE_EXTENSION_NAME) || !hasExtension(exts, platformExt)) {
            error = "Vulkan: no surface extension for this window system"; return false;
        }
        want.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
        want.push_back(platformExt);
        surfaceInstance_ = true;
    }
    std::vector<const char*> layers;
    const bool validation = envOn("AIVERSE_VK_VALIDATION");
    if (validation) {
        vkEnumerateInstanceLayerProperties(&n, nullptr);
        std::vector<VkLayerProperties> ls(n);
        vkEnumerateInstanceLayerProperties(&n, ls.data());
        for (const auto& l : ls) if (std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0) layers.push_back("VK_LAYER_KHRONOS_validation");
        if (layers.empty()) std::fprintf(stderr, "[vulkan] AIVERSE_VK_VALIDATION: VK_LAYER_KHRONOS_validation is not installed\n");
        if (hasExtension(exts, VK_EXT_DEBUG_UTILS_EXTENSION_NAME)) want.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    auto app = vkStruct<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);
    app.pApplicationName = "AIVERSE";
    app.pEngineName = "AIVERSE";
    app.apiVersion = api >= VK_API_VERSION_1_1 ? VK_API_VERSION_1_1 : VK_API_VERSION_1_0;
    auto ci = vkStruct<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);
    ci.pApplicationInfo = &app;
    ci.enabledExtensionCount = uint32_t(want.size());
    ci.ppEnabledExtensionNames = want.data();
    ci.enabledLayerCount = uint32_t(layers.size());
    ci.ppEnabledLayerNames = layers.data();
    if (vkCreateInstance(&ci, nullptr, &instance_) != VK_SUCCESS) { error = "Vulkan: vkCreateInstance failed"; return false; }
    if (!loadInstanceFunctions(instance_)) { error = "Vulkan: instance functions missing"; return false; }
    if (validation && !layers.empty()) {
        auto create = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_, "vkCreateDebugUtilsMessengerEXT"));
        if (create) {
            auto mi = vkStruct<VkDebugUtilsMessengerCreateInfoEXT>(VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT);
            mi.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
            mi.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
            mi.pfnUserCallback = debugCallback;
            create(instance_, &mi, nullptr, &g_messenger);
        }
    }
    return true;
}

bool VulkanDevice::pickDevice(std::string& error) {
    uint32_t n = 0;
    vkEnumeratePhysicalDevices(instance_, &n, nullptr);
    std::vector<VkPhysicalDevice> gpus(n);
    vkEnumeratePhysicalDevices(instance_, &n, gpus.data());
    const char* forced = std::getenv("AIVERSE_VK_DEVICE");   // index or part of the name
    int best = -1, bestScore = -1;
    for (uint32_t i = 0; i < n; ++i) {
        VkPhysicalDeviceProperties p;
        vkGetPhysicalDeviceProperties(gpus[i], &p);
        uint32_t qn = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qn, nullptr);
        std::vector<VkQueueFamilyProperties> qs(qn);
        vkGetPhysicalDeviceQueueFamilyProperties(gpus[i], &qn, qs.data());
        int family = -1;
        for (uint32_t q = 0; q < qn && family < 0; ++q) {
            if (!(qs[q].queueFlags & VK_QUEUE_GRAPHICS_BIT)) continue;
            VkBool32 present = VK_TRUE;
            if (surface_) vkGetPhysicalDeviceSurfaceSupportKHR(gpus[i], q, surface_, &present);
            if (present) family = int(q);
        }
        if (family < 0) continue;
        if (surface_) {
            uint32_t en = 0;
            vkEnumerateDeviceExtensionProperties(gpus[i], nullptr, &en, nullptr);
            std::vector<VkExtensionProperties> ex(en);
            vkEnumerateDeviceExtensionProperties(gpus[i], nullptr, &en, ex.data());
            if (!hasExtension(ex, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) continue;
        }
        int score = p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 4 : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 3
                  : p.deviceType == VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU ? 2 : 1;
        if (forced && forced[0]) {
            const bool digits = std::strspn(forced, "0123456789") == std::strlen(forced);
            if (digits ? uint32_t(std::atoi(forced)) == i : std::strstr(p.deviceName, forced) != nullptr) score += 100;
        }
        if (score > bestScore) { bestScore = score; best = int(i); queueFamily_ = uint32_t(family); }
    }
    if (best < 0) { error = "Vulkan: no GPU with graphics and present support"; return false; }
    gpu_ = gpus[size_t(best)];
    vkGetPhysicalDeviceProperties(gpu_, &gpuProps_);

    VkPhysicalDeviceFeatures have{}, enable{};
    vkGetPhysicalDeviceFeatures(gpu_, &have);
    enable.textureCompressionBC = have.textureCompressionBC;
    // Nonzero firstInstance selects the per-draw transform in the shared
    // instance buffer. Without drawIndirectFirstInstance the indirect shader
    // would read only instance 0, so disable MDI on such devices.
    enable.multiDrawIndirect = have.multiDrawIndirect;
    enable.drawIndirectFirstInstance = have.drawIndirectFirstInstance;
    multiDrawIndirect_ = have.multiDrawIndirect && have.drawIndirectFirstInstance;
    enable.textureCompressionETC2 = have.textureCompressionETC2;
    enable.textureCompressionASTC_LDR = have.textureCompressionASTC_LDR;
    const float priority = 1.0f;
    auto qi = vkStruct<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);
    qi.queueFamilyIndex = queueFamily_;
    qi.queueCount = 1;
    qi.pQueuePriorities = &priority;
    const char* swapExt = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    auto di = vkStruct<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);
    di.queueCreateInfoCount = 1;
    di.pQueueCreateInfos = &qi;
    di.pEnabledFeatures = &enable;
    // offscreen devices have no surface extension and no swapchain; a windowed one keeps it for later windows (Android)
    uint32_t en = 0;
    vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &en, nullptr);
    std::vector<VkExtensionProperties> ex(en);
    vkEnumerateDeviceExtensionProperties(gpu_, nullptr, &en, ex.data());
    swapchainExt_ = surfaceInstance_ && hasExtension(ex, swapExt);
    if (swapchainExt_) { di.enabledExtensionCount = 1; di.ppEnabledExtensionNames = &swapExt; }
    if (vkCreateDevice(gpu_, &di, nullptr, &device_) != VK_SUCCESS) { error = "Vulkan: vkCreateDevice failed"; return false; }
    if (!loadDeviceFunctions(device_)) {
        if (vkDestroyDevice) vkDestroyDevice(device_, nullptr);
        device_ = VK_NULL_HANDLE;
        error = "Vulkan: device functions missing";
        return false;
    }
    vkGetDeviceQueue(device_, queueFamily_, 0, &queue_);

    const auto& lim = gpuProps_.limits;
    // Optional compute support on a queue that actually implements compute.
    // Do NOT advertise storage images when RGBA16F sampled+storage or a
    // 4-SSBO/80-byte push-constant ABI is unsupported.
    uint32_t familyCount=0;
    vkGetPhysicalDeviceQueueFamilyProperties(gpu_,&familyCount,nullptr);
    std::vector<VkQueueFamilyProperties> queueProps(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(gpu_,&familyCount,queueProps.data());
    VkFormatProperties giFormat{};
    vkGetPhysicalDeviceFormatProperties(gpu_,VK_FORMAT_R16G16B16A16_SFLOAT,&giFormat);
    const auto giFeatures=giFormat.optimalTilingFeatures;
    computeAvailable_=queueFamily_<familyCount &&
        (queueProps[queueFamily_].queueFlags&VK_QUEUE_COMPUTE_BIT) &&
        (giFeatures&(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                     VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))==
                    (VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
                     VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) &&
        lim.maxComputeWorkGroupInvocations>=64 &&
        lim.maxComputeWorkGroupSize[0]>=64 &&
        lim.maxPerStageDescriptorStorageBuffers>=4 &&
        lim.maxPerStageDescriptorStorageImages>=1 &&
        lim.maxDescriptorSetStorageBuffers>=4 &&
        lim.maxDescriptorSetStorageImages>=1 &&
        lim.maxPushConstantsSize>=80;
    textureSlots_ = int(std::min<uint32_t>({uint32_t(kMaxTextureSlots), lim.maxPerStageDescriptorSamplers, lim.maxPerStageDescriptorSampledImages,
                                            lim.maxDescriptorSetSamplers / 2u}));
    auto fail = [&](const char* why) { vkDestroyDevice(device_, nullptr); device_ = VK_NULL_HANDLE; error = why; return false; };
    if (lim.maxDescriptorSetUniformBuffersDynamic < uint32_t(kUniformSlots) || textureSlots_ < 8) return fail("Vulkan: descriptor limits too small");
    auto features = [&](VkFormat f) { VkFormatProperties fp; vkGetPhysicalDeviceFormatProperties(gpu_, f, &fp); return fp; };
    depthFormat_ = VK_FORMAT_UNDEFINED;
    for (VkFormat f : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_X8_D24_UNORM_PACK32, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
        const auto fp = features(f).optimalTilingFeatures;
        if ((fp & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) && (fp & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)) {
            const bool linear = fp & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
            if (depthFormat_ == VK_FORMAT_UNDEFINED || (linear && !depthLinear_)) { depthFormat_ = f; depthLinear_ = linear; }
            if (linear) break;
        }
    }
    if (depthFormat_ == VK_FORMAT_UNDEFINED) return fail("Vulkan: no sampled depth format");
    uscaled_ = features(VK_FORMAT_R8G8B8A8_USCALED).bufferFeatures & VK_FORMAT_FEATURE_VERTEX_BUFFER_BIT;
    if (!uscaled_) std::fprintf(stderr, "[gfx] Vulkan: R8G8B8A8_USCALED vertex attributes unsupported, UByte4 reads as UNORM * 255\n");
    return true;
}

bool VulkanDevice::init(const VulkanWindow& window, std::string& error) {
    window_ = window;
    if (!loadLoader()) { error = "Vulkan: loader not found"; return false; }
    const bool wantSurface = window.kind != VulkanWindow::Kind::None;
    if (!createInstance(wantSurface, error)) return false;
    if (wantSurface && createPlatformSurface(instance_, window, &surface_) != VK_SUCCESS) { error = "Vulkan: surface creation failed"; return false; }
    if (!pickDevice(error)) return false;
    memory_.init(gpu_, device_);

    info_.backend = Backend::Vulkan;
    info_.clipDepthZeroToOne = false;   // GL depth range, see translateGlsl
    info_.renderTargetFlipY = false;
    info_.name = std::string("Vulkan ") + gpuProps_.deviceName + " / " + std::to_string(VK_API_VERSION_MAJOR(gpuProps_.apiVersion)) + "." +
                 std::to_string(VK_API_VERSION_MINOR(gpuProps_.apiVersion)) + "." + std::to_string(VK_API_VERSION_PATCH(gpuProps_.apiVersion));

    auto pc = vkStruct<VkPipelineCacheCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO);
    vkCreatePipelineCache(device_, &pc, nullptr, &pipelineCache_);
    std::vector<VkDescriptorSetLayoutBinding> b;
    for (int i = 0; i < kUniformSlots; ++i)
        b.push_back({uint32_t(i), VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    for (int i = 0; i < textureSlots_; ++i)
        b.push_back({uint32_t(kTextureBinding0 + i), VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, nullptr});
    auto li = vkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    li.bindingCount = uint32_t(b.size());
    li.pBindings = b.data();
    vkCreateDescriptorSetLayout(device_, &li, nullptr, &setLayout_);
    auto pl = vkStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount = 1;
    pl.pSetLayouts = &setLayout_;
    vkCreatePipelineLayout(device_, &pl, nullptr, &pipelineLayout_);

    for (auto& s : slots_) {
        auto ci = vkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
        ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        ci.queueFamilyIndex = queueFamily_;
        vkCreateCommandPool(device_, &ci, nullptr, &s.pool);
        auto ai = vkStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
        ai.commandPool = s.pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = 2;
        VkCommandBuffer cbs[2];
        vkAllocateCommandBuffers(device_, &ai, cbs);
        s.pre = cbs[0]; s.main = cbs[1];
        auto fi = vkStruct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkCreateFence(device_, &fi, nullptr, &s.fence);
        auto si = vkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO);
        vkCreateSemaphore(device_, &si, nullptr, &s.acquired);
        s.uniformSize = 1 << 20;
        createBufferObject(s.uniformSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, s.uniforms, s.uniformAlloc);
    }
    prepareSlot();

    TextureDesc dd;
    dd.width = dd.height = 1;
    const uint8_t black[4] = {0, 0, 0, 255};
    dd.data = black;
    dd.filter = Filter::Nearest;
    {   // dummies live outside the pool: ids never reach the user
        const TextureHandle h = createTexture(dd);
        dummy_ = *textures_.get(h.id);
        textures_.remove(h.id);
    }
    TextureDesc dz;
    dz.width = dz.height = 1;
    dz.format = PixelFormat::Depth;
    dz.compare = true;
    dz.renderTarget = true;
    {
        const TextureHandle h = createTexture(dz);
        dummyDepth_ = *textures_.get(h.id);
        textures_.remove(h.id);
    }
    if (surface_) swapDirty_ = true;
    return true;
}

VulkanDevice::~VulkanDevice() {
    if (device_) destroyDeviceObjects();
    if (!instance_) return;
    if (surface_) vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (g_messenger) {
        auto destroy = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance_, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroy) destroy(instance_, g_messenger, nullptr);
        g_messenger = VK_NULL_HANDLE;
    }
    vkDestroyInstance(instance_, nullptr);
}

void VulkanDevice::destroyDeviceObjects() {
    vkDeviceWaitIdle(device_);
    destroySwapchain();
    completed_ = ~uint64_t(0);
    collectGarbage();
    for(const auto& [token,fence]:retirementFences_) {
        (void)token;
        if(fence)vkDestroyFence(device_,fence,nullptr);
    }
    retirementFences_.clear();
    buffers_.each([&](VBuffer& b) { for (auto& v : b.versions) { vkDestroyBuffer(device_, v.buffer, nullptr); memory_.release(v.alloc); } });
    auto killTex = [&](VTexture& t) { if (t.view) vkDestroyImageView(device_, t.view, nullptr); if (t.image) vkDestroyImage(device_, t.image, nullptr); memory_.release(t.alloc); };
    textures_.each(killTex);
    killTex(dummy_); killTex(dummyDepth_); killTex(back_); killTex(backDepth_);
    shaders_.each([&](VShader& s) { for (VkShaderModule m : {s.vert, s.frag, s.hdrVert, s.hdrFrag}) if (m) vkDestroyShaderModule(device_, m, nullptr); });
    pipelines_.each([&](VPipeline& p) { for (auto& v : p.variants) vkDestroyPipeline(device_, v.pipeline, nullptr); });
    computes_.each([&](VCompute& c) {
        if(c.pipeline)vkDestroyPipeline(device_,c.pipeline,nullptr);
        if(c.layout)vkDestroyPipelineLayout(device_,c.layout,nullptr);
        if(c.setLayout)vkDestroyDescriptorSetLayout(device_,c.setLayout,nullptr);
        if(c.shader)vkDestroyShaderModule(device_,c.shader,nullptr);
    });
    for (auto& s : samplers_) vkDestroySampler(device_, s.sampler, nullptr);
    for (auto& f : framebuffers_) vkDestroyFramebuffer(device_, f.fb, nullptr);
    for (auto& r : renderPasses_) vkDestroyRenderPass(device_, r.pass, nullptr);
    for (auto& c : staging_) { vkDestroyBuffer(device_, c.buffer, nullptr); memory_.release(c.alloc); }
    for (auto& s : slots_) {
        for (auto p : s.descriptorPools) vkDestroyDescriptorPool(device_, p, nullptr);
        for (auto p : s.computePools) vkDestroyDescriptorPool(device_,p,nullptr);
        vkDestroyBuffer(device_, s.uniforms, nullptr); memory_.release(s.uniformAlloc);
        vkDestroyFence(device_, s.fence, nullptr);
        vkDestroySemaphore(device_, s.acquired, nullptr);
        vkDestroyCommandPool(device_, s.pool, nullptr);
    }
    vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    vkDestroyDescriptorSetLayout(device_, setLayout_, nullptr);
    vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
    memory_.shutdown();
    vkDestroyDevice(device_, nullptr);
    device_ = VK_NULL_HANDLE;
}

bool VulkanDevice::setWindow(const VulkanWindow* w) {
    vkDeviceWaitIdle(device_);
    destroySwapchain();
    if (surface_) { vkDestroySurfaceKHR(instance_, surface_, nullptr); surface_ = VK_NULL_HANDLE; }
    if (!w || w->kind == VulkanWindow::Kind::None) return true;
    if (!swapchainExt_) return false;
    window_ = *w;
    if (createPlatformSurface(instance_, *w, &surface_) != VK_SUCCESS) { surface_ = VK_NULL_HANDLE; return false; }
    VkBool32 present = VK_FALSE;
    vkGetPhysicalDeviceSurfaceSupportKHR(gpu_, queueFamily_, surface_, &present);
    if (!present) { vkDestroySurfaceKHR(instance_, surface_, nullptr); surface_ = VK_NULL_HANDLE; return false; }
    swapDirty_ = true;
    return true;
}

bool VulkanDevice::createSwapchain() {
    VkSurfaceCapabilitiesKHR caps;
    if (vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu_, surface_, &caps) != VK_SUCCESS) return false;
    VkExtent2D ext = caps.currentExtent;
    if (ext.width == 0xFFFFFFFFu) ext = {uint32_t(std::max(1, fbW_)), uint32_t(std::max(1, fbH_))};
    ext.width = std::clamp(ext.width, caps.minImageExtent.width, caps.maxImageExtent.width);
    ext.height = std::clamp(ext.height, caps.minImageExtent.height, caps.maxImageExtent.height);
    if (ext.width == 0 || ext.height == 0) return false;   // minimized
    uint32_t n = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(n);
    vkGetPhysicalDeviceSurfaceFormatsKHR(gpu_, surface_, &n, formats.data());
    if (formats.empty()) return false;
    VkSurfaceFormatKHR fmt = formats[0];
    for (const auto& f : formats)   // UNORM: the back buffer already holds display values (as GL's default framebuffer)
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) && f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) { fmt = f; break; }
    if (fmt.format == VK_FORMAT_UNDEFINED) fmt = {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR};
    VkPresentModeKHR mode = VK_PRESENT_MODE_FIFO_KHR;
    if (!window_.vsync) {
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, nullptr);
        std::vector<VkPresentModeKHR> modes(n);
        vkGetPhysicalDeviceSurfacePresentModesKHR(gpu_, surface_, &n, modes.data());
        for (auto m : modes) if (m == VK_PRESENT_MODE_MAILBOX_KHR || (m == VK_PRESENT_MODE_IMMEDIATE_KHR && mode == VK_PRESENT_MODE_FIFO_KHR)) mode = m;
    }
    auto ci = vkStruct<VkSwapchainCreateInfoKHR>(VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR);
    ci.surface = surface_;
    ci.minImageCount = std::max(caps.minImageCount, 2u);
    if (caps.maxImageCount) ci.minImageCount = std::min(ci.minImageCount + 1, caps.maxImageCount);
    ci.imageFormat = fmt.format;
    ci.imageColorSpace = fmt.colorSpace;
    ci.imageExtent = ext;
    ci.imageArrayLayers = 1;
    ci.imageUsage = VK_IMAGE_USAGE_TRANSFER_DST_BIT;   // filled by a flipped blit from the back buffer
    ci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.preTransform = (caps.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR : caps.currentTransform;
    ci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (auto a : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR})
        if (caps.supportedCompositeAlpha & a) { ci.compositeAlpha = a; break; }
    ci.presentMode = mode;
    ci.clipped = VK_TRUE;
    ci.oldSwapchain = swapchain_;
    VkSwapchainKHR next = VK_NULL_HANDLE;
    const VkResult r = vkCreateSwapchainKHR(device_, &ci, nullptr, &next);
    destroySwapchain();
    if (r != VK_SUCCESS) return false;
    swapchain_ = next;
    swapFormat_ = fmt.format;
    swapExtent_ = ext;
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, nullptr);
    swapImages_.resize(n);
    vkGetSwapchainImagesKHR(device_, swapchain_, &n, swapImages_.data());
    swapRendered_.resize(n);
    for (auto& s : swapRendered_) { auto si = vkStruct<VkSemaphoreCreateInfo>(VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO); vkCreateSemaphore(device_, &si, nullptr, &s); }
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(gpu_, swapFormat_, &fp);
    if (!(fp.optimalTilingFeatures & VK_FORMAT_FEATURE_BLIT_DST_BIT)) std::fprintf(stderr, "[gfx] Vulkan: swapchain format can't be a blit target\n");
    swapDirty_ = false;
    return true;
}

void VulkanDevice::destroySwapchain() {
    if (!swapchain_) return;
    vkDeviceWaitIdle(device_);
    for (auto s : swapRendered_) vkDestroySemaphore(device_, s, nullptr);
    swapRendered_.clear();
    swapImages_.clear();
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
}

void VulkanDevice::ensureBackBuffer(int w, int h) {
    if (back_.image && back_.width == w && back_.height == h) return;
    if (back_.image) {
        VTexture old = back_, oldDepth = backDepth_;
        deferDestroy([this, old, oldDepth]() mutable {
            for (VTexture* t : {&old, &oldDepth}) { vkDestroyImageView(device_, t->view, nullptr); vkDestroyImage(device_, t->image, nullptr); memory_.release(t->alloc); }
        });
        for (size_t i = 0; i < framebuffers_.size();) {
            if (framebuffers_[i].color == kBackColorId || framebuffers_[i].depth == kBackDepthId) {
                VkFramebuffer fb = framebuffers_[i].fb;
                deferDestroy([this, fb] { vkDestroyFramebuffer(device_, fb, nullptr); });
                framebuffers_.erase(framebuffers_.begin() + long(i));
            } else ++i;
        }
    }
    back_ = {};
    back_.format = PixelFormat::RGBA8; back_.vkFormat = VK_FORMAT_R8G8B8A8_UNORM; back_.width = w; back_.height = h; back_.renderTarget = true;
    createImage(back_, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);
    backDepth_ = {};
    backDepth_.format = PixelFormat::Depth; backDepth_.vkFormat = depthFormat_; backDepth_.width = w; backDepth_.height = h;
    backDepth_.depth = true; backDepth_.renderTarget = true;
    createImage(backDepth_, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    VkCommandBuffer cb = transferCmd();
    initImageLayout(back_, cb, true);
    initImageLayout(backDepth_, cb, true);
}

bool VulkanDevice::waitForResourceIdle() {
    // Frame serial counts submissions, NOT GPU completion. Only a
    // successful device-wide wait establishes the retirement fence.
    // Caller must be outside passes and before any current-frame packet
    // can borrow a model scheduled for destruction.
    if(!device_ || inPass_ || !healthy_)return false;
    const VkResult result=vkDeviceWaitIdle(device_);
    if(result!=VK_SUCCESS) {
        if(result==VK_ERROR_DEVICE_LOST)healthy_=false;
        return false;
    }
    completed_=std::max(completed_,serial_-1);
    collectGarbage();
    return true;
}

uint64_t VulkanDevice::beginResourceRetirement() {
    // Enqueue an empty ordered queue submission. The fence completes only
    // after earlier submitted graphics/transfer work on this same queue.
    // Recorded-but-unsubmitted current-frame commands are NOT included.
    if(!device_ || !healthy_ || inPass_ || !queue_)return 0;
    VkFence fence=VK_NULL_HANDLE;
    const auto info=vkStruct<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);
    if(vkCreateFence(device_,&info,nullptr,&fence)!=VK_SUCCESS)return 0;
    const VkResult result=vkQueueSubmit(queue_,0,nullptr,fence);
    if(result!=VK_SUCCESS) {
        if(result==VK_ERROR_DEVICE_LOST)healthy_=false;
        vkDestroyFence(device_,fence,nullptr);
        return 0;
    }
    const uint64_t token=nextRetirementToken_++;
    retirementFences_.emplace(token,fence);
    return token;
}

bool VulkanDevice::resourceRetirementComplete(uint64_t token) {
    return resourceRetirementStatus(token)==ResourceRetirementStatus::Complete;
}

ResourceRetirementStatus VulkanDevice::resourceRetirementStatus(uint64_t token) {
    const auto it=retirementFences_.find(token);
    if(it==retirementFences_.end() || !device_ || !healthy_)
        return ResourceRetirementStatus::Failed;
    const VkResult result=vkGetFenceStatus(device_,it->second);
    if(result==VK_SUCCESS)return ResourceRetirementStatus::Complete;
    if(result==VK_NOT_READY)return ResourceRetirementStatus::Pending;
    if(result==VK_ERROR_DEVICE_LOST)healthy_=false;
    return ResourceRetirementStatus::Failed;
}

void VulkanDevice::endResourceRetirement(uint64_t token) {
    const auto it=retirementFences_.find(token);
    if(it==retirementFences_.end() || !device_)return;
    if(vkGetFenceStatus(device_,it->second)!=VK_SUCCESS)return;
    vkDestroyFence(device_,it->second,nullptr);
    retirementFences_.erase(it);
}

void VulkanDevice::abandonRetirementTokenAfterIdle(uint64_t token) {
    // This API requires an earlier successful vkDeviceWaitIdle: it is
    // forbidden for failed/vkDeviceLost paths with incomplete submissions.
    const auto it=retirementFences_.find(token);
    if(it==retirementFences_.end() || !device_)return;
    vkDestroyFence(device_,it->second,nullptr);
    retirementFences_.erase(it);
}

void VulkanDevice::prepareSlot() {
    FrameSlot& s = slots_[size_t(slot_)];
    const VkResult waited = vkWaitForFences(device_, 1, &s.fence, VK_TRUE, UINT64_MAX);
    if (waited == VK_SUCCESS)
        completed_ = std::max(completed_, s.serial);
    else
        healthy_ = false; // no verified completion on error/device lost
    if (healthy_) collectGarbage();
    vkResetCommandPool(device_, s.pool, 0);
    for (auto p : s.descriptorPools) vkResetDescriptorPool(device_, p, 0);
    for (auto p : s.computePools) vkResetDescriptorPool(device_,p,0);
    s.computePool=0;
    s.descriptorPool = 0;
    s.uniformOffset = 0;
    s.preRecording = false;
    for (auto& c : staging_) if (c.serial <= completed_) c.used = 0;
}

void VulkanDevice::collectGarbage() {
    size_t keep = 0;
    for (size_t i = 0; i < garbage_.size(); ++i) {
        if (garbage_[i].serial <= completed_) garbage_[i].fn();
        else garbage_[keep++] = std::move(garbage_[i]);
    }
    garbage_.resize(keep);
}

VkCommandBuffer VulkanDevice::transferCmd() {
    FrameSlot& s = slots_[size_t(slot_)];
    if (recording_ && !inPass_) return s.main;
    if (!s.preRecording) {
        auto bi = vkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(s.pre, &bi);
        s.preRecording = true;
    }
    return s.pre;
}

bool VulkanDevice::waitForSubmittedGpuWork() {
    if (!device_ || !queue_ || !healthy_) return false;
    // Only call at the PRE-RENDER boundary. Current frame's recording
    // command buffer may be open, but no new draw referencing old resources
    // has been encoded. vkQueueWaitIdle covers already SUBMITTED work only.
    if (vkQueueWaitIdle(queue_) != VK_SUCCESS) {
        healthy_ = false;
        return false;
    }
    if (serial_ > 0) completed_ = std::max(completed_,serial_-1);
    collectGarbage(); // uses real GPU-idle confirmation, not CPU frame age
    return true;
}

void VulkanDevice::beginFrame(int w, int h) {
    fbW_ = w; fbH_ = h;
    FrameSlot& s = slots_[size_t(slot_)];
    auto bi = vkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(s.main, &bi);
    recording_ = true;
    uniformOffsets_.fill(0);
    descriptorsDirty_ = true;
    ensureBackBuffer(std::max(1, w), std::max(1, h));
    swapAcquired_ = false;
    if (surface_ && w > 0 && h > 0) {
        if (w != swapForW_ || h != swapForH_) swapDirty_ = true;   // resized: the driver may not report it (X11)
        if (swapDirty_ || !swapchain_) { swapForW_ = w; swapForH_ = h; createSwapchain(); }
        for (int attempt = 0; swapchain_ && attempt < 2; ++attempt) {
            const VkResult r = vkAcquireNextImageKHR(device_, swapchain_, UINT64_MAX, s.acquired, VK_NULL_HANDLE, &swapIndex_);
            if (r == VK_SUCCESS || r == VK_SUBOPTIMAL_KHR) { swapAcquired_ = true; if (r == VK_SUBOPTIMAL_KHR) swapDirty_ = true; break; }
            if (r != VK_ERROR_OUT_OF_DATE_KHR || !createSwapchain()) break;
        }
    }
}

void VulkanDevice::present() {
    VkCommandBuffer cb = slots_[size_t(slot_)].main;
    VkImage dst = swapImages_[swapIndex_];
    barrierImage(cb, back_.image, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barrierImage(cb, dst, false, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit blit{};
    blit.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    blit.dstSubresource = blit.srcSubresource;
    blit.srcOffsets[1] = {back_.width, back_.height, 1};
    // the back buffer is bottom-up (GL): its last row is the top of the screen
    blit.dstOffsets[0] = {0, int32_t(swapExtent_.height), 0};
    blit.dstOffsets[1] = {int32_t(swapExtent_.width), 0, 1};
    const bool same = uint32_t(back_.width) == swapExtent_.width && uint32_t(back_.height) == swapExtent_.height;
    vkCmdBlitImage(cb, back_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit,
                   same ? VK_FILTER_NEAREST : VK_FILTER_LINEAR);
    barrierImage(cb, dst, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    barrierImage(cb, back_.image, false, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanDevice::endFrame() {
    if (!recording_) return;
    if (inPass_) endPass();
    if(activeCompute_)endCompute(); // restore storage image sampled layout
    FrameSlot& s = slots_[size_t(slot_)];
    if (swapAcquired_) present();
    VkBuffer readback = VK_NULL_HANDLE;
    Allocation readAlloc;
    const int cw = back_.width, ch = back_.height;
    if (capture_) {
        createBufferObject(size_t(cw) * size_t(ch) * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true, readback, readAlloc);
        barrierImage(s.main, back_.image, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
        VkBufferImageCopy c{};
        c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        c.imageExtent = {uint32_t(cw), uint32_t(ch), 1};
        vkCmdCopyImageToBuffer(s.main, back_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &c);
        barrierImage(s.main, back_.image, false, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    VkCommandBuffer cbs[2];
    uint32_t count = 0;
    if (s.preRecording) { vkEndCommandBuffer(s.pre); cbs[count++] = s.pre; s.preRecording = false; }
    vkEndCommandBuffer(s.main);
    cbs[count++] = s.main;
    recording_ = false;
    auto si = vkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (swapAcquired_) {
        si.waitSemaphoreCount = 1; si.pWaitSemaphores = &s.acquired; si.pWaitDstStageMask = &waitStage;
        si.signalSemaphoreCount = 1; si.pSignalSemaphores = &swapRendered_[swapIndex_];
    }
    si.commandBufferCount = count;
    si.pCommandBuffers = cbs;
    vkResetFences(device_, 1, &s.fence);
    if (vkQueueSubmit(queue_, 1, &si, s.fence) == VK_ERROR_DEVICE_LOST) healthy_ = false;
    s.serial = serial_;
    if (swapAcquired_) {
        auto pi = vkStruct<VkPresentInfoKHR>(VK_STRUCTURE_TYPE_PRESENT_INFO_KHR);
        pi.waitSemaphoreCount = 1; pi.pWaitSemaphores = &swapRendered_[swapIndex_];
        pi.swapchainCount = 1; pi.pSwapchains = &swapchain_; pi.pImageIndices = &swapIndex_;
        const VkResult r = vkQueuePresentKHR(queue_, &pi);
        if (r == VK_ERROR_OUT_OF_DATE_KHR || r == VK_SUBOPTIMAL_KHR) swapDirty_ = true;
        swapAcquired_ = false;
    }
    if (capture_) {
        vkWaitForFences(device_, 1, &s.fence, VK_TRUE, UINT64_MAX);
        std::vector<uint8_t> px(size_t(cw) * size_t(ch) * 4);
        const size_t row = size_t(cw) * 4;
        const auto* src = static_cast<const uint8_t*>(readAlloc.mapped);
        for (int y = 0; y < ch; ++y) std::memcpy(px.data() + size_t(y) * row, src + size_t(ch - 1 - y) * row, row);   // top-down
        vkDestroyBuffer(device_, readback, nullptr);
        memory_.release(readAlloc);
        auto fn = std::move(capture_);
        capture_ = nullptr;
        fn(cw, ch, std::move(px));
    }
    ++serial_;
    slot_ = (slot_ + 1) % kFramesInFlight;
    prepareSlot();
}

} // namespace aiverse::gfx::vk

namespace aiverse::gfx {

std::unique_ptr<Device> createVulkanDevice(const VulkanWindow& window, std::string* error) {
    auto d = std::make_unique<vk::VulkanDevice>();
    std::string err;
    if (!d->init(window, err)) { if (error) *error = err; return nullptr; }
    return d;
}

bool vulkanSetWindow(Device& device, const VulkanWindow* window) {
    if (device.info().backend != Backend::Vulkan) return false;
    return static_cast<vk::VulkanDevice&>(device).setWindow(window);
}

} // namespace aiverse::gfx
