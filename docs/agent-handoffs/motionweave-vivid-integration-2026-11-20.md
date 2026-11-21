# MotionWeave + VIVID integration — 2026-11-20

Branch: `integration/motionweave-studio-facial-stage184`.
Merge implementation: `1d8c76e99c924f0373067ba413fac7b5d4803018`.
Parents: Studio/MotionWeave `04b4b3dc3` and VIVID `b5bbd3b319f67758ee0bb61c9061a78f99c00c25`.
User explicitly requested this merge and further joint completion. ALL subsequent builds, logs and artifacts MUST live on KINGSTON: `/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/`. Isolated source worktree: `/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/motionweave-completion`. Main user Studio worktree not modified.

## Reconciliation
15 conflicted files reconciled, retaining both histories and independent features: Model upload keeps 64-channel source morph bounds/aliases and transactional shared-texture publication; Renderer3D keeps Studio cache/batching and VIVID dynamic vertex override/provenance. Dynamic overrides bypass static caches, legacy morph blending and LOD. Tracked submission frames bypass packet replay to avoid stale invocation IDs; pure cache policy test added. Metal completed serial and retirement token APIs both retained; Vulkan failed fence does not collect garbage. WorldScene joins CPU workers and fences GPU before release, retaining Terra/NEREID cleanup. FPS timing/router and animation resource retirement both retained. Studio renderer receives the same GPU epoch as facial preview and polls retirements at frame boundary. Shared ModelVertex stays 60 bytes, MotionWeave remains pose owner; VIVID 52 semantic channel order and gameplay saves unchanged. MotionWeave Studio 64-channel shader ABI retained (requires hardware validation).

Fixed old web fixtures for facial_rig tab, current dock defaults and folders in assets envelope. No layout/runtime mock behavior changed.

## Actual verification
- `npm --prefix apps/studio-web run build`: PASS before merge (TypeScript + Vite).
- `npm --prefix apps/studio-web run test:facial`: PASS 4/4 before merge.
- `TMPDIR=/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/tmp npm --prefix apps/studio-web run test:web`: PASS 73/73 after fixture fixes, log `web-tests.log` on KINGSTON.
- CMake Debug configure with Studio HTML5/tests/tools/runtime AutoLOD ON: PASS, native directory on KINGSTON.
- `cmake --build /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/native --target aiverse_studio aiverse_studio_facial_runtime_tests aiverse_studio_facial_preview_binding_tests aiverse_auto_lod_tests aiverse_vivid_creator aiverse_vivid_renderer_preview -j 8`: INCOMPLETE/FAIL detected in RtsView.cpp:49 (void init used as bool) and :498 (ScenePacketBatch has no empty). Common models and runtime compiled; full build not successful. See `build.log`.
- Native CTest, Metal pixels, GPU validation, Vulkan/GLES, performance: NOT RUN. CI not dispatched/queried. Imported historical Markdown trailing spaces/EOF blanks reported by cached diff-check; no conflict markers remain.

## Next
Fix RtsView API mismatches without changing animation ownership, finish native build, then focused combined animation/VIVID/Studio CTests and actual Metal preview. VIVID remote advanced during work; reconcile its portable fixture fix only after this checkpoint. Studio shared ModelAssetProvider/read lease/async decode adoption remains unfinished (Stage 186). No main merge, push, release or production-ready claim.

| Subsystem | Status | Remaining |
|---|---|---|
| Combined MotionWeave + VIVID source | implemented in source | native build fixes |
| Studio frontend | partial | native WKWebView screenshot |
| Shared provider in Studio facial preview | not implemented | adopt existing provider safely |
| GPU/backend acceptance | not tested | Metal pixel suite; Vulkan/GLES |

## Native build checkpoint — 2026-11-20

Implementation: `4f7d6afd60c962b7a6594aea97f8eef01acc0ce0`, same integration branch. Fixed RTS retirement result forwarding and ScenePacketBatch size query; qualified shared render::findNodePath and made retarget/loader headers self-contained; corrected VIVID groom subtraction, Creator slider helper pointer type, and portable gfx fixture namespace (also present in upstream 6064542).

Actual KINGSTON builds: Studio, facial runtime/binding, AutoLOD, VIVID Creator PASS. Remaining acceptance target build still running in `build-fix4.log`. Earlier failed attempts retained as build.log/build-fix1..3.log. No animation save schema, 52 channel ordering, UI layout or gameplay ownership changes.

Unified Debug runner (tests-first.log): 12/17 PASS, 5 FAIL: auto_lod_runtime (malformed envelope throws), vivid_human_appearance (irisRadius fixture out of schema bounds), vivid_human_import (invalid fixture accepted), vivid_metal_sculpt_id (record rejected), vivid_actor_retirement (initialization rejected). Real Metal GPU and RHI tests PASS. Studio facial runtime and binding PASS. Python Studio contracts: 24/29 PASS; stale CMake/asset catalog/doc assertions and Python 3.8 Path.is_relative_to compatibility need repair. Do not report full suite passing. CI NOT RUN; GUI/pixel preview NOT RUN yet.

Next: finish acceptance target build, diagnose the five failures without relaxing runtime validation, update stale contracts, run combined tests and real Renderer3D preview. Studio async provider adoption remains open. Correction to previous entry: RTS error was void collectRetired used as bool (not init).

## Combined Metal acceptance — 2026-11-20

Code: `647b7bc2e07ab1f99ca567022ac94cfed0d80dec` on this branch. Fixed reserved MSL identifiers `vertex` and `thread` in both actual-stream expansion kernels without changing CPU/GPU struct layout. GPU actor initialization now works before beginFrame; dispatch still rejects zero frame serial, covered by the original zero-frame fake RHI plus new rejection assertion. Updated malformed AutoLOD rejection, valid iris bound, real newline rejection and portable namespace fixtures. Studio source-contract assertions follow current asset catalog and documentation; bundle check supports system Python 3.8.

Actual verification (KINGSTON):
- Native selected Debug tests: 24/24 PASS (`tests-second.log`). Additional owner/Metal ID/actor regression: 3/3 PASS (`tests-gpu-fixed.log`), actor overlaps previous set: 26 distinct passing tests.
- Real Metal sculpt ID/depth/readback: PASS, `metal-id-fixed.log`.
- Real single-actor Renderer3D + MotionWeave preview: PASS (`preview-actor-fixed.log`); 81,989 morph-changed pixels, 63,241 MotionWeave-changed pixels. Both same-frame colour/ID depth probes PASS with zero reported delta. PPM files: preview-baseline, preview-morph, preview-motionweave on KINGSTON. These verify a procedural fixture, not photoreal human quality.
- Studio Python: 29/29 PASS (`studio-python-final.log`); TypeScript typecheck PASS (`web-typecheck.log`). Web suite remains 73/73 PASS from merge.
- VIVID Creator, preview, Studio and focused targets compile. CI, Vulkan/GLES, complete regression, Studio visual parity, imported Creator and mask/crowd GUI cases NOT RUN yet.

Next: validate mask and crowd scenes; close 64-slot VIVID adapter coverage; continue shared provider/lease adoption in Studio. No performance or full production parity claim.

## Full palette and masked scene checkpoint — 2026-11-20

Code `1cd6b732a100b8a01c302f2749b1d0bce2b4ee5e` on the same branch. MorphBinding documentation/diagnostic now follows shared kMaxMorphTargets, with regression covering all 52 fixed semantic channels, last palette slot and rejection just past the limit. No ABI/order changes. Renamed another reserved MSL `vertex` identifier in the preview-only mask producer.

KINGSTON validation: VIVID morph binding + real MotionWeave bridge 2/2 PASS (`tests-palette.log`); both single-mask and two-mask real GPU scenes PASS (`preview-mask-fixed.log`, `preview-multi-mask-fixed.log`), alpha-hole and solid-occlusion probes report identical scene/ID depths. 12 independent actors PASS (`preview-crowd.log`: 179676 morph pixels, 183285 animated pixels). Actual Creator skin/eye/groom smoke PASS (`creator.log`, `creator.ppm`). Inspected preview-motionweave.png: procedural deformation fixture visible on shadowed floor, NOT photoreal face quality. Imported Creator/optical sweeps/full backend parity NOT RUN. Build log build-palette-mask.log. CI NOT RUN.

Next: finish native Studio facial preview ownership using shared ModelAssetProvider, async CPU decode, frame read leases and fenced retirement; imported Creator and broader animation tests remain.

## Studio async provider completion — 2026-11-20

Code `4a719af8c1c5a97b381c768155e28fd858877903`, branch `integration/motionweave-studio-facial-stage184`. Studio facial preview now owns one shared ModelAssetProvider instead of a direct unique Model/upload/release loop. CPU decoding/preparation uses its worker queue; takePrepared permits facial rig validation BEFORE publishPrepared performs the existing ModelStagingTransaction. Fresh publication keys retain last-good until success. Replacement invalidates renderer caches and uses the cache's fenced retirement; a ModelReadLease spans Animator evaluation through Renderer3D rendering/endFrame. CPU source cancellation is separate from generation retirement. Project changes drain the owner and texture cache. Failed epoch/fence cleanup quarantines the stopped provider, preserving GPU safety (exceptional memory retention remains intentional). One Animator remains pose owner; no browser file writes, animation schema or 52-channel order changes. Public provider additions are source-compatible; existing load/publishReady unchanged.

Actual checks, all outputs on KINGSTON:
- `cmake --build .../native --target aiverse_studio aiverse_studio_facial_gpu_tests -j 8`: PASS (`build-final-studio.log`). Also built model_asset_provider_api, async_model_cpu_queue, model_image_transaction targets. Existing unused-lambda-capture warning in ModelResourceCache.h remains.
- `TMPDIR=.../tmp bash run-tests.sh --no-build --build-dir .../native --filter '^(studio_facial_(gpu|runtime|preview_binding)|model_asset_provider_api|render_model_image_transaction|render_async_model_cpu_queue)$' --ctest-jobs 1`: 6/6 PASS (`tests-final-studio.log`, report `native/test-reports/20261120-143957-3541`). Real Metal Studio GPU test verifies asynchronous glTF import, render read lease, bad reimport retaining last-good, superseded same-source request and safe replacement/clear.
- `PYTHONDONTWRITEBYTECODE=1 TMPDIR=.../tmp python3 -m unittest discover -s tools/studio -p 'test_*.py'`: 29/29 PASS (`studio-python-provider.log`). git diff --check PASS (line-ending conversion notices only).
- Real Creator imported OBJ/manifest/morph Metal smoke: PASS (`creator-import.log`, `creator-import.ppm`).
- Earlier failed native GPU-test setup attempts: build-studio-gpu-test.log had missing new Make target until explicit CMake regenerate; build-studio-gpu-test2.log lacked aiverseMain. Corrected entrypoint, regenerated and verified subsequent builds. No hidden passing claim for those attempts.

Cumulative scoped coverage: 30 DISTINCT native tests PASS across recorded runs (some repeated), 73 web tests PASS, 29 Python Studio contracts PASS, single/crowd/mask/Creator Metal cases PASS. This is not the full engine suite. CI not dispatched; Vulkan/GLES, complete animation roadmap, optical sweeps, production human art and WKWebView screenshot parity remain NOT VERIFIED. Source worktree has only local untracked dist/node_modules symlinks used for external builds/reused dependencies. No push or main checkout edits.

Next actionable work: integrate the latest VIVID documentation/portable fixture ancestry, then cross-platform/Studio UI visual acceptance when those hosts are available. The prior shared Studio provider gap is closed and covered by native tests.

## Final VIVID ancestry reconciliation — 2026-11-20

Merge `ef11263a5afec830c4522c5ed7d719af5fad01aa` includes VIVID `2b7c4f967309726562afc84c0672fdbfca9a4c29`. Only additional documentation changed relative to tested parent; the upstream portable BufferHandle fix was already included. One append-only vivid.md conflict resolved by keeping BOTH upstream historical Stage 7BA and our later actual validation entries, without silently treating upstream source-only claims as test passes. No code rebuild required for this documentation-only delta. `git merge-base --is-ancestor 2b7c4f... HEAD`: PASS. Aggregated latest per-test results from actual JUnit logs: 30 distinct native tests, all PASS, no unresolved failures. Tracked worktree clean; local dist/node_modules symlinks are intentionally untracked. No push, CI or main checkout mutation.

| Area | Current readiness |
|---|---|
| MotionWeave + latest available VIVID ancestry | Implemented and merged |
| 52 semantic / 64 Model palette CPU routing | Implemented; focused regression PASS |
| Single/crowd/masked Metal rendering and Creator | Implemented; recorded procedural/import fixtures PASS |
| Studio async import, last-good and frame leases | Implemented; real Metal lifecycle PASS |
| Complete cross-platform/photoreal/Studio UI parity | Partial; Vulkan/GLES, production artwork and WKWebView screenshot parity NOT VERIFIED |

Deliverables: `/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/native/apps/studio-native/AiverseStudioHTML5.app`, native `aiverse_vivid_creator`, logs and captures in the same external build root. Source remains the isolated integration worktree. Next: Windows/Linux/Vulkan/GLES builds on corresponding hosts and visual Studio acceptance; use this joint handoff rather than restarting from earlier source-only stage notes.


## Visual acceptance repairs — 2026-11-20

Implementation: `a16fbb965ad88e6d120592f6c0db5840d12d675b` on
`integration/motionweave-studio-facial-stage184`; starting HEAD
`9340e8c7ffa79db9d47e6a550653f3925a76bc73`.

