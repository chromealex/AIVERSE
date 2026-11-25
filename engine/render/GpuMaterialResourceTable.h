// AIVERSE presentation-only material resource table (CPU staging/identity).
// The future heterogeneous GPU ICB/descriptor-table ABI may consume these
// snapshots, but this class does not bind textures or assume that per-command
// GPU resource selection is implemented. Existing ICB stays homogeneous.
#pragma once

#include "ModelVirtualTextureSampling.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <bit>
#include <cmath>
#include <limits>
#include <unordered_map>
#include <utility>
#include <vector>

namespace aiverse::render {

// A complete immutable snapshot of fragment resources needed by one
// material command. The caller must keep every TextureHandle alive until
// GPU completion. Slots not named in graphTextureCount MUST be empty.
struct GpuMaterialResourceBinding {
    static constexpr uint32_t kMaxGraphTextures = 4;
    std::array<gfx::TextureHandle,kMaxGraphTextures> graphTextures{};
    uint32_t graphTextureCount = 0;
    gfx::TextureHandle alphaCoverage{};
    // Opt-in per-command built-in PBR texture slots 1..6 and exact U2,
    // 9 std140/Metal float4 rows. No Shader Graph or material palette.
    bool pbrEnabled = false;
    std::array<gfx::TextureHandle,6> pbrTextures{};
    std::array<float,36> pbrU2{};
    bool virtualTextureEnabled = false;
    ModelVirtualTextureBinding virtualTexture{};

    bool valid() const {
        if (graphTextureCount > kMaxGraphTextures ||
            (graphTextureCount && !pbrEnabled)) return false;
        for (uint32_t i = 0; i < graphTextureCount; ++i)
            if (!graphTextures[i].valid()) return false;
        for (uint32_t i = graphTextureCount; i < kMaxGraphTextures; ++i)
            if (graphTextures[i].valid()) return false;
        if (pbrEnabled) {
            for (const auto t : pbrTextures) if (!t.valid()) return false;
            for (float x : pbrU2) if (!std::isfinite(x)) return false;
        } else {
            for (const auto t : pbrTextures) if (t.valid()) return false;
            for (float x : pbrU2) if (x != 0.0f) return false;
        }
        if (virtualTextureEnabled) return virtualTexture.valid();
        return !virtualTexture.physicalAtlas.valid() &&
               !virtualTexture.pageTable.valid();
    }
    bool operator==(const GpuMaterialResourceBinding&) const = default;
};

// A ticket is valid only for its exact staging generation and frame.
// Index alone is never sufficient: a later frame may reuse the same index.
struct GpuMaterialResourceTicket {
    uint64_t frameSerial = 0;
    uint64_t generation = 0;
    uint32_t index = std::numeric_limits<uint32_t>::max();
    bool valid() const {
        return frameSerial && generation &&
               index != std::numeric_limits<uint32_t>::max();
    }
    bool operator==(const GpuMaterialResourceTicket&) const = default;
};

class GpuMaterialResourceTable {
public:
    // Bounded CPU table. Does not own GPU textures or pass GPU fences.
    explicit GpuMaterialResourceTable(size_t limit = 4096)
        : limit_(limit > 0 && limit <= 65536 ? limit : 4096) {}
    GpuMaterialResourceTable(const GpuMaterialResourceTable&) = delete;
    GpuMaterialResourceTable& operator=(const GpuMaterialResourceTable&) = delete;

    // Reset on the render owner thread before staging a new ICB frame.
    // Repeating with the same frame serial still invalidates old tickets.
    void beginFrame(uint64_t frameSerial) {
        if (++generation_ == 0) ++generation_;
        frameSerial_ = frameSerial;
        values_.clear();
        buckets_.clear();
    }

    // Explicitly invalidate all outstanding tickets on resource reload,
    // renderer shutdown or archive promotion. Must not retire GPU storage.
    void reset() { beginFrame(0); }

    bool ready() const { return frameSerial_ != 0; }
    uint64_t frameSerial() const { return frameSerial_; }
    size_t size() const { return values_.size(); }
    size_t capacity() const { return limit_; }
    // CPU staging table only. True blocks image retirement until this
    // table generation is explicitly detached; no GPU wait or ownership.
    bool referencesTexture(gfx::TextureHandle texture) const noexcept {
        if(!texture.valid())return false;
        for(const auto& value:values_){
            if(value.alphaCoverage==texture||value.virtualTexture.physicalAtlas==texture||
               value.virtualTexture.pageTable==texture)return true;
            for(auto handle:value.pbrTextures)if(handle==texture)return true;
            for(auto handle:value.graphTextures)if(handle==texture)return true;
        }
        return false;
    }

    // Exact deduplication; collisions in a 64-bit prefilter are harmless.
    // Returns invalid when full or if any requested GPU handle is invalid.
    GpuMaterialResourceTicket intern(const GpuMaterialResourceBinding& value) {
        if (!ready() || !value.valid()) return {};
        const auto key=hash(value);
        if (const auto it=buckets_.find(key);it!=buckets_.end())
            for (uint32_t index : it->second)
                if (values_[index] == value)
                    return {frameSerial_,generation_,index};
        if (values_.size() >= limit_) return {};
        const auto index=uint32_t(values_.size());
        values_.push_back(value);
        buckets_[key].push_back(index);
        return {frameSerial_,generation_,index};
    }

    // Copy the immutable staged snapshot. No raw pointers into the table
    // may escape (vector growth would otherwise invalidate them).
    bool resolve(GpuMaterialResourceTicket ticket,
                 GpuMaterialResourceBinding& out) const {
        out={};
        if (!ticket.valid() || ticket.frameSerial != frameSerial_ ||
            ticket.generation != generation_ ||
            ticket.index >= values_.size()) return false;
        out=values_[ticket.index];
        return true;
    }

    // Validate resource identity AGAIN just before GPU submission.
    // A changed atlas revision, binding, alpha mask or source texture
    // requires ordinary CPU fallback until a new snapshot is staged.
    bool matches(GpuMaterialResourceTicket ticket,
                 const GpuMaterialResourceBinding& current) const {
        if (!current.valid() || !ticket.valid() ||
            ticket.frameSerial != frameSerial_ ||
            ticket.generation != generation_ ||
            ticket.index >= values_.size()) return false;
        return values_[ticket.index] == current;
    }

private:
    static uint64_t hash(const GpuMaterialResourceBinding& value) {
        uint64_t h=14695981039346656037ULL;
        const auto mix=[&](uint64_t n) {
            h ^= n;
            h *= 1099511628211ULL;
        };
        mix(value.graphTextureCount);
        for (auto t : value.graphTextures) mix(t.id);
        mix(value.alphaCoverage.id);
        mix(value.pbrEnabled ? 1u : 0u);
        if (value.pbrEnabled) {
            for (auto t : value.pbrTextures) mix(t.id);
            for (float valueU2 : value.pbrU2)
                mix(valueU2 == 0.0f ? 0u : std::bit_cast<uint32_t>(valueU2));
        }
        mix(value.virtualTextureEnabled ? 1u : 0u);
        if (value.virtualTextureEnabled)
            mix(modelVirtualTextureBindingHash(value.virtualTexture));
        return h;
    }
    size_t limit_ = 4096;
    uint64_t generation_ = 0, frameSerial_ = 0;
    std::vector<GpuMaterialResourceBinding> values_;
    std::unordered_map<uint64_t,std::vector<uint32_t>> buckets_;
};

} // namespace aiverse::render
