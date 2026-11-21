#pragma once
// Renderer-owned RHI resource lower-bound accounting. Read-only: never maps,
// reads back, synchronizes, retires, or releases a GPU resource.
// Each owner submits only resources it allocates, never borrowed assets.
#include "engine/gfx/Gfx.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <set>

namespace aiverse::render {
struct GpuOwnedMemoryStats {
    uint64_t textureKnownBytes = 0;
    uint64_t bufferKnownBytes = 0;
    size_t textures = 0, buffers = 0;
    size_t unknownTextures = 0, unknownBuffers = 0;
    size_t multiVersionBuffers = 0;
    uint64_t knownBytes() const {
        const uint64_t maximum = std::numeric_limits<uint64_t>::max();
        return textureKnownBytes>maximum-bufferKnownBytes
            ? maximum : textureKnownBytes+bufferKnownBytes;
    }
};

class GpuOwnedMemoryCounter {
public:
    explicit GpuOwnedMemoryCounter(const gfx::Device& device) : device_(device) {}
    bool texture(gfx::TextureHandle h) {
        if (!h.valid() || !seenTextures_.insert(h.id).second) return false;
        ++stats_.textures;
        gfx::TextureInfo info{};
        if (!device_.textureInfo(h, info) || info.width<=0 || info.height<=0) {
            ++stats_.unknownTextures;return false;
        }
        uint64_t bytesPerPixel=0;
        switch (info.format) {
        case gfx::PixelFormat::R8: bytesPerPixel=1; break;
        case gfx::PixelFormat::RGBA16F: bytesPerPixel=8; break;
        case gfx::PixelFormat::RGBA32F: bytesPerPixel=16; break;
        case gfx::PixelFormat::RGBA8:
        case gfx::PixelFormat::RGBA8_sRGB:
        case gfx::PixelFormat::Depth: bytesPerPixel=4;break;
        default:break; // block compression requires mip/format metadata
        }
        if (!bytesPerPixel) { ++stats_.unknownTextures;return false; }
        const uint64_t max=std::numeric_limits<uint64_t>::max();
        const uint64_t pixels=uint64_t(info.width)*uint64_t(info.height);
        const uint64_t bytes=pixels>max/bytesPerPixel ? max :
            pixels*bytesPerPixel;
        stats_.textureKnownBytes=saturatingAdd(stats_.textureKnownBytes,bytes);
        return true;
    }
    bool buffer(gfx::BufferHandle h) {
        if (!h.valid() || !seenBuffers_.insert(h.id).second) return false;
        ++stats_.buffers;
        gfx::BufferInfo info{};
        if (!device_.bufferInfo(h,info) || !info.logicalBytes) {
            ++stats_.unknownBuffers;return false;
        }
        stats_.bufferKnownBytes=saturatingAdd(stats_.bufferKnownBytes,
                                             uint64_t(info.logicalBytes));
        if (info.versions>1) ++stats_.multiVersionBuffers;
        return true;
    }
    GpuOwnedMemoryStats stats() const { return stats_; }
    static uint64_t saturatingAdd(uint64_t a,uint64_t b) {
        const uint64_t max=std::numeric_limits<uint64_t>::max();
        return a>max-b ? max : a+b;
    }
private:
    const gfx::Device& device_;
    GpuOwnedMemoryStats stats_{};
    std::set<uint32_t> seenTextures_,seenBuffers_;
};
} // namespace aiverse::render
