# PHYSIS Stage 87 — authoritative TerraWeave fall host and Box3D derived reconciliation

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `feature/physis-terraweave-fracture-host`
Base commit: `76ab26a45a9fedb0a580765231f1a9e4858f0010` from `feature/physis-fracture-delivery`
Prior checkpoint: `docs/agent-handoffs/physis-stage86b-native-28-green-2026-11-19.md`.
Feature commits:
- `75acb891f1e53d607f64458c4cd24eced4c74b42` persistent plant ledger
- `4d04fbdeba3630bd0cc302fe6d27bde3e4880a95` typed TerraWeave/Box3D bridge
- `2bab841f78ca9291f6be8443a918ffc06ee83f77` stale body and ownership rebind fixes
- `c57d9e363c07d681487d690f912f68b418f11e4c` native host CTest fixture
- `9d96e1c6896c58aa9a8b6ceb0b470340420be08e` CTest registration
- `27579ac29278e7a9fb2f9b5dfb81df5524c6ec61` optional real Terra/Box3D test
- `8b09dd52a10e2c8e050931420b158202a235ab50` fixture construction fix
- `2394174344d5f56caaaa13f52c1cd2d671733580` conditional real integration CTest
- `32bbf71feb1cc506ddd0d1f67c3c158dfa1e6c4c` bounded pending-ID root scan
- `7d062bfaeafbcae3b91f3de96ed9ae729a78c075` unique binding validation
- `fe5a44328e3d065c42d5dac641286d9b0b9caf0b` efficient sorted validation

## What is implemented

1. New `engine/world/PhysisNatureFallHost.h` — opt-in native host resource `world.physis_nature_fall` schema v1. No PHYSIS schema change. `NatureFallPlantId`, `NatureFallBinding`, `NatureFallRecord`, `NatureFallHostState`. Register explicitly from GameModule::setup via `installNatureFallHost(sim)`. Bind generation-safe PHYSIS Entity to stable Terra species/key via `bindNatureFallPlant(world, entity, id)`. The ECS journal stores active/tombstoned events keyed by entity/tick/key, Terra signed-centimetre fall inputs, deterministic direction, root support and soil revision. Limits: 8192 bindings, 32768 history entries. On malformed input, Archive read rejects invalid/unsorted/double-bound records. The first durable action at host barrier is `stageNatureFall(world, event, exactRecord)`, which only yields DurablyCommitted on exact saved identity. `retireNatureFall` after acknowledged repair/rearm keeps history to guard repeat delivery.
2. New `engine/world/PhysisTerraWeaveFallBridge.h` — actual typed conversion between PHYSIS `NatureFractureEvent` and TerraWeave `AnchoredPlant`/`PlantFallEvent`. `fractureFallDirection` uses bounded fixed-point integer math. `dispatchPhysisTerraWeaveFalls` builds a requested-ID map from pending records and performs **one rooted-plant scan**, then calls existing PHYSIS dispatcher to durably stage to ECS before ack. Grass is excluded from rooted physics: treat visual grass as clustered game-side coverage.
3. `NatureFallBox3DReconciler` derives Box3D `FallenPlantPhysics::spawn` results from active ECS journal records at physics owner barrier. Uses canonical TerraWeave body specs via resolver, reuses valid existing body, repairs stale handles, deduplicates by plant identity and only removes bodies created by this bridge on ECS rollback/replant. Canonical TerraWeave/Box3D code is **not copied** into this branch.
4. New `tests/physis_nature_fall_host_test.cpp` and CMake `physis_nature_fall_host` (PHYSIS-only independent test), covering binding, missing/wrong mapping, journal idempotence, repair retirement, stale replay, native Simulation save/load and COW snapshot rollback.
5. New `tests/physis_terraweave_fall_bridge_test.cpp` and conditional CTest `physis_terraweave_fall_bridge`; **builds only when actual TerraWeave headers exist in the checkout and AIVERSE_ENABLE_PHYSICS=ON**. Proves same-World PHYSIS -> Terra PlantFallEvent -> Box3D body and rollback/replay. It is NOT built or run on the current PHYSIS-only branch because TerraWeave lives on `feature/terraweave`.
6. `engine/physis/AGENTS.md` updated to clarify genre independence, source owner and save/scheduler boundary.

## Authoritative ordering and design constraints

