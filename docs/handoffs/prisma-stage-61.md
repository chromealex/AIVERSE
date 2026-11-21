# PRISMA Stage 61 — reactive transparency-aware MotionBlur and safe FXAA

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
Base: feature/universal-entity-inspector-fps
Previous handoff: docs/handoffs/prisma-stage-60.md
Pre-handoff branch HEAD: 0b10bb77ad0d2b5a6a826a4ae5c37a4033b34944
Main unchanged. Never merge to main without explicit user authorization.
User development sequence: finish actual functional work first; native build,
CTest, GLSL/MSL/SPIR-V/GPU visual and profiling tests later.

## Scope and completed functionality

This bounded stage closes two concrete runtime gaps:
1. SDR FXAA could replace the postprocess source with an invalid kAa
   render target even when that target/depth allocation failed.
   FXAA now preserves the previously valid source on those failures.
   HDR output FXAA is also not allowed to process an unrendered
   composite target after composite pipeline failure.
2. HDR shutter MotionBlur previously sampled neighbor colors even
   across strongly different current-frame reactive (transparency/
   particle) coverage. A new opt-in setting rejects mismatched
   reactive neighbor taps with a soft weight. MotionBlur now preserves
   its incoming color alpha rather than unconditionally writing 1.

No new GPU histories, shader texture slots, authoritative ECS state,
or separate SpectraFX postprocess owner were introduced. Existing
effect schemas remain compatible because the only catalog field
was appended and defaults to zero.

## Actual source commits with exact SHAs

1. `53c7b41a9dc292577beebd978bbf3c7176c9b2e9` —
   `engine/render/Prisma.cpp`: SDR FXAA retains `fxaaInput`,
   allocates separate `fxaaOutput`, verifies source, output and
   depth before attempting graph/direct draws, and only replaces
   `src` if an FXAA pass was successfully encoded.
2. `8b27debc95b2adefeef09c192babbcd2c755d663` —
   `engine/render/PrismaData.cpp`: append MotionBlur
   `reactiveTapRejection` (range 0..1, default 0) after
   all existing MotionBlur parameters, retaining old
   profile/volume flattened numeric offsets and JSON behavior.
3. `a2e5e9a57fcf9d108a725ae3edabd783a5b9b293` —
   `engine/render/Prisma.cpp`: add `tapInfo` as fifth vec4
   only in MotionBlurU/ShaderDesc's generated uniform fields,
   and pass authored strength only if the current-frame reactive
   mask passes the existing `MotionTextureInput` metadata and
   available RHI size/format validation.
4. `3736bb7db11912d7b6fa11327a3c39e7bc15815c` —
   `engine/render/PrismaShaders.h`: reactive center-vs-tap
   coverage mismatch downweights neighbor color sampling,
   combined with existing depth rejection and center reactive
   blur suppression. Keep existing baseline when strength=0;
   preserve source `centerColor.a` on HDR blur output.
5. `7de0542af94267aa8bcb1e21c6f78cfd352cb124` —
   `engine/render/Prisma.h`: add GPU-free per-stable-view
   `reactiveInputSupplied`, `reactiveInputAccepted`, and
   `motionBlurReactiveEdgesEnabled` diagnostic flags.
6. `f59ba822436724c8b3eabddf127d0606f7a37e48` —
   `Prisma.cpp`: reset reactive report every view resolve,
   capture supplied/validated mask metadata before borrowed
   current-frame inputs are cleared; set tap-filter status
   only after an encoded MotionBlur pass. Prevent HDR final
   FXAA pass if the preceding composite pipeline is invalid.
7. `24003e4501d595e2be18f58a600461d27e26f109` —
   `apps/editor/windows/PrismaWindow.cpp`: show reactive
   mask metadata acceptance and whether transparent edge
   rejection was encoded for each camera; add authoring help
   for the new opt-in MotionBlur field.
8. `cf2f7210a941e0bb1113da0ddcbdf468f0551860` —
   `Prisma.cpp`: clarify the guarded SDR FXAA code branch
   and graph/direct fallback indentation, no semantic changes.
9. `8b0200635c2661011f468ac73fdbb202fa6e96de` —
   `docs/prisma.md`: full Stage 61 API, shader behavior,
   input ownership, fallback semantics and verification notes.
10. `0b10bb77ad0d2b5a6a826a4ae5c37a4033b34944` —
    `skills/visuals/aiverse-prisma/SKILL.md`: agent continuation
    contract and restrictions.

This Stage 61 handoff is committed separately AFTER the source,
design document and skill, per AGENTS.md requirements. Read latest
PR branch HEAD before editing the next stage.

## Architecture and API / actual rendering order

### Current-frame reactive inputs

`Renderer3D::resolvePrisma` encodes object/reactive overlays
before calling `Prisma::resolve`. Other providers (particles,
water or a native game) can supply a BORROWED current-frame
`MotionTextureInput` for reactive coverage using existing
`PostProcess::acceptMotionReactiveMask`, or
`Prisma::setMotionReactiveMask(stableViewId, input)`.
Prisma never owns or destroys that borrowed producer texture.
The source must be RGBA8 and match the INTERNAL scene dimensions.
A declaration is verified against available RHI `textureInfo`,
but where legacy handles lack metadata and the RHI cannot
describe them, the existing permissive compatibility contract
remains. Do not treat `reactiveInputAccepted` as GPU pixel proof.

