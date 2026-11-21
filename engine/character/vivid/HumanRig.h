// VIVID facial-joint overlay on top of the immutable MotionWeave pose.
// VIVID does NOT own graph evaluation, IK, retarget or skeleton scheduling.
#pragma once
#include "HumanDNA.h"
#include "Face.h"
#include "engine/render/Model.h"
#include <array>
#include <span>
#include <string>

namespace aiverse::vivid {
struct HumanRigBinding {
    int root=-1,head=-1,jaw=-1,leftEye=-1,rightEye=-1;
    bool valid() const noexcept {return root>=0&&head>=0;}
    bool hasFacialJoints() const noexcept {return jaw>=0||leftEye>=0||rightEye>=0;}
    // Required root/head are mandatory; jaw/eyes can legitimately be
    // morph-only on imported scans. Reject mismatched skeleton hierarchy.
    static HumanRigBinding compile(const render::Model& model,const HumanRigDNA& dna);
    // Inputs and outputs are model-space node-world matrices. Never mutates
    // the MotionWeave pose or a shared skeletal animation cache.
    // Rejects invalid/non-finite facial controls without changing output.
    bool compose(std::span<const render::mat4> motionNodeMatrices,
                 std::span<render::mat4> output,
                 const FaceFrame& face,
                 float intensity=1.0f) const noexcept;
};
} // namespace aiverse::vivid
