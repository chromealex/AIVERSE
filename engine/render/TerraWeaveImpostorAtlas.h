// TerraWeave multi-azimuth tree impostors. All azimuths share ONE GPU
// texture, with separate bark and leaf mask pages in the same atlas.
// Two co-located model primitives keep trunks opaque in winter; per-view
// MATERIAL overrides select the right image tile without shader changes.
// This preserves Renderer3D's normal model instancing/material grouping.
#pragma once

#include "engine/render/TerraWeaveArborBillboard.h"
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::terraweave {

struct ArborImpostorAtlasSettings {
    ArborBillboardSettings tile{};
    uint32_t azimuthViews = 8; // 4, 8, or 16
};

// Keep two materials (bark, foliage) per azimuth in ModelDraw::materials.
// These overrides MUST outlive all Renderer3D submissions of the frame.
struct ArborImpostorAtlas {
    std::unique_ptr<render::Model> model;
    std::vector<std::vector<render::Material>> viewMaterials;
    uint32_t views = 0;
};

inline ArborImpostorAtlas makeArborImpostorAtlas(
        const TreeProfile& profile, uint64_t seed,
        ArborImpostorAtlasSettings settings = {}) {
    const uint32_t views = settings.azimuthViews;
    const uint32_t width = settings.tile.width;
    const uint32_t height = settings.tile.height;
    const uint32_t gutter = settings.tile.margin;
    if ((views != 4 && views != 8 && views != 16) ||
        width < 32 || width > 512 || height < 32 || height > 1024 ||
        gutter < 2 || gutter * 3 >= width || gutter * 3 >= height ||
        uint64_t(width) * views * 2u > 8192u)
        throw std::invalid_argument("TerraWeave impostor atlas settings");

    // Reuse the generic quad geometry, then add a second coplanar-ish quad
    // for foliage, only 2 extra triangles. Bark/leaf share one GPU texture,
    // so foliage can become transparent without fading the trunk.
    ArborImpostorAtlas out;
    out.model = makeArborBillboardModel(profile, seed, settings.tile);
    out.views = views;
    out.model->path = "terraweave/arbor-impostor-atlas/" +
                      std::to_string(profile.speciesId) + "/" +
                      std::to_string(seed) + "/" + std::to_string(views);

    render::Image& atlas = out.model->images[0];
    atlas.name = "TerraWeave Arbor bark + foliage azimuth atlas";
    atlas.width = static_cast<int>(width * views * 2u);
    atlas.height = static_cast<int>(height);
    atlas.rgba.assign(static_cast<size_t>(atlas.width) * atlas.height * 4, 0);
    atlas.repeat = false;

    // Derive all viewpoints from the *same* full skeleton. Never regenerate
    // branches with a different seed as the camera azimuth changes.
    const TreeSkeleton tree = generateTree(profile, seed, TreeLOD::High);
    const float halfWidthCm = float(std::max<int32_t>(100, tree.crownRadiusCm));
    const float visualHeightCm = float(std::max<int32_t>(
        tree.heightCm, tree.heightCm + static_cast<int32_t>(profile.leafRadiusCm)));
    const float pixelsX = float(width / 2 - gutter) / halfWidthCm;
    const float pixelsY = float(height - 2 * gutter) / visualHeightCm;
    constexpr double tau = 6.283185307179586476925286766559;

    // Pixel masks use direct RGBA8 writes. Transparent texels stay black,
    // and each view receives its own padded tile (bilinear filtering cannot
    // sample into an adjacent view at the quad's inset UVs).
    for (uint32_t frame = 0; frame < views; ++frame) {
        const double angle = tau * double(frame) / double(views);
        const float c = static_cast<float>(std::cos(angle));
        const float s = static_cast<float>(std::sin(angle));
        // All bark azimuths occupy the first half, foliage the second.
        // Each independent tile retains its own bilinear-filter gutters.
        const int W = static_cast<int>(width), H = static_cast<int>(height);
        auto xScreen = [&](int32_t x, int32_t z) {
            return W / 2 + static_cast<int>(std::lround(
                (float(x) * c - float(z) * s) * pixelsX));
        };
        auto yScreen = [&](int32_t y) {
            return H - 1 - static_cast<int>(gutter) -
                   static_cast<int>(std::lround(float(y) * pixelsY));
        };
        auto disk = [&](uint32_t layer,int cx,int cy,int radius,
                        uint8_t red,uint8_t green,uint8_t blue) {
            const size_t tileLeft=size_t(layer*views+frame)*width;
            radius = std::clamp(radius, 1, std::max(W, H));
            const int minX = std::max(0, cx - radius);
            const int maxX = std::min(W - 1, cx + radius);
            const int minY = std::max(0, cy - radius);
            const int maxY = std::min(H - 1, cy + radius);
            for (int y = minY; y <= maxY; ++y) {
                for (int x = minX; x <= maxX; ++x) {
                    const int dx = x - cx, dy = y - cy;
                    if (dx * dx + dy * dy > radius * radius) continue;
                    const size_t at = (static_cast<size_t>(y) * atlas.width +
                                       tileLeft + static_cast<size_t>(x)) * 4;
                    atlas.rgba[at] = red;
                    atlas.rgba[at + 1] = green;
                    atlas.rgba[at + 2] = blue;
                    atlas.rgba[at + 3] = 255;
                }
            }
        };
        // Bake bark and foliage as separate opaque cutout masks. The
        // renderer later overlays foliage ahead of bark, and seasons only
        // change the foliage primitive's material.
        for (const TreeBranch& branch : tree.branches) {
            const int x0 = xScreen(branch.x0, branch.z0);
            const int y0 = yScreen(branch.y0);
            const int x1 = xScreen(branch.x1, branch.z1);
            const int y1 = yScreen(branch.y1);
            const int steps = std::max(1, std::max(std::abs(x1 - x0),
                                                   std::abs(y1 - y0)));
            for (int step = 0; step <= steps; ++step) {
                const int x = x0 + (x1 - x0) * step / steps;
                const int y = y0 + (y1 - y0) * step / steps;
                const int32_t radiusCm = branch.startRadiusCm +
                    (branch.endRadiusCm - branch.startRadiusCm) * step / steps;
                disk(0u,x,y,std::max(1,static_cast<int>(std::lround(
                     float(radiusCm)*pixelsX))),103,64,38);
            }
        }
        for (const LeafCluster& leaf : tree.leaves) {
            const uint8_t v = static_cast<uint8_t>(leaf.variation % 29);
            disk(1u,xScreen(leaf.xCm,leaf.zCm),yScreen(leaf.yCm),
                 std::max(1,static_cast<int>(std::lround(
                     float(leaf.radiusCm)*std::min(pixelsX,pixelsY)))),
                 static_cast<uint8_t>(34+v),static_cast<uint8_t>(88+v),
                 static_cast<uint8_t>(27+v/2));
        }
    }

    // One model, one texture, two independent alpha-masked materials.
    // A tiny front offset keeps foliage ahead of bark under depth testing,
    // while the root-origin transform and camera-facing yaw remain shared.
    auto& model=*out.model;
    if(model.vertices.size()!=4 || model.indices.size()!=6 ||
       model.meshes.size()!=1 || model.meshes[0].primitives.size()!=1 ||
       model.materials.size()!=1)
        throw std::logic_error("TerraWeave impostor expected one source quad");
    render::Material bark=model.materials[0];
    bark.name="TerraWeave impostor bark";
    bark.baseTex=0;
    bark.alpha=render::AlphaMode::Mask;
    render::Material leaves=bark;
    leaves.name="TerraWeave impostor foliage";
    // Vertex alpha stores GPU wind height weight on foliage only.
    // Do not use it for cutout opacity: the atlas alpha remains authoritative.
    leaves.vertexColors=false;
    model.materials={std::move(bark),std::move(leaves)};
    for(size_t i=0;i<4;++i){
        auto vertex=model.vertices[i];
        vertex.pos.z+=0.02f; // 2cm toward camera reduces far-LOD depth fighting
        vertex.color=(i>=2 ? 0xff000000u : 0u)|0x00ffffffu;
        model.vertices.push_back(vertex);
    }
    model.indices.insert(model.indices.end(),{4u,5u,6u,4u,6u,7u});
    model.meshes[0].primitives[0].material=0;
    auto foliagePrimitive=model.meshes[0].primitives[0];
    foliagePrimitive.material=1;
    foliagePrimitive.firstVertex=4;
    foliagePrimitive.vertexCount=4;
    foliagePrimitive.firstIndex=6;
    foliagePrimitive.indexCount=6;
    model.meshes[0].primitives.push_back(foliagePrimitive);
    // makeArborBillboardModel already finalized a single-quad model.
    // Recompute its bounds/material batches after the two-layer conversion.
    model.finalize();

    // Two material slots per view, matching Primitive.material 0/1.
    // Both materials sample disjoint regions of the SAME atlas image.
    out.viewMaterials.reserve(views);
    for(uint32_t frame=0;frame<views;++frame){
        auto materials=model.materials;
        for(uint32_t layer=0;layer<2u;++layer){
            auto& m=materials[layer];
            const float atlasWidth=float(width*views*2u);
            const float tileX=float((layer*views+frame)*width);
            m.uvOffset.x=(tileX+0.5f)/atlasWidth;
            m.uvOffset.y=0.5f/float(height);
            m.uvScale.x=(float(width)-1.0f)/atlasWidth;
            m.uvScale.y=(float(height)-1.0f)/float(height);
        }
        out.viewMaterials.push_back(std::move(materials));
    }
    return out;
}


// Re-bake an impostor atlas from the REAL near tree: CPU rasterization of
// `nearModel` (every node's base mesh, model space in metres, identity node
// transforms as built by Arbor) with its base-colour textures and alpha
// cutout, 2x2 supersampled, simple wrap lighting. Bark triangles (material 0)
// go to the bark page, foliage (material 1) to the foliage page, so seasons
// and winter bark-only draws keep working. The quad is resized to the mesh's
// real extent. Images must still hold CPU pixels (call before GPU upload).
// Deterministic: same model -> same atlas bytes.
inline void bakeArborImpostorFromModel(ArborImpostorAtlas& out,
        const render::Model& nearModel, ArborImpostorAtlasSettings settings = {}) {
    if (!out.model || out.views == 0 || out.model->images.empty() ||
        out.model->vertices.size() != 8)
        throw std::invalid_argument("TerraWeave impostor atlas not built");
    const uint32_t views = out.views;
    const int W = int(settings.tile.width), H = int(settings.tile.height);
    const int gutter = int(settings.tile.margin);
    render::Image& atlas = out.model->images[0];
    if (atlas.width != W * int(views) * 2 || atlas.height != H)
        throw std::invalid_argument("TerraWeave impostor atlas size");

    // Real extent of the near geometry (metres).
    float radius = 0.5f, top = 1.0f;
    for (const auto& node : nearModel.nodes) {
        if (node.mesh < 0) continue;
        for (const auto& p : nearModel.meshes[size_t(node.mesh)].primitives)
            for (uint32_t i = p.firstVertex; i < p.firstVertex + p.vertexCount && i < nearModel.vertices.size(); ++i) {
                const auto& v = nearModel.vertices[i].pos;
                radius = std::max(radius, std::sqrt(v.x * v.x + v.z * v.z));
                top = std::max(top, v.y);
            }
    }
    // Resize both coplanar quads (bark 0..3, foliage 4..7) to that extent.
    for (size_t i = 0; i < 8; ++i) {
        auto& pos = out.model->vertices[i].pos;
        pos.x = pos.x < 0 ? -radius : radius;
        pos.y = pos.y > 0.001f ? top : 0.0f;
    }
    out.model->finalize();

    const int SS = 2, SW = W * SS, SH = H * SS;
    const float pxX = float(SW / 2 - gutter * SS) / radius;
    const float pxY = float(SH - 2 * gutter * SS) / top;
    std::fill(atlas.rgba.begin(), atlas.rgba.end(), uint8_t(0));
    std::vector<float> depth(size_t(SW) * SH), color(size_t(SW) * SH * 3);
    std::vector<uint8_t> cover(size_t(SW) * SH);
    constexpr double tau = 6.283185307179586476925286766559;
    auto sample = [&](const render::Material& m, render::float2 uv, float rgba[4]) {
        rgba[0] = m.baseColor.x; rgba[1] = m.baseColor.y; rgba[2] = m.baseColor.z; rgba[3] = m.baseColor.w;
        if (m.baseTex < 0 || size_t(m.baseTex) >= nearModel.images.size()) return;
        const auto& img = nearModel.images[size_t(m.baseTex)];
        if (img.width <= 0 || img.rgba.size() < size_t(img.width) * img.height * 4) return;
        float u = uv.x * m.uvScale.x + m.uvOffset.x, v = uv.y * m.uvScale.y + m.uvOffset.y;
        u -= std::floor(u); v -= std::floor(v);
        const int x = std::clamp(int(u * float(img.width)), 0, img.width - 1);
        const int y = std::clamp(int(v * float(img.height)), 0, img.height - 1);
        const uint8_t* t = &img.rgba[(size_t(y) * img.width + x) * 4];
        // sRGB texel -> linear-ish (gamma 2.2) so lighting mixes sensibly.
        for (int k = 0; k < 3; ++k) rgba[k] *= std::pow(float(t[k]) / 255.f, 2.2f);
        rgba[3] *= float(t[3]) / 255.f;
    };
    for (uint32_t frame = 0; frame < views; ++frame) {
        const double angle = tau * double(frame) / double(views);
        const float c = float(std::cos(angle)), sn = float(std::sin(angle));
        // Screen right = (c, 0, -s) as in makeArborImpostorAtlas; camera
        // forward = (-s, 0, -c): smaller depth = nearer the camera.
        const render::float3 toCamera{sn, 0.f, c};
        const render::float3 light = [&] {
            render::float3 l{sn * 0.6f + c * 0.35f, 0.75f, c * 0.6f - sn * 0.35f};
            const float n = std::sqrt(l.x * l.x + l.y * l.y + l.z * l.z);
            return render::float3{l.x / n, l.y / n, l.z / n};
        }();
        for (int layer = 0; layer < 2; ++layer) {
            std::fill(depth.begin(), depth.end(), 1e30f);
            std::fill(cover.begin(), cover.end(), uint8_t(0));
            for (const auto& node : nearModel.nodes) {
                if (node.mesh < 0) continue;
                for (const auto& p : nearModel.meshes[size_t(node.mesh)].primitives) {
                    if (p.material != layer || size_t(p.material) >= nearModel.materials.size()) continue;
                    const auto& mat = nearModel.materials[size_t(p.material)];
                    const float cutoff = mat.alpha == render::AlphaMode::Mask ? mat.alphaCutoff : 0.01f;
                    for (uint32_t i = 0; i + 2 < p.indexCount; i += 3) {
                        const render::ModelVertex* v[3];
                        float sx[3], sy[3], sz[3];
                        bool ok = true;
                        for (int k = 0; k < 3; ++k) {
                            const uint32_t idx = nearModel.indices[p.firstIndex + i + uint32_t(k)];
                            if (idx >= nearModel.vertices.size()) { ok = false; break; }
                            v[k] = &nearModel.vertices[idx];
                            const auto& q = v[k]->pos;
                            sx[k] = float(SW) * 0.5f + (q.x * c - q.z * sn) * pxX;
                            sy[k] = float(SH - 1 - gutter * SS) - q.y * pxY;
                            sz[k] = -(q.x * sn + q.z * c);
                        }
                        if (!ok) continue;
                        const float area = (sx[1] - sx[0]) * (sy[2] - sy[0]) - (sy[1] - sy[0]) * (sx[2] - sx[0]);
                        if (std::fabs(area) < 1e-6f) continue;
                        const int x0 = std::max(0, int(std::floor(std::min({sx[0], sx[1], sx[2]}))));
                        const int x1 = std::min(SW - 1, int(std::ceil(std::max({sx[0], sx[1], sx[2]}))));
                        const int y0 = std::max(0, int(std::floor(std::min({sy[0], sy[1], sy[2]}))));
                        const int y1 = std::min(SH - 1, int(std::ceil(std::max({sy[0], sy[1], sy[2]}))));
                        for (int y = y0; y <= y1; ++y)
                            for (int x = x0; x <= x1; ++x) {
                                const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                                const float w0 = ((sx[1] - px) * (sy[2] - py) - (sy[1] - py) * (sx[2] - px)) / area;
                                const float w1 = ((sx[2] - px) * (sy[0] - py) - (sy[2] - py) * (sx[0] - px)) / area;
                                const float w2 = 1.f - w0 - w1;
                                if (w0 < 0 || w1 < 0 || w2 < 0) continue;
                                const float z = w0 * sz[0] + w1 * sz[1] + w2 * sz[2];
                                const size_t at = size_t(y) * SW + x;
                                if (z >= depth[at]) continue;
                                render::float2 uv{w0 * v[0]->uv.x + w1 * v[1]->uv.x + w2 * v[2]->uv.x,
                                                  w0 * v[0]->uv.y + w1 * v[1]->uv.y + w2 * v[2]->uv.y};
                                float rgba[4];
                                sample(mat, uv, rgba);
                                if (rgba[3] < cutoff) continue;
                                render::float3 n{w0 * v[0]->nrm.x + w1 * v[1]->nrm.x + w2 * v[2]->nrm.x,
                                                 w0 * v[0]->nrm.y + w1 * v[1]->nrm.y + w2 * v[2]->nrm.y,
                                                 w0 * v[0]->nrm.z + w1 * v[1]->nrm.z + w2 * v[2]->nrm.z};
                                const float len = std::sqrt(n.x * n.x + n.y * n.y + n.z * n.z);
                                if (len > 1e-6f) { n.x /= len; n.y /= len; n.z /= len; }
                                // Two-sided cards: face the normal toward the camera.
                                if (n.x * toCamera.x + n.z * toCamera.z < 0 && layer == 1) { n.x = -n.x; n.y = -n.y; n.z = -n.z; }
                                const float ndl = n.x * light.x + n.y * light.y + n.z * light.z;
                                // Self-shadow inside the crown: deeper (farther) texels darker.
                                const float shade = 0.42f + 0.58f * std::clamp(ndl * 0.5f + 0.5f, 0.f, 1.f);
                                depth[at] = z;
                                cover[at] = 1;
                                for (int k = 0; k < 3; ++k) color[at * 3 + size_t(k)] = rgba[k] * shade;
                            }
                    }
                }
            }
            // 2x2 resolve into this view's tile; alpha = coverage >= 2/4.
            const size_t tileLeft = size_t(layer * int(views) + int(frame)) * size_t(W);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x) {
                    float sum[3] = {0, 0, 0};
                    int n = 0;
                    for (int dy = 0; dy < SS; ++dy)
                        for (int dx = 0; dx < SS; ++dx) {
                            const size_t at = size_t(y * SS + dy) * SW + size_t(x * SS + dx);
                            if (!cover[at]) continue;
                            ++n;
                            for (int k = 0; k < 3; ++k) sum[k] += color[at * 3 + size_t(k)];
                        }
                    if (n == 0) continue;
                    const size_t o = (size_t(y) * size_t(atlas.width) + tileLeft + size_t(x)) * 4;
                    for (int k = 0; k < 3; ++k)
                        atlas.rgba[o + size_t(k)] = uint8_t(std::clamp(
                            std::pow(sum[k] / float(n), 1.f / 2.2f) * 255.f + 0.5f, 0.f, 255.f));
                    atlas.rgba[o + 3] = n * 2 >= SS * SS ? 255 : 0;
                }
            // Bleed colour into transparent texels (alpha stays 0) so mips and
            // bilinear filtering do not pull black halos into the silhouette.
            std::vector<uint8_t> filled(size_t(W) * H);
            for (int y = 0; y < H; ++y)
                for (int x = 0; x < W; ++x)
                    filled[size_t(y) * W + x] = atlas.rgba[(size_t(y) * atlas.width + tileLeft + x) * 4 + 3] ||
                        atlas.rgba[(size_t(y) * atlas.width + tileLeft + x) * 4] ||
                        atlas.rgba[(size_t(y) * atlas.width + tileLeft + x) * 4 + 1];
            for (int pass = 0; pass < 6; ++pass) {
                std::vector<uint8_t> next = filled;
                for (int y = 0; y < H; ++y)
                    for (int x = 0; x < W; ++x) {
                        if (filled[size_t(y) * W + x]) continue;
                        int sum[3] = {0, 0, 0}, n = 0;
                        for (int dy = -1; dy <= 1; ++dy)
                            for (int dx = -1; dx <= 1; ++dx) {
                                const int nx = x + dx, ny = y + dy;
                                if (nx < 0 || ny < 0 || nx >= W || ny >= H || !filled[size_t(ny) * W + nx]) continue;
                                const size_t q = (size_t(ny) * atlas.width + tileLeft + nx) * 4;
                                for (int k = 0; k < 3; ++k) sum[k] += atlas.rgba[q + size_t(k)];
                                ++n;
                            }
                        if (!n) continue;
                        const size_t o = (size_t(y) * atlas.width + tileLeft + x) * 4;
                        for (int k = 0; k < 3; ++k) atlas.rgba[o + size_t(k)] = uint8_t(sum[k] / n);
                        next[size_t(y) * W + x] = 1;
                    }
                filled.swap(next);
            }
        }
    }
    // Textured bake: the atlas already carries the albedo; keep the impostor
    // materials untinted, and copy the near foliage cutout threshold.
    for (auto& variant : out.viewMaterials)
        for (auto& m : variant) { m.baseColor = {1, 1, 1, 1}; m.alphaCutoff = 0.5f; }
    for (auto& m : out.model->materials) { m.baseColor = {1, 1, 1, 1}; m.alphaCutoff = 0.5f; }
}

