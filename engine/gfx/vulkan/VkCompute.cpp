// Vulkan compute RHI. Uses the existing graphics queue (which must support
// VK_QUEUE_COMPUTE_BIT), the same frame command buffer, and per-frame descriptor
// pools reset only AFTER the frame fence. No device idle stalls or CPU readbacks.
// The GI path writes an RGBA16F storage image and transitions it back to sampled
// layout before fragment shading. See GiGpuVulkanShaders.h for the exact ABI.
#include "VkDevice.h"
#include <algorithm>
#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace aiverse::gfx::vk {

ComputeHandle VulkanDevice::createCompute(const ComputeDesc& d) {
    if (!supportsCompute() || !d.glsl) return {};
    std::vector<uint32_t> words;
    std::string compileError;
    if (!compileComputeProgram(d.glsl,words,compileError)) {
        error_ = std::move(compileError);
        return {};
    }
    VCompute c;
    c.shader=createModule(words);
    if (!c.shader) { error_="Vulkan GI compute: shader module failed"; return {}; }
    const auto fail=[&]() -> ComputeHandle {
        error_="Vulkan GI compute: descriptor layout/pipeline creation failed";
        if(c.pipeline)vkDestroyPipeline(device_,c.pipeline,nullptr);
        if(c.layout)vkDestroyPipelineLayout(device_,c.layout,nullptr);
        if(c.setLayout)vkDestroyDescriptorSetLayout(device_,c.setLayout,nullptr);
        if(c.shader)vkDestroyShaderModule(device_,c.shader,nullptr);
        return {};
    };

    VkDescriptorSetLayoutBinding bindings[5]{};
    for(uint32_t slot=0;slot<5;++slot) {
        bindings[slot].binding=slot;
        bindings[slot].descriptorType=slot==4 ? VK_DESCRIPTOR_TYPE_STORAGE_IMAGE
                                              : VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[slot].descriptorCount=1;
        bindings[slot].stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    }
    auto sl=vkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    sl.bindingCount=5;
    sl.pBindings=bindings;
    if(vkCreateDescriptorSetLayout(device_,&sl,nullptr,&c.setLayout)!=VK_SUCCESS)
        return fail();

    VkPushConstantRange push{};
    push.stageFlags=VK_SHADER_STAGE_COMPUTE_BIT;
    push.offset=0;
    push.size=80; // GiGpuSmallScene::Uniforms / GLSL GIParams (sun + sky)
    {
        auto layout=vkStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
        layout.setLayoutCount=1;
        layout.pSetLayouts=&c.setLayout;
        layout.pushConstantRangeCount=1;
        layout.pPushConstantRanges=&push;
        if(vkCreatePipelineLayout(device_,&layout,nullptr,&c.layout)!=VK_SUCCESS)
            return fail();
    }
    {
        auto pi=vkStruct<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
        pi.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
        pi.stage.module=c.shader;
        pi.stage.pName="main";
        pi.layout=c.layout;
        if(vkCreateComputePipelines(device_,pipelineCache_,1,&pi,nullptr,&c.pipeline)!=VK_SUCCESS)
            return fail();
    }
    return {computes_.add(c)};

}

void VulkanDevice::destroyCompute(ComputeHandle h) {
    VCompute* c=computes_.get(h.id);
    if(!c)return;
    if(activeCompute_==h.id)endCompute();
    const VCompute old=*c;
    computes_.remove(h.id);
    deferDestroy([this,old] {
        if(old.pipeline)vkDestroyPipeline(device_,old.pipeline,nullptr);
        if(old.layout)vkDestroyPipelineLayout(device_,old.layout,nullptr);
        if(old.setLayout)vkDestroyDescriptorSetLayout(device_,old.setLayout,nullptr);
        if(old.shader)vkDestroyShaderModule(device_,old.shader,nullptr);
    });
}

bool VulkanDevice::beginCompute(ComputeHandle h) {
    if(!computeAvailable_ || !recording_ || inPass_ || activeCompute_ ||
       !healthy_ || !computes_.get(h.id))return false;
    activeCompute_=h.id;
    computeBuffers_.fill({});
    computeTexture_={};
    computeUniformSize_=0;
    computeImageWriting_=false;
    computeDispatchSucceeded_=false;
    return true;
}

void VulkanDevice::setComputeBuffer(int slot, BufferHandle h) {
    if(!activeCompute_ || slot<0 || slot>=int(computeBuffers_.size()))return;
    const VBuffer* b=buffers_.get(h.id);
    computeBuffers_[size_t(slot)]=(b && b->type==BufferType::Storage)?h:BufferHandle{};
}

