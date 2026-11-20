// Vulkan backend: memory, buffers, textures, uploads.
#include "VkDevice.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace aiverse::gfx::vk {

// ---------- memory ----------
namespace { constexpr VkDeviceSize kBlockSize = VkDeviceSize(64) << 20; }

void MemoryAllocator::init(VkPhysicalDevice gpu, VkDevice device) {
    device_ = device;
    vkGetPhysicalDeviceMemoryProperties(gpu, &props_);
}

void MemoryAllocator::shutdown() {
    for (auto& b : blocks_) if (b.memory) vkFreeMemory(device_, b.memory, nullptr);
    blocks_.clear();
}

int MemoryAllocator::findType(uint32_t bits, VkMemoryPropertyFlags want) const {
    for (uint32_t i = 0; i < props_.memoryTypeCount; ++i)
        if ((bits & (1u << i)) && (props_.memoryTypes[i].propertyFlags & want) == want) return int(i);
    return -1;
}

bool MemoryAllocator::allocate(const VkMemoryRequirements& req, VkMemoryPropertyFlags want, bool linear, Allocation& out) {
    int type = findType(req.memoryTypeBits, want);
    if (type < 0 && (want & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)) type = findType(req.memoryTypeBits, want & ~VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    if (type < 0) return false;
    const bool host = props_.memoryTypes[type].propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    auto mapBlock = [&](VkDeviceMemory m, VkDeviceSize size) -> void* {
        void* p = nullptr;
        if (host && vkMapMemory(device_, m, 0, size, 0, &p) != VK_SUCCESS) p = nullptr;
        return p;
    };
    if (req.size > kBlockSize / 2) {   // dedicated
        auto ai = vkStruct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        ai.allocationSize = req.size;
        ai.memoryTypeIndex = uint32_t(type);
        if (vkAllocateMemory(device_, &ai, nullptr, &out.memory) != VK_SUCCESS) return false;
        out.offset = 0; out.size = req.size; out.block = -1;
        out.mapped = mapBlock(out.memory, req.size);
        return true;
    }
    for (int pass = 0; pass < 2; ++pass) {
        for (size_t bi = 0; bi < blocks_.size(); ++bi) {
            Block& b = blocks_[bi];
            if (!b.memory || b.type != uint32_t(type) || b.linear != linear) continue;
            for (size_t i = 0; i < b.free.size(); ++i) {
                Range& r = b.free[i];
                const VkDeviceSize start = (r.offset + req.alignment - 1) / req.alignment * req.alignment;
                if (start + req.size > r.offset + r.size) continue;
                const Range before{r.offset, start - r.offset}, after{start + req.size, r.offset + r.size - start - req.size};
                b.free.erase(b.free.begin() + long(i));
                if (after.size) b.free.insert(b.free.begin() + long(i), after);
                if (before.size) b.free.insert(b.free.begin() + long(i), before);
                out.memory = b.memory; out.offset = start; out.size = req.size; out.block = int(bi);
                out.mapped = b.mapped ? static_cast<uint8_t*>(b.mapped) + start : nullptr;
                return true;
            }
        }
        if (pass == 1) break;
        Block nb;
        auto ai = vkStruct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
        ai.allocationSize = kBlockSize;
        ai.memoryTypeIndex = uint32_t(type);
        if (vkAllocateMemory(device_, &ai, nullptr, &nb.memory) != VK_SUCCESS) return false;
        nb.size = kBlockSize; nb.type = uint32_t(type); nb.linear = linear;
        nb.mapped = mapBlock(nb.memory, kBlockSize);
        nb.free.push_back({0, kBlockSize});
        auto slot = std::find_if(blocks_.begin(), blocks_.end(), [](const Block& b) { return !b.memory; });
        if (slot != blocks_.end()) *slot = std::move(nb); else blocks_.push_back(std::move(nb));
    }
    return false;
}

void MemoryAllocator::release(Allocation& a) {
    if (!a.memory) return;
    if (a.block < 0) { vkFreeMemory(device_, a.memory, nullptr); a = {}; return; }
    Block& b = blocks_[size_t(a.block)];
    Range r{a.offset, a.size};
    auto it = std::lower_bound(b.free.begin(), b.free.end(), r, [](const Range& x, const Range& y) { return x.offset < y.offset; });
    it = b.free.insert(it, r);
    if (it + 1 != b.free.end() && it->offset + it->size == (it + 1)->offset) { it->size += (it + 1)->size; b.free.erase(it + 1); }
    if (it != b.free.begin() && (it - 1)->offset + (it - 1)->size == it->offset) { (it - 1)->size += it->size; b.free.erase(it); }
    if (b.free.size() == 1 && b.free[0].size == b.size) { vkFreeMemory(device_, b.memory, nullptr); b = Block{}; }   // empty block
    a = {};
}

// ---------- helpers ----------
VkFormat VulkanDevice::vkFormat(PixelFormat f) const {
    switch (f) {
        case PixelFormat::RGBA8: return VK_FORMAT_R8G8B8A8_UNORM;
        case PixelFormat::R8: return VK_FORMAT_R8_UNORM;
        case PixelFormat::Depth: return depthFormat_;
        case PixelFormat::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case PixelFormat::ETC2_RGB8: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        case PixelFormat::ETC2_RGBA8: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
        case PixelFormat::BC1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case PixelFormat::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
        case PixelFormat::BC7: return VK_FORMAT_BC7_UNORM_BLOCK;
        case PixelFormat::ASTC_4x4: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
        case PixelFormat::ASTC_6x6: return VK_FORMAT_ASTC_6x6_UNORM_BLOCK;
        case PixelFormat::RGBA8_sRGB: return VK_FORMAT_R8G8B8A8_SRGB;
        case PixelFormat::RGBA16F: return VK_FORMAT_R16G16B16A16_SFLOAT;
    }
    return VK_FORMAT_UNDEFINED;
}

bool VulkanDevice::supports(PixelFormat f) const {
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(gpu_, vkFormat(f), &fp);
    // supports() indicates that the format is sampleable. An RGBA16F GI
    // storage image need not be a color render target on every Vulkan GPU.
    // createTexture() separately validates COLOR_ATTACHMENT for that usage.
    return (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT)!=0;
}

void VulkanDevice::barrierImage(VkCommandBuffer cb, VkImage image, bool depth, VkImageLayout from, VkImageLayout to, uint32_t levels, uint32_t baseLevel) {
    auto b = vkStruct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), baseLevel, levels, 0, 1};
    // Conservative full barriers: correctness first; the RHI's passes are coarse anyway.
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0, nullptr, 1, &b);
}