`PrismaType::MotionBlur` gets an additional
`reactiveTapRejection` authored number, default zero.
In `MotionBlurU.tapInfo.x` it is zero when no valid mask
was accepted, and otherwise the authored value. The
kMotionBlur shader reads current and neighboring TEX3
reactive coverage R. Each depth-compatible color tap gets
an additional multiplicative weight:
```text
mismatch = abs(tapReactive - centerReactive)
weight *= 1 - rejection * smoothstep(0.1,0.8,mismatch)
```
This is an optional neighborhood transparency-boundary guard,
not temporal history rejection. Existing `reactiveStrength`
still suppresses the final shutter blur at the CENTER pixel;
the two controls have independent semantics. The original
depth metric (device or optional linear-depth mode), velocity
dilation, sample count, HDR RGB and sharp zero-weight fallback
are retained. Output alpha is now taken from the current source
pixel; the separate canonical TemporalResolve continues to write
hardware depth into its OWN history alpha, not from MotionBlur.

### SDR FXAA and HDR/TAA cooperation

SDR FXAA is a prepass before later Bloom/Composite. It keeps
`fxaaInput=src`, allocates/checks `fxaaOutput` independently,
checks the matching depth attachment and never assigns invalid
`src`. It attempts the opt-in graph executor only if the
attachments exist; its existing direct FXAA draw fallback is
kept. Only after an actual encoded FXAA graph/direct draw does
`src=fxaaOutput` become visible to later effects. A failed
buffer allocation skips FXAA for that frame and composes the
original previous source without GPU waits.

HDR final output FXAA still runs only when canonical
TemporalResolve was NOT successfully encoded. Stage 61
additionally requires `pipe.valid()` before allocating the
intermediate final-AA target, so a missing composite cannot
produce an FXAA pass over unwritten pixels. The existing
color/depth target validation remains. Both avoid duplicating
canonical TAA. HUD/UI remains outside PRISMA's postprocess.

### Diagnostics

`Prisma::ViewEffectReport` is still keyed by stable
presentation `viewId` and is updated only after actual
`Prisma::resolve`. The new flags distinguish whether a
reactive handle was supplied, whether metadata/RHI checks
accepted it, and whether the opt-in MotionBlur tap filter
was actually encoded. Only the latter implies a matching
MotionBlur draw command was issued; none means GPU completed
or visual output was validated. The editor displays them
without GPU queries or shader allocations.

## Actually performed verification

- Read current PR branch SHA and Stage 60 handoff first.
- Inspected current MotionBlur and TemporalResolve GLSL/MSL
  shared shader body, `MotionTextureInput` declaration,
  Renderer3D reactive overlay producer, FXAA graph/direct
  branch, final composite/HDR AA and per-camera diagnostics.
- Fetched and verified the source content of
  `engine/render/PrismaData.cpp`,
  `engine/render/PrismaShaders.h`,
  `engine/render/Prisma.cpp`,
  `engine/render/Prisma.h`,
  `apps/editor/windows/PrismaWindow.cpp`,
  and `engine/render/PrismaMotionTextureInput.h`
  after writes; **40/40 static source-presence,
  call-order and ownership checks passed**.
- These checks are source-only and did NOT compile
  any C++, shaders or launch native rendering.

**NOT RUN** per user's features-first workflow:
CMake/clang/C++/Obj-C++ builds, GLSL/MSL/SPIR-V
compiler, `./run-tests.sh`, CTest, editor/client launch,
Metal/GL/GLES/Vulkan/WebGL GPU execution, image/parity
or HDR alpha validation, first-frame/stale-reactive
visual tests, GPU fence overlap, performance/profiling,
CI. Do not claim unperformed tests passed.

## Remaining risks and concrete next Stage 62

- Alpha preservation changes the MotionBlur intermediate's
  alpha behavior: test effect-chain invariants on actual GPUs
  before calling it production-safe. External transparencies
  without correct reactive coverage still cause halos.
- The optional reactive rejection is a simple coverage
  mismatch heuristic, not a full opaque/translucent layer
  separation or a per-material transparency sort.
- Metadata-absent legacy reactive handles may be accepted
  when RHI texture info is unavailable. Encourage typed
  `MotionTextureInput::declared` from native hosts.
- The FXAA graph executor and HDR fallback need real
  shader compilation, runtime and image tests; source
  checks do not prove backend success.
- Suggested Stage 62 bounded functionality: review the
  remaining first-frame/camera-cut reactive producer
  behavior and quality/temporal edges, especially correct
  reactive mask generation when motion history is invalid.
  Preserve cross-view isolation and GPU-fenced retirement.
  Consider an opt-in camera-cut-safe reactive coverage pass
  independent of previous motion matrices if needed.
- Once ALL requested features are complete, begin a separate
  native build/CTest, backend image/parity, performance,
  GPU-fence stress and fixes phase, documenting real
  commands and PASS/FAIL.
- For any new source stage, create a separate
  `docs/handoffs/prisma-stage-XX.md`, with exact commits,
  actual tests vs NOT RUN, current branch, and next step.
  PR #26 stays Draft; no merge into main or PR base
  without the user's explicit approval.

## Mac branch fetch and pull

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If the local checkout contains a rebase or conflicts,
do not reset or discard another agent's changes;
use a separate clean worktree based on
`origin/feature/prisma-postfx`.
