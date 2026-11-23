# AIVERSE SpectraFX modernization — implementation plan (2026-11-23)

Status: **planned multi-stage program, NOT fully implemented**. This document is the technical contract for subsequent agents. The current branch contributes camera/event priority scheduling, not every system below.

## Baseline confirmed in `main` at `87ba5b1439267bea86c570489e8869b3f7641a0e`

- Universal VFX `engine/fx`, `aiverse::spectra` and `fx::SpectraFxSceneHost`; version 1 and version 2 graphs; bounded GPU VM subset and shared CPU world.
- Metal GPU particle path, CPU twin parity report from 2026-11-22; cue queue/scene host, pooled emitters, GPU budget governor and LOD; entity effects declared in game/map data.
- `SpectraGraphWindow` is native/editor-specific and cannot be assumed to be ported to the ongoing `feature/studio-html5` editor. Existing graph-v2 and module interfaces should be reused rather than forked.
- SpectraFX owns particle simulation and scene HDR effects. PRISMA owns bloom/postprocess. LENS owns cameras. PHYSIS owns weather/wind. TerraWeave owns terrain. NEREID owns water. Box3D owns physics. MotionWeave owns animation/poses. No genre-specific copies.

## Current implementation notes (feature branch)

- SpectraScale queued-cue significance: implemented in source, focused Ubuntu
  build and 2/2 CTests passed for a pre-fields revision. Other platforms and
  complete engine acceptance are not yet confirmed.
- Initial SpectraFields acceleration bridge: implemented in source for dedicated
  GPU and CPU-twin emitters; generic provider registration, validation and
  lifecycle tests added. Full environment Data Interfaces, shared-arena GPU
  fields, physics collision, fluids and editor authoring remain future work.
- Focused Linux/macOS PR workflow: `.github/workflows/spectrafx-significance.yml`.

## Stage 4 (2026-11-23) — typed scalar Data Interfaces implemented in source

- The Graph v2 authoring model now supports a bounded `dataInterfaces`
  declaration, a runtime-only `DataInterface` value node, and a strict
  name/slot/fallback validator. See
  `docs/agent-handoffs/spectrafx-data-interfaces-stage4-2026-11-23.md`.
- Existing CPU/Metal VM param0..param7 carries the values without creating
  an incompatible second graph language. Data is sampled once at `burstAt`,
  not continuously per particle. Shared CPU `BatchWorld` is covered by
  source-level tests.
- Only scalar host callbacks and shared authoring helpers are implemented;
  full HTML5 Studio panel, texture/buffer GPU Data Interfaces, real PHYSIS
  adapters, Vulkan compute and fluid/destruction systems remain unimplemented.
- Test status is **pending**, not accepted: focused graph runtime/compiler
  CTest additions and CI workflow were authored, but no native PASS has been
  recorded for the latest Stage 4 commit.

## Stage 5–6 — HTML5 SpectraStudio authoring (2026-11-23; draft PR #115)

Source in `feature/spectrafx-html5-data-interfaces-20261123`, based on draft
PR #113. The `fx_graph` HTML5 dock tab now uses a dedicated Graph v2
Data Interface inspector, native graph/draft operations and real project
Assets texture picker. Stage 6 adds a topology canvas with node drag, zoom,
native catalog, Add/Delete and execution/typed-port Connect/Disconnect through
`studio.op` → `fx_graph_edit`; the front end does not implement a second
graph compiler. Changes save only after native Validate or into explicit
unpublished drafts. Test models are in `apps/studio-web/tests/` and linked
by `npm run test:graphs`. Separate handoffs:
`docs/agent-handoffs/spectrafx-studio-stage5-2026-11-23.md`
and `docs/agent-handoffs/spectrafx-studio-stage6-graph-canvas-2026-11-23.md`.

**Not accepted yet:** current feature SHA has no confirmed Node TypeScript
or Vite PASS, WebView interaction QA or real particle GPU preview. Full GPU
preview/time controls, complex graph function/program editors, advanced
curves/gradient widgets, external PHYSIS adapter, Vulkan compute and
fluid/destruction systems remain separate work.

