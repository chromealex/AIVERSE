// VIVID controlled optical-capture variants used by the native Creator.
// Each case starts from HumanDNA::preset("neutral") and modifies ONE scalar.
// Does not alter the versioned DNA format or core gameplay/animation state.
#pragma once
#include "engine/character/vivid/HumanDNA.h"
#include <string_view>

namespace aiverse::vivid {
struct OpticalCaptureCases {
    static constexpr std::string_view reference="reference";
    static bool apply(HumanDNA& dna,std::string_view name) noexcept {
        if(name==reference)return true;
        if(name=="skin-scatter-low") {dna.skin.scatteringMm=0.10f;return true;}
        if(name=="skin-scatter-high") {dna.skin.scatteringMm=4.0f;return true;}
        if(name=="eye-ior-low") {dna.eyes.corneaIor=1.25f;return true;}
        if(name=="eye-ior-high") {dna.eyes.corneaIor=1.45f;return true;}
        if(name=="hair-rough-low") {dna.groom.roughness=0.08f;return true;}
        if(name=="hair-rough-high") {dna.groom.roughness=0.90f;return true;}
        return false;
    }
};
} // namespace aiverse::vivid