**Correction of earlier status:** old Creator smoke PASS was insufficient visual
acceptance. Inspection of its actual screenshot showed only hair: CPU actor
borrowed a sparse library from a candidate subsequently moved into the live asset.
Floor/hair draw counts let that failure pass. This entry supersedes earlier claims
of visible Creator head acceptance, but not independent renderer parity fixtures.

Changes: CpuActorStream owns an immutable sparse-library snapshot, bounded with
its fixed vertex payload; source replacement is covered by the CPU actor test.
Creator can select compute before frame 1; `--cpu-morph` explicitly exercises the
fallback on Metal. Smoke requires head attachment AND actual tracked head commands,
logs real draw counts. Procedural facial parenting preserves world TRS rather than
applying head scale twice. Iris is ahead of sclera, inside cornea. High/compact iris
graphs use XYZ length: UV W=1 previously made all pupil/iris masks empty. Camera,
floor and a frontal authoring key reveal the bust and eye glints. Existing MSL
source-contract tests now match vertexIndex/threadIndex from the earlier compiler
fix. Validation script builds CPU actor tests and captures CPU fallback.

Paths: engine/character/vivid/{CpuActorStream.h,HumanPreview.cpp,HumanMaterials.cpp},
tests/vivid/{cpu_actor.cpp,human_appearance.cpp,human_creator.cpp},
tools/vivid-creator/{main.cpp,README.md}, tools/vivid-validate.sh, docs/vivid.md.
Public call signatures, DNA/save format, 52 semantic channels and 64 Model palette
unchanged; CPU actor now owns its morph snapshot (initialization-only copy). No
second scheduler, authoritative ECS edits, renderer shader changes or default
experimental batching/ICB changes.

