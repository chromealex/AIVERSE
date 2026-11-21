# PRISMA Stage 40 — safe CPU-only preview playback continuity

**Date:** 2026-11-19.
**Working branch:** `feature/prisma-postfx`.
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/26.
**Previous handoff:** `docs/handoffs/prisma-stage-39.md`.
**Main:** unchanged; no unrelated branches merged.

## Actual implementation and exact commits

1. `d65fb0840b4469896c762382a04ba527fe01d1c0` — `engine/render/AnimationGraph.h`: `AnimationGraphInstance::PreviewClockSnapshot`, `snapshotPreviewClocks()` and `restorePreviewClocks()`. Optional CPU-only preview checkpoint API.
2. `cfc060eb2d79f5300ed81054c2d3ad7740cc286f` — `engine/render/AnimationGraph.cpp`: capture current state ID/time, all clip clocks plus model skeleton/clip identities and validate every field before changing the new graph instance. Reject stateful graphs, incomplete transition fades and incompatible models.
3. `849d70999046e7d9b09479807a2863b3d675540f` — `apps/editor/animation/AnimationEditor.h`: snapshot the previous graph before GPU bundle retirement; on recompile, consume only if model and authored graph program keys match; retain existing editor controls and authoring state. Explicit Preview Reset and document Reset clear cached clocks; failed compilation never sets the successful program key.
4. `d93c6ea1fb863c309faa9020f469dc2bf5e40f0b` — `docs/prisma.md`: add Stage 40 architecture, conservative limitations and update outdated status that had described later effects as unimplemented.
5. This separate handoff file `docs/handoffs/prisma-stage-40.md` is committed after the source and documentation commits.

## Behavior and API contract

`AnimationGraphInstance::snapshotPreviewClocks()` returns `std::optional<PreviewClockSnapshot>`. It does not retain GPU resources, `Model*`, raw graph nodes, poses, the motion database, native callbacks, contact solvers, controllers, trait buffers or animation environment. It is deliberately a narrow *preview-only* clock checkpoint.

Eligible programs contain only `Bind`, `Clip`, `Blend`, `Additive` and `TwoBoneIK` nodes and **no** stateful trait/native/rig/motion/controller/subgraph operations. An unfinished fade or unhealthy graph yields no snapshot. The checkpoint records state IDs, model skeleton node names/parents, node ID/kind order, clip identity/duration/loop mode and per-clip elapsed time. A new graph validates state/skeleton/nodes/clip metadata and finite bounded times before applying state index and clocks. The caller **must additionally compare** the full authored program key: snapshot alone intentionally does not encode every graph operation parameter, transition condition and binding. In this editor `AnimationEditorPanel::previewProgramKey()` supplies that key and excludes editor-only node positions.

`AnimationEditorPanel::release()` saves the eligible snapshot before moving old `Model` / `Portrait` / `AnimationGraphInstance` into the GPU-fence-owned bundle from Stage 39. A hidden/closed Graph or Rig tab can then rebuild its preview, retaining the current playback state if the program and model are compatible. Document actions that reset the authored graph clear snapshots; explicit preview Reset restarts playback. If validation or compatibility fails, playback safely restarts from the newly compiled graph's initial state; no GPU fence waiting occurs within this checkpoint logic.

Other animation runtime state is **not** serialized or restored. For transition fades, complex rig and surface locomotion, native operations, motion matching, controllers, nested subgraphs or traits, the old initialization behavior is deliberately preserved. The snapshot is an in-process CPU object, not a durable file format or a game replay/save state.

This stage is presentation-only and genre-independent at the `engine/render` API. It does not change FPS/RTS ECS ticks, simulation state, production animation playback, PRISMA shader effect flags or original `postfx` JSON.

## Actual verification / tests

**Performed:** read the Stage 39 handoff and current GitHub branch sources, inspected `AnimationGraphInstance::update/evaluate/reset`, `AnimationEditorPanel::release/preview`, and Stage 39 GPU retirement contract. Applied changes as four separate source/docs GitHub commits; fetched changed files back to confirm the symbols and UI wiring on the feature branch.

**NOT RUN** (features-first instruction): CMake generation, C++ / Objective-C++ build, CTest, GUI playback/hide-reopen, Graph/Rig runtime, real Metal/GL/Vulkan GPU fence validation, device loss, CI. No compile/test PASS or runtime safety certification is claimed.

## Limitations / next Stage 41

1. Test on real editor builds once allowed: simple clip loops, multi-state transitions after a completed fade, paused playback, hide/show, model reload with changed clips, renamed bones, graph edit, explicit Preview Reset, tab destruction and asset project switch. The checkpoint must not restore across modified authored program keys.
2. A clock snapshot is not a full transition/pose snapshot. Mid-fade and model-dependent operations are excluded intentionally. If seamless complex runtime playback is required, design a separate **versioned** CPU-only state protocol for motion controllers, rig/IK state, graph branches and transitions, with explicit model/graph fingerprint checks; never transfer raw pointers or Pose state blindly.
3. The editor's full authored program key is produced from a JSON dump. If changes are made to keys excluded as visual-only, ensure they remain presentation-only; otherwise cache invalidation may suppress a required graph rebuild.
4. The existing normal-release GPU retire queues still depend on the backend's real completed submission fence. This stage does not change those semantics or fix unverified hardware-specific barriers.
5. Stage 41 recommended **PRISMA-specific feature priority**: consolidate memory/pressure telemetry per view (including ShaderGraph/Portrait unknown bytes) and improve viewport-aware render-target lifetime. Alternatively, return to actual color/post effects for a new opt-in effect only after reviewing the existing full Prisma catalog; do not conflate SpectraFX VFX emitters with Prisma screen post-processing.
6. When the user permits tests, run CMake/CTest and an editor/Metal/GL/Vulkan smoke pass for all Stage 35–40 code, then fix build failures before merging the still-draft PR #26.
7. Write `docs/handoffs/prisma-stage-41.md` after the next feature stage with branch, exact SHAs, changed files, API, checks actually run, limitations, and next owner action. Do **not** merge PR #26 to main without explicit direction.
