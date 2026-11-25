// Vulkan compute backend for AIVERSE's genre-independent Device API.
// Storage images: set=0 bindings 0..3, sampled textures: bindings 4..11,
// storage buffers: 12..15, optional std140 uniform block: binding 16.
// GLSL compute source is Vulkan 450 body (compileComputeProgram supplies
// #version and 64x1x1 local size if it is not declared by the caller).
#include "VkDevice.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <vector>

namespace aiverse::gfx::vk {

bool VulkanDevice::supportsStorageUniformCompute(uint32_t slots) const{
    const auto& l=gpuProps_.limits;
    return supportsCompute()&&slots>=9&&slots<=32&&
        slots-1<=l.maxPerStageDescriptorStorageBuffers&&slots-1<=l.maxDescriptorSetStorageBuffers&&
        slots<=l.maxPerStageResources&&l.maxPerStageDescriptorUniformBuffers>=1&&
        l.maxDescriptorSetUniformBuffers>=1&&l.maxUniformBufferRange>=4096;
}
ComputeHandle VulkanDevice::createCompute(const ComputeDesc& desc) {
    if (!supportsCompute() || !desc.glsl) {
        error_="Vulkan: compute requires a compute-capable queue and GLSL source";
        return {};
    }
    std::vector<uint32_t> spirv;
    const bool bufferOnly=desc.vulkanLayout==VulkanComputeLayout::BufferPushConstants;
    const bool extended=desc.vulkanLayout==VulkanComputeLayout::StorageUniforms;
    if(extended&&!supportsStorageUniformCompute(desc.storageUniformSlots)){
        error_="Vulkan: insufficient storage/uniform compute descriptors";return {};
    }
    if (!((bufferOnly||extended) ? compileComputeProgram(desc.glsl,spirv,&error_)
                     : compileComputeProgram(desc.glsl,spirv,error_))) return {};
    VCompute compute;
    compute.bufferPushConstants=bufferOnly;compute.storageUniforms=extended;
    compute.bufferBindings=extended?desc.storageUniformSlots:bufferOnly ? std::min(8u,std::min(
        gpuProps_.limits.maxPerStageDescriptorStorageBuffers,
        gpuProps_.limits.maxDescriptorSetStorageBuffers)) : 4u;
    if (compute.bufferBindings<4 || (bufferOnly && gpuProps_.limits.maxPushConstantsSize<128)) {
        error_="Vulkan: insufficient compute buffer/push-constant limits";
        return {};
    }
    compute.shader=createModule(spirv);
    if (!compute.shader) {
        error_="Vulkan: compute shader module allocation failed";
        return {};
    }
    auto cleanup=[&]() {
        if (compute.pipeline) vkDestroyPipeline(device_,compute.pipeline,nullptr);
        if (compute.layout) vkDestroyPipelineLayout(device_,compute.layout,nullptr);
        if (compute.descriptorLayout)
            vkDestroyDescriptorSetLayout(device_,compute.descriptorLayout,nullptr);
        if (compute.shader) vkDestroyShaderModule(device_,compute.shader,nullptr);
    };
    std::array<VkDescriptorSetLayoutBinding,32> bindings{};
    for (uint32_t i=0;i<4;++i)
        bindings[i]={i,VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    for (uint32_t i=0;i<8;++i)
        bindings[4u+i]={4u+i,VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1,
                        VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    for (uint32_t i=0;i<4;++i)
        bindings[12u+i]={12u+i,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,
                         VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    bindings[16]={16u,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    auto li=vkStruct<VkDescriptorSetLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    if (bufferOnly||extended) {
        bindings={};
        for (uint32_t i=0;i<compute.bufferBindings;++i)
            bindings[i]={i,extended&&i==8?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr};
    }
    li.bindingCount=(bufferOnly||extended) ? compute.bufferBindings : 17u;
    li.pBindings=bindings.data();
    if (vkCreateDescriptorSetLayout(device_,&li,nullptr,&compute.descriptorLayout)!=VK_SUCCESS) {
        error_="Vulkan: compute resource binding layout creation failed";
        cleanup();return {};
    }
    auto pl=vkStruct<VkPipelineLayoutCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pl.setLayoutCount=1;pl.pSetLayouts=&compute.descriptorLayout;
    VkPushConstantRange push{VK_SHADER_STAGE_COMPUTE_BIT,0,128};
    if (bufferOnly) { pl.pushConstantRangeCount=1;pl.pPushConstantRanges=&push; }
    if (vkCreatePipelineLayout(device_,&pl,nullptr,&compute.layout)!=VK_SUCCESS) {
        error_="Vulkan: compute pipeline layout creation failed";
        cleanup();return {};
    }
    auto ci=vkStruct<VkComputePipelineCreateInfo>(
        VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);
    ci.layout=compute.layout;
    ci.stage=vkStruct<VkPipelineShaderStageCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    ci.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
    ci.stage.module=compute.shader;
    ci.stage.pName="main";
    if (vkCreateComputePipelines(device_,pipelineCache_,1,&ci,nullptr,
                                 &compute.pipeline)!=VK_SUCCESS) {
        error_="Vulkan: compute pipeline creation failed (check shader bindings)";
        cleanup();return {};
    }
    error_.clear();
    return {computes_.add(compute)};
}

void VulkanDevice::destroyCompute(ComputeHandle handle) {
    VCompute* c=computes_.get(handle.id);
    if (!c) return;
    if (activeComputeId_==handle.id) endCompute();
    const VCompute disposed=*c;
    computes_.remove(handle.id);
    deferDestroy([this,disposed]() {
        if (disposed.pipeline) vkDestroyPipeline(device_,disposed.pipeline,nullptr);
        if (disposed.layout) vkDestroyPipelineLayout(device_,disposed.layout,nullptr);
        if (disposed.descriptorLayout)
            vkDestroyDescriptorSetLayout(device_,disposed.descriptorLayout,nullptr);
        if (disposed.shader) vkDestroyShaderModule(device_,disposed.shader,nullptr);
    });
}

bool VulkanDevice::beginCompute(ComputeHandle handle) {
    if (!supportsCompute() || !recording_ || inPass_ || activeComputeId_ ||
        !computes_.get(handle.id)) {
        error_="Vulkan: compute must begin within a graphics frame outside render passes";
        return false;
    }
    activeComputeId_=handle.id;
    computeStateValid_=true;
    computeStorage_.fill({});
    computeSampled_.fill({});
    computeBuffers_.fill({});
    computeUniforms_.clear();
    error_.clear();
    return true;
}

void VulkanDevice::setComputeBuffer(int slot,BufferHandle buffer) {
    const auto* compute=computes_.get(activeComputeId_);
    const auto* source=buffers_.get(buffer.id);
    if (!compute || slot<0 || uint32_t(slot)>=compute->bufferBindings || (compute->storageUniforms&&slot==8)|| !source ||
        (source->type!=BufferType::Storage &&
         !((compute->bufferPushConstants||compute->storageUniforms) && source->type==BufferType::Indirect))) {
        computeStateValid_=false;
        error_="Vulkan: invalid compute storage buffer binding";
        return;
    }
    computeBuffers_[size_t(slot)]=buffer;
}

void VulkanDevice::setComputeTexture(int slot,TextureHandle image) {
    if (!activeComputeId_ || (computes_.get(activeComputeId_)->bufferPushConstants||computes_.get(activeComputeId_)->storageUniforms) ||
        slot<0 || slot>=int(computeSampled_.size()) ||
        !textures_.get(image.id)) {
        computeStateValid_=false;
        error_="Vulkan: compute sampled texture requires active pipeline and slot 0..7";
        return;
    }
    computeSampled_[size_t(slot)]=image;
}

bool VulkanDevice::bindComputeStorageTexture(int slot,TextureHandle image) {
    const VTexture* t=textures_.get(image.id);
    if (!activeComputeId_ || (computes_.get(activeComputeId_)->bufferPushConstants||computes_.get(activeComputeId_)->storageUniforms) ||
        slot<0 || slot>=int(computeStorage_.size()) ||
        !t || !t->image || !t->storage || t->samples!=1 ||
        !supportsStorageTexture(t->format,t->dimension)) {
        computeStateValid_=false;
        error_="Vulkan: compute write requires a storage image and writable slot 0..3";
        return false;
    }
    computeStorage_[size_t(slot)]=image;
    return true;
}

void VulkanDevice::setComputeUniforms(const void* data,size_t size) {
    const auto* compute=computes_.get(activeComputeId_);
    if (!compute || (size && !data) || size>4096 ||
        (compute->bufferPushConstants && (!size || size>128 || (size&3u)))) {
        computeStateValid_=false;
        error_="Vulkan: compute uniform bytes require active pipeline and size <=4096";
        return;
    }
    if (size) {
        const auto* ptr=static_cast<const uint8_t*>(data);
        computeUniforms_.assign(ptr,ptr+size);
    } else computeUniforms_.clear();
}

void VulkanDevice::dispatch(uint32_t x,uint32_t y) {
    (void)dispatchChecked(x,y);
}

bool VulkanDevice::dispatchChecked(uint32_t x,uint32_t y){return dispatchInternal(x,y,{},0);}
bool VulkanDevice::dispatchGpuIndirect(BufferHandle args,size_t offset){
    if(!args.valid())return false;
    return dispatchInternal(0,0,args,offset);
}
bool VulkanDevice::dispatchInternal(uint32_t x,uint32_t y,BufferHandle indirect,size_t offset) {
    if (!activeComputeId_ || !computeStateValid_ || !healthy_ || !recording_ || inPass_ || (!indirect.valid()&&(!x||!y))) return false;
    VkBuffer indirectBuffer=VK_NULL_HANDLE;
    if(indirect.valid()){
        const auto* b=buffers_.get(indirect.id);
        if(!b||(b->type!=BufferType::Storage&&b->type!=BufferType::Indirect)||(offset&3u)||offset>b->size||12u>b->size-offset)return false;
        indirectBuffer=resolveBuffer(indirect.id,serial_);if(!indirectBuffer)return false;
    }
    const VCompute* compute=computes_.get(activeComputeId_);
    if (!compute || ((compute->bufferPushConstants||compute->storageUniforms) && computeUniforms_.empty())) {
        error_="Vulkan: compute resource was destroyed";return false;
    }
    if (!indirect.valid()&&((uint64_t(x)+63u)/64u>gpuProps_.limits.maxComputeWorkGroupCount[0] ||
        y>gpuProps_.limits.maxComputeWorkGroupCount[1])) {
        error_="Vulkan: compute dispatch exceeds device workgroup count limit";
        return false;
    }
    for (auto image : computeStorage_)
        if (image.valid())
            for (auto sample : computeSampled_)
                if (sample==image) {
                    error_="Vulkan: a compute dispatch cannot read and write the same image";
                    return false;
                }

    std::array<VkDescriptorPoolSize,4> poolSizes{{
        {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,4},
        {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,8},
        {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,4},
        {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}
    }};
    auto poolInfo=vkStruct<VkDescriptorPoolCreateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    poolInfo.maxSets=1;
    VkDescriptorPoolSize bufferPool{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,compute->bufferBindings};
    const VkDescriptorPoolSize extendedPool[2]={{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,compute->bufferBindings-1},{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1}};
    poolInfo.poolSizeCount=compute->storageUniforms?2u:compute->bufferPushConstants ? 1u : uint32_t(poolSizes.size());
    poolInfo.pPoolSizes=compute->storageUniforms?extendedPool:compute->bufferPushConstants ? &bufferPool : poolSizes.data();
    VkDescriptorPool pool=VK_NULL_HANDLE;
    if (vkCreateDescriptorPool(device_,&poolInfo,nullptr,&pool)!=VK_SUCCESS) {
        error_="Vulkan: compute descriptor pool allocation failed";return false;
    }
    auto ai=vkStruct<VkDescriptorSetAllocateInfo>(
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    ai.descriptorPool=pool;ai.descriptorSetCount=1;
    ai.pSetLayouts=&compute->descriptorLayout;
    VkDescriptorSet set=VK_NULL_HANDLE;
    if (vkAllocateDescriptorSets(device_,&ai,&set)!=VK_SUCCESS) {
        error_="Vulkan: compute descriptor set allocation failed";
        vkDestroyDescriptorPool(device_,pool,nullptr);return false;
    }

    // Immutable addresses; no vector reallocation can invalidate the
    // VkWriteDescriptorSet pImageInfo/pBufferInfo pointers.
    std::array<VkDescriptorImageInfo,12> images{};
    std::array<VkDescriptorBufferInfo,33> buffers{};
    std::array<VkWriteDescriptorSet,45> writes{};
    size_t writeCount=0;
    auto pushImage=[&](uint32_t binding,VkDescriptorType type,
                       VkDescriptorImageInfo* image) {
        auto& w=writes[writeCount++];
        w=vkStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        w.dstSet=set;w.dstBinding=binding;w.descriptorType=type;
        w.descriptorCount=1;w.pImageInfo=image;
    };
    auto pushBuffer=[&](uint32_t binding,VkDescriptorType type,
                        VkDescriptorBufferInfo* buffer) {
        auto& w=writes[writeCount++];
        w=vkStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
        w.dstSet=set;w.dstBinding=binding;w.descriptorType=type;
        w.descriptorCount=1;w.pBufferInfo=buffer;
    };

    for(size_t slot=0;slot<computeStorage_.size();++slot) {
        const auto handle=computeStorage_[slot];
        if (!handle.valid()) continue;
        const VTexture* t=textures_.get(handle.id);
        if(!t || !t->storage || !t->view) {
            error_="Vulkan: storage image was deleted before compute submission";
            vkDestroyDescriptorPool(device_,pool,nullptr);return false;
        }
        images[slot]={VK_NULL_HANDLE,t->view,VK_IMAGE_LAYOUT_GENERAL};
        pushImage(uint32_t(slot),VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,&images[slot]);
    }
    // Mixed descriptor sets reserve all eight samplers, even for buffer-only shaders.
    // Populate unused entries with the device-owned texture, as for graphics draws.
    const size_t sampledCount = (compute->bufferPushConstants || compute->storageUniforms) ? 0 : computeSampled_.size();
    for(size_t slot=0;slot<sampledCount;++slot) {
        const auto handle=computeSampled_[slot];
        const VTexture* t=handle.valid() ? textures_.get(handle.id) : &dummy_;
        if(!t || !t->view || !t->sampler) {
            error_="Vulkan: sampled image was deleted before compute submission";
            vkDestroyDescriptorPool(device_,pool,nullptr);return false;
        }
        images[4u+slot]={t->sampler,t->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        pushImage(uint32_t(4u+slot),VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                  &images[4u+slot]);
    }
    for(size_t slot=0;slot<compute->bufferBindings;++slot) {
        if(compute->storageUniforms&&slot==8)continue;
        const auto h=computeBuffers_[slot];
        if(!h.valid())continue;
        VBuffer* b=buffers_.get(h.id);
        if (!b || (b->type!=BufferType::Storage &&
            !((compute->bufferPushConstants||compute->storageUniforms) && b->type==BufferType::Indirect)) || !b->size) {
            error_="Vulkan: storage buffer was deleted before compute submission";
            vkDestroyDescriptorPool(device_,pool,nullptr);return false;
        }
        VkBuffer native=resolveBuffer(h.id,serial_);
        if(!native) {
            error_="Vulkan: compute storage buffer version missing";
            vkDestroyDescriptorPool(device_,pool,nullptr);return false;
        }
        buffers[slot]={native,0,VkDeviceSize(b->size)};
        pushBuffer((compute->bufferPushConstants||compute->storageUniforms) ? uint32_t(slot) : uint32_t(12u+slot),VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                   &buffers[slot]);
    }

    VkBuffer uniformBuffer=VK_NULL_HANDLE;
    Allocation uniformAlloc{};
    if(!compute->bufferPushConstants && !computeUniforms_.empty()) {
        const size_t bytes=computeUniforms_.size();
        if (!createBufferObject(bytes,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,true,
                                uniformBuffer,uniformAlloc) || !uniformAlloc.mapped) {
            error_="Vulkan: compute uniform staging allocation failed";
            if(uniformBuffer)vkDestroyBuffer(device_,uniformBuffer,nullptr);
            memory_.release(uniformAlloc);
            vkDestroyDescriptorPool(device_,pool,nullptr);return false;
        }
        std::memcpy(uniformAlloc.mapped,computeUniforms_.data(),bytes);
        buffers[32]={uniformBuffer,0,VkDeviceSize(bytes)};
        pushBuffer(compute->storageUniforms?8u:16u,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,&buffers[32]);
    }
    vkUpdateDescriptorSets(device_,uint32_t(writeCount),writes.data(),0,nullptr);
    VkCommandBuffer command=slots_[size_t(slot_)].main;
    for(const auto handle:computeStorage_) {
        if(!handle.valid())continue;
        const VTexture* t=textures_.get(handle.id);
        barrierImage(command,t->image,false,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_IMAGE_LAYOUT_GENERAL,
                     VK_REMAINING_MIP_LEVELS,0,uint32_t(t->layers));
    }
    // Staging uploads of immutable metadata/instances in this frame must
    // happen-before compute reads; do not assume a transfer->vertex barrier
    // covers shader storage usage.
    auto before=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    before.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT |
                         VK_ACCESS_SHADER_WRITE_BIT;
    before.dstAccessMask=VK_ACCESS_SHADER_READ_BIT |
                         VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_INDIRECT_COMMAND_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT |
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT | VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT,
                         0,1,&before,0,nullptr,0,nullptr);
    vkCmdBindPipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE,compute->pipeline);
    vkCmdBindDescriptorSets(command,VK_PIPELINE_BIND_POINT_COMPUTE,
                            compute->layout,0,1,&set,0,nullptr);
    if (compute->bufferPushConstants)
        vkCmdPushConstants(command,compute->layout,VK_SHADER_STAGE_COMPUTE_BIT,
            0,uint32_t(computeUniforms_.size()),computeUniforms_.data());
    if(indirectBuffer)vkCmdDispatchIndirect(command,indirectBuffer,offset);
    else vkCmdDispatch(command,uint32_t((uint64_t(x)+63u)/64u),y,1);
    for(const auto handle:computeStorage_) {
        if(!handle.valid())continue;
        const VTexture* t=textures_.get(handle.id);
        barrierImage(command,t->image,false,VK_IMAGE_LAYOUT_GENERAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                     VK_REMAINING_MIP_LEVELS,0,uint32_t(t->layers));
    }
    // GPU-written culling results / indexed indirect arguments must be
    // visible to subsequent transfer diagnostics or actual draw commands.
    auto after=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    after.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
    after.dstAccessMask=VK_ACCESS_SHADER_READ_BIT |
                        VK_ACCESS_TRANSFER_READ_BIT |
                        VK_ACCESS_INDIRECT_COMMAND_READ_BIT |
                        VK_ACCESS_INDEX_READ_BIT |
                        VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
        VK_PIPELINE_STAGE_TRANSFER_BIT |
        VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT |
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
        0,1,&after,0,nullptr,0,nullptr);
    deferDestroy([this,pool,uniformBuffer,uniformAlloc]() mutable {
        vkDestroyDescriptorPool(device_,pool,nullptr);
        if (uniformBuffer) {
            vkDestroyBuffer(device_,uniformBuffer,nullptr);
            memory_.release(uniformAlloc);
        }
    });
    error_.clear();
    return true;
}

void VulkanDevice::endCompute() {
    activeComputeId_=0;
    computeStateValid_=false;
    computeStorage_.fill({});
    computeSampled_.fill({});
    computeBuffers_.fill({});
    computeUniforms_.clear();
}

bool VulkanDevice::readBufferAsync(BufferHandle handle,size_t size,BufferReadFn callback){
    return readBufferRangeAsync(handle,0,size,std::move(callback));
}
bool VulkanDevice::readBufferRangeAsync(BufferHandle handle,size_t offset,size_t size,BufferReadFn callback) {
    if(!healthy_ || !recording_ || inPass_ || activeComputeId_ ||
       !callback || !handle.valid() || !size || (offset&3u) || (size&3u) || size>(64u*1024u) ||
       computeReads_.size()>=64)
        return false;
    const auto* buffer=buffers_.get(handle.id);
    if(!buffer || offset>buffer->size||size>buffer->size-offset||((offset|size)&3u))return false;
    GpuTransferReservation charge(transientGpuBudget(),size);if(!charge)return false;
    const VkBuffer source=resolveBuffer(handle.id,serial_);
    if(!source)return false;
    PendingComputeRead pending;
    if(!createBufferObject(size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                           true,pending.buffer,pending.allocation))
        return false;
    pending.serial=serial_;
    pending.bytes=size;
    pending.callback=std::move(callback);
    VkCommandBuffer cb=slots_[size_t(slot_)].main;
    auto before=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    before.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT |
                         VK_ACCESS_TRANSFER_WRITE_BIT;
    before.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT |
                         VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_TRANSFER_BIT,0,
                         1,&before,0,nullptr,0,nullptr);
    VkBufferCopy region{VkDeviceSize(offset),0,VkDeviceSize(size)};
    vkCmdCopyBuffer(cb,source,pending.buffer,1,&region);
    auto after=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    after.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    after.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,
                         VK_PIPELINE_STAGE_HOST_BIT,0,
                         1,&after,0,nullptr,0,nullptr);
    computeReads_.push_back(std::move(pending));
    charge.submitted(gpuSubmissionFence());return true;
}

bool VulkanDevice::readBuffer(BufferHandle handle,void* data,size_t size){
    if(!healthy_||recording_||inPass_||activeComputeId_||!data||!size||(size&3u)||
       slots_[size_t(slot_)].preRecording)return false;
    const auto* src=buffers_.get(handle.id);
    if(!src||size>src->size||src->versions.empty())return false;
    GpuTransferReservation charge(transientGpuBudget(),size);if(!charge)return false;
    VkBuffer dst=VK_NULL_HANDLE;Allocation alloc{};
    if(!createBufferObject(size,VK_BUFFER_USAGE_TRANSFER_DST_BIT,true,dst,alloc))return false;
    VkCommandPool pool=VK_NULL_HANDLE;
    auto cleanup=[&](){if(pool)vkDestroyCommandPool(device_,pool,nullptr);vkDestroyBuffer(device_,dst,nullptr);memory_.release(alloc);};
    auto pi=vkStruct<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);
    pi.queueFamilyIndex=queueFamily_;pi.flags=VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    if(vkCreateCommandPool(device_,&pi,nullptr,&pool)!=VK_SUCCESS){cleanup();return false;}
    VkCommandBuffer cb=VK_NULL_HANDLE;
    auto ai=vkStruct<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);
    ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    if(vkAllocateCommandBuffers(device_,&ai,&cb)!=VK_SUCCESS){cleanup();return false;}
    auto bi=vkStruct<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);
    bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if(vkBeginCommandBuffer(cb,&bi)!=VK_SUCCESS){cleanup();return false;}
    auto before=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    before.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;before.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&before,0,nullptr,0,nullptr);
    VkBufferCopy copy{0,0,size};
    vkCmdCopyBuffer(cb,src->versions[size_t(src->current)].buffer,dst,1,&copy);
    auto after=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
    after.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;after.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&after,0,nullptr,0,nullptr);
    if(vkEndCommandBuffer(cb)!=VK_SUCCESS){cleanup();return false;}
    auto si=vkStruct<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);si.commandBufferCount=1;si.pCommandBuffers=&cb;
    bool ok=vkQueueSubmit(queue_,1,&si,VK_NULL_HANDLE)==VK_SUCCESS;
    if(ok)ok=vkQueueWaitIdle(queue_)==VK_SUCCESS;
    if(ok&&alloc.mapped)std::memcpy(data,alloc.mapped,size);else{healthy_=false;ok=false;}
    cleanup();return ok;
}

void VulkanDevice::deliverComputeReadbacks() {
    size_t kept=0;
    std::vector<PendingComputeRead> finished;
    for(size_t i=0;i<computeReads_.size();++i){
        if(computeReads_[i].serial<=completed_)
            finished.push_back(std::move(computeReads_[i]));
        else {
            if (kept!=i) computeReads_[kept]=std::move(computeReads_[i]);
            ++kept;
        }
    }
    computeReads_.resize(kept);
    for(auto& pending:finished){
        std::vector<uint8_t> bytes(pending.bytes);
        if(healthy_ && pending.allocation.mapped)
            std::memcpy(bytes.data(),pending.allocation.mapped,pending.bytes);
        else bytes.clear();
        vkDestroyBuffer(device_,pending.buffer,nullptr);
        memory_.release(pending.allocation);
        if(pending.callback)pending.callback(std::move(bytes));
    }
}

void VulkanDevice::shutdownCompute() {
    for (auto& pending:computeReads_) {
        if (pending.buffer) vkDestroyBuffer(device_,pending.buffer,nullptr);
        memory_.release(pending.allocation);
    }
    computeReads_.clear();
    endCompute();
}

} // namespace aiverse::gfx::vk
