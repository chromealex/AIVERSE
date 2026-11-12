#include "LightmapUnwrap.h"
#include <algorithm>
#include <cmath>
#include "xatlas.h"

namespace aiverse::bake {

int unwrapLightmapUvs(render::Model& m, float texelsPerUnit, int padding, std::string& sidecar, std::string& error) {
    sidecar.clear();
    if (m.vertices.empty()) { error = m.path + ": no CPU geometry to unwrap"; return 0; }
    const uint64_t sourceHash = render::lightmapSourceHash(m);
    std::vector<render::LightmapUvMesh> out;
    for (size_t mi = 0; mi < m.meshes.size(); ++mi) {
        const auto& mesh = m.meshes[mi];
        if (mesh.primitives.empty() || render::meshHasLightmapUv(m, int(mi))) continue;
        bool skinned = false;
        for (const auto& p : mesh.primitives) skinned |= !p.jointPalette.empty();
        if (skinned) continue;   // skinned meshes are lit by probes
        xatlas::Atlas* atlas = xatlas::Create();
        double area = 0;
        std::vector<std::vector<uint32_t>> local(mesh.primitives.size());
        bool failed = false;
        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
            const auto& p = mesh.primitives[pi];
            uint32_t first = 0, count = 0;
            render::primitiveVertexRange(m, p, first, count);
            auto& li = local[pi];
            li.reserve(p.indexCount);
            for (uint32_t i = 0; i < p.indexCount; ++i) li.push_back(m.indices[p.firstIndex + i] - first);
            for (size_t t = 0; t + 2 < li.size(); t += 3) {
                const auto& a = m.vertices[first + li[t]].pos, &b = m.vertices[first + li[t + 1]].pos, &c = m.vertices[first + li[t + 2]].pos;
                area += 0.5 * double(render::length(render::cross(b - a, c - a)));
            }
            xatlas::MeshDecl d;
            if (count == 0 || li.empty()) { error = m.path + ": mesh '" + mesh.name + "': empty primitive"; failed = true; break; }
            d.vertexCount = count;
            d.vertexPositionData = &m.vertices[first].pos;
            d.vertexPositionStride = sizeof(render::ModelVertex);
            d.vertexNormalData = &m.vertices[first].nrm;
            d.vertexNormalStride = sizeof(render::ModelVertex);
            d.indexCount = uint32_t(li.size());
            d.indexData = li.data();
            d.indexFormat = xatlas::IndexFormat::UInt32;
            const auto err = xatlas::AddMesh(atlas, d, uint32_t(mesh.primitives.size()));
            if (err != xatlas::AddMeshError::Success) { error = m.path + ": mesh '" + mesh.name + "': " + xatlas::StringForEnum(err); failed = true; break; }
        }
        if (failed || area <= 0) { xatlas::Destroy(atlas); if (failed) return -1; continue; }
        xatlas::ChartOptions co;
        co.maxIterations = 2;
        xatlas::PackOptions po;
        po.padding = uint32_t(std::clamp(padding, 1, 8));
        po.bilinear = true;
        // single atlas sized for the expected lightmap density (padding then means texels in the bake)
        po.texelsPerUnit = std::max(0.05f, texelsPerUnit);
        po.resolution = 0;
        xatlas::Generate(atlas, co, po);
        const float dim = float(std::max(atlas->width, atlas->height));
        if (atlas->meshCount != mesh.primitives.size() || dim <= 0) { xatlas::Destroy(atlas); continue; }
        render::LightmapUvMesh lm;
        lm.mesh = int(mi);
        lm.prims.resize(mesh.primitives.size());
        for (size_t pi = 0; pi < mesh.primitives.size(); ++pi) {
            const xatlas::Mesh& xm = atlas->meshes[pi];
            auto& lp = lm.prims[pi];
            lp.xref.resize(xm.vertexCount); lp.uv.resize(size_t(xm.vertexCount) * 2);
            for (uint32_t v = 0; v < xm.vertexCount; ++v) {
                lp.xref[v] = xm.vertexArray[v].xref;
                for (int k = 0; k < 2; ++k)
                    lp.uv[size_t(v) * 2 + size_t(k)] = uint16_t(std::clamp(xm.vertexArray[v].uv[k] / dim, 0.0f, 1.0f) * 65535.0f + 0.5f);
            }
            lp.indices.assign(xm.indexArray, xm.indexArray + xm.indexCount);
        }
        xatlas::Destroy(atlas);
        out.push_back(std::move(lm));
    }
    if (out.empty()) return 0;
    sidecar = render::encodeLightmapUvs(sourceHash, out);
    render::applyLightmapUvMeshes(m, out);
    return int(out.size());
}

} // namespace aiverse::bake
