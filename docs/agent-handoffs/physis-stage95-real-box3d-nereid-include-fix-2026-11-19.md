# PHYSIS Stage 95 — native combined Box3D test missing NEREID public include path

Date: 2026-11-19
Repo: `chromealex/AIVERSE`
Branch: `integration/physis-terraweave-complete-20261119`
PR: https://github.com/chromealex/AIVERSE/pull/40 (Draft)
Prior handoff: `docs/agent-handoffs/physis-stage94-combined-fall-integration-2026-11-19.md`
Source change: `a42a3f65d44510d8b0194c701b46b2b88dbcc07e`.
Handoff SHA: `git log -1 -- docs/agent-handoffs/physis-stage95-real-box3d-nereid-include-fix-2026-11-19.md`.

## Actual user-native build result (failure)

The user ran the final one-pass full desktop Debug build from
`~/Projects/AIVERSE-physis-terra-final`. The build advanced to 55% and
failed in the real `aiverse_physis_terraweave_fall_bridge_test` target.

Compiler include chain:

```text
tests/physis_terraweave_fall_bridge_test.cpp:4
engine/world/PhysisTerraWeaveFallBridge.h:7
engine/world/TerraWeaveEcsAuthority.h:7
engine/world/TerraWeaveEcsBankBridge.h:6
engine/world/TerraWeaveNereidSoilBridge.h:7:10:
fatal error: 'aiverselib/water/NereidErosion.h' file not found
```

The native build failed (`make: *** [all] Error 2`); **CTest was NOT RUN** on this combined source. Other unrelated targets reaching 61% are not evidence of completed whole-repo build. No Box3D physical runtime outcome has been verified.

## Root cause

The canonical TerraWeave authority header transitively includes the
NEREID erosion policy at `<aiverselib/water/NereidErosion.h>`.
The header exists in `libraries/include/aiverselib/water/NereidErosion.h`.
CMake's standalone PHYSIS/Terra/Box3D test target was linked to
`aiverse_engine aiverse_physics aiverse_models aiverse_zstd_dec`, which
do NOT expose `libraries/include` as a compile include directory.
The existing package `AIVERSE::lib_nereid_water`, defined by the
`libraries` package catalog, already exports this path.

## Exact committed fix

Changed ONLY `CMakeLists.txt` for the combined test:

```cmake
target_link_libraries(aiverse_physis_terraweave_fall_bridge_test PRIVATE
    aiverse_engine aiverse_physics aiverse_models aiverse_zstd_dec
    AIVERSE::lib_nereid_water)
```

This is a per-target public package dependency, not a global include path or
copy of the NEREID solver. No ECS save schema, PHYSIS/TerraWeave ownership,
simulation, Box3D runtime, model, source algorithm, or test assertion changed.

## Verification status

- GitHub change committed and pushed.
- Static source check: `NereidErosion.h` really exists under
  `libraries/include/aiverselib/water/`, and `libraries/CMakeLists.txt`
  configures `AIVERSE::lib_nereid_water` as an INTERFACE target exposing
  `libraries/include`. Earlier targets in root CMake already link this alias.
- **NOT RUN** after the fix: local AppleClang compile, full Debug build,
  `physis_terraweave_fall_bridge` CTest, unfiltered suite, CI acceptance.
- Earlier PHYSIS-only 29/29 native PASS is on uncombined source and does
  not validate the new cross-system header.

## Next action — reuse the already configured build directory

```bash
git -C ~/Projects/AIVERSE fetch origin
cd ~/Projects/AIVERSE-physis-terra-final
git status --short
git switch --detach origin/integration/physis-terraweave-complete-20261119
bash tools/physis-terraweave-final-validation.sh
```

Do not switch if the worktree is dirty; inspect changes first.
The script reconfigures the same `build/physis-terraweave-onepass` directory,
reuses compiled targets and runs ONE full unfiltered CTest only after
the entire build succeeds. Preserve the first new compile error if present.
Do not loosen tests to obtain a false PASS, do not merge into main yet.

Current test responsibility remains: native ECS rollback, authoritative
Terra rooted.fallen transition, Box3D tree/shrub bodies, resimulation,
repair/replant, and mid-fall host-owned float pose snapshot, followed by
full AIVERSE CTest. CI is a separate acceptance signal; do not report
queued GitHub Actions as passed.
