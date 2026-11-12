#include "LightmapUv.h"
#include <algorithm>
#include <cstring>

namespace aiverse::render {
namespace {
constexpr uint64_t kFnvBasis = 1469598103934665603ull, kFnvPrime = 1099511628211ull;
void mix(uint64_t& h, const void* p, size_t n) {
    const auto* b = static_cast<const uint8_t*>(p);
    for (size_t i = 0; i < n; ++i) { h ^= b[i]; h *= kFnvPrime; }
}
template <class T> void put(std::string& s, T v) { s.append(reinterpret_cast<const char*>(&v), sizeof v); }
struct Reader {
    const std::string& s; size_t at = 0; bool ok = true;
    template <class T> T get() {
        T v{};
        if (at + sizeof v > s.size()) { ok = false; return v; }
        std::memcpy(&v, s.data() + at, sizeof v); at += sizeof v;
        return v;
    }
};
} // namespace

void primitiveVertexRange(const Model& m, const Primitive& p, uint32_t& first, uint32_t& count) {
    first = p.firstVertex; count = p.vertexCount;
    if (count && size_t(first) + count <= m.vertices.size()) {
        bool inside = true;
        for (uint32_t i = 0; i < p.indexCount && inside; ++i) { const uint32_t v = m.indices[p.firstIndex + i]; inside = v >= first && v < first + count; }
        if (inside) return;
    }
    uint32_t lo = UINT32_MAX, hi = 0;
    for (uint32_t i = 0; i < p.indexCount && p.firstIndex + i < m.indices.size(); ++i) { const uint32_t v = m.indices[p.firstIndex + i]; lo = std::min(lo, v); hi = std::max(hi, v); }
    if (lo > hi) { first = 0; count = 0; return; }
    first = lo; count = hi - lo + 1;
}

bool meshHasLightmapUv(const Model& m, int mesh) {
    if (mesh < 0 || size_t(mesh) >= m.meshes.size() || m.meshes[size_t(mesh)].primitives.empty()) return false;
    for (const auto& p : m.meshes[size_t(mesh)].primitives) if (!p.lightmapUv && p.indexCount) return false;
    return true;
}

uint64_t lightmapSourceHash(const Model& m) {
    uint64_t h = kFnvBasis;
    for (const auto& mesh : m.meshes)
        for (const auto& p : mesh.primitives) {
            uint32_t first = 0, count = 0;
            primitiveVertexRange(m, p, first, count);
            mix(h, &count, 4); mix(h, &p.indexCount, 4);
            for (uint32_t i = 0; i < count && first + i < m.vertices.size(); ++i) {
                const auto& v = m.vertices[first + i];
                mix(h, &v.pos, sizeof v.pos); mix(h, &v.nrm, sizeof v.nrm);
            }
            for (uint32_t i = 0; i < p.indexCount && p.firstIndex + i < m.indices.size(); ++i) {
                const uint32_t rel = m.indices[p.firstIndex + i] - first;
                mix(h, &rel, 4);
            }
        }
    return h;
}

std::string encodeLightmapUvs(uint64_t sourceHash, const std::vector<LightmapUvMesh>& meshes) {
    std::string s = "LMUV";
    put<uint32_t>(s, 1); put<uint64_t>(s, sourceHash); put<uint32_t>(s, uint32_t(meshes.size()));
    for (const auto& m : meshes) {
        put<uint32_t>(s, uint32_t(m.mesh)); put<uint32_t>(s, uint32_t(m.prims.size()));
        for (const auto& p : m.prims) {
            put<uint32_t>(s, uint32_t(p.xref.size())); put<uint32_t>(s, uint32_t(p.indices.size()));
            for (size_t i = 0; i < p.xref.size(); ++i) { put<uint32_t>(s, p.xref[i]); put<uint16_t>(s, p.uv[i * 2]); put<uint16_t>(s, p.uv[i * 2 + 1]); }
            for (uint32_t i : p.indices) put<uint32_t>(s, i);
        }
    }
    return s;
}

void applyLightmapUvMeshes(Model& m, const std::vector<LightmapUvMesh>& meshes) {
    std::vector<const LightmapUvMesh*> byMesh(m.meshes.size(), nullptr);
    for (const auto& lm : meshes) if (lm.mesh >= 0 && size_t(lm.mesh) < m.meshes.size()) byMesh[size_t(lm.mesh)] = &lm;
    std::vector<ModelVertex> verts;
    std::vector<uint32_t> idx;
    verts.reserve(m.vertices.size()); idx.reserve(m.indices.size());
    // rebuild in the original primitive order so untouched primitives keep their data (offsets move)
    for (size_t mi = 0; mi < m.meshes.size(); ++mi)
        for (size_t pi = 0; pi < m.meshes[mi].primitives.size(); ++pi) {
            Primitive& p = m.meshes[mi].primitives[pi];
            const LightmapUvMesh* lm = byMesh[mi];
            uint32_t srcFirst = 0, srcCount = 0;
            primitiveVertexRange(m, p, srcFirst, srcCount);
            const uint32_t first = uint32_t(verts.size()), firstIdx = uint32_t(idx.size());
            if (lm && pi < lm->prims.size() && p.jointPalette.empty()) {
                const auto& lp = lm->prims[pi];
                for (size_t i = 0; i < lp.xref.size(); ++i) {
                    ModelVertex v = m.vertices[srcFirst + lp.xref[i]];
                    v.joints[0] = uint8_t(lp.uv[i * 2] & 255); v.joints[1] = uint8_t(lp.uv[i * 2] >> 8);
                    v.joints[2] = uint8_t(lp.uv[i * 2 + 1] & 255); v.joints[3] = uint8_t(lp.uv[i * 2 + 1] >> 8);
                    std::memset(v.weights, 0, 4);
                    verts.push_back(v);
                }
                for (uint32_t i : lp.indices) idx.push_back(first + i);
                p.vertexCount = uint32_t(lp.xref.size());
                p.lightmapUv = true;
                p.morphTargets = 0; p.morphDeltas.clear();
            } else {
                for (uint32_t i = 0; i < srcCount; ++i) verts.push_back(m.vertices[srcFirst + i]);
                for (uint32_t i = 0; i < p.indexCount; ++i) idx.push_back(m.indices[p.firstIndex + i] - srcFirst + first);
                p.vertexCount = srcCount;
            }
            p.firstVertex = first; p.firstIndex = firstIdx; p.indexCount = uint32_t(idx.size()) - firstIdx;
        }
    m.vertices = std::move(verts);
    m.indices = std::move(idx);
}

bool applyLightmapUvs(Model& m, const std::string& bytes, std::string* error) {
    auto fail = [&](const char* why) { if (error) *error = why; return false; };
    if (bytes.size() < 20 || bytes.compare(0, 4, "LMUV") != 0) return fail("not a lightmap UV file");
    Reader r{bytes, 4};
    if (r.get<uint32_t>() != 1) return fail("unknown lightmap UV version");
    if (r.get<uint64_t>() != lightmapSourceHash(m)) return fail("made for other geometry (re-bake to regenerate)");
    std::vector<LightmapUvMesh> meshes(r.get<uint32_t>());
    if (meshes.size() > m.meshes.size()) return fail("too many meshes");
    for (auto& lm : meshes) {
        lm.mesh = int(r.get<uint32_t>());
        if (lm.mesh < 0 || size_t(lm.mesh) >= m.meshes.size()) return fail("mesh index out of range");
        const auto& src = m.meshes[size_t(lm.mesh)].primitives;
        lm.prims.resize(r.get<uint32_t>());
        if (lm.prims.size() != src.size()) return fail("primitive count differs");
        for (size_t pi = 0; pi < lm.prims.size() && r.ok; ++pi) {
            auto& p = lm.prims[pi];
            uint32_t rangeFirst = 0, rangeCount = 0;
            primitiveVertexRange(m, src[pi], rangeFirst, rangeCount);
            const uint32_t nv = r.get<uint32_t>(), ni = r.get<uint32_t>();
            if (!r.ok || size_t(nv) * 8 + size_t(ni) * 4 > bytes.size() - r.at) return fail("truncated");
            p.xref.resize(nv); p.uv.resize(size_t(nv) * 2); p.indices.resize(ni);
            for (uint32_t i = 0; i < nv; ++i) {
                p.xref[i] = r.get<uint32_t>(); p.uv[i * 2] = r.get<uint16_t>(); p.uv[i * 2 + 1] = r.get<uint16_t>();
                if (p.xref[i] >= rangeCount) return fail("vertex reference out of range");
            }
            for (uint32_t i = 0; i < ni; ++i) if ((p.indices[i] = r.get<uint32_t>()) >= nv) return fail("index out of range");
        }
    }
    if (!r.ok) return fail("truncated");
    applyLightmapUvMeshes(m, meshes);
    return true;
}

} // namespace aiverse::render
