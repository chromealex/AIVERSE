// AIVERSE texture streaming: deterministic CPU staging and bounded render-thread uploads.
// Presentation-only; never an authoritative ECS state or a replacement for asynchronous
// native GPU readback. Device::updateTextureRegion MUST copy the supplied bytes during the call.
#pragma once
#include "Gfx.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::gfx {

struct TextureStreamId {
    uint64_t value = 0;
    bool valid() const { return value != 0; }
    bool operator==(const TextureStreamId&) const = default;
};
struct TextureUploadTicket {
    uint64_t value = 0;
    bool valid() const { return value != 0; }
    bool operator==(const TextureUploadTicket&) const = default;
};
struct TextureStreamConfig {
    size_t gpuBudgetBytes = size_t(512) << 20;
    size_t stagingBudgetBytes = size_t(64) << 20;
    size_t uploadBytesPerFrame = size_t(8) << 20;
    size_t uploadsPerFrame = 64;
};
struct TextureStreamStats {
    size_t gpuAllocatedBytes = 0;
    size_t stagedBytes = 0;
    size_t pendingUploads = 0;
    size_t liveResources = 0;
    uint64_t completedUploads = 0;
    uint64_t failedUploads = 0;
    uint64_t cancelledUploads = 0;
};

// Owns the GPU resources it creates. A Resource ID is NEVER reused even if a
// backend reuses TextureHandle.id. This prevents a queued write from targeting
// a new resource after the old one has been destroyed.
class TextureStreamQueue {
public:
    // Callback is dispatched after the frame's drain loop has completed,
    // so it may safely queue/cancel/destroy resources for a later frame.
    // It is invoked on the device thread, never a background worker.
    using CompletionFn = std::function<void(TextureUploadTicket, TextureStreamId, bool)>;
    explicit TextureStreamQueue(Device& device, TextureStreamConfig config = {})
        : device_(&device), config_(config) {}
    ~TextureStreamQueue() { shutdown(); }
    TextureStreamQueue(const TextureStreamQueue&) = delete;
    TextureStreamQueue& operator=(const TextureStreamQueue&) = delete;
    TextureStreamQueue(TextureStreamQueue&&) = delete;
    TextureStreamQueue& operator=(TextureStreamQueue&&) = delete;

    TextureStreamId createTexture(TextureDesc desc) {
        if (!device_ || desc.data || desc.levelCount || desc.levelData ||
            desc.levelSize || desc.renderTarget || desc.storage ||
            desc.sampleCount != 1 || desc.mipmaps ||
            !device_->supportsTextureDimension(desc.dimension) ||
            !device_->supports(desc.format) ||
            validateTextureDesc(desc) != TextureDescError::None ||
            uncompressedTexturePixelBytes(desc.format) <= 0) {
            error_ = "stream texture requires supported, empty uncompressed sampled storage";
            return {};
        }
        if (!desc.mipLevels) desc.mipLevels = 1;
        const size_t estimated = estimateBytes(desc);
        if (!estimated || estimated > config_.gpuBudgetBytes ||
            gpuBytes_ > config_.gpuBudgetBytes - estimated) {
            error_ = "texture streaming GPU budget exceeded or invalid dimensions";
            return {};
        }
        if (!nextResource_ || nextResource_ == std::numeric_limits<uint64_t>::max()) {
            error_ = "texture stream resource ID range exhausted";
            return {};
        }
        // Labels are caller-owned, and are not retained by this wrapper.
        TextureHandle gpu = device_->createTexture(desc);
        if (!gpu.valid()) {
            error_ = device_->lastError();
            if (error_.empty()) error_ = "GPU texture allocation failed";
            return {};
        }
        const TextureStreamId id{nextResource_++};
        desc.label = nullptr;
        resources_.emplace(id.value, Record{gpu, desc, estimated, false});
        gpuBytes_ += estimated;
        error_.clear();
        return id;
    }

    TextureHandle texture(TextureStreamId id) const {
        auto it = resources_.find(id.value);
        return it == resources_.end() ? TextureHandle{} : it->second.gpu;
    }
    bool failed(TextureStreamId id) const {
        auto it = resources_.find(id.value);
        return it == resources_.end() || it->second.failed;
    }

