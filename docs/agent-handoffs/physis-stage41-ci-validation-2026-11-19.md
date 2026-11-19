# PHYSIS Stage 41 — CI queue control and journal-test cleanup (2026-11-19)

Repository: chromealex/AIVERSE
Branch: feature/physis-nature-simulation
PR: https://github.com/chromealex/AIVERSE/pull/25 — DRAFT
Previous handoff: docs/agent-handoffs/physis-stage40-cross-process-journal-2026-11-19.md

## Implemented / committed changes

1. `620906fcd79927c99c6512091e78529990f9d857`
   - `tests/physis_admin_journal_test.cpp`
   - In `killedEndpointRecoversQueuedJournal`, explicitly scope the final
     `LoopbackAdminEndpoint again` so its lock and listener are destroyed
     BEFORE `std::filesystem::remove_all(dir)`. The previous Stage 40 test
     deleted the live journal lock inode, violating the ownership contract.
     No production ECS/protocol code or persistence schema changed.

2. `d2738bdf06e1c089ace74fd4c714cf5eaa01e22a`
   - `.github/workflows/physis.yml`
   - Add per-workflow/ref GitHub Actions concurrency with
     `cancel-in-progress: true`. New pushes on this branch will supersede
     obsolete queued runs rather than endlessly piling up additional jobs.

3. `714a0b15fe46e95f1da5b3b4dd14cb6a709e257e`
   - `.github/workflows/physis.yml`
   - Pipe the PHYSIS native build to `build/physis-ci/physis-build.log`,
     preserve the build's exit code with `set -o pipefail`, and upload
     the build log alongside CTest and configure evidence through
     `actions/upload-artifact@v4` on success OR failure.

4. `76c8d350d834f3cb705932575874aab1587cbdce`
   - `engine/physis/AGENTS.md`: explicit lock-inode lifecycle rules and
     native CI status caveat.

5. `50e59d2dccc4b06a3b45bbef85940a85c60fbcf7`
   - `skills/world/aiverse-physis/SKILL.md`: same advice for AI agents.

## Verification ACTUALLY performed

- GitHub connector returned commit SHAs for all source, workflow and
  documentation writes; fetched `physis.yml`, confirmed the job targets
  `aiverse_physis_admin_journal_test`,
  `aiverse_physis_remote_edit_test` and
  `aiverse_physis_field_runtime_test` among all focused PHYSIS targets.
- Queried GitHub Actions for `feature/physis-nature-simulation` before
  the CI fix: 149 workflow runs, all `queued`, **zero PASS/FAIL**; none
  reported completed CTest. A newer run associated with
  `714a0b15` was still `queued` for BOTH `macos-latest` and
  `ubuntu-latest`. `d2738bdf` run was marked `cancelled` after the
  next commit, consistent with the new concurrency policy.
- Native source checkout attempt:
  `git ls-remote https://github.com/chromealex/AIVERSE.git ...`
  failed because the local container could not resolve `github.com`.
  A separate `curl --resolve` connection also failed, so no native
  repository compilation was possible in this environment.
- **NOT RUN:** AIVERSE CMake configure/build, PHYSIS CTest, real macOS
  editor, full network recovery fixture, sanitizer, Windows ABI tests.
  No native test PASS is claimed by this stage. Prior isolated POSIX
  header smoke is not full AIVERSE validation.

## Technical constraints

- Do NOT merge PR #25 on the basis of authored tests or queued CI jobs.
- The canonical admin route remains `FieldRuntimeState` +
  `installEditableFromMap`; do not install legacy
  `RuntimeFieldRegistry/FieldCommandHost` simultaneously.
- The journal and authoritative Simulation World checkpoint are still
  separate persistence domains. Crash-durable exactly-once receipts
  are NOT promised; Unknown/Conflict is intentional on ambiguity.
- `JournalProcessLock` is advisory and relies on a trusted local
  directory and stable lock inode. Neither distributed fencing nor
  TLS to public remote clients has been added.
- PHYSIS owns nature simulation; NEREID water; TerraWeave terrain and
  foliage; Box3D physics. Remain genre-independent.

## Exact next steps for the next agent

1. Check `https://github.com/chromealex/AIVERSE/actions/workflows/physis.yml`
   for the newest PHYSIS branch run. Wait for a **completed** native
   Ubuntu/macOS run; read build and test artifact logs, not simply job
   registration. Do not infer PASS from a queued state.
2. On a native checkout of this branch, run:

   ```sh
   cmake -S . -B build/physis41 -DCMAKE_BUILD_TYPE=Debug \
       -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON \
       -DAIVERSE_BUILD_EDITOR=OFF -DAIVERSE_BUILD_TOOLS=OFF \
       -DAIVERSE_BUILD_CLIENT=OFF -DAIVERSE_BUILD_HEADLESS=OFF \
       -DAIVERSE_BUILD_SERVER=OFF -DAIVERSE_BUILD_PHYSICS_DEMO=OFF
   cmake --build build/physis41 --parallel 4 --target \
       aiverse_physis_admin_journal_test \
       aiverse_physis_remote_edit_test \
       aiverse_physis_field_runtime_test
   ctest --test-dir build/physis41 --output-on-failure \
       -R '^physis_(admin_journal|remote_edit|field_runtime)$'
   ```

3. If the focused build passes, build ALL PHYSIS targets defined in
   `.github/workflows/physis.yml` and run
   `ctest --test-dir build/physis41 --output-on-failure -R '^physis_'`.
4. Diagnose and correct the FIRST real compiler/CTest failure. Pay
   particular attention to POSIX fork/SIGKILL test cleanup, the
   restored Queued/Applied receipt state, and macOS/Windows behavior.
5. Record actual failures/fixtures, branch HEAD and next step in a
   fresh per-stage handoff; leave PR draft until all validations pass.