// ---------- buffers ----------
bool VulkanDevice::createBufferObject(size_t size, VkBufferUsageFlags usage, bool host, VkBuffer& buf, Allocation& alloc) {
    auto ci = vkStruct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    ci.size = std::max<size_t>(size, 4);
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device_, &ci, nullptr, &buf) != VK_SUCCESS) { error_ = "vkCreateBuffer failed"; return false; }
    VkMemoryRequirements req;
    vkGetBufferMemoryRequirements(device_, buf, &req);
    const VkMemoryPropertyFlags want = host ? VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT : VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
    if (!memory_.allocate(req, want, true, alloc)) { vkDestroyBuffer(device_, buf, nullptr); buf = VK_NULL_HANDLE; error_ = "out of GPU memory"; return false; }
    // A successfully created VkBuffer is not usable unless its memory bind
    // succeeds. Model uploaders must never observe an apparently valid handle
    // whose storage is unbound.
    const VkResult bound = vkBindBufferMemory(device_, buf, alloc.memory, alloc.offset);
    if (bound != VK_SUCCESS) {
        vkDestroyBuffer(device_, buf, nullptr);
        buf = VK_NULL_HANDLE;
        memory_.release(alloc);
        error_ = "vkBindBufferMemory failed";
        return false;
    }
    return true;
}

VkBufferUsageFlags VulkanDevice::bufferUsage(BufferType t) const {
    VkBufferUsageFlags u = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    if (t == BufferType::Index) u |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    else u |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    if (t == BufferType::Storage) u |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (t == BufferType::Indirect) u |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    return u;
}

