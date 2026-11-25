# GitHub Actions queue concurrency — 2026-11-25

Branch: `fix/ci-queue-concurrency-20261125`, based on `origin/main` `8511f3094`.
Owner area: platforms / CI. Scope: workflow scheduling only.

## Delivery record before implementation

- Problem: one PR synchronize event starts many path-matched workflows. Six PR workflows have no run-level concurrency, so successive commits accumulate queued runs. Live GitHub Actions inspection on 2026-11-25 found more than 1,800 queued or pending non-Pages runs across the repository; one Pages deployment was preserved during cleanup.
- Intended change: add one run-level concurrency group per workflow and PR head branch to `ecs-experiments.yml`, `enhanced-input.yml`, `spectrafx-native-preview.yml`, `spectrafx-significance.yml`, `spectrafx-studio-ui.yml`, and `studio-validation.yml`. Use `cancel-in-progress: true`. Keep Pages publishing and test commands unchanged.
- Acceptance: each edited YAML parses; all six groups have the expected expression and cancellation flag; distinct workflows and branches do not share a group; the existing CI workflow remains unchanged. Live GitHub cancellation behavior requires a subsequent PR push and is NOT RUN at this checkpoint.
- Feature Gate criteria: implementation, architecture, dependencies, tests, handoff, documentation apply. Native API, skill, MCP, Studio, inspector, persistence, graphs, performance, platform runtime, headless, examples and visible publishing are NOT APPLICABLE because this changes GitHub Actions scheduling only. Site roadmap and invisible-work blog record apply under the site contract.
- Next: commit this roadmap and scope record, implement the six workflow changes, validate YAML and hand off the exact result.

## Roadmap stage handoff

Commit `e2024bd23` added the in-progress Builds and platforms roadmap item and this delivery record. Paths: `docs/devlog/systems.json`, `docs/agent-handoffs/ci-queue-concurrency-2026-11-25.md`. Verification: `git diff --cached --stat` showed only these two files before commit; no build or GitHub Actions run was executed. Next: add cancellation groups to the six identified workflows.

## Implementation stage handoff

Code commit: `dcc3670f4` on `fix/ci-queue-concurrency-20261125`. Changed the six workflows listed above and marked the Builds and platforms roadmap item done on 2026-11-25. Each workflow now uses a run-level group made from workflow name, source repository and source branch, with `cancel-in-progress: true`. This coalesces push and PR events for one branch while keeping different workflows and branches separate. Pages workflow and job commands were not changed.

Verification on macOS worktree at `dcc3670f4`:

- Ruby `YAML.load_file` on all six edited YAML files, asserting group includes the PR head ref and cancellation is true: PASS 6/6.
- `git diff --check`: PASS.
- `python3.12 tools/site/build.py check`: PASS, 56 entries and 0 errors. The default `python3` is 3.10 and failed on an existing Python 3.12 f-string in `tools/site/render.py`; Python 3.12 was used successfully.
- Native build, CTest, merged-tree Mac quick gate, and live GitHub scheduling: NOT RUN at this handoff. No runtime code, native API, save format or test commands changed.

Feature Gate: implementation PASS (source change); architecture PASS (workflow-level isolation); dependencies PASS (no new dependency); tests PASS for YAML and site validation, NOT RUN for live GitHub behavior; handoff PASS; documentation PASS (roadmap and this record). Native API, skill, MCP, Studio, inspector, persistence, graphs, runtime performance, platform runtime, headless, examples, visual publishing: NOT APPLICABLE to CI scheduling. Invisible site blog item is deferred to a later visible Builds and platforms stage per the publishing contract.

Next: run the required merged-tree Mac quick gate on this branch's exact tree, then squash into current `main` and push. After push, verify that repeated PR updates for a single workflow cancel superseded runs. Do not count a queued or cancelled run as passing.

## Current-main synchronization handoff

The first `./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/ci-queue-concurrency-20261125/mac --jobs 12 --ctest-jobs 2` configured and built at `ac5a185ed`, then reached 39/468 CTest passes before it was intentionally interrupted: `origin/main` had advanced by two commits, so its result could not authorize integration. Report: `/Volumes/KINGSTON/AIVERSE-builds/ci-queue-concurrency-20261125/mac/test-reports/20261125-074444-66710`.

Merge commit `800f9adae` synchronized `origin/main` `a8783c55f`. `docs/devlog/systems.json` conflicted because main reformatted and expanded the file; resolution took current main's full file and inserted only this branch's completed CI queue roadmap entry. The merge also brought `.github/workflows/feature-gate.yml` into main. That new PR workflow has no concurrency, so it now joins the bounded queue scope. Next: add its group, validate, then rerun the Mac quick gate on the new combined tree.

## Feature Gate workflow handoff

Code commit `815699696` added the same per-workflow/source-branch cancellation group to `.github/workflows/feature-gate.yml` after it entered main. Verification: Ruby YAML parse on every `.github/workflows/*.yml` plus cancellation assertion on seven target workflows PASS; `python3.12 tools/site/build.py check` PASS (58 entries, 0 errors); `git diff --check` PASS. No native code changed. GitHub run behavior and the current-main Mac quick gate remain NOT RUN at this checkpoint. Next: perform the quick gate on the `a8783c55f` combined tree, then integrate and clean the newly queued runs while preserving Pages.

## Integration gate and user-directed acceleration

On current `origin/main` `a8783c55f`, macOS configure and build passed. The first CTest run reached 343/516 passes before a Codex app restart interrupted it. The resumed no-build quick run at commit `a970a41e6` passed 515/516 tests with zero reported failures; `aiverse_tests` was still running when the user explicitly directed immediate integration. Report: `/Volumes/KINGSTON/AIVERSE-builds/ci-queue-concurrency-20261125/mac/test-reports/20261125-092236-85601`. The full gate is therefore INCOMPLETE at the time of this handoff; do not claim it passed. This exception to the repository's pre-main gate follows the user's direct instruction on 2026-11-25. Follow up on the remaining test result after pushing.

GitHub queue cleanup before integration: 130 queued non-site runs identified; 129 cancellation requests succeeded, one returned HTTP 409. Pages/site runs were excluded. Repeat cleanup after push.

## Final main synchronization

Immediately before push, `origin/main` advanced to `fe7517614` with World Processes. Rebased the single integration squash without conflict. Its new `.github/workflows/world-processes.yml` is PR-triggered and lacked concurrency, so added the same per-workflow/source-branch cancellation group. Ruby YAML parse and `git diff --check`: PASS. The full Mac gate was not rerun on this newer merged tree because the user explicitly requested immediate integration; the only extra source change is workflow scheduling YAML.
