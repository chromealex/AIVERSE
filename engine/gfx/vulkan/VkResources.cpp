// Vulkan backend: memory, buffers, textures, uploads.
#include "VkDevice.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace aiverse::gfx::vk {

bool VulkanDevice::supportsStorageTexture(PixelFormat f, TextureDimension dim) const {
    if (!supportsCompute() || (f!=PixelFormat::RGBA8 && f!=PixelFormat::RGBA16F) ||
        (dim!=TextureDimension::Texture2D && dim!=TextureDimension::Texture2DArray &&
         dim!=TextureDimension::Texture3D)) return false;
    VkFormatProperties properties{};
    const VkFormat format=vkFormat(f);
    if (format==VK_FORMAT_UNDEFINED) return false;
    vkGetPhysicalDeviceFormatProperties(gpu_,format,&properties);
    const VkFormatFeatureFlags required=VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT |
        VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    return (properties.optimalTilingFeatures & required)==required;
}

std::vector<TextureResourceInfo> VulkanDevice::textureResources() const {
    std::vector<TextureResourceInfo> result;
    textures_.eachWithId([&](uint32_t id, const VTexture& tex) {
        if (!tex.image) return;
        TextureResourceInfo item;
        item.handle = {id};
        item.dimension = tex.dimension;
        item.format = tex.format;
        item.width = tex.width;
        item.height = tex.height;
        item.depth = tex.volumeDepth;
        item.arrayLayers = tex.layers;
        item.mipLevels = tex.levels;
        item.sampleCount = tex.samples;
        item.renderTarget = tex.renderTarget || tex.depth;
        item.storage = tex.storage;
        TextureDesc desc;
        desc.width = tex.width; desc.height = tex.height;
        desc.depth = tex.volumeDepth; desc.arrayLayers = tex.layers;
        desc.dimension = tex.dimension; desc.format = tex.format;
        desc.mipLevels = tex.levels; desc.sampleCount = tex.samples;
        item.estimatedBytes = estimateTextureFootprint(desc);
        result.push_back(item);
    });
    return result;
}

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
        case PixelFormat::RG8: return VK_FORMAT_R8G8_UNORM;
        case PixelFormat::R16F: return VK_FORMAT_R16_SFLOAT;
        case PixelFormat::RG16F: return VK_FORMAT_R16G16_SFLOAT;
        case PixelFormat::R32F: return VK_FORMAT_R32_SFLOAT;
        case PixelFormat::RG32F: return VK_FORMAT_R32G32_SFLOAT;
        case PixelFormat::Depth: return depthFormat_;
        case PixelFormat::RGBA32F: return VK_FORMAT_R32G32B32A32_SFLOAT;
        case PixelFormat::ETC2_RGB8: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
        case PixelFormat::ETC2_RGBA8: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
        case PixelFormat::BC1: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
        case PixelFormat::BC3: return VK_FORMAT_BC3_UNORM_BLOCK;
        case PixelFormat::BC4: return VK_FORMAT_BC4_UNORM_BLOCK;
        case PixelFormat::BC5: return VK_FORMAT_BC5_UNORM_BLOCK;
        case PixelFormat::EAC_R11: return VK_FORMAT_EAC_R11_UNORM_BLOCK;
        case PixelFormat::BC7: return VK_FORMAT_BC7_UNORM_BLOCK;
        case PixelFormat::BC1_sRGB: return VK_FORMAT_BC1_RGBA_SRGB_BLOCK;
        case PixelFormat::BC3_sRGB: return VK_FORMAT_BC3_SRGB_BLOCK;
        case PixelFormat::BC7_sRGB: return VK_FORMAT_BC7_SRGB_BLOCK;
        case PixelFormat::ETC2_RGB8_sRGB: return VK_FORMAT_ETC2_R8G8B8_SRGB_BLOCK;
        case PixelFormat::ETC2_RGBA8_sRGB: return VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK;
        case PixelFormat::ASTC_4x4_sRGB: return VK_FORMAT_ASTC_4x4_SRGB_BLOCK;
        case PixelFormat::ASTC_6x6_sRGB: return VK_FORMAT_ASTC_6x6_SRGB_BLOCK;
        case PixelFormat::BC6H_UFloat: return VK_FORMAT_BC6H_UFLOAT_BLOCK;
        case PixelFormat::BC6H_SFloat: return VK_FORMAT_BC6H_SFLOAT_BLOCK;
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
    const VkFormatFeatureFlags need = f == PixelFormat::RGBA16F ? VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT | VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT
                                                                : VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
    return (fp.optimalTilingFeatures & need) == need;
}

