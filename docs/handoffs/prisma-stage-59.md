# PRISMA Stage 59 — manual cinematic EV, box ROI metering and failed-pyramid fallback

Date: 2026-11-19
Repository: chromealex/AIVERSE
Working branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base branch: feature/universal-entity-inspector-fps
Previous stage: docs/handoffs/prisma-stage-58.md
Pre-handoff branch HEAD (after source/docs/skill): 66ae971a6811a177f204963fa6f451d73dd124b2
Main not modified. Do NOT merge without explicit user authorization.
User-requested sequence: complete functional features FIRST; native CMake/CTest/shader/GPU/visual and performance validation only after the functional backlog.

## Scope and actual functional completion

Stage 59 adds two opt-in, genre-independent PRISMA features to the
existing `PrismaType::AutoExposure` catalog:

1. Fixed manual exposure for cinematics: author `manualExposure=true`,
   `manualEV` in [-12,+12] stops; or override a stable native view ID.
   This mode uses an existing HDR composite scalar, NOT Auto Exposure
   history, shader or sampled texture. It is independent of the
   `AIVERSE_PRISMA_AUTO_EXPOSURE=1` runtime flag but still requires the
   authored AutoExposure effect, active quality tier and usable linear HDR.
2. Metering region shape: default Stage 58 circle, or a soft-edged
   rectangle with `meterBox=true`, `meterHalfWidth` and
   `meterHalfHeight`. Both are in normalized viewport axes. No GPU
   texture mask or new postprocess pass is introduced.

The source also prevents allocating/writing an exposure history when
the measured HDR mip pyramid did not reach a valid 1×1 result.
These are source-level implementations, NOT native-tested releases.

## Exact source commits and touched paths

1. `8407de151f90fb726d43836859e767dd32acde53` —
   `engine/render/PrismaData.cpp`: append opt-in `manualExposure`
   (default false) and `manualEV` (default 0, range −12..12)
   to the existing Auto Exposure catalog. Preserve all legacy
   parameter offsets and external JSON/MCP naming.
2. `435f10cc778f2ef22549dce60c0653a8d4e83d11` —
   `engine/render/Prisma.h`: public
   `setViewManualExposure(id,enabled,ev)` and
   `clearViewManualExposure(id)`; camera-private override map;
   `ViewEffectReport.manualExposureEncoded/manualExposureEV`.
3. `dd2a09daf67185ef9a5516ce110d4b8543668535` —
   `engine/render/Prisma.cpp`: override setter/clearer,
   finite/saturating EV clamps and per-view removal from
   `retireView`, `retireUnregisteredViews`, `shutdown`.
   Transition back to auto invalidates camera exposure history.
4. `2c4cf87bb837ce574c9e27ce16a3b6be00e995fe` —
   `Prisma.cpp`: precedence of per-view override over
   profile/volume manual flag (including explicit false),
   disable AE meter when manual selected, apply
   `u.filt[2] *= std::exp2(manualEV)` before final HDR
   tonemapping, report only if final composite pipeline exists.
   It composes with authored ColorGrade exposure, does not
   allocate another history texture or sampler.
5. `87dacddbd1fa58b75d83e1c716776ff1829ff8c6` —
   `Prisma.cpp`: Stage 53 AutoExposure effect status now
   treats a valid fixed manual EV as a final HDR composite
   operation, not as a missing AE shader; no false
   ExposureLog readiness requirement.
6. `19e0f83d88b14b0f8bb2adf195cb852b6ef0bbe8` —
   `Prisma.cpp`: validate HDR metering pyramid before
   allocating/writing per-camera 1×1 exposure histories.
   Failed/incomplete reduction invalidates previous AE
   history and skips the adaptation pass. No forced GPU wait.
7. `d0f0234a796e367023b14a5811f80855a92d16dd` —
   `apps/editor/windows/PrismaWindow.cpp`: editor manual EV
   diagnostics and authoring help (fixed vs freeze),
   including stage-58 inherited region controls.
8. `252a0fb6d2b413b49b1841ac704c563b18bbc51b` —
   `PrismaData.cpp`: append `meterBox` (default false),
   `meterHalfWidth` (.25, range .01..0.5) and
   `meterHalfHeight` (.25, range .01..0.5) for the
   alternate rectangular mask.
9. `96e8d9b73cdb2ac71e7f8b8b1395773a1236e249` —
   `Prisma.h`: append `box/halfWidth/halfHeight` to
   `ExposureMeterRegion` aggregate without changing its
   old six initializer positions; expose
   `ViewEffectReport.autoExposureBoxMetered`.
10. `85f96c0a6b8cf479d7c50a5191eee05a3181ec27` —
    `Prisma.cpp`: validate native ROI half-size finite
    inputs; resolve authored/native ROI shape, bind a fifth
    `regionBox` vec4 only to the `kExposureLog` pipeline
    through `ExposureMeterU` (including the 1×1 case);
    preserve `SimpleU` for Reduce/Adapt and other shaders.
11. `8ca81449431c7f582c07da49aa9e07b94e4e9773` —
    `engine/render/PrismaShaders.h`: opt-in
    circle/rectangle selection in `kExposureLog` with
    aspect-correct signed-distance rectangle edge,
    soft `smoothstep` and a nonzero outside weight.
12. `4d9133a3208d0ffe11ffcfaac5c99300f9013d28` —
    `PrismaWindow.cpp`: editor shows actual ROI shape
    per rendered camera and documents box half-sizes.
13. `4523946653f484f902c9f16d9102e78e206f7b2a` —
    `Prisma.cpp`: additional finite/clamp guard for
    authored/native-mutated manual EV before multiplying HDR.
