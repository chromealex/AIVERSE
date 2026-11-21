// TerraWeave BranchLOD: local structural mesh detail, not whole-tree decimation.
// Every branch keeps a stable parent and bound; the existing Renderer3D can
// select its own MeshDef::lods per branch based on projected screen size.
// Cosmetic render geometry only; never substitute for authoritative collision.
#pragma once
#include "engine/world/TerraWeaveArborMesh.h"
#include <cstddef>
#include <cstdint>
#include <stdexcept>

namespace aiverse::terraweave {

enum class BranchDetail : uint8_t { Full = 0, Reduced = 1, Minimal = 2 };

// An individual structural branch and only its leaf clusters. Geometry stays
// in tree-local centimetres so the original branch hierarchy remains stable.
// Full: radialSegments (default 8), every leaf and crossed cards.
// Reduced: 4-6 sides, roughly half of the local leaf clusters.
// Minimal: 3 sides + one local leaf cluster as a single alpha-card plane.
// The trunk is never culled. A whole-tree billboard is a separate far-tier.
inline ArborMesh buildArborBranchMesh(const TreeSkeleton& tree,
                                    size_t branchIndex, BranchDetail detail,
                                    ArborMeshSettings base = {8, true}) {
    if (branchIndex >= tree.branches.size())
        throw std::out_of_range("TerraWeave branch index");
    if (detail != BranchDetail::Full && detail != BranchDetail::Reduced &&
        detail != BranchDetail::Minimal)
        throw std::invalid_argument("TerraWeave branch detail");

    base.trunkFlare=branchIndex==0;
    base.includeLeader=branchIndex==0;
    if (detail == BranchDetail::Reduced) {
        base.longitudinalSegments=3;
        base.sprayPairs = std::max(2u,base.sprayPairs/2);
        base.radialSegments = std::max<uint32_t>(3, (base.radialSegments + 1) / 2);
    } else if (detail == BranchDetail::Minimal) {
        base.longitudinalSegments=1;
        base.sprayPairs = 2;
        base.radialSegments = 3;
        base.leafPlanes = 1;
    }
    if(base.organicBranches)return buildArborMesh(tree,base,int32_t(branchIndex),detail!=BranchDetail::Full);

    TreeSkeleton section;
    section.speciesId = tree.speciesId;
    section.seed = tree.seed;
    section.heightCm = tree.heightCm;
    section.crownRadiusCm = tree.crownRadiusCm;
    section.branches.push_back(tree.branches[branchIndex]);
    // Ignore the inherited parent index ONLY inside this isolated mesh
    // builder; the render::Node retains the real structural parent.
    section.branches[0].parent = -1;
    for (const LeafCluster& leaf : tree.leaves) {
        if (leaf.parentBranch != static_cast<int32_t>(branchIndex)) continue;
        auto localLeaf=leaf;localLeaf.parentBranch=0;section.leaves.push_back(localLeaf);
    }
    if (detail != BranchDetail::Full && section.leaves.size() > 1) {
        // Keep the outermost cluster stable, not a globally selected half-tree.
        const LeafCluster far = section.leaves.back();
        section.leaves.clear();
        section.leaves.push_back(far);
    }
    return buildArborMesh(section, base);
}

} // namespace aiverse::terraweave