void VulkanDevice::barrierImage(VkCommandBuffer cb, VkImage image, bool depth, VkImageLayout from, VkImageLayout to, uint32_t levels, uint32_t baseLevel, uint32_t arrayLayers) {
    auto b = vkStruct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    b.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT;
    b.dstAccessMask = VK_ACCESS_MEMORY_READ_BIT | VK_ACCESS_MEMORY_WRITE_BIT;
    b.oldLayout = from;
    b.newLayout = to;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = image;
    b.subresourceRange = {VkImageAspectFlags(depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT), baseLevel, levels, 0, arrayLayers};
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
    // Vulkan diagnostic async readback copies from any buffer as TRANSFER_SRC.
    // GPU-authored indirect commands must ALSO carry STORAGE usage to be
    // writable by compute. Legacy CPU-authored indirect buffers still work.
    VkBufferUsageFlags u = VK_BUFFER_USAGE_TRANSFER_DST_BIT |
                           VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    if (t == BufferType::Index) u |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
    if (t == BufferType::Vertex) u |= VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    // GlobalGeometryPool uses ONE Storage page as vertex AND index input.
    // Do not regress that existing pooled-geometry addressing contract.
    // Storage also carries INDIRECT usage: compute-authored draw arguments
    // may live in a Storage buffer (drawIndexedGpuIndirect, as on Metal).
    if (t == BufferType::Storage) u |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                      VK_BUFFER_USAGE_INDEX_BUFFER_BIT |
                                      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT |
                                      VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
    if (t == BufferType::Indirect) u |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT |
                                       VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
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
        vkCmdCopyBuffer(transferCmd(), src, v.buffer, 1, &c);
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
            mb.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT;
            vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 1, &mb, 0, nullptr, 0, nullptr);
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
        case PixelFormat::RG8: case PixelFormat::R16F: return 2;
        case PixelFormat::RG16F: case PixelFormat::R32F: return 4;
        case PixelFormat::RG32F: case PixelFormat::RGBA16F: return 8;
        case PixelFormat::RGBA32F: return 16;
        default: return 4;
    }
}
bool isCompressed(PixelFormat f) {
    return isCompressedTextureFormat(f);
}
} // namespace

bool VulkanDevice::createImage(VTexture& t, VkImageUsageFlags usage) {
    auto ci = vkStruct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    ci.imageType = t.dimension == TextureDimension::Texture1D ? VK_IMAGE_TYPE_1D :
                   t.dimension == TextureDimension::Texture3D ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
    ci.flags = (t.dimension == TextureDimension::Cube || t.dimension == TextureDimension::CubeArray) ?
               VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0;
    ci.format = t.vkFormat;
    ci.extent = {uint32_t(std::max(1, t.width)), uint32_t(std::max(1, t.height)), uint32_t(std::max(1, t.volumeDepth))};
    ci.mipLevels = uint32_t(t.levels);
    ci.arrayLayers = uint32_t(t.layers);
    ci.samples = VkSampleCountFlagBits(t.samples);
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
    vi.viewType = t.dimension == TextureDimension::Texture1D ? VK_IMAGE_VIEW_TYPE_1D :
                  t.dimension == TextureDimension::Texture3D ? VK_IMAGE_VIEW_TYPE_3D :
                  t.dimension == TextureDimension::Texture2DArray ? VK_IMAGE_VIEW_TYPE_2D_ARRAY :
                  t.dimension == TextureDimension::Cube ? VK_IMAGE_VIEW_TYPE_CUBE :
                  t.dimension == TextureDimension::CubeArray ? VK_IMAGE_VIEW_TYPE_CUBE_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    vi.format = t.vkFormat;
    vi.subresourceRange = {VkImageAspectFlags(t.depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT),
                           0, uint32_t(t.levels), 0, uint32_t(t.layers)};
    if (t.format == PixelFormat::R8) vi.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE};
    if (t.format == PixelFormat::ETC2_RGB8 || t.format == PixelFormat::BC1) vi.components.a = VK_COMPONENT_SWIZZLE_ONE;
    if (vkCreateImageView(device_, &vi, nullptr, &t.view) != VK_SUCCESS) {
        vkDestroyImage(device_, t.image, nullptr);
        memory_.release(t.alloc);
        t.image = VK_NULL_HANDLE;
        error_ = "vkCreateImageView failed";
        return false;
    }
    return true;
}