14. `6df4c208f7baf084c08a947e66c9e5d57e99b278` —
    `docs/prisma.md`: Stage 59 architecture, scene-linear
    color order, fallback semantics, per-camera lifetime,
    limitations and unrun tests.
15. `66ae971a6811a177f204963fa6f451d73dd124b2` —
    `skills/visuals/aiverse-prisma/SKILL.md`: Stage 59
    public API, ownership, shader encoding and feature-first
    continuation guidance.

This handoff file is committed SEPARATELY after source, design
documentation and skill to comply with AGENTS.md.

## Canonical public API and invariants

```cpp
// Stable ID, never camera dimensions or position.
prisma.setViewId(viewId);
prisma.setViewManualExposure(viewId, true, 1.5f);

// Explicit false suppresses authored manualExposure=true:
prisma.setViewManualExposure(viewId, false, 0.0f);
// Return to postfx/profile/volume inheritance:
prisma.clearViewManualExposure(viewId);

// Stage 58 existing initializer remains compatible. For box mode:
aiverse::render::Prisma::ExposureMeterRegion region{};
region.enabled = true;
region.centerX = region.centerY = 0.5f;
region.box = true;
region.halfWidth = 0.3f;
region.halfHeight = 0.2f;
region.feather = 0.1f;
region.outsideWeight = 0.05f;
prisma.setViewExposureMeterRegion(viewId, region);
```

Manual EV is an explicitly authored/overridden **fixed camera
exposure factor** and is not a second GPU autoexposure algorithm.
When selected, it skips all AE meter/adaptation passes, invalidates
old history so later auto resume measures the current scene, and
uses the existing `CompositeU.filt[2]` scene-linear multiplier
before ACES/sRGB, display grade, LUT, Shader Graph and UI.
`std::exp2(manualEV)` is bounded from 1/4096 to 4096.
This same multiplier remains effective on a successfully prepared
plain `composite(0)` fallback after graph failure. If no final
pipeline was prepared, report `encoded=false`; never infer GPU
completion or correct pixels merely from the API being called.

Auto Exposure still owns exactly one camera-private 1×1
history pair keyed by stable `viewId`. It is not used by
manual mode. Removing the view also removes per-view manual
overrides; render-size-shared targets remain independent.

Metering ROI is not a separate screen effect: the original
weighted-log numerator/weight reduction and Stage 57
highlight rejection remain unchanged. Rectangle is optional,
with signed distance to the box (normalized X in width, Y in
height) and feather scaled to frame height. Outside samples
have positive nonzero weight, and the old circular mode is
selected by default. The kExposureLog UBO gains `regionBox`
as fifth vec4; no unrelated shader UBO or texture layout was
changed. The native struct preserves the original positional
aggregate initializer by appending fields only.

If any stage of an automatic meter pyramid fails, do not
allocate or write 1×1 camera history from incomplete data.
Other fixed/composite processing continues without sample
Auto Exposure. Previous history validity is discarded and
a later frame can retry; no backend fence is fabricated.

## Actual verification vs unrun paths

Performed: inspected PR #26 and Stage 58 handoff at branch
`1b3b8016bf0c6474209daaed8c47fa74aba1678b`;
read existing AutoExposure implementation, catalog, shader,
camera override map, composite stage and editor.
After code changes, fetched all five changed source files
from GitHub. **49/49 structural source-presence,
call-order and ownership checks passed**: manual mode
opt-in, authoring/clamp, camera overrides, lifecycle
and explicit false, avoiding new AE history, pre-tonemap
multiplier, final pipeline reporting, ROI default,
shape/aspect/weight safeguards, five-field UBO and
failed-pyramid validation. Also fetched back both
updated documentation and skill pages.

**NOT RUN** by feature-first user instruction:
CMake configuration, local clang/C++/Obj-C++ build/link,
GLSL/MSL/SPIR-V shader compiler, CTest,
`./run-tests.sh`, editor/client runtime, GPU smoke
on Metal/GL/GLES/Vulkan/WebGL, HDR pixel comparisons,
GPU fence overlap/stress, screenshots, perf counters,
profiling or CI. Source checks are NOT an assertion
that any GPU path works correctly on hardware.

## Limitations / bounded next Stage 60

- A circular or rectangular **analytic ROI** is not
  a user-supplied **texture metering mask**. Texture masks
  and arbitrary shapes would need a separate RHI format,
  trusted external texture owner, shader sampler and
  explicit fallback; they are not implemented here.
- Manual EV is a global fixed exposure per camera.
  No physically-based camera shutter/aperture/ISO
  calibration or tone-mapping dynamic range redesign.
- Exposure and postFX Shader Graph failures still
  require native shader/pixel validation, especially
  on GLES/WebGL with sampler limits. The first
  presentation after toggling manual exposure needs
  GPU verification for exact temporal behavior.
- Preserve the owning model: only Prisma owns the
  final screen-space passes, SpectraFX never becomes
  an image compositor, and no authoritative ECS
  state is modified.
- Recommended Stage 60: audit the remaining PRISMA
  catalog for functional completion (LUT/Shader Graph
  failure ordering, reactive transparency, temporal
  history, camera lifecycle), implement any concrete
  missing features, and write its own separate
  handoff. When feature scope is truly closed, begin
  a dedicated native build/CTest, multi-RHI GPU
  smoke/parity, image/latency/fence/profiling phase.
  Report actual test commands and PASS/FAIL.
- Keep PR #26 Draft and do not merge to main/base
  without explicit user approval.

## Mac pull commands including branch pull

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If the local checkout is rebasing or conflicted,
leave it untouched and create a clean separate
worktree from `origin/feature/prisma-postfx`;
do not reset, force-push or discard other agents'
changes.
