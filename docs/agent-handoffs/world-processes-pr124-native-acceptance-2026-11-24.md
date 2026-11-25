# PR #124 native acceptance repair — 2026-11-24

Owner: World Processes integration audit. Branch: `fix/pr124-arm64-acceptance`, based on PR #124 head `4abd37dda77cfbc5a3b30090620c006618750501`. Code commit: `00b0a5e28`. Main and the existing PR branch were not changed.

## Scope and changes

- Repaired two observed ARM64 CI compilation failures: full aggregate initialization of `NewIdentityRollback`, and Box3D's actual `b3Quat {v,s}` API in `Character.cpp`.
- Repaired native acceptance compile failures: explicit ECS component construction in the sediment test, unique `imprintCanonical` name, and the missing `PlanetTerrainCollisionConfig` example alias. Used the Box3D quaternion layout in two planet publishers to avoid missing-braces warnings.
- Preserved ECS Entity index zero in EVOLVE LOD, the shared budget cursor, source identity and durable structure consequence validation. The first valid entity has `packed()==0`; zero cannot serve as its absence sentinel. Existing `PlanetPlant` keys retain their nonzero requirement.
- Fixed the authored planet demo's walker data to use the GameCore `move.speed` field. The old top-level `speed` was ignored, so the walker had no `Mobile` or `Orders` components. The GameCore test now asserts actual eligibility.

## Actual verification

Mac x86_64, AppleClang 17, Box3D, Debug, build and logs on KINGSTON. Exact source commit `00b0a5e28`:

`bash tests/world_processes_native_acceptance.sh /Volumes/KINGSTON/AIVERSE-builds/pr124-finish/native 4` exited **1**. Evidence: `/Volumes/KINGSTON/AIVERSE-builds/pr124-finish/native/world-processes-native-evidence/20261124T184707Z-23949/`. Configure and build passed; CTest **8 PASS / 1 FAIL**. The sole failing test is `world_processes_gamecore_native_test`, at `tests/world_processes_gamecore_native_test.cpp:1872`: `granular::previewPlanetSoilGeometry` returned no preview for the authored station cell and a one-quantum fill. Do not merge or claim native acceptance yet. A local diagnostic 16-quantum attempt moved beyond this assertion but exposed `TerraWeave invalid selected root support`; it was reverted because the root cause is unresolved.

The five-test filter `world_processes_linked_test|world_processes_planet_host_test|physics_scene|physics_runtime|physis_box3d` passed **5/5** after the fixes. The full nine-test script provides the stronger current evidence above. `git diff --check` passed before the code commit. No fresh Linux ARM64, Windows, web, Studio, MCP linked-authentication, GPU capture, visual, performance, whole-suite or merged-tree Mac result exists for this commit.

## Feature delivery gate

Implementation: PARTIAL, fixes above; authored terrain acceptance fails. Architecture: PASS for the bounded changes, existing ECS/PHYSIS/TerraWeave owners retained. Native API: no new API. Skill: existing World Processes skill, no new operation. MCP/Studio/Inspector: NOT RUN on this commit. Persistence/rollback: linked and planet host focused tests pass; complete authored demo fails. Graphs: NOT APPLICABLE to this repair. Performance/platforms/headless/examples: Mac CPU focused test only; all broader gates NOT RUN. Dependencies: no new target/dependency. Tests: 8/9, FAIL overall. Handoff: this record. Documentation/publishing: no readiness claim or publication.

## Next step

Diagnose why the authored station cell produces no `previewPlanetSoilGeometry` result at one height quantum. Preserve the conservation and validation rules; do not widen test bounds merely to hide the failure. Then resolve the root-support exception seen with a larger edit, rerun the complete native script, `tools/evolve-demo-acceptance.py`, Studio/MCP acceptance and the merged-tree Mac gate. Rerun ARM64 CI after integration. PR #124 remains draft.
