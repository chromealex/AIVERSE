#include "GlobalGeometryPool.h"
#include "GeometryPoolEligibility.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <map>
#include <mutex>
#include <vector>

namespace aiverse::render {

// Pages intentionally have no GPU destructor. The last owning Model releases
// the buffer explicitly while its gfx::Device is still alive, like releaseModel().
struct GeometryPoolPage {
    gfx::BufferHandle buffer;
    size_t capacity = 0;
    size_t vertexEnd = 0;
    size_t indexBegin = 0;
    struct RetiredPair {
        size_t vertexOffset = 0, indexOffset = 0;
        uint32_t vertexBytes = 0, indexBytes = 0;
        uint64_t retireFrame = 0;
    };
    // No concurrent RHI calls: owned by poolMutex, with bounded retention.
    std::vector<RetiredPair> retired;
    uint64_t recycledUploads = 0;
};
namespace {
std::atomic<bool> poolEnabled{true};
std::mutex poolMutex;
std::map<gfx::Device*, std::vector<std::weak_ptr<GeometryPoolPage>>> pools;
// Small pages reduce stranded memory when a scene has many distinct models.
// Oversized models still get a dedicated power-of-two page below.
constexpr size_t kDefaultPageSize = 2 * 1024 * 1024;
constexpr size_t kMaximumPooledModelSize = 16 * 1024 * 1024;
constexpr size_t kMaxRetiredPairsPerPage = 256;
bool recyclingEnabled() {
    static const bool requested = [] {
        const char* value = std::getenv("AIVERSE_GEOMETRY_POOL_RECYCLE");
        return value && std::strcmp(value, "1") == 0;
    }();
    return requested;
}
// -1 inherits the one-time environment setting; editor/native hosts may
// override it before asset (re)upload without mutating existing pooled data.
std::atomic<int> rigidLodPoolingOverride{-1};
bool rigidLodPoolAllowed() {
    static const bool envEnabled = [] {
        const char* value = std::getenv("AIVERSE_GEOMETRY_POOL_RIGID_LOD");
        return value && std::strcmp(value, "1") == 0;
    }();
    const int overrideValue = rigidLodPoolingOverride.load(std::memory_order_relaxed);
    return overrideValue < 0 ? envEnabled : overrideValue != 0;
}
bool disabled() {
    if (const char* safe = std::getenv("AIVERSE_RENDER_SAFE"); safe && std::string(safe) == "1") return true;
    const char* e = std::getenv("AIVERSE_GEOMETRY_POOL");
    return !poolEnabled.load(std::memory_order_relaxed) ||
           (e && (std::string(e) == "off" || std::string(e) == "0"));
}
} // namespace
GeometryPoolStats geometryPoolStats(gfx::Device& dev) {
    GeometryPoolStats stats;
    std::lock_guard lock(poolMutex);
    const auto iter = pools.find(&dev);
    if (iter == pools.end()) return stats;
    const uint64_t completed = dev.completedFrameSerial();
    for (const auto& weak : iter->second) {
        const auto page = weak.lock();
        if (!page) continue;
        ++stats.pages;
        stats.allocatedBytes += page->capacity;
        stats.occupiedBytes += page->vertexEnd + (page->capacity - page->indexBegin);
        const long owners = page.use_count() - 1; // exclude this temporary lock
        if (owners > 0) stats.liveModels += size_t(owners);
        stats.retiredPairs += page->retired.size();
        stats.recycledUploads += page->recycledUploads;
        if (completed != 0 && dev.gpuHealthy())
            for (const auto& span : page->retired)
                if (span.retireFrame != 0 && span.retireFrame <= completed)
                    ++stats.reusablePairs;
    }
    return stats;
}

bool globalGeometryPoolingEnabled() { return !disabled(); }
void setGlobalGeometryPoolingEnabled(bool enabled) { poolEnabled.store(enabled, std::memory_order_relaxed); }
bool rigidLodGeometryPoolingEnabled() { return rigidLodPoolAllowed(); }
void setRigidLodGeometryPoolingEnabled(bool enabled) {
    rigidLodPoolingOverride.store(enabled ? 1 : 0, std::memory_order_relaxed);
}

const char* geometryPoolAdmissionName(GeometryPoolAdmissionReason reason) {
    switch (reason) {
    case GeometryPoolAdmissionReason::Eligible: return "eligible for shared geometry upload";
    case GeometryPoolAdmissionReason::Disabled: return "geometry pooling disabled";
    case GeometryPoolAdmissionReason::UnsupportedBackend: return "backend has no compatible shared indexed MDI buffers";
    case GeometryPoolAdmissionReason::EmptyGeometry: return "model has no CPU vertex/index data";
    case GeometryPoolAdmissionReason::DedicatedAsset: return "asset explicitly requests dedicated buffers";
    case GeometryPoolAdmissionReason::UnsafeGeometry: return "model is not rigid geometry-pool eligible";
    case GeometryPoolAdmissionReason::Oversized: return "model exceeds 16 MiB pool upload limit";
    }
    return "unknown geometry pool admission";
}

GeometryPoolAdmission geometryPoolAdmission(gfx::Device& dev, const Model& model) {
    if (disabled()) return {GeometryPoolAdmissionReason::Disabled};
    if (!dev.supportsMultiDrawIndirect() || !dev.canUpdateSharedBuffers())
        return {GeometryPoolAdmissionReason::UnsupportedBackend};
    if (model.vertices.empty() || model.indices.empty())
        return {GeometryPoolAdmissionReason::EmptyGeometry};
    using PoolPolicy = Model::Batching::GeometryPoolPolicy;
    const PoolPolicy policy = model.batching.geometryPoolPolicy;
    // A per-model opt-out also applies when global rigid LOD pooling is on.
    if (!model.batching.allowGeometryPool || policy == PoolPolicy::Dedicated)
        return {GeometryPoolAdmissionReason::DedicatedAsset};
    const bool allowRigidLods = policy == PoolPolicy::AllowRigidLods ||
        (policy == PoolPolicy::Inherit && rigidLodPoolAllowed());
    const RigidGeometryPoolBlock block = rigidGeometryPoolBlock(model, allowRigidLods);
    if (block != RigidGeometryPoolBlock::None)
        return {GeometryPoolAdmissionReason::UnsafeGeometry, block};
    // Do these checks BEFORE multiplying counts; never overflow size_t.
    if (model.vertices.size() > kMaximumPooledModelSize / sizeof(ModelVertex) ||
        model.indices.size() > kMaximumPooledModelSize / sizeof(uint32_t))
        return {GeometryPoolAdmissionReason::Oversized};
    const size_t vb = model.vertices.size() * sizeof(ModelVertex);
    const size_t ib = model.indices.size() * sizeof(uint32_t);
    if (vb > kMaximumPooledModelSize || ib > kMaximumPooledModelSize ||
        vb + ib > kMaximumPooledModelSize)
        return {GeometryPoolAdmissionReason::Oversized};
    return {};
}

bool uploadModelToGeometryPool(gfx::Device& dev, Model& m) {
    // This prediction is the authoritative CPU admission check. GPU buffer
    // creation may still fail, in which case uploadModel uses dedicated VB/IB.
    if (!geometryPoolAdmission(dev, m).eligible()) return false;
    const size_t vertexBytes = m.vertices.size() * sizeof(ModelVertex);
    const size_t indexBytes = m.indices.size() * sizeof(uint32_t);

    std::unique_lock lock(poolMutex);
    auto& pages = pools[&dev];
    std::shared_ptr<GeometryPoolPage> target;
    bool newPage = false;
    size_t selectedPair = SIZE_MAX;
    const uint64_t completed = recyclingEnabled() && dev.gpuHealthy()
        ? dev.completedFrameSerial() : 0;
    // Try completed, whole vertex/index pairs first. No split/coalescing of
    // mixed geometry allocations: it must never overwrite a live neighbor.
    if (completed != 0) {
        for (auto it = pages.begin(); it != pages.end(); ++it) {
            auto page = it->lock();
            if (!page) continue;
            for (size_t n = 0; n < page->retired.size(); ++n) {
                const auto& span = page->retired[n];
                if (span.retireFrame != 0 && span.retireFrame <= completed &&
                    span.vertexBytes >= vertexBytes &&
                    span.indexBytes >= indexBytes) {
                    target = std::move(page);
                    selectedPair = n;
                    break;
                }
            }
            if (target) break;
        }
    }
    if (!target) for (auto it = pages.begin(); it != pages.end();) {
        auto page = it->lock();
        if (!page) { it = pages.erase(it); continue; }
        if (!target && page->indexBegin >= page->vertexEnd &&
            vertexBytes <= page->indexBegin - page->vertexEnd &&
            indexBytes <= page->indexBegin - page->vertexEnd - vertexBytes)
            target = std::move(page);
        ++it;
    }
    if (!target) {
        size_t capacity = kDefaultPageSize;
        while (capacity < vertexBytes + indexBytes) capacity *= 2;
        gfx::BufferDesc desc;
        desc.type = gfx::BufferType::Storage; // usable as vertex + index on all RHI backends
        desc.usage = gfx::Usage::Dynamic;
        desc.size = capacity;
        desc.label = "AIVERSE global rigid geometry page";
        const gfx::BufferHandle buffer = dev.createBuffer(desc);
        if (!buffer.valid()) return false;
        target = std::make_shared<GeometryPoolPage>();
        target->buffer = buffer;
        target->capacity = capacity;
        target->indexBegin = capacity;
        newPage = true; // publish only after both writes succeed
    }
    // Vertex base is measured in 60-byte vertices; index base in uint32s.
    const bool reusePair = selectedPair != SIZE_MAX;
    const size_t vbOffset = reusePair
        ? target->retired[selectedPair].vertexOffset : target->vertexEnd;
    const size_t ibOffset = reusePair
        ? target->retired[selectedPair].indexOffset
        : target->indexBegin - indexBytes;
    const bool offsetsValid =
        vbOffset / sizeof(ModelVertex) <= std::numeric_limits<uint32_t>::max() &&
        ibOffset / sizeof(uint32_t) <= std::numeric_limits<uint32_t>::max();
    // Failed writes remain outside committed ranges. The first successful
    // copy may occupy an unused slot if the second fails; it is not exposed
    // to any Model and may safely be overwritten by a future upload.
    const bool uploaded = offsetsValid &&
        dev.updateSharedBufferChecked(target->buffer, m.vertices.data(),
                                      vertexBytes, vbOffset) &&
        dev.updateSharedBufferChecked(target->buffer, m.indices.data(),
                                      indexBytes, ibOffset);
    if (!uploaded) {
        if (newPage) {
            const gfx::BufferHandle doomed = target->buffer;
            if (pages.empty()) pools.erase(&dev);
            lock.unlock();
            dev.destroyBuffer(doomed);
        }
        return false;
    }
    // The model owns the *entire* reserved pair, not merely the uploaded
    // prefix. A smaller successor must return the original extents on release,
    // otherwise every recycle generation silently loses the unused tails.
    uint32_t reservedVertices = uint32_t(vertexBytes);
    uint32_t reservedIndices = uint32_t(indexBytes);
    if (reusePair) {
        const GeometryPoolPage::RetiredPair previous =
            target->retired[selectedPair];
        reservedVertices = previous.vertexBytes;
        reservedIndices = previous.indexBytes;
        auto& holes = target->retired;
        holes[selectedPair] = holes.back();
        holes.pop_back();
        ++target->recycledUploads;
    } else {
        target->vertexEnd += vertexBytes;
        target->indexBegin = ibOffset;
    }
    if (newPage) pages.push_back(target);
    m.geometryPoolPage = target;
    m.vb = target->buffer;
    m.ib = target->buffer;
    m.gpuBaseVertex = uint32_t(vbOffset / sizeof(ModelVertex));
    m.gpuFirstIndex = uint32_t(ibOffset / sizeof(uint32_t));
    m.gpuPoolVertexBytes = reservedVertices;
    m.gpuPoolIndexBytes = reservedIndices;
    return true;
}

void releaseModelGeometryPool(gfx::Device& dev, Model& m) {
    // Upload, stats and model teardown share one ownership boundary.
    // Without this lock, two last owners can both observe use_count()==2,
    // reset concurrently and leave the GPU buffer permanently allocated.
    gfx::BufferHandle orphan{};
    {
        std::lock_guard lock(poolMutex);
        if (!m.geometryPoolPage) return;
        const std::shared_ptr<GeometryPoolPage> page =
            std::move(m.geometryPoolPage);
        if (page.use_count() > 1 && recyclingEnabled() &&
            m.gpuPoolVertexBytes && m.gpuPoolIndexBytes &&
            page->retired.size() < kMaxRetiredPairsPerPage) {
            const uint64_t retireFrame = dev.frameSerial();
            if (retireFrame != 0) {
                // Copy byte offsets BEFORE clearing the Model allocation.
                // The new owner must wait for this GPU frame to complete.
                page->retired.push_back({
                    size_t(m.gpuBaseVertex) * sizeof(ModelVertex),
                    size_t(m.gpuFirstIndex) * sizeof(uint32_t),
                    m.gpuPoolVertexBytes, m.gpuPoolIndexBytes,
                    retireFrame});
            }
        }
        if (page.use_count() == 1) {
            orphan = page->buffer;
            const auto iter = pools.find(&dev);
            if (iter != pools.end()) {
                auto& pages = iter->second;
                pages.erase(std::remove_if(pages.begin(), pages.end(),
                    [&](const std::weak_ptr<GeometryPoolPage>& weak) {
                        const auto alive = weak.lock();
                        return !alive || alive.get() == page.get();
                    }), pages.end());
                if (pages.empty()) pools.erase(iter);
            }
        }
    }
    // Release GPU buffers outside the pool mutex. The expired weak entry
    // has already been retired; a concurrent upload can never reuse it.
    // RHI destroy is responsible for retaining in-flight GPU uses.
    if (orphan.valid()) dev.destroyBuffer(orphan);
    m.gpuBaseVertex = m.gpuFirstIndex = 0;
    m.gpuPoolVertexBytes = m.gpuPoolIndexBytes = 0;
}
} // namespace aiverse::render
