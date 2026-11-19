# PHYSIS Stage 101 — Place optimized Debug perf acceptance PASS (Mac Studio)

Date: 2026-11-19
Repo: `chromealex/AIVERSE`
Branch: `integration/physis-terraweave-complete-20261119`
PR: https://github.com/chromealex/AIVERSE/pull/40 — remains Draft
Previous handoff: `docs/agent-handoffs/physis-stage100-place-replica-perf-2026-11-19.md`
Tested source: the user ran the Stage100 command after fetching the integration branch whose source head was `79ef7e4f5490b44115998708873c6f6ba53562db`. Their pasted excerpt **does not itself show `git rev-parse HEAD`**; verify actual SHA from the worktree or log before attributing native run to an exact commit.

## Native results actually received

User reports the CTest invoked before the focused Place scenario:
```text
100% tests passed, 0 tests failed out of 1
Total Test time (real) = 2.47 sec
```
In the Stage100 runbook this was `ctest -R '^aiverse_replica_tests$'`; the pasted tail does not repeat the CTest name. Treat as user's reported **one green CTest** immediately preceding the Place command, not proof of full 167-test suite.

Then directly executed:
```bash
./build/physis-terraweave-onepass/aiverse_place_tests \
  examples/place/scene.json \
  sixteen_clients_over_4g_converge_predict_and_late_join
```

The focused original load scenario **PASSED**, 11 checks, 0 failed, wall **13.4 seconds** including 30s simulated load and 6s quiet convergence:
```text
[place progress] ... virtual 30.0/30.0 s | wall 11.6 s
server avg 0.789 ms worst 1.905 ms | clients 17 | bodies 523
[place phases] poll 0.019 + players 0.128 + Box3D 0.301 +
               sync 0.050 + broadcast 0.290 ms/tick (1740 samples)
523 bodies, tick avg 0.789 ms worst 1.905 ms, 31954 B/s/client
resting bodies worst 0.0069 m, awake mean 0.022 m,
missing 0, extra 0
late joiner: worst 0.0069 m, missing 0, extra 0
494 bodies moved >0.5 m, 179 pushes, 22 boxes spawned
probe patrolled, 601 reconciles, corrections >1cm: 4
(0 while patrolling), max while patrolling 0.0058m, mean 0.00292m
[place 1/1] PASS ... wall 13.4 s
11 checks, 0 failed
```

The five per-tick phases sum to approximately **0.788 ms**, consistent with measured 0.789 ms average. By virtual 5s the average was 0.160 ms (501 bodies), by 10s 0.302 ms, by 15s 0.507 ms, by 20s 0.666 ms, by 25s 0.729 ms (17 clients / 520 bodies), by 30s 0.789 ms. After quiet convergence end at virtual 36s the cumulative avg was 0.745 ms.

Compared with PRE-Stage100 standalone macOS **unoptimized Debug** run of 8.362 ms average / 19.817 ms worst, the new optimized Debug avg is approximately **10.6x lower**. **Important attribution:** Stage100 changed both the production replica field/viewer fast paths AND enabled `AIVERSE_PLACE_FAST_DEBUG=ON` which adds `-O2` to Place/Box3D/net hot-path targets while keeping Debug symbols/asserts. This is NOT a controlled A/B proving the 10.6x improvement is from algorithmic changes alone, nor proof that ordinary -O0 Debug is below 5ms. Workload, 4G link, 523 physical bodies, late join and the strict 5ms benchmark threshold were kept unchanged.

## Code state

No additional source implementation in Stage101: it records a green native focused test and its caveats. Stage100 changes:
- `Server::setFields` reuses object/layout lookup, identical per-field quantization/change and reliability logic; Place uses it for body and character sync.
- `Server::setViewer` avoids per-client temporary vector.
- `AIVERSE_PLACE_FAST_DEBUG` is **OFF by default** and enabled by the combined one-pass acceptance script.
- `aiverse_replica_tests` includes batched-vs-legacy wire regression.
- Stage99 per-phase + live progress instrumentation is opt-in and leaves normal gameplay unaffected.

## Remaining acceptance before merge

- **Full 167/167 unfiltered CTest on CURRENT Stage100/101 combined source has NOT yet been run.**
- Prior full combined build produced 165/167 failures in `place` perf and `skill_audit`. Stage98's strict skills fix was verified by user (`0 errors, 0 warnings`), and Stage101 focused Place perf now passes.
- Run one full Debug all-target build + full unfiltered CTest with acceptance's optimized hot dependencies. This is still required to establish `place` entire 8-scenario test, all 31 PHYSIS, TerraWeave/Box3D, security, editor and other AIVERSE regression tests green together.
- Do NOT lift 5ms threshold, shrink 4G clients or remove validations. Keep PR Draft and `main` unchanged until actual complete PASS.

## Next exact Mac Studio command (clean worktree)

```bash
cd ~/Projects/AIVERSE-physis-terra-final
git status --short
git fetch origin
git switch --detach origin/integration/physis-terraweave-complete-20261119
AIVERSE_CTEST_VERBOSE=1 bash tools/physis-terraweave-final-validation.sh
```

Reuses existing `build/physis-terraweave-onepass` cache and records
configure/build/CTest/JUnit and live Place progress in
`build/physis-terraweave-onepass/onepass-reports/`.
Watch available disk (~20Gi reported during prior suite) and avoid
deleting another agent's cache. If further failures, save exact first
compiler or CTest log, diagnose, fix without weakening test, and
write a Stage102 handoff. Always report actual results honestly.