## Stage 7 — Curve / Gradient key authoring (2026-11-23, unverified UI)

The HTML5 Graph v2 pane now includes `SpectraCurveEditor.tsx` and
`spectraCurveModel.ts`. Existing Curve nodes support key time/value
edits and a static scalar plot, and Gradient nodes support RGBA/alpha
key stops with a static gradient strip. The immutable model validates
1..32 unique normalized keys, finite ±1e7 scalar values and safe RGB(A)
hex strings; it does not invent a new effect format or GPU renderer.
`spectraCurveModel.test.mjs` has been included in `test:graphs`.

**Current test status:** source committed in draft PR #115, CI pending;
do not call TypeScript, Vite, WebView or native graphics PASS on this SHA.
Handoff:
`docs/agent-handoffs/spectrafx-studio-stage7-curve-gradient-2026-11-23.md`.

**Still remaining:** real RHI-owned particle preview and timeline, advanced
Bezier curves, actual hardware scene captures, Vulkan/Metal GLES parity,
PHYSIS adapters, shared arena fields, fluids/destruction and final merge.

## Stage matrix

| Stage | Feature | Design/implementation requirements | Completion criteria |
|---|---|---|---|
| 0 | Audit/baseline | Diff `main` against active SpectraFX/Studio branches; list actual graph node/compiler/backend coverage; reproduce baseline build | SHA-tagged baseline + native acceptance results |
| 1 | SpectraScale cue priority | Camera/event weighted scheduling, bounded queue, no lifecycle inversion; project JSON opt-in | Native + mobile smoke and full regression; code present in this feature branch but not build-validated |
| 2 | SpectraGraph 2.0 | Typed SSA/VM IR with module schemas, functions/subgraphs, variable lifetime, stable serialization; compile CPU/GPU from same effect; avoid a second incompatible graph format | Compiler fuzz/property tests, shader parity for supported ops, old graph compatibility |
| 3 | SpectraStudio HTML5 | Graph canvas, typed inspector, curves, color gradients, drag-drop assets, parameter search, lockable fields, undo/redo, live GPU/CPU preview, frame stepping, profiling | Browser interaction QA on macOS, captured frames, persisted import/export, UI-scale tests |
| 4 | Vulkan compute backend | RHI compute/upload/barriers/indirect dispatch; feature gates match Metal semantics; explicit CPU fallback for unsupported ops/devices | Real Vulkan tests, GPU parity, buffer lifetime and device-loss cases |
| 5 | GLES/WebGL compatibility | CPU twin and reduced GPU options, strict capability map, no implicit unsupported compute; memory and shader variants | Android GLES3 + WebGL2 smoke and visual QA |
| 6 | SpectraCollision | Depth + SDF + TerraWeave heightfields, collision events with bounded async feedback; Box3D adapters are read-only and presentation-only | Collision screenshots, GPU/CPU parity on agreed collision subset; no authoritative ECS mutation |
| 7 | SpectraGeometry | GPU mesh/sprite, flipbook, ribbon, beam, spline and decal output stages sharing simulation state | Multi-camera render tests, correct transparent order/depth/PRISMA, performance budgets |
| 8 | SpectraFields | Data interfaces for PHYSIS wind/turbulence, TerraWeave terrain, NEREID water, actor sockets and generic vectors; no cyclic includes | Pure adapters, sampling tests, fixed-timestep behavior and explicit fallback |
| 9 | SpectraFluid | Grid smoke/fire/gas temperature/density with sparse adaptive tiles and deterministic authoring cache; optional 2D fallback | Mobile limits, desktop compute/perf tests, real image captures, stable cache serialization |
| 10 | SpectraLighting | Transient particle lights into common light collector, capped clustered injection and distortion scene pass before PRISMA | Cross-backend HDR/Bloom/depth captures; no competing postprocess engine |
| 11 | SpectraDestruction/Surface | Submeshes, debris, material stamps, char/snow/wetness masks, Box3D rigid bodies with visual-only small fragments | Resource retirement and surface lifetime tests; gameplay-safe ownership |
| 12 | SpectraEvents | Bounded GPU-to-GPU child events and spawning, generation-safe handles, per-frame quotas | No recursive unbounded effects, no stuck emitters, rollback tests |
| 13 | SpectraCache | Capture/play/reverse interpolation, versioned files, hashes/checkpoint redaction, no leakage of raw sensitive state | Artifact compat & corruption tests, playback video/frames |
| 14 | SpectraAudio | Render-only audio band envelopes, FFT/beat synchronization, deterministic envelope replay optional | Audio-disabled fallback, sample correctness and latency tests |
| 15 | SpectraProcedural/AI | Preset generator and MCP create/validate/preview/screenshot/profile tool chain, without unsafe arbitrary code execution | Agent can edit asset, collect screenshots, diagnose shader/graph errors |
| 16 | Final QA/perf | Hardware matrix, 10K/100K/1M particle benchmarks, CPU p50/p95, GPU frame p50/p95, memory lifetime, overflow and visibility/fog | Acceptance artifacts on hardware and clean cherry-pick/merge review |