- After climate, forces, contacts, stress, outbox and TerraWeave root/terrain authoritative updates, the trusted GAME host calls `dispatchPhysisTerraWeaveFalls` (outside deterministic PHYSIS iteration). The durable transaction is a native ECS resource in the SAME World; `dispatchNatureFractures` acknowledges after the record is staged. Native save/snapshot must happen at a committed barrier, never in between stage and ACK.
- NEREID alone computes water, bank erosion, flow and canonical water levels. TerraWeave owns root generation, plant support and species; Box3D owns live float bodies. No PHYSIS core include dependencies on TerraWeave, Box3D or genre.
- The host then calls `NatureFallBox3DReconciler::reconcile` *after* Terra scene binding to make render state/physics reflect the journal. On rollback/save load, call it again; physics bodies are not an ECS authority.
- Mid-fall Box3D velocity/position is **not** serialized here: this bridge reconstructs the initial fall pose. Exact physics replay requires a separate Box3D snapshot/authoritative pose mechanism.
- A trusted repair sequence must (a) repair Terra rooted plant and physics state, (b) reset PHYSIS stress latch and call `rearmNatureFractureAfterRepair` on a later tick, (c) call `retireNatureFall`, then reconcile. Do not allow active old history to block a second fracture.
- Soil-driven Terra fall events can already have their own body. `FallenPlantPhysics::spawn` deduplicates by stable `AnchoredPlantId`. The host must reconcile competing fall causes; this code never deletes a pre-existing soil-owned body.
- External entity deletion before delivery and bounded ledger history cleanup after checkpoints are future host concerns. One rooted scan per pending batch can still be expensive for very large forests; add revisioned lookup cache only with deterministic invalidation.

## Tests ACTUALLY run

**NOT RUN** on this Stage87 source: native compiler, CMake/CTest, cross-branch TerraWeave/Box3D test, CI, UI/Metal. The earlier 28/28 PASS on Mac Studio belongs to `feature/physis-fracture-delivery` and does NOT validate this new code. No cherry-pick or merge of TerraWeave was performed; this is a draft integration bridge to be validated when the branches are composed.

## Next commands for Mac Studio — isolated worktree

```bash
git -C ~/Projects/AIVERSE fetch origin
git -C ~/Projects/AIVERSE worktree add --detach \
  ~/Projects/AIVERSE-physis87 \
  origin/feature/physis-terraweave-fracture-host
cd ~/Projects/AIVERSE-physis87
python3 tools/aiverse-progress.py --mode all \
  --build-dir build/physis87-validation \
  --define AIVERSE_BUILD_TESTS=ON \
  --define AIVERSE_ENABLE_PHYSICS=ON \
  --target aiverse_physis_nature_fall_host_test \
  --target aiverse_physis_fracture_outbox_test \
  --test-regex '^physis_(nature_fall_host|fracture_outbox)$' \
  --jobs 4
```

Then build the broader `^physis_` suite using `bash ./run-tests.sh --filter '^physis_' --build-dir build/physis87-full --jobs 4 --ctest-jobs 4`.

After the focused source passes, create an **isolated integration worktree** combining `feature/physis-terraweave-fracture-host` with `feature/terraweave` without touching original agents' worktrees; resolve integration conflicts and run the additional conditional CTest `physis_terraweave_fall_bridge` with Box3D enabled. Mark the actual test results in a new handoff. Do not merge to `main` or the source PHYSIS/TerraWeave branches until validated.

## Final Stage87 follow-up code/docs commits (same stage)

- `5c6187dde5bdb6e1f9b80fb87495df2f8bf8a5ac`: conditional true TerraWeave CTest also requires `TerraWeaveEcsAuthority.h` present.
- `92179203031cbbae4892ce2410442efb4f6b5d24`: normalize Stage87 `engine/physis/AGENTS.md` Markdown references.
- `5b010708a6d0fe790004c475e981e39feb7a33e3`: explicitly include `<utility>` in the native host header.
- Handoff commit: get via `git log -1 -- docs/agent-handoffs/physis-stage87-terraweave-fall-host-2026-11-19.md`.

GitHub draft PR #38: https://github.com/chromealex/AIVERSE/pull/38.
This work remains **NOT RUN** on Stage87. Do not attribute prior 28/28 PASS to this branch. Before applying to a combined TerraWeave checkout, inspect new commits arriving on both feature branches.