void* VulkanDevice::stagingWrite(size_t size, VkBuffer& buf, VkDeviceSize& offset) {
    size = (size + 15) & ~size_t(15);
    for (auto& c : staging_) {
        if (c.serial != serial_ && c.used == 0) c.serial = serial_;
        if (c.serial == serial_ && c.used + size <= c.size) {
            buf = c.buffer; offset = c.used; c.used += size;
            return static_cast<uint8_t*>(c.alloc.mapped) + offset;
        }
    }
    StagingChunk c;
    c.size = std::max<size_t>(size, size_t(8) << 20);
    if (!createBufferObject(c.size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, true, c.buffer, c.alloc)) return nullptr;
    c.serial = serial_;
    c.used = size;
    buf = c.buffer; offset = 0;
    staging_.push_back(c);
    return c.alloc.mapped;
}

BufferHandle VulkanDevice::createBuffer(const BufferDesc& d) {
    VBuffer b;
    b.type = d.type; b.usage = d.usage; b.size = std::max<size_t>(d.size, 4);
    b.host = d.usage == Usage::Dynamic;
    b.discardOnUpdate = d.discardOnUpdate;
    if (b.type == BufferType::Indirect && b.host) {
        b.transferStats.available = true;
        b.transferStats.allocations = 1;
    }
    VBuffer::Version v;
    if (!createBufferObject(b.size, bufferUsage(d.type), b.host, v.buffer, v.alloc)) return {};
    if (b.host) {
        b.shadow.assign(b.size, 0);
        if (d.data) {
            std::memcpy(b.shadow.data(), d.data, d.size);
            if (b.transferStats.available) b.transferStats.cpuShadowWriteBytes += d.size;
        }
        std::memcpy(v.alloc.mapped, b.shadow.data(), b.size);
        if (b.transferStats.available) b.transferStats.mappedWriteBytes += b.size;
    } else if (d.data) {
        VkBuffer src; VkDeviceSize off;
        void* staged = stagingWrite(d.size, src, off);
        if (!staged) {
            // Incomplete immutable uploads previously returned a valid RHI
            // handle with uninitialized contents. This is especially unsafe
            // for the fallback VB/IB pair after pooled uploads are rejected.
            vkDestroyBuffer(device_, v.buffer, nullptr);
            memory_.release(v.alloc);
            error_ = "immutable buffer staging allocation failed";
            return {};
        }
        std::memcpy(staged, d.data, d.size);
        VkBufferCopy c{off, 0, d.size};
        VkCommandBuffer cb=transferCmd();
        vkCmdCopyBuffer(cb, src, v.buffer, 1, &c);
        // Storage sources may be consumed by a compute dispatch in the
        // same frame. Transfer-order alone is not shader-read visibility.
        auto mb=vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
        mb.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
        mb.dstAccessMask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                         VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
        VkPipelineStageFlags dst=VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                                 VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                 VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        if(computeAvailable_)dst|=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,dst,0,
                             1,&mb,0,nullptr,0,nullptr);
    }
    b.versions.push_back(v);
    return {buffers_.add(std::move(b))};
}

VBuffer::Version* VulkanDevice::writableVersion(VBuffer& b, bool keepContents) {
    VBuffer::Version& cur = b.versions[size_t(b.current)];
    if (cur.usedSerial < serial_ && cur.usedSerial <= completed_) return &cur; // completed version
    bool foundReusable = false;
    for (size_t i = 0; i < b.versions.size(); ++i) {
        const auto& v = b.versions[i];
        if (int(i) != b.current && v.usedSerial <= completed_) {
            b.current = int(i);
            foundReusable = true;
            break;
        }
    }
    if (!foundReusable) {
        VBuffer::Version v;
        // An allocation failure must never fall back to the in-flight
        // current version. Keep the previous serial/handle untouched.
        if (!createBufferObject(b.size, bufferUsage(b.type), true, v.buffer, v.alloc))
            return nullptr;
        b.versions.push_back(v);
        b.current = int(b.versions.size() - 1);
    }
    VBuffer::Version& next = b.versions[size_t(b.current)];
    if (keepContents) {
        std::memcpy(next.alloc.mapped, b.shadow.data(), b.size);
        if (b.transferStats.available) {
            b.transferStats.mappedWriteBytes += b.size;
            b.transferStats.preservedBytes += b.size;
        }
    }
    return &next;
}

