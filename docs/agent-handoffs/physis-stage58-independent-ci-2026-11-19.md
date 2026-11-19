# PHYSIS Stage 58 — isolate native journal CI from engine workflow (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR #25 Draft: https://github.com/chromealex/AIVERSE/pull/25
Previous handoff: docs/agent-handoffs/physis-stage57-standalone-ci-2026-11-19.md
Code commit: 0d213e8f91a08b81eadd0d0f2eb847474444ad34
This handoff and agents documentation are committed together in the next documentation commit.

## Issue found

Stage 57 had added a standalone job inside the existing
`.github/workflows/physis.yml`, but the whole workflow still used
one concurrency group per ref with cancel-in-progress. Pushes to
PHYSIS continuously cancelled queued runs, including the independent
journal job. Thus the "standalone" job was independent of the engine's
CMake dependency graph but NOT independent of workflow cancellation.

## Changes and architecture

- `.github/workflows/physis-journal.yml` (NEW): separate workflow
  with its own `physis-journal-...` concurrency group, specific
  source/test/CMake/workflow triggers, a two-platform
  `ubuntu-latest,macos-latest` job matrix, strict isolated CMake
  configuration, C++ compilation, two CTests and log uploads.
  GitHub blob SHA: `01fb5150b0b66295167d7b34429d363fcfa12173`.
- `.github/workflows/physis.yml`: remove the standalone job and
  its standalone-CMake trigger. Retain the original full PHYSIS
  Ubuntu/macOS matrix, all PHYSIS test targets, CMake/CTest and logs,
  with its separate existing concurrency group.
  GitHub blob SHA: `986ba0bb4afec5c08aede52f41f3d8b99abf64c9`.
- `engine/physis/AGENTS.md`, `skills/world/aiverse-physis/SKILL.md`:
  document ownership and true CI verification status.
- No C++ runtime files, ECS schema, natural phenomena API, net
  protocol, Box3D, TerraWeave or NEREID ownership changed.
- Existing test CMake:
  `tests/physis_journal_standalone/CMakeLists.txt`.
  No test source implementation changed.

## Actual verification

- GitHub source API confirmed BOTH workflow files exactly after the
  atomic commit. The full workflow now has only `physis-native`;
  the new workflow has only `physis-journal-standalone`, with
  independent concurrency groups.
- GitHub Actions created TWO actual separate workflow runs for the
  same implementation SHA:
  `44495750669` PHYSIS journal standalone security checks,
  `44495750438` PHYSIS nature + Box3D integration checks.
  Both showed `status=queued` and no conclusion at inspection;
  these are NOT completed tests.
- Actual local native Linux result:
  ```sh
  cmake -S /mnt/data/physis54_smoke/tests/physis_journal_standalone \
      -B /mnt/data/physis54_smoke/build-staged -DCMAKE_BUILD_TYPE=Debug
  cmake --build /mnt/data/physis54_smoke/build-staged --parallel 2
  ctest --test-dir /mnt/data/physis54_smoke/build-staged --output-on-failure
  ```
  Output: `physis_journal_posix_io` Passed,
  `physis_journal_locked_io` Passed, **2/2, 0 failed**.
  This is a real CMake/CTest of the standalone project only.
- Full repository `git clone` still fails in local container:
  `Could not resolve host: github.com`.
- GitHub compare from main: PHYSIS branch was `behind_by=0`.
- GitHub API reported PR #25 `mergeable_state=dirty`, due to
  divergence with its target
  `feature/universal-entity-inspector-fps`. Do not merge that
  unrelated target automatically: user previously requested main
  only, and main is already included.

## Still NOT verified

- Real GitHub Actions runner build/result on Ubuntu and macOS for
  the separate journal workflow and the full PHYSIS workflow.
- Complete AIVERSE root CMake, engine integration, authenticated
  `physis_admin_journal`, `physis_remote_edit`,
  `physis_field_runtime` and all `^physis_` tests.
- Windows file handles, path security, real editor.
- The journal sidecar + World snapshot remain separate persistence
  domains, with no joint crash-atomic transaction.

## Next agent action

1. Inspect actual completed GitHub Actions run for the separate
   `physis-journal.yml` and full `physis.yml`. Gather CMake logs,
   CTest failures and platform differences. Queued runs are not
   proof of success.
2. Build full AIVERSE native PHYSIS targets (especially
   `aiverse_physis_admin_journal_test`) on Ubuntu/macOS; fix
   actual compiler/test errors before further security scope.
3. Verify existing startup/rollback/fail-closed PHYSIS tests with
   real ECS and WebSocket, then all full-engine `^physis_` tests.
4. Discuss PR #25 dirty target-base with user before integrating
   unrelated feature branch or retargeting to main.
5. Record exact HEAD, changed source files and ACTUALLY executed tests
   in another per-stage handoff. Keep PR Draft until native QA is green.
