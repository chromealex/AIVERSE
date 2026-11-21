// Vulkan backend: shaders, pipelines, render passes and draws.
#include "VkDevice.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace aiverse::gfx::vk {

// ---------- shaders ----------
VkShaderModule VulkanDevice::createModule(const std::vector<uint32_t>& code) {
    auto ci = vkStruct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    ci.codeSize = code.size() * sizeof(uint32_t);
    ci.pCode = code.data();
    VkShaderModule m = VK_NULL_HANDLE;
    if (vkCreateShaderModule(device_, &ci, nullptr, &m) != VK_SUCCESS) return VK_NULL_HANDLE;
    return m;
}

ShaderHandle VulkanDevice::createShader(const ShaderDesc& d) {
    if (!d.glslVertex || !d.glslFragment) { error_ = "shader has no GLSL source"; return {}; }
    SpirvProgram prog;
    if (!compileProgram(d.glslVertex, d.glslFragment, d.linearTarget, prog)) { error_ = prog.error; return {}; }
    VShader s;
    s.vs = d.glslVertex; s.fs = d.glslFragment; s.linear = d.linearTarget; s.shadowSlots = prog.shadowSlots;
    s.vert = createModule(prog.vertex);
    s.frag = createModule(prog.fragment);
    if (!s.vert || !s.frag) {
        if (s.vert) vkDestroyShaderModule(device_, s.vert, nullptr);
        if (s.frag) vkDestroyShaderModule(device_, s.frag, nullptr);
        error_ = "vkCreateShaderModule failed";
        return {};
    }
    return {shaders_.add(std::move(s))};
}

void VulkanDevice::destroyShader(ShaderHandle h) {
    VShader* s = shaders_.get(h.id);
    if (!s) return;
    VkShaderModule ms[4] = {s->vert, s->frag, s->hdrVert, s->hdrFrag};
    deferDestroy([this, ms] { for (VkShaderModule m : ms) if (m) vkDestroyShaderModule(device_, m, nullptr); });
    shaders_.remove(h.id);
}

PipelineHandle VulkanDevice::createPipeline(const PipelineDesc& d) {
    if (d.colorAttachmentCount < 1 ||
        d.colorAttachmentCount > kMaxRenderTargetColors) {
        error_ = "Vulkan: color output count must be 1..4";
        return {};
    }
    const VkSampleCountFlags samples = VkSampleCountFlags(d.sampleCount);
    const VkSampleCountFlags limits = d.depthOnly ?
        gpuProps_.limits.framebufferDepthSampleCounts :
        gpuProps_.limits.framebufferColorSampleCounts;
    if ((d.sampleCount != 1 && d.sampleCount != 2 &&
         d.sampleCount != 4 && d.sampleCount != 8 &&
         d.sampleCount != 16) || !(limits & samples)) {
        error_ = "Vulkan: unsupported multisample pipeline count";
        return {};
    }
    if (!shaders_.get(d.shader.id)) { error_ = "pipeline: invalid shader"; return {}; }
    VPipeline p;
    p.desc = d;
    return {pipelines_.add(std::move(p))};
}

void VulkanDevice::destroyPipeline(PipelineHandle h) {
    VPipeline* p = pipelines_.get(h.id);
    if (!p) return;
    std::vector<VPipeline::Variant> vs = std::move(p->variants);
    deferDestroy([this, vs] { for (const auto& v : vs) vkDestroyPipeline(device_, v.pipeline, nullptr); });
    pipelines_.remove(h.id);
    if (pipelineId_ == h.id) { pipelineId_ = 0; boundPipeline_ = VK_NULL_HANDLE; }
}

// ---------- render passes ----------
VkRenderPass VulkanDevice::renderPassFor(VkFormat color, VkFormat depth, bool clearColor, bool clearDepth) {
    const uint64_t key = uint64_t(uint32_t(color)) | (uint64_t(uint32_t(depth) & 0x3FFFFFFFu) << 32) | (uint64_t(clearColor) << 62) | (uint64_t(clearDepth) << 63);
    for (const auto& r : renderPasses_) if (r.key == key) return r.pass;
    VkAttachmentDescription att[2]{};
    VkAttachmentReference colorRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}, depthRef{0, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    uint32_t n = 0;
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    if (color != VK_FORMAT_UNDEFINED) {
        auto& a = att[n];
        a.format = color;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = clearColor ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorRef.attachment = n++;
        sub.colorAttachmentCount = 1;
        sub.pColorAttachments = &colorRef;
    }
    if (depth != VK_FORMAT_UNDEFINED) {
        auto& a = att[n];
        a.format = depth;
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        depthRef.attachment = n++;
        sub.pDepthStencilAttachment = &depthRef;
    }
    VkSubpassDependency deps[2]{};
    for (int i = 0; i < 2; ++i) {   // conservative: everything before / after the pass
        deps[i].srcSubpass = i == 0 ? VK_SUBPASS_EXTERNAL : 0;
        deps[i].dstSubpass = i == 0 ? 0 : VK_SUBPASS_EXTERNAL;
        deps[i].srcStageMask = i == 0 ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT : VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        deps[i].dstStageMask = i == 0 ? VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT : VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        deps[i].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        deps[i].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }
    auto ci = vkStruct<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    ci.attachmentCount = n;
    ci.pAttachments = att;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 2;
    ci.pDependencies = deps;
    VkRenderPass rp = VK_NULL_HANDLE;
    vkCreateRenderPass(device_, &ci, nullptr, &rp);
    renderPasses_.push_back({key, rp});
    return rp;
}