void VulkanDevice::updateBuffer(BufferHandle h, const void* data,
                                size_t size, size_t offset) {
    (void)updateBufferChecked(h, data, size, offset);
}

bool VulkanDevice::updateSharedBufferChecked(BufferHandle h, const void* data,
                                             size_t size, size_t offset) {
    const VBuffer* b = buffers_.get(h.id);
    if (!b || b->type != BufferType::Storage) return false;
    return updateBufferChecked(h, data, size, offset);
}

bool VulkanDevice::updateBufferChecked(BufferHandle h, const void* data,
                                       size_t size, size_t offset) {
    VBuffer* b = buffers_.get(h.id);
    if (!b || !data || size == 0 || offset > SIZE_MAX - size) return false;
    if (!b->host) {
        if (offset + size > b->size) {   // immutable->dynamic growth
            const size_t capacity = (offset + size) * 3 / 2;
            VBuffer::Version v;
            if (!createBufferObject(capacity, bufferUsage(b->type), true, v.buffer, v.alloc))
                return false; // preserve existing immutable version on failure
            for (auto& old : b->versions) {
                VkBuffer buf = old.buffer; Allocation a = old.alloc;
                deferDestroy([this, buf, a]() mutable {
                    vkDestroyBuffer(device_, buf, nullptr); memory_.release(a);
                });
            }
            b->versions.clear();
            b->host = true;
            b->size = capacity;
            b->shadow.assign(capacity, 0);
            b->versions.push_back(v);
            b->current = 0;
        } else {   // immutable buffer: staged copy in queue order
            VkBuffer src; VkDeviceSize off;
            void* p = stagingWrite(size, src, off);
            if (!p) return false;
            std::memcpy(p, data, size);
            VkCommandBuffer cb = transferCmd();
            VkBufferCopy c{off, offset, size};
            vkCmdCopyBuffer(cb, src, b->versions[0].buffer, 1, &c);
            auto mb = vkStruct<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);
            mb.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
            mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT |
                               VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
            VkPipelineStageFlags stages=VK_PIPELINE_STAGE_VERTEX_INPUT_BIT |
                                        VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                                        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
            if(computeAvailable_)stages|=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
            vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,stages,0,
                                 1,&mb,0,nullptr,0,nullptr);
            return true;
        }
    }
    if (offset + size > b->size) {   // grow: old GPU versions may still be in flight
        const size_t capacity = (offset + size) * 3 / 2;
        VBuffer::Version v;
        // Stage the replacement before touching the old versions or shadow:
        // failed growth preserves the prior drawable buffer and CPU mirror.
        if (!createBufferObject(capacity, bufferUsage(b->type), true, v.buffer, v.alloc))
            return false;
        b->shadow.resize(capacity, 0);
        std::memcpy(b->shadow.data() + offset, data, size);
        const bool discardPrefix = b->discardOnUpdate && offset == 0;
        if (discardPrefix)
            std::memcpy(v.alloc.mapped, data, size);
        else
            std::memcpy(v.alloc.mapped, b->shadow.data(), capacity);
        for (auto& old : b->versions) {
            VkBuffer buf = old.buffer; Allocation a = old.alloc;
            deferDestroy([this, buf, a]() mutable {
                vkDestroyBuffer(device_, buf, nullptr); memory_.release(a);
            });
        }
        b->versions.clear();
        b->versions.push_back(v);
        b->current = 0;
        b->size = capacity;
        if (b->transferStats.available) {
            ++b->transferStats.updateCalls;
            b->transferStats.requestedBytes += size;
            b->transferStats.cpuShadowWriteBytes += size;
            b->transferStats.mappedWriteBytes += discardPrefix ? size : capacity;
            ++b->transferStats.allocations;
        }
        return true;
    }
    // Select or allocate a GPU-fence-safe destination FIRST. Do not
    // mutate the shadow or a version that is still in flight when a new
    // version allocation fails. Preserved bytes use the previous shadow.
    const bool whole = offset == 0 &&
        (b->discardOnUpdate || size >= b->size);
    const size_t versionCountBefore = b->versions.size();
    VBuffer::Version* v = writableVersion(*b, !whole);
    if (!v) return false;
    std::memcpy(b->shadow.data() + offset, data, size);
    if (b->transferStats.available) b->transferStats.cpuShadowWriteBytes += size;
    std::memcpy(static_cast<uint8_t*>(v->alloc.mapped) + offset, data, size);
    if (b->transferStats.available) {
        ++b->transferStats.updateCalls;
        b->transferStats.requestedBytes += size;
        b->transferStats.mappedWriteBytes += size;
        if (b->versions.size() > versionCountBefore) ++b->transferStats.allocations;
        else ++b->transferStats.reused;
    }
    return true;
}