// Every image rests in SHADER_READ_ONLY_OPTIMAL between operations; passes and copies return it there.
void VulkanDevice::initImageLayout(VTexture& t, VkCommandBuffer cb, bool clear) {
    if (!clear || t.compressed) { barrierImage(cb, t.image, t.depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_REMAINING_MIP_LEVELS, 0, uint32_t(t.layers)); return; }
    barrierImage(cb, t.image, t.depth, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_REMAINING_MIP_LEVELS, 0, uint32_t(t.layers));
    if (t.depth) {
        VkClearDepthStencilValue v{1.0f, 0};
        VkImageSubresourceRange r{VK_IMAGE_ASPECT_DEPTH_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, uint32_t(t.layers)};
        vkCmdClearDepthStencilImage(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &r);
    } else {
        VkClearColorValue v{};
        VkImageSubresourceRange r{VK_IMAGE_ASPECT_COLOR_BIT, 0, VK_REMAINING_MIP_LEVELS, 0, uint32_t(t.layers)};
        vkCmdClearColorImage(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &v, 1, &r);
    }
    barrierImage(cb, t.image, t.depth, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_REMAINING_MIP_LEVELS, 0, uint32_t(t.layers));
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

bool VulkanDevice::uploadImage(VTexture& t, int level, int x, int y, int w, int h,
                               const void* data, size_t bytes, VkCommandBuffer cb,
                               int z, int depth, int baseArrayLayer, int layerCount) {
    VkBuffer src; VkDeviceSize off;
    void* p = stagingWrite(bytes, src, off);
    if (!p) {
        if (error_.empty()) error_ = "Vulkan texture staging allocation failed";
        return false;
    }
    std::memcpy(p, data, bytes);
    VkBufferImageCopy c{};
    c.bufferOffset = off;
    c.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(level),
                          uint32_t(baseArrayLayer), uint32_t(layerCount)};
    c.imageOffset = {x, y, z};
    c.imageExtent = {uint32_t(w), uint32_t(h), uint32_t(depth)};
    vkCmdCopyBufferToImage(cb, src, t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &c);
    return true;
}

void VulkanDevice::generateMips(VTexture& t, VkCommandBuffer cb) {
    // Level zero is TRANSFER_DST; each generated level finishes in SHADER_READ.
    int w = t.width, h = t.height, z = t.volumeDepth;
    for (int l = 1; l < t.levels; ++l) {
        barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 1, uint32_t(l - 1), uint32_t(t.layers));
        VkImageBlit b{};
        b.srcSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(l - 1), 0, uint32_t(t.layers)};
        b.srcOffsets[1] = {w, h, z};
        w = std::max(1, w / 2); h = std::max(1, h / 2); z = std::max(1, z / 2);
        b.dstSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(l), 0, uint32_t(t.layers)};
        b.dstOffsets[1] = {w, h, z};
        vkCmdBlitImage(cb, t.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                       t.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &b, VK_FILTER_LINEAR);
        barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, uint32_t(l - 1), uint32_t(t.layers));
    }
    barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1, uint32_t(t.levels - 1), uint32_t(t.layers));
}

