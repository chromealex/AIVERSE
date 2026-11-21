// Human-specific material graphs for the existing AIVERSE PBR renderer.
// All operations compile to paired GLSL/MSL via ShaderGraph: no renderer fork.
#pragma once
#include "HumanDNA.h"
#include "engine/render/Model.h"
#include "engine/render/graph/ShaderGraph.h"

namespace aiverse::vivid {
enum class HumanSurface : std::uint8_t {
    Skin, Iris, Sclera, Cornea, Hair, Eyelash, Teeth
};
// Explicit per-material quality policy. High preserves existing appearances;
// Low avoids dedicated human BRDFs and excessive graph noise on mobile.
// Quality is a presentation setting, NOT part of versioned HumanDNA.
enum class HumanMaterialQuality : std::uint8_t { Low, Medium, High };
struct HumanMaterialBudget {
    int maxNodes=0;
    int maxSamples=0;
    bool dedicatedBrdf=false;
};
constexpr HumanMaterialBudget humanMaterialBudget(HumanMaterialQuality q) noexcept {
    switch(q) {
    case HumanMaterialQuality::Low: return {24,0,false};
    case HumanMaterialQuality::Medium: return {48,0,true};
    case HumanMaterialQuality::High: return {128,0,true};
    }
    return {24,0,false};
}
struct HumanSurfaceMaterials {
    render::Material skin,iris,sclera,cornea,hair,eyelash,teeth;
};
render::Material humanMaterial(HumanSurface surface,const HumanDNA& dna);
render::Material humanMaterial(HumanSurface surface,const HumanDNA& dna,
                               HumanMaterialQuality quality);
// Preserve imported scan albedo/normal/roughness/AO/emissive texture slots
// when upgrading a glTF/FBX material to VIVID human-specific shading.
render::Material humanMaterialFromSource(HumanSurface surface,const HumanDNA& dna,
                                         const render::Material& source);
render::Material humanMaterialFromSource(HumanSurface surface,
                                        const HumanDNA& dna,
                                        const render::Material& source,
                                        HumanMaterialQuality quality);
HumanSurfaceMaterials humanMaterials(const HumanDNA& dna);
HumanSurfaceMaterials humanMaterials(const HumanDNA& dna,
                                     HumanMaterialQuality quality);
}