VkBuffer VulkanDevice::resolveBuffer(uint32_t id, uint64_t serialUse) {
    VBuffer* b = buffers_.get(id);
    if (!b || b->versions.empty()) return VK_NULL_HANDLE;
    auto& v = b->versions[size_t(b->current)];
    v.usedSerial = serialUse;
    return v.buffer;
}

BufferUpdateStats VulkanDevice::bufferUpdateStats(BufferHandle h) {
    auto* b = buffers_.get(h.id);
    if (!b || !b->transferStats.available) return {};
    auto stats = b->transferStats;
    // Allocation::size is the Vulkan suballocation span, not a claim of
    // unique physical VRAM usage or a driver-specific residency metric.
    for (const auto& v : b->versions)
        stats.retainedBufferBytes += size_t(v.alloc.size);
    stats.retainedCpuShadowBytes = b->shadow.capacity();
    return stats;
}

void VulkanDevice::destroyBuffer(BufferHandle h) {
    VBuffer* b = buffers_.get(h.id);
    if (!b) return;
    std::vector<VBuffer::Version> vs = std::move(b->versions);
    deferDestroy([this, vs]() mutable { for (auto& v : vs) { vkDestroyBuffer(device_, v.buffer, nullptr); memory_.release(v.alloc); } });
    buffers_.remove(h.id);
}

// ---------- textures ----------
namespace {
size_t texelBytes(PixelFormat f) {
    switch (f) {
        case PixelFormat::R8: return 1;
        case PixelFormat::RGBA16F: return 8;
        case PixelFormat::RGBA32F: return 16;
        default: return 4;
    }
}
bool isCompressed(PixelFormat f) {
    return f == PixelFormat::ETC2_RGB8 || f == PixelFormat::ETC2_RGBA8 || f == PixelFormat::BC1 || f == PixelFormat::BC3 || f == PixelFormat::BC7 ||
           f == PixelFormat::ASTC_4x4 || f == PixelFormat::ASTC_6x6;
}
} // namespace

bool VulkanDevice::createImage(VTexture& t, VkImageUsageFlags usage) {
    auto ci = vkStruct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ci.imageType = VK_IMAGE_TYPE_2D;
    ci.format = t.vkFormat;
    ci.extent = {uint32_t(std::max(1, t.width)), uint32_t(std::max(1, t.height)), 1};
    ci.mipLevels = uint32_t(t.levels);
    ci.arrayLayers = 1;
    ci.samples = VK_SAMPLE_COUNT_1_BIT;
    ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    ci.usage = usage;
    ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (vkCreateImage(device_, &ci, nullptr, &t.image) != VK_SUCCESS) { error_ = "vkCreateImage failed"; t.image = VK_NULL_HANDLE; return false; }
    VkMemoryRequirements req;
    vkGetImageMemoryRequirements(device_, t.image, &req);
    if (!memory_.allocate(req, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, false, t.alloc)) {
        vkDestroyImage(device_, t.image, nullptr); t.image = VK_NULL_HANDLE; error_ = "out of GPU memory"; return false;
    }
    vkBindImageMemory(device_, t.image, t.alloc.memory, t.alloc.offset);
    auto vi = vkStruct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    vi.image = t.image;
    vi.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vi.format = t.vkFormat;
    vi.subresourceRange = {VkImageAspectFlags(t.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), 0, uint32_t(t.levels), 0, 1};
    if (t.format == PixelFormat::R8) vi.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE};
    if (t.format == PixelFormat::ETC2_RGB8 || t.format == PixelFormat::BC1) vi.components.a = VK_COMPONENT_SWIZZLE_ONE;
    vkCreateImageView(device_, &vi, nullptr, &t.view);
    return true;
}