TextureHandle VulkanDevice::createTexture(const TextureDesc& d) {
    if (const TextureDescError e = validateTextureDesc(d); e != TextureDescError::None) {
        error_ = textureDescErrorString(e); return {};
    }
    if (!supportsTextureDimension(d.dimension)) {
        error_ = "Vulkan: requested texture dimension unsupported by the physical device";
        return {};
    }
    if (d.renderTarget && isExtendedDataTextureFormat(d.format)) {
        error_ = "Vulkan: R/RG render targets require matching pipeline color formats";
        return {};
    }
    if (d.storage && (!supportsStorageTexture(d.format,d.dimension) ||
                      d.sampleCount!=1 || d.renderTarget || d.mipmaps ||
                      d.compare || d.mipLevels>1 || d.levelCount>1)) {
        error_ = "Vulkan: unsupported storage image format, dimensions or usage";
        return {};
    }
    if ((d.dimension == TextureDimension::Texture1D ||
         d.dimension == TextureDimension::Texture3D) &&
        (d.renderTarget || d.format == PixelFormat::Depth)) {
        error_ = "Vulkan: 1D/3D render attachments unsupported";
        return {};
    }
    VTexture t;
    t.format = d.format;
    t.samples = d.sampleCount;
    t.vkFormat = vkFormat(d.format);
    t.dimension = d.dimension;
    t.width = d.width;
    t.height = d.height;
    t.volumeDepth = d.dimension == TextureDimension::Texture3D ? d.depth : 1;
    t.layers = d.dimension == TextureDimension::Texture2DArray ||
               d.dimension == TextureDimension::Cube || d.dimension == TextureDimension::CubeArray ?
               d.arrayLayers : 1;
    t.depth = d.format == PixelFormat::Depth;
    t.renderTarget = d.renderTarget;
    t.storage = d.storage;
    t.compressed = isCompressed(d.format);
    if (!supports(d.format) && !t.depth) { error_ = "texture format unsupported"; return {}; }
    if (t.compressed && (d.dimension == TextureDimension::Texture1D ||
                         d.dimension == TextureDimension::Texture3D)) {
        error_ = "Vulkan: compressed 1D/3D textures are not supported by this upload path"; return {};
    }
    if (t.depth && (d.data || d.levelData)) {
        error_ = "Vulkan: depth images cannot be initialized by the color upload path"; return {};
    }
    if (t.samples > 1 &&
        (d.dimension != TextureDimension::Texture2D || !d.renderTarget ||
         (d.format != PixelFormat::Depth && d.format != PixelFormat::RGBA8 &&
          d.format != PixelFormat::RGBA8_sRGB &&
          d.format != PixelFormat::RGBA16F))) {
        error_ = "Vulkan: multisample images require supported 2D render-target formats";
        return {};
    }
    if (d.levelCount > 0 && (!d.levelData || !d.levelSize)) {
        error_ = "Vulkan: levelCount requires data and byte sizes for every mip"; return {};
    }
    for (int l = 0; l < d.levelCount; ++l) {
        const int w = std::max(1, d.width >> l), h = std::max(1, d.height >> l);
        const int z = d.dimension == TextureDimension::Texture3D ? std::max(1, d.depth >> l) : 1;
        const size_t blocks = size_t(compressedTextureBlockSize(d.format));
        const size_t bytesPerBlock = size_t(compressedTextureBlockBytes(d.format));
        const size_t expected = t.compressed ?
            size_t((w + int(blocks) - 1) / int(blocks)) * size_t((h + int(blocks) - 1) / int(blocks)) *
            bytesPerBlock * size_t(z) * size_t(t.layers) :
            size_t(w) * size_t(h) * size_t(z) * size_t(t.layers) * texelBytes(d.format);
        if (!d.levelData[l] || d.levelSize[l] < expected) {
            error_ = "Vulkan: truncated texture mip/array payload"; return {};
        }
    }
    if (t.compressed) t.levels = d.levelCount;
    else if (d.levelCount > 0 && d.levelData) t.levels = d.levelCount;
    else if (d.mipLevels > 0) t.levels = d.mipLevels;
    else if (d.mipmaps && !t.depth && !d.renderTarget &&
             d.format != PixelFormat::RGBA16F && d.format != PixelFormat::RGBA32F) {
        VkFormatProperties fp;
        vkGetPhysicalDeviceFormatProperties(gpu_, t.vkFormat, &fp);
        const auto need = VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT |
                          VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
        if ((fp.optimalTilingFeatures & need) == need) {
            t.generateMips = true;
            t.levels = maxTextureMipLevels(t.width, t.height, t.volumeDepth);
        }
    }
    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    if (t.depth) usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                         VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    else if (d.renderTarget) usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    if (d.storage) usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    if (t.samples > 1) {
        // Multisampled images are transient render targets, not sampled RHI
        // textures. A matching single-sample resolve target is sampled later.
        usage = t.depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT :
                          VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
        const VkSampleCountFlags sampleBit = VkSampleCountFlags(t.samples);
        const VkSampleCountFlags limit = t.depth ?
            gpuProps_.limits.framebufferDepthSampleCounts :
            gpuProps_.limits.framebufferColorSampleCounts;
        VkImageFormatProperties imageProps{};
        if (!(limit & sampleBit) ||
            vkGetPhysicalDeviceImageFormatProperties(gpu_, t.vkFormat,
                VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL, usage, 0,
                &imageProps) != VK_SUCCESS ||
            !(imageProps.sampleCounts & sampleBit)) {
            error_ = "Vulkan: multisample format/count unsupported by physical device";
            return {};
        }
        if (!createImage(t, usage)) return {};
        // Renderpass transitions from UNDEFINED on first clear; the image
        // has no shader-readable descriptor or CPU upload contents.
        return {textures_.add(t)};
    }
    if (!createImage(t, usage)) return {};
    t.sampler = samplerFor(d, t);
    // The image may already be referenced by recorded barriers/copies.
    // On upload failure defer its destruction until that frame has retired.
    auto failUpload = [&]() -> TextureHandle {
        VTexture rejected = t;
        deferDestroy([this, rejected]() mutable {
            if (rejected.view) vkDestroyImageView(device_, rejected.view, nullptr);
            if (rejected.image) vkDestroyImage(device_, rejected.image, nullptr);
            memory_.release(rejected.alloc);
        });
        return {};
    };
    VkCommandBuffer cb = transferCmd();
    if (d.levelCount > 0 && d.levelData) {
        barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_REMAINING_MIP_LEVELS, 0, uint32_t(t.layers));
        for (int l = 0; l < t.levels; ++l) {
            const int w = std::max(1, t.width >> l), h = std::max(1, t.height >> l);
            const int z = std::max(1, t.volumeDepth >> l);
            const size_t bytes = t.compressed ? d.levelSize[l] :
                                 size_t(w) * size_t(h) * size_t(z) * size_t(t.layers) * texelBytes(d.format);
            if (!uploadImage(t, l, 0, 0, w, h, d.levelData[l], bytes, cb,
                             0, z, 0, t.layers)) return failUpload();
        }
        barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_REMAINING_MIP_LEVELS, 0, uint32_t(t.layers));
    } else if (d.data && !t.depth) {
        barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_UNDEFINED,
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_REMAINING_MIP_LEVELS, 0, uint32_t(t.layers));
        const size_t bytes = size_t(t.width) * size_t(t.height) *
                             size_t(t.volumeDepth) * size_t(t.layers) * texelBytes(d.format);
        if (!uploadImage(t, 0, 0, 0, t.width, t.height, d.data, bytes, cb,
                         0, t.volumeDepth, 0, t.layers)) return failUpload();
        if (t.generateMips) generateMips(t, cb);
        else barrierImage(cb, t.image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                          VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_REMAINING_MIP_LEVELS, 0, uint32_t(t.layers));
    } else initImageLayout(t, cb, true);
    return {textures_.add(t)};
}