VkRenderPass VulkanDevice::renderPassForMrt(
        const std::array<VkFormat, kMaxRenderTargetColors>& formats,
        VkFormat depth, int colors, uint32_t clearMask, bool clearDepth,
        int samples, uint32_t resolveMask) {
    if (colors < 0 || colors > kMaxRenderTargetColors ||
        (colors < 2 && samples == 1) ||
        (colors == 0 && depth == VK_FORMAT_UNDEFINED) ||
        (samples != 1 && samples != 2 && samples != 4 &&
         samples != 8 && samples != 16) ||
        (samples == 1 && resolveMask) ||
        (resolveMask & ~((1u << colors) - 1u)))
        return VK_NULL_HANDLE;
    for (int i = 0; i < colors; ++i)
        if (formats[size_t(i)] == VK_FORMAT_UNDEFINED) return VK_NULL_HANDLE;
    for (const auto& rp : mrtRenderPasses_)
        if (rp.formats == formats && rp.depth == depth &&
            rp.colorCount == colors && rp.clearMask == clearMask &&
            rp.clearDepth == clearDepth && rp.samples == samples &&
            rp.resolveMask == resolveMask)
            return rp.pass;

    // Physical attachment order is color[0..N), actual resolves in slot order,
    // then depth. VK_ATTACHMENT_UNUSED marks colors without a resolve.
    VkAttachmentDescription attachments[2 * kMaxRenderTargetColors + 1]{};
    VkAttachmentReference colorRefs[kMaxRenderTargetColors]{};
    VkAttachmentReference resolveRefs[kMaxRenderTargetColors]{};
    for (int i = 0; i < colors; ++i) {
        auto& a = attachments[i];
        a.format = formats[size_t(i)];
        a.samples = VkSampleCountFlagBits(samples);
        a.loadOp = (clearMask & (1u << i)) ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = samples > 1 ? VK_IMAGE_LAYOUT_UNDEFINED :
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        a.finalLayout = samples > 1 ? VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL :
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        colorRefs[i] = {uint32_t(i), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        resolveRefs[i] = {VK_ATTACHMENT_UNUSED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }
    int count = colors;
    for (int i = 0; i < colors; ++i) {
        if (!(resolveMask & (1u << i))) continue;
        auto& a = attachments[count];
        a.format = formats[size_t(i)];
        a.samples = VK_SAMPLE_COUNT_1_BIT;
        a.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        a.initialLayout = a.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        resolveRefs[i] = {uint32_t(count++), VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    }
    VkAttachmentReference depthRef{uint32_t(count), VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    if (depth != VK_FORMAT_UNDEFINED) {
        auto& a = attachments[count++];
        a.format = depth;
        a.samples = VkSampleCountFlagBits(samples);
        a.loadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.stencilLoadOp = clearDepth ? VK_ATTACHMENT_LOAD_OP_CLEAR : VK_ATTACHMENT_LOAD_OP_LOAD;
        a.stencilStoreOp = VK_ATTACHMENT_STORE_OP_STORE;
        a.initialLayout = samples > 1 ? VK_IMAGE_LAYOUT_UNDEFINED :
                                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        a.finalLayout = samples > 1 ? VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL :
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    }
    VkSubpassDescription sub{};
    sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sub.colorAttachmentCount = uint32_t(colors);
    sub.pColorAttachments = colors ? colorRefs : nullptr;
    sub.pResolveAttachments = samples > 1 && colors ? resolveRefs : nullptr;
    if (depth != VK_FORMAT_UNDEFINED) sub.pDepthStencilAttachment = &depthRef;
    VkSubpassDependency deps[2]{};
    for (int i = 0; i < 2; ++i) {
        deps[i].srcSubpass = i == 0 ? VK_SUBPASS_EXTERNAL : 0;
        deps[i].dstSubpass = i == 0 ? 0 : VK_SUBPASS_EXTERNAL;
        deps[i].srcStageMask = i == 0 ? VK_PIPELINE_STAGE_ALL_COMMANDS_BIT :
                                        VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT;
        deps[i].dstStageMask = i == 0 ? VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT :
                                        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
        deps[i].srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
        deps[i].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    }
    auto ci = vkStruct<VkRenderPassCreateInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO);
    ci.attachmentCount = uint32_t(count);
    ci.pAttachments = attachments;
    ci.subpassCount = 1;
    ci.pSubpasses = &sub;
    ci.dependencyCount = 2;
    ci.pDependencies = deps;
    VkRenderPass result = VK_NULL_HANDLE;
    if (vkCreateRenderPass(device_, &ci, nullptr, &result) != VK_SUCCESS) {
        error_ = "Vulkan: vkCreateRenderPass MRT/MSAA failed";
        return VK_NULL_HANDLE;
    }
    mrtRenderPasses_.push_back({formats, depth, colors, clearMask, clearDepth,
                               samples, resolveMask, result});
    return result;
}

VkFramebuffer VulkanDevice::framebufferFor(VkImageView color, VkImageView depth, VkRenderPass rp, int w, int h, uint32_t colorId, uint32_t depthId) {
    for (const auto& f : framebuffers_)
        if (f.color == colorId && f.depth == depthId && f.pass == rp && f.w == w && f.h == h) return f.fb;
    VkImageView views[2];
    uint32_t n = 0;
    if (color) views[n++] = color;
    if (depth) views[n++] = depth;
    auto ci = vkStruct<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    ci.renderPass = rp;
    ci.attachmentCount = n;
    ci.pAttachments = views;
    ci.width = uint32_t(w); ci.height = uint32_t(h); ci.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    vkCreateFramebuffer(device_, &ci, nullptr, &fb);
    framebuffers_.push_back({colorId, depthId, rp, w, h, fb});
    return fb;
}

void VulkanDevice::beginPass(const PassDesc& d) {
    if (!recording_ || inPass_) return;
    explicitMrtPass_ = false;
    VTexture* color = nullptr;
    VTexture* depth = nullptr;
    uint32_t colorId = 0, depthId = 0;
    if (d.colorTarget.valid() || d.depthTarget.valid()) {
        color = textures_.get(d.colorTarget.id);
        depth = textures_.get(d.depthTarget.id);
        colorId = color ? d.colorTarget.id : 0;
        depthId = depth ? d.depthTarget.id : 0;
        if (color && depth && (color->width != depth->width || color->height != depth->height)) depth = nullptr, depthId = 0;
    } else {
        color = &back_; depth = &backDepth_;
        colorId = 0xFFFFFFF0u; depthId = 0xFFFFFFF1u;
    }
    passSkipped_ = !color && !depth;
    if ((color && color->samples > 1) || (depth && depth->samples > 1)) {
        error_ = "Vulkan: use beginRenderTargets for multisample attachments";
        passSkipped_ = true;
    }
    passSamples_ = 1;
    passResolveMask_ = 0;
    passColors_ = color ? 1 : 0;
    inPass_ = true;
    pipelineId_ = 0;
    boundPipeline_ = VK_NULL_HANDLE;
    descriptorsDirty_ = true;
    if (passSkipped_) return;
    const VTexture& ref = color ? *color : *depth;
    passW_ = ref.width; passH_ = ref.height;
    passColor_ = color ? color->vkFormat : VK_FORMAT_UNDEFINED;
    passFormats_.fill(VK_FORMAT_UNDEFINED);
    passFormats_[0] = passColor_;
    passDepth_ = depth ? depth->vkFormat : VK_FORMAT_UNDEFINED;
    hdrPass_ = color && color->format == PixelFormat::RGBA16F;
    VkRenderPass rp = renderPassFor(passColor_, passDepth_, d.clearColorEnabled, d.clearDepthEnabled);
    VkFramebuffer fb = framebufferFor(color ? color->view : VK_NULL_HANDLE, depth ? depth->view : VK_NULL_HANDLE, rp, passW_, passH_, colorId, depthId);
    VkClearValue clears[2];
    uint32_t n = 0;
    if (color) { std::memcpy(clears[n].color.float32, d.clearColor, sizeof d.clearColor); ++n; }
    if (depth) { clears[n].depthStencil = {d.clearDepth, 0}; ++n; }
    auto bi = vkStruct<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    bi.renderPass = rp;
    bi.framebuffer = fb;
    bi.renderArea = {{0, 0}, {uint32_t(passW_), uint32_t(passH_)}};
    bi.clearValueCount = n;
    bi.pClearValues = clears;
    VkCommandBuffer cb = slots_[size_t(slot_)].main;
    vkCmdBeginRenderPass(cb, &bi, VK_SUBPASS_CONTENTS_INLINE);
    setViewport({0, 0, passW_, passH_});
    setScissor({});
}

bool VulkanDevice::beginRenderTargets(const RenderTargetPassDesc& desc) {
    if (!validateRenderTargetPassDesc(desc) || !recording_ || inPass_) {
        error_ = "Vulkan: invalid or nested render-target pass";
        return false;
    }
    auto check = [&](const RenderTargetAttachment& a, bool wantDepth, int& w, int& h) {
        const VTexture* t = textures_.get(a.texture.id);
        if (!t || !t->image || (!t->renderTarget && !t->depth) ||
            a.mipLevel < 0 || a.arrayLayer < 0 ||
            a.mipLevel >= t->levels || a.arrayLayer >= t->layers ||
            (t->samples > 1 && (a.mipLevel != 0 || a.arrayLayer != 0 || !a.clear)) ||
            t->depth != wantDepth ||
            t->dimension == TextureDimension::Texture1D ||
            t->dimension == TextureDimension::Texture3D) return false;
        if (!wantDepth && t->format != PixelFormat::RGBA8 &&
            t->format != PixelFormat::RGBA8_sRGB &&
            t->format != PixelFormat::RGBA16F) return false;
        w = std::max(1, t->width >> a.mipLevel);
        h = std::max(1, t->height >> a.mipLevel);
        return true;
    };
    VTexture* colors[kMaxRenderTargetColors]{};
    VTexture* depth = nullptr;
    int width = 0, height = 0;
    std::array<VkFormat, kMaxRenderTargetColors> activeFormats{};
    activeFormats.fill(VK_FORMAT_UNDEFINED);
    bool allHdr = desc.colorCount > 0;
    uint32_t clearMask = 0, resolveMask = 0;
    int samples = 0;
    std::array<VTexture*, kMaxRenderTargetColors> resolveTargets{};
    for (int i = 0; i < desc.colorCount; ++i) {
        const auto& a = desc.colors[size_t(i)];
        int w = 0, h = 0;
        if (!check(a, false, w, h) ||
            (width && (width != w || height != h))) {
            error_ = "Vulkan: MRT color attachment mip/extent/format invalid";
            return false;
        }
        VTexture* c = textures_.get(a.texture.id);
        if (samples && samples != c->samples) {
            error_ = "Vulkan: MRT color attachments have different sample counts";
            return false;
        }
        samples = c->samples;
        if (a.resolveTexture.valid()) {
            VTexture* r = textures_.get(a.resolveTexture.id);
            const bool validResolve = c->samples > 1 && r && r->image &&
                r->samples == 1 && r->renderTarget && !r->depth &&
                !r->compressed && r->vkFormat == c->vkFormat &&
                r->dimension == TextureDimension::Texture2D &&
                a.resolveArrayLayer == 0 &&
                a.resolveMipLevel >= 0 && a.resolveMipLevel < r->levels &&
                std::max(1, r->width >> a.resolveMipLevel) == w &&
                std::max(1, r->height >> a.resolveMipLevel) == h;
            if (!validResolve) {
                error_ = "Vulkan: MSAA resolve destination format, extent or samples invalid";
                return false;
            }
            for (int prev = 0; prev < i; ++prev)
                if (desc.colors[size_t(prev)].resolveTexture == a.resolveTexture) {
                    error_ = "Vulkan: overlapping MRT resolve destinations";
                    return false;
                }
            resolveTargets[size_t(i)] = r;
            resolveMask |= 1u << i;
        }
        colors[i] = c;
        width = w; height = h;
        activeFormats[size_t(i)] = c->vkFormat;
        allHdr &= c->format == PixelFormat::RGBA16F;
        if (a.clear) clearMask |= 1u << i;
    }
    if (desc.depth.texture.valid()) {
        int w = 0, h = 0;
        if (!check(desc.depth, true, w, h) ||
            (width && (width != w || height != h))) {
            error_ = "Vulkan: MRT depth attachment mismatch";
            return false;
        }
        if (!width) { width = w; height = h; }
        depth = textures_.get(desc.depth.texture.id);
        if (samples && samples != depth->samples) {
            error_ = "Vulkan: depth and color attachment sample counts differ";
            return false;
        }
        samples = depth->samples;
    }
    if (width < 1 || height < 1) return false;
    if (!samples) samples = 1;
    std::array<VkImageView, 2*kMaxRenderTargetColors + 1> imageViews{};
    auto makeView = [&](const VTexture* t, const RenderTargetAttachment& a,
                        VkImageView& out) {
        if (!t) return true;
        auto ci = vkStruct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
        ci.image = t->image;
        ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ci.format = t->vkFormat;
        ci.subresourceRange = {
            VkImageAspectFlags(t->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT),
            uint32_t(a.mipLevel), 1, uint32_t(a.arrayLayer), 1};
        return vkCreateImageView(device_, &ci, nullptr, &out) == VK_SUCCESS;
    };
    auto cleanupViews = [&] {
        for (auto& imageView : imageViews)
            if (imageView) vkDestroyImageView(device_, imageView, nullptr);
    };
    for (int i = 0; i < desc.colorCount; ++i)
        if (!makeView(colors[i], desc.colors[size_t(i)], imageViews[i])) {
            cleanupViews();
            error_ = "Vulkan: MRT color image view creation failed";
            return false;
        }
    int nextAttachment = desc.colorCount;
    std::array<VkImageView, kMaxRenderTargetColors> resolveViews{};
    for (int i = 0; i < desc.colorCount; ++i) {
        if (!resolveTargets[size_t(i)]) continue;
        const auto& a = desc.colors[size_t(i)];
        RenderTargetAttachment ra;
        ra.texture = a.resolveTexture;
        ra.mipLevel = a.resolveMipLevel;
        ra.arrayLayer = a.resolveArrayLayer;
        if (!makeView(resolveTargets[size_t(i)], ra, imageViews[size_t(nextAttachment)])) {
            cleanupViews();
            error_ = "Vulkan: MRT resolve image view creation failed";
            return false;
        }
        resolveViews[size_t(i)] = imageViews[size_t(nextAttachment++)];
    }
    const int depthSlot = nextAttachment;
    if (depth && !makeView(depth, desc.depth, imageViews[size_t(depthSlot)])) {
        cleanupViews();
        error_ = "Vulkan: MRT depth image view creation failed";
        return false;
    }

    const VkFormat df = depth ? depth->vkFormat : VK_FORMAT_UNDEFINED;
    const bool clearDepth = depth && desc.depth.clear;
    const VkRenderPass rp = samples > 1 || desc.colorCount > 1 ?
        renderPassForMrt(activeFormats, df, desc.colorCount, clearMask,
                         clearDepth, samples, resolveMask) :
        renderPassFor(activeFormats[0], df, (clearMask & 1u) != 0, clearDepth);
    if (!rp) { cleanupViews(); return false; }
    auto fi = vkStruct<VkFramebufferCreateInfo>(VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);
    fi.renderPass = rp;
    fi.attachmentCount = uint32_t(nextAttachment + (depth ? 1 : 0));
    fi.pAttachments = imageViews.data();
    fi.width = uint32_t(width);
    fi.height = uint32_t(height);
    fi.layers = 1;
    VkFramebuffer fb = VK_NULL_HANDLE;
    if (vkCreateFramebuffer(device_, &fi, nullptr, &fb) != VK_SUCCESS) {
        cleanupViews();
        error_ = "Vulkan: MRT framebuffer creation failed";
        return false;
    }
    VkClearValue values[2*kMaxRenderTargetColors + 1]{};
    for (int i = 0; i < desc.colorCount; ++i)
        for (int channel = 0; channel < 4; ++channel)
            values[i].color.float32[channel] = desc.colors[size_t(i)].clearColor[channel];
    if (depth) values[depthSlot].depthStencil = {desc.clearDepth, 0};

    auto start = vkStruct<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    start.renderPass = rp;
    start.framebuffer = fb;
    start.renderArea = {{0,0}, {uint32_t(width), uint32_t(height)}};
    start.clearValueCount = fi.attachmentCount;
    start.pClearValues = values;
    vkCmdBeginRenderPass(slots_[size_t(slot_)].main, &start, VK_SUBPASS_CONTENTS_INLINE);
    scopedPassFb_ = fb;
    scopedPassColors_.fill(VK_NULL_HANDLE);
    for (int i = 0; i < desc.colorCount; ++i)
        scopedPassColors_[size_t(i)] = imageViews[i];
    scopedPassDepth_ = depth ? imageViews[size_t(depthSlot)] : VK_NULL_HANDLE;
    scopedPassResolves_ = resolveViews;
    passColors_ = desc.colorCount;
    passSamples_ = samples;
    passResolveMask_ = resolveMask;
    explicitMrtPass_ = true;
    inPass_ = true;
    passSkipped_ = false;
    pipelineId_ = 0;
    boundPipeline_ = VK_NULL_HANDLE;
    descriptorsDirty_ = true;
    passW_ = width;
    passH_ = height;
    passColor_ = activeFormats[0];
    passFormats_ = activeFormats;
    passDepth_ = df;
    // Legacy auto-HDR shader specialization is only valid for all-HDR MRT.
    // Heterogeneous MRT uses an explicitly typed PipelineDesc instead.
    hdrPass_ = allHdr;
    setViewport({0,0,width,height});
    setScissor({});
    return true;
}

void VulkanDevice::endPass() {
    if (!inPass_) return;
    if (!passSkipped_) vkCmdEndRenderPass(slots_[size_t(slot_)].main);
    if (scopedPassFb_) {
        // A framebuffer must outlive command execution; destroy its views
        // only after destroying the framebuffer, on frame completion.
        const VkFramebuffer fb = scopedPassFb_;
        const auto colorViews = scopedPassColors_;
        const auto resolveViews = scopedPassResolves_;
        const VkImageView depthView = scopedPassDepth_;
        deferDestroy([this, fb, colorViews, resolveViews, depthView] {
            vkDestroyFramebuffer(device_, fb, nullptr);
            for (VkImageView view : colorViews)
                if (view) vkDestroyImageView(device_, view, nullptr);
            for (VkImageView view : resolveViews)
                if (view) vkDestroyImageView(device_, view, nullptr);
            if (depthView) vkDestroyImageView(device_, depthView, nullptr);
        });
        scopedPassFb_ = VK_NULL_HANDLE;
        scopedPassColors_.fill(VK_NULL_HANDLE);
        scopedPassResolves_.fill(VK_NULL_HANDLE);
        scopedPassDepth_ = VK_NULL_HANDLE;
    }
    inPass_ = false;
    passSkipped_ = false;
    hdrPass_ = false;
    passColors_ = 1;
    passSamples_ = 1;
    passResolveMask_ = 0;
    explicitMrtPass_ = false;
    pipelineId_ = 0;
    boundPipeline_ = VK_NULL_HANDLE;
}

// Pixel rects are top-left based; the pass is stored bottom-up (GL), so y counts from the last row.
void VulkanDevice::setViewport(const Rect& r) {
    if (!inPass_ || passSkipped_) return;
    VkViewport v{float(r.x), float(passH_ - (r.y + r.h)), float(r.w), float(r.h), 0.0f, 1.0f};
    vkCmdSetViewport(slots_[size_t(slot_)].main, 0, 1, &v);
}

void VulkanDevice::setScissor(const Rect& r) {
    if (!inPass_ || passSkipped_) return;
    VkRect2D s{{0, 0}, {uint32_t(passW_), uint32_t(passH_)}};
    if (r.w > 0 && r.h > 0) {
        int x0 = std::clamp(r.x, 0, passW_), x1 = std::clamp(r.x + r.w, 0, passW_);
        int y0 = std::clamp(passH_ - (r.y + r.h), 0, passH_), y1 = std::clamp(passH_ - r.y, 0, passH_);
        s = {{x0, y0}, {uint32_t(std::max(0, x1 - x0)), uint32_t(std::max(0, y1 - y0))}};
    }
    vkCmdSetScissor(slots_[size_t(slot_)].main, 0, 1, &s);
}

// ---------- pipelines ----------
namespace {
VkFormat attrFormat(VertexFormat f, bool uscaled) {
    switch (f) {
        case VertexFormat::Float2: return VK_FORMAT_R32G32_SFLOAT;
        case VertexFormat::Float3: return VK_FORMAT_R32G32B32_SFLOAT;
        case VertexFormat::Float4: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case VertexFormat::UByte4N: return VK_FORMAT_R8G8B8A8_UNORM;
        case VertexFormat::UByte4: return uscaled ? VK_FORMAT_R8G8B8A8_USCALED : VK_FORMAT_R8G8B8A8_UNORM;
    }
    return VK_FORMAT_R32G32B32_SFLOAT;
}

void blendState(BlendMode m, VkPipelineColorBlendAttachmentState& a) {
    a.blendEnable = m != BlendMode::Opaque;
    a.colorBlendOp = a.alphaBlendOp = VK_BLEND_OP_ADD;
    switch (m) {
        case BlendMode::Opaque: break;
        case BlendMode::Alpha:
            a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
        case BlendMode::Additive:
            a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; break;
        case BlendMode::Premultiplied:
            a.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
            a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
        case BlendMode::Multiply:
            a.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR; a.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
            a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; break;
        case BlendMode::Subtract:
            a.colorBlendOp = VK_BLEND_OP_REVERSE_SUBTRACT;
            a.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
            a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO; a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE; break;
        case BlendMode::Screen:
            a.srcColorBlendFactor = VK_BLEND_FACTOR_ONE; a.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_COLOR;
            a.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; a.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; break;
    }
}
} // namespace

VkPipeline VulkanDevice::pipelineFor(
        VPipeline& p, const std::array<VkFormat, kMaxRenderTargetColors>& formats,
        VkFormat depth, bool hdr, int colorCount, int samples, uint32_t resolveMask) {
    const VkFormat color = formats[0];
    const uint64_t key = uint64_t(uint32_t(color)) |
        (uint64_t(uint32_t(depth) & 0x3FFFFFFFu) << 32) | (uint64_t(hdr) << 62);
    for (const auto& v : p.variants)
        if (v.key == key && v.colorCount == colorCount && v.formats == formats &&
            v.samples == samples && v.resolveMask == resolveMask)
            return v.pipeline;
    const PipelineDesc& d = p.desc;
    VShader* s = shaders_.get(d.shader.id);
    if (!s) return VK_NULL_HANDLE;
    VkShaderModule vert = s->vert, frag = s->frag;
    if (hdr) {   // RGBA16F pass, LDR pipeline: the same GLSL with AIVERSE_HDR=1 (as the GL backend)
        if (!s->hdrVert && !s->hdrFailed) {
            SpirvProgram prog;
            if (compileProgram(s->vs, s->fs, true, prog)) { s->hdrVert = createModule(prog.vertex); s->hdrFrag = createModule(prog.fragment); }
            else { std::fprintf(stderr, "[gfx] HDR shader: %s\n", prog.error.c_str()); }
            s->hdrFailed = !s->hdrVert || !s->hdrFrag;
        }
        if (s->hdrFailed) return VK_NULL_HANDLE;
        vert = s->hdrVert; frag = s->hdrFrag;
    }
    VkPipelineShaderStageCreateInfo stages[2]{};
    stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vert; stages[0].pName = "main";
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = frag; stages[1].pName = "main";

    VkVertexInputBindingDescription bind{0, d.stride, VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[8];
    const int ac = std::min(d.attrCount, 8);
    for (int i = 0; i < ac; ++i) attrs[i] = {d.attrs[i].location, 0, attrFormat(d.attrs[i].format, uscaled_), d.attrs[i].offset};
    auto vi = vkStruct<VkPipelineVertexInputStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    vi.vertexBindingDescriptionCount = ac > 0 ? 1 : 0;
    vi.pVertexBindingDescriptions = &bind;
    vi.vertexAttributeDescriptionCount = uint32_t(ac);
    vi.pVertexAttributeDescriptions = attrs;
    auto ia = vkStruct<VkPipelineInputAssemblyStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    ia.topology = d.primitive == Primitive::Lines ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto vp = vkStruct<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    vp.viewportCount = vp.scissorCount = 1;
    auto rs = vkStruct<VkPipelineRasterizationStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    rs.polygonMode = VK_POLYGON_MODE_FILL;
    rs.cullMode = d.cull == CullMode::None ? VK_CULL_MODE_NONE : d.cull == CullMode::Back ? VK_CULL_MODE_BACK_BIT : VK_CULL_MODE_FRONT_BIT;
    // Viewports are not flipped (GL orientation), so GL's counter-clockwise front faces read clockwise here.
    rs.frontFace = VK_FRONT_FACE_CLOCKWISE;
    rs.depthBiasEnable = d.depthBiasConstant != 0 || d.depthBiasSlope != 0;
    rs.depthBiasConstantFactor = d.depthBiasConstant;
    rs.depthBiasSlopeFactor = d.depthBiasSlope;
    rs.lineWidth = 1.0f;
    auto ms = vkStruct<VkPipelineMultisampleStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    ms.rasterizationSamples = VkSampleCountFlagBits(samples);
    auto ds = vkStruct<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    ds.depthTestEnable = d.depthTest && depth != VK_FORMAT_UNDEFINED;
    ds.depthWriteEnable = d.depthWrite && depth != VK_FORMAT_UNDEFINED;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState ba[kMaxRenderTargetColors]{};
    for (int i = 0; i < colorCount; ++i) {
        blendState(d.blend, ba[i]);
        ba[i].colorWriteMask = d.colorWrite ?
            VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
            VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT : 0;
    }
    auto cbs = vkStruct<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    cbs.attachmentCount = uint32_t(colorCount);
    cbs.pAttachments = ba;
    const VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    auto dy = vkStruct<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dy.dynamicStateCount = 2;
    dy.pDynamicStates = dyn;
    auto ci = vkStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    ci.stageCount = 2;
    ci.pStages = stages;
    ci.pVertexInputState = &vi;
    ci.pInputAssemblyState = &ia;
    ci.pViewportState = &vp;
    ci.pRasterizationState = &rs;
    ci.pMultisampleState = &ms;
    ci.pDepthStencilState = &ds;
    ci.pColorBlendState = &cbs;
    ci.pDynamicState = &dy;
    ci.layout = pipelineLayout_;
    ci.renderPass = samples > 1 || colorCount > 1 ?
        renderPassForMrt(formats, depth, colorCount, (1u << colorCount) - 1u,
                         true, samples, resolveMask) :
        renderPassFor(color, depth, true, true);   // any compatible load-op variant
    VkPipeline pipe = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &ci, nullptr, &pipe) != VK_SUCCESS) {
        error_ = "vkCreateGraphicsPipelines failed";
        std::fprintf(stderr, "[gfx] Vulkan pipeline %s failed\n", d.label ? d.label : "");
        pipe = VK_NULL_HANDLE;
    }
    p.variants.push_back({key, pipe, colorCount, formats, samples, resolveMask});
    return pipe;
}

void VulkanDevice::setPipeline(PipelineHandle h) {
    if (!inPass_ || passSkipped_) return;
    VPipeline* p = pipelines_.get(h.id);
    pipelineId_ = 0;
    boundPipeline_ = VK_NULL_HANDLE;
    if (!p) return;
    const bool hdr = hdrPass_ && !p->desc.depthOnly &&
                     p->desc.colorFormat != PixelFormat::RGBA16F;
    if (p->desc.sampleCount != passSamples_) {
        error_ = "Vulkan: pipeline raster sample count differs from render pass";
        return;
    }
    if (explicitMrtPass_) {
        const int expected = p->desc.depthOnly ? 0 : p->desc.colorAttachmentCount;
        if (expected != passColors_ || expected < 0 ||
            expected > kMaxRenderTargetColors) {
            error_ = "Vulkan: pipeline color output count differs from MRT pass";
            return;
        }
        for (int i = 0; i < expected; ++i) {
            const PixelFormat fmt = i == 0 ?
                p->desc.colorFormat : p->desc.additionalColorFormats[i - 1];
            if (!hdr && vkFormat(fmt) != passFormats_[size_t(i)]) {
                error_ = "Vulkan: pipeline format differs from MRT color slot";
                return;
            }
        }
    }
    // Legacy beginPass remains permissive: existing color/depth-only and sRGB
    // callers select pipeline variants by the real attachment configuration.
    const int actualColors = passColors_;
    VkPipeline vp = pipelineFor(*p, passFormats_, passDepth_, hdr, actualColors,
                                passSamples_, passResolveMask_);
    if (!vp) return;
    pipelineId_ = h.id;
    boundPipeline_ = vp;
    vkCmdBindPipeline(slots_[size_t(slot_)].main, VK_PIPELINE_BIND_POINT_GRAPHICS, vp);
    descriptorsDirty_ = true;   // rebinding the set after a pipeline change keeps validation quiet on layout-compatible drivers
}

void VulkanDevice::setVertexBuffer(BufferHandle b, size_t offset) { vbId_ = b.id; vbOffset_ = offset; }
void VulkanDevice::setIndexBuffer(BufferHandle b, size_t offset) { ibId_ = b.id; ibOffset_ = offset; }

void VulkanDevice::setTexture(int slot, TextureHandle t) {
    if (slot < 0 || slot >= textureSlots_) return;
    if (const VTexture* resource = textures_.get(t.id))
        if (resource->samples > 1) {
            error_ = "Vulkan: MSAA attachments cannot be sampled directly; use a resolve target";
            t = {};
        }
    const size_t i = size_t(slot);
    if (textures_bound_[i] != t.id || views_bound_[i] || samplers_bound_[i]) {
        textures_bound_[i] = t.id;
        views_bound_[i] = 0;
        samplers_bound_[i] = 0;
        descriptorsDirty_ = true;
    }
}
bool VulkanDevice::bindTextureView(int slot, TextureViewHandle view, SamplerHandle sampler) {
    if (slot < 0 || slot >= textureSlots_) return false;
    const VTextureView* v = views_.get(view.id);
    if (!v || !textures_.get(v->parentId)) return false;
    if (sampler.valid() && !customSamplers_.get(sampler.id)) return false;
    const size_t i = size_t(slot);
    textures_bound_[i] = v->parentId;
    views_bound_[i] = view.id;
    samplers_bound_[i] = sampler.id;
    descriptorsDirty_ = true;
    return true;
}

void VulkanDevice::setUniforms(int slot, const void* data, size_t size) {
    if (slot < 0 || slot >= kUniformSlots || size == 0 || !recording_) return;
    size = std::min(size, kUniformRange);
    FrameSlot& s = slots_[size_t(slot_)];
    const size_t align = std::max<size_t>(16, size_t(gpuProps_.limits.minUniformBufferOffsetAlignment));
    size_t off = (s.uniformOffset + align - 1) / align * align;
    if (off + kUniformRange > s.uniformSize) {   // grow the ring for the rest of this frame; the old one dies with it
        VkBuffer old = s.uniforms;
        Allocation oldAlloc = s.uniformAlloc;
        deferDestroy([this, old, oldAlloc]() mutable { vkDestroyBuffer(device_, old, nullptr); memory_.release(oldAlloc); });
        s.uniformSize *= 2;
        createBufferObject(s.uniformSize, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, true, s.uniforms, s.uniformAlloc);
        uniformOffsets_.fill(0);   // earlier slots pointed into the old buffer: their data is not carried over
        off = 0;
        descriptorsDirty_ = true;
    }
    std::memcpy(static_cast<uint8_t*>(s.uniformAlloc.mapped) + off, data, size);
    s.uniformOffset = off + size;
    uniformOffsets_[size_t(slot)] = uint32_t(off);
}

bool VulkanDevice::prepareDraw() {
    if (!inPass_ || passSkipped_ || !boundPipeline_) return false;
    FrameSlot& s = slots_[size_t(slot_)];
    VkCommandBuffer cb = s.main;
    if (descriptorsDirty_) {
        if (s.descriptorPool >= s.descriptorPools.size()) {
            const uint32_t sets = 1024;
            VkDescriptorPoolSize sizes[2] = {{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, sets * kUniformSlots},
                                             {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, sets * uint32_t(textureSlots_)}};
            auto pi = vkStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
            pi.maxSets = sets;
            pi.poolSizeCount = 2;
            pi.pPoolSizes = sizes;
            VkDescriptorPool pool = VK_NULL_HANDLE;
            if (vkCreateDescriptorPool(device_, &pi, nullptr, &pool) != VK_SUCCESS) return false;
            s.descriptorPools.push_back(pool);
        }
        auto ai = vkStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
        ai.descriptorPool = s.descriptorPools[s.descriptorPool];
        ai.descriptorSetCount = 1;
        ai.pSetLayouts = &setLayout_;
        if (vkAllocateDescriptorSets(device_, &ai, &currentSet_) != VK_SUCCESS) {
            ++s.descriptorPool;   // pool full: next one
            return prepareDraw();
        }
        VkDescriptorBufferInfo bufs[kUniformSlots];
        VkDescriptorImageInfo imgs[kMaxTextureSlots];
        VkWriteDescriptorSet w[2]{};
        for (int i = 0; i < kUniformSlots; ++i) bufs[i] = {s.uniforms, 0, kUniformRange};
        uint32_t shadow = 0;
        if (const VPipeline* p = pipelines_.get(pipelineId_)) if (const VShader* sh = shaders_.get(p->desc.shader.id)) shadow = sh->shadowSlots;
        for (int i = 0; i < textureSlots_; ++i) {
            const VTexture* t = textures_.get(textures_bound_[size_t(i)]);
            const VTexture& use = t ? *t : (shadow >> i) & 1u ? dummyDepth_ : dummy_;
            const VTextureView* view = views_.get(views_bound_[size_t(i)]);
            const VSampler* sampler = customSamplers_.get(samplers_bound_[size_t(i)]);
            imgs[i] = {sampler ? sampler->native : use.sampler,
                       view ? view->native : use.view,
                       VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        }
        w[0].sType = w[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w[0].dstSet = w[1].dstSet = currentSet_;
        w[0].dstBinding = 0;
        w[0].descriptorCount = kUniformSlots;
        w[0].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
        w[0].pBufferInfo = bufs;
        w[1].dstBinding = kTextureBinding0;
        w[1].descriptorCount = uint32_t(textureSlots_);
        w[1].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w[1].pImageInfo = imgs;
        vkUpdateDescriptorSets(device_, 2, w, 0, nullptr);
        descriptorsDirty_ = false;
    }
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1, &currentSet_, kUniformSlots, uniformOffsets_.data());
    return true;
}

void VulkanDevice::draw(uint32_t vertexCount, uint32_t firstVertex) {
    if (!prepareDraw() || vertexCount == 0) return;
    VkCommandBuffer cb = slots_[size_t(slot_)].main;
    const VPipeline* p = pipelines_.get(pipelineId_);
    if (p->desc.attrCount > 0) {
        VkBuffer vb = resolveBuffer(vbId_, serial_);
        if (!vb) return;
        const VkDeviceSize off = vbOffset_;
        vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
    }
    vkCmdDraw(cb, vertexCount, 1, firstVertex, 0);
}

void VulkanDevice::drawIndexed(uint32_t indexCount, uint32_t firstIndex, int32_t baseVertex, uint32_t instances) {
    if (!prepareDraw() || indexCount == 0 || instances == 0) return;
    VkCommandBuffer cb = slots_[size_t(slot_)].main;
    const VPipeline* p = pipelines_.get(pipelineId_);
    VkBuffer ib = resolveBuffer(ibId_, serial_);
    if (!ib) return;
    if (p->desc.attrCount > 0) {
        VkBuffer vb = resolveBuffer(vbId_, serial_);
        if (!vb) return;
        // base vertex as a buffer offset (as the GL backend): gl_VertexIndex stays the plain index
        const VkDeviceSize off = VkDeviceSize(int64_t(vbOffset_) + int64_t(baseVertex) * int64_t(p->desc.stride));
        vkCmdBindVertexBuffers(cb, 0, 1, &vb, &off);
    }
    const bool u32 = p->desc.indexType == IndexType::UInt32;
    vkCmdBindIndexBuffer(cb, ib, ibOffset_, u32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cb, indexCount, instances, firstIndex, 0, 0);
}

// Pooled rigid indexed models use the Vulkan-standard vertexOffset rule:
 // the physical VB binding stays at its page base, while the GPU adds
 // baseVertex to each fetched index. The shader-visible gl_VertexIndex is
 // therefore identical to a matching VkDrawIndexedIndirectCommand. The old
 // drawIndexed() remains untouched for other RHI clients (including morph).
void VulkanDevice::drawIndexedCanonical(uint32_t indexCount, uint32_t firstIndex,
                                        int32_t baseVertex, uint32_t instances) {
    if (!prepareDraw() || !indexCount || !instances) return;
    VkCommandBuffer cb = slots_[size_t(slot_)].main;
    const VPipeline* p = pipelines_.get(pipelineId_);
    if (!p) return;
    VkBuffer ib = resolveBuffer(ibId_, serial_);
    if (!ib) return;
    if (p->desc.attrCount > 0) {
        VkBuffer vb = resolveBuffer(vbId_, serial_);
        if (!vb) return;
        const VkDeviceSize offset = vbOffset_;
        vkCmdBindVertexBuffers(cb, 0, 1, &vb, &offset);
    }
    const bool u32 = p->desc.indexType == IndexType::UInt32;
    vkCmdBindIndexBuffer(cb, ib, ibOffset_, u32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    vkCmdDrawIndexed(cb, indexCount, instances, firstIndex, baseVertex, 0);
}

bool VulkanDevice::drawIndexedIndirect(BufferHandle commands, size_t offset,
                                         uint32_t drawCount, uint32_t stride) {
    if (!multiDrawIndirect_ || !drawCount || stride < sizeof(DrawIndexedIndirectCommand) ||
        (stride & 3u) || (offset & 3u)) return false;
    VBuffer* cmd = buffers_.get(commands.id);
    if (!cmd || cmd->type != BufferType::Indirect ||
        offset > cmd->size || size_t(drawCount - 1) * stride + sizeof(DrawIndexedIndirectCommand) > cmd->size - offset)
        return false;
    return encodeIndexedIndirect(commands, offset, drawCount, stride);
}

bool VulkanDevice::encodeIndexedIndirect(BufferHandle commands, size_t offset,
                                         uint32_t drawCount, uint32_t stride) {
    if (!prepareDraw()) return false;
    const VPipeline* p = pipelines_.get(pipelineId_);
    VkCommandBuffer cb = slots_[size_t(slot_)].main;
    VkBuffer ib = resolveBuffer(ibId_, serial_);
    VkBuffer indirect = resolveBuffer(commands.id, serial_);
    if (!ib || !indirect || !p) return false;
    if (p->desc.attrCount > 0) {
        VkBuffer vb = resolveBuffer(vbId_, serial_);
        if (!vb) return false;
        const VkDeviceSize base = vbOffset_;
        vkCmdBindVertexBuffers(cb, 0, 1, &vb, &base);
    }
    vkCmdBindIndexBuffer(cb, ib, ibOffset_,
                         p->desc.indexType == IndexType::UInt32 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16);
    const uint32_t maxCount = std::max(1u, gpuProps_.limits.maxDrawIndirectCount);
    for (uint32_t first = 0; first < drawCount;) {
        const uint32_t batch = std::min(maxCount, drawCount - first);
        vkCmdDrawIndexedIndirect(cb, indirect, VkDeviceSize(offset) + VkDeviceSize(first) * stride,
                                 batch, stride);
        first += batch;
    }
    return true;
}

bool VulkanDevice::drawIndexedGpuIndirect(BufferHandle arguments,
                                                 size_t offset) {
    // The caller must already bind the correct shader pipeline, materials,
    // instance uniforms and matching source VB/IB. This RHI API only consumes
    // the one GPU-authored command; it NEVER bypasses Renderer3D decisions.
    // One draw needs no multiDrawIndirect feature; the GPU-written
    // firstInstance does need drawIndirectFirstInstance.
    if(!supportsCompute() || !drawIndirectFirstInstance_ || !recording_ ||
       !inPass_ || activeComputeId_ || !healthy_ || (offset & 3u))return false;
    // Indirect and Storage buffers both carry INDIRECT usage (bufferUsage).
    const VBuffer* args = buffers_.get(arguments.id);
    if(!args || (args->type != BufferType::Indirect &&
                 args->type != BufferType::Storage) ||
       offset > args->size ||
       sizeof(DrawIndexedIndirectCommand) > args->size - offset)return false;
    // The earlier compute dispatch encodes its shader-write ->
    // indirect-command barrier (VkCompute.cpp, global memory barrier).
    return encodeIndexedIndirect(arguments,offset,1,
                                 sizeof(DrawIndexedIndirectCommand));
}

bool VulkanDevice::copyColor(TextureHandle from, TextureHandle to) {
    if (!recording_ || inPass_) return false;
    VTexture* dst = textures_.get(to.id);
    if (!dst || dst->dimension != TextureDimension::Texture2D ||
        dst->format != PixelFormat::RGBA8 || dst->levels != 1) return false;
    VTexture* src = from.valid() ? textures_.get(from.id) : &back_;
    if (!src || src->dimension != TextureDimension::Texture2D ||
        src->depth || src->width != dst->width || src->height != dst->height) return false;
    VkCommandBuffer cb = slots_[size_t(slot_)].main;
    barrierImage(cb, src->image, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barrierImage(cb, dst->image, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    VkImageBlit b{};   // a blit converts formats (sRGB / float sources), as glBlitFramebuffer
    b.srcSubresource = b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    b.srcOffsets[1] = b.dstOffsets[1] = {src->width, src->height, 1};
    vkCmdBlitImage(cb, src->image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, dst->image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_NEAREST);
    barrierImage(cb, dst->image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrierImage(cb, src->image, false, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

} // namespace aiverse::gfx::vk
