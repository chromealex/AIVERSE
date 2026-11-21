// Asset-free VIVID character reference builder: head, facial joints,
// layered eyes, mouth, and a separate dynamic groom model.
// Real scanned glTF/FBX humans can use the same DNA/materials/rig APIs.
#pragma once
#include "HumanDNA.h"
#include "HumanGroom.h"
#include "HumanRig.h"
#include "HumanMaterials.h"
#include "SparseMorph.h"
#include "FaceShapeBinding.h"

#include <memory>

namespace aiverse::vivid {
struct HumanPreviewAsset {
    std::unique_ptr<render::Model> head;
    std::unique_ptr<render::Model> hair;
    HumanGroomRuntime groom;
    SparseMorphLibrary facialShapes;
    FaceShapeBinding facialBinding;
    HumanRigBinding rig;
};
// Procedural reference asset for editor/testing, NOT a realistic scan.
// It uses the standard render::Model/Material/ShaderGraph stack, so it
// works with Metal, GL and Vulkan when the underlying RHI supports them.
HumanPreviewAsset generateHumanPreview(const HumanDNA& dna,
                                        GroomQuality quality=GroomQuality::High);
HumanPreviewAsset generateHumanPreview(const HumanDNA& dna,GroomQuality quality,
                                       HumanMaterialQuality materialQuality);
struct VividPresentationSettings;
HumanPreviewAsset generateHumanPreview(const HumanDNA& dna,
                                       const VividPresentationSettings& settings);
} // namespace aiverse::vivid
