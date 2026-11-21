// Device-local weak cache for immutable VIVID morph geometry + compute
// programs. Keeps no GPU resource alive unless at least one actor or host
// holds a shared_ptr<GpuMorphSharedAsset>.
#pragma once
#include "GpuMorphSharedAsset.h"
#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string>

namespace aiverse::vivid {

class GpuMorphAssetCache final {
public:
    explicit GpuMorphAssetCache(gfx::Device& device) : device_(device) {}
    GpuMorphAssetCache(const GpuMorphAssetCache&)=delete;
    GpuMorphAssetCache& operator=(const GpuMorphAssetCache&)=delete;

    // GPU device thread only. A different authored model/morph revision
    // automatically gets a separate GPU asset. A shared asset's arrays
    // and compute programs are read-only while its actors are alive.
    std::shared_ptr<GpuMorphSharedAsset> acquire(
        const SparseMorphLibrary& library,
        std::span<const render::ModelVertex> vertices,
        std::string* error=nullptr);

    // Remove stale weak metadata. Live actor GPU resources are NOT touched.
    std::size_t pruneExpired();
    std::size_t cachedKeys() const noexcept { return entries_.size(); }
    void clear() noexcept { entries_.clear(); }

private:
    // Double 64-bit digest + counts; exact bytes are included for all
    // GPU-relevant vertex and CSR input streams. Collision probability is
    // extremely small; not a cryptographic identity/security primitive.
    using Key=std::array<std::uint64_t,6>;
    static Key keyFor(const SparseMorphLibrary& library,
                      std::span<const render::ModelVertex> vertices) noexcept;
    gfx::Device& device_;
    std::map<Key,std::weak_ptr<GpuMorphSharedAsset>> entries_;
};

} // namespace aiverse::vivid
