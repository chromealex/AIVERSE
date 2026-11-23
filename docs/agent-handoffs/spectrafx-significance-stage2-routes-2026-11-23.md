# SpectraFX Stage 2 — authored cue significance (2026-11-23)

## Provenance and test status

- Repository: `chromealex/AIVERSE`; base main `87ba5b1439267bea86c570489e8869b3f7641a0e`.
- Branch: `feature/spectrafx-significance-data-20261123`.
- Implemented on top of Stage 1 `docs/agent-handoffs/spectrafx-significance-stage1-2026-11-23.md`.
- Stage 2 commits: `5db30365` routes parser, `2c713cd0` scene host, `5ab35fc8` contracts, `2595dba4` umbrella API, `86386afb` safe numeric validation.
- Native build, CTest, Metal/GLES/Vulkan and Studio GUI **NOT RUN in this session**. GitHub source review is not test execution. Do not mark this stage accepted without a clean build.

## Changed files

- `engine/fx/SpectraFxGpuRoutes.h`: optional top-level `cuePriority` for camera significance, per-route `importance` in [0.01,100]. Strict field/schema and finite bounds validation; old assets still parse and remain FIFO. All output parameters are committed only after complete successful validation.
- `engine/fx/SpectraFxSceneHost.h`: scene init reads policy and maps `eventType` to significance weight, preserving opt-in semantics. Host shutdown clears per-project importance weights to prevent leaked settings on project swap.
- `engine/fx/SpectraFX.h`: the native umbrella exposes `SpectraFxSignificance.h` and `SpectraFxCuePriority.h`.
- `tests/spectrafx_contract_test.cpp`: parser tests for legacy default, authored config, event weights and rejection of inverted near/far range without state mutation.

## Authoring example

Place in `fx/gpu/routes.json`:

```json
{
  "version": 1,
  "cuePriority": {
    "enabled": true,
    "nearDistance": 12,
    "farDistance": 250,
    "targetRadiusPixels": 24,
    "peripheralMultiplier": 0.25,
    "behindMultiplier": 0.05
  },
  "routes": [
    { "eventType": 42, "asset": "sparks", "importance": 5.0 },
    { "eventType": 43, "asset": "ambient_dust", "importance": 0.1 }
  ]
}
```

The assets `sparks` and `ambient_dust` must actually exist in `fx/gpu/`. IDs 42/43 are *examples*, not known game event IDs. Omit `cuePriority` for historical FIFO behavior. `importance` is a relative ranking weight and **does not suppress an event** or bypass `visibleToViewer`.

## Acceptance commands / requirements

```sh
cmake -S . -B build/spectrafx-priority -DCMAKE_BUILD_TYPE=Debug
cmake --build build/spectrafx-priority -j8
ctest --test-dir build/spectrafx-priority -R 'spectrafx_contract|spectrafx_architecture' --output-on-failure
AIVERSE_ACCEPTANCE_REQUIRE_METAL=1 bash tools/spectrafx/acceptance.sh build/spectrafx-priority
```

Note: actual CMake target names and environment specifics must be verified on the checked-out branch, not inferred from this document. Inspect the existing test registrations. Compile on macOS with Metal; separately test Vulkan/GLES/WebGL where supported.

## Constraints, remaining work

- Priority applies to the scene host inbox, not direct `dispatch` or entity attachments; do not claim whole-system significance culling.
- Normal `SceneHost::init` calls `shutdown`; programmatic priority set before `init` is intentionally replaced by the project configuration. To override, set it *after* `init`.
- No HTML5 inspector UI has been added for these properties. The `feature/studio-html5` branch is under parallel development and must be integrated deliberately.
- Priority is an ordering policy, not a GPU load governor or scheduler for particle birth rate. Existing governor remains independent.
- Next step: run native acceptance, fix compilation/behavior if needed; extend acceptance to critical source event flows; then begin typed graph compiler and Studio authoring as separate stages. See `docs/spectrafx-modernization-roadmap-2026-11-23.md`.

## Required handoff discipline

After every follow-up stage write a separate dated file in `docs/agent-handoffs/` with files, API, exact current branch/SHA, tests **actually executed**, known gaps, and explicit next work. Never overwrite previous stage evidence with newer unverified assumptions.
