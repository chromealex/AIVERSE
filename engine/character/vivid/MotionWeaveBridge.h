// VIVID + MotionWeave: compose facial morphs after MotionWeave's presentation
// scheduler has joined, without changing its retained/cached body pose.
//
// MotionWeave owns AnimationGraph, IK, retarget, motion sampling and all body
// animation policy. This adapter intentionally has no scheduler, ECS system,
// model importer or alternate animation graph.
#pragma once

#include "MorphBinding.h"
#include "engine/animation/MotionWeave.h"

namespace aiverse::vivid {

// Must be called AFTER MotionWeave::update(presentationWorld, dt) has finished.
// Only local morph channels are modified: bone matrices in source remain valid
// for the unmodified skeletal transforms. For jaw/eye bone drivers, provide a
// dedicated ordered rig stage in a later release.
//
// The host owns both reusable output poses and passes them by distinct refs.
// On success, output contains the completed pose, scratch retains the previous
// output allocation; each frame starts from fresh MotionWeave local pose.
// On any invalid frame/binding/pose, output is unchanged (scratch may be
// overwritten). Never use MotionWeave's own pose as either destination.
// No per-frame heap allocation after caller-owned pose capacities are warm.
bool composeFaceAfterMotionWeave(
    const motionweave::AnimationPoseComponent& source,
    const FaceFrame& facial,
    const MorphBinding& binding,
    render::Pose& output,
    render::Pose& scratch,
    float influence = 1.0f,
    MorphApply mode = MorphApply::Override);

} // namespace aiverse::vivid
