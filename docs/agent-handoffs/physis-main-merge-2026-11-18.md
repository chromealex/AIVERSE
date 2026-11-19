# PHYSIS stage 18 prerequisite — merge main into PHYSIS (2026-11-18)

**Destination:** `feature/physis-nature-simulation`, draft PR #25.  
**Integration merge commit:** `810feba70f01cdc35003ac80ea8486f39dc16049`.  
**First parent:** `4eaee58e20631c2466eba3694dfd9b2700ab18d8` (PHYSIS, including saved Stage 17+ branch audit).  
**Second parent:** `9ee6ff6fbf390bd45363bc92c838f492dfe92eee` (main).  
**Shared merge-base:** `309d7035e7317c5e3be66e8118298d2b64d490a1`.  
**Resulting tree:** `fa5699c2180e4b8ef1b3492dac1cdfb08bb7e41b`.

## Outcome and conflict resolution

Used all three recursive Git trees plus a linewise three-way merge on actual shared-ancestor text. No destructive `reset`, no forced branch reference update. The GitHub branch update used `expected_sha` and fast-forward; subsequent fetch verified BOTH parent commits and the expected merged tree.

45 main-side paths differ from the common ancestor. One (`run-tests.sh`) was already identical on PHYSIS, 41 source paths were imported by their Git blob identities, and three files had changes on both sides:

1. `AGENTS.md`: merge non-overlapping InputFlow rules/ownership and PHYSIS/SpectraFX + stage handoffs without deleting either.
2. `CMakeLists.txt`: preserve current PHYSIS/VFX CTests while adding `AIVERSE::InputFlow` link requirements to client/editor targets.
3. `skills/catalog.json`: preserve PHYSIS/SpectraFX entries and update InputFlow metadata/source. JSON parse was checked in the three-way merge routine.

Main also brought the `EnhancedInput*`, `InputFlow*`, client/editor runtime routing, ECS test updates and additional test files. Existing PHYSIS Stage 16/17 code, independent NEREID/Terra ownership instructions and in-progress authoring of the next Scene overlay stage remain intact.

## Test status

- **PASS**: GitHub created commit with correct parent commits, tree SHA and updated `feature/physis-nature-simulation` without forcing.
- **PASS**: source-side JSON syntax check for merged `skills/catalog.json`.
- **NOT RUN**: native configure, compiler, full `./run-tests.sh`, `physis_*` CTest, `enhanced_input*` CTest, gameplay parity, editor launch, UI screenshot and device validation.
- Actual local `git ls-remote` failed: `Could not resolve host: github.com`. The GitHub connector supports Git data writes but not running C++ compile jobs.

## Required next steps

1. On a checkout, run `./run-tests.sh --filter '^physis_'`, the relevant input/ecs tests and an editor build after this merge; inspect first failure and record actual logs rather than assuming PASS.
2. Continue Stage 18 PHYSIS Scene viewport overlay and inspector editing using the merged editor interfaces. Keep PHYSIS world state read-only in runtime; map-authored configuration edits must use proper document Undo/Redo and native project loader contract.
3. No direct dependency on FPS, RTS or VFX from `engine/physis`. The existing Box3D opt-in adapter remains physics-owned, Water/NEREID PR #23 and TerraWeave PR #24 retain independent ownership.
4. After implementation commit(s), create a new Stage 18 handoff with exact SHAs and actual test status. No merge PR #25 to main before real verification.
