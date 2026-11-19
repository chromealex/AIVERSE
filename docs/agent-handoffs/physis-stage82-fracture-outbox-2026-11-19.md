# PHYSIS Stage 82 — persistent fracture event outbox

Date: 2026-11-19
Repo: chromealex/AIVERSE
Branch: `feature/physis-nature-simulation`
PR #25: DRAFT; main unchanged
Preceding handoff:
`docs/agent-handoffs/physis-stage81-nature-tests-green-2026-11-19.md`
Code SHA: `2b0190cb93e4335e7e458fa03d75239ef65c8424`
Handoff SHA: see `git log -1 -- docs/agent-handoffs/physis-stage82-fracture-outbox-2026-11-19.md`

## Scope and architecture

Build a rollback-aware persistent ECS handoff for PHYSIS
`NatureStressOutputComponent::newlyFractured`, without adding
its own physics or foliage implementation. PHYSIS computes
environmental causes and the outbox; the game host owns
actual Box3D/TerraWeave falling and network replication.

Changed files:
- `engine/physis/PhysisFractureOutbox.h` (new).
  Defines `NatureFractureDelivery` status
  Empty/Pending/Acknowledged, persistent
  `NatureFractureOutboxComponent`, deterministic nonzero
  `natureFractureEventKey(Entity,climateTick)`,
  `advanceNatureFractureOutbox(World&)`,
  `pendingNatureFractures(const World&)` sorted by
  generation-safe Entity::packed(), and
  `acknowledgeNatureFracture(World&,Entity,key,tick)`.
  It publishes a valid current-tick newlyFractured transition
  once; Pending stays until ack even as climate advances.
  Wrong/stale/duplicate acknowledgements fail.
- `engine/physis/PhysisIntegration.h`:
  `enableFractureOutbox=false` by default, requires
  `enableNatureStress` (which requires forces/contacts),
  v1 opt-in persistent component schema ID
  `physis.fracture_outbox`, and post-stress scheduler.
  Previous save manifests unchanged unless enabled.
- `tests/physis_fracture_outbox_test.cpp` (new):
  CTest `physis_fracture_outbox`, tests event order and
  keys, pending/ack, late delivery, native persistence,
  snapshot rollback, COW deterministic replay.
- `CMakeLists.txt`: test registration with
  PHYSIS/ECS/PERSISTENCE labels.
- Agent docs, shared ownership contract and this handoff
  updated in a separate docs commit.

## External TerraWeave/Box3D contract

Source inspection confirms the separate
`feature/terraweave` branch implements
`engine/world/TerraWeaveRootEvents.h`
(`AnchoredPlantId`, `PlantFallEvent`,
`RootSupportRegistry`) and
`TerraWeaveFallenPhysics.h`
(`FallenPlantPhysics::spawn`).
Do not copy or merge unrelated concurrent Terra work.
When available in the game's integration branch, map each
PHYSIS Entity to a stable TerraWeave plant identity, root
support, species dimensions and valid fall direction.
Host must durably stage and deduplicate the physical reaction,
then acknowledge the PHYSIS event. ECS ack is NOT an atomic
Box3D transaction; external float physics must be
reconciled on rollback/save replay. PHYSIS may treat one
grass patch as one proxy rather than one entity per blade.
NEREID still solely owns water and erosion.

## Tests — actual vs NOT RUN

Actual user macOS results on earlier Stage78/79 source:
`physis_nereid_contacts` PASS (0.85s),
`physis_nature_stress` PASS (1.01s), **2/2 PASS**,
monitor full command PASS 20.71s, zero diagnostics.
Source tested then: `4d9c1adb94f88c7aefef58e3320afda9297e0b48`.

Stage82 `physis_fracture_outbox`: **NOT RUN** on the
new source; no macOS compiler or CTest evidence.
No full-repo CTest, production Box3D falling-tree host,
GUI/Metal QA or completed CI in this stage. Keep PR DRAFT.

## Next macOS command

```sh
cd ~/Projects/AIVERSE && \
git fetch origin && \
git switch feature/physis-nature-simulation && \
git pull --ff-only origin feature/physis-nature-simulation && \
python3 tools/aiverse-progress.py --mode all \
  --build-dir build/physis-nereid-validation \
  --define AIVERSE_BUILD_TESTS=ON \
  --define AIVERSE_ENABLE_PHYSICS=ON \
  --target aiverse_physis_nereid_contacts_test \
  --target aiverse_physis_nature_stress_test \
  --target aiverse_physis_fracture_outbox_test \
  --test-regex '^physis_(nereid_contacts|nature_stress|fracture_outbox)$' \
  --jobs 4
```

Fix the first actual failure without relaxing assertions.
After green, run a broader PHYSIS suite and coordinate with
TerraWeave/Box3D integration owner for actual falling trees.
