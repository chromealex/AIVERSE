# Ada head/groom binding — 2026-11-20

User identified detached hair and head/shoulder geometry protruding through the
shirt. The prior static-frame attachment conclusion was incorrect and withdrawn.

Shoulder code eb4cc813b: head has upperarm joints but no lowerarm chains, so previous
pose generation skipped them. Compatible bind-frame direction transfer now moves
head shoulders with body/clothes. Scoped session test PASS; native T-pose removes
large exposed patches, with small collar-edge intersections still present.

Hair2264 and brows2263 were unskinned children of vivid_full_root, not the mapped
vivid_face_head. Offline attach_grooms.cjs reparents both with exact inverse-TRS
compensation. Actual bind matrix error <4.4e-7. Brows retain46 morph targets and
use named facial channels from the same pose; lashes remain inside the skinned
face. This is rigid groom-card following, not UE strand bindings or hair physics.

Updated external KINGSTON content/Ada/model.gltf by changing hierarchy only,
keeping original node/skin/accessor indices and buffers. Original JSON backed up
under full-character/groom-binding/source-model-before.gltf. Separate assembled
content/Characters/Ada-Groom-Bound/model.glb is used for capture; old assembled
packages remain unchanged. bindings.json records both assets. No binary assets
are committed to Git. Future companion preparation binds these card nodes too.

Checks actually executed:
- Node test_attach_grooms.cjs PASS: unchanged bind, rotation/nonuniform scale,
  morph ownership, idempotence, cycle/skinned rejection, GLB roundtrip.
- Full prepare_full_character.cjs on original ada-source.glb with material/texture
  conversion PASS; output groom-binding/prepared-pipeline, prepare.log.
- Creator CMake PASS on KINGSTON: build-groom-binding.log and final
  build-groom-capture-lifetime.log. No Studio frontend changes.
- Native head-final/:168 frames, 7s@24fps; poses-final/:360 frames, 15s@24fps.
  Both successful native captures, actor=MODEL_MORPH, pipelineFailures=0.
  Diagnostic --preview-head-sweep uses head yaw±0.45rad/pitch±0.20rad, not a new
  production idle. Inspected left/right/tilt and blink plus final T pose.
  Hair follows those head poses; no gross detachment in inspected frames. Fine
  card edges, gray eyes and small collar intersections remain visual failures.
- UI interaction, all rigs, true strand deformation and real-time frame pacing
  NOT RUN. No CI/push. These are offline native pixels, not retouched images.

Important failed attempt: initial body-poses/ stopped after238frames, exit139.
System crash report capture-crash.ips: Animator::advance dereferenced old Model.
failed-last.png shows revision2 and head scale0.964; model was rebuilt during the
interactive recording. Diagnostic Animator had retained the retired model pointer.
Fixed publication/release to rebind/clear it; recording now omits interactive
controls and explicitly rejects unexpected mid-recording rebuilds. Ordinary Creator
UI unchanged. Both complete final captures ran after this fix. The head-sweep/
first7s capture succeeded before it; body-poses/ is NOT final acceptance evidence.
LLDB retry stalled in Rosetta launch and was explicitly stopped; no unrelated
process stopped. System crash report supplied the usable stack instead.

Artifacts: /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/groom-binding/
head-final.mp4, poses-final.mp4, head-final.png, t-pose-final.png, gallery.html.
Source frames, logs, failed capture and backups remain there. Last user request
now prioritizes gray eyes: actual baked iris PNG contains brown iris/black pupil,
so next isolate eye-shell/edge/render UV/texture binding instead of inventing color.