bool VulkanDevice::readTextureAsync(TextureHandle h, TextureReadbackDesc request,
                                    TextureReadFn callback) {
    const VTexture* texture = textures_.get(h.id);
    // A lost device never completes the copy: refuse instead of accepting.
    if (!healthy_ || !recording_ || inPass_ || !callback || !texture || !texture->image ||
        texture->samples != 1 || texture->compressed || texture->depth ||
        request.mipLevel < 0 || request.mipLevel >= texture->levels ||
        request.arrayLayer < 0 ||
        request.arrayLayer >= (texture->dimension == TextureDimension::Texture3D ?
            std::max(1,texture->volumeDepth >> request.mipLevel) : texture->layers) ||
        textureReads_.size() >= 32)
        return false;
    if (texture->dimension != TextureDimension::Texture2D &&
        texture->dimension != TextureDimension::Texture2DArray &&
        texture->dimension != TextureDimension::Cube &&
        texture->dimension != TextureDimension::CubeArray &&
        texture->dimension != TextureDimension::Texture3D)
        return false;
    if (texture->format != PixelFormat::RGBA8 &&
        texture->format != PixelFormat::RGBA8_sRGB &&
        texture->format != PixelFormat::RGBA16F)
        return false;
    const int width = std::max(1, texture->width >> request.mipLevel);
    const int height = std::max(1, texture->height >> request.mipLevel);
    const size_t bpp = texture->format == PixelFormat::RGBA16F ? 8u : 4u;
    const size_t bytes = size_t(width) * size_t(height) * bpp;
    if (!bytes || bytes > (size_t(256) << 20)) return false;
    VkBuffer staging = VK_NULL_HANDLE;
    Allocation allocation;
    if (!createBufferObject(bytes, VK_BUFFER_USAGE_TRANSFER_DST_BIT, true,
                            staging, allocation))
        return false;
    VkCommandBuffer cb = slots_[size_t(slot_)].main;
    // Each selected mip is otherwise shader-readable; transition the full
    // array-layer range for this mip before/after the buffer copy.
    barrierImage(cb, texture->image, false,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 1, uint32_t(request.mipLevel), uint32_t(texture->layers));
    VkBufferImageCopy region{};
    region.bufferOffset = 0; // tightly packed GPU row and image pitch
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT,
        uint32_t(request.mipLevel),
        texture->dimension==TextureDimension::Texture3D ? 0u :
            uint32_t(request.arrayLayer), 1};
    region.imageOffset.z = texture->dimension==TextureDimension::Texture3D ?
        request.arrayLayer : 0;
    region.imageExtent = {uint32_t(width), uint32_t(height), 1};
    vkCmdCopyImageToBuffer(cb, texture->image,
                           VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                           staging, 1, &region);
    barrierImage(cb, texture->image, false,
                 VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                 VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 1, uint32_t(request.mipLevel), uint32_t(texture->layers));
    textureReads_.push_back({staging, allocation, bytes, width, height,
                             texture->format, serial_, std::move(callback),
                             texture->renderTarget});
    return true;
}

