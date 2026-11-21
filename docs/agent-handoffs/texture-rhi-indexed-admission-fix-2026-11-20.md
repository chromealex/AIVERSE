# Indexed VT ICB admission fix — 2026-11-20

Branch `codex/texture-completion`, implementation
`b17dba1a02066fd23d9764a5e07fbc16b9e27e63`; previous test gates
`a6d32f7b8eb02302eb0242ba656c1c7b52defe8b`, failure handoff `95692781d`.
See `texture-rhi-final-gates-2026-11-20.md` for the reproduced failure.

## Fix and compatibility

`engine/render/Renderer3DModels.cpp::prepareGpuModelDraws` resolves each
candidate VT binding once, removes indexed bindings before preflight/cache
construction, and counts only accepted dynamic/LOD/total candidates.
The legacy per-command ICB ABI carries 16 bytes of VT parameters; indexed
sampling needs 48 bytes and has no ICB shader permutation. These draws now
use the ordinary renderer without a doomed backend submission each frame.
The strengthened real Renderer3D regression also requires zero eligible
GPU items. Legacy VT/PBR/graph ICB remains enabled only by existing opt-ins;
no default flag, public API, shader ABI, save/network or ECS state changes.
Docs and Texture RHI skill describe this supported fallback.

## Actual targeted validation

All builds, reports, temp files and module/Python caches are on KINGSTON.
Build root: `/Volumes/KINGSTON/AIVERSE-builds/texture-completion/Debug`.
Source: `/Users/aleksandrfeer/Projects/AIVERSE/.worktrees/texture-completion`.
Environment: `TMPDIR=<build>/tmp`,
`CLANG_MODULE_CACHE_PATH=<build>/module-cache`,
`PYTHONPYCACHEPREFIX=<build>/python-cache`.

- Full Debug default-target build including editor and all registered test
  binaries PASS (`build-full.log`). Existing warnings: EXR test unused u64,
  SpectraGraphWindow unused ed/validId. No compiler errors.
- Full incremental rebuild after this fix PASS (`build-fix.log`), no warnings.
- `bash run-tests.sh --no-build --build-dir <build> --filter
  '^metal_renderer_(indexed_vt|graph_vt_gutter)_gpu$' --ctest-jobs 1`: 2/2 PASS,
  3.01 s; report `test-reports/20261120-143651-94146`.
  Legacy: actual graph/mixed VT ICB commands=2, errors=0 at every probe.
  Indexed: eligible=0 asserted; commands=0, errors=0 at every probe.
  Both have exact direct-vs-opt-in image parity and independent scene oracle.
- `git diff --check`: PASS.
- Complete registered regression launched with `bash run-tests.sh --quick
  --no-build --build-dir <build> --ctest-jobs 4`; report
  `test-reports/20261120-143715-97790`, aggregate `final-regression.log`.
  RUNNING at this checkpoint (291/300 passed; no failures). The --quick flag
  skips unrelated extra shell suites, not any registered CTest. Eight Metal
  tests are RUN_SERIAL; generated CTest properties inspected individually.

External CI for previous PR #95 remains queued:
https://github.com/chromealex/AIVERSE/actions/runs/44752147600
https://github.com/chromealex/AIVERSE/actions/runs/44752147644
No GL/GLES/Vulkan runtime PASS or performance claim. The local Docker daemon
is unavailable; native shader compilers passed in the prior gate stage.

## Next action

Wait for the already running full regression, inspect failures/skips and
append its exact result before merging the draft follow-up PR. Do not run
a competing full regression. Then merge ordinarily (user authorized main),
verify merge SHA and inspect focused CI. Original draft #21 head
`9145e6efe72a31306f7197f32f080e29908beb3b` is already an ancestor of this
main-based branch; it can be closed as superseded after the final merge.

## Final registered regression — PASS, 2026-11-20 14:53 UTC

This supersedes the RUNNING checkpoint above. Tested runtime code remains
`b17dba1a02066fd23d9764a5e07fbc16b9e27e63`; later changes are handoff-only.
PR: https://github.com/chromealex/AIVERSE/pull/96 (main target).

Actual command, from the source worktree with the external cache/temp
environment recorded above:

```
bash run-tests.sh --quick --no-build --build-dir /Volumes/KINGSTON/AIVERSE-builds/texture-completion/Debug --ctest-jobs 4
```

PASS: **300/300 CTests, 0 failures, 0 skipped, 0 disabled**, 949.78 s wall
clock. Main `aiverse_tests` PASS 864.31 s; isolated native library selections
PASS 264.32 s. All eight Texture RHI Metal GPU tests PASS, serially scheduled
(57.25 s sum of test durations, NOT GPU execution time). Other registered
engine/editor/FPS/RTS/SpectraFX/PHYSIS/streaming/shader/compiler tests also
passed. The extra ECS shell/sanitizer matrix outside CTest was NOT RUN;
this claim is exactly the complete registered CTest suite, not every optional
platform/toolchain or benchmark.

Canonical Metal samples: 136 bilinear + 18 indexed-color + 1170 nonuniform
indexed/LRU + 6 physical-mip = **1330 probes**. Nonuniform and original
bilinear maximum channel delta = 1; 18 LRU transitions verified. Indexed
Renderer3D: zero eligible ICB items asserted, commands=0/errors=0 at all five
UV probes, exact direct/opt-in image parity and independent scene oracle.
Legacy graph/VT ICB still executes actual commands with correct pixels.

Evidence root remains on KINGSTON. SHA-256:

- `test-reports/20261120-143715-97790/ctest.xml`:
  `0704ee18ec32cbe827e6ea13adfd6f82b881d481e2ae44b8cdd62a71e5f4f0eb`
- `build-full.log`:
  `bb665311dea22fe23dc1ada01f80ad6d51aad3ee805494aa82b6b04303f7442a`
- `build-fix.log`:
  `eed6a90b85bfb4485eb9fa060f4445482372c41fcd0355adf3590bbbe3161fdf`
- `final-regression.log`:
  `b28fbb96dda5e1cf3cb53ffe9125fe195ace73bcde9c78d9699457ee12221b4b`

Final `git diff --check` and clean code worktree inspection passed before
this documentation append. No runtime source changed after the full run.

## External completion boundary

Focused CI for PR #96 was observed queued, not green:
https://github.com/chromealex/AIVERSE/actions/runs/44756153112
Actual GL/GLES/Vulkan runtime parity still requires those runners/devices;
local GLSL/GLES/SPIR-V compilation is not a substitute. No performance/FPS
claim and no enabling experimental ICB defaults. The supported indexed
material path is ordinary rendering, now without failed ICB attempts.

Next external action: inspect the focused workflow when runners execute;
fix any actual platform failure in a new bounded stage. Per AGENTS.md §8,
the MAIN integrator must republish Windows/macOS packages from the final
verified main commit after template-match gates. This texture feature agent
has not published or force-pushed the builds branch. Preserve the external
evidence while these acceptance/release follow-ups remain open.
