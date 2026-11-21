// Shared botanical presentation recipe for native streaming and authoring.
// Generation/age/ecology ownership remains in the existing TreePreset/hosts.
#pragma once
#include "engine/world/TerraWeaveTreePresets.h"
#include "engine/world/TerraWeaveArborMesh.h"
namespace aiverse::terraweave {
inline ArborMeshSettings botanicalArborMeshSettings(const TreePreset& preset) {
    ArborMeshSettings mesh;
    mesh.radialSegments=8;mesh.doubleSidedLeaves=false;
    mesh.organicRootFlare=true;mesh.organicBranches=true;mesh.curveSegments=4;
    mesh.naturalConifer=preset.profile.shape==CrownShape::Conifer;
    mesh.naturalBroadleaf=!mesh.naturalConifer;mesh.compactConifer=mesh.naturalConifer;
    mesh.sprayDroop=mesh.naturalBroadleaf?(preset.name=="willow"?.85f:.12f):
        preset.name=="pine"?.08f:preset.name=="larch"?.55f:.35f;
    mesh.sprayPairs=4;
    // Live forest owners apply age/season once. Offline callers may assign
    // treeCanopyAtAge and treeProfileAtAge explicitly before mesh creation.
    return mesh;
}
} // namespace aiverse::terraweave
