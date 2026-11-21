# PRISMA Stage 65 — 19-effect source closure and finite native/JSON authoring

Date: 2026-11-19
Repository: chromealex/AIVERSE
Working branch: feature/prisma-postfx
Draft PR: https://github.com/chromealex/AIVERSE/pull/26
PR base: feature/universal-entity-inspector-fps
Previous handoff: docs/handoffs/prisma-stage-64.md
Pre-handoff verified branch HEAD: d3f9f1974096dbb0cf135fad64dc355a081be82e
Main: UNCHANGED. No merge to main/base without explicit user approval.
User sequencing: implement ALL bounded feature requirements first; then ONE
consolidated native CMake, CTest, GPU rendering, shader, visual/fence/profiling
validation and subsequent fixes with actual command logs.

## 1. Completed source/API feature review

Stage 65 audited the current PRISMA feature inventory, Native C++/ECS
authoring and Scene/Game viewport wiring, rather than expanding the
postprocessor with unrelated new effects. The full result is
`docs/prisma-feature-closure-stage-65.md` and must be read before
running final verification.

- The `PrismaType` enum has 19 effects. The source `buildCatalog`
  contains exactly 19. Each type has an explicit
  `prismaEffectWork` cost branch, a `Prisma::resolve`
  runtime mapping and a corresponding per-effect
  `EffectExecution` status branch: **19/19** matched.
  This only proves source coverage, not GPU behavior.
- Editor Scene (viewId 1) and Game (viewId 2) and native
  GameClient (viewId 3) call the canonical PRISMA volume,
  per-view render resolution, post-scene resolve and GPU-fence
  retirement APIs. HUD is rendered AFTER PRISMA.
- Volumes, shader graph, LUT, temporal resolve, DoF,
  cinematic manual/frozen/ROI exposure, motion-reactive
  coverage, Actor Reveal and Stage-64 alpha-cutout are
  present in their already assigned owners.
- No new postprocess features added in this phase. Catalog
  is source/API scoped for validation, not production-ready.

## 2. Actual bounded source bug fixes: invalid numeric input

The original non-strict/strict `parsePrismaEffect` did this for
Number and Color: cast `double` to `float`, then
`std::clamp(float,lo,hi)`. This is unsafe for NaN/Inf
and huge finite numbers exceeding float range. For Color triplets,
it also called `v[i].toDouble()` on values that were not
actually numeric. Native C++ `PrismaEffect.values` can bypass
JSON entirely, and partial native `PrismaVolumeStack` interpolation
could feed malformed values to GPU uniform creation.

These issues are now addressed with a single shared catalog policy:

1. **`86af877bf3e7c697f960c46d114cea8f9ec83c6`** —
   `engine/render/PrismaData.cpp`: strict JSON numeric
   validation, finite guard, `std::clamp` in double
   precision before float conversion. Invalid numeric
   parameters leave their catalogue defaults in non-strict
   mode and append to `issues`; strict MCP validation throws.
   Invalid RGB arrays preserve the ENTIRE default triple,
   not a half-authored mix. No shader/GPU changes.
2. **`a066723e4393f8eb80118b9516d8e3e2cd1ca136`** —
   `engine/render/PrismaData.h`: public GPU-free
   `void sanitizePrismaSettings(PrismaSettings&)`
   declaration for native game/ECS profiles. Also
   replace obsolete fixed-14-effects header comments
   with the actual full PRISMA pipeline description.
3. **`d446ac7d49f9b11bcc43d9b3fc604fa0ca267874`** —
   `engine/render/PrismaData.cpp`: sanitizer definition
   based on `prismaCatalog()`. If a native effect's
   flattened `values` length is too short or too long,
   align to current `defaultPrismaEffect` length;
   replace nonfinite floats with parameter defaults;
   clamp Color/Number to defined ranges; normalize
   Bool to 0 or 1. Text occupies no numeric index.
   GPU-free and genre-independent.
4. **`773b1bdc0cdf348e9b0e6ce69538c2f848e068eb`** —
   `engine/render/PrismaVolumes.h`: normalize the
   FINAL native camera-volume-interpolated profile
   before returning it. The underlying influence
   already checked nonfinite camera/center/radius/
   weight/blend distance.
5. **`a94a4737e9ee552708fc3d37116f4fddd3550403`** —
   `engine/render/Prisma.cpp`: sanitize base native
   `Prisma::setSettings` before preserving it and
   before GPU presentation. When JSON is the source,
   the same validation runs in parse and remains
   compatible for valid data.

All previously valid authoring and serialized JSON/MCP
parameter names, type IDs and effect order are retained.
No ECS simulation state, network protocol, shader macro,
render pass or GPU sampler allocation changes.

