// Incremental GPU upload for a dynamic-only GI L1 probe overlay.
// Shader composition is a separate step: NEVER use this as the baked static probe atlas.
// Data layout per probe: three float4 rows {constant, x, y, z}, RGB order, 3 texels.
// Rendering thread only, between render/compute passes (per gfx::Device contract).
#pragma once
#include "GiDynamicProbeReference.h"
#include "../gfx/Gfx.h"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace aiverse::render {

class GiDynamicProbeTexture {
public:
    static constexpr int kTextureWidth = 384;       // 128 probes/row, 3 float4 texels/probe
    static constexpr int kProbesPerRow = 128;
    static_assert(sizeof(float4) == sizeof(float) * 4);

    ~GiDynamicProbeTexture() { release(); }
    GiDynamicProbeTexture() = default;
    GiDynamicProbeTexture(const GiDynamicProbeTexture&) = delete;
    GiDynamicProbeTexture& operator=(const GiDynamicProbeTexture&) = delete;

    bool initialize(gfx::Device& device, size_t probeCount, bool computeWritable = false) {
        release();
        if (!probeCount || probeCount > 20000 ||
            !device.supports(computeWritable ? gfx::PixelFormat::RGBA16F : gfx::PixelFormat::RGBA32F) ||
            (computeWritable && !device.supportsCompute()))
            return false;
        const int rows = int((probeCount + kProbesPerRow - 1) / kProbesPerRow);
        std::vector<float4> zeroes;
        std::vector<uint16_t> zeroesHalf;
        if (computeWritable) zeroesHalf.resize(size_t(kTextureWidth) * size_t(rows) * 4);
        else zeroes.resize(size_t(kTextureWidth) * size_t(rows));
        gfx::TextureDesc desc;
        desc.width = kTextureWidth;
        desc.height = rows;
        // The texture-RHI branch currently permits RGBA16F, not RGBA32F, writable storage.
        // Sampling is float on both; CPU RGBA32F upload remains the legacy/default path.
        desc.format = computeWritable ? gfx::PixelFormat::RGBA16F : gfx::PixelFormat::RGBA32F;
        desc.filter = gfx::Filter::Nearest;
        desc.storage = computeWritable; // Metal: shader read + shader write; legacy default stays read-only.
        desc.data = computeWritable ? static_cast<const void*>(zeroesHalf.data())
                                    : static_cast<const void*>(zeroes.data());
        desc.label = "dynamic GI probe overlay (3 float4/probe)";
        auto texture = device.createTexture(desc);
        if (!texture.valid()) return false;
        dev_ = &device;
        handle_ = texture;
        count_ = probeCount;
        computeWritable_ = computeWritable;
        return true;
    }

    void release() {
        if (dev_ && handle_.valid()) dev_->destroyTexture(handle_);
        dev_ = nullptr;
        handle_ = {};
        count_ = 0;
        computeWritable_ = false;
    }

    const gfx::Device* device() const { return dev_; }
    gfx::TextureHandle texture() const { return handle_; }
    size_t probeCount() const { return count_; }
    bool computeWritable() const { return computeWritable_; }
    int height() const { return int((count_ + kProbesPerRow - 1) / kProbesPerRow); }

    struct UploadStats { size_t probes=0, regions=0, bytes=0; };

    // Stable sorted ids are grouped into row-contiguous updates to avoid a full
    // (possibly 1 MB) atlas upload each frame. Does not change immutable baked GI.
    UploadStats upload(const GiDynamicProbeReference& gi, std::vector<uint32_t> updated) {
        if (computeWritable_)
            throw std::logic_error("compute-writable half-float GI atlas requires GPU updates");
        if (!dev_ || !handle_.valid() || gi.coefficients().size() != count_)
            throw std::logic_error("dynamic GI probe texture not configured");
        for (auto id : updated) if (id >= count_) throw std::out_of_range("GI upload probe ID");
        std::sort(updated.begin(), updated.end());
        updated.erase(std::unique(updated.begin(), updated.end()), updated.end());

        UploadStats stats;
        std::vector<float4> packed;
        for (size_t begin = 0; begin < updated.size();) {
            size_t end = begin + 1;
            // Adjacent probe IDs can be uploaded together only within a texture row.
            while (end < updated.size() &&
                   updated[end] == updated[end - 1] + 1u &&
                   updated[end] / kProbesPerRow == updated[begin] / kProbesPerRow)
                ++end;
            packed.clear();
            packed.reserve((end - begin) * 3);
            for (size_t i = begin; i < end; ++i) {
                for (const auto& c : gi.coefficient(updated[i]).irradiance) packed.push_back(c);
            }
            const int x = int(updated[begin] % kProbesPerRow) * 3;
            const int y = int(updated[begin] / kProbesPerRow);
            const int width = int(packed.size());
            dev_->updateTexture(handle_, x, y, width, 1, packed.data(),
                                width * int(sizeof(float4)));
            stats.probes += end - begin;
            ++stats.regions;
            stats.bytes += packed.size() * sizeof(float4);
            begin = end;
        }
        return stats;
    }

    UploadStats uploadAll(const GiDynamicProbeReference& gi) {
        std::vector<uint32_t> ids;
        ids.reserve(count_);
        for (uint32_t i = 0; i < count_; ++i) ids.push_back(i);
        return upload(gi, std::move(ids));
    }

private:
    gfx::Device* dev_ = nullptr;
    gfx::TextureHandle handle_{};
    size_t count_ = 0;
    bool computeWritable_ = false;
};

} // namespace aiverse::render
