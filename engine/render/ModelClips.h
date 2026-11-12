// Clip sidecar "<model>.clips.json" next to a model (written by tools/importers/unity_anim from
// Unity ModelImporter settings and .anim files, or by hand). loadModel applies it after any loader:
//
//   {"format": "aiverse-clips", "version": 1, "keepSource": false,
//    "clips": [
//      {"name": "Walk", "take": "Take 001", "frames": [500, 530]},          // split a take (file frames)
//      {"name": "Run",  "take": "Run", "seconds": [0, 1.2]},                // ... or take-relative seconds
//      {"name": "Idle", "file": "anims/Idle.glb", "clip": "Idle"},           // external clip, bound by node path
//      {"name": "Bite", "take": "Bite", "rootMotion": {"node": "jnt_org", "bakeXZ": false, "bakeY": true, "bakeRotation": true}}
//    ]}
//
// keepSource (default true) keeps the file's own clips next to the listed ones (Unity drops them
// when clipAnimations is not empty). rootMotion removes the parts of the root node's motion that are
// not baked into the pose (in-place clips: the simulation moves the unit) and records the removed
// horizontal speed in AnimClip::rootSpeed. Errors are warnings: the model still loads.
#pragma once
#include "Model.h"
#include <string>
#include <vector>

namespace aiverse::render {
// "Hips/Spine" matches the node whose ancestor chain ends with those names (exact name without '/').
int findNodePath(const Model& m, const std::string& path);
// Applies "<path>.clips.json" when it exists. Returns false when there is none.
bool applyClipSidecar(Model& m, const std::string& path, const FileReader& read, std::vector<std::string>* warnings);
// Same with an already parsed sidecar (tests, tools); `dir` resolves relative "file" entries.
void applyClipSidecar(Model& m, const std::string& dir, const std::string& sidecarText, const FileReader& read, std::vector<std::string>* warnings);
// Clip `src` restricted to [start, end] seconds, keys shifted to 0 (boundary keys interpolated).
AnimClip sliceClip(const AnimClip& src, float start, float end);
} // namespace aiverse::render
