// TerraWeave far-forest impostor: bake a deterministic tree silhouette
// into RGBA8, then render ONE camera-facing alpha-masked quad per tree.
// CPU-only construction; upload through standard render::uploadModel.
#pragma once

#include "engine/world/TerraWeaveArbor.h"
#include "engine/render/Model.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace aiverse::terraweave {

struct ArborBillboardSettings {
    uint32_t width = 128;
    uint32_t height = 256;
    uint32_t margin = 5;
};

inline std::unique_ptr<render::Model> makeArborBillboardModel(
        const TreeProfile& profile, uint64_t seed,
        ArborBillboardSettings settings = {}) {
    if (settings.width < 32 || settings.width > 1024 ||
        settings.height < 32 || settings.height > 1024 ||
        settings.margin < 2 || settings.margin * 3 >= settings.width ||
        settings.margin * 3 >= settings.height)
        throw std::invalid_argument("TerraWeave billboard resolution");

    const TreeSkeleton tree = generateTree(profile, seed, TreeLOD::High);
    const int32_t halfWidthCm = std::max<int32_t>(100, tree.crownRadiusCm);
    const int32_t visualHeightCm = std::max<int32_t>(
        tree.heightCm, tree.heightCm + static_cast<int32_t>(profile.leafRadiusCm));
    const float widthMetres = float(halfWidthCm) * 0.02f;
    const float heightMetres = float(visualHeightCm) * 0.01f;
    const int32_t W = static_cast<int32_t>(settings.width);
    const int32_t H = static_cast<int32_t>(settings.height);
    const float pxPerCmX = float(W / 2 - static_cast<int32_t>(settings.margin)) /
                           float(halfWidthCm);
    const float pxPerCmY = float(H - 2 * settings.margin) /
                           float(visualHeightCm);

    render::Image image;
    image.name = "TerraWeave Arbor silhouette";
    image.width = W;
    image.height = H;
    image.repeat = false;
    image.nearest = false;
    image.rgba.resize(static_cast<size_t>(W) * H * 4, 0);

    // Fixed side-projection, with an oblique contribution from Z to retain
    // volume in the pre-baked silhouette. No dependency on camera/gameplay.
    const auto screenX = [&](int32_t x, int32_t z) {
        const float projected = float(x) * 0.83f + float(z) * 0.42f;
        return W / 2 + static_cast<int32_t>(std::lround(projected * pxPerCmX));
    };
    const auto screenY = [&](int32_t y) {
        return H - 1 - static_cast<int32_t>(settings.margin) -
               static_cast<int32_t>(std::lround(float(y) * pxPerCmY));
    };
    const auto disk = [&](int cx, int cy, int radius,
                          uint8_t r, uint8_t g, uint8_t b) {
        radius = std::clamp(radius, 1, std::max(W, H));
        const int x0 = std::max(0, cx - radius), x1 = std::min(W - 1, cx + radius);
        const int y0 = std::max(0, cy - radius), y1 = std::min(H - 1, cy + radius);
        for (int y = y0; y <= y1; ++y) {
            for (int x = x0; x <= x1; ++x) {
                const int dx = x - cx, dy = y - cy;
                if (dx * dx + dy * dy > radius * radius) continue;
                const size_t index = (static_cast<size_t>(y) * W + x) * 4;
                image.rgba[index] = r;
                image.rgba[index + 1] = g;
                image.rgba[index + 2] = b;
                image.rgba[index + 3] = 255;
            }
        }
    };
    // Rasterize bark first, then leaf clusters. The silhouette is a
    // deterministic temporary asset: authoring can replace it with a baked
    // multi-view atlas once the engine's texture pipeline is integrated.
    for (const TreeBranch& branch : tree.branches) {
        const int x0 = screenX(branch.x0, branch.z0);
        const int y0 = screenY(branch.y0);
        const int x1 = screenX(branch.x1, branch.z1);
        const int y1 = screenY(branch.y1);
        const int steps = std::max(1, std::max(std::abs(x1 - x0), std::abs(y1 - y0)));
        for (int step = 0; step <= steps; ++step) {
            const int x = x0 + (x1 - x0) * step / steps;
            const int y = y0 + (y1 - y0) * step / steps;
            const int32_t r = branch.startRadiusCm +
                (branch.endRadiusCm - branch.startRadiusCm) * step / steps;
            const int radius = std::max(1, static_cast<int>(
                std::lround(float(r) * pxPerCmX)));
            disk(x, y, radius, 103, 64, 38);
        }
    }
    for (const LeafCluster& leaf : tree.leaves) {
        const int x = screenX(leaf.xCm, leaf.zCm);
        const int y = screenY(leaf.yCm);
        const int radius = std::max(1, static_cast<int>(
            std::lround(float(leaf.radiusCm) *
                        std::min(pxPerCmX, pxPerCmY))));
        const uint8_t variation = static_cast<uint8_t>(leaf.variation % 29);
        disk(x, y, radius, static_cast<uint8_t>(34 + variation),
             static_cast<uint8_t>(88 + variation), static_cast<uint8_t>(27 + variation / 2));
    }

    auto model = std::make_unique<render::Model>();
    model->path = "terraweave/arbor-billboard/" + std::to_string(profile.speciesId) +
                  "/" + std::to_string(seed);
    model->batching.autoLod = false;
    render::Material foliage;
    foliage.name = "TerraWeave tree billboard cutout";
    foliage.alpha = render::AlphaMode::Mask;
    foliage.alphaCutoff = 0.45f;
    foliage.baseTex = 0;
    foliage.doubleSided = true;
    // A flat camera-facing silhouette should not cast a long fake tree shadow.
    foliage.castShadow = false;
    foliage.roughness = 0.85f;
    model->materials.push_back(foliage);
    model->images.push_back(std::move(image));

    render::ModelVertex a, b, c, d;
    a.pos = {-widthMetres * 0.5f, 0, 0}; a.nrm = {0, 0, 1}; a.uv = {0, 1};
    b.pos = {widthMetres * 0.5f, 0, 0}; b.nrm = {0, 0, 1}; b.uv = {1, 1};
    c.pos = {widthMetres * 0.5f, heightMetres, 0}; c.nrm = {0, 0, 1}; c.uv = {1, 0};
    d.pos = {-widthMetres * 0.5f, heightMetres, 0}; d.nrm = {0, 0, 1}; d.uv = {0, 0};
    model->vertices = {a, b, c, d};
    model->indices = {0, 1, 2, 0, 2, 3};
    render::Primitive primitive;
    primitive.material = 0;
    primitive.firstVertex = 0; primitive.vertexCount = 4;
    primitive.firstIndex = 0; primitive.indexCount = 6;
    render::MeshDef mesh;
    mesh.name = "Arbor full-tree billboard";
    mesh.primitives.push_back(primitive);
    model->meshes.push_back(std::move(mesh));
    render::Node node;
    node.name = "Arbor impostor";
    node.mesh = 0;
    model->nodes.push_back(std::move(node));
    model->finalize();
    return model;
}

} // namespace aiverse::terraweave