Actually executed (all build/log/image artifacts under
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid`):
- CMake builds Creator, CPU actor, human appearance and human creator tests: PASS.
  Final logs build-iris-radius.log, build-creator-contract.log, build-creator-light.log.
- Rebuilt aiverse_vivid_renderer_preview and aiverse_studio: PASS,
  build-visual-deliverables.log.
- `TMPDIR=.../tmp bash run-tests.sh --no-build --build-dir .../native --filter
  '^vivid_(cpu_actor|human_appearance|human_rig|human_creator|motionweave_bridge)$'
  --ctest-jobs 1`: **5/5 PASS**, tests-visual-accepted.log,
  native/test-reports/20261120-145600-39306. CPU lifetime test uses fake GLES device;
  real fallback capture below runs on Metal, not native GLES hardware.
- Final Creator `--smoke`: GPU, --cpu-morph, imported OBJ manifest, seven frozen
  optical cases, mobile low config and medium materials: **12/12 PASS**.
  Actual head command counts are 24 procedural / 1 imported. Files/logs in
  visual-acceptance-final/. creator-gpu.png visually inspected: head, facial parts,
  coloured iris and pupils, torso and shadow present; crude procedural test art.
- optical_ab.py with original ROIs, threshold=4, min-changed=1: **3/3 PASS**.
  Skin .10/4.0: 56233 changed pixels; eye IOR 1.25/1.45: 44; hair .08/.90: 2313.
  JSON/logs in visual-acceptance-final/{skin-scatter,eye-ior,hair-rough}.*.
  These prove parameter response in this scene, not physical accuracy.
- Real Metal MotionWeave/VIVID preview: capture-pass=1; morph 81989 changed pixels,
  MotionWeave 63241; both same-frame colour/ID/native depth probes PASS, delta=0.
  visual-acceptance-final/motionweave.log and three PPM captures; animated PNG
  inspected (deliberately simple geometry fixture, not a realistic head).
- bash -n tools/vivid-validate.sh and git diff --check: PASS. Entire branch-fetching
  vivid-validate.sh NOT RUN because it switches to the separate upstream branch.

Recorded failed attempts: build-face-transforms.log initially lacked Animation.h
in appearance test, then fixed. tests-visual-fixes.log and
 tests-visual-fixes-final.log each failed human_creator's stale MSL string checks;
all corrected and rerun above. Original top-lit eye IOR A/B failed (max RGB delta1,
no pixels above threshold), including full viewport diagnostic; final frontal key
passes WITHOUT lowering threshold. Older images remain under visual-acceptance/
for provenance; use visual-acceptance-final/ for final lighting/material captures.

CI NOT DISPATCHED (no URL); no push or main checkout edits. Full engine suite,
Vulkan/GLES hardware, production artwork and Studio WKWebView interaction/screens
NOT VERIFIED. Native Mac UI was locked; user was asked to unlock, no reply received.
CLI framebuffer captures succeeded independently. No synthetic UI screenshot.
Next action: after Mac unlock, inspect Studio Facial/animation windows via CUA,
exercise controls and take UI screenshots; then cross-backend acceptance on real
hosts and production character assets. Do not label the whole VIVID roadmap done.


## Real MetaHuman DNA sample import — 2026-11-20

User explicitly requested a ready internet DNA character and screenshots, then
selected **actual MetaHuman .dna, add compatible import first**, rather than a
substitute glTF character. Code: `af940c85dbfa7967a836e6b300522e34f261b9c3` on
`integration/motionweave-studio-facial-stage184`; parent
`b6c17725d3fd97d24e005c960a5e60977f2bd8f1`.

Source: official Epic MetaHuman-DNA-Calibration revision
`8297ff1bd6f1692a6e221aa8395a66486e5a53c9`, data/dna_files/Ada.dna.
SHA256 `e764cfab938283231bcb3e5af1c7bcbec5154bab4804aafa1b5b7dc6e400ca5e`.
Internal DNA descriptor is "Archtype" (filename is Ada). SDK, original sample,
converted assets, builds and captures live ONLY under
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/metahuman/`; not vendored.
SOURCE.json records source URL/revision/hash and local SDK license path.
The official repository supports legacy UE <=5.5 DNA, not 5.6+; current general
MetaHuman licensing and the older Calibration license are separate documents.
No rights to redistribute the SDK or sample with AIVERSE are asserted.

Changes: tools/vivid-dna-import/{CMakeLists.txt,decode.cpp,package.py,README.md,
test_package.py} implement a separate authoring adapter linked to an external
user-supplied official DNA reader. Decoder exports selected LOD geometry, all
source target names and optionally one exact target's deltas. Package creates
glTF + VIVID JSON/manifest, expands sparse deltas across UV/normal seams, records
preview center/scale and optional baked weight. No SDK/game runtime coupling.
Creator `--smoke --still` freezes MotionWeave and snaps facial weights for controlled
captures. docs/vivid.md and Creator README document partial support.

Actual native/sample checks:
- External SDK CMake + Apple Silicon Release reader/adapter: PASS,
  import-configure.log, import-build.log, import-build-final.log. SDK has ordinary
  compiler warnings and ranlib empty platform translation units; build succeeded.
- Creator rebuild: PASS, build-still.log. No new Studio build required; adapter is
  offline and --still is standalone Creator-only.
- Real Ada decode LOD0: **9 meshes, 34625 layout vertices**, head lists 681 source
  blendshape targets. Selected exact source target `brow_raiseIn_L` exported.
  Final rebuilt decoder output byte-compares equal to prior successful output.
- `PYTHONDONTWRITEBYTECODE=1 TMPDIR=.../tmp python3 -m unittest discover
  -s tools/vivid-dna-import -p 'test_*.py'`: **4/4 PASS**, package-tests-final.log.
  Cases: UV seam morph expansion, invalid layout rejected without publishing,
  baked+live double deformation rejected, existing output preserved.
- Reader negative tests: invalid binary signature, out-of-range LOD 99 and `0junk`
  all rejected without successful output files (bad-signature.log, bad-lod.log,
  partial-lod.log). Input capped at 512MiB and LOD layout vertices at 1M; this does
  not claim hardened adversarial parsing of the external SDK.
- Real Metal `aiverse_vivid_creator --smoke --still --asset <manifest>`:
  ada-neutral-final, ada-three-quarter, ada-brow-live and ada-brow-cpu
  (`--cpu-morph`) **4/4 capture PASS**; each has .log/.ppm, PNGs of first three.
  Actual accepted head provenance commands=9, opaque/shadow counts=5 after hiding
  untextured helper surfaces. Front and 3/4 PNGs visually inspected: recognizable
  human source geometry, not procedural spheres, no generated replacement wig.
- Explicit diagnostic live route: VIVID browInnerUp -> source brow_raiseIn_L, gain1;
  expressionRest[2]=1. This maps ONE left source target for test, not a faithful
  bilateral ARKit/RigLogic mapping. Neutral vs live: 200 changed viewport pixels
  at RGB-total threshold4. GPU vs CPU: one changed pixel of 586500 (max RGB sum8).
  **Strict bit-exact image equality FAILED**; bounded visual similarity only.
  Both actor paths rendered correctly. morph-parity.json preserves all counts.
- git diff --check PASS. Full engine/Studio/web suites NOT RUN (unchanged runtime
  systems outside Creator capture option); no CI/push, no main checkout changes.

Failed discovery commands: initial unquoted GitHub tree URL hit zsh glob expansion;
quoted retry succeeded. media.githubusercontent.com Ada URL returned 404 (file is
ordinary Git blob); raw.githubusercontent.com pinned URL succeeded. No fabricated
sample was substituted. Earlier first Ada render had opaque helper eye shells and
Creator-generated hair; final package keeps helper geometry but explicitly hides
untextured eyelash/eye-edge/tear/saliva surfaces and tags authored eyelashes so
Creator does not add its procedural wig. Final screenshots are untextured/clay
materials; they do not show original MetaHuman skin/iris textures or groom.

| Subsystem | Status | Remaining |
|---|---|---|
| Legacy binary DNA reader + geometry package | implemented in source; Ada Metal tested | Other DNA versions/axes, broader assets |
| One exact source morph, GPU/CPU routes | partial; Ada tested | Full corrective combinations, normals/tangents, semantic mapping |
| RigLogic, original skeleton and skin weights | not implemented in this adapter | Behavior/joint solver and full expression parity |
| Original textures, eye shading and groom | not implemented in this adapter | Obtain companion licensed assets; DNA alone lacks them |
| Newer UE DNA, Vulkan/GLES, full Studio UI | not tested | Matching SDK and backend/UI hosts |

No VIVID save schema, canonical 52-channel ordering, authoritative ECS state,
shared animation scheduler or GPU ABI changed. Next actionable step: expand the
explicit adapter to original skeleton/skin weights and RigLogic behavior with
real reference-pose tests; obtain companion texture/groom assets separately.
Do not call this complete MetaHuman runtime support or a production character.


## Full Ada companion-character capture — 2026-11-20

User requested a full character because the preceding DNA sample showed only the
head. Code `3699a445d1383fe7048f9fa8afe6443575b77272` on
`integration/motionweave-studio-facial-stage184`, parent
`25d60660c4f17089d8b5c68e57f8338fb7ca29fa`.

Discovery: Epic's old Ada.dna contains no full-body mesh. Its public data/body
folder contains skeleton Maya files only. Found separately published full Ada
export by CreateGameCharacters:
https://creategamecharacters.github.io/metahuman-to-glb/characters/ada/ada.glb
Repository https://github.com/creategamecharacters/metahuman-to-glb
SHA256 `70ad5d190e3cdfb54e88d405b75d7b4731547f2bd93d5cbad0ce4720946d99de`.
This is a separate UE 5.6 companion GLB, NOT a body generated from the previous
legacy DNA file. The user was told this distinction before and after import.

Changes: new tools/vivid-dna-import/prepare_full_character.cjs decodes Draco via
external pinned tooling (@gltf-transform/core + extensions 4.2.1, draco3dgltf 1.5.7),
preserves mesh/skin/texture data, assigns unique duplicate garment bone names and
selects the actual face skin head. A parent frame normalizes the whole figure,
with optional yaw. Published mh_materials.json provides uniform cloth colours,
roughness/metallic fallback because the GLB base-colour input is a UE RGB region
mask. This is not complete original UE cloth-detail shader reconstruction.
Authored hair tags prevent Creator from adding procedural hair. Documentation
updated in adapter README and docs/vivid.md. Creator's demo idle translation now
uses bind + bob instead of absolute zero, preserving nonzero imported head offsets.
No new scheduler, authoritative gameplay, DNA schema or shared renderer edits.

Actual checks (all artifacts in
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/):
- `cmake --build .../native --target aiverse_vivid_creator -j 8`: PASS, build.log.
- Node preparation front/quarter: PASS, prepare-final.log, prepare-quarter.log.
  node --check script and git diff --check PASS.
- Input/output asset structural checks: preserved **7 meshes, 15 primitives,
  5 skins with unchanged joint-count multiset, 16 texture images**, removed Draco
  requirement. Source has 119916 vertices; native skin-palette splitting gives
  125302 displayed vertices. Garments include shirt, slacks, flats, full body,
  face, eyebrow cards and hair cards.
- `aiverse_vivid_creator --smoke --still --asset <manifest>` final front and ¾
  GPU captures PASS; front with --cpu-morph PASS. Logs/images:
  ada-front-final.{log,ppm,png}, ada-quarter-final.{log,ppm,png},
  ada-cpu-final.{log,ppm}. Actual tracked head/model commands=47,
  shadow=41, colour=41, pass=1, pipelineFailures=0.
- Both final GPU PNGs visually inspected: full head-to-shoe character framed,
  hands/legs/garments/hair visible, body/face remain attached in rest pose.
  Earlier ada-front.png showed RGB clothing masks as red/green/purple colours;
  final uniform author-colour fallback fixes those diagnostic mask colours.
- SOURCE.json records URL, hash and companion-export distinction. SDK/sample and
  npm package-lock/dependencies remain external on KINGSTON, none vendored.

A read attempt at sparse checkout UnrealFest.zip failed because sparse checkout
excluded it; no asset depended on that archive. Full source and final snapshots
are retained externally for reproducibility. No source code from the external
pipeline scripts was executed; only downloaded data and standard pinned decoder
libraries were used by our preparation script.

| Subsystem | Status | Remaining |
|---|---|---|
| Full Ada companion export in VIVID | implemented in source; real Metal captures passed | Broader asset/backend parity |
| Imported head bind translation | implemented in source; full rest-pose capture verified | Comprehensive moving full-rig tests |
| Original UE materials / RigLogic | partial / not implemented | Cloth mask-detail, exact eye/hair shader and facial solver parity |
| Full body animation / other backends | not tested | Actual clips, skinning/retarget reference tests |

Full engine tests, Studio UI screenshots, Vulkan/GLES and complete MetaHuman
animation parity NOT RUN. No CI/push/main checkout changes. Next step: bring
actual body clips and validated facial routing into this full asset, then check
pose/retarget and material fidelity independently. Do not conflate this successful
full-body rest-pose import with full binary body-DNA/RigLogic support.


## Ada companion textures and hair alpha — 2026-11-20

User: "И текстуры". Implementation `e1dc2a63fe65f8519505ff62a4a3163492b24f86`,
branch `integration/motionweave-studio-facial-stage184`, parent
`6aa5aff8037a5a6e231eb2c6aca9346bb56d20ee`.

New bake_materials.cjs + test_bake_materials.cjs under tools/vivid-dna-import;
prepare_full_character.cjs accepts optional source-map directory after material
JSON. Companion textures were obtained from the same published Ada directory:
https://creategamecharacters.github.io/metahuman-to-glb/characters/ada/textures/
Read the published assets/viewer.js as a material-channel reference, did not
execute it. Existing 16 embedded images were copied directly where applicable;
extra maps downloaded via curl with normal certificate checking. Python urllib
attempts failed with local CERTIFICATE_VERIFY_FAILED; no TLS verification bypass.
External sharp@0.34.5 added to the KINGSTON tooling/package-lock.

Offline conversion: cloth RGB region masks now blend authored colours, available
micro/macro detail and AO, with linear-to-sRGB output and a mip-like prefilter for
highly tiled micro detail. Hair/brow/lash source red coverage becomes alpha in
neutral RGB images. Eye R/B iris masks + UV pole/radial ramps become ordinary
colour textures; original atlas transform is removed to avoid double zoom. Skin
cavity and companion teeth colour/normal/mouth-occlusion are attached. Final
package has 29 images and 13 conversion/attachment provenance records. All 16
original image payloads verified byte-identical in output. Source-map SHA256s in
full-character/texture-source-hashes.json. No image/model asset vendored.

HumanMaterials.cpp high/compact hair graphs now use sampled input alpha via
setAlpha rather than overwriting coverage with procedural RGB's constant alpha.
This applies to existing colour and shadow graph evaluation; no renderer edits.
Creator adds --portrait for close inspection of the normalized full-body asset.
README and docs/vivid.md updated. No DNA/save/API/channel ordering, gameplay,
shared scheduler, renderer GPU ABI or default experimental settings changed.

Actually executed, all under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid:
- Native Creator + human appearance target build PASS: full-character/build-textures.log.
- Studio rebuild PASS: full-character/build-studio-textures.log.
- Unified run-tests.sh --no-build --build-dir .../native --filter
  '^vivid_human_appearance$' --ctest-jobs 1: 1/1 PASS, including material quality
  budgets. Log full-character/test-textures.log; report
  native/test-reports/20261120-152153-72698.
- node test_bake_materials.cjs <tooling> <KINGSTON tmp>: PASS,
  full-character/test-bake-final.log. Synthetic fixtures deliberately separate R
  coverage from source A; check correct alpha, authored cloth colour/sRGB values,
  colour roundtrip and path traversal rejection. Syntax checks and diff --check PASS.
- Final prepare-textured-filtered.log PASS. Final asset is
  full-character/ada-textured-filtered/manifest.json. Earlier ada-textured-final
  has unfiltered microdetail and is NOT the preferred delivery.
- Real Metal --smoke --still --asset ... with default and --portrait:
  **2/2 PASS**, head/model commands=47, shadow=44, colour=44, pipelineFailures=0.
  full-character/ada-textures-final-{full,portrait}.{log,ppm,png}.
  Both final PNGs viewed: whole figure and close skin/eyes/hair visible, cloth
  moire removed. Intermediate unfiltered captures also succeeded technically but
  showed an aliased large check pattern; prefilter was added and recaptured.

Limitations: offline baked cloth at finite resolution, nearest source sampling;
no view-dependent iris refraction, procedural sclera veins, tangent-atlas hair
anisotropy or full UE skin/scattering parity. Specular glints/card edges remain
visible and are not presented as final production fidelity. Full engine suite,
Vulkan/GLES, complete facial/body rig animation and Studio UI visual parity NOT
RUN. No CI/push/main mutation. Source worktree only intentional untracked web
symlinks remain after commits.

Next: validate original UE reference lighting/materials against native results,
then integrate actual full-body clips and RigLogic-compatible facial routing.
Texture wiring is verified in native Metal captures; photoreal/UE parity is partial.


## Soft hair/lash coverage correction — 2026-11-20

User reported missing alpha on hair/eyelashes after the texture checkpoint.
Implementation: `1f8b8b2852328864aafe8f1a453c3be5defa0be4`, branch
`integration/motionweave-studio-facial-stage184`, parent `f7e932bfb2860c2ad2a7e9f7a6d05a04733c9421`.
Affected: tools/vivid-dna-import/{bake_materials.cjs,prepare_full_character.cjs,
test_bake_materials.cjs,README.md}, docs/vivid.md.

Correction to prior acceptance: PNG coverage packing and successful render
submission did NOT establish correct soft edges. The previous baker forced
MASK at .35 on both hair and lashes, discarding fractional coverage. Re-read
external viewer-reference.js: original uses blended lashes and blended hair
fringe with a depth-writing masked core. Baker now emits BLEND lashes (coverage
texture preferred over albedo), BLEND hair/brows and cloned MASK .5 core
primitives sharing glTF accessors/textures. Invalid coverage channels reject.
Preview groom roughness .55 follows reference viewer floor and reduces pin glints.
No native renderer, DNA schema, API/save/channel ordering, gameplay or scheduler
changes. Additional primitives increase native imported vertices 125302 ->159396;
interchange sharing does NOT currently imply zero additional native GPU memory.

All assets/logs/captures remain external under
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/`.
Preferred asset supersedes ada-textured-filtered:
`ada-soft-alpha/manifest.json` (diagnostic-*.json are opaque controls, NOT delivery).
Actual verification:
- node --check baker and test: PASS; git diff --check: PASS.
- node tools/vivid-dna-import/test_bake_materials.cjs <external tooling> <KINGSTON tmp>:
  PASS, test-alpha.log. Fractional R=128 versus A=0 preserved, transparent zero,
  BLEND lash path, shared core accessors/textures + MASK .5, cloth conversion,
  unsafe path rejection. Synthetic test updated to exercise fractional coverage.
- prepare_full_character.cjs <tooling> ada-source.glb ada-soft-alpha 0
  mh_materials.json source-maps: PASS, prepare-soft-alpha.log.
- Existing native Creator --smoke --still --asset ada-soft-alpha/manifest.json,
  with/without --portrait: 2/2 PASS, commands49 shadow43 colour43,
  pipelineFailures0. ada-soft-alpha-{portrait,full}.{log,ppm,png}; both PNGs viewed.
- Separate all-hair/lash OPAQUE control and lash-only OPAQUE control: 2/2 PASS,
  ada-{alpha,lashes}-control-opaque.{log,ppm,png}. Both viewed. Fixed-camera PPM
  RGB comparison (>3/255 max-channel difference): all-opaque hair region
  [620,300,880,480) 24851 changed pixels; lash-only eye region
  [660,475,825,520) 335. Demonstrates coverage affects actual rendered pixels,
  NOT a UE visual parity metric. Opaque control exposes solid card strips.
- Native rebuild/CTest NOT RUN this stage: only offline JS/asset preparation
  changed, existing e1dc2a63 native binary used for actual Metal captures.
  Full suite, moving camera/animation sorting, Vulkan/GLES and Studio UI NOT RUN.
  No CI URL/run, push or main mutation.

Limits: primitive-sorted alpha blending is not order-independent transparency;
intersecting strand/pose sorting still needs multi-angle/animated validation.
Hair tangent-atlas anisotropy and original UE skin/eye lighting parity remain
unfinished; this is a soft-coverage correction, not a photorealism completion.
Next action: validate full-body clips and moving-camera/animated hair layers,
then refine original skin/eye and strand-lighting fidelity. Keep all builds and
artifacts on KINGSTON. Untracked web dist/node_modules symlinks left unchanged.


## Imported appearance profiles and matte correction — 2026-11-20

Implementation fdf3c8d22cede2ca3ff9a382c7f18fdcb081a76a on
`integration/motionweave-studio-facial-stage184`, parent d113d8a5d.
User requested matte clothes, adjustable skin/hair/eyes/textures/tones and body.
Root cause: shirt authored roughness .30; packed skin roughness can approach
zero; old skin/eye DNA controls were not bound to Ada materials.

HumanAppearance.h adds a separate version-1 bounded 72-scalar recipe with
explicit role mapping. HumanMaterials.cpp builds portable <=128-node graphs
over source colour/coverage with explicit roughness; no extra fixed human
highlight lobes. Skin tone/saturation/contrast/exposure/normal/cavity/procedural
freckles/pores; hair melanin/redness/dye/gray/root/ombre/highlights/coverage;
eyes radial iris/pupil/limbus/sclera/detail; cloth roughness/normal/colour;
10 Y-up skin-joint body proportions. Material application is transactional on
fresh models. Body deformation is post-animation and includes garment joints,
never canonical physics/skeleton data. Bounds are bind-pose; animated extremes
and clipping remain unverified. Hair uses sRGB-encoded root/seed in baked R/G
and coverage A, not raw data interpreted as albedo. Existing old companion
packages need regeneration to enable new hair colour controls correctly.

HumanImport manifest accepts optional relative appearance path (old v1 readers
reject this new key). VIVID DNA/52-channel order unchanged. Creator auto-loads
profile and supports --appearance override.json. No new ImGui UI. Next stage
is Studio UI with real native apply/save/load, not yet present in this commit.
Touched engine/character/vivid/{HumanAppearance.h,HumanMaterials.cpp,HumanImport.*},
tools/vivid-creator/main.cpp, tools/vivid-dna-import scripts/README, tests/vivid/
human_appearance.cpp and docs/vivid.md.

Executed on KINGSTON under AIVERSE-builds/motionweave-vivid/full-character:
- build-look-final.log: Creator, appearance/import tests build PASS.
- test-look-final.log: run-tests.sh --no-build --filter
  '^vivid_human_(appearance|import)$' --ctest-jobs 1, 2/2 PASS. Tests cover
  profile roundtrip/range rejection, graph budget/texture+alpha preservation,
  cloth roughness, changed skin roughness, identity body and skin-joint-only
  height change. Earlier compile attempts failed due to an overbroad text
  replacement and test API typos, corrected before final PASS.
- test-look-bake.log: node material bake tests PASS, including root/seed packing.
- prepare-look.log: ada-look-controls/manifest.json and appearance.json produced.
- ada-matte-portrait.{log,ppm,png}: real Metal --smoke --still --portrait PASS,
  commands49/shadow43/colour43/pipelineFailures0. Viewed: matte shirt and softer
  face lighting. Capture predates addition of neutral cloth tint controls only.
- git diff --check PASS. Full suite/CI/push/Vulkan/GLES/animated shape QA NOT RUN.

References read: Epic docs /metahuman/hair-overrides and /skin-material-tools;
external companion viewer-reference.js. This is an implemented portable subset,
not all MH controls: no original texture-index library, authored accent masks,
geometry hair grooming, corneal refraction or complete UE scattering parity.
Next: Studio native profile UI, final contrasting look/body frames and persistence
checks. All build outputs/assets/logs/screens remain on KINGSTON.


## Studio appearance panel and rigid-groom body follow — 2026-11-20

Implementation `994e7c7b41b24916121ceeaefe65f25ea4626cef`, branch
`integration/motionweave-studio-facial-stage184`, parent handoff `77c66ef8e`;
preceding native appearance implementation `fdf3c8d22cede2ca3ff9a382c7f18fdcb081a76a`.

Implemented a dockable Studio UI v1 VIVID Appearance panel (generated through
new_panel.py), 72 native-schema controls: skin19, hair20, lashes2, eyes14, cloth7,
body10. Draft undo/redo/reset, native apply, role assignments, reading a local
recipe, loading sibling appearance.json by checked model GUID, native named
save/load in project/human-looks. Explicit typed methods humanAppearanceInfo,
Apply, Save, Load, List, FromAsset; no arbitrary filesystem/shell bridge and no
browser project-file writes. Native errors/readiness shown separately from drafts.
Pure web validator rejects malformed/out-of-range profile input before rendering
number controls. The native validator remains authoritative.

StudioFacialPreviewBinding keeps a same-model MotionWeave rig while applying
appearance. A new appearance-only mode works without a fabricated facial rig.
StudioFacialGpuPreview applies materials before publishPrepared; fresh generation
ownership, read lease and GPU retirement preserved. Body field runs on the
post-animation presentation matrices. Still one Animator; no simulation, save-
state, GPU ABI, shared Renderer3D shader or batching default changes. CMake links
Studio/GPU test to existing vivid_human. A separate native session integration
fixture checks actual dispatch/load/apply/save/read/stale and symlink rejection.
Project/asset switch clears unrelated appearance. Profile schema/DNA unchanged.

Visual regression discovered and fixed: skin joints followed changed proportions,
but rigid hair/brows did not. applyHumanBody now also warps unskinned mesh-owner
matrices about each mesh's transformed centre (not its often foot-level origin).
Skinned mesh owner nodes remain untouched to avoid double deformation. Added a
rigid attachment numerical regression. Invalid body bounds are caught by both
native hosts instead of escaping the frame loop. Intermediate
ada-look-variant-full is BAD (bald/displaced groom); use fixed-full below.

Affected files: apps/studio-native/{CMakeLists.txt,StudioSession.cpp,
StudioFacialPreviewBinding.h,StudioHumanLookDocument.h,mac/StudioFacialGpuPreview.*,
tests/StudioFacialGpuTests.mm,tests/StudioFacialPreviewBindingTests.cpp,
tests/StudioHumanLookSessionTests.cpp}; apps/studio-web package, registry/types,
generated/HumanAppearancePanel TSX/CSS, humanLookSchema and tests; VIVID
HumanMaterials.cpp, appearance test, Creator main; docs/vivid.md and new
vivid-appearance-controls.md. See git show for exact full paths.

Verification, all builds/caches/assets/logs on KINGSTON at
`/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/`:
- Final native app/Creator/GPU target build PASS: full-character/
  build-look-publish-final.log. Previous build-studio-look-final.log also built
  the app and new session target; build-look-session-test.log final fixture.
- Unified run-tests.sh --no-build --build-dir .../native --filter
  '^studio_facial_(preview_binding|gpu)$' --ctest-jobs 1: 2/2 PASS,
  test-studio-look.log. Binding tests include named persistence, path rejection,
  stale controls, same-model rig preservation and appearance-only binding.
- '--filter ^(vivid_human_appearance|studio_facial_gpu)$': 2/2 PASS after rigid
  attachment fix, test-look-rigid-fix.log, report 20261120-154720-98436.
- '--filter ^studio_human_appearance_session$': 1/1 PASS,
  test-look-session-final.log, report 20261120-155139-9875. Initial fixture failed
  because empty gameplay rules had no types; corrected fixture, not production.
- Final '--filter ^studio_facial_gpu$': 1/1 PASS after frame error handling,
  test-look-publish-final.log, report 20261120-155407-14665. Real Metal shader
  submission tests use a small fixture, not a claim of full Ada Studio UI QA.
- npm run typecheck PASS; npm run test:web 75/75 PASS (including 2 profile import
  tests). web-look-checks.log records passing tests plus an initial failed
  `tsc -b --tsBuildInfoFile` attempt (TS5094). Corrected to `npx tsc --incremental
  --tsBuildInfoFile <KINGSTON>/web-look.tsbuildinfo` and `npx vite build`: PASS.
  Final web-look-final-build.log PASS; bundle copied into native app and verified
  present. Existing dist symlink targets KINGSTON. First npm attempt used repo
  root and failed ENOENT; corrected explicit apps/studio-web workdir. Dock tests
  initially exposed generator not adding the default right-stack entry, then
  old two-tab assumptions; registration and fixtures updated, all pass.
- Python UI contracts: 29/29 PASS, test-studio-contracts-look-final.log, using
  PYTHONPYCACHEPREFIX on KINGSTON. git diff --check PASS.
- Real Creator Metal --smoke --still --asset ada-look-controls/manifest.json:
  ada-matte-final-portrait.{log,ppm,png} and --appearance look-variation.json
  ada-look-variant-fixed-full.{log,ppm,png}, ada-look-variant-portrait.*.
  All PASS and viewed, pipelineFailures0; matte cloth/skin, colour changes,
  changed body and following groom observed. Pixel frames precede only the
  final error-catching wrappers, which do not change valid rendering.
- UI tool reports Mac locked. User explicitly replied 'Пока проверяй без
  интерфейса'. WKWebView desktop/240px interaction/screenshots NOT RUN; no claim
  of manual UI acceptance. verify_mac.sh not run literally because it hardcodes
  a repo-local build directory; relevant builds/tests performed at KINGSTON.
  Full engine suite, animation-wide body/clothing/culling checks and Vulkan/GLES
  NOT RUN. No CI/push/main mutation.

Ready external sample: KINGSTON root `VIVID-Look-Lab/content/Ada` with source
companion, `human-looks/Ada-Matte.json`, `Ada-Variation.json`, and launcher
`Open-VIVID-Look-Lab.command`. Project was prepared, not opened interactively.
Choose Panels > VIVID Appearance, select Ada/model.gltf, Load model profile,
Apply to preview. The profile can also be passed to Creator --appearance.
Source assets remain external and are not committed or published.

Limitations/next: perform unlocked Studio interaction and 240px layout check;
validate multiple poses/angles and extreme garment deformations. Body warp is a
bounded Y-up presentation approximation, not anatomical morphs/physics. Hair
colour controls require packed root/seed attributes and do not regenerate hair
geometry. No MetaHuman face texture-index library/region masks, tangent-atlas
anisotropy or full cornea/SSS parity. Control reference documents this explicitly.
Only pre-existing dist/node_modules symlinks should remain untracked after commit.

## Wardrobe prerequisite: preserve facial export — 2026-11-20

Implementation `4a7f0a3da4c54c210ac75450413ed6f165a54b2a`, branch `integration/motionweave-studio-facial-stage184`.
Updated tools/modeling/Export.cpp and tests/modeling/modeling.cpp: GLB now exports
position/normal morph deltas, semantic targetNames, mesh/node weights and SCALAR
weight-animation channels. Previously morphs were silently lost and weight clips
were serialized as rotation. CPU deltas must exist; missing data fails clearly.
No save schema/runtime/renderer change. Wardrobe, body masks, baking and Studio
assembly button are still outstanding; this is a prerequisite, not completed UI.
Verified cmake build target aiverse_modeling_tests and run-tests.sh --no-build
--filter '^modeling_authoring$' --ctest-jobs 1: 1/1 PASS including skinned morph
roundtrip. All builds/logs on KINGSTON: motionweave-vivid/full-character/
{build,test}-wardrobe-export.log; report native/test-reports/20261120-155942-27803.
git diff --check PASS. Full regression/UI/other platforms NOT RUN. No CI/push.
Next: native wardrobe recipe, masking and bake pipeline, then typed Studio UI.

## Wardrobe CPU assembly and bake — 2026-11-20

Code `3bad4c000730c7bbc51aedbe2664462c6132a639`, branch integration/motionweave-studio-facial-stage184.
HumanWardrobe v1 composes imported CPU models: selectable source meshes, one
enabled item per slot, rigid bone sockets/TRS, skinned clothes with exact bone-name
and bind-world checks. Per-item UV0 red-channel visibility masks multiply base
alpha; black hides. Masks remain texture cutouts (no unsafe triangle deletion).
Optional cloth finish uses existing appearance controls; source finish preserves
jewelry PBR. Body deformation is baked in bind-space vertices/morph vectors with
translated joint pivots/inverse binds and offset translation animation keys. It is
an approximation for upright characters, not pose-wide anatomical retargeting.
HumanAppearanceBake executes the same private DAG, bakes linear colour/alpha to
sRGB textures (16..4096 max edge), stores roughness/normal/AO factors, removes live
graphs. CPU/GPU procedural-noise floating-point differences possible. Export also
preserves COLOR_0 and base KHR_texture_transform. Final package removes unused
meshes/materials/images, keeps separate skin/material primitives in one asset.
No automatic clothing fitting or physics; external skinned clothes must match rig.
No UI/button or filesystem publication yet. Next stage: safe native async build,
Studio garment/mask controls, actual Ada package roundtrip and native captures.

Verification: KINGSTON full-character/build-wardrobe-core-final.log and
test-wardrobe-core.log: modeling_authoring + vivid_human_wardrobe +
vivid_human_appearance 3/3 PASS (report 20261120-160550-42160). Final core changes
(baseline body bounds, idempotent hides, optional cloth finish) rebuilt and
vivid_human_wardrobe 1/1 PASS, build/test-wardrobe-core-last.log, report
20261120-160628-42693. New fixture covers mask alpha, socket attachment, hidden
mesh removal, facial/skinning export/reimport, duplicate IDs/missing socket,
cloth roughness/alpha. Initial make invocation could not select newly generated
target in same command after CMake regeneration; next invocation passed.
Full suite/multi-pose body and actual Ada/UI NOT RUN yet. No CI/push.

## Studio wardrobe and safe character publication — 2026-11-20

Implementation `9ade6c85d3f5411bf8bf1fc1f04977598118dda6`, branch `integration/motionweave-studio-facial-stage184`;
prerequisites 4a7f0a3da (morph export) and 3bad4c000 (CPU wardrobe/bake).

Added Studio UI v1 wardrobe controls within VIVID Appearance: real source mesh
visibility, model GUID pickers, slots, enabled items, cloth/source PBR finish,
bone sockets and local TRS, per-item source mesh hiding and body mask texture
GUIDs/material names. Draft undo, native sibling wardrobe.json preset loading,
resolution/name controls, Russian Собрать персонажа button, async status/error,
and loading the completed GLB through existing checked preview binding. There
is NO live per-item wardrobe preview; display the assembled output. No JS project
file writes or new ImGui controls. StudioField labels get actual input IDs.

Native typed methods: humanWardrobeInfo, humanWardrobeFromAsset,
humanCharacterBuild, humanCharacterBuildStatus. A worker captures validated
source/model/mask selections; native FileReader restricts reads to nonsymlink
content paths. Output is content/Characters/name/{model.glb,source.json,
appearance.json}. Exclusive staging, fsync and macOS RENAME_EXCL publish avoid
source/previous-output replacement. Reimport and mesh/skin/morph-name checks
precede publication. The live output appearance is empty to avoid double baking;
source.json retains the editable request. 128 MiB output/import cap; the standalone
Creator cap increased from 64 to 128 MiB to match Studio. aiverse_vivid_assemble
uses the SAME publisher for headless acceptance (content paths instead of GUIDs).
Wardrobe presets normalize optional TRS/finish defaults before returning to TS.

Important body regression found by actual Ada screenshots, despite unit tests:
packed UNORM skin weights sum to 254/256 on some vertices; GPU normalizes by
actual sum. Dividing by 255 during bake produced up to .01556 model-unit error
and severe face/hand artifacts. Fixed by normalized weighted bind/warped matrices,
inverting each vertex's weighted bind, and matching GPU linear skin normals.
Source vertices are snapshotted and only referenced primitive vertices are baked,
preventing repeated/shared-range deformation. Public bakeHumanBody plus tests
cover shared primitives and non-255 weight sums. Body translation keys/pivots
are adjusted; pose-wide anatomical retargeting remains unverified.

Baked Creator manifests optionally support `authoredHair:true` (v1 additive,
false by default), suppressing an extra generated wig WITHOUT replacing baked
PBR. Strict boolean parsing and roundtrip test added. Earlier captures with empty
material roles generated a second wig. Native Studio raw GLB never used that
Creator-only generation path. No core renderer/Metal batching changes.

Affected paths: apps/studio-native CMake/Session/header/StudioHumanCharacterBuild
and session tests; studio-web types and HumanWardrobeControls/appearance panel;
HumanImport, HumanWardrobe, their tests; tools/vivid-creator main/assemble and
wardrobe_fixture.py; docs/vivid.md and vivid-character-assembly.md. No external
MetaHuman/companion data is committed. Procedural fixture accessories are original.

Verification: ALL builds, assets, logs, packages and captures on KINGSTON under
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/. Logs below are full-character/.
- cmake --build native targets aiverse_studio, aiverse_vivid_creator,
  aiverse_studio_human_appearance_session_tests, aiverse_vivid_human_wardrobe_tests,
  aiverse_vivid_human_import_tests, aiverse_vivid_assemble: PASS across scoped builds;
  final build-wardrobe-complete.log. Final app contains built web dist.
- Unified run-tests.sh --no-build --build-dir <KINGSTON>/native --ctest-jobs 1
  filter '^(studio_human_appearance_session|vivid_human_import|vivid_human_wardrobe|
  vivid_human_appearance|modeling_authoring)$': 5/5 PASS, test-wardrobe-final.log,
  report 20261120-161832-58357 (before final body normalization).
- After body/preset fixes, filter '^(studio_human_appearance_session|
  vivid_human_wardrobe|vivid_human_import)$': 3/3 PASS, test-wardrobe-complete.log,
  report 20261120-163045-80190. Earlier shared-vertex and weight regressions also
  PASS in test-body-shared-regression.log and test-body-weight-normalization.log.
  Session tests exercise actual async dispatch, imported defaults/GUID resolution,
  output/recipe creation, duplicate-name failure without partial output, bad GUID.
- npm run typecheck PASS; npm run test:web 75/75 PASS; npx tsc --incremental
  --tsBuildInfoFile <KINGSTON>/web-wardrobe.tsbuildinfo && npx vite build PASS
  (tool output, bundle index-BHaoxO61.js). Initial typecheck caught missing
  StudioField htmlFor and 'texture' instead of AssetKind 'image'; corrected.
- PYTHONPYCACHEPREFIX=<KINGSTON>/python-cache python3 -m unittest discover
  -s tools/studio -p 'test_*.py' -v: 29/29 PASS, test-wardrobe-ui-contracts.log.
- Actual Ada builds: Characters/Ada-Wardrobe-Ready/model.glb (160971 vertices,
  9 meshes,22 materials,89316788 bytes); Characters/Ada-Baked-Body-Normalized/
  model.glb (159396 vertices,7 meshes,17 materials,88986864 bytes). Companion
  preset includes separate BlueShirt, SilverWatchBand/dial, GoldChain and
  BodyVisibility.png. Imported garment is a same-rig shirt variant, not proof
  of automatic fitting. Bodies/masks/accessories are all in the final GLB.
- --compare-body PROJECT Ada/model.gltf Characters/Ada-Baked-Body-Normalized/
  model.glb look-variation.json: PASS, compare-ada-body-normalized.log;
  225900 samples, maxPositionError=2.38814e-06, RMS=4.21594e-07,
  minNormalDot=.999165 against live applyHumanBody with GPU-normalized weights.
- Real Metal Creator --smoke --still captures inspected: ada-wardrobe-ready.png
  (shadow48/colour48, pipelineFailures0) and ada-baked-body-normalized.png
  (shadow43/colour43, pipelineFailures0). These are Creator framebuffer captures
  of assembled GLBs, NOT screenshots of the new Studio controls.
- BAD intermediates: Ada-Baked-Variation, Ada-Baked-Body-Fixed (marked DO-NOT-USE),
  ada-baked-variation*.png and ada-baked-body-fixed.png retain dents from the
  pre-normalization implementation. Do not deliver as passing. Initial 93 MiB
  Creator load failed at its old 64 MiB limit. A diagnostic AIVERSE_SHADOWS=off
  did not override Creator's explicit Medium setting (shadow43): NOT a valid
  no-shadow comparison. Temporary override code was removed. Final normal
  shadows are enabled; no rendering defect was hidden by disabling shadows.
- git diff --check PASS. No CI/push/main mutation. Full engine/Vulkan/GLES,
  manual WKWebView 1440/240px and multi-pose wardrobe acceptance NOT RUN. User
  explicitly authorized checking without UI while Mac is locked; no repeated
  unlock/approval request. verify_mac.sh not run verbatim because it hardcodes
  repo-local build output; equivalent scoped builds use KINGSTON.

Ready launch: KINGSTON Open-VIVID-Look-Lab.command. In VIVID Appearance select
Ada/model.gltf, Load model profile, Загрузить гардероб модели, choose a NEW name,
Собрать персонажа, then Показать собранного персонажа. START-HERE.txt updated.
Only existing untracked dist/node_modules symlinks should remain in worktree.
Next: unlocked Studio interaction/narrow dock checks, animation/morph-pose
wardrobe and body tests; live garment preview, LOD/atlas optimization and
automatic fitting are separate unfinished features. Preserve all source assets.

## Face identity deformation core — 2026-11-20

Code `be6bfcb2e5a837d073c056cb13416d1c07cac782`, branch integration/motionweave-studio-facial-stage184.
HumanFaceShape defines strict v1 selected-mesh identity recipe, 18 bounded
region controls and <=256 normalized XYZ landmark displacements. CPU candidate
geometry uses GPU-normalized bind skin weights; UVs/topology/skin ownership stay
unchanged. Morph direction vectors follow the deformation Jacobian. It changes
identity shape, not the fixed 52 expression semantics or authoritative ECS.
A coarse aligned-scan nearest-vertex correspondence helper produces <=128
landmarks; it is NOT automatic registration, dense surface reconstruction or
MetaHuman RigLogic. Front-photo XY landmark displacement preserves template
depth. No photographic texture extraction, landmark detector or UI implemented
yet. Face bones are not anatomically refitted. Models must be Y-up, +Z forward.

User explicitly expanded request to include scan/photo creation, not only
separate head/body import. Clarification answered 'Также создание по скану/фото'.
Explained planned template fitting and preservation of unseen photo geometry.
Official references researched: Epic metahuman-creator-from-dna-tool-in-unreal-engine,
metahuman-creator-import-tools-in-unreal-engine, head-controls. These describe
separate DNA/template/Identity flows; do not claim full Unreal feature parity.
Current DNA adapter still legacy head diagnostic only; its limitations remain.

Verification: cmake builds aiverse_vivid_human and aiverse_vivid_face_shape_tests
PASS. Unified run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
'^vivid_face_shape$' --ctest-jobs 1 PASS. Logs full-character/build-face-shape-
{core,test}.log and test-face-shape-core.log on KINGSTON. Fixture verifies
recipe validation, neutral identity, width, unchanged topology, photo depth,
scan correspondences. No UI/actual Ada visual/animated-pose acceptance yet.
No CI/push. Next: Studio native preview/build persistence, photo landmark UI,
scan fitting, and separate head/body import with rig-safe attachment.


## 2026-11-20 — Studio facial identity authoring, scan/photo reference and parts import

Code: `ff1f660e60742cfc67c7a724c73e892daf0a1e27`, branch
`integration/motionweave-studio-facial-stage184`; preceding core `be6bfcb2e`.
Affected: Studio native binding/session/GPU/build helper, Studio shared-UI
HumanFaceControls and appearance/wardrobe integration, HumanWardrobe independent
rig merge, assembly CLI, native fixtures, docs/vivid-face-authoring.md.

Implemented 18 facial controls, manual local reference-photo points (front XY /
profile ZY), aligned-scan coarse nearest-vertex fit, independent head/body model
merge preserving separate rigs/clips/morphs/materials. Socket inverse TRS chain
retains imported world bind frame while following body. Face applies to fresh
native preview CPU geometry and final assembly before body deformation; source.json
persists recipe. Read-only file import restores recipe against original source.
Native methods humanFaceFitScan and humanHeadBodyImport share existing single
async build status. Optional face field is additive, old appearance/wardrobe callers
remain valid; wardrobe independentRig defaults false. No renderer core changes,
no automatic binary DNA compatibility expansion, no browser project writes.

All outputs on `/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid`.
Executed verification:
- CMake targets aiverse_studio_human_appearance_session_tests,
  aiverse_vivid_human_wardrobe_tests, aiverse_vivid_assemble, aiverse_studio,
  aiverse_studio_facial_gpu_tests, aiverse_studio_facial_preview_binding_tests PASS.
- Initial make target AiverseStudioHTML5 failed (bundle name, not target); reran
  correct aiverse_studio. Initial binding link failed missing vivid_human; fixed
  target dependency. Initial wardrobe test fixture declared morph name without
  channels; fixed fixture. Logs build-face-integration.log/build-face-studio.log
  retain these failures, later build-face-final-tests.log PASS.
- run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
  '^(vivid_face_shape|vivid_human_wardrobe|studio_human_appearance_session|studio_facial_preview_binding|studio_facial_gpu)$'
  --ctest-jobs 1 PASS 5/5; report 20261120-164640-3967.
  Additional translated/nonuniform-scaled socket fixture PASS, report
  20261120-164734-4623. Session tests execute real async face bake/scan/parts import.
- npm run typecheck, npm run test:web (75 tests), npx tsc --incremental
  --tsBuildInfoFile <KINGSTON>/web-face.tsbuildinfo, npx vite build PASS.
  After final recipe restoration UI, typecheck + vite repeated PASS.
  One initial npm invocation at repo root failed ENOENT; reran in studio-web.
- PYTHONPYCACHEPREFIX=<KINGSTON>/python-cache python3 -m unittest discover
  -s tools/studio -p 'test_*.py' -v PASS 29/29.
- Actual Ada build using optional assembly CLI FACE_JSON: Ada-Face-Edited,
  159396 vertices, 7 meshes, 17 materials, 95831620 bytes, self-contained GLB.
  Recipe full-character/face-ada-test.json (jawWidth .12, cheekWidth .07,
  chinForward .03, noseWidth -.08). Native Creator --smoke --still --portrait
  capture PASS, Metal pipelineFailures=0, pass=1. Inspected
  full-character/ada-face-edited.png and control ada-face-baseline.png.
  Eye/eyelash edge artifacts exist in BOTH old undeformed wardrobe and new face
  preview; NOT clean portrait visual acceptance. No shadows disabled to hide them.
- Latest web dist copied into KINGSTON AiverseStudioHTML5.app Resources/studio-web.
  git diff --check PASS. No CI/push. User authorized tests without locked Mac UI;
  manual WKWebView/narrow panel interaction NOT RUN. Creator image is NOT a Studio
  panel screenshot. Full animated-pose/foreign head asset acceptance NOT RUN.

Limits: photo tool is MANUAL template guidance, not automatic reconstruction,
photogrammetry, detected landmarks or texture extraction. Scan requires prior
alignment/units; coarse nearest vertex, not ICP. No anatomical bone refit, neck
weld, automatic retarget or independent-clip synchronization. Existing MetaHuman
binary DNA importer remains legacy head diagnostic, not full modern body/RigLogic.
No user-specific scan/photo supplied. New tools implement a bounded stage of the
broader request, not full Mesh to MetaHuman parity. Source recipes must NOT be
applied to already baked assets. Photo reference itself is not persisted.
Next: fix existing close-up eye/eyelash rendering; unlocked panel QA; validate
real independent head/body animated assets; automatic scan registration and
photo reconstruction remain separate unfinished work. Keep original source data.


## 2026-11-20 — Eight studio lighting rigs and curved cyclorama

Code commit `77e23959c1dcf7157f635c5d9b432c8ed65d7bd6`, branch
`integration/motionweave-studio-facial-stage184`. User requested 8 Unreal-style
lighting presets, multiple sources, curved backdrop. Shared presentation header
`engine/character/vivid/HumanStudioLighting.h` defines eight original rigs:
neutral, portrait, warm, cool, dramatic, rim, gel, catalog. Four sources each:
shadowed directional key plus three High-tier point sources, hemisphere ambient.
64-segment smooth quarter-circle joins floor and wall (134 vertices/396 indices).
Cyclorama and source layout scale/translate to character world bind bounds.
No engine renderer/shader/batching policy changes or experimental flags enabled.

Studio native facial binding owns preview-only settings; validated typed
`studio.humanStudioLighting` gets catalog/settings and optionally sets the whole
settings object atomically. Invalid presets/ranges/types reject without publication.
Light changes do not increment binding revision or reload character GPU assets.
Native Metal Scene owns/releases one immutable unit cyclorama mesh. Facial mode
uses Medium sun shadows and High local lights; map/material modes retain previous
Off shadows/Medium local tier. No map light document or baked texture mutation.
Shared Studio UI component exposes 8 choices, rig yaw [-180,180], gain [.25,2],
backdrop toggle and reset. Settings remain until project/binding reset, not on disk.
Creator CLI `--studio-lighting ID` uses the SAME rigs/geometry for captures;
without option its legacy optical lighting/floor is retained. No new ImGui controls.

Verification actually executed, all outputs on KINGSTON:
- CMake targets aiverse_studio, aiverse_vivid_creator,
  aiverse_studio_human_appearance_session_tests PASS. Initial aggregate braces
  error in new header failed compile; fixed. Original build-studio-lighting.log
  retains failure; build-studio-lighting-retry.log final success.
- run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
  '^studio_human_appearance_session$' --ctest-jobs 1 PASS. Report
  native/test-reports/20261120-165318-10617; test-studio-lighting.log.
  Checks 8 catalog entries, selection, invalid selection atomicity, cyclorama
  vertex/index counts and every triangle winding against smooth normals.
- npm run typecheck, npm run test:web (75), npx vite build PASS.
  dist is KINGSTON symlink; latest dist copied into native app Resources/studio-web.
- PYTHONPYCACHEPREFIX=<KINGSTON>/python-cache python3 -m unittest discover
  -s tools/studio -p 'test_*.py' -v PASS29; test-lighting-ui-contracts.log.
- Actual native Creator --smoke --still --studio-lighting <ID> with the SAME
  external Ada-Face-Edited asset/camera/pose for all8: PASS8/8, each log has
  pipelineFailures=0 and pass=1. All eight PNGs visually inspected: visible
  distinct light tint/contrast, curved continuous ground/background and sun shadow.
  full-character/light-{neutral,portrait,warm,cool,dramatic,rim,gel,catalog}.{png,log,ppm}
  under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid.
  full-character/lighting-gallery.html links all actual PNGs; no generated/fake
  character images. Left overlay is legacy Creator, NOT new Studio control UI.
- git diff --check PASS. No CI/push. Manual WKWebView/narrow dock screenshots
  NOT RUN: user explicitly asked to keep testing without locked desktop UI.
  Vulkan/GLES and dynamic eight-preset switching visual acceptance NOT RUN.

Limits: original AIVERSE rigs, not copied Epic assets or UE render parity.
Current point lights have no individual shadow maps; no area lights/Lumen/ray
tracing added. Existing eye/eyelash and occasional shadow-edge artifacts remain;
lighting stage does not repair imported material anatomy. Cyclorama is oriented
behind +Z-facing character and fixed in world; rig yaw rotates sources only.
No scene export or persistent project light-preset file. Next: unlocked Studio
controls QA, orbit/rotation acceptance and existing close-up eye material fix.
Build/launch remains KINGSTON/Open-VIVID-Look-Lab.command. No source assets staged.


## 2026-11-20 — Visual QA: hair and eyelashes FAIL

Report commit `23df5d50b490d6926d5a0e0228827c12d17c475c` on
`integration/motionweave-studio-facial-stage184`; no product/source asset changes.
Read docs/qa/vivid-hair-lashes-2026-11-20.md before further appearance work.
Actual existing Creator binary hash/version and asset hash captured in
KINGSTON/full-character/hair-lashes-qa-20261120/environment.json.
Three new actual Metal portraits: catalog, neutral, rim; all renderer logs
pipelineFailures=0/pass=1, but eyelashes visually FAIL3/3 (dark jagged bands),
hair silhouette FAIL especially against light background (hard card fragments).
Critical new evidence: source Ada/baseColor_8.png AND assembled GLB eyelash
baseColor texture each contain1048576 alpha255 pixels, zero transparent/partial.
BLEND mode survives, but meaningful lash coverage is ALREADY LOST IN SOURCE
companion texture. Do not claim alpha correctness from material mode alone.
Hair alpha DOES survive:676354 alpha0,306724 partial,65498 alpha255 (1024²)
for both BLEND fringe and MASK core. Hair defects therefore need separate
coverage/filter/sorting investigation; not established as alpha loss.
Eye shell/edge grey artifacts also seen; origin not isolated.

Executed:3 CLI captures + sips PNG conversion + read-only GLB/PNG alpha analysis
using standard-library PNG unfilter/zlib. No build/tests/CI/push. Pillow missing
in default Python3.8 and3.12; no dependencies installed. Report2 PASS technical
checks,3 FAIL,3 NOT RUN. Animation/blink/orbit/Studio UI NOT RUN (user authorized
without locked UI). No retouching, UI simulation or fabricated screenshots.
Evidence/report under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/
full-character/hair-lashes-qa-20261120/. Next: investigate companion eyelash
opacity extraction first; restore texture coverage, then isolate eyeshell/edge
and hair core/fringe quality with matched A/B and actual blink/orbit validation.


## 2026-11-20 — Default facial idle and four preview poses

Code `8c71909c5f9e5f69b3b92fae4ad3dddbf7a6458c`, branch
`integration/motionweave-studio-facial-stage184`. User requested default facial
idle and poses applied. HumanPreviewAnimation.h generates preview-only six-second
clips: relaxed(default), A-pose, T-pose, portrait × face on/off. Semantic upperarm/
lowerarm targeting converts world direction into parent bind rotation, updating
matching garment/body skeletons; numeric suffixes/namespaces are recognized.
Small neck/spine motions, blink peaks1.7/4.4s, subtle smile/brow morphs. Uses current
native64 morph capacity, not outdated8-channel restrictions in older AGENTS text.
No import/source edits, rig schema/channel reordering or renderer shader changes.
One existing Studio Animator remains owner; default play/speed1/face enabled,
0.25s pose fade, immediate change while paused. Existing manual facial layer runs
afterwards. Source clips retained but default preview selects generated clips.
Native validated studio.humanPreviewAnimation settings are session-only; no export
or baked animation mutation. Shared Studio UI exposes pose, face toggle, speed,
pause and reset. See docs/vivid-preview-animation.md.

Creator opt-in --preview-pose uses its existing MotionWeave graph. Diagnostic
--preview-time samples a separate pose copy without modifying canonical ECS pose.
IMPORTANT: initial screenshots face-open.png/face-blink.png DID NOT close eyes:
Creator's sparse GPU actor override suppresses the imported Model morph texture.
Fixed preview-pose mode to use ModelDraw.pose through the same standard morph
renderer path as Studio, without actor vertex override. Legacy actor modes remain.
Final capture logs identify actor=MODEL_MORPH. Do NOT cite the early snapshots as
blink acceptance. No second animation scheduler added.

Also corrected prior Studio light/cyclorama positioning to match character preview
normalization (max bind extent→2), rather than raw asset units. No viewport layering
or camera/UI geometry changes. Runtime source model/wardrobe assets unchanged.

Actually executed, outputs under /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid:
- CMake aiverse_studio, aiverse_vivid_creator,
  aiverse_studio_human_appearance_session_tests, aiverse_studio_facial_gpu_tests PASS.
  build-idle-poses.log, build-idle-checks.log, build-idle-capture-fix.log.
  One Python edit invoked from web directory failed FileNotFoundError before any
  write; rerun at explicit worktree root succeeded. No failed native compile.
- run-tests.sh --no-build --build-dir <KINGSTON>/native --filter
  '^(studio_human_appearance_session|studio_facial_gpu)$' --ctest-jobs 1 PASS2/2;
  native/test-reports/20261120-170617-19019, full-character/test-idle-poses.log.
  Fixture tests default enabled state, invalid settings atomicity, paired blink,
  relaxed/T target directions, source immutability, loop endpoint and face-off.
  Existing real Metal GPU fixture passes with default preview clip integration.
- npm run typecheck && npm run test:web (75) && npx vite build PASS. After final
  clothing-intersection hint, typecheck/vite repeated PASS; latest dist copied
  into native app Resources/studio-web on KINGSTON.
- Python UI contracts using KINGSTON PYTHONPYCACHEPREFIX PASS29/29,
  full-character/test-idle-ui.log. git diff --check PASS.
- Actual Creator fixed-time native renders visually inspected:
  full-character/idle-poses/{relaxed-native,a_pose-native,t_pose-native,
  face-open-native,face-blink-native}.png. All five pipelineFailures=0,
  pass=1, actor=MODEL_MORPH. Open t0 vs blink t1.7 clearly shows closed eyelids.
  Default relaxed/A-pose garment placement acceptable in these static front views.
  T-pose exposes shoulder skin through shirt: NOT clean wardrobe visual acceptance.
  Portrait inherited opaque lashes/eye-edge artifacts remain. Gallery.html links
  all final snapshots (lowercase filename gallery.html). These are native Creator
  scene snapshots, NOT screenshots of Studio UI or a real-time video.
- UI interaction, continuous playback pacing, other models/rig conventions,
  cross-platform GPU, full arbitrary animation/clothing collision NOT RUN.
  User still authorizes testing without locked desktop UI. No CI/push/main change.

Known limits: semantic matching is not full retarget/foot IK/anatomical correctives.
Unknown bones/morphs are skipped; original assets have no supplied animation clips.
Default clips are procedural presentation helpers, not licensed mocap assets.
Imported source eye alpha/hair problems from QA remain (do not claim fixed).
Next: fix companion eyelash alpha, shoulder mask/corrective deformation for T-pose,
then unlocked panel switching/pause/idle and continuous blink/orbit validation.


## 2026-11-20 — Unreal preview reference and animation source discovery

Investigation commit `e3f59a8d064b1cd33089535071e9fd6ff3899cfc`, branch
`integration/motionweave-studio-facial-stage184`. See
`docs/qa/vivid-unreal-animation-source-2026-11-20.md`. User asks for Unreal-like
preview. Local MetaHumanSample 5.7 contains MM_Idle and f/m sample AnimSequence
packages, but no portable animation exports. Standard Mac editor executable
paths are absent; launcher UE_5.7 directory contains plugin remnants. Custom
unindexed engine locations remain possible. Official Creator documentation
confirms separate head/body animation assignment. No asset export/import, code,
public API, save format, runtime behavior or screenshot changes in this stage.
Executed read-only filesystem/launcher discovery and official documentation
lookup. Builds/tests NOT RUN (investigation only); CI NOT RUN; no push.
Next: locate usable Unreal installation or skeletal-animation export, then
validate retargeting with garment rigs and independent face/body clips. Keep
all project copies/builds/caches/captures on KINGSTON. Current procedural idle
is not MetaHuman-quality parity; eyelash alpha and T-pose shirt defects remain.


## 2026-11-20 — Idle phase, motion recordings and actual eyelash alpha fix

Implementation `cdf6db13da6e401ec3bbbdc1402192f4c94e6ca7`, branch integration/motionweave-studio-facial-stage184.
Paths: HumanPreviewAnimation.h, render/Animation.h/.cpp, StudioFacialGpuPreview,
StudioHumanLookSessionTests, vivid-creator, vivid-dna-import baker/test/docs;
Studio stage90 and docs/qa/vivid-motion-review-2026-11-20.md contain evidence.
Three-spine breathing, gentle resting arms, named conjugate gaze and smooth
asymmetric blink envelope. Existing four poses; 0.65s transitions preserve phase.
New optional Animator::Play.startTime defaults0, no existing caller behavior or
save/bridge format change. IMPORTANT first attempt used seek after play, which
clears the fade; test caught it. Final uses initial time during play, retaining
outgoing track. No new production scheduler; Creator offline sequence pauses
MotionWeave and samples a diagnostic pose via the shared Animator helper.
--preview-sequence N writes 24fps fixed-step frames (1..720), path is directory;
requires --smoke --preview-pose, no --preview-time. Not FPS/UI parity evidence.

Actual source eyelash PNG has RGB entirely255 and meaningful alpha:935962 zero,
76 full, remainder fractional. Baker now infers A only for white-RGB meaningful-A
lash exports; explicit metadata wins; hair retains R. Node fixture covers both.
New external Ada-Motion-Review GLB has repaired lashes, original face-edited GLB
unchanged. Working content/Ada/baseColor_8.png repaired with original backed up
under full-character/motion-review/source-texture-backup. Other baked packages
still need rebake. No third-party assets added to Git.

All builds/artifacts on /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid.
CMake aiverse_studio, aiverse_vivid_creator, aiverse_studio_human_appearance_session_tests,
aiverse_studio_facial_gpu_tests PASS (build-motion-phase-fix.log).
First tests FAIL1/2 due seek cancelling fade (test-motion-review.log), corrected
run PASS2/2 (test-motion-phase-fix.log), report native/test-reports/20261120-174042-42412.
Node test_bake_materials.cjs PASS; git diff --check PASS. Web NOT RUN (no frontend
changes); Studio manual UI, other platforms, real-time pacing/complete orbit NOT RUN.
No CI/push. Existing compiler warnings. First body/ sequence is pre-fix, not final.

Final native Metal body-final/:360 distinct frames→body-poses.mp4 15s, face/:168
frames→face-idle.mp4 7s, both1500x920@24fps. AVFoundation encoding successful.
full-character/motion-review/gallery.html includes videos and inspected native
PNGs. Source frames at open/two blinks, relaxed endpoints/intermediate/A/T inspected.
No pipelineFailures, actor=MODEL_MORPH. Hair remains attached in inspected views
but coarse card silhouette FAIL; lashes now transparent and blink with eyelids,
fine edges still imperfect; gray eye artifacts FAIL; T shoulder skin poke-through
FAIL, relaxed/A samples acceptable. Not full MetaHuman Creator quality parity.

Next: author genuine shirt visibility masks/correctives for shoulder skin, isolate
iris/eye-shell shading and hair coverage/filtering; then orbit/continuous UI checks.
Do not hide entire body or mark existing synthetic BodyVisibility test patch as
artist-approved clothing coverage. Root/legs stay bind: no weight-transfer/foot IK,
cloth collision or physically simulated hair was added. Preserve source backups.


## 2026-11-20 — Named sockets and independent SKM attachments

Code `9e9f43bb32c46314046548c8afc7c5b870c4d740`, integration/motionweave-studio-facial-stage184.
HumanWardrobe.cpp/.h adds optional sockets and item.socket/attachmentRule to
version1 recipes. Existing recipes retain semantics; old strict readers reject
new fields. Rules keep_world and snap require independentRig, preserving imported
child joints/skins/morphs/clips rather than name-remapping. Shared-skeleton clothing
still validates matching bind frames. Sockets are ordinary socket/<name> nodes
compatible with generic Model/View traversal and GLB export, not a new scheduler.
Keep-world exact inverse TRS chain follows rotated/nonuniform-scaled parents;
explicit offset is socket-local, legacy offset order unchanged. Duplicate/missing/
ambiguous parents and invalid transforms reject. Multiple attachments on a bone
use distinct arbitrary slot names. Limits128 sockets/32 assets remain explicit.

HumanWardrobeControls/types expose socket name/bone/local TRS editing, separate
skeleton mode, rules, slot input and draft undo via shared Studio UI. StudioSession
normalizes omitted socket TRS on loading. Existing native async assemble/show
flow publishes changes, not live gizmos. No new bridge allowlist or browser writes.
Docs: vivid-skm-attachments.md, Studio stage91; vivid.md and motion QA explicitly
WITHDRAW the previous hair-attachment conclusion after user reported hair detached.
This stage does NOT claim Ada hair is repaired; no new character assets changed.

Actual checks: all builds/caches/output KINGSTON. Native aiverse_studio,
aiverse_vivid_human_wardrobe_tests, aiverse_studio_human_appearance_session_tests
PASS (build-skm-final.log). CTest2/2 PASS, final report
native/test-reports/20261120-175028-57514, test-skm-final.log; earlier scoped run
also2/2. Added real snap/local-offset/keep-world, parent rotation+scale, independent
skin and exported GLB socket roundtrip, duplicate/missing socket rejection checks.
npm typecheck + existing75 web tests + incremental tsc(KINGSTON cache) + vite PASS;
final typecheck/vite repeated after selector adjustment; dist copied into KINGSTON
native app. Python UI29 PASS(test-skm-ui.log); diff --check PASS.
verify_mac.sh NOT RUN because it hardcodes internal build output contrary to user;
used scoped KINGSTON checks instead. Native UI/screenshots and visual SKM motion
NOT RUN. No CI/push/main change. No imported Unreal .uasset parsing or full parity.

Next: fix Ada's actual hair attachment by locating/extracting its mesh/rig and
using an explicit head attachment, then exaggerate head tilt/turn in native video.
Do not infer attached hair from a few near-static frontal frames. Existing shared
submesh reweighting, vertex-picking/gizmo, cloth collision and independent child
animation playback remain outside this bounded assembly stage.


## 2026-11-20 — Modular head shoulder pose synchronization

Code `eb4cc813bf48e782232600ca4a8b5a82517e0e94`, integration/motionweave-studio-facial-stage184.
HumanPreviewAnimation resolves missing lowerarm directions from complete matching
upperarm bind-world frames only (relative1e-3, reject conflicting donors). Ada head
upperarms1052/1061 were skipped before; head includes exposed shoulder geometry.
No mask or vertex removal. StudioHumanLookSessionTests asserts truncated joint
follows complete arm and distant same-name joint does not. Builds aiverse_studio,
aiverse_vivid_creator, session tests PASS on KINGSTON (build-shoulder-sync.log);
scoped studio_human_appearance_session PASS1/1 report20261120-191116-6147.
Actual native Metal shoulder-sync/t-pose.png inspected: large shoulder patches gone;
minor collar-edge skin still visible. pass1/pipelineFailures0. No full outfit QA,
UI/CI NOT RUN. No save/API changes. docs/vivid-preview-animation.md updated.
Next: hair2264 and brows2263 are unskinned children of vivid_full_root2265, not
vivid_face_head1848. Fix their bind-preserving hierarchy and test exaggerated
head motion; lashes already belong to skinned face1915. New user question answered
honestly: those grooms were NOT yet attached. Do not claim this shoulder fix does it.


## 2026-11-20 — Actual Ada groom binding and capture lifetime fix

Code `440a57d56b63b700d626b04675a95cc482f88328`, integration/motionweave-studio-facial-stage184.
New attach_grooms.cjs bind-preserves unskinned cards using inverse TRS chains;
prepare_full_character applies it to Hair_/Eyebrows_ nodes after framing.
Working KINGSTON Ada/model.gltf hierarchy corrected with backup; new assembled
Ada-Groom-Bound used for testing. Both cards now descend from vivid_face_head;
brows46 morph targets retained, eyelashes remain skinned face. Actual bind error
<4.4e-7. No copied animation, shaders, vertex edits, binary Git assets or save API.
Generic skinned meshes must use independent SKM attachment, not this rigid helper.

Node test PASS (bind, rotate/scale, morph ownership, idempotence, cycle/skin reject,
GLB roundtrip). Full original companion preparation PASS to groom-binding/prepared-pipeline.
Creator builds PASS (build-groom-binding.log, build-groom-capture-lifetime.log).
Diagnostic --preview-head-sweep gives ±0.45rad yaw/±0.20rad pitch on pose copy.
Initial7s capture succeeded; initial body recording crashed after238frames.
System report capture-crash.ips points to Animator::advance reading retired model;
failed-last.png shows revision2/headscale0.964 during interactive edit. Fixed
sequenceAnimator lifetime on publication/release; offline sequences omit controls
and reject unexpected model rebuilds. LLDB launch stalled under Rosetta and was
stopped, system report provided stack. No unrelated processes stopped.
Final head-final168 and poses-final360 native frames completed successfully;
encoded7s/15s videos on KINGSTON, pipelineFailures0. Inspected turn/tilt/blinks/T:
hair follows head; large shoulder protrusions gone; small collar intersections,
coarse hair silhouette and GRAY EYES still fail. Not universal groom/UE parity.
See docs/qa/vivid-head-binding-2026-11-20.md and groom-binding/gallery.html.
No frontend/GUI/CI/push; true strand physics, all rigs and realtime pacing NOT RUN.

Next USER STEERING: fix gray colorless eyes. Read actual eye baked PNG: brown
iris, black pupil present (centerRGB10), so investigate overlay layers/UV/render
binding. Source eyeShell blend alpha.35, eyeEdge alpha.25. Latest files extracted
to full-character/eye-review. Do not silently paint over screenshot or claim
fix until matched native captures; retain completed shoulder/groom work.


## 2026-11-20 — Gray eye glare repair and native comparison

Code `8f76aa33aefc796791361bc54eb52d0eddef74d5`, branch
integration/motionweave-studio-facial-stage184. HumanAppearance eye roughness
default .6; importer uses same portable single-layer PBR baseline, occlusion
alpha .12 / wet edge .04. Creator --closeup adds actual native camera close-up.
Existing explicit appearance values and save schema preserved; no renderer
shader edits, no final unlit material, no repainted iris pixels.

Source brown atlas/pupil were valid. Native no-shell diagnostic remained gray;
unlit/matte and .45/.65 roughness pair recovered contrast. Matched catalog
before-closeup.png/final-closeup.png confirms brown iris/dark pupil restored.
Neutral-light168frame head sweep completed, inspected turn36/blink41; eyes
close, hair follows, contrast retained. Jagged lash/eyelid edges, bright sclera,
coarse iris detail and no refractive cornea still limit quality. No UE parity.

All artifacts full-character/eye-review on KINGSTON. New Ada-Eyes-Reviewed
assembled model; working content/Ada factors/default updated with source-backup.
Historical assembled assets unchanged. Native Studio/Creator/session targets
built PASS. Node test_bake_materials.cjs PASS incl overlay regression. Scoped
studio_human_appearance_session PASS1/1 report20261120-193153-13311.
Native captures pipelineFailures0; GUI/CI NOT RUN; not realtime performance.
See docs/qa/vivid-eyes-2026-11-20.md. No push this stage.
Next: correct eyelid/lash edge coverage/deformation and improve iris detail plus
separate optical cornea response, retaining matched native before/after evidence.


## 2026-11-20 — Layered eye optics, material/assembly integration

Code585e6b4d2e34eb5f43a24d53eff766bb98775e66, integration/motionweave-studio-facial-stage184.
Material.eyeOptics appends UV centre/radius/depth to U2, equality/hash/merge;
GLSL/MSL refracted UV plus wet coat, glTF extras.aiverseEye v1 import/export,
assembly accepts baked Eye with portable PBR. Appearance adds3 fields(schema75).
Baker uses original iris fibers, radial limbus and restrained sclera; cartilage
strip material fixed white jagged edges. No geometry shrinking/deletion, lashes
alpha and51 morphs retained. Working Ada source updated with JSON backups; new
Ada-Eye-Optics asset, all external on KINGSTON. Creator --eye-detail .85 camera.

Native builds PASS; Node baker PASS;4/4 CTests PASS report20261120-200027-32474.
GLSL12/12 stages syntaxPASS ES300/330,Q0/1/2; initial harness missing ColorSpace
prefix fixed. Initial schema count72 and new fixture missing hiddenMeshes[]
failed; corrected tests, retained logs. Native Metal motion168frames7s24fps
completed pipelineFailures0. Eye/source reload logs show correct shading3 and
optics. GUI/mobile/Vulkan/OpenGL GPU/CI NOT RUN; no push.

CRITICAL latest visual finding: final-catalog.png after3.125 normalization
reveals excess pale glare again. dynLight ds includes broad environment/grid
response, not only source glints; multiplying the whole ds was insufficient.
Do NOT call MetaHuman parity or final quality achieved. Next: isolate sharp
reflections of actual point-light directions from broad ambient response; rerun
matched catalog capture and motion before delivering final media. Existing
final-motion.mp4/gallery.html are pre-refinement artifacts. Full analysis/tests
and known skin-normal/card-hair limits in docs/qa/vivid-eye-optics-2026-11-20.md.


## 2026-11-20 — Local corneal glints; final reviewed delivery

Code7773c40b0a141fa76907d828c776500d75086d3e, same integration branch.
Replaces broad dynLight ds scaling with a bounded up-to8 actual point/spot-light
reflection loop (distance/cone/normalized sharp lobe). Both GLSL/MSL changed;
low keeps sun/environment coat and omits point glints/parallax. All ordinary
PBR behavior and prior optical data/API preserved. No new asset/UV/exposure
change during this refinement.

Native Creator/Studio/appearance tests built PASS (build-local-glints.log).
Appearance CTest PASS1/1 report20261120-200337-34885 following integration4/4
report20261120-200027-32474. GLSL syntax12/12 PASS ES300/330 Q0/1/2, LDR
skinned+morph+normal permutations (glsl-local-glints.log). Vulkan/OpenGL GPU,
mobile, GUI, CI and realtime performance NOT RUN. No push this stage.

Native Metal catalog comparison local-catalog.png restores brown iris/dark
pupil with small sharp highlights; previous final-catalog.png was pale and is
DIAGNOSTIC ONLY. Final local-motion168 frames completed, inspected turn36,
blink41,tilt110; glints move without erasing iris; cartilage white edges gone.
pipelineFailures0. Encoded eyes-reviewed.mp4:168frames1500x92024fps (7s).
All artifacts /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/eye-optics/.
gallery.html points at reviewed images/video, previous takes retained as history.
Source content/Ada and assembled Ada-Eye-Optics retain previous stage's changes.

Readiness: iris/material/optics/export/reassembly implemented+scoped verified;
eyelid white-fleck fix and lash alpha/morph retention verified on this asset.
MetaHuman reference parity is NOT established: no volumetric ray tracing, source
skin-normal/card-hair artifacts persist; no all-rig/blendshape coverage claim.
Next if requested: controlled Unreal reference comparison and broader facial
corrective/eyelid deformation QA; do not re-enable broad grid specular as coat.


## 2026-11-20 — User-rejected glass eyes; blink normal repair

Implementation a1b0870f43f336d809e6613d32ac8f7ba7de6ac2 on
integration/motionweave-studio-facial-stage184. Prior eye-optics delivery was
explicitly rejected as glass-like. Softer/reduced cornea glints (GLSL/MSL), lower
sclera/fiber intensity, UV-faded lid occlusion; defaults pupil .048/sclera .40.
No schema/ABI/save changes; saved values override new defaults.

User then reported black blink stripes. Color-coded accessories, unlit skin,
normal-texture-off, shadow-off and morph-normal-off captures isolated malformed
imported blink NORMAL deltas on head skin. New repair_morph_normals.cjs derives
normal changes from deformed triangles, welds smooth coincident vertices and
repairs only eyeBlinkLeft/Right on known companion head material in
prepare_full_character.cjs. No position/UV/skin/morph-position changes. Creator
adds --shadow-quality off|medium|high diagnostic flag; default medium preserved.
Smoke gate permits zero shadow submissions only for explicit off.

PASS: Creator/Studio/appearance build (build-final.log), final CLI Creator build
(build-final-cli.log), CTest vivid_human_appearance 1/1, Node baker + folded-plane
normal repair/idempotence tests, full Draco companion import, GLSL syntax12/12
ES300/330 Q0/1/2. Initial nonexistent /Volumes/KINGSTON/tmp baker path failed;
rerun existing directory PASS. Initial no-shadow smoke gate failed, corrected.
All logs/media at /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/eye-natural/.
No GUI/Vulkan/OpenGL GPU/CI runs; no push.

Native Metal final-motion168frames/7s24fps captured; open, turn and both blinks
reviewed. Broad black upper-lid stripe removed with full textures+shadows intact.
eyes-blink-fixed.mp4 encoded168frames1500x920. Working content/Ada source
updated with backed-up JSON/appearance, appended normal buffer/accessors and
new eye/shell textures; old geometry/node/accessor IDs retained. Source reload
42frames confirmed repaired blink (source-blink.png). Candidate
content/Characters/Ada-Eyes-Repaired retains original groom/wardrobe bindings.
Prior Ada-Eye-Optics remains an older comparison; do not deliver as latest.

Known limits: small shadow/skin speckles around lids remain, card-hair edges, no
full RigLogic corrective parity or MetaHuman visual acceptance. Current repair
is deliberately scoped to known companion smooth head, not arbitrary hard-edge
SKMs. QA details docs/qa/vivid-eye-optics-2026-11-20.md. Next: diagnose remaining
shadow flecks and evaluate other expressions under matched reference lighting.


## 2026-11-21 — Adaptive character-stage shadows and soft portrait key

Code864bd2d8c589960bac9d9e46b60e67a1f80a3fe1; branch
integration/motionweave-studio-facial-stage184. User approved adapting shadows
to screen size after reporting stepped neck shadows and crawling nose edges.
Generic engine/render/AdaptiveShadowResolution.h chooses1024/2048/4096 from
focus-plane screen density, with15% upshift margin,40% downshift threshold,
60 quiet frames and120-frame cooldown. Host cap4096; per-view state; no ECS or
asset/save schema changes. VIVID lighting bounds fit foot/height with arm/floor
margin. Creator and Studio facial view opt in; Studio clears bounds on exit.
GPU target retirement remains existing Renderer3D behavior. No shader changes.

First fitted4096 still produced a hard nose patch. Scoped studio soft PCF
(.012*height) replaces its tiny-angle PCSS key; ordinary game lighting remains.
Source Ada geometry/materials untouched. Creator --camera-distance .5..120
and shadow diagnostics allow reproducible acceptance; default High now adaptive.
This is a single-map policy, NOT VSM/cascades/per-pixel demand/temporal denoising.

PASS Creator/Studio builds on KINGSTON (build-distance.log/build-studio-final.log);
standalone policy test and CTest adaptive_shadow_resolution1/1 with assertions
active in Release. First combined make regenerated CMake but missed the new
second target; initial CTest NOT RUN. Separate build + tests-assertions.log PASS.
Native captures: close1.05=4096, distant12=2048; default distance6.4 still4096
for this asset. soft-motion168frames1500x92024fps, pipelineFailures0; reviewed
open/turn/blink/opposite turn. Broad nose patch removed and neck softer.
Final adaptive-soft-shadows.mp4 encoded168frames. Earlier adaptive-shadows.mp4
is pre-softness diagnostic, do not deliver as latest. All artifacts/logs:
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/shadow-review/.
QA docs/qa/vivid-adaptive-shadows-2026-11-21.md records exact commands/limits.

No GUI/live zoom-tier-boundary visual check, Vulkan/mobile GPU, CI or frame-time
benchmark; no push. Policy hysteresis tested, not a zero-pop or zero-noise claim.
Background outside character fit/oversized attachments need broader checks.
Next: interactive zoom transitions and all8light rigs; quantify remaining
filtered shadow noise before temporal filtering changes. Preserve head/groom
bindings and previous blink-normal repair.

## 2026-11-21 — native FXAA and neck albedo diagnostic checkpoint

Implementation: `3d267f847a5a6500884418f9d34ec78d61ca9237`; branch
`integration/motionweave-studio-facial-stage184`. Native Studio/Creator now
resolve LDR FXAA through existing PostFx; retirement/shutdown included. Creator
--no-aa supports comparison. No temporal AA, save format or ECS changes.
Opt-in importer neck_albedo_smoothing filters only known lower MetaHuman atlas;
face/normal maps retained. External Ada source updated with backup on KINGSTON.

PASS Creator+Studio builds (build-restored.log), filter test, existing baker
test, optional real-import comparison; native168frame video and source reload
AA enabled=1 passes=2 pipelineFailures=0. Exact artifacts/commands and limitations:
docs/qa/vivid-neck-aa-2026-11-21.md. PostFx CTest discovery found zero tests.
No GUI, Vulkan/mobile, performance, CI or push. Skin texture cleanup is cosmetic.
Residual hard neck shadow at blink frame41 UNRESOLVED; unoffset receiver-plane
derivative experiment failed visually and was reverted (background seams).
Next: test user-requested elderly wrinkled model, then isolate neck shadow edge.
All builds/media: /Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/.

## 2026-11-21 — older male skin QA (visual acceptance failed)

Code/QA commit `a8414da4bca3ff8f2dc14e376696692feea6de26`, same branch
`integration/motionweave-studio-facial-stage184`. Added only fixture preparation
tool and QA report; no renderer/schema changes. External Patchbay MakeHuman older
male, not MetaHuman DNA or a scan. Provenance/license/SHA and exact paths in
docs/qa/vivid-elder-skin-2026-11-21.md. Asset/media on KINGSTON elder-skin/.

Initial native import FAILED:67 named morph targets exceed64. Tool now removes
speech visemes retaining52 ARKit shapes, remaps weights, decodes meshopt/WebP,
adds framing wrapper and VIVID skin mapping. No Ada neck filter, no synthetic
normal maps. Native catalog48frames, dramatic144frames, shadow-off1frame PASS;
AA1/passes2/map4096/pipelineFailures0. PNG/morph/frame-count validation PASS.
144frame24fps video encoded and selected front/turn/blink/opposite stills inspected.
Severe triangle shadow patches reproduce independently of Ada, disappear with
shadows off. Eyebrow/lash edges coarse; source2K diffuse lacks normal/roughness
maps and cannot prove skin microdetail. Visual quality FAIL, no MH parity.
No GUI/Vulkan/mobile/CI/performance check; no push. Latest native binary is
restored-shader FXAA build from preceding stage. Next: isolate shadow bias/receiver
plane discontinuity against both fixtures, then acquire higher-detail elder scan.

## 2026-11-21 — authored LOD preparation, partial stage

Implementation `7b9646b83d751d23d822006c5dbb189577ee0bec`, branch
`integration/motionweave-studio-facial-stage184`. New configure_lods.cjs links
authored meshes through existing aiverseLods metadata; rejects skin/morph/cycle
mismatches and detaches alternative leaf nodes from visible scene. Creator
--lod auto|highest added. No renderer/Studio UI/schema changes.
PASS JS policy and synthetic GLB roundtrip tests; Creator KINGSTON build; CTest
auto_lod_runtime1/1. Transition executable missing, direct command FAILED;
transition test NOT RUN (corrected QA's initial inaccurate statement).
See docs/qa/vivid-lod-2026-11-21.md for external logs and reproduction.
Incomplete: wardrobe assembly clears chains; no synchronized character policy,
arbitrary manual level UI, authored real character visual acceptance or CI.
Next preserve/remap wardrobe chains then real skinned/morphed character LOD
near/far native capture and editor controls. All outputs on KINGSTON; no push.

## 2026-11-21 — character LOD assembly + Studio controls

Implementation `95bae785b3893ca75f4d5135165f6478254df97f`, branch
`integration/motionweave-studio-facial-stage184`. Wardrobe keeps/remaps reachable
authored LOD chains for base and added parts; body/face deformation includes
alternatives. Native Studio existing animation bridge accepts optional validated
automaticLod (defaulttrue for old calls). Shared UI offers auto/LOD0, native facial
renderer consumes it. Parts keep individual thresholds; no equal-level controller.
Creator logs actual level counts; GLB chain tool registers standard extensions.

PASS Creator/Studio KINGSTON builds; CTest4/4 auto_lod_runtime, vivid_human_wardrobe,
vivid_face_shape, studio_human_appearance_session. Initial session test compile
failed Json::number(3) ambiguous, fixed3.; no remaining build failure. Standalone
ModelLodTransition assertions built with -UNDEBUG and passed (configured target
was absent). Typecheck/web tests/build PASS; Python UI29/29 PASS. Native elder
auto48frames LOD1=5 vs highest1frame LOD0=6; full Ada auto48frames LOD1=5,chains6,
blocked0,pipelineFailures0. Selected PNG inspected; video48frames24fps encoded.
Fixtures use diagnostic index simplification, retain original vertex/morph arrays,
threshold5000 deliberately exercises coarse levels. No VRAM/performance claim.
See docs/qa/vivid-lod-completion-2026-11-21.md for exact logs/limits and paths.

No GUI/live zoom-boundary/Vulkan/mobile/CI/push. Previous authorization: verify
without interface. No production source asset replacement. Shadow artifacts
remain unresolved. Next: production-authored LOD threshold tuning and live zoom
visual QA, followed by separate renderer-shadow investigation. No save/ECS change.

## 2026-11-21 — full older Bruce companion and planted body idle

Implementation `063c0a2a76595361618dd2a51eee88861a69725c`, branch
`integration/motionweave-studio-facial-stage184`. More visible spine/shoulder/arm/
elbow/hand breathing and settling; optional complete-leg pelvis weight shift
baked with shared solveTwoBoneIK and planted ankle targets/orientations. Canonical
spine_01/neck_01 suffix bug fixed, MakeHuman upper-body aliases supported. Default
Creator uses same full-body preview instead of head-only bob. Diagnostic
--preview-fixed-pose prevents automatic pose cycling in pure-idle capture.
No gameplay ECS/save change; no full motion-capture/MetaHuman quality claim.

Bruce published companion GLB downloaded from creategamecharacters/metahuman-to-glb
docs/5.7/characters/bruce; actual full body/outfits/grooms/face. Generic companion
preparation accepts FaceMesh names, attaches beard/mustache. Separate upstream
coverage maps + metadata converted by new prepare_companion_coverage.cjs. Explicit
external grey colour blend, source skin pixels retained, eye shell mask disabled
for layered eye. Final alpha BLEND; early MASK was too sparse. Source/license/SHA/
adaptations recorded externally, no binary vendoring. Repository code MIT not
a claim about Epic character content licensing. Python download certificate failed;
curl retry succeeded. Source content Ada untouched.

PASS Creator/Studio KINGSTON builds, extended native wardrobe test (initial test
found canonical spine name bug, then PASS), shared groom and morph-normal tests;
Studio appearance session PASS. Synthetic complete-leg test samples5phases and
checks ankle drift<.001 plus moving pelvis; loop endpoint/body channel tests PASS.
Final bruce-continuous-idle144frames/24fps fixed relaxed pose; pipelineFailures0,
57commands. Video bruce-continuous-body-idle.mp4 encoded144frames1500x920. Reviewed
fullbody36/108 and closeup0/41 blink. Earlier bruce-body-idle.mp4 switches poses,
NOT final pure idle. All files on KINGSTON full-character/elder-skin/.

QA: docs/qa/vivid-bruce-body-idle-2026-11-21.md. Known visual FAIL remains on
triangular collar/cloth patches and shoulder intersections; do not claim polished
clothing or complete MH Creator parity. No GUI/Vulkan/mobile/CI/performance/push.
Next: isolate clothing faceting/shadow vs deformation and mask head shoulders
against outfit; validate idle on additional body rigs and actual ankle targets.

## 2026-11-21 — Bruce clothing deformation/visibility and fixed-PCF repair

Implementation `93fb42cbd79a4c3877bf6f460331d9733d907e33`; branch
`integration/motionweave-studio-facial-stage184`. Explicit offline jacket weight
smoothing (final8iterations/.8 strength,top4normalized) and closed-outfit skin
index mask (3302triangles removed,neck ancestry+hands retained) repair Bruce's
spikes/skin protrusions. Geometry/UV/normals and texture contents unchanged.
Original source remains model-final.glb; corrected model-clothing-fixed.glb and
manifest published under KINGSTON content/Characters/Bruce-Full. Manifest backup
manifest.before-clothing-fix.json; clothing-repair.json + clothing-visibility-mask.json
record reversible parameters. Source binaries not vendored. Mask is outfit-specific;
rebuild from original for other clothes. Imported vertex numbering can change,
so external CSR/LOD sidecars must not be reused blindly (this fixture has none).

Common preview now synchronizes missing-leg pelvis only with complete rigs sharing
the same bind-world frame; resolves modular face detachment during weight shift.
Native regression rejects a distant donor. Ada42frame before/after inspected: broad
shoulder patch disappears without source edits. Creator --preview-bind-pose added.

GLSL/MSL fixed-PCF SKINNED/BAKED path omits triangle receiver-plane extrapolation
and bounds normal offset to1..4 existing texel offsets by filter footprint. Rigid
receivers/cyclorama and PCSS unchanged. No RHI/batching/ECS/save changes.
Unconditional shadow derivative call experiment did not help; global removal of
receiver-plane bias leaked background, rejected. First4/.65 weight/mask variant
still exposed neck skin in T pose; final8/.8+neck ancestry resolved inspected case.

PASS Creator/Studio/native session builds; CTest wardrobe/face/session3/3;
modular-pelvis wardrobe regression additionally rebuilt and PASS. JS smoothing
test PASS; actual-asset geometry/normal/UV/weight/index subset validation PASS
(validation8.log). GLSL ES300/GL330 Q0/1/2 skinned+morph+normal12/12 glslang PASS.
Native Metal approved-motion144frames24fps + torso37frames + Tpose1frame inspected;
55headcommands,50colour/shadow,pipelineFailures0. Final video
bruce-clothing-approved.mp4 encoded144frames1500x920. Earlier reviewed/fixed videos
are intermediate variants; final still approved-torso.png, t-pose8.png. All under
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/cloth-review/.
QA docs/qa/vivid-bruce-clothing-2026-11-21.md. No GUI/Vulkan/mobile/CI/perf/push.
Next: broader outfits/pose/material-quality checks and authored corrective poses.
No claim of full Unreal cloth physics or universal zero-shadow-artifact behavior.

## 2026-11-21 — eight-light wardrobe/pose regression and backdrop fix

Implementation `ba0f89312774dbd21ecafa52cc12f87f88ca578a`; branch
`integration/motionweave-studio-facial-stage184`. Human studio cyclorama now uses
existing kDrawNoCastShadow in Creator and native Studio facial view: it receives
character shadows but no longer casts onto itself. Found thin backdrop seam in
cool/portrait light; matching-phase cool before/after confirms removal while
character silhouette remains. No shared shader/RHI/batching/save/asset change.
Creator --preview-light-sweep cycles eight presets every48 offline sequence frames;
invalid prerequisites reject exit2. Offline sequence logs ankle/ball bone origin
max drift/sample count; no per-frame sampling in normal interactive mode.

PASS Creator+Studio KINGSTON builds (build-metrics.log,
build-receiver-background.log), invalid CLI test, final frame counts/log checks.
Native macOS15.6 Apple M3 Ultra Metal: final-transitions360frames/15s and
final-lights384frames/16s,24fps,1500x920; pipelineFailures0. Selected A/T/return
checkpoints and all8final lighting PNGs inspected. No old shoulder skin/needle
folds observed in these views. Numeric endpoints: transitions max0.00050682
model units/2880samples; idle0.00000933/3072samples (~0.013% of3.8-unit height
for transitions). This is bone-origin characterization, NOT physical sole
contact/cloth simulation or locomotion acceptance.

Videos encoded: bruce-pose-transitions.mp4 and bruce-eight-lights.mp4. All
artifacts, binary/asset/source hashes, detailed report and logs:
/Volumes/KINGSTON/AIVERSE-builds/motionweave-vivid/full-character/wardrobe-regression/.
QA docs/qa/vivid-wardrobe-regression-2026-11-21.md. Initial light-sweep images
retain found seam; use final-lights/final-*.png for delivery. No new unit suite
or unchanged-suite repetition; actual native captures cover this host fix.
No GUI (standing user instruction: verify without interface), other outfits,
Vulkan/mobile, CI, performance benchmark or push. Next: additional outfits and
camera angles, production LOD tuning and interactive editor parity.
