#pragma once
#include "Model.h"
#include "GpuOwnedMemory.h"
#include <map>
#include <atomic>
#include <limits>
#include <mutex>
namespace aiverse::render {
// Per-content session. CPU access is synchronized; GPU methods belong to the render thread.
// The owning view shuts this down after releasing all models/native consumers.
class ModelTextureCache : public std::enable_shared_from_this<ModelTextureCache> {
public:
    bool read(const std::string& path, const FileReader& reader, Image& out);
    void upload(gfx::Device& dev, Image& image);
    // Read-only GPU lease lookup for speculative Model imports. Returns
    // false without allocating a GPU texture or mutating the shared cache.
    bool borrowResident(Image& image) const;

    // Phase 1: prepare all GPU-cache nodes and shared-lease control blocks
    // without altering resident GPU cache entries or Model ownership.
    // Phase 2: commit moves preallocated map nodes and switches scratch
    // Models to shared leases. Replaced dedicated duplicate textures are
    // returned to the caller for normal GPU-fenced retirement.
    struct PendingImage {
        Model* model=nullptr;
        size_t appendIndex=0;
        const Image* staged=nullptr; // remains valid until ModelImage transaction commit
    };
    class PreparedGpuPromotion final {
        friend class ModelTextureCache;
    public:
        PreparedGpuPromotion(const PreparedGpuPromotion&)=delete;
        PreparedGpuPromotion& operator=(const PreparedGpuPromotion&)=delete;
        PreparedGpuPromotion(PreparedGpuPromotion&&)=default;
        PreparedGpuPromotion& operator=(PreparedGpuPromotion&&)=default;
        size_t newSharedTextures() const noexcept {return pending_.size();}
        size_t bindings() const noexcept {return bindings_.size();}
        // Caller MUST enqueue returned redundant GPU textures into a
        // persistent rollback pool; never destroy them immediately.
        std::vector<Image> commit();
    private:
        explicit PreparedGpuPromotion(ModelTextureCache& cache):cache_(&cache){}
        struct Binding {
            // Model* addresses remain stable across std::map node transfers.
            // Images inside Model::images DO NOT remain at stable addresses
            // when the material transaction reserves/appends new textures.
            Model* model=nullptr;
            size_t index=0;
            Image* sharedOwner=nullptr; // stable std::map<Image> node address
        };
        ModelTextureCache* cache_=nullptr;
        std::map<std::string,Image> pending_;
        std::vector<Binding> bindings_;
        std::vector<Image> redundant_;
        bool committed_=false;
    };
    PreparedGpuPromotion prepareGpuPromotion(
        const std::vector<Model*>& models,
        const std::vector<PendingImage>& appended={});

    void shutdown(gfx::Device& dev);
    void trim(gfx::Device& dev);
    // Render-thread source revision. Discards only its CPU decode; a new
    // upload receives a versioned GPU cache key while existing image leases
    // keep old textures alive until every borrower releases them.
    void invalidateSource(const std::string& path);
    size_t decodedCount() const { return decodedCount_.load(); }
    size_t gpuCount() const { return gpu_.size(); } // render thread only
    // Cache owns every non-borrowed GPU image. Model::Image instances
    // referencing this cache are borrowers and must NOT add these handles.
    // Use only from graphics thread (same contract as upload/trim).
    void appendOwnedGpuMemory(GpuOwnedMemoryCounter& counter) const {
        for (const auto& [key, image] : gpu_) {
            (void)key;
            if (image.tex.valid()) counter.texture(image.tex);
        }
    }
    struct GpuOwnerEstimate {
        size_t uniqueTextures=0,compressedTextures=0;
        uint64_t rgba8BaseMipEquivalentBytes=0;
    };
    // Each shared GPU texture cache owner is counted ONCE by its unique
    // sampler+revision key, never once per Model/Image borrower. This is an
    // RGBA8 base-mip equivalent, not driver bytes (compressed formats,
    // mipmaps, padding and backend allocation can differ).
    GpuOwnerEstimate gpuOwnerEstimate() const {
        GpuOwnerEstimate result;
        for(const auto& [key,image]:gpu_) {
            (void)key;
            if(!image.tex.valid() || image.width<=0 || image.height<=0)
                continue;
            ++result.uniqueTextures;
            if(image.gpuCompressed)++result.compressedTextures;
            const auto width=uint64_t(image.width);
            const auto height=uint64_t(image.height);
            const auto max=std::numeric_limits<uint64_t>::max();
            const auto bytes=height>max/width/4u?max:width*height*4u;
            result.rgba8BaseMipEquivalentBytes=
                bytes>max-result.rgba8BaseMipEquivalentBytes?max:
                result.rgba8BaseMipEquivalentBytes+bytes;
        }
        return result;
    }
    // Actual decoded pixel cache allocation (not source file size / GPU textures).
    size_t decodedBytes() const { std::lock_guard lock(mutex_); return bytes_; }
private:
    struct Decoded { Image image; size_t bytes = 0; uint64_t used = 0; };
    mutable std::mutex mutex_;
    std::map<std::string, Decoded> decoded_;
    std::map<std::string, Image> gpu_;
    // Render thread only: upload() and invalidateSource() use this map.
    std::map<std::string, uint64_t> sourceRevisions_;
    // Must be identical for normal and speculative GPU ownership paths.
    std::string gpuKey(const Image& image) const;
    size_t bytes_ = 0;
    std::atomic<size_t> decodedCount_{0};
    uint64_t clock_ = 0;
    static constexpr size_t kCpuBudget = 64u * 1024u * 1024u;
};
}
