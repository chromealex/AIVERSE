// Bridge from project-level VIVID appearance policy to the existing
// genre-independent Renderer3D material tier. No additional render quality
// owner, no texture-residency state and no gameplay ECS dependencies.
#pragma once
#include "VividPresentationSettings.h"
#include "engine/render/Renderer3D.h"
#include <stdexcept>

namespace aiverse::vivid {

constexpr render::MaterialQuality rendererMaterialQuality(
        HumanMaterialQuality quality) noexcept {
    switch(quality) {
    case HumanMaterialQuality::Low: return render::MaterialQuality::Low;
    case HumanMaterialQuality::Medium: return render::MaterialQuality::Medium;
    case HumanMaterialQuality::High: return render::MaterialQuality::High;
    }
    return render::MaterialQuality::Low;
}

constexpr HumanMaterialQuality vividMaterialQuality(
        render::MaterialQuality quality) noexcept {
    switch(quality) {
    case render::MaterialQuality::Low: return HumanMaterialQuality::Low;
    case render::MaterialQuality::Medium: return HumanMaterialQuality::Medium;
    case render::MaterialQuality::High: return HumanMaterialQuality::High;
    }
    return HumanMaterialQuality::Low;
}

// A host can resolve project-requested VIVID appearance against its chosen
// actual Renderer3D tier (often Renderer3D::defaultMaterialQualityFor()).
// A low renderer cannot accidentally pay for high human material graphs.
// Groom geometry is an independent CPU/LOD decision and is preserved.
// The returned value is a snapshot: it never modifies HumanDNA, the host's
// project settings, renderer state, or any in-flight GPU resources.
inline VividPresentationSettings resolveVividPresentation(
        const VividPresentationSettings& requested,
        render::MaterialQuality rendererTier) {
    if(!requested.valid())
        throw std::invalid_argument("VIVID invalid project quality policy");
    const auto maxMaterial=vividMaterialQuality(rendererTier);
    auto resolved=requested;
    if(static_cast<int>(resolved.materialQuality)>
       static_cast<int>(maxMaterial))
        resolved.materialQuality=maxMaterial;
    return resolved;
}

// Convenience single-owner application for the Creator or a host that wants
// to use the explicitly selected VIVID tier as the renderer tier too. Call
// at a render frame boundary (Renderer3D retires old shader programs).
inline void applyVividMaterialQuality(render::Renderer3D& renderer,
                                      const VividPresentationSettings& settings) {
    if(!settings.valid())
        throw std::invalid_argument("VIVID invalid project quality policy");
    renderer.setMaterialQuality(rendererMaterialQuality(settings.materialQuality));
}

} // namespace aiverse::vivid
