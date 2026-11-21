#include "GpuMorphAssetCache.h"

#include <bit>
#include <cstddef>
#include <cstdint>
#include <span>
#include <utility>

namespace aiverse::vivid {
namespace {
struct Hasher {
    std::uint64_t left=14695981039346656037ull;
    std::uint64_t right=1099511628211ull;
    void bytes(std::span<const std::byte> values) noexcept {
        for(const auto b:values) {
            const auto octet=std::uint64_t(std::to_integer<unsigned char>(b));
            left=(left^octet)*1099511628211ull;
            right=(right^octet)*0x9e3779b185ebca87ull;
            right=std::rotl(right,13);
        }
    }
    void number(std::uint64_t value) noexcept {
        for(int i=0;i<8;++i) {
            const std::byte octet{static_cast<unsigned char>(value&255u)};
            bytes({&octet,1});
            value>>=8;
        }
    }
    template<typename T>
    void buffer(std::span<const T> values) noexcept {
        number(values.size());
        bytes(std::as_bytes(values));
    }
};
}

GpuMorphAssetCache::Key GpuMorphAssetCache::keyFor(
        const SparseMorphLibrary& library,
        std::span<const render::ModelVertex> vertices) noexcept {
    Hasher hash;
    hash.number(vertices.size());
    hash.number(library.vertices());
    hash.number(library.targets());
    hash.buffer(vertices);
    hash.buffer(library.gpuDeltas());
    hash.buffer(library.gpuVertexOffsets());
    hash.buffer(library.gpuVertexLinks());
    return {hash.left,hash.right,
            static_cast<std::uint64_t>(vertices.size()),
            static_cast<std::uint64_t>(library.targets()),
            static_cast<std::uint64_t>(library.deltas()),
            static_cast<std::uint64_t>(library.gpuVertexLinks().size())};
}

std::shared_ptr<GpuMorphSharedAsset> GpuMorphAssetCache::acquire(
        const SparseMorphLibrary& library,
        std::span<const render::ModelVertex> vertices,
        std::string* error) {
    if(!device_.gpuHealthy() || !device_.supportsCompute()) {
        if(error)*error="VIVID GPU asset cache: device compute unavailable";
        return {};
    }
    const auto key=keyFor(library,vertices);
    auto it=entries_.find(key);
    if(it!=entries_.end()) {
        if(auto alive=it->second.lock()) {
            if(alive->device()==&device_ && alive->ready()) {
                if(error)error->clear();
                return alive;
            }
        }
        entries_.erase(it);
    }
    auto asset=GpuMorphSharedAsset::create(device_,library,vertices,error);
    if(asset)entries_.emplace(key,asset);
    return asset;
}

std::size_t GpuMorphAssetCache::pruneExpired() {
    std::size_t erased=0;
    for(auto it=entries_.begin();it!=entries_.end();) {
        if(it->second.expired()) {it=entries_.erase(it);++erased;}
        else ++it;
    }
    return erased;
}

} // namespace aiverse::vivid