void VulkanDevice::updateTexture(TextureHandle h, int x, int y, int w, int hh,
                                 const void* pixels, int rowPitch) {
    // Reuse the checked region-upload path. A failed staging allocation
    // must never leave a command buffer with only a layout transition.
    if (!pixels || rowPitch < 0) {
        error_ = "Vulkan texture update has invalid source or row pitch";
        return;
    }
    const VTexture* t = textures_.get(h.id);
    if (!t || t->samples > 1 || t->dimension != TextureDimension::Texture2D ||
        t->compressed || t->depth || w <= 0 || hh <= 0)
        return;
    TextureUploadDesc u{};
    u.x=x; u.y=y; u.width=w; u.height=hh;
    u.mipLevel=0; u.arrayLayer=0;
    u.rowPitchBytes=rowPitch;
    u.slicePitchBytes=0;
    u.data=pixels;
    if (!updateTextureRegion(h,u) && error_.empty())
        error_="Vulkan texture update failed";
}

bool VulkanDevice::updateTextureRegion(TextureHandle handle, const TextureUploadDesc& u) {
    // transferCmd() selects the pre-frame command buffer while in a pass.
    // That buffer submits BEFORE the main pass, even when the caller asks
    // to overwrite a virtual atlas slice after earlier draw commands.
    // Fail closed instead of reordering an overwrite ahead of its readers.
    // Streamers must drain on the graphics thread BETWEEN render passes.
    if (inPass_) {
        error_ = "Vulkan texture region upload requires no active render pass";
        return false;
    }
    VTexture* t = textures_.get(handle.id);
    if (!t || t->samples > 1 || t->compressed || t->depth || !u.data || u.width <= 0 ||
        u.height <= 0 || u.depth <= 0 || u.mipLevel < 0 || u.mipLevel >= t->levels ||
        u.x < 0 || u.y < 0 || u.z < 0 || u.arrayLayer < 0 || u.arrayLayer >= t->layers)
        return false;
    const int w = std::max(1, t->width >> u.mipLevel);
    const int h = std::max(1, t->height >> u.mipLevel);
    const int d = std::max(1, t->volumeDepth >> u.mipLevel);
    if (u.x > w - u.width || u.y > h - u.height || u.z > d - u.depth ||
        (t->dimension != TextureDimension::Texture3D && (u.z != 0 || u.depth != 1)))
        return false;
    const size_t bpp = texelBytes(t->format);
    const size_t row = size_t(u.width) * bpp;
    if (u.rowPitchBytes < 0 || size_t(u.rowPitchBytes) < row) return false;
    const size_t imagePitch = u.slicePitchBytes > 0 ? size_t(u.slicePitchBytes) :
                              size_t(u.rowPitchBytes) * size_t(u.height);
    if (imagePitch < size_t(u.rowPitchBytes) * size_t(u.height)) return false;
    std::vector<uint8_t> packed(row * size_t(u.height) * size_t(u.depth));
    const auto* source = static_cast<const uint8_t*>(u.data);
    for (int z = 0; z < u.depth; ++z)
        for (int y = 0; y < u.height; ++y)
            std::memcpy(packed.data() + (size_t(z) * size_t(u.height) + size_t(y)) * row,
                        source + size_t(z) * imagePitch + size_t(y) * size_t(u.rowPitchBytes), row);
    // This upload is used by the virtual-texture page streamer as its
    // publication acknowledgement. Never report success when staging memory
    // could not be allocated: the generic uploadImage() helper silently
    // returns in that case, leaving the page uninitialized.
    VkBuffer staging = VK_NULL_HANDLE;
    VkDeviceSize stagingOffset = 0;
    void* mapped = stagingWrite(packed.size(), staging, stagingOffset);
    if (!mapped) {
        if (error_.empty()) error_ = "Vulkan texture region staging allocation failed";
        return false;
    }
    std::memcpy(mapped, packed.data(), packed.size());
    VkCommandBuffer cb = transferCmd();
    const bool regen = t->generateMips && u.mipLevel == 0;
    barrierImage(cb, t->image, false, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, regen ? VK_REMAINING_MIP_LEVELS : 1,
                 uint32_t(u.mipLevel), uint32_t(t->layers));
    VkBufferImageCopy copy{};
    copy.bufferOffset = stagingOffset;
    copy.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, uint32_t(u.mipLevel),
                             uint32_t(u.arrayLayer), 1};
    copy.imageOffset = {u.x, u.y, u.z};
    copy.imageExtent = {uint32_t(u.width), uint32_t(u.height),
                        uint32_t(u.depth)};
    vkCmdCopyBufferToImage(cb, staging, t->image,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &copy);
    if (regen) generateMips(*t, cb);
    else barrierImage(cb, t->image, false, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, 1,
                      uint32_t(u.mipLevel), uint32_t(t->layers));
    return true;
}


