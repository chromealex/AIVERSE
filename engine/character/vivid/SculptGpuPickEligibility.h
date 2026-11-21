// Stage 7AQ: fail-closed selection compatibility for the experimental ID
// pass. Renderer3D remains the only owner of actual LOD/culling decisions.
// Until the GPU ID path consumes its exact visible mesh/LOD records, a
// Model with authored LOD chains cannot be claimed pixel-parity eligible.
#pragma once
#include "engine/render/Model.h"
#include <cstddef>
#include <cstdint>

namespace aiverse::vivid {

enum class SculptGpuPickEligibility : std::uint8_t {
    Eligible, InvalidMesh, MissingOwner, AmbiguousOwner,
    AuthoredLodSelection, InvalidSceneMesh
};

inline SculptGpuPickEligibility sculptGpuPickEligibility(
        const render::Model& model,std::size_t selectedMesh) noexcept {
    if(selectedMesh>=model.meshes.size())
        return SculptGpuPickEligibility::InvalidMesh;
    std::size_t selectedOwners=0;
    for(const auto& node:model.nodes) {
        if(node.mesh<0)continue;
        const auto mesh=std::size_t(node.mesh);
        if(mesh>=model.meshes.size())
            return SculptGpuPickEligibility::InvalidSceneMesh;
        if(mesh==selectedMesh)++selectedOwners;
        // The normal Renderer3D colour pass may select an alternate
        // authored LOD or apply LOD fade. The current ID pass draws only
        // the base model mesh; even other occluder LODs could differ.
        if(!model.meshes[mesh].lods.empty())
            return SculptGpuPickEligibility::AuthoredLodSelection;
    }
    if(!selectedOwners)return SculptGpuPickEligibility::MissingOwner;
    if(selectedOwners>1)return SculptGpuPickEligibility::AmbiguousOwner;
    return SculptGpuPickEligibility::Eligible;
}

inline const char* sculptGpuPickEligibilityReason(
        SculptGpuPickEligibility value) noexcept {
    switch(value) {
        case SculptGpuPickEligibility::Eligible:return "supported selected-mesh topology";
        case SculptGpuPickEligibility::InvalidMesh:return "invalid selected mesh";
        case SculptGpuPickEligibility::MissingOwner:return "selected mesh has no visible node";
        case SculptGpuPickEligibility::AmbiguousOwner:return "selected mesh has multiple node owners";
        case SculptGpuPickEligibility::AuthoredLodSelection:return "Renderer3D may select a different authored LOD";
        case SculptGpuPickEligibility::InvalidSceneMesh:return "invalid node-attached scene mesh";
    }
    return "unknown model geometry policy";
}
} // namespace aiverse::vivid
