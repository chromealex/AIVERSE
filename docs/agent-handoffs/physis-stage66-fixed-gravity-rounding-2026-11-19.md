# PHYSIS Stage 66 — macOS full-suite fixed-point forces test expectation

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `feature/physis-nature-simulation`
PR: https://github.com/chromealex/AIVERSE/pull/25 (DRAFT)
Previous handoff: `docs/agent-handoffs/physis-stage65-macos-integration-fixes-2026-11-19.md`
Implementation commit: `2dda8ccfa242b14b327a01a0cd7ea7ca16ea4d90`
Documentation commit: inspect `git log -1 -- docs/agent-handoffs/physis-stage66-fixed-gravity-rounding-2026-11-19.md`.

## Evidence — actual user-provided full native macOS run, PRE-FIX

Uploaded files:
- `build(5).log`: all selected PHYSIS headless targets built; Box3D and `aiverse_physis_box3d_test` built without errors.
- `configure(6).log`: AppleClang 17.0.0.17000604, Darwin x86_64, configure and generation successful.
- `ctest(4).log`: 23/24 passed, 1 failure:
  `physis_forces` (#109), line 34:
  `sampled.gravityForce.y == Fixed::fromRatio(-981,10)`.
  `physis_box3d`, `physis_wind`, `physis_gravity`,
  `physis_storm`, editor view, network/admin journal,
  field assets and integration tests all PASSED.
- `summary(6).txt`: mode=full,
  source commit `3a1c1536b904dcdc6174e2054ab6ed97ea20e0a2`,
  PASS configure, PASS build, FAIL ctest exit 8.
  Total CTest time: 10.65 seconds.

## Root cause

`Fixed` uses signed Q47.16 integer arithmetic with
`Fixed::fromRatio` rounding to nearest. `GravitySettings::baseline`
is -9.81 m/s² and the test ForceReceiver mass is 10 kg.

- `Fixed::fromRatio(-981,100).raw == -642908`
- `(Fixed::fromRatio(-981,100) * Fixed::fromInt(10)).raw == -6429080`
- `Fixed::fromRatio(-981,10).raw == -6429082`

There is a two-raw-tick (~0.0000305 Newton) difference because
quantization is applied BEFORE the product, versus after it.
`PhysisForces::evaluateForces` computes exactly the documented policy
`F = quantizedMass * quantizedAcceleration * gravityScale`.
This is not evidence of incorrect gravity, overflow, nondeterminism
or Box3D coupling. Do not change engine force physics to force the
two independent quantization routes to match.

## Change and resulting checks

`tests/physis_forces_test.cpp` only:
- Assert sampled gravity acceleration exactly equals the authored
  `gravity.baseline.y`.
- Assert gravity force exactly equals baseline times receiver mass
  on this default gravityScale=1 fixture.
- Preserve a sanity physical-unit comparison with the ideal -98.1 N
  but permit at most `Fixed::fromRaw(2)` error.

Source implementation commit: `2dda8ccfa242b14b327a01a0cd7ea7ca16ea4d90`.
Docs/instructions updated in `engine/physis/AGENTS.md`,
`skills/world/aiverse-physis/SKILL.md`, and `docs/physis.md`.

## Verification status — distinguish before/after fix

- **ACTUAL native macOS compiler and CTest performed by user BEFORE fix**:
  build PASS, Box3D linked, 23/24 PHYSIS CTests PASS.
- The Q47.16 raw integer relation above was independently checked
  using the repository's `Fixed::fromRatio` formula.
- **NOT RUN after fix**: changed `physis_forces` native CTest,
  full `--full` 24/24 run, editor-enabled PHYSIS authoring fixture,
  complete repository-wide CTest, multi-platform GitHub CI.
  PR remains DRAFT. Do NOT claim all-green until test output exists.

## Next step

On the full macOS checkout:
```sh
cd ~/Projects/AIVERSE && \
git fetch origin && \
git switch feature/physis-nature-simulation && \
git pull --ff-only origin feature/physis-nature-simulation && \
bash tools/physis-validate-native.sh --full --jobs 4
```

If 24/24 native headless PHYSIS tests pass, record the report path and
commit and move to editor-enabled PHYSIS authoring tests, then CI and
security/transport QA. Fix any real compiler or test failures. Continue
mandatory per-stage handoffs. Keep engine generic and ECS-based, with
NEREID owning water, TerraWeave terrain/foliage, Box3D dynamics.
