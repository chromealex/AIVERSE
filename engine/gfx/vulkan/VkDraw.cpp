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
    inPass_ = true;
    pipelineId_ = 0;
    boundPipeline_ = VK_NULL_HANDLE;
    descriptorsDirty_ = true;
    if (passSkipped_) return;
    const VTexture& ref = color ? *color : *depth;
    passW_ = ref.width; passH_ = ref.height;
    passColor_ = color ? color->vkFormat : VK_FORMAT_UNDEFINED;
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

void VulkanDevice::endPass() {
    if (!inPass_) return;
    if (!passSkipped_) vkCmdEndRenderPass(slots_[size_t(slot_)].main);
    inPass_ = false;
    passSkipped_ = false;
    hdrPass_ = false;
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

VkPipeline VulkanDevice::pipelineFor(VPipeline& p, VkFormat color, VkFormat depth, bool hdr) {
    const uint64_t key = uint64_t(uint32_t(color)) | (uint64_t(uint32_t(depth) & 0x3FFFFFFFu) << 32) | (uint64_t(hdr) << 62);
    for (const auto& v : p.variants) if (v.key == key) return v.pipeline;
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
    ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    auto ds = vkStruct<VkPipelineDepthStencilStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    ds.depthTestEnable = d.depthTest && depth != VK_FORMAT_UNDEFINED;
    ds.depthWriteEnable = d.depthWrite && depth != VK_FORMAT_UNDEFINED;
    ds.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    VkPipelineColorBlendAttachmentState ba{};
    blendState(d.blend, ba);
    ba.colorWriteMask = d.colorWrite ? VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT : 0;
    auto cbs = vkStruct<VkPipelineColorBlendStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    cbs.attachmentCount = color != VK_FORMAT_UNDEFINED ? 1 : 0;
    cbs.pAttachments = &ba;
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
    ci.renderPass = renderPassFor(color, depth, true, true);   // any compatible pass
    VkPipeline pipe = VK_NULL_HANDLE;
    if (vkCreateGraphicsPipelines(device_, pipelineCache_, 1, &ci, nullptr, &pipe) != VK_SUCCESS) {
        error_ = "vkCreateGraphicsPipelines failed";
        std::fprintf(stderr, "[gfx] Vulkan pipeline %s failed\n", d.label ? d.label : "");
        pipe = VK_NULL_HANDLE;
    }
    p.variants.push_back({key, pipe});
    return pipe;
}

void VulkanDevice::setPipeline(PipelineHandle h) {
    if (!inPass_ || passSkipped_) return;
    VPipeline* p = pipelines_.get(h.id);
    pipelineId_ = 0;
    boundPipeline_ = VK_NULL_HANDLE;
    if (!p) return;
    const bool hdr = hdrPass_ && !p->desc.depthOnly && p->desc.colorFormat != PixelFormat::RGBA16F;
    VkPipeline vp = pipelineFor(*p, passColor_, passDepth_, hdr);
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
    if (textures_bound_[size_t(slot)] != t.id) { textures_bound_[size_t(slot)] = t.id; descriptorsDirty_ = true; }
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
            imgs[i] = {use.sampler, use.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
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

bool VulkanDevice::copyColor(TextureHandle from, TextureHandle to) {
    if (!recording_ || inPass_ || (from.valid() && from.id==to.id)) return false;
    VTexture* dst = textures_.get(to.id);
    // RGBA8 preserves the established conversion/blit path. Motion vectors
    // require a format-exact RGBA16F copy: blitting or 8-bit conversion
    // would lose signed current-minus-previous UV and B-valid semantics.
    if (!dst || dst->levels!=1 ||
        (dst->format!=PixelFormat::RGBA8 && dst->format!=PixelFormat::RGBA16F))
        return false;
    VTexture* src = from.valid() ? textures_.get(from.id) : &back_;
    if (!src || src->depth || src->levels!=1 ||
        src->width!=dst->width || src->height!=dst->height ||
        (dst->format==PixelFormat::RGBA16F && src->format!=dst->format))
        return false;
    VkCommandBuffer cb = slots_[size_t(slot_)].main;
    barrierImage(cb, src->image, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);
    barrierImage(cb, dst->image, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if(dst->format==PixelFormat::RGBA16F) {
        VkImageCopy c{};
        c.srcSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        c.dstSubresource=c.srcSubresource;
        c.extent={uint32_t(src->width),uint32_t(src->height),1};
        vkCmdCopyImage(cb,src->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&c);
    } else {
        VkImageBlit b{}; // existing RGBA8 blit/conversion path, unchanged
        b.srcSubresource=b.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        b.srcOffsets[1]=b.dstOffsets[1]={src->width,src->height,1};
        vkCmdBlitImage(cb,src->image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       dst->image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                       1,&b,VK_FILTER_NEAREST);
    }
    barrierImage(cb,dst->image,false,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    barrierImage(cb,src->image,false,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    return true;
}

} // namespace aiverse::gfx::vk
