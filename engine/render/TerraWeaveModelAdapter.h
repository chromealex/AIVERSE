// TerraWeave Arbor -> standard AIVERSE render::Model adapter.
// Render-only conversion. CPU-safe; call render::uploadModel(dev, model) on
// the owning render thread. Uses existing model LOD/instancing/RHI pipeline.
#pragma once
#include "engine/world/TerraWeaveArborMesh.h"
#include "engine/world/TerraWeaveBranchLod.h"
#include "engine/render/Model.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <string>

namespace aiverse::terraweave {

// vertexOwners (optional): one structural-branch index per model vertex for
// hierarchical wind rigs (TerraWeaveArborWind.h); -1 where a tier's skeleton
// differs from the High-detail one.
inline std::unique_ptr<render::Model> makeArborModel(
        const TreeProfile& profile, uint64_t seed,
        ArborMeshSettings meshSettings = {},
        std::vector<int32_t>* vertexOwners = nullptr) {
    if (vertexOwners) vertexOwners->clear();
    auto model = std::make_unique<render::Model>();
    model->path = "terraweave/arbor/" + std::to_string(profile.speciesId) + "/" +
                  std::to_string(seed);
    model->batching.independentShadowLod = true; // native authored tiers drive the camera independently
    model->batching.autoLod = false; // explicit generated geometric LOD meshes
    render::Material bark;
    bark.name = "TerraWeave Bark";
    bark.baseColor = {0.31f, 0.18f, 0.11f, 1.0f};
    bark.roughness = 0.9f;
    render::Material foliage;
    foliage.name = "TerraWeave Foliage";
    foliage.baseColor = {0.21f, 0.43f, 0.16f, 1.0f};
    foliage.roughness = 0.85f;
    foliage.doubleSided = true;
    if(meshSettings.organicBranches)foliage.thinTransmission=.35f;
    // Opaque fallback until a project assigns an alpha-masked leaf texture.
    // The crossed cards are visible (green rectangles), not fake alpha masks.
    foliage.alpha = render::AlphaMode::Opaque;
    model->materials.push_back(bark);
    model->materials.push_back(foliage);

    for (TreeLOD tier : {TreeLOD::High, TreeLOD::Medium, TreeLOD::Low}) {
        auto tierSettings=meshSettings;
        if(meshSettings.naturalConifer||meshSettings.naturalBroadleaf) {
            tierSettings.longitudinalSegments=tier==TreeLOD::High?meshSettings.longitudinalSegments:tier==TreeLOD::Medium?3u:1u;
            tierSettings.radialSegments=tier==TreeLOD::High?meshSettings.radialSegments:tier==TreeLOD::Medium?4u:3u;
            tierSettings.sprayPairs=tier==TreeLOD::High?meshSettings.sprayPairs:tier==TreeLOD::Medium?std::max(2u,meshSettings.sprayPairs/2):2u;
        }
        if(meshSettings.naturalBroadleaf) {
            tierSettings.radialSegments=tier==TreeLOD::High?8u:tier==TreeLOD::Medium?5u:3u;
            tierSettings.sprayPairs=tier==TreeLOD::High?3u:tier==TreeLOD::Medium?2u:1u;
            tierSettings.longitudinalSegments=tier==TreeLOD::High?4u:tier==TreeLOD::Medium?2u:1u;
            tierSettings.leafPlanes=tier==TreeLOD::Low?1u:2u;
        }
        if(meshSettings.compactConifer) {
            tierSettings.doubleSidedLeaves=false; // material already draws both sides
            tierSettings.longitudinalSegments=tier==TreeLOD::High?3u:tier==TreeLOD::Medium?2u:1u;
            tierSettings.radialSegments=tier==TreeLOD::High?6u:tier==TreeLOD::Medium?4u:3u;
            tierSettings.sprayPairs=tier==TreeLOD::High?4u:tier==TreeLOD::Medium?3u:1u;
            tierSettings.leafPlanes=tier==TreeLOD::Low?1u:2u;
        }
        const bool sharedSkeleton=meshSettings.naturalConifer||meshSettings.naturalBroadleaf||tier==TreeLOD::High;
        const ArborMesh geometry = buildArborMesh(generateTree(profile, seed,
            (meshSettings.naturalConifer||meshSettings.naturalBroadleaf)?TreeLOD::High:tier), tierSettings);
        const uint32_t baseVertex = static_cast<uint32_t>(model->vertices.size());
        const uint32_t baseIndex = static_cast<uint32_t>(model->indices.size());
        uint32_t barkVertices = 0;
        bool leavesStarted = false;
        for (const ArborVertex& v : geometry.vertices) {
            if (v.material > 1 || (leavesStarted && v.material == 0))
                throw std::logic_error("TerraWeave Arbor vertex material ordering");
            if (v.material == 0) ++barkVertices;
            else leavesStarted = true;
            render::ModelVertex vertex;
            // Render coordinates are metres. Generator data is in centimetres.
            vertex.pos = {v.x * 0.01f, v.y * 0.01f, v.z * 0.01f};
            vertex.nrm = {v.nx, v.ny, v.nz};
            vertex.uv = {v.u, v.v};
            model->vertices.push_back(vertex);
            if (vertexOwners) vertexOwners->push_back(sharedSkeleton ? v.owner : -1);
        }
        uint32_t barkIndexCount = 0;
        bool foundLeafIndex = false;
        for (uint32_t index : geometry.indices) {
            if (index >= geometry.vertices.size())
                throw std::logic_error("TerraWeave Arbor invalid mesh index");
            const uint8_t mat = geometry.vertices[index].material;
            if (mat == 1) foundLeafIndex = true;
            else if (foundLeafIndex)
                throw std::logic_error("TerraWeave Arbor mixed index materials");
            if (!foundLeafIndex) ++barkIndexCount;
            model->indices.push_back(baseVertex + index);
        }
        render::MeshDef mesh;
        mesh.name = tier == TreeLOD::High ? "Arbor LOD0" :
                    tier == TreeLOD::Medium ? "Arbor LOD1" : "Arbor LOD2";
        if (barkIndexCount) {
            render::Primitive primitive;
            primitive.material = 0;
            primitive.firstVertex = baseVertex;
            primitive.vertexCount = barkVertices;
            primitive.firstIndex = baseIndex;
            primitive.indexCount = barkIndexCount;
            mesh.primitives.push_back(primitive);
        }
        const uint32_t leafCount = static_cast<uint32_t>(geometry.indices.size()) -
                                   barkIndexCount;
        if (leafCount) {
            render::Primitive primitive;
            primitive.material = 1;
            primitive.firstVertex = baseVertex + barkVertices;
            primitive.vertexCount = static_cast<uint32_t>(geometry.vertices.size()) -
                                    barkVertices;
            primitive.firstIndex = baseIndex + barkIndexCount;
            primitive.indexCount = leafCount;
            mesh.primitives.push_back(primitive);
        }
        model->meshes.push_back(std::move(mesh));
    }
    // Renderer3D uses screen-size LOD selection with its existing fade policy.
    // Future billboard atlas should be added as LOD3, not a naked empty mesh.
    if(meshSettings.naturalBroadleaf)model->meshes[0].lods={{1,180.f},{2,64.f}};
    else if(meshSettings.compactConifer)model->meshes[0].lods={{1,180.f},{2,48.f}};
    else model->meshes[0].lods={{1,128.f},{2,32.f}};
    render::Node node;
    node.name = "TerraWeave Arbor";
    node.mesh = 0;
    model->nodes.push_back(std::move(node));
    render::generateTangents(model->vertices,model->indices,0,uint32_t(model->indices.size()));
    model->finalize();
    return model;
}


struct BranchLodSettings {
    ArborMeshSettings mesh{8, true};
    float reducedPixels = 96.0f; // per-branch projected screen diameter
    float minimalPixels = 30.0f; // lower threshold => simplest branch
};

// A model with one *render node per branch*, not one LOD chain for the
// entire tree. The renderer performs frustum culling and screen-space LOD
// selection for each node separately, including children of the same tree.
// Every leaf cluster belongs to exactly one parent branch. Trunk remains
// independently visible at all detail tiers (tree billboard uses a different
// single-quad model).
inline std::unique_ptr<render::Model> makeArborBranchLodModel(
        const TreeProfile& profile, uint64_t seed,
        BranchLodSettings settings = {},
        std::vector<int32_t>* vertexOwners = nullptr) {
    if (vertexOwners) vertexOwners->clear();
    if (!std::isfinite(settings.reducedPixels) ||
        !std::isfinite(settings.minimalPixels) ||
        settings.reducedPixels <= settings.minimalPixels ||
        settings.minimalPixels <= 0 ||
        settings.mesh.radialSegments < 3 || settings.mesh.radialSegments > 16 ||
        settings.mesh.leafPlanes < 1 || settings.mesh.leafPlanes > 2)
        throw std::invalid_argument("TerraWeave BranchLOD settings");
    const TreeSkeleton tree = generateTree(profile, seed, TreeLOD::High);
    auto model = std::make_unique<render::Model>();
    model->path = "terraweave/arbor-branches/" + std::to_string(profile.speciesId) +
                  "/" + std::to_string(seed);
    model->batching.independentShadowLod = true; // native authored tiers drive the camera independently
    model->batching.autoLod = false; // explicit per-branch LOD meshes
    render::Material bark;
    bark.name = "TerraWeave Branch Bark";
    bark.baseColor = {0.31f, 0.18f, 0.11f, 1.0f};
    bark.roughness = 0.9f;
    render::Material foliage;
    foliage.name = "TerraWeave Branch Foliage";
    foliage.baseColor = {0.21f, 0.43f, 0.16f, 1.0f};
    foliage.roughness = 0.85f;
    foliage.doubleSided = true;
    if(settings.mesh.organicBranches)foliage.thinTransmission=.35f;
    foliage.alpha = render::AlphaMode::Opaque; // project can supply cutout texture
    // Leaf vertex alpha carries GPU crown-wind height weight. Ignore it in
    // fragment shading; the base RGB texture/material still controls opacity.
    foliage.vertexColors=false;
    model->materials = {bark, foliage};

    for (size_t branch = 0; branch < tree.branches.size(); ++branch) {
        const uint32_t primary = static_cast<uint32_t>(model->meshes.size());
        for (const BranchDetail detail : {BranchDetail::Full,
                                          BranchDetail::Reduced,
                                          BranchDetail::Minimal}) {
            const ArborMesh geometry = buildArborBranchMesh(
                tree, branch, detail, settings.mesh);
            const uint32_t baseVertex = static_cast<uint32_t>(model->vertices.size());
            const uint32_t baseIndex = static_cast<uint32_t>(model->indices.size());
            uint32_t barkVertices = 0;
            bool foliageStarted = false;
            for (const ArborVertex& v : geometry.vertices) {
                if (v.material > 1 || (foliageStarted && v.material == 0))
                    throw std::logic_error("TerraWeave branch material order");
                if (v.material == 0) ++barkVertices;
                else foliageStarted = true;
                render::ModelVertex vertex;
                vertex.pos = {v.x * 0.01f, v.y * 0.01f, v.z * 0.01f};
                vertex.nrm = {v.nx, v.ny, v.nz};
                vertex.uv = {v.u, v.v};
                if(v.material==1) {
                    // Reusable, bounded source attribute; identical for
                    // Full/Reduced/Minimal branch geometry and every season.
                    const int64_t y=std::clamp<int64_t>(
                        v.y,0,std::max<int32_t>(1,tree.heightCm));
                    const uint32_t windWeight=static_cast<uint32_t>(
                        y*255/std::max<int32_t>(1,tree.heightCm));
                    vertex.color=(windWeight<<24)|0x00ffffffu;
                }
                model->vertices.push_back(vertex);
                // Each render node is one structural branch.
                if (vertexOwners) vertexOwners->push_back(int32_t(branch));
            }
            uint32_t barkIndices = 0;
            bool leafIndicesStarted = false;
            for (const uint32_t index : geometry.indices) {
                if (index >= geometry.vertices.size())
                    throw std::logic_error("TerraWeave branch invalid mesh index");
                const uint8_t mat = geometry.vertices[index].material;
                if (mat == 1) leafIndicesStarted = true;
                else if (leafIndicesStarted)
                    throw std::logic_error("TerraWeave branch mixed index materials");
                if (!leafIndicesStarted) ++barkIndices;
                model->indices.push_back(baseVertex + index);
            }
            render::MeshDef mesh;
            mesh.name = "Arbor branch " + std::to_string(branch) + " LOD" +
                        std::to_string(static_cast<int>(detail));
            if (barkIndices) {
                render::Primitive p;
                p.material = 0;
                p.firstVertex = baseVertex;
                p.vertexCount = barkVertices;
                p.firstIndex = baseIndex;
                p.indexCount = barkIndices;
                mesh.primitives.push_back(p);
            }
            if (const uint32_t leaves = static_cast<uint32_t>(geometry.indices.size()) -
                                        barkIndices) {
                render::Primitive p;
                p.material = 1;
                p.firstVertex = baseVertex + barkVertices;
                p.vertexCount = static_cast<uint32_t>(geometry.vertices.size()) -
                                barkVertices;
                p.firstIndex = baseIndex + barkIndices;
                p.indexCount = leaves;
                mesh.primitives.push_back(p);
            }
            model->meshes.push_back(std::move(mesh));
        }
        model->meshes[primary].lods = {
            {static_cast<int>(primary + 1), settings.reducedPixels},
            {static_cast<int>(primary + 2), settings.minimalPixels}
        };
        render::Node node;
        node.name = branch == 0 ? "Arbor trunk" :
                    "Arbor branch " + std::to_string(branch);
        node.mesh = static_cast<int>(primary);
        node.parent = tree.branches[branch].parent;
        model->nodes.push_back(std::move(node));
    }
    render::generateTangents(model->vertices,model->indices,0,uint32_t(model->indices.size()));
    model->finalize();
    return model;
}


// Whole-tree LOD chain with every branch of a detail tier MERGED into one
// mesh: one node, three MeshDefs (Full -> Reduced -> Minimal), each with
// exactly two primitives (bark, foliage). All trees of a species and tier
// share one primitive, so Renderer3D draws a whole forest of that species
// with ONE instanced draw per material (per instance-limit chunk) instead of
// one draw per branch node. The renderer picks the tier per TREE by its
// projected size (MeshDef::lods). Use with GPU hierarchical wind
// (vertex owners -> rig parts); CPU per-branch poses need the BranchLOD model.
struct TreeLodSettings {
    ArborMeshSettings mesh{8, true};
    float reducedPixels = 260.0f; // whole-tree projected diameter
    float minimalPixels = 110.0f;
};

inline std::unique_ptr<render::Model> makeArborTreeLodModel(
        const TreeProfile& profile, uint64_t seed,
        TreeLodSettings settings = {},
        std::vector<int32_t>* vertexOwners = nullptr) {
    if (vertexOwners) vertexOwners->clear();
    if (!std::isfinite(settings.reducedPixels) ||
        !std::isfinite(settings.minimalPixels) ||
        settings.reducedPixels <= settings.minimalPixels ||
        settings.minimalPixels <= 0 ||
        settings.mesh.radialSegments < 3 || settings.mesh.radialSegments > 16 ||
        settings.mesh.leafPlanes < 1 || settings.mesh.leafPlanes > 2)
        throw std::invalid_argument("TerraWeave tree LOD settings");
    const TreeSkeleton tree = generateTree(profile, seed, TreeLOD::High);
    auto model = std::make_unique<render::Model>();
    model->path = "terraweave/arbor-tree/" + std::to_string(profile.speciesId) +
                  "/" + std::to_string(seed);
    model->batching.independentShadowLod = true;
    model->batching.autoLod = false; // explicit merged tiers below
    render::Material bark;
    bark.name = "TerraWeave Branch Bark";
    bark.baseColor = {0.31f, 0.18f, 0.11f, 1.0f};
    bark.roughness = 0.9f;
    render::Material foliage;
    foliage.name = "TerraWeave Branch Foliage";
    foliage.baseColor = {0.21f, 0.43f, 0.16f, 1.0f};
    foliage.roughness = 0.85f;
    foliage.doubleSided = true;
    if(settings.mesh.organicBranches)foliage.thinTransmission=.35f;
    foliage.alpha = render::AlphaMode::Opaque;
    foliage.vertexColors=false;
    model->materials = {bark, foliage};

    for (const BranchDetail detail : {BranchDetail::Full, BranchDetail::Reduced,
                                      BranchDetail::Minimal}) {
        // Gather all branches of this tier: bark first, then foliage, so each
        // material is ONE contiguous vertex/index range.
        std::vector<render::ModelVertex> verts[2];
        std::vector<int32_t> owners[2];
        std::vector<uint32_t> idx[2];
        for (size_t branch = 0; branch < tree.branches.size(); ++branch) {
            const ArborMesh geometry = buildArborBranchMesh(tree, branch, detail, settings.mesh);
            std::vector<uint32_t> remap(geometry.vertices.size(), 0);
            for (size_t i = 0; i < geometry.vertices.size(); ++i) {
                const ArborVertex& v = geometry.vertices[i];
                if (v.material > 1) throw std::logic_error("TerraWeave tree material");
                render::ModelVertex vertex;
                vertex.pos = {v.x * 0.01f, v.y * 0.01f, v.z * 0.01f};
                vertex.nrm = {v.nx, v.ny, v.nz};
                vertex.uv = {v.u, v.v};
                if (v.material == 1) {
                    const int64_t y = std::clamp<int64_t>(v.y, 0, std::max<int32_t>(1, tree.heightCm));
                    const uint32_t windWeight = static_cast<uint32_t>(
                        y * 255 / std::max<int32_t>(1, tree.heightCm));
                    vertex.color = (windWeight << 24) | 0x00ffffffu;
                }
                remap[i] = uint32_t(verts[v.material].size());
                verts[v.material].push_back(vertex);
                owners[v.material].push_back(int32_t(branch));
            }
            for (const uint32_t index : geometry.indices) {
                if (index >= geometry.vertices.size())
                    throw std::logic_error("TerraWeave tree invalid mesh index");
                const uint8_t mat = geometry.vertices[index].material;
                idx[mat].push_back(remap[index]);
            }
        }
        render::MeshDef mesh;
        mesh.name = std::string("Arbor tree LOD") + std::to_string(static_cast<int>(detail));
        for (uint8_t mat = 0; mat < 2; ++mat) {
            if (idx[mat].empty()) continue;
            const uint32_t baseVertex = uint32_t(model->vertices.size());
            const uint32_t baseIndex = uint32_t(model->indices.size());
            model->vertices.insert(model->vertices.end(), verts[mat].begin(), verts[mat].end());
            if (vertexOwners) vertexOwners->insert(vertexOwners->end(), owners[mat].begin(), owners[mat].end());
            for (const uint32_t i : idx[mat]) model->indices.push_back(baseVertex + i);
            render::Primitive p;
            p.material = mat;
            p.firstVertex = baseVertex;
            p.vertexCount = uint32_t(verts[mat].size());
            p.firstIndex = baseIndex;
            p.indexCount = uint32_t(idx[mat].size());
            mesh.primitives.push_back(p);
        }
        model->meshes.push_back(std::move(mesh));
    }
    model->meshes[0].lods = {{1, settings.reducedPixels}, {2, settings.minimalPixels}};
    render::Node node;
    node.name = "Arbor tree";
    node.mesh = 0;
    model->nodes.push_back(std::move(node));
    render::generateTangents(model->vertices,model->indices,0,uint32_t(model->indices.size()));
    model->finalize();
    return model;
}

} // namespace aiverse::terraweave
