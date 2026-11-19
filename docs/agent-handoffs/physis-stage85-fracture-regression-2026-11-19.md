# PHYSIS Stage 85 — fracture lifecycle/delivery regression fixtures

Date: 2026-11-19
Repository: `chromealex/AIVERSE`
Branch: `feature/physis-fracture-delivery`
Based on PHYSIS branch `feature/physis-nature-simulation` at `a95a2df567184d430348243c3700de792395cb08`. Do not merge into `main` directly: integrate into PHYSIS branch after review and native validation.

## Source changes
- `tests/physis_fracture_outbox_test.cpp` extended, commits:
  - `30fe31463ca1dfebcd5cbd48f87978c014cf548c`: explicit repair/rearm, second fracture at a new climate tick and key, stale ACK rejection, snapshot rollback; dispatch budget, retry, sink exception, host journal deduplication across rollback.
  - `3fc8e210b7c5bc04ab5b58e9133976d9e445e90e`: include `<algorithm>` explicitly.
  - `68e578870e34821019854700f296b7d8a8eb2a29`: correct false-positive guard so missing callback exception fails the test.
- `engine/physis/AGENTS.md`, commit `4a30287162cc7df9b36b62ff17888d4a3457259b`: Stages 83–85 ownership/API/verification rules.
- Existing `physis_fracture_outbox` CTest target already registered in CMake by Stage82; no duplicate target, no schema bump, no engine scheduling changes.

## Behavioral contract and coverage
1. Pending fractures are not rearmed. Acknowledgement is necessary but insufficient until stress state is repaired and a later climate tick is reached.
2. Wrong key/tick and duplicate rearm are rejected. A fresh later fracture produces a new key, and old ACK cannot consume it.
3. Budget 1..4096 bounds sink calls; Retry and sink exception preserve pending events. DurablyCommitted acknowledges only matching current ECS event.
4. The mock external journal keeps track of event keys independently of ECS copy/rollback. It models idempotence, **not** real filesystem durability, nor Box3D/TerraWeave behavior.
5. Existing Stage82 file save/load + COW test remains in place.

## Tests ACTUALLY RUN and evidence
**None.** These are authored but **NOT RUN** in a connector-only environment with no AIVERSE checkout/compiler linked. No claim of zero warnings or clean CTest. Last user-provided native PASS belongs to Stage81 source (2/2) and does not prove this branch.

## Next owner steps on macOS
```sh
cd ~/Projects/AIVERSE &&
git fetch origin &&
git worktree add ../AIVERSE-physis-fracture origin/feature/physis-fracture-delivery &&
cd ../AIVERSE-physis-fracture &&
python3 tools/aiverse-progress.py --mode all \
  --build-dir build/physis-fracture-validation \
  --define AIVERSE_BUILD_TESTS=ON \
  --define AIVERSE_ENABLE_PHYSICS=ON \
  --target aiverse_physis_fracture_outbox_test \
  --target aiverse_physis_nature_stress_test \
  --target aiverse_physis_nereid_contacts_test \
  --test-regex '^physis_(fracture_outbox|nature_stress|nereid_contacts)$' \
  --jobs 4
```
If an existing worktree/branch name conflicts, use `git worktree list` and create a differently named detached worktree without deleting another agent's checkout.

After fixes and targeted PASS, run broad `^physis_` and full AIVERSE CTest/CI. Keep PHYSIS PR #25 Draft until verified. Real native `PlantFallEvent` conversion, stable `AnchoredPlantId` mapping, host durable journal and Box3D restart/rollback reconciliation are NOT yet wired; coordinate with active TerraWeave work, do not copy that branch's implementation.