    // The source pointer is copied immediately into the CPU staging queue.
    // rowPitchBytes=0 and slicePitchBytes=0 mean tightly packed texels.
    // The texture must have been created through this queue.
    TextureUploadTicket enqueue(TextureStreamId id, TextureUploadDesc upload,
                                const void* source, size_t sourceBytes) {
        auto it = resources_.find(id.value);
        if (it == resources_.end() || it->second.failed || !source) {
            error_ = "unknown/failed stream resource or null upload buffer";
            return {};
        }
        const TextureDesc& d = it->second.desc;
        if (upload.mipLevel < 0 || upload.mipLevel >= d.mipLevels ||
            upload.arrayLayer < 0 || upload.x < 0 || upload.y < 0 ||
            upload.z < 0 || upload.width < 1 || upload.height < 1 ||
            upload.depth < 1 || upload.rowPitchBytes < 0 ||
            upload.slicePitchBytes < 0) {
            error_ = "invalid streamed texture subresource";
            return {};
        }
        const int mipW = std::max(1, d.width >> upload.mipLevel);
        const int mipH = std::max(1, d.height >> upload.mipLevel);
        const int mipD = d.dimension == TextureDimension::Texture3D ?
                         std::max(1, d.depth >> upload.mipLevel) : 1;
        if (upload.x > mipW - upload.width || upload.y > mipH - upload.height ||
            upload.z > mipD - upload.depth ||
            (d.dimension == TextureDimension::Texture1D &&
             (upload.y || upload.height != 1)) ||
            (d.dimension != TextureDimension::Texture3D &&
             (upload.z || upload.depth != 1)) ||
            upload.arrayLayer >= d.arrayLayers ||
            (d.dimension == TextureDimension::Texture3D && upload.arrayLayer != 0)) {
            error_ = "texture stream upload exceeds mip/layer bounds";
            return {};
        }
        const size_t pixelBytes = size_t(uncompressedTexturePixelBytes(d.format));
        if (size_t(upload.width) > std::numeric_limits<size_t>::max() / pixelBytes) {
            error_ = "streamed row pitch overflow";
            return {};
        }
        const size_t minRow = size_t(upload.width) * pixelBytes;
        const size_t row = upload.rowPitchBytes ? size_t(upload.rowPitchBytes) : minRow;
        if (row < minRow || row > size_t(std::numeric_limits<int>::max()) ||
            row > std::numeric_limits<size_t>::max() / size_t(upload.height)) {
            error_ = "invalid streamed row pitch";
            return {};
        }
        const size_t minSlice = row * size_t(upload.height);
        const size_t slice = upload.slicePitchBytes ?
                             size_t(upload.slicePitchBytes) : minSlice;
        if (slice < minSlice || slice > size_t(std::numeric_limits<int>::max()) ||
            size_t(upload.depth - 1) >
                (std::numeric_limits<size_t>::max() - minSlice) / slice) {
            error_ = "invalid streamed slice pitch";
            return {};
        }
        const size_t bytes = slice * size_t(upload.depth - 1) + minSlice;
        if (!bytes || bytes > sourceBytes ||
            bytes > config_.uploadBytesPerFrame ||
            bytes > config_.stagingBudgetBytes ||
            stagedBytes_ > config_.stagingBudgetBytes - bytes ||
            !nextTicket_ || nextTicket_ == std::numeric_limits<uint64_t>::max()) {
            error_ = "stream texture payload truncated or frame/staging budget exceeded";
            return {};
        }
        std::vector<uint8_t> copy(bytes);
        std::memcpy(copy.data(), source, bytes);
        upload.data = nullptr; // pointer rebuilt after moving the pending job
        upload.rowPitchBytes = int(row);
        upload.slicePitchBytes = int(slice);
        TextureUploadTicket ticket{nextTicket_++};
        pending_.push_back(Job{ticket, id, upload, std::move(copy)});
        stagedBytes_ += bytes;
        error_.clear();
        return ticket;
    }

    bool cancel(TextureUploadTicket ticket) {
        auto it = std::find_if(pending_.begin(), pending_.end(),
                               [ticket](const Job& j) { return j.ticket == ticket; });
        if (it == pending_.end()) return false;
        stagedBytes_ -= it->bytes.size();
        pending_.erase(it);
        ++cancelled_;
        return true;
    }

    bool destroy(TextureStreamId id) {
        auto it = resources_.find(id.value);
        if (it == resources_.end()) return false;
        // Discard pending jobs before the physical handle can be recycled.
        cancelPendingFor(id);
        device_->destroyTexture(it->second.gpu);
        gpuBytes_ -= it->second.estimatedBytes;
        resources_.erase(it);
        return true;
    }