// Every image rests in SHADER_READ_ONLY_OPTIMAL between operations; passes and copies return it there.
void VulkanDevice::initImageLayout(VTexture& t, VkCommandBuffer cb, bool clear) {
    if (!clear || t.compressed) { barrierImage(cb, t.image, t.depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL); return; }
    barrierImage(cb, t.image, t.depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    if (t.depth) {
        VkClearDepthStencilValue v{1.0f, 0};
        VkImageSubresourceRange r{VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
        vkCmdClearDepthStencilImage(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &r);
    } else {
        VkClearColorValue v{};
        VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, 1};
        vkCmdClearColorImage(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &r);
    }
    barrierImage(cb, t.image, t.depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

VkSampler VulkanDevice::samplerFor(const TextureDesc& d, const VTexture& t) {
    VkFormatProperties fp;
    vkGetPhysicalDeviceFormatProperties(gpu_, t.vkFormat, &fp);
    const bool filterable = fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    const bool linear = d.filter == Filter::Linear && d.format != PixelFormat::RGBA32F && filterable;   // float textures: nearest (as GL ES)
    const bool mips = t.levels > 1;
    const uint32_t key = (linear ? 1u : 0u) | (d.repeat ? 2u : 0u) | (d.compare ? 4u : 0u) | (mips ? 8u : 0u) | (uint32_t(t.levels) << 8);
    for (const auto& s : samplers_) if (s.key == key) return s.sampler;
    auto ci = vkStruct<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    ci.magFilter = ci.minFilter = linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.mipmapMode = linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = ci.addressModeV = ci.addressModeW = d.repeat ? VK_SAMPLER_ADDRESS_MODE_REPEAT : VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    ci.compareEnable = d.compare ? VK_TRUE : VK_FALSE;
    ci.compareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
    ci.minLod = 0;
    ci.maxLod = mips ? float(t.levels - 1) : 0.0f;
    ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VkSampler s = VK_NULL_HANDLE;
    vkCreateSampler(device_, &ci, nullptr, &s);
    samplers_.push_back({key, s});
    return s;
}

void VulkanDevice::uploadImage(VTexture& t, int level, int x, int y, int w, int h, const void* data, size_t bytes, VkCommandBuffer cb) {
    VkBuffer src; VkDeviceSize off;
    void* p = stagingWrite(bytes, src, off);
    if (!p) return;
    std::memcpy(p, data, bytes);
    VkBufferImageCopy c{};
    c.bufferOffset = off;
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(level), 0, 1};
    c.imageOffset = {x, y, 0};
    c.imageExtent = {uint32_t(w), uint32_t(h), 1};
    vkCmdCopyBufferToImage(cb, src, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
}

void VulkanDevice::generateMips(VTexture& t, VkCommandBuffer cb) {   // level 0 in TRANSFER_DST; all levels end in SHADER_READ_ONLY
    int w = t.width, h = t.height;
    for (int l = 1; l < t.levels; ++l) {
        barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, uint32_t(l - 1));
        VkImageBlit b{};
        b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(l - 1), 0, 1};
        b.srcOffsets[1] = {w, h, 1};
        w = std::max(1, w / 2); h = std::max(1, h / 2);
        b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(l), 0, 1};
        b.dstOffsets[1] = {w, h, 1};
        vkCmdBlitImage(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
        barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, uint32_t(l - 1));
    }
    barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, uint32_t(t.levels - 1));
}

