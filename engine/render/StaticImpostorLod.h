// Generic far LOD for static models: two crossed alpha-masked cards with a
// baked front (+Z) and side (+X) silhouette of the finest mesh. Built on the
// CPU at import time, appended as the last ModelLod of the base mesh, so it
// is an ordinary mesh: instancing, shadows and LOD cross-fade keep working.
// Opt-in per asset: "<model>.batching.json" {"impostor": {"maxPixels": 20}}.
#pragma once
#include "engine/render/Model.h"
#include "engine/render/ImpostorMaskBounds.h"
#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace aiverse::render {

struct StaticImpostorSettings {
    float maxPixels = 20.f; // switch to cards below this projected size
    int tile = 256;         // texels per card (height); width follows aspect
    ImpostorFit fit = ImpostorFit::Auto;
};

namespace static_impostor_detail {
inline const std::vector<uint8_t>* pixels(const Image& img) {
    if (!img.rgba.empty()) return &img.rgba;
    if (img.sharedRgba && !img.sharedRgba->empty()) return img.sharedRgba.get();
    return nullptr;
}
} // namespace static_impostor_detail

// Returns false (model unchanged) when the asset is not a single rigid node
// whose mesh already has an authored or generated LOD chain.
inline bool appendStaticImpostorLod(Model& m, StaticImpostorSettings s = {},
                                    std::string* why = nullptr) {
    auto fail = [&](const char* reason) { if (why) *why = reason; return false; };
    if (!std::isfinite(s.maxPixels) || s.maxPixels <= 0.f) return fail("invalid maxPixels");
    s.tile = std::clamp(s.tile, 64, 1024);
    if (!m.skins.empty() || !m.clips.empty()) return fail("animated or skinned model");
    int nodeIndex = -1;
    for (size_t n = 0; n < m.nodes.size(); ++n)
        if (m.nodes[n].mesh >= 0) {
            if (nodeIndex >= 0) return fail("more than one mesh node");
            nodeIndex = int(n);
        }
    if (nodeIndex < 0) return fail("no mesh node");
    const int baseIndex = m.nodes[size_t(nodeIndex)].mesh;
    if (baseIndex < 0 || size_t(baseIndex) >= m.meshes.size()) return fail("invalid base mesh");
    {
        const MeshDef& base = m.meshes[size_t(baseIndex)];
        for (const auto& p : base.primitives)
            if (p.morphTargets) return fail("morph targets");
        if (base.primitives.empty()) return fail("empty base mesh");
    }

    // Bounds of the finest mesh in its local space.
    float3 lo{1e30f, 1e30f, 1e30f}, hi{-1e30f, -1e30f, -1e30f};
    for (const auto& p : m.meshes[size_t(baseIndex)].primitives)
        for (uint32_t i = 0; i < p.indexCount; ++i) {
            const uint32_t v = m.indices[p.firstIndex + i];
            if (v >= m.vertices.size()) return fail("index out of range");
            const float3 q = m.vertices[v].pos;
            lo = {std::min(lo.x, q.x), std::min(lo.y, q.y), std::min(lo.z, q.z)};
            hi = {std::max(hi.x, q.x), std::max(hi.y, q.y), std::max(hi.z, q.z)};
        }
    const float spanX = hi.x - lo.x, spanY = hi.y - lo.y, spanZ = hi.z - lo.z;
    if (!(spanX > 1e-4f && spanY > 1e-4f && spanZ > 1e-4f)) return fail("degenerate bounds");
    const float cx = (lo.x + hi.x) * .5f, cz = (lo.z + hi.z) * .5f;

    // Card resolution: height = tile, width by aspect (rounded to 4).
    const int H = s.tile;
    const int WA = std::clamp((int(float(H) * spanX / spanY) + 3) & ~3, 16, H * 2);
    const int WB = std::clamp((int(float(H) * spanZ / spanY) + 3) & ~3, 16, H * 2);
    const int SS = 2;
    ImpostorMaskBounds masks[2];
    Image atlas;
    atlas.name = m.path + "#impostor";
    atlas.width = WA + WB;
    atlas.height = H;
    atlas.repeat = false;
    atlas.rgba.assign(size_t(atlas.width) * size_t(H) * 4, 0);

    const auto sample = [&](const Material& mat, float2 uv, float out[4]) {
        out[0] = mat.baseColor.x; out[1] = mat.baseColor.y; out[2] = mat.baseColor.z; out[3] = mat.baseColor.w;
        const auto* resolved=resolveMaterialImage(m,mat,&Material::baseTex);
        if(!resolved)return;
        const Image& img=*resolved;
        const auto* px = static_impostor_detail::pixels(img);
        if (!px || img.width <= 0 || img.height <= 0 ||
            size_t(img.width)>px->size()/4/size_t(img.height)) return;
        float u = uv.x * mat.uvScale.x + mat.uvOffset.x, v = uv.y * mat.uvScale.y + mat.uvOffset.y;
        u -= std::floor(u); v -= std::floor(v);
        const int x = std::clamp(int(u * float(img.width)), 0, img.width - 1);
        const int y = std::clamp(int(v * float(img.height)), 0, img.height - 1);
        const uint8_t* t = &(*px)[(size_t(y) * size_t(img.width) + size_t(x)) * 4];
        for (int k = 0; k < 3; ++k) out[k] *= std::pow(float(t[k]) / 255.f, 2.2f);
        out[3] *= float(t[3]) / 255.f;
    };

    // view 0: from +Z, screen u = (x-lo.x)/spanX;  view 1: from +X, u = (hi.z-z)/spanZ.
    for (int view = 0; view < 2; ++view) {
        const int W = view == 0 ? WA : WB, SW = W * SS, SH = H * SS;
        std::vector<float> depth(size_t(SW) * SH, 1e30f), color(size_t(SW) * SH * 3, 0.f);
        std::vector<uint8_t> cover(size_t(SW) * SH, 0);
        const float3 light = view == 0 ? float3{0.35f, 0.8f, 0.48f} : float3{0.48f, 0.8f, -0.35f};
        for (const auto& p : m.meshes[size_t(baseIndex)].primitives) {
            if (p.material < 0 || size_t(p.material) >= m.materials.size()) continue;
            const Material& mat = m.materials[size_t(p.material)];
            const float cutoff = mat.alpha == AlphaMode::Mask ? mat.alphaCutoff : 0.01f;
            for (uint32_t i = 0; i + 2 < p.indexCount; i += 3) {
                const ModelVertex* v[3];
                float sx[3], sy[3], sz[3];
                for (int k = 0; k < 3; ++k) {
                    v[k] = &m.vertices[m.indices[p.firstIndex + i + uint32_t(k)]];
                    const float3 q = v[k]->pos;
                    const float u = view == 0 ? (q.x - lo.x) / spanX : (hi.z - q.z) / spanZ;
                    sx[k] = u * float(SW);
                    sy[k] = (hi.y - q.y) / spanY * float(SH);
                    sz[k] = view == 0 ? -q.z : -q.x; // nearer the viewer = smaller
                }
                const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
                if (std::fabs(area) < 1e-6f) continue;
                const int x0 = std::max(0, int(std::floor(std::min({sx[0], sx[1], sx[2]}))));
                const int x1 = std::min(SW - 1, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
                const int y0 = std::max(0, int(std::floor(std::min({sy[0], sy[1], sy[2]}))));
                const int y1 = std::min(SH - 1, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
                for (int y = y0; y <= y1; ++y)
                    for (int x = x0; x <= x1; ++x) {
                        const float fx = float(x) + .5f, fy = float(y) + .5f;
                        const float w0 = ((sx[1] - fx) * (sy[2] - fy) - (sy[1] - fy) * (sx[2] - fx)) / area;
                        const float w1 = ((sx[2] - fx) * (sy[0] - fy) - (sy[2] - fy) * (sx[0] - fx)) / area;
                        const float w2 = 1.f - w0 - w1;
                        if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                        const float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
                        const size_t at = size_t(y) * size_t(SW) + size_t(x);
                        if (z >= depth[at]) continue;
                        const float2 uv{w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x,
                                        w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y};
                        float rgba[4];
                        sample(mat, uv, rgba);
                        if (rgba[3] < cutoff) continue;
                        float3 n{w0 * v[0]->nrm.x + w1 * v[1]->nrm.x + w2 * v[2]->nrm.x,
                                 w0 * v[0]->nrm.y + w1 * v[1]->nrm.y + w2 * v[2]->nrm.y,
                                 w0 * v[0]->nrm.z + w1 * v[1]->nrm.z + w2 * v[2]->nrm.z};
                        const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                        if (len > 1e-6f) { n.x /= len; n.y /= len; n.z /= len; }
                        const float ndl = std::fabs(n.x * light.x + n.y * light.y + n.z * light.z);
                        // Mild baked self-shading: the runtime card normal is flat.
                        const float shade = 0.62f + 0.38f * ndl;
                        depth[at] = z;
                        cover[at] = 1;
                        for (int k = 0; k < 3; ++k) color[at * 3 + size_t(k)] = rgba[k] * shade;
                    }
            }
        }
        // Downsample SSxSS -> card texels; bleed colour into empty texels so
        // mip filtering never pulls black into the silhouette edge.
        std::vector<float> rgb(size_t(W) * H * 3, 0.f), alpha(size_t(W) * H, 0.f);
        std::vector<uint8_t> has(size_t(W) * H, 0);
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                float r = 0, g = 0, b = 0; int c = 0;
                for (int j = 0; j < SS; ++j)
                    for (int i = 0; i < SS; ++i) {
                        const size_t at = size_t(y * SS + j) * size_t(SW) + size_t(x * SS + i);
                        if (!cover[at]) continue;
                        r += color[at * 3]; g += color[at * 3 + 1]; b += color[at * 3 + 2]; ++c;
                    }
                const size_t o = size_t(y) * size_t(W) + size_t(x);
                alpha[o] = float(c) / float(SS * SS);
                if (c) { rgb[o * 3] = r / float(c); rgb[o * 3 + 1] = g / float(c); rgb[o * 3 + 2] = b / float(c); has[o] = 1; }
            }
        for (int pass = 0; pass < 8; ++pass) {
            std::vector<uint8_t> next = has;
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    const size_t o = size_t(y) * size_t(W) + size_t(x);
                    if (has[o]) continue;
                    float r = 0, g = 0, b = 0; int c = 0;
                    for (int dy = -1; dy <= 1; ++dy)
                        for (int dx = -1; dx <= 1; ++dx) {
                            const int nx = x + dx, ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= W || ny >= H) continue;
                            const size_t q = size_t(ny) * size_t(W) + size_t(nx);
                            if (!has[q]) continue;
                            r += rgb[q * 3]; g += rgb[q * 3 + 1]; b += rgb[q * 3 + 2]; ++c;
                        }
                    if (c) { rgb[o * 3] = r / float(c); rgb[o * 3 + 1] = g / float(c); rgb[o * 3 + 2] = b / float(c); next[o] = 1; }
                }
            has.swap(next);
        }
        const int ox = view == 0 ? 0 : WA;
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x) {
                const size_t o = size_t(y) * size_t(W) + size_t(x);
                uint8_t* t = &atlas.rgba[(size_t(y) * size_t(atlas.width) + size_t(ox + x)) * 4];
                for (int k = 0; k < 3; ++k)
                    t[k] = uint8_t(std::clamp(std::pow(rgb[o * 3 + size_t(k)], 1.f / 2.2f), 0.f, 1.f) * 255.f + .5f);
                t[3] = uint8_t(std::clamp(alpha[o], 0.f, 1.f) * 255.f + .5f);
                if (t[3]) masks[view].include(x,y);
            }
    }

    // Two crossed cards in base-mesh local space.
    const int imageIndex = int(m.images.size());
    m.images.push_back(std::move(atlas));
    Material card{};
    card.name = "impostor";
    card.baseColor = {1, 1, 1, 1};
    card.roughness = 0.95f;
    card.alpha = AlphaMode::Mask;
    card.alphaCutoff = 0.5f;
    card.doubleSided = true;
    card.baseTex = imageIndex;
    const int materialIndex = int(m.materials.size());
    m.materials.push_back(card);


    const uint32_t firstVertex = uint32_t(m.vertices.size());
    const auto vert = [&](float3 p, float2 uv) {
        ModelVertex v{};
        v.pos = p;
        v.nrm = {0.f, 1.f, 0.f}; // lit like a canopy from above
        v.uv = uv;
        v.tangent = {1.f, 0.f, 0.f, 1.f};
        m.vertices.push_back(v);
    };
    for (int view=0; view<2; ++view) {
        const int w = view==0 ? WA : WB;
        const auto rect = masks[view].select(w,H,s.fit);
        for (int corner : {0,1,3,2}) {
            const int x = corner&1 ? rect.right : rect.left;
            const int y = corner&2 ? rect.bottom : rect.top;
            const float u = float(x)/w, v = float(y)/H;
            const float3 pos = view==0 ? float3{lo.x+spanX*u,hi.y-spanY*v,cz}
                                      : float3{cx,hi.y-spanY*v,hi.z-spanZ*u};
            vert(pos,{float((view==0 ? 0 : WA)+x)/(WA+WB),v});
        }
    }
    const uint32_t firstIndex = uint32_t(m.indices.size());
    for (uint32_t c = 0; c < 2; ++c) {
        const uint32_t b = firstVertex + c * 4;
        for (uint32_t i : {0u, 1u, 2u, 0u, 2u, 3u}) m.indices.push_back(b + i);
    }
    MeshDef mesh{};
    mesh.name = m.meshes[size_t(baseIndex)].name + "#impostor";
    Primitive prim{};
    prim.firstIndex = firstIndex;
    prim.indexCount = 12;
    prim.material = materialIndex;
    prim.firstVertex = firstVertex;
    prim.vertexCount = 8;
    prim.boundsMin = lo;
    prim.boundsMax = hi;
    mesh.primitives.push_back(std::move(prim));
    const int impostorIndex = int(m.meshes.size());
    m.meshes.push_back(std::move(mesh));

    // Last level of the chain: strictly below the previous threshold.
    MeshDef& base = m.meshes[size_t(baseIndex)];
    float threshold = s.maxPixels;
    if (!base.lods.empty())
        threshold = std::min(threshold, base.lods.back().maxPixels * 0.75f);
    ModelLod lod{};
    lod.mesh = impostorIndex;
    lod.maxPixels = threshold;
    base.lods.push_back(lod);
    m.finalize();
    return true;
}

} // namespace aiverse::render
