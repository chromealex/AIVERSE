# MotionWeave walk generator — handoff, 2026-11-23

Branch: `feature/motionweave-walk-generator-current` (started from `7351e960b`).
Roadmap commit: `373fe330f`. Implementation commit: `7ee169375`.

## Delivered

- `engine/render/WalkCycleGenerator.h` adds `render::WalkRig`, bounded
  `WalkCycleSettings`, and `render::generateWalkCycle(const Model&, ...)`.
  It returns a normal, in-place, looping `AnimClip` and does not mutate the
  input Model or simulation ECS state. The caller supplies explicit node IDs;
  the generator validates both leg chains, inputs and sample count.
- The clip contains eight rotation channels (hips, knees, ankles, shoulders)
  and a root translation bob channel. Phase endpoints match for looping.
  The public MotionWeave include exposes the generator.
- `tests/animation/walk_generator.cpp` checks channel shape, seam continuity,
  opposite hip movement, sampleClip compatibility and invalid-rig rejection.
  CMake registers `motionweave_walk_generator`.
- `docs/motionweave.md` describes the prototype and its limits. The MotionWeave
  site roadmap records the stage as `in-progress`.

## Checks actually run

- CMake configure: PASS, AppleClang 17, `AIVERSE_BUILD_TESTS=ON`.
- Native target `aiverse_motionweave_walk_generator_tests`: built PASS.
- `ctest -R '^motionweave_walk_generator$'`: 1/1 PASS.
- `python3.12 tools/site/build.py check`: 37 entries, 0 errors.
- Build directory: `/Volumes/KINGSTON 1/AIVERSE-builds/motionweave-walk`.
  This is the mounted KINGSTON volume's current macOS path.

## Limits and next step

This is a simple cyclic pose generator, not a finished natural walk. It does
not plant feet during stance, adapt to ground contacts, infer a rig mapping,
guarantee joint limits, serialize a catalog asset, or provide a visual
preview. No screenshot or video of this generated clip was captured; therefore
no successful devlog entry was added. The full CTest suite, non-Mac backends,
GPU rendering and performance were not run.

Next: render the generated clip on the existing procedural humanoid, measure
foot slide and joint bounds, then add stance-aware IK and a proper asset
writer. Publish the result in the catalog only after visual approval. At that
point capture real engine frames, update the MotionWeave devlog and mark the
roadmap item `done` after integration into `main`.

## Preview follow-up

The focused test now optionally writes a 60 Hz, two-cycle joint CSV from the
actual generated clip through `bindPose`, `sampleClip` and `worldMatrices`.
`tools/animation/walk_preview.py` converts this CSV into a standalone HTML
player and an eight-phase PNG contact sheet using only the Python standard
library. The artifacts are under
`/Volumes/KINGSTON 1/AIVERSE-builds/motionweave-walk/preview/`.
The PNG was visually inspected. It confirms leg alternation but also makes
the mechanical motion and absent foot planting clear. The HTML browser player
was generated but could not be opened through the current computer-use bridge;
do not claim interactive browser validation. These are stick-figure pose
visualizations, not native Metal viewport frames, so they are not site media.

## 3D and root motion follow-up, 2026-11-23

- `WalkCycleSettings::rootSpeed` is opt-in, default zero. A positive value
  writes forward local +Z root translation into the clip. Bounds: 0–5 model
  units/s. The rotation seam stays closed; the root channel advances exactly
  speed times duration at the seam. Callers must preserve per-cycle root
  displacement when looping the clip.
- The focused test now uses 1.2 units/s, checks the root delta and exports
  two continuous cycles at 60 Hz. `generated-root-motion.csv` has 121 rows;
  root Z is 0, 1.2 and 2.4 at frames 0, 60 and 120 respectively.
- `tools/animation/walk_preview_3d.py` converts actual evaluated joints to a
  standalone 3D-projected canvas player with camera orbit and a PNG four-pose
  sheet. Output is under the same KINGSTON preview directory. PNG inspected;
  JavaScript syntax checked with Node. Browser interaction itself was not run.
- Rebuilt targeted native test and reran CTest: 1/1 PASS. `git diff --check`
  passed. This is still a stylized skeletal visualization, not a skinned
  character or native renderer capture. Foot sliding remains visible.
