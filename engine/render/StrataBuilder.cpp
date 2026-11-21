#include "StrataBuilder.h"
#include "StrataSeamTopology.h"
#include "meshoptimizer.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <iterator>
#include <limits>
#include <numeric>
#include <utility>

namespace aiverse::render::strata {
namespace {
struct BuildNode {
    ClusterNode node;
    ClusterBounds bounds;
    std::vector<uint32_t> children;
};
bool fail(std::string* error, const char* why) {
    if (error) *error = why;
    return false;
}
std::array<float, 3> position(const BuildInput& input, uint32_t id) {
    std::array<float, 3> value;
    const auto* data = reinterpret_cast<const uint8_t*>(input.positions) +
                       size_t(id) * input.positionStride;
    std::memcpy(value.data(), data, 12);
    return value;
}
void expand(ClusterBounds& a, const std::array<float, 3>& p) {
    for (unsigned k = 0; k < 3; ++k) {
        a.min[k] = std::min(a.min[k], p[k]);
        a.max[k] = std::max(a.max[k], p[k]);
    }
}
ClusterBounds emptyBounds() {
    ClusterBounds b;
    for (unsigned k = 0; k < 3; ++k) {
        b.min[k] = std::numeric_limits<float>::infinity();
        b.max[k] = -std::numeric_limits<float>::infinity();
    }
    return b;
}
float diagonal(const ClusterBounds& b) {
    double sum = 0;
    for (unsigned k = 0; k < 3; ++k) {
        const double v = double(b.max[k]) - double(b.min[k]);
        sum += v * v;
    }
    return float(std::sqrt(sum));
}
void merge(ClusterBounds& a, const ClusterBounds& b) {
    for (unsigned k = 0; k < 3; ++k) {
        a.min[k] = std::min(a.min[k], b.min[k]);
        a.max[k] = std::max(a.max[k], b.max[k]);
    }
}
} // namespace

bool build(const BuildInput& input, const BuildOptions& options,
           Asset& out, std::string* error) {
    if (error) error->clear();
    if (!input.indices || !input.positions || input.indexCount == 0 ||
        input.indexCount % 3 || input.indexCount > 3000000 ||
        input.vertexCount == 0 || input.vertexCount > 1000000 ||
        input.positionStride < 12 || input.positionStride % 4 ||
        input.positionStride > 4096 ||
        options.maxVertices < 3 || options.maxVertices > 255 ||
        options.maxTriangles < 1 || options.maxTriangles > 512 ||
        options.childrenPerNode < 2 || options.childrenPerNode > 8 ||
        options.maxLevels < 1 || options.maxLevels > 16 ||
        !std::isfinite(options.targetRatio) ||
        options.targetRatio < 0.1f || options.targetRatio > 0.95f ||
        !std::isfinite(options.relativeError) ||
        options.relativeError < 0 || options.relativeError > 1)
        return fail(error, "invalid Strata input dimensions or build options");

    const bool hasAttributes = input.attributes != nullptr;
    if (hasAttributes) {
        if (!input.attributeWeights || input.attributeComponents < 1 ||
            input.attributeComponents > 32 ||
            input.attributeStride < size_t(input.attributeComponents) * sizeof(float) ||
            input.attributeStride % sizeof(float) ||
            input.attributeStride > 4096)
            return fail(error, "invalid vertex attribute layout");
        for (uint32_t i = 0; i < input.attributeComponents; ++i)
            if (!std::isfinite(input.attributeWeights[i]) ||
                input.attributeWeights[i] <= 0)
                return fail(error, "invalid attribute simplification weights");
    } else if (input.attributeWeights || input.attributeComponents ||
               input.attributeStride) {
        return fail(error, "incomplete vertex attribute layout");
    }

    // Validate indices and finite source positions before calling meshoptimizer.
    // Check every input vertex, including unreferenced data, for predictable assets.
    for (size_t i = 0; i < input.vertexCount; ++i) {
        const auto p = position(input, uint32_t(i));
        for (float c : p)
            if (!std::isfinite(c))
                return fail(error, "non-finite vertex position");
        if (hasAttributes) {
            const auto* bytes = reinterpret_cast<const uint8_t*>(input.attributes) +
                                i * input.attributeStride;
            for (uint32_t c = 0; c < input.attributeComponents; ++c) {
                float component = 0;
                std::memcpy(&component, bytes + size_t(c) * sizeof(float),
                            sizeof(component));
                if (!std::isfinite(component))
                    return fail(error, "non-finite vertex attribute");
            }
        }
    }
    for (size_t i = 0; i < input.indexCount; ++i)
        if (input.indices[i] >= input.vertexCount)
            return fail(error, "triangle index out of range");

    Asset asset;
    std::vector<BuildNode> created;
    std::vector<uint32_t> frontier;
    std::vector<uint32_t> localVertices;
    std::vector<uint8_t> localTriangles;
    localVertices.reserve(options.maxVertices);
    localTriangles.reserve(size_t(options.maxTriangles) * 3);

    auto flush = [&]() {
        if (localTriangles.empty()) return;
        const uint32_t nodeId = uint32_t(created.size());
        ClusterNode node;
        node.firstIndex = uint32_t(asset.indices.size());
        node.indexCount = uint32_t(localTriangles.size());
        node.materialGroup = input.materialGroup;
        node.resident = true;
        ClusterBounds bounds = emptyBounds();
        for (uint32_t index : localVertices)
            expand(bounds, position(input, index));
        for (uint8_t local : localTriangles)
            asset.indices.push_back(localVertices[local]);
        Meshlet m;
        m.vertexOffset = uint32_t(asset.meshletVertices.size());
        m.vertexCount = uint32_t(localVertices.size());
        m.triangleOffset = uint32_t(asset.meshletTriangles.size());
        m.triangleCount = uint32_t(localTriangles.size() / 3);
        m.node = nodeId;
        asset.meshletVertices.insert(asset.meshletVertices.end(),
                                    localVertices.begin(), localVertices.end());
        asset.meshletTriangles.insert(asset.meshletTriangles.end(),
                                     localTriangles.begin(), localTriangles.end());
        asset.meshlets.push_back(m);
        created.push_back({node, bounds, {}});
        frontier.push_back(nodeId);
        localVertices.clear();
        localTriangles.clear();
    };

    for (size_t i = 0; i < input.indexCount; i += 3) {
        const std::array<uint32_t, 3> tri = {
            input.indices[i], input.indices[i + 1], input.indices[i + 2]
        };
        // Count unique new global vertices for the pending meshlet.
        uint32_t needed = 0;
        for (unsigned k = 0; k < 3; ++k) {
            if (std::find(localVertices.begin(), localVertices.end(), tri[k]) !=
                localVertices.end()) continue;
            bool earlier = false;
            for (unsigned j = 0; j < k; ++j) earlier |= tri[k] == tri[j];
            if (!earlier) ++needed;
        }
        if (!localTriangles.empty() &&
            (localTriangles.size() / 3 >= options.maxTriangles ||
             localVertices.size() + needed > options.maxVertices))
            flush();
        for (uint32_t id : tri) {
            auto it = std::find(localVertices.begin(), localVertices.end(), id);
            if (it == localVertices.end()) {
                localVertices.push_back(id);
                it = std::prev(localVertices.end());
            }
            localTriangles.push_back(uint8_t(it - localVertices.begin()));
        }
    }
    flush();

    // Group spatial neighbours at EVERY hierarchy level, not adjacent
    // triangles in import order. Imported indices are often material- or
    // exporter-sorted and can interleave distant islands. Combining those
    // islands produces enormous parent bounds, poor LOD and unstable culling.
    // Morton sorting changes only hierarchy grouping: leaf triangle order,
    // vertex IDs, seam-aware border locks and the source VB remain untouched.
    auto sortFrontierSpatially = [&]() {
        std::array<double, 3> low{
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::infinity()};
        std::array<double, 3> high{-low[0], -low[1], -low[2]};
        for (uint32_t id : frontier)
            for (unsigned k = 0; k < 3; ++k) {
                low[k] = std::min(low[k], double(created[id].bounds.min[k]));
                high[k] = std::max(high[k], double(created[id].bounds.max[k]));
            }
        std::vector<uint32_t> keys(created.size(), 0);
        for (uint32_t id : frontier) {
            uint32_t q[3]{};
            for (unsigned k = 0; k < 3; ++k) {
                const auto& b = created[id].bounds;
                const double center = (double(b.min[k]) + double(b.max[k])) * 0.5;
                const double extent = high[k] - low[k];
                const double normalized = extent > 0
                    ? std::clamp((center - low[k]) / extent, 0.0, 1.0) : 0.0;
                q[k] = uint32_t(normalized * 1023.0 + 0.5);
            }
            uint32_t morton = 0;
            for (uint32_t bit = 0; bit < 10; ++bit)
                for (uint32_t axis = 0; axis < 3; ++axis)
                    morton |= ((q[axis] >> bit) & 1u) << (bit * 3 + axis);
            keys[id] = morton;
        }
        std::stable_sort(frontier.begin(), frontier.end(),
                         [&](uint32_t a, uint32_t b) {
                             return keys[a] < keys[b];
                         });
    };

    // Build from leaves upward. A parent covers the exact union of its
    // children. meshopt_SimplifyLockBorder avoids collapsing mesh boundaries,
    // and reports an absolute geometric error for cumulative LOD error.
    for (uint32_t level = 0; level < options.maxLevels && frontier.size() > 1; ++level) {
        sortFrontierSpatially();
        std::vector<uint32_t> next;
        bool addedParent = false;
        for (size_t begin = 0; begin < frontier.size();
             begin += options.childrenPerNode) {
            const size_t end = std::min(frontier.size(),
                                        begin + options.childrenPerNode);
            if (end - begin == 1) {
                next.push_back(frontier[begin]);
                continue;
            }
            ClusterBounds bounds = emptyBounds();
            std::vector<uint32_t> source;
            float inheritedError = 0;
            for (size_t j = begin; j < end; ++j) {
                const BuildNode& child = created[frontier[j]];
                merge(bounds, child.bounds);
                inheritedError = std::max(inheritedError, child.node.geometricError);
                const size_t b = child.node.firstIndex, count = child.node.indexCount;
                source.insert(source.end(), asset.indices.begin() + b,
                              asset.indices.begin() + b + count);
            }
            const float size = diagonal(bounds);
            if (!std::isfinite(size))
                return fail(error, "geometry bounds overflow");
            const double tolerated = double(size) * options.relativeError;
            const float targetError = float(tolerated);
            if (!std::isfinite(targetError))
                return fail(error, "simplification error overflow");
            const size_t target = std::max<size_t>(3,
                (size_t(double(source.size()) * options.targetRatio) / 3) * 3);
            std::vector<uint32_t> simplified(source.size());
            float simplificationError = 0;
            const unsigned simplifyFlags = meshopt_SimplifyLockBorder |
                                           meshopt_SimplifyErrorAbsolute;
            const size_t count = hasAttributes
                ? meshopt_simplifyWithAttributes(
                    simplified.data(), source.data(), source.size(),
                    input.positions, input.vertexCount, input.positionStride,
                    input.attributes, input.attributeStride,
                    input.attributeWeights, input.attributeComponents, nullptr,
                    target, targetError, simplifyFlags, &simplificationError)
                : meshopt_simplify(
                    simplified.data(), source.data(), source.size(),
                    input.positions, input.vertexCount, input.positionStride,
                    target, targetError, simplifyFlags, &simplificationError);

            if (count < 3 || count % 3 || count > source.size() ||
                !std::isfinite(simplificationError) || simplificationError < 0)
                return fail(error, "meshoptimizer returned invalid cluster");
            // Keeping a parent with identical geometry would only increase
            // draw/index memory and could create an ineffective LOD chain.
            if (count >= source.size() * 9 / 10) {
                for (size_t j = begin; j < end; ++j)
                    next.push_back(frontier[j]);
                continue;
            }
            // The simplifier's border-lock flag is NOT a proof. Different
            // neighboring groups may select different parent/child LODs in
            // one camera, so certify that this candidate preserves the
            // EXACT original union contour by vertex ID and orientation.
            // A nonmanifold/degenerate candidate or a collapsed/split edge
            // stays at its drawable original children, never a cracked LOD.
            if (!sameSeamContour(source.data(),source.size(),
                                 simplified.data(),count,nullptr)) {
                ++asset.seamRejectedParents;
                for (size_t j = begin; j < end; ++j)
                    next.push_back(frontier[j]);
                continue;
            }
            if (asset.indices.size() + count > uint64_t(UINT32_MAX))
                return fail(error, "Strata index buffer limit exceeded");
            ClusterNode node;
            node.firstIndex = uint32_t(asset.indices.size());
            node.indexCount = uint32_t(count);
            node.materialGroup = input.materialGroup;
            node.geometricError = inheritedError + simplificationError;
            node.resident = true;
            if (!std::isfinite(node.geometricError))
                return fail(error, "cumulative LOD error overflow");
            asset.indices.insert(asset.indices.end(),
                                 simplified.begin(), simplified.begin() + count);
            BuildNode parent;
            parent.node = node;
            parent.bounds = bounds;
            for (size_t j = begin; j < end; ++j)
                parent.children.push_back(frontier[j]);
            next.push_back(uint32_t(created.size()));
            created.push_back(std::move(parent));
            addedParent = true;
        }
        frontier = std::move(next);
        if (!addedParent) break;
    }

    // Reorder as BFS, because the selector requires contiguous direct children
    // with IDs greater than their parent. Preserve original material partitions.
    std::vector<uint32_t> order = frontier;
    for (size_t i = 0; i < order.size(); ++i) {
        const auto& kids = created[order[i]].children;
        order.insert(order.end(), kids.begin(), kids.end());
    }
    if (order.size() != created.size())
        return fail(error, "Strata hierarchy has disconnected nodes");
    std::vector<uint32_t> remap(created.size(), invalidNode);
    for (uint32_t i = 0; i < order.size(); ++i)
        remap[order[i]] = i;
    asset.roots.reserve(frontier.size());
    for (uint32_t root : frontier) asset.roots.push_back(remap[root]);
    asset.nodes.reserve(order.size());
    asset.bounds.reserve(order.size());
    for (uint32_t oldId : order) {
        const auto& old = created[oldId];
        ClusterNode node = old.node;
        if (!old.children.empty()) {
            node.firstChild = remap[old.children.front()];
            node.childCount = uint32_t(old.children.size());
        }
        asset.nodes.push_back(node);
        asset.bounds.push_back(old.bounds);
    }
    for (Meshlet& m : asset.meshlets) m.node = remap[m.node];
    if (!validate(asset.nodes, asset.roots))
        return fail(error, "Strata hierarchy validation failed");
    SeamAudit audit{};
    // Full independent audit before exposing the asset: even if a future
    // optimizer/builder path bypasses the per-parent gate, production
    // cannot accept an unverified hierarchy. Unrepairable input stays on
    // the original Legacy source, never a partial cracked STRATA draw.
    if (!auditSeamHierarchy(asset,audit,error))
        return false;
    asset.seamCertified=true;
    out = std::move(asset);
    return true;
}
} // namespace aiverse::render::strata
