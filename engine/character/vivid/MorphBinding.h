// VIVID Model facial morph adapter. Routes the fixed 52 semantic channels
// into the shared MotionWeave palette (render::kMaxMorphTargets slots).
#pragma once

#include "Face.h"
#include "engine/render/Animation.h"
#include <cstddef>
#include <span>
#include <vector>

namespace aiverse::vivid {

struct MorphRoute {
    Channel channel = Channel::JawOpen;
    int node = -1;            // index of render::Model::nodes; must own a morphable mesh
    int target = -1;          // morph slot in that node's mesh: 0 .. kMaxMorphTargets-1
    float scale = 1.0f;       // nonnegative, finite multiplier; output saturates to [0,1]
};

enum class MorphApply : unsigned char { Override, Additive };

// Compiled immutable routes: safe for many character instances sharing a Model.
// Apply only after the host's AnimationGraph/Animator evaluates render::Pose.
// Does not mutate model shared data; does not allocate during apply.
class MorphBinding {
public:
    // Throws std::invalid_argument on invalid node, channel, missing target,
    // duplicate destination, or gain outside [0,4].
    static MorphBinding compile(const render::Model& model, std::span<const MorphRoute> routes);

    // False and no changes on invalid influence, non-finite source data,
    // or mismatched pose length. This method does not call the GPU.
    bool apply(const FaceFrame& frame, render::Pose& pose, float influence = 1.0f,
               MorphApply mode = MorphApply::Override) const noexcept;

    std::size_t routes() const noexcept { return routes_.size(); }
    std::size_t nodes() const noexcept { return nodeCount_; }

private:
    std::size_t nodeCount_ = 0;
    std::vector<MorphRoute> routes_;
};

} // namespace aiverse::vivid
