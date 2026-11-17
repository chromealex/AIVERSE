#include "GlobalGeometryPool.h"
#include <algorithm>
#include <atomic>
#include <cstdlib>
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
};
namespace {
std::atomic<bool> poolEnabled{true};
std::mutex poolMutex;
std::map<gfx::Device*, std::vector<std::weak_ptr<GeometryPoolPage>>> pools;
// Small pages reduce stranded memory when a scene has many distinct models.
// Oversized models still get a dedicated power-of-two page below.
constexpr size_t kDefaultPageSize = 2 * 1024 * 1024;
constexpr size_t kMaximumPooledModelSize = 16 * 1024 * 1024;
bool canPool(const Model& m) {
    // Keep skin/morph/LOD/cluster paths on dedicated buffers because those
    // paths can reference original GPU vertex IDs or expanded index ranges.
    if (m.geometry || !m.skins.empty() || !m.clips.empty() ||
        !m.variants.empty() || m.morphTex.valid()) return false;
    for (const auto& mesh : m.meshes) {
        if (mesh.deformable || !mesh.lods.empty()) return false;
        for (const auto& p : mesh.primitives)
            if (p.morphTargets || !p.jointPalette.empty() || !p.variantMaterial.empty()) return false;
    }
    return true;
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
    for (const auto& weak : iter->second) {
        const auto page = weak.lock();
        if (!page) continue;
        ++stats.pages;
        stats.allocatedBytes += page->capacity;
        stats.occupiedBytes += page->vertexEnd + (page->capacity - page->indexBegin);
        const long owners = page.use_count() - 1; // exclude this temporary lock
        if (owners > 0) stats.liveModels += size_t(owners);
    }
    return stats;
}

bool globalGeometryPoolingEnabled() { return !disabled(); }
void setGlobalGeometryPoolingEnabled(bool enabled) { poolEnabled.store(enabled, std::memory_order_relaxed); }

bool uploadModelToGeometryPool(gfx::Device& dev, Model& m) {
    // Pooling is primarily for MDI across assets. On GLES/WebGL/legacy GL,
    // retain compact per-model buffers instead of allocating unused large pages.
    if (disabled() || !dev.supportsMultiDrawIndirect() ||
        !dev.canUpdateSharedBuffers() || !canPool(m) ||
        m.vertices.empty() || m.indices.empty()) return false;
    // Guard size arithmetic before allocating or calculating page offsets.
    if (m.vertices.size() > kMaximumPooledModelSize / sizeof(ModelVertex) ||
        m.indices.size() > kMaximumPooledModelSize / sizeof(uint32_t)) return false;
    const size_t vertexBytes = m.vertices.size() * sizeof(ModelVertex);
    const size_t indexBytes = m.indices.size() * sizeof(uint32_t);
    if (vertexBytes > kMaximumPooledModelSize ||
        indexBytes > kMaximumPooledModelSize ||
        vertexBytes + indexBytes > kMaximumPooledModelSize) return false;

    std::lock_guard lock(poolMutex);
    auto& pages = pools[&dev];
    std::shared_ptr<GeometryPoolPage> target;
    for (auto it = pages.begin(); it != pages.end();) {
        auto page = it->lock();
        if (!page) { it = pages.erase(it); continue; }
        if (!target && page->indexBegin >= page->vertexEnd &&
            vertexBytes <= page->indexBegin - page->vertexEnd &&
            indexBytes <= page->indexBegin - page->vertexEnd - vertexBytes) target = page;
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
        pages.push_back(target);
    }
    // Vertex base is measured in 60-byte vertices; index base in uint32s.
    const size_t vbOffset = target->vertexEnd;
    const size_t ibOffset = target->indexBegin - indexBytes;
    if (vbOffset / sizeof(ModelVertex) > std::numeric_limits<uint32_t>::max() ||
        ibOffset / sizeof(uint32_t) > std::numeric_limits<uint32_t>::max()) return false;
    target->vertexEnd += vertexBytes;
    target->indexBegin = ibOffset;
    dev.updateBuffer(target->buffer, m.vertices.data(), vertexBytes, vbOffset);
    dev.updateBuffer(target->buffer, m.indices.data(), indexBytes, ibOffset);
    m.geometryPoolPage = target;
    m.vb = target->buffer;
    m.ib = target->buffer;
    m.gpuBaseVertex = uint32_t(vbOffset / sizeof(ModelVertex));
    m.gpuFirstIndex = uint32_t(ibOffset / sizeof(uint32_t));
    return true;
}

void releaseModelGeometryPool(gfx::Device& dev, Model& m) {
    if (!m.geometryPoolPage) return;
    if (m.geometryPoolPage.use_count() == 1)
        dev.destroyBuffer(m.geometryPoolPage->buffer);
    m.geometryPoolPage.reset();
    m.gpuBaseVertex = m.gpuFirstIndex = 0;
}
} // namespace aiverse::render
