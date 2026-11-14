#pragma once
#include "Model.h"
#include <map>
#include <atomic>
#include <mutex>
namespace aiverse::render {
// Per-content session. CPU access is synchronized; GPU methods belong to the render thread.
// The owning view shuts this down after releasing all models/native consumers.
class ModelTextureCache : public std::enable_shared_from_this<ModelTextureCache> {
public:
    bool read(const std::string& path, const FileReader& reader, Image& out);
    void upload(gfx::Device& dev, Image& image);
    void shutdown(gfx::Device& dev);
    void trim(gfx::Device& dev);
    size_t decodedCount() const { return decodedCount_.load(); }
    size_t gpuCount() const { return gpu_.size(); }
private:
    struct Decoded { Image image; size_t bytes = 0; uint64_t used = 0; };
    std::mutex mutex_;
    std::map<std::string, Decoded> decoded_;
    std::map<std::string, Image> gpu_;
    size_t bytes_ = 0;
    std::atomic<size_t> decodedCount_{0};
    uint64_t clock_ = 0;
    static constexpr size_t kCpuBudget = 64u * 1024u * 1024u;
};
}
