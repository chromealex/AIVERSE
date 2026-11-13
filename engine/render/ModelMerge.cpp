#include "ModelMerge.h"
#include <algorithm>
#include <cmath>
#include <map>

namespace aiverse::render {
namespace {
struct Range { uint32_t first = 0, count = 0, minVertex = 0, maxVertex = 0; };
Range rangeOf(const Model& m, const Primitive& p) {
    Range r{p.firstIndex, p.indexCount, ~0u, 0};
    for (uint32_t i = 0; i < p.indexCount; ++i) { const uint32_t v = m.indices.at(p.firstIndex + i); r.minVertex = std::min(r.minVertex, v); r.maxVertex = std::max(r.maxVertex, v); }
    if (!p.indexCount) r.minVertex = r.maxVertex = 0;
    return r;
}
float det3(const mat4& w) {
    return w.at(0,0) * (w.at(1,1) * w.at(2,2) - w.at(1,2) * w.at(2,1)) - w.at(0,1) * (w.at(1,0) * w.at(2,2) - w.at(1,2) * w.at(2,0)) +
           w.at(0,2) * (w.at(1,0) * w.at(2,1) - w.at(1,1) * w.at(2,0));
}
float3 safeNormalize(float3 v, float3 fallback) { const float l = length(v); return l > 1e-12f ? v * (1.0f / l) : fallback; }
}

bool shadowBatchCompatible(const MeshDef& mesh, const std::vector<Material>& materials) {
    if (!mesh.shadowBatch.indexCount || materials.empty()) return false;
    auto material = [&](int i) -> const Material& { return materials[size_t(std::clamp(i, 0, int(materials.size()) - 1))]; };
    const bool sided = material(mesh.shadowBatch.material).doubleSided;
    for (const auto& p : mesh.primitives) if (p.shadowBatched) {
        const auto& mt = material(p.material);
        if (mt.alpha != AlphaMode::Opaque || !mt.castShadow || mt.transparent || mt.shaderGraph || mt.doubleSided != sided) return false;
    }
    return true;
}

size_t prepareShadowBatches(Model& m) {
    if (m.vertices.empty() || m.indices.empty() || m.vb.valid() || m.ib.valid() ||
        m.geometry || !m.variants.empty() || !m.skins.empty()) return 0;
    size_t covered = 0;
    for (auto& mesh : m.meshes) {
        if (mesh.shadowBatch.indexCount || mesh.deformable) continue;
        if (std::any_of(mesh.primitives.begin(), mesh.primitives.end(), [](const Primitive& p) {
            return !p.jointPalette.empty() || p.morphTargets > 0 || !p.variantMaterial.empty();
        })) continue;
        std::vector<size_t> groups[2];
        for (size_t i = 0; i < mesh.primitives.size(); ++i) {
            const auto& p = mesh.primitives[i];
            if (!p.indexCount || p.material < 0 || size_t(p.material) >= m.materials.size()) continue;
            const auto& mt = m.materials[size_t(p.material)];
            if (mt.alpha == AlphaMode::Opaque && mt.castShadow && !mt.transparent && !mt.shaderGraph)
                groups[mt.doubleSided ? 1 : 0].push_back(i);
        }
        // MeshDef has one shadow batch: choose the face-culling group saving the most draws.
        auto& group = groups[groups[1].size() > groups[0].size() ? 1 : 0];
        if (group.size() < 2) continue;
        std::sort(group.begin(), group.end(), [&](size_t a, size_t b) {
            return mesh.primitives[a].firstIndex < mesh.primitives[b].firstIndex;
        });
        Primitive batch;
        batch.material = mesh.primitives[group[0]].material;
        batch.firstIndex = mesh.primitives[group[0]].firstIndex;
        batch.boundsMin = {1e30f, 1e30f, 1e30f}; batch.boundsMax = {-1e30f, -1e30f, -1e30f};
        bool contiguous = true;
        for (size_t i : group) {
            const auto& p = mesh.primitives[i];
            contiguous &= p.firstIndex == batch.firstIndex + batch.indexCount;
            batch.indexCount += p.indexCount;
            batch.boundsMin = min3(batch.boundsMin, p.boundsMin);
            batch.boundsMax = max3(batch.boundsMax, p.boundsMax);
        }
        if (!contiguous) {
            batch.firstIndex = uint32_t(m.indices.size());
            m.indices.reserve(m.indices.size() + batch.indexCount);
            for (size_t i : group) {
                const auto& p = mesh.primitives[i];
                for (uint32_t j = 0; j < p.indexCount; ++j) m.indices.push_back(m.indices[p.firstIndex + j]);
            }
        }
        mesh.shadowBatch = std::move(batch);
        for (size_t i : group) mesh.primitives[i].shadowBatched = true;
        covered += group.size();
    }
    return covered;
}

ModelMergeResult mergeStaticPrimitives(Model& m, const ModelMergeOptions& o) {
    ModelMergeResult r;
    auto skip = [&](const char* why) { r.reason = why; return r; };
    if (m.vertices.empty() || m.indices.empty()) return skip("no CPU geometry (merge before uploadModel)");
    if (!m.skins.empty() || !m.clips.empty()) return skip("skinned or animated");
    if (!m.variants.empty()) return skip("material variants");
    if (m.geometry) return skip("built geometry data (clusters, occluders)");
    const float3 extent = m.boundsMax - m.boundsMin;
    if (!(std::max({extent.x, extent.y, extent.z}) <= o.maxExtent)) return skip("larger than maxExtent");
    for (const auto& mesh : m.meshes) {
        if (!mesh.lods.empty()) return skip("mesh LODs");
        for (const auto& p : mesh.primitives)
            if (!p.jointPalette.empty() || p.morphTargets > 0 || !p.variantMaterial.empty()) return skip("skinned, morphing or variant primitives");
    }
    auto kept = [&](const Node& n) { return std::find(o.keepNodes.begin(), o.keepNodes.end(), n.name) != o.keepNodes.end(); };
    auto mergeable = [&](const Primitive& p) {
        const auto& mt = m.materials.at(size_t(p.material));
        return mt.alpha != AlphaMode::Blend && !mt.transparent;   // blended parts keep their own depth sort
    };
    // Groups: material -> (node, primitive) pairs that become one primitive.
    std::map<int, std::vector<std::pair<int, int>>> groups;
    for (size_t n = 0; n < m.nodes.size(); ++n) {
        const Node& node = m.nodes[n];
        if (node.mesh < 0) continue;
        const auto& mesh = m.meshes.at(size_t(node.mesh));
        r.primitivesBefore += mesh.primitives.size();
        for (size_t p = 0; p < mesh.primitives.size(); ++p) {
            if (kept(node) || !mergeable(mesh.primitives[p])) continue;
            groups[mesh.primitives[p].material].push_back({int(n), int(p)});
        }
    }
    // Nodes keep their mesh if any primitive stays (kept node or blended part): count them once.
    std::vector<bool> nodeKeepsMesh(m.nodes.size());
    for (size_t n = 0; n < m.nodes.size(); ++n) {
        const Node& node = m.nodes[n];
        if (node.mesh < 0) continue;
        bool keep = kept(node);
        for (const auto& p : m.meshes[size_t(node.mesh)].primitives) keep |= !mergeable(p);
        nodeKeepsMesh[n] = keep;
    }
    size_t after = 0;
    for (size_t n = 0; n < m.nodes.size(); ++n) if (nodeKeepsMesh[n]) after += m.meshes[size_t(m.nodes[n].mesh)].primitives.size();
    // Primitives of nodes that keep their mesh are drawn there; merging them too would draw them twice.
    for (auto& [material, items] : groups)
        items.erase(std::remove_if(items.begin(), items.end(), [&](const auto& it) { return nodeKeepsMesh[size_t(it.first)]; }), items.end());
    for (const auto& [material, items] : groups) if (!items.empty()) ++after;
    if (after >= r.primitivesBefore) return skip("merging does not reduce the draw count");

    std::vector<ModelVertex> vertices;std::vector<uint32_t> indices;std::vector<MeshDef> meshes;
    std::vector<int> meshRemap(m.meshes.size(), -1);
    // 1) meshes still used by nodes that keep them: copied with their vertex ranges.
    for (size_t n = 0; n < m.nodes.size(); ++n) {
        if (!nodeKeepsMesh[n]) continue;
        const int src = m.nodes[n].mesh;
        if (meshRemap[size_t(src)] >= 0) continue;
        MeshDef mesh = m.meshes[size_t(src)];
        for (auto& p : mesh.primitives) {
            const Range rg = rangeOf(m, p);
            const uint32_t base = uint32_t(vertices.size());
            vertices.insert(vertices.end(), m.vertices.begin() + rg.minVertex, m.vertices.begin() + (p.indexCount ? rg.maxVertex + 1 : rg.minVertex));
            p.firstIndex = uint32_t(indices.size());
            for (uint32_t i = 0; i < rg.count; ++i) indices.push_back(m.indices[rg.first + i] - rg.minVertex + base);
            p.firstVertex = base; p.vertexCount = uint32_t(vertices.size()) - base;
        }
        meshRemap[size_t(src)] = int(meshes.size());
        meshes.push_back(std::move(mesh));
    }
    // 2) one merged primitive per material, in model space (bind pose). Opaque shadow casters that
    // share the face culling come first, so one contiguous index range can draw all their shadows.
    auto casts = [&](int material) { const auto& mt = m.materials.at(size_t(material));
        return mt.alpha == AlphaMode::Opaque && mt.castShadow && !mt.shaderGraph; };
    std::vector<int> orderMaterials;
    int batchSided = -1;
    for (const auto& [material, items] : groups) if (!items.empty() && casts(material)) {
        const int sided = m.materials[size_t(material)].doubleSided ? 1 : 0;
        if (batchSided < 0) batchSided = sided;
        if (sided == batchSided) orderMaterials.push_back(material);
    }
    const size_t batchCount = orderMaterials.size();
    for (const auto& [material, items] : groups)
        if (!items.empty() && std::find(orderMaterials.begin(), orderMaterials.end(), material) == orderMaterials.end()) orderMaterials.push_back(material);
    MeshDef merged; merged.name = "merged";
    for (const int material : orderMaterials) {
        const auto& items = groups[material];
        Primitive out; out.material = material; out.firstIndex = uint32_t(indices.size()); out.firstVertex = uint32_t(vertices.size());
        for (const auto& [n, pi] : items) {
            const mat4& w = m.bindWorld.at(size_t(n));
            const mat4 inv = w.inverse();
            const bool mirrored = det3(w) < 0;
            const Primitive& p = m.meshes[size_t(m.nodes[size_t(n)].mesh)].primitives[size_t(pi)];
            const Range rg = rangeOf(m, p);
            const uint32_t base = uint32_t(vertices.size());
            for (uint32_t v = rg.minVertex; p.indexCount && v <= rg.maxVertex; ++v) {
                ModelVertex x = m.vertices[v];
                x.pos = w.transformPoint(x.pos);
                // normals use the inverse transpose (non-uniform scale)
                const float3 nn{inv.at(0,0) * x.nrm.x + inv.at(1,0) * x.nrm.y + inv.at(2,0) * x.nrm.z,
                                inv.at(0,1) * x.nrm.x + inv.at(1,1) * x.nrm.y + inv.at(2,1) * x.nrm.z,
                                inv.at(0,2) * x.nrm.x + inv.at(1,2) * x.nrm.y + inv.at(2,2) * x.nrm.z};
                x.nrm = safeNormalize(nn, x.nrm);
                const float3 t = safeNormalize(w.transformDir({x.tangent.x, x.tangent.y, x.tangent.z}), {1, 0, 0});
                x.tangent = {t.x, t.y, t.z, mirrored ? -x.tangent.w : x.tangent.w};
                vertices.push_back(x);
            }
            for (uint32_t i = 0; i + 3 <= rg.count; i += 3) {
                uint32_t a = m.indices[rg.first + i] - rg.minVertex + base, b = m.indices[rg.first + i + 1] - rg.minVertex + base,
                         c = m.indices[rg.first + i + 2] - rg.minVertex + base;
                if (mirrored) std::swap(b, c);   // keep front faces front after a mirroring transform
                indices.push_back(a); indices.push_back(b); indices.push_back(c);
            }
        }
        out.indexCount = uint32_t(indices.size()) - out.firstIndex;
        out.vertexCount = uint32_t(vertices.size()) - out.firstVertex;
        merged.primitives.push_back(std::move(out));
    }
    if (batchCount >= 2) {
        auto& sb = merged.shadowBatch;
        sb.material = merged.primitives[0].material;
        sb.firstIndex = merged.primitives[0].firstIndex;
        for (size_t i = 0; i < batchCount; ++i) { sb.indexCount += merged.primitives[i].indexCount; merged.primitives[i].shadowBatched = true; }
        r.shadowBatched = batchCount;
    }
    const int mergedMesh = int(meshes.size());
    meshes.push_back(std::move(merged));
    for (size_t n = 0; n < m.nodes.size(); ++n) {
        Node& node = m.nodes[n];
        if (node.mesh < 0) continue;
        node.mesh = nodeKeepsMesh[n] ? meshRemap[size_t(node.mesh)] : -1;
    }
    Node root; root.name = "merged"; root.mesh = mergedMesh;
    m.nodes.push_back(root);
    m.vertices = std::move(vertices); m.indices = std::move(indices); m.meshes = std::move(meshes);
    m.finalize();
    r.merged = true; r.primitivesAfter = after;
    return r;
}
}