TextureHandle VulkanDevice::createTexture(const TextureDesc& d) {
    VTexture t;
    t.format = d.format;
    t.vkFormat = vkFormat(d.format);
    t.width = std::max(1, d.width); t.height = std::max(1, d.height);
    t.depth = d.format == PixelFormat::Depth;
    t.renderTarget = d.renderTarget;
    t.storage = d.storage;
    t.compressed = isCompressed(d.format);
    if (!supports(d.format) && !t.depth) { error_ = "texture format unsupported"; return {}; }
    if (d.renderTarget && !t.depth) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(gpu_,t.vkFormat,&props);
        if(!(props.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)){
            error_="Vulkan: color render-target format unsupported";return {};
        }
    }
    if (d.storage) {
        VkFormatProperties props{};
        vkGetPhysicalDeviceFormatProperties(gpu_,t.vkFormat,&props);
        if(!computeAvailable_ || t.depth || t.compressed || d.mipmaps ||
           d.format!=PixelFormat::RGBA16F ||
           !(props.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) {
            error_="Vulkan: compute storage texture format unsupported";return {};
        }
    }
    if (t.compressed) t.levels = std::max(1, d.levelCount);
    else if (d.mipmaps && !t.depth && !d.renderTarget && d.format != PixelFormat::RGBA16F && d.format != PixelFormat::RGBA32F) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(gpu_, t.vkFormat, &fp);
        const auto need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((fp.optimalTilingFeatures & need) == need) {
            t.generateMips = true;
            for (int s = std::max(t.width, t.height); s > 1; s >>= 1) ++t.levels;
        }
    }
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (t.depth) usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    else if (d.renderTarget) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if(d.storage)usage|=VK_IMAGE_USAGE_STORAGE_BIT;
    if (!createImage(t, usage)) return {};
    t.sampler = samplerFor(d, t);
    VkCommandBuffer cb = transferCmd();
    if (t.compressed && d.levelData) {
        barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        for (int l = 0; l < t.levels; ++l)
            uploadImage(t, l, 0, 0, std::max(1, t.width >> l), std::max(1, t.height >> l), d.levelData[l], d.levelSize[l], cb);
        barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else if (d.data && !t.depth) {
        barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        uploadImage(t, 0, 0, 0, t.width, t.height, d.data, size_t(t.width) * size_t(t.height) * texelBytes(d.format), cb);
        if (t.generateMips) generateMips(t, cb);
        else barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    } else initImageLayout(t, cb, true);
    return {textures_.add(t)};
}

void VulkanDevice::updateTexture(TextureHandle h, int x, int y, int w, int hh, const void* pixels, int rowPitch) {
    VTexture* t = textures_.get(h.id);
    if (!t || t->compressed || t->depth || w <= 0 || hh <= 0) return;
    const size_t bpp = texelBytes(t->format), row = size_t(w) * bpp;
    std::vector<uint8_t> packed;
    const void* src = pixels;
    if (size_t(rowPitch) != row) {
        packed.resize(row * size_t(hh));
        for (int r = 0; r < hh; ++r) std::memcpy(packed.data() + size_t(r) * row, static_cast<const uint8_t*>(pixels) + size_t(r) * size_t(rowPitch), row);
        src = packed.data();
    }
    VkCommandBuffer cb = transferCmd();
    barrierImage(cb, t->image, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
    uploadImage(*t, 0, x, y, w, hh, src, row * size_t(hh), cb);
    if (t->generateMips) generateMips(*t, cb);
    else barrierImage(cb, t->image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void VulkanDevice::destroyTexture(TextureHandle h) {
    VTexture* t = textures_.get(h.id);
    if (!t) return;
    VTexture old = *t;
    deferDestroy([this, old]() mutable { vkDestroyImageView(device_, old.view, nullptr); vkDestroyImage(device_, old.image, nullptr); memory_.release(old.alloc); });
    for (size_t i = 0; i < framebuffers_.size();) {
        if (framebuffers_[i].color == h.id || framebuffers_[i].depth == h.id) {
            VkFramebuffer fb = framebuffers_[i].fb;
            deferDestroy([this, fb] { vkDestroyFramebuffer(device_, fb, nullptr); });
            framebuffers_.erase(framebuffers_.begin() + long(i));
        } else ++i;
    }
    for (auto& b : textures_bound_) if (b == h.id) b = 0;
    textures_.remove(h.id);
    descriptorsDirty_ = true;
}

} // namespace aiverse::gfx::vk
