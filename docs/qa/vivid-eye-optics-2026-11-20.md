# Layered eyes and eyelid edges — native QA, 2026-11-20

User expectation: detailed colored irises, corneal depth/live highlights, smooth
eyelash/lid edges, movement evidence, all builds on KINGSTON.
Reference controls: [Epic eye material tools](https://dev.epicgames.com/documentation/metahuman/eye-material-tools).
No downloaded Epic shader source or MetaHuman parity certification.

Branch integration/motionweave-studio-facial-stage184; baseline20fda55e1.
Native macOS Metal, Creator deterministic offline24fps,1500x920. No UI automation.
Root for all evidence: /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/eye-optics/.

| ID | Scenario / actions | Result | Evidence |
|---|---|---|---|
| E1 | Catalog portrait, original versus detailed iris material | Iris fibers/dark pupil preserved, no gray wash | optics-first.png, final-detail.png |
| E2 | Isolate white eyelid flecks: remove lashes/overlays, black eyes, unlit head, shrink eyes | Those hypotheses did not remove flecks; diagnostic only | no-lashes.png, edge-diagnostic.png, head-unlit.png, fit.png |
| E3 | Correct cartilage strip pigment/roughness, retain all geometry | White jagged strip gone in matched camera/light | cartilage-closeup.png versus eye-review/final-closeup.png |
| E4 | Portrait light,168frame yaw/pitch sweep plus two blinks | Complete capture; reviewed open/turn/blink frames | motion.log, turn.png, blink.png |
| E5 | Reload updated editable source manifest | Layered materials present, shading3/radius.14/depth.012/IOR1.376, actual native image | source-final.log, source-final.png |

Investigations: first --eye-detail distance.5 placed camera inside the head;
`detail.png` is a FAILED diagnostic framing, not a deliverable. Corrected to.85.
The .96 eye-scale candidate is NOT adopted. Eyelashes stay alpha blended with
original coverage pixels and51 morph targets; no opaque replacement or deletion.
Cartilage was the primary white-edge source. Hair card silhouette, small skin
normal-map artifacts and detailed facial corrective quality remain limitations.

Validation: Node test_bake_materials.cjs PASS (test-baker.log), now tests iris
contrast despite saturated B radial mask, pupil darkness, matte lid fallback.
C++ tests cover GPU material optical state distinction and GLB optical roundtrip
plus reassembly. Initial studio session test failed its old schema count72 after
three controls were added; expected count updated to75, rerun passed. Logs retain
that initial failure (tests.log), rather than hiding it.

Editable source content/Ada updated by appending eye textures and material
metadata, preserving buffer/accessor/node IDs. JSON backups in source-backup/.
New assembled candidate content/Characters/Ada-Eye-Optics. Historical assembled
characters unchanged. No art assets committed to Git.

NOT RUN: Studio GUI controls, Vulkan/OpenGL/mobile GPU, real-time performance,
ray-traced reference comparison, every animation/pose/face preset. The shader's
medium/high cornea uses an extra bounded light evaluation; low skips that cost.
Orchestrator catalog search failed on pre-existing missing/unregistered skills;
used the actual local VIVID, renderer and native-test entry points.

## Final verification

Native targets Studio, Creator, model_batching_policy, vivid_human_wardrobe,
vivid_human_appearance and studio_human_appearance_session built on KINGSTON
(build-complete.log; wardrobe fixture corrected/rebuilt in build-test-fixture.log).
Final `run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
'^(model_batching_policy|vivid_human_wardrobe|vivid_human_appearance|studio_human_appearance_session)$'
--ctest-jobs1`: PASS4/4, report20261120-200027-32474 (`tests-verified.log`).
The new reassembly fixture initially omitted required hiddenMeshes:[] and failed
validation; corrected the fixture, not the validator. See tests-complete.log.

GLSL syntax:12/12 stages PASS for ES300/desktop330, Q0/Q1/Q2, skinned+morph+normal
permutations in LDR (`glsl-validation-final.log`). First standalone harness
omitted the backend ColorSpace prefix, causing6 fragment failures; final harness
uses the real colorShaderPrefix. This is syntax validation, not GL/Vulkan GPU QA.

Final native motion:168 frames,7s at offline24fps,1500x920, portrait light, yaw/
pitch sweep and blinks. `final-motion.mp4`; inspected turn36 and tilt110, plus
earlier same-motion blink41. `final-tilt.png` shows view-dependent corneal glints
without wiping iris/pupil. pipelineFailures0, no model rebuild/capture error.
`final-catalog` is the matched1.05-distance catalog camera comparison.
No real-time FPS claim or full frame-by-frame review.

Status: implemented/verified iris contrast, native optical depth/Fresnel coat,
cartilage white-fleck repair, alpha/morph retention, export/reassembly.
MetaHuman reference parity remains unverified; optical model is an approximation
and the existing source still has skin-normal/card-hair artifacts.

## Local-glint refinement (supersedes final-catalog/final-motion above)

Final matched catalog review caught excessive pale glare when scaling the entire
dynLight specular output: that output also contains broad grid/environment energy.
The delivered coat now evaluates up to8 actual point/spot-light directions with
attenuation/cone and a normalized sharp lobe, never amplifying the grid response.
Low quality retains the directional/environment coat and omits this loop/parallax.
Matched `local-catalog.png` inspected: brown iris and dark pupil remain distinct
with small localized highlights. No atlas/camera/exposure change for this test.

Creator/Studio/appearance-test rebuild PASS (build-local-glints.log).
Appearance CTest PASS1/1 report20261120-200337-34885, after prior complete4/4.
GLSL12/12 syntax stages PASS again (glsl-local-glints.log).
Remaining change only paired GLSL/MSL optical branch; old PBR path unchanged.

Delivered native take: `local-motion/`,168 frames completed, pass1/pipelineFailures0.
Inspected turn36 and full blink41 with the final local-light coat; sharp moving
highlights retain iris contrast, white cartilage flecks remain eliminated.
Final video name `eyes-reviewed.mp4`; gallery points only at the reviewed take.
The earlier final-motion.mp4 is retained as diagnostic history, not delivery.

## User rejection and blink-normal repair — 2026-11-20

The preceding reviewed eyes were explicitly rejected as glass-like by the user.
Do not interpret the earlier delivery as MetaHuman visual acceptance.
New evidence root: `/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/eye-natural/`.

Refinement: reduced/softened local cornea glints in GLSL/MSL, less bright sclera
and iris fiber contrast, UV-faded upper-lid occlusion instead of uniform shell
opacity. New default pupil radius .048 and sclera lightness .40; existing saved
values remain authoritative. Ada candidate uses pupil .048, authored iris/sclera
colors multiplied .75 before baking. Candidate `Ada-Eyes-Repaired` and editable
`content/Ada` updated; original source JSON/appearance backed up in source-backup.
Source node/topology/old accessor IDs preserved; new normal buffer/accessors appended.

User reported black stripes on blinking lids during this work. Controlled tests:
- Color-coded shell/lashes/cartilage: stripe remains on skin (diagnostic-blink.png).
- Unlit skin: stripe disappears (unlit-view.png); diagnostic only.
- Skin normal texture removed: stripe remains (no-normal-blink.png).
- Shadow map disabled: stripe remains (no-shadow-blink.png).
- Imported morph NORMAL deltas removed: stripe disappears (no-morph-normal-blink.png).
- Geometry-derived blink normal repair, with original normal map and shadows:
  broad dark stripe removed (repaired-blink.png, source-blink.png).

The explicit smooth-face helper recomputes area-weighted normals of base/target
triangles, welds coincident smooth vertices across partitions and applies their
change to authored base normals. Only eyeBlinkLeft/Right on companion head skin
are repaired; positions, skin weights, UVs and unrelated morphs retained.
Importer now applies this known companion repair. It is not a universal RigLogic
corrective solver. Small shadow/skin speckles and hair-card edge artifacts remain.

Executed: Creator/Studio/appearance-test native builds PASS (build-final.log),
final Creator diagnostic CLI build PASS (build-final-cli.log). CTest
`-R '^vivid_human_appearance$'` PASS 1/1 (test-native.log). Node baker and morph
repair tests PASS (test-baker.log, test-normals.log); full Draco source preparation
PASS (import-check.log). GLSL ES300/330 Q0/1/2 vertex/fragment syntax 12/12 PASS
using check-shaders and glslangValidator. No GPU Vulkan/OpenGL/GUI/CI tests.
Initial baker invocation failed because /Volumes/KINGSTON/tmp did not exist;
rerun used existing eye-natural directory. First no-shadow capture hit the smoke
gate's mandatory shadow-draw check; CLI now permits zero shadow draws only for
explicit --shadow-quality off. Defaults retain medium shadows.

Native Metal final-motion: 168 frames, 1500x920, 24fps; open, turn, both blink
peaks reviewed. source-check: 42 frames, editable source reload verified.
Video eyes-blink-fixed.mp4 encoded successfully (encode.log). Images
final-0000.png, final-0038.png, final-0041.png, final-0105.png are native output.
No image retouching; diagnostic disabled-normal/shadow variants not delivered.
No MetaHuman parity or user approval claimed. Next: broader face-expression
correctives and lighting-reference comparison, including remaining shadow flecks.