// Nearest captured azimuth relative to the *tree's local yaw*.
// The quad itself always faces the camera; the captured view determines
// which side of the deterministic original tree is visible on the card.
inline uint32_t selectArborImpostorAzimuth(uint32_t instanceYawMilliDegrees,
                                          int64_t treeXcm, int64_t treeZcm,
                                          int64_t cameraXcm, int64_t cameraZcm,
                                          uint32_t views) {
    if ((views != 4 && views != 8 && views != 16) ||
        instanceYawMilliDegrees >= 360000 ||
        treeXcm < -kWorldLimitCm || treeXcm > kWorldLimitCm ||
        treeZcm < -kWorldLimitCm || treeZcm > kWorldLimitCm ||
        cameraXcm < -kWorldLimitCm || cameraXcm > kWorldLimitCm ||
        cameraZcm < -kWorldLimitCm || cameraZcm > kWorldLimitCm)
        throw std::invalid_argument("TerraWeave impostor selection");
    constexpr double tau = 6.283185307179586476925286766559;
    // Avoid floating-point subtraction of large absolute world coordinates.
    const double dx = double(cameraXcm - treeXcm);
    const double dz = double(cameraZcm - treeZcm);
    const double treeYaw = double(instanceYawMilliDegrees) * tau / 360000.0;
    double relative = std::atan2(dx, dz) - treeYaw;
    relative = std::fmod(relative, tau);
    if (relative < 0) relative += tau;
    return static_cast<uint32_t>(std::floor(
        relative * double(views) / tau + 0.5)) % views;
}

} // namespace aiverse::terraweave