namespace {
VkImageViewType viewType(TextureDimension d) {
    switch (d) {
    case TextureDimension::Texture1D: return VK_IMAGE_VIEW_TYPE_1D;
    case TextureDimension::Texture2D: return VK_IMAGE_VIEW_TYPE_2D;
    case TextureDimension::Texture3D: return VK_IMAGE_VIEW_TYPE_3D;
    case TextureDimension::Texture2DArray: return VK_IMAGE_VIEW_TYPE_2D_ARRAY;
    case TextureDimension::Cube: return VK_IMAGE_VIEW_TYPE_CUBE;
    case TextureDimension::CubeArray: return VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
    }
    return VK_IMAGE_VIEW_TYPE_2D;
}
VkSamplerAddressMode vkAddress(SamplerAddressMode m) {
    switch (m) {
    case SamplerAddressMode::Repeat: return VK_SAMPLER_ADDRESS_MODE_REPEAT;
    case SamplerAddressMode::MirrorRepeat: return VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
    case SamplerAddressMode::ClampToBorder: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_BORDER;
    case SamplerAddressMode::ClampToEdge: return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    }
    return VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
}
VkCompareOp vkCompare(CompareFunction f) {
    switch (f) {
    case CompareFunction::Never: return VK_COMPARE_OP_NEVER;
    case CompareFunction::Less: return VK_COMPARE_OP_LESS;
    case CompareFunction::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareFunction::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareFunction::Greater: return VK_COMPARE_OP_GREATER;
    case CompareFunction::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareFunction::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareFunction::Always: return VK_COMPARE_OP_ALWAYS;
    }
    return VK_COMPARE_OP_LESS_OR_EQUAL;
}
} // namespace

TextureViewHandle VulkanDevice::createTextureView(TextureHandle handle, const TextureViewDesc& v) {
    VTexture* t = textures_.get(handle.id);
    if (!t || t->samples > 1) {
        error_ = "Vulkan texture view: invalid or multisampled render-only parent";
        return {};
    }
    TextureDesc parent;
    parent.width = t->width; parent.height = t->height; parent.depth = t->volumeDepth;
    parent.arrayLayers = t->layers; parent.dimension = t->dimension;
    parent.format = t->format; parent.mipLevels = t->levels;
    if (!validateTextureViewDesc(parent, v)) {
        error_ = "Vulkan texture view: invalid mip, layer, aspect or dimension"; return {};
    }
    VkImageViewCreateInfo ci = vkStruct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    ci.image = t->image;
    ci.viewType = viewType(v.dimension);
    ci.format = t->vkFormat;
    ci.subresourceRange.aspectMask = t->depth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
    ci.subresourceRange.baseMipLevel = uint32_t(v.baseMipLevel);
    ci.subresourceRange.levelCount = uint32_t(v.mipLevelCount);
    ci.subresourceRange.baseArrayLayer = uint32_t(v.baseArrayLayer);
    ci.subresourceRange.layerCount = uint32_t(v.arrayLayerCount);
    if (t->format == PixelFormat::R8) {
        ci.components = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ZERO,
                         VK_COMPONENT_SWIZZLE_ZERO, VK_COMPONENT_SWIZZLE_ONE};
    }
    if (t->format == PixelFormat::ETC2_RGB8 || t->format == PixelFormat::BC1)
        ci.components.a = VK_COMPONENT_SWIZZLE_ONE;
    VkImageView native = VK_NULL_HANDLE;
    if (vkCreateImageView(device_, &ci, nullptr, &native) != VK_SUCCESS) {
        error_ = "Vulkan: vkCreateImageView for subresource failed"; return {};
    }
    return {views_.add({native, handle.id})};
}

