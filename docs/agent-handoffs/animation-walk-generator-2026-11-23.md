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

## Ada native 3D follow-up, 2026-11-23

Implementation: `f1244cec3` on `feature/motionweave-walk-generator-current`.
The shared generator now derives each joint's local pitch axis from its bind
rotation, rather than assuming local X is horizontal. On the MetaHuman rig,
local X points nearly up, so the old preview barely bent its legs. The VIVID
presentation adapter `HumanWalkPreview.h` maps five modular Ada roots: each
complete body/clothing rig gets the same generated channels; partial roots
get matching travel. Creator's offline `--preview-walk` path samples the
published clip and accumulates its 120 cm/s source-root delta over each cycle.
The outer 0.01 scale yields approximately 1.2 m/s in the scene. The clip
index is committed only after successful model publication.

Actual native verification on macOS AppleClang 17 and Metal: Creator target
built; the focused walk test rebuilt and CTest 1/1 PASS; site check 37 entries,
0 errors; `git diff --check` PASS. Creator captured 48/48 frames at 24 fps
from the external `Ada-Groom-Bound` manifest, with `actor=MODEL_MORPH`,
`pipelineFailures=0`, 43 colour and 43 shadow submissions per frame. System
AVFoundation encoded the frames to a 2-second, 1500x920 H.264 MP4. Frames 6
and 36 were inspected. Artifacts:
`/Volumes/KINGSTON 1/AIVERSE-builds/motionweave-walk/ada-groom-bound-preview/`
including `ada-generated-walk.mp4` and `frame-0006.png` / `frame-0036.png`.

The initial capture used the older `Ada-Motion-Review` package. Its groom
detached during root motion, matching the known source asset defect; that
capture is not acceptance evidence. The final capture uses corrected
`Ada-Groom-Bound`; no external asset was modified or committed.

Remaining: the gait has no stance contact IK or foot planting, and visual
quality is not yet suitable for the public catalog. No Studio interactive
preview, non-Metal backend, full CTest, performance or release packaging was
run. Next implement ground-aware foot targets and stride calibration on Ada,
then repeat the native motion capture and compare foot drift on world-space
ankle tracks. Keep this site roadmap item `in-progress`; add public devlog
media only when the result passes visual review.

## Ada A-pose and independent head motion, 2026-11-23

Implementation commit: `0865842d6` on
`feature/motionweave-walk-generator-current`.

The shared `classifyWalkArmRestPose` compares bind shoulder-to-elbow direction
with model down and reports Relaxed (within 30°), A (30–70°), T (above 70°),
or Unknown for missing/degenerate joints. Generated shoulder channels lower
A/T arms toward a relaxed downward direction before adding walk swing; already
relaxed arms retain their bind direction. Optional neck and head receive
separate phase-lagged yaw and pitch channels. The modular-human adapter maps
lower arms, neck and head across all relevant skin roots and reports actual
classification counts.

Actually run: AppleClang 17 native walk-test and VIVID Creator builds PASS;
`ctest -R '^motionweave_walk_generator$'` 1/1 PASS, now including relaxed,
A and T classification, arm-lowering direction, head-channel change and
clip seam. `python3.12 tools/site/build.py check`: 37 entries, 0 errors;
`git diff --check` PASS. The corrected external `Ada-Groom-Bound` was captured
on Metal: 48/48 frames, `pipelineFailures=0`, 43 colour and 43 shadow
submissions per frame. System AVFoundation encoded 1500x920 H.264 at 24 fps
to `/Volumes/KINGSTON 1/AIVERSE-builds/motionweave-walk/ada-pose-head-final/ada-walk-arms-head.mp4`.
Frames 0 and 6 were inspected. A final one-frame diagnostic capture from the
same asset and build reported `rigs=4`, `arms relaxed=0 A=8 T=0 unknown=0`,
`headChannels=9`, showing Ada is recognized as an A-pose across the four
complete modular rigs. No source Ada asset was edited or committed.

Limits: the head motion is a small authored procedural variation rather than
gaze tracking or physics; the gait still lacks planted-foot IK and natural
weight transfer. The full VIVID suite, Studio UI and other GPU backends remain
untested. Next stage should solve foot contact on Ada and review a full-body
video side by side with this baseline before marking the roadmap item done.