    // Call on the graphics device thread, outside a render/compute encoder.
    // Each update is sent in FIFO order with strict byte/upload count budgets.
    // On failure mark that resource failed and cancel its remaining uploads;
    // the caller should destroy/recreate it to recover.
    size_t drain(const CompletionFn& completion = {}) {
        if (!device_) return 0;
        struct Result { TextureUploadTicket ticket; TextureStreamId resource; bool ok; };
        std::vector<Result> results;
        size_t submitted = 0, transferred = 0;
        while (!pending_.empty() && submitted < config_.uploadsPerFrame) {
            const size_t bytes = pending_.front().bytes.size();
            if (bytes > config_.uploadBytesPerFrame - transferred) break;
            Job job = std::move(pending_.front());
            pending_.pop_front();
            stagedBytes_ -= bytes;
            auto it = resources_.find(job.resource.value);
            if (it == resources_.end() || it->second.failed) continue;
            job.upload.data = job.bytes.data();
            const bool ok = device_->updateTextureRegion(it->second.gpu, job.upload);
            ++submitted;
            transferred += bytes;
            results.push_back({job.ticket, job.resource, ok});
            if (ok) ++completed_;
            else {
                it->second.failed = true;
                ++failed_;
                error_ = device_->lastError();
                if (error_.empty()) error_ = "GPU streaming subresource upload failed";
                cancelPendingFor(job.resource);
            }
        }
        // Deliberately invoke callbacks only after the queue iteration.
        // The callback can now destroy a resource or schedule more work.
        if (completion)
            for (const auto& r : results) completion(r.ticket, r.resource, r.ok);
        return submitted;
    }

    TextureStreamStats stats() const {
        return {gpuBytes_, stagedBytes_, pending_.size(), resources_.size(),
                completed_, failed_, cancelled_};
    }
    const std::string& lastError() const { return error_; }
    const TextureStreamConfig& configuration() const { return config_; }
    void shutdown() {
        pending_.clear();
        stagedBytes_ = 0;
        if (device_) {
            for (const auto& [_, entry] : resources_) device_->destroyTexture(entry.gpu);
        }
        resources_.clear();
        gpuBytes_ = 0;
    }

private:
    // One linear pass (FIFO order of the remaining jobs is preserved);
    // the previous per-match erase was quadratic for large queues.
    void cancelPendingFor(TextureStreamId id) {
        size_t kept = 0;
        for (size_t i = 0; i < pending_.size(); ++i) {
            if (pending_[i].resource == id) {
                stagedBytes_ -= pending_[i].bytes.size();
                ++cancelled_;
            } else {
                if (kept != i) pending_[kept] = std::move(pending_[i]);
                ++kept;
            }
        }
        pending_.resize(kept);
    }
    struct Record {
        TextureHandle gpu;
        TextureDesc desc;
        size_t estimatedBytes = 0;
        bool failed = false;
    };
    struct Job {
        TextureUploadTicket ticket;
        TextureStreamId resource;
        TextureUploadDesc upload;
        std::vector<uint8_t> bytes;
    };
    static size_t estimateBytes(const TextureDesc& d) {
        const int bpp = uncompressedTexturePixelBytes(d.format);
        if (bpp < 1) return 0;
        size_t total = 0;
        const int count = d.mipLevels > 0 ? d.mipLevels : 1;
        for (int mip = 0; mip < count; ++mip) {
            size_t bytes = size_t(bpp);
            const size_t dims[4] = {
                size_t(std::max(1, d.width >> mip)),
                size_t(std::max(1, d.height >> mip)),
                size_t(d.dimension == TextureDimension::Texture3D ?
                       std::max(1, d.depth >> mip) : 1),
                size_t(d.arrayLayers)
            };
            for (size_t dim : dims) {
                if (bytes > std::numeric_limits<size_t>::max() / dim) return 0;
                bytes *= dim;
            }
            if (total > std::numeric_limits<size_t>::max() - bytes) return 0;
            total += bytes;
        }
        return total;
    }

    Device* device_ = nullptr; // must outlive this queue
    TextureStreamConfig config_;
    std::map<uint64_t, Record> resources_;
    std::deque<Job> pending_; // FIFO: drain pops from the front in O(1)
    size_t gpuBytes_ = 0;
    size_t stagedBytes_ = 0;
    uint64_t nextResource_ = 1, nextTicket_ = 1;
    uint64_t completed_ = 0, failed_ = 0, cancelled_ = 0;
    std::string error_;
};

} // namespace aiverse::gfx