void VulkanDevice::destroyTextureView(TextureViewHandle handle) {
    VTextureView* v = views_.get(handle.id);
    if (!v) return;
    const VkImageView native = v->native;
    for (auto& b : views_bound_)
        if (b == handle.id) b = 0;
    views_.remove(handle.id);
    descriptorsDirty_ = true;
    deferDestroy([this, native] { vkDestroyImageView(device_, native, nullptr); });
}

SamplerHandle VulkanDevice::createSampler(const SamplerDesc& d) {
    if (!validateSamplerDesc(d) ||
        (d.maxAnisotropy > 1.0f && !samplerAnisotropy_) ||
        std::abs(d.lodBias) > gpuProps_.limits.maxSamplerLodBias) {
        error_ = "Vulkan sampler: unsupported LOD bias or anisotropy"; return {};
    }
    VkSamplerCreateInfo ci = vkStruct<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    ci.minFilter = d.minFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.magFilter = d.magFilter == Filter::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    ci.mipmapMode = d.mipFilter == SamplerMipFilter::Linear ?
                    VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
    ci.addressModeU = vkAddress(d.addressU);
    ci.addressModeV = vkAddress(d.addressV);
    ci.addressModeW = vkAddress(d.addressW);
    ci.mipLodBias = d.lodBias;
    ci.anisotropyEnable = d.maxAnisotropy > 1.0f ? VK_TRUE : VK_FALSE;
    ci.maxAnisotropy = std::max(1.0f, std::min(d.maxAnisotropy, gpuProps_.limits.maxSamplerAnisotropy));
    ci.compareEnable = d.compare ? VK_TRUE : VK_FALSE;
    ci.compareOp = vkCompare(d.comparison);
    // mipFilter None emulates a non-mipmapped GL/Metal filter: always the
    // base level, i.e. minLod = maxLod = 0. Keeping a caller minLod > 0
    // with maxLod = 0 violated maxLod >= minLod (invalid create info).
    const bool noMips = d.mipFilter == SamplerMipFilter::None;
    ci.minLod = noMips ? 0.0f : d.minLod;
    ci.maxLod = noMips ? 0.0f : d.maxLod;
    ci.borderColor = VK_BORDER_COLOR_FLOAT_OPAQUE_WHITE;
    VkSampler native = VK_NULL_HANDLE;
    if (vkCreateSampler(device_, &ci, nullptr, &native) != VK_SUCCESS) {
        error_ = "Vulkan sampler: allocation failed"; return {};
    }
    return {customSamplers_.add({native})};
}

void VulkanDevice::destroySampler(SamplerHandle handle) {
    VSampler* s = customSamplers_.get(handle.id);
    if (!s) return;
    const VkSampler native = s->native;
    for (auto& b : samplers_bound_) if (b == handle.id) b = 0;
    customSamplers_.remove(handle.id);
    descriptorsDirty_ = true;
    deferDestroy([this, native] { vkDestroySampler(device_, native, nullptr); });
}

bool VulkanDevice::bufferInfo(BufferHandle h, BufferInfo& out) const {
    const auto* b = buffers_.get(h.id);
    if (!b) return false;
    out = {b->size,uint32_t(b->versions.size())};
    return true;
}

bool VulkanDevice::textureInfo(TextureHandle h, TextureInfo& out) const {
    const auto* t = textures_.get(h.id);
    if (!t) return false;
    out = {t->width,t->height,t->format};
    return true;
}

void VulkanDevice::destroyTexture(TextureHandle h) {
    VTexture* t = textures_.get(h.id);
    if (!t) return;
    VTexture old = *t;
    std::vector<uint32_t> children;
    views_.eachWithId([&](uint32_t id, VTextureView& v) {
        if (v.parentId == h.id) children.push_back(id);
    });
    for (uint32_t id : children) destroyTextureView({id});
    deferDestroy([this, old]() mutable {
        vkDestroyImageView(device_, old.view, nullptr);
        vkDestroyImage(device_, old.image, nullptr);
        memory_.release(old.alloc);
    });
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
