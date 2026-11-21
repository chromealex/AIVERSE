#include "StrataGpuScene.h"

#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <utility>

namespace aiverse::render::strata {
namespace {
bool fail(std::string* error, const char* reason) {
    if (error) *error = reason;
    return false;
}
bool finiteBounds(const ClusterBounds& b) {
    for (uint32_t i = 0; i < 3; ++i)
        if (!std::isfinite(b.min[i]) || !std::isfinite(b.max[i]) ||
            b.min[i] > b.max[i]) return false;
    return true;
}
bool totalFits(uint64_t v) {
    return v <= uint64_t(std::numeric_limits<uint32_t>::max());
}
} // namespace

bool packGpuMetadata(const ModelAsset& source, std::vector<uint8_t>& out,
                     std::string* error) {
    if (error) error->clear();
    if (source.primitives.empty() || source.primitives.size() > 65536)
        return fail(error, "Strata GPU metadata requires 1..65536 primitives");

    uint64_t totalNodes = 0, totalRoots = 0;
    std::set<std::pair<uint32_t, uint32_t>> uniquePrimitives;
    for (const ModelPrimitiveAsset& p : source.primitives) {
        const Asset& geometry = p.geometry;
        if (!validate(geometry.nodes, geometry.roots) ||
            geometry.bounds.size() != geometry.nodes.size())
            return fail(error, "invalid Strata GPU cluster tree");
        if (!uniquePrimitives.insert({p.mesh, p.primitive}).second ||
            p.material != geometry.nodes.front().materialGroup)
            return fail(error, "duplicate Strata primitive or inconsistent material");
        for (size_t i = 0; i < geometry.nodes.size(); ++i) {
            const auto& node = geometry.nodes[i];
            if (node.materialGroup != p.material ||
                !finiteBounds(geometry.bounds[i]) ||
                node.firstIndex > geometry.indices.size() ||
                node.indexCount > geometry.indices.size() - node.firstIndex)
                return fail(error, "invalid Strata GPU bounds, index range or material");
        }
        totalNodes += geometry.nodes.size();
        totalRoots += geometry.roots.size();
        if (!totalFits(totalNodes) || !totalFits(totalRoots))
            return fail(error, "Strata GPU metadata exceeds u32 capacity");
    }
    const uint64_t primitivesOffset = sizeof(GpuMetadataHeader);
    const uint64_t nodesOffset =
        primitivesOffset + source.primitives.size() * uint64_t(sizeof(GpuPrimitive));
    const uint64_t rootsOffset = nodesOffset + totalNodes * sizeof(GpuNode);
    const uint64_t endOffset = rootsOffset + totalRoots * sizeof(uint32_t);
    const uint64_t alignedSize = (endOffset + 15u) & ~uint64_t(15u);
    // Bound temporary and GPU metadata memory (source staging has tighter caps).
    if (!totalFits(endOffset) || alignedSize > 64ull * 1024 * 1024)
        return fail(error, "Strata GPU metadata payload too large");

    std::vector<uint8_t> payload(size_t(alignedSize), uint8_t(0));
    GpuMetadataHeader header;
    header.primitiveCount = uint32_t(source.primitives.size());
    header.nodeCount = uint32_t(totalNodes);
    header.rootCount = uint32_t(totalRoots);
    header.primitivesByteOffset = uint32_t(primitivesOffset);
    header.nodesByteOffset = uint32_t(nodesOffset);
    header.rootsByteOffset = uint32_t(rootsOffset);
    std::memcpy(payload.data(), &header, sizeof(header));

    uint32_t nextNode = 0, nextRoot = 0;
    for (size_t i = 0; i < source.primitives.size(); ++i) {
        const auto& p = source.primitives[i];
        const Asset& a = p.geometry;
        GpuPrimitive gpuPrimitive;
        gpuPrimitive.mesh = p.mesh;
        gpuPrimitive.primitive = p.primitive;
        gpuPrimitive.materialGroup = p.material;
        gpuPrimitive.nodeBase = nextNode;
        gpuPrimitive.nodeCount = uint32_t(a.nodes.size());
        gpuPrimitive.rootBase = nextRoot;
        gpuPrimitive.rootCount = uint32_t(a.roots.size());
        std::memcpy(payload.data() + primitivesOffset + i * sizeof(GpuPrimitive),
                    &gpuPrimitive, sizeof(gpuPrimitive));

        for (size_t j = 0; j < a.nodes.size(); ++j) {
            const ClusterNode& n = a.nodes[j];
            const ClusterBounds& b = a.bounds[j];
            GpuNode gpu;
            for (uint32_t k = 0; k < 3; ++k) {
                gpu.boundsMin[k] = b.min[k];
                gpu.boundsMax[k] = b.max[k];
            }
            gpu.firstChild = n.childCount ? nextNode + n.firstChild : invalidNode;
            gpu.childCount = n.childCount;
            gpu.sourceFirstIndex = n.firstIndex; // NOT a pooled index offset
            gpu.sourceIndexCount = n.indexCount;
            gpu.materialGroup = n.materialGroup;
            gpu.resident = n.resident ? 1u : 0u;
            gpu.geometricError = n.geometricError;
            std::memcpy(payload.data() + nodesOffset +
                        (size_t(nextNode) + j) * sizeof(GpuNode),
                        &gpu, sizeof(gpu));
        }
        for (size_t j = 0; j < a.roots.size(); ++j) {
            const uint32_t root = nextNode + a.roots[j];
            std::memcpy(payload.data() + rootsOffset +
                        (size_t(nextRoot) + j) * sizeof(uint32_t),
                        &root, sizeof(root));
        }
        nextNode += uint32_t(a.nodes.size());
        nextRoot += uint32_t(a.roots.size());
    }
    out = std::move(payload);
    return true;
}

bool GpuMetadataScene::stage(gfx::Device& dev, const std::string& sourceKey,
                             const Model& model, const ModelAsset& asset) {
    if (sourceKey.empty()) {
        ++stats_.failed;
        warnings_.push_back("Strata GPU metadata: empty source key");
        return false;
    }
    if (auto it = entries_.find(sourceKey); it != entries_.end()) {
        // Never overwrite live metadata while GPU might reference it. The
        // scene owner must explicitly shutdown/reload for a newer revision.
        if (find(sourceKey, model).valid()) return true;
        ++stats_.failed;
        warnings_.push_back("Strata GPU metadata: source changed; reload required: " + sourceKey);
        return false;
    }
    // Original Model GPU geometry must already belong to the EXISTING pool.
    // Dedicated/source-cluster paths keep normal rendering; no second pool.
    if ((dev.info().backend != gfx::Backend::Metal &&
         dev.info().backend != gfx::Backend::Vulkan) ||
        !dev.supportsCompute() || !model.geometryPoolPage ||
        !model.vb.valid() || !model.ib.valid() || model.vb != model.ib ||
        !model.gpuPoolVertexBytes || !model.gpuPoolIndexBytes) {
        ++stats_.declined;
        return false;
    }
    std::vector<uint8_t> payload;
    std::string reason;
    if (!packGpuMetadata(asset, payload, &reason)) {
        ++stats_.failed;
        warnings_.push_back("Strata GPU metadata '" + sourceKey + "': " + reason);
        return false;
    }
    uint64_t indexCount = 0;
    for (const auto& primitive : asset.primitives) {
        indexCount += primitive.geometry.indices.size();
        if (indexCount > std::numeric_limits<uint32_t>::max())
            break;
    }
    if (payload.size() > limits_.maxSourceBytes ||
        stats_.bytes > limits_.maxSceneBytes ||
        payload.size() > limits_.maxSceneBytes - stats_.bytes) {
        ++stats_.declined;
        return false;
    }
    const uint64_t requestedIndexBytes = indexCount * sizeof(uint32_t);
    if (!indexCount || indexCount > std::numeric_limits<uint32_t>::max() ||
        requestedIndexBytes > limits_.maxSourceIndexBytes ||
        requestedIndexBytes > limits_.maxSceneIndexBytes ||
        stats_.indexBytes > limits_.maxSceneIndexBytes ||
        requestedIndexBytes > limits_.maxSceneIndexBytes - stats_.indexBytes) {
        ++stats_.indexDeclined;
        return false;
    }

    std::vector<uint32_t> indices;
    indices.reserve(size_t(indexCount));
    for (const auto& primitive : asset.primitives)
        indices.insert(indices.end(), primitive.geometry.indices.begin(),
                       primitive.geometry.indices.end());

    // All requested indices must fit into the source's current, shared pool
    // page. This uses no Strata vertex or GPU geometry pool allocation.
    GeometryPoolIndexSpan span;
    if (!uploadAuxiliaryGeometryIndices(dev, model, indices.data(),
                                         indices.size(), span)) {
        ++stats_.indexDeclined;
        return false;
    }

    // Version 2 contains actual pooled GPU first-index offsets. Version 1
    // remains a pure CPU/local metadata format, never drawable. The metadata
    // and sidecar belong to one staged source lifetime and geometryRevision.
    GpuMetadataHeader header;
    std::memcpy(&header, payload.data(), sizeof(header));
    header.version = 2;
    std::memcpy(payload.data(), &header, sizeof(header));
    uint64_t primitiveIndexOffset = 0;
    for (size_t i = 0; i < asset.primitives.size(); ++i) {
        const size_t descriptorOffset =
            size_t(header.primitivesByteOffset) + i * sizeof(GpuPrimitive);
        GpuPrimitive gpuPrimitive;
        std::memcpy(&gpuPrimitive, payload.data() + descriptorOffset,
                    sizeof(gpuPrimitive));
        const uint64_t gpuFirst = uint64_t(span.firstIndex) +
                                  primitiveIndexOffset;
        if (gpuFirst > std::numeric_limits<uint32_t>::max()) {
            releaseAuxiliaryGeometryIndices(dev, span);
            ++stats_.indexFailed;
            return false;
        }
        gpuPrimitive.gpuFirstIndex = uint32_t(gpuFirst);
        std::memcpy(payload.data() + descriptorOffset, &gpuPrimitive,
                    sizeof(gpuPrimitive));
        primitiveIndexOffset += asset.primitives[i].geometry.indices.size();
    }

    gfx::BufferDesc desc;
    desc.type = gfx::BufferType::Storage;
    desc.usage = gfx::Usage::Immutable;
    desc.size = payload.size();
    desc.data = payload.data();
    desc.label = "Strata experimental GPU cluster metadata + pooled indices (no draws)";
    const gfx::BufferHandle h = dev.createBuffer(desc);
    if (!h.valid()) {
        releaseAuxiliaryGeometryIndices(dev, span);
        ++stats_.failed;
        warnings_.push_back("Strata GPU metadata upload failed: " + sourceKey);
        return false;
    }
    Entry record;
    record.metadata = h;
    record.indices = std::move(span);
    record.source = &model;
    record.sourceRevision = model.geometryRevision;
    record.sourceVb = model.vb;
    record.sourceIb = model.ib;
    record.bytes = payload.size();
    entries_.emplace(sourceKey, std::move(record));
    ++stats_.sources;
    ++stats_.indexedSources;
    stats_.bytes += payload.size();
    stats_.indexBytes += requestedIndexBytes;
    return true;
}

gfx::BufferHandle GpuMetadataScene::find(const std::string& key,
                                         const Model& model) const {
    const auto it = entries_.find(key);
    if (it == entries_.end()) return {};
    const Entry& e = it->second;
    return e.source == &model && model.geometryPoolPage &&
           model.geometryRevision == e.sourceRevision &&
           model.vb == e.sourceVb && model.ib == e.sourceIb &&
           model.vb.valid() && model.ib.valid() &&
           auxiliaryGeometryIndicesValidFor(e.indices, model)
           ? e.metadata : gfx::BufferHandle{};
}

bool GpuMetadataScene::indexBinding(const std::string& key,
                                    const Model& model,
                                    IndexBinding& out) const {
    const auto it = entries_.find(key);
    if (it == entries_.end() || !find(key, model).valid()) return false;
    const auto& span = it->second.indices;
    IndexBinding binding;
    binding.buffer = span.buffer;
    binding.firstIndex = span.firstIndex;
    binding.indexCount = span.indexCount;
    binding.baseVertex = int32_t(model.gpuBaseVertex);
    out = binding;
    return true;
}

void GpuMetadataScene::shutdown(gfx::Device& dev) {
    for (auto& [key, entry] : entries_) {
        if (entry.metadata.valid()) dev.destroyBuffer(entry.metadata);
        // Index spans are borrowed from the ORIGINAL shared Model page.
        // Release them before GeometryScene releases that Model.
        releaseAuxiliaryGeometryIndices(dev, entry.indices);
    }
    entries_.clear();
    warnings_.clear();
    stats_ = {};
}
} // namespace aiverse::render::strata