## 3. Documentation/feature-closure commits

6. **`34c692d4e6eeba86fe0e26438bcf2638ff34f387`** —
   `docs/prisma-feature-closure-stage-65.md`: an exact
   19-effect source coverage table, host/native ownership
   and suggested ONE final validation run with
   known CMake options, registered CTest targets and
   GPU parity/fence cases. All results labeled NOT RUN.
7. **`5cd50bd41105c090030053f51f8b3162e2100999`** —
   `docs/prisma.md`: Stage 65 numeric policy and
   source-level feature closure without claiming
   runtime success.
8. **`d3f9f1974096dbb0cf135fad64dc355a081be82e`** —
   `skills/visuals/aiverse-prisma/SKILL.md`: continuation
   contract for native sanitizer, catalog ownership,
   readiness and verified vs unverified tests.

THIS HANDOFF is independently committed after code,
system documentation and skill. The definitive post-handoff
branch SHA is reported in the GitHub response/assistant's
final handoff notice.

## 4. Actually performed checks (NOT a compiler)

- Read Stage 64 handoff and confirmed GitHub branch/PR
  remains Draft on the expected base.
- Inspected the complete 19-effect GPU-free catalog and
  `PrismaType` enum, per-effect runtime dispatch/status,
  `prismaEffectWork`, Scene/Game and GameClient
  host wiring/lifecycle, CMake test target registrations,
  volume resolver, JSON and native authoring paths.
- **19/19** catalog types have cost/runtime/report source mappings.
- **36/36** static source/host/test registration assertions
  passed for source completeness and sanitizer behavior.
- Fetched all changed source/manifest/skill files from GitHub
  after commits and checked 19/19 additional final
  source-presence/readback contracts.
- NO CMake, compiler, GPU device, shader compiler, CTest,
  viewer, editor or native runtime was launched. These
  are static text/structural checks only.

**NOT RUN:** CMake configuration, native C++/Obj-C++ build,
GLSL/MSL/SPIR-V shaders, CTest or `./run-tests.sh`,
macOS Metal/GL or other platform GPU, pixel/alpha/
HDR/temporal visual checks, GPU-fence stress, production
performance, memory profiling and CI. None is PASS.

## 5. Next Stage 66 — one consolidated actual verification

User specifically requested features before tests and prefers
ALL tests in ONE consolidated validation cycle.

Proposed exact source-backed Mac commands are in
`docs/prisma-feature-closure-stage-65.md`:

```bash
cd ~/Projects/AIVERSE
git fetch origin
git worktree add ../AIVERSE-prisma-validation origin/feature/prisma-postfx
cd ../AIVERSE-prisma-validation
cmake -S . -B build/prisma-final \
  -DAIVERSE_BUILD_TESTS=ON \
  -DAIVERSE_BUILD_HDR_TESTS=ON \
  -DAIVERSE_BUILD_EDITOR=ON
cmake --build build/prisma-final --parallel 4
ctest --test-dir build/prisma-final --output-on-failure -j 4
```

The CMake code ACTUALLY registers `prisma_graph`,
`prisma_volume`, `shader_graph`, `aiverse_tests`
and optional `hdr_policy`; shader/GPU scenes also
need explicit execution/pixel capture where not
registered with CTest. Do not invent results from
these future commands. Stage 66 agent must record
actual diagnostics, shader failures, CTest numbers,
GPU screenshots/pixels, real PASS/FAIL, fix all
blocking regressions and repeat the consolidated
test cycle as required.

High-priority cases: Stage-55 ShaderGraph four textures
+ AutoExposure TEX9 vs graphless TEX8; failed LUT/graph
fallback preserving Bloom/Grade; manual/frozen/ROI
exposure across cuts; metric DoF perspective/ortho;
MotionBlur alpha/reactive boundaries; Stage-64
animated skin/morph/baked alpha/dissolve/aperture;
first frame / camera cut / resize reactive coverage;
two simultaneous camera IDs and GPU-fence retirement;
GPU memory/quality/perf across available backends.

Do not merge PR #26 into base or main without explicit
user permission. On any additional source change, write
a new distinct Stage-66 handoff with actual commands,
changed files and remaining errors.

## Safe Mac pull for feature branch

```bash
cd ~/Projects/AIVERSE
git status --short
git fetch origin
git switch feature/prisma-postfx
git pull --ff-only origin feature/prisma-postfx
git log -1 --oneline
```

If other agents have an in-progress rebase/conflict,
do not use reset or force push; create a clean dedicated
worktree from the remote feature branch instead.
