# PR #124 GCC policy fixture repair

Date 2026-11-25. Branch `fix/pr124-arm64-acceptance`; implementation `137729beb`.

The Linux ARM64 job in [CI run 45498443937](https://github.com/chromealex/AIVERSE/actions/runs/45498443937) reached 1609/2347 objects and failed compiling `tests/libraries/policy_mechanics_tests.cpp` and `tests/world_processes_gamecore_native_test.cpp`. The library policy fixture warnings (`SecretZone::seats`, `ShopItem::components`, `HeroRecord::items`, and dereference of optional reports/events) are inherited from `main`; the World Processes fixture had one unused lambda parameter. The commit makes aggregates complete, uses default constructed hero records, checks returned optionals after calls, and removes the unused name. No production API or simulation behavior changed.

Mac targeted builds of `aiverse_library_policy_mechanics_tests` and `aiverse_world_processes_gamecore_native_test` PASS; CTest `library_policy_mechanics` and `world_processes_gamecore_native_test` 2/2 PASS; `git diff --check` PASS. GCC 13 rerun on this SHA NOT RUN yet. The merged-tree Mac quick gate on previous code `81d8bb0b3` built and ran all 478 cases: 477 passed, `skill_library_isolation` timed out at 600 s under concurrent builds (the same test passed in 391 s on earlier gate). Isolated retry is in progress. No full gate pass or main integration claim.

Next: finish isolated skill test, rerun merged-tree gate without competing long tests, push and dispatch Linux CI on current code, then handle any further failures.
