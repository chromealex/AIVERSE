// TerraWeave Arbor trees -> hierarchical wind rig (WindRigModel.h).
// The procedural skeleton IS the hierarchy: trunk -> primary branches ->
// twigs; leaf cards flutter. Vertices carry their structural owner
// (ArborVertex::owner) from buildArborMesh; vertices without a known owner
// (coarse tiers generated from a different skeleton) bind to the nearest
// High-tier segment. One WindProfile per species, one mask atlas per game.
#pragma once
#include "engine/render/TerraWeaveModelAdapter.h"
#include "engine/render/WindRigModel.h"

namespace aiverse::terraweave {

inline render::wind::WindRig arborWindRig(const TreeSkeleton& tree,
                                          const render::wind::WindProfile& profile,
                                          const render::wind::WindMaskLibrary& masks) {
    std::vector<render::wind::WindNodeDesc> desc;
    desc.reserve(tree.branches.size());
    for (size_t i = 0; i < tree.branches.size(); ++i) {
        const auto& b = tree.branches[i];
        render::wind::WindNodeDesc d;
        if (b.parent >= int32_t(i)) throw std::logic_error("TerraWeave Arbor branch order");
        d.parent = b.parent;
        d.pivot = {float(b.x0) * 0.01f, float(b.y0) * 0.01f, float(b.z0) * 0.01f};
        d.tip = {float(b.x1) * 0.01f, float(b.y1) * 0.01f, float(b.z1) * 0.01f};
        desc.push_back(d);
    }
    return render::wind::buildWindRig(desc, profile, masks, tree.seed);
}

// makeArborModel / makeArborBranchLodModel + rig. `vertexOwners` comes from
// the same call (one entry per model vertex, -1 = unknown).
inline render::wind::WindBindReport applyArborWind(
        render::Model& model, const TreeSkeleton& high, const std::vector<int32_t>& vertexOwners,
        const render::wind::WindProfile& profile, const render::wind::WindMaskLibrary& masks,
        const render::wind::WindMaskAtlas& atlas) {
    if (vertexOwners.size() != model.vertices.size())
        throw std::invalid_argument("TerraWeave Arbor wind owners size");
    const auto rig = arborWindRig(high, profile, masks);
    std::vector<uint8_t> unknown(model.vertices.size(), 0);
    bool anyUnknown = false;
    for (size_t i = 0; i < vertexOwners.size(); ++i)
        if (vertexOwners[i] < 0 || size_t(vertexOwners[i]) >= rig.nodes.size()) { unknown[i] = 1; anyUnknown = true; }
    render::wind::WindBinding binding;
    if (anyUnknown) binding = render::wind::bindNearest(model, rig, &unknown);
    else binding.node.assign(model.vertices.size(), render::wind::kNoNode);
    binding.flutter.assign(model.vertices.size(), 0.f);
    for (size_t i = 0; i < vertexOwners.size(); ++i)
        if (!unknown[i]) binding.node[i] = uint16_t(vertexOwners[i]);
    // Foliage (material 1 in Arbor models) flutters.
    for (const auto& mesh : model.meshes)
        for (const auto& p : mesh.primitives)
            if (p.material == 1)
                for (uint32_t i = p.firstVertex; i < p.firstVertex + p.vertexCount && i < binding.flutter.size(); ++i)
                    binding.flutter[i] = 1.f;
    return render::wind::bindWindRig(model, rig, atlas, binding, {}, &masks);
}

} // namespace aiverse::terraweave