void VulkanDevice::setComputeTexture(int slot, TextureHandle h) {
    if(!activeCompute_ || slot!=0)return;
    // Switching output storage images inside one compute pass is legal, but
    // the PREVIOUS image must return to sampled layout after its writes.
    // Otherwise its next fragment reader sees GENERAL while the descriptor
    // expects SHADER_READ_ONLY_OPTIMAL.
    if(computeImageWriting_ && computeTexture_.id!=h.id) {
        if(VTexture* previous=textures_.get(computeTexture_.id))
            barrierImage(slots_[size_t(slot_)].main,previous->image,false,
                         VK_IMAGE_LAYOUT_GENERAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        computeImageWriting_=false;
    }
    const VTexture* t=textures_.get(h.id);
    computeTexture_=(t && t->storage && t->format==PixelFormat::RGBA16F)
        ? h : TextureHandle{};
}

void VulkanDevice::setComputeUniforms(const void* data,size_t size) {
    if(!activeCompute_ || !data || size==0 || size>computeUniforms_.size() ||
       size>gpuProps_.limits.maxPushConstantsSize){
        computeUniformSize_=0;
        return;
    }
    std::memcpy(computeUniforms_.data(),data,size);
    computeUniformSize_=size;
}

void VulkanDevice::dispatch(uint32_t logicalX,uint32_t logicalY) {
    computeDispatchSucceeded_=false;
    VCompute* c=computes_.get(activeCompute_);
    VTexture* texture=textures_.get(computeTexture_.id);
    if(!recording_ || inPass_ || !c || !texture || !texture->storage ||
       logicalX==0 || logicalY!=1 || computeUniformSize_!=80 ||
       uint64_t(logicalX)>uint64_t(gpuProps_.limits.maxComputeWorkGroupCount[0])*64u)
        return;
    std::array<VkDescriptorBufferInfo,4> buffers{};
    for(size_t i=0;i<buffers.size();++i) {
        VBuffer* b=this->buffers_.get(computeBuffers_[i].id);
        if(!b || b->type!=BufferType::Storage || b->versions.empty())return;
        buffers[i].buffer=resolveBuffer(computeBuffers_[i].id,serial_);
        buffers[i].offset=0;
        buffers[i].range=VkDeviceSize(b->size);
        if(!buffers[i].buffer)return;
    }
    FrameSlot& s=slots_[size_t(slot_)];
    if(s.computePool>=s.computePools.size()) {
        VkDescriptorPoolSize sizes[2]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4},
                                      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1}};
        auto pi=vkStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
        pi.maxSets=1;
        pi.poolSizeCount=2;
        pi.pPoolSizes=sizes;
        VkDescriptorPool pool=VK_NULL_HANDLE;
        if(vkCreateDescriptorPool(device_,&pi,nullptr,&pool)!=VK_SUCCESS) {
            error_="Vulkan GI compute descriptor pool failed";
            return;
        }
        s.computePools.push_back(pool);
    }
    VkDescriptorPool pool=s.computePools[s.computePool++];
    auto alloc=vkStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    alloc.descriptorPool=pool;
    alloc.descriptorSetCount=1;
    alloc.pSetLayouts=&c->setLayout;
    VkDescriptorSet set=VK_NULL_HANDLE;
    if(vkAllocateDescriptorSets(device_,&alloc,&set)!=VK_SUCCESS) {
        error_="Vulkan GI compute descriptor allocation failed";
        return;
    }
    VkDescriptorImageInfo image{};
    image.imageView=texture->view;
    image.imageLayout=VK_IMAGE_LAYOUT_GENERAL;
    std::array<VkWriteDescriptorSet,5> writes{};
    for(uint32_t i=0;i<5;++i) {
        writes[i].sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dstSet=set;
        writes[i].dstBinding=i;
        writes[i].descriptorCount=1;
        writes[i].descriptorType=i==4?VK_DESCRIPTOR_TYPE_STORAGE_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        if(i==4)writes[i].pImageInfo=&image;
        else writes[i].pBufferInfo=&buffers[i];
    }
    vkUpdateDescriptorSets(device_,uint32_t(writes.size()),writes.data(),0,nullptr);
    const VkCommandBuffer command=s.main;
    if(!computeImageWriting_) {
        barrierImage(command,texture->image,false,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL);
        computeImageWriting_=true;
    } else {
        // A second dispatch may consume/overwrite the same atlas texels.
        // Command submission order alone is NOT a shader-storage memory
        // dependency. Make earlier compute writes visible before next work.
        auto barrier=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dstAccessMask=VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                             VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,
                             1,&barrier,0,nullptr,0,nullptr);
    }
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,c->pipeline);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,
                            c->layout,0,1,&set,0,nullptr);
    vkCmdPushConstants(command,c->layout,VK_SHADER_STAGE_COMPUTE_BIT,
                       0,uint32_t(computeUniformSize_),computeUniforms_.data());
    vkCmdDispatch(command,(logicalX+63u)/64u,1,1);
    computeDispatchSucceeded_=true;
}

void VulkanDevice::endCompute() {
    if(!activeCompute_)return;
    if(computeImageWriting_ && recording_ && !inPass_) {
        if(VTexture* t=textures_.get(computeTexture_.id))
            barrierImage(slots_[size_t(slot_)].main,t->image,false,
                         VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    computeImageWriting_=false;
    activeCompute_=0;
    computeTexture_={};
    computeBuffers_.fill({});
    computeUniformSize_=0;
}

} // namespace aiverse::gfx::vk