## Non-negotiable runtime contracts

1. `SpectraFxSceneHost::prepareGpu` runs once per presentation tick before render passes; multiple LENS views may prepare camera ordering separately but must never step the same particles multiple times.
2. GPU work is submitted only at safe RHI frame points. Recycled GPU resources remain fenced/quarantined until hardware completion, preserving existing retirement code.
3. CPU fallback preserves authored effect semantics; never silently drop an unsupported module when selecting a backend.
4. Visual presentation is allowed to be nondeterministic, but never writes to authoritative ECS, server simulation, rollback checksums, or network ownership.
5. Quality/LOD may defer or suppress only explicitly cosmetic Play; all Stop/Kill and entity removal cleanup always run. Never weaken visibility checks in order to render hidden opponents' events.
6. Use shared graph and GPU capability/feature contracts. Do not add genre-specific FPS/RTS particle engines, duplicate PRISMA stages, or clone NEREID/PHYSIS/TerraWeave simulation.
7. Every implementation stage must have a **separate** `docs/agent-handoffs/` file describing changed files, APIs, branch and commit SHA, actual tests/results (including skipped tests), limitations and an unambiguous next step. Update `docs/agents/spectrafx.md`, `skills/visuals/aiverse-spectrafx/SKILL.md` and this matrix when behavior changes.
8. Preserve existing versioned assets and source aliases; migration must be explicit, lossless or diagnostic.
9. Do not merge to `main` while native acceptance is missing or another agent's branch has unreconciled changes.

## Performance validation

Record real GPU device model, backend and test project. Suggested scenarios: 10,000 sprites with random births, 100,000 additive particles, 1 million particles under heavy culling, ribbons/mesh output, transparent alpha sort, 16 cameras in one simulated tick, 1,000 entity attachments, queue saturation 10,000 cues, editor hot-reload at 60 Hz, rollback and device recreation. Record per-scenario CPU/GPU p50/p95, peak allocations, dropped/deferred particles, emitted/dropped events and stable image comparisons. Never substitute whole-GPU frame time for SpectraFX-only time.

## Next immediate work after Stage 1

1. Run the existing CMake smoke and `spectrafx_contract` tests on a clean checked-out feature SHA, fix any errors.
2. Verify routes.json `cuePriority` validation and `importance` ranking under real `SpectraFxSceneHost`, including no behavior change when absent.
3. Capture bounded queue profiling and non-regression of rollback/Stop/Kill; add positive/negative property tests.
4. Integrate a typed editor inspector and HTML5 Studio source browser **after** synchronizing `feature/studio-html5` ownership; do not modify that parallel branch blindly.
5. Continue with Stage 2 IR design and capability-based compiler as a separate code+handoff stage.
