# PRISMA Stage 52 — HDR bounce / volumetric composition without legacy TAA

Date: 2026-11-19
Repository: chromealex/AIVERSE
Branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
PR base: feature/universal-entity-inspector-fps
Previous handoff: docs/handoffs/prisma-stage-51.md
Pre-handoff branch tip after code/docs: 884790a070580294d688cd845f5adcf8373e9779
Main untouched. Do NOT merge without explicit user authorization.
Development order: functional features first, native CMake/CTest/GPU/tests only after functionality is closed.

## Completed source changes and exact commits

1. `70998b085258828a0f5e147a5332b341e4819cf7` — `engine/render/PrismaScreenLighting.h`: separate `legacyTemporal_` from `running_`; public `lightingOnly()` for the HDR lighting-only path; `temporalShadowResolve()` now truthfully signals only active legacy history; `passCount()` counts lighting shader(s), mandatory final copy, and two legacy TAA passes according to mode; arrays expanded to five shaders/pipelines.
2. `59ebf0aae8a31581812031d84c2c7f36fab72fc9` — `engine/render/PrismaScreenLightingShaders.h`: keep the historical TAA copy that replaces depth-in-alpha with opaque alpha; add `copyLinear` to preserve source HDR SceneColor alpha in non-temporal lighting composition.
3. `8b633135acb7489b7cc29398088430320e0f6f5f` — `engine/render/PrismaScreenLighting.cpp`: canonical-vs-legacy ownership gate; retain diffuse bounce + shadowed volumetric contributions when canonical Temporal Resolve is requested, render through ping-pong HDR targets and composite back to the scene before `Prisma::resolve`; never run the wrapper's second TAA, copy from legacy history or apply its own jitter in canonical mode. Legacy mode still executes history temporal shader and legacy copy. Target images are allocated on demand: no wrapper history pair or private depth attachment is created for a canonical-only session. A mode switch does not synchronously delete old allocations. Remove artificial depth attachment for lighting-only fullscreen draws; depth remains sampled through the actual scene depth texture. Bind a valid non-target texture in the unused history sampler slot for lighting-only passes.
4. `6379343baa0e6db8ae4aa7ee68271fdd79ae54e0` — `docs/prisma.md`: document Stage 52 dual-mode adapter, alpha, allocation, noise fallback, open limitations and explicit untested status.
5. `884790a070580294d688cd845f5adcf8373e9779` — `skills/visuals/aiverse-prisma/SKILL.md`: update agent ownership and integration contract; replace stale Stage 51 statement that canonical TAA disables all lighting.

This handoff is a distinct documentation commit on the same branch.

## Exact runtime contract

- `PrismaScreenLighting::jittered(camera,w,h)` delegates to `Prisma::jitteredCamera` if canonical HDR/High Temporal Resolve is authored/requested; clears the wrapper's legacy sequence/offset and never applies both jitters.
- `PrismaScreenLighting::scenePass` asks Prisma for the scene framebuffer first and checks actual `linearScene()` + High quality. If legacy temporal owns the frame, the adapter may run diffuse bounce, volumetric scattering, then temporal history + opaque-alpha copy. If canonical owns the frame, it may run bounce/scatter then an alpha-preserving HDR copy to SceneColor. Canonical Prisma owns final history, resolve, tonemapping, exposure and postfx thereafter. UI stays after Prisma.
- `PrismaScreenLighting::resolve` does not read or write wrapper `history_[0/1]` when in lighting-only mode. No dual TAA or dual jitter/velocity history is introduced. `temporalShadowResolve()` returns true only with active legacy temporal history so legacy PCSS tap rotation stays guarded. If no sun shadow / sun radiance exists, volumetric scatter is skipped for that frame.
- Volumetric sampling uses fixed per-pixel blue-noise-like hash phase while no legacy history is available. This prevents frame-animated noise from appearing unfiltered during canonical preflight fallback. It is not a temporal equivalence or denoiser.
- RHI allocations: canonical-only lighting allocates only bounce/scatter targets that are actually configured; legacy TAA additionally allocates history[2] and private depth. Allocated targets are kept across switches to avoid new in-flight mode-switch destruction. **Historical resize/shutdown `releaseTargets()` still synchronously destroys its textures** and needs separate verified GPU-fenced lifetime design before production.

No data-format migration, ECS/gameplay write, input change, SpectraFX duplication, feature-default enablement, external JSON `postfx` rename, or editor saved-ID change.

## Source review actually performed

- Pulled current branch HEAD and fetched the current branch `PrismaScreenLighting.h`, `PrismaScreenLighting.cpp`, `PrismaScreenLightingShaders.h`, render hook `Renderer3D.h`, adapter shader body, Prisma capabilities and docs from the connected GitHub repository.
- After updating code, re-read all three source files through GitHub and checked 15/15 source-presence/call-order invariants: requested temporal precedence, bounce/scatter contributions, direct final HDR copy, preservation of scene alpha, legacy copy semantics, branch-separated history allocations/writes, no target/history alias in direct paths, conditional depth attachment, jitter delegation, deterministic noise fallback, final Prisma resolve, truthful shadow temporal flag, shader-array/loop parity and pass accounting. **These are static checks only.**
- `clang`/C++ or Obj-C++ compilation: NOT RUN (no native AIVERSE checkout in the agent environment; user requested features-first work).
- CMake configure/build, CTest, render GPU smoke, GLSL/MSL compiler, `./run-tests.sh`, editor/client launches, GPU fences, memory/perf counters, HDR screenshots/pixel parity, and CI: **NOT RUN / NOT VERIFIED**. Never describe the Stage 52 shaders or pixel output as tested.
- Source-level known risk: real Metal/GL render-pass compatibility of lighting-only passes without a private depth attachment, correctness of volumetric sampling under a jittered canonical projection and alpha preservation need actual GPU confirmation. Existing `PrismaScreenLighting` legacy resize destruction still needs GPU-fenced retirement; lifetime wasn't rewritten in Stage 52.

## Next *functional* Stage 53 (before any native tests)

1. Add a GPU-free **per-effect execution/fallback report** (per stable view where possible): authored/requested, high-tier supported, prepared pipelines, actually encoded last resolve, and why an effect fell back. Do not confuse a shader handle with GPU-verified output. Extend editor PRISMA panel to display status without generating shader programs or allocating resources merely to inspect.
2. Extend host-independent custom camera stable-ID registration and cleanup to include `Renderer3D` object-motion targets, using actual GPU-issued submission/completion fences. Do not retire a size cache referenced by a different active view, or borrow a screenshot's camera ID.
3. Investigate a deferred GPU-fenced cleanup for historical `PrismaScreenLighting` targets on resize/shutdown; avoid destroying attachments referenced by in-flight command buffers. Preserve the single-owner history rule.
4. Review remaining postprocess feature/catalog/editor UX gaps. No unverified vendor FSR/TSR/DLSS claims. Once the functional backlog is closed, perform a dedicated native build/CTest, Metal/GL/GLES/Vulkan smoke, parity/profiling and fixes stage, with exact results in its own handoff.
5. Commit each bounded stage plus a **separate** `docs/handoffs/prisma-stage-XX.md`; preserve other agents' work and keep PR #26 Draft until requested merge.

## Mac continuation including remote branch pull

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If the local main worktree is rebasing/conflicted, do not run `git switch` there; instead use a clean worktree based on `origin/feature/prisma-postfx` and resolve the old conflict separately. Never force-push or reset another agent's work.
