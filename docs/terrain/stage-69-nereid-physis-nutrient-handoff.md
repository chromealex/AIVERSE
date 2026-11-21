# TerraWeave Stage 69 — NEREID junction nutrients, narrow creeks and PHYSIS direct sampling

Date: 2026-11-19. Branch: `feature/terraweave`. Base PR #24
(`main`, Draft). **Source-feature phase**; native compilation, CTest,
GPU screenshots and PHYSIS runtime verification remain **NOT RUN**.

## What changed, exact commits

- `25eac2662ad392c268ba69490e4ddf074f397bff` —
  `engine/world/TerraWeaveForestNutrientCycle.h`: pure
  `ForestNutrientJunctionLink` contract and bounded same-tick transfer
  into the existing saved Stage68 dissolved ledger, an explicit
  `junctionTransferredUnits` output, max 64 links per default due tick
  (configurable 1..256); no new water or mass authority.
- `843bdf93324b3a2bdfe47e6fe2d13a3f0be39207` — NEW
  `engine/world/TerraWeaveNereidNutrientJunction.h`:
  inspect LIVE `WaterConfluenceComponent` and
  `WaterCascadeConnectionsComponent` with actual authored
  `WaterCascadeComponent`, valid generation-safe inlet/outlet bodies,
  strict NEREID footprint checks, bounded near-mouth cell discovery.
  Creates sorted links, never arbitrary river-to-river guesses.
- `e99de241863f6275d9a0fbbb0c7659e185e83e17` —
  pure nutrient settings gain `waterSamplesPerAxis` 1/2/4/8
  (default **8**, 4m sampling within a 32m saved nutrient cell).
- `e75a9bd5078c38b6ad0f5de4950fd781fa373c3a` —
  `engine/world/TerraWeaveForestNutrientEcs.h` now uses the
  `WaterQueryIndex` center-first plus bounded subcell samples so a
  narrow NEREID creek does not disappear just because its 32m cell
  center is dry. Discover junctions on the same ECS tick, pass into
  the pure nutrient kernel and publish one authoritative World COW
  transaction. Native add-system signature appends optional
  `NereidNutrientJunctionSettings`.
- `6442b7c5edb81e9dffd00ab280fb1c22414fc056` — NEW
  `engine/world/TerraWeavePhysisNutrientBridge.h`:
  directly consumes `physis::sampleNatureField` from
  `engine/physis/PhysisPresentation.h` when that PHYSIS module is
  actually available in the build. Integer Fixed conversion
  supplies authoritative temperature (centi-C), rain and wetness,
  through existing Stage68 climate callbacks. A terrain-only build
  without PHYSIS compiles a fail-fast fallback instead of inventing
  environmental state.
- `a474a11ed6345afc3bdaf0d7fcea5b034ad1e78d` —
  initial shared 32m water-cell overlap check: never re-label a
  cell that simultaneously belongs to both linked rivers.
- `d5218c8d5052e537cd083e48155f416e8cb2a5dd` —
  `examples/world/terraweave_native/Integration.h`:
  append `junctionSettings` and `useNativePhysisWeather=false`
  native setup parameters, explicit source-availability check and
  callback wiring; defaults retain existing games.
- `d7574ef66c0c99f3f28cccd7a15979b47564355c` —
  the junction planner now also requires `WaterQueryIndex` to
  report **submerged ground** and the expected body at the exact
  authored terrain height, not just a water footprint near the mouth.
  Prevents transfer when erosion/landslide leaves an old dry bank.
- `03254ccbd3f0ea1a83732aa235d1ff917f20d243` —
  native nutrient ECS passes its SAME immutable Earthworks snapshot
  and existing NEREID water index into junction planning.
- `bd25b0bb920e9b0792240c8bed290e1eef58e596` —
  cache source/target candidate water-body ownership in small
  `std::set<Cell>` once per link, rather than repeat 64 subcell
  `WaterQueryIndex` probes inside every source/target pair.

## Correctness contract

NEREID remains the only owner of river, current, confluence/cascade
geometry, hydraulic obstruction, sediment and water volume. Stage69
only moves *dissolved nutrient units* inside the existing
`ForestDeadwoodResource` v2 ledger using **linked NEREID ECS entities**.
No new ECS persistence schema/version, water body, renderer, physics
body or gameplay genre dependency. PHYSIS remains the sole
weather generator; TerraWeave reads a completed climate snapshot.

**Link construction:** `buildNereidNutrientJunctionPlan(world,ground,
waterIndex,settings)` visits valid/active NEREID confluences
(inlet slot → outlet) and cascades (start/top body → end/foot body)
on the current ECS world. Links require alive distinct bodies, valid
canonical water-body geometry, wet/submerged NEREID samples in both
destination and source cells, bounded search (default immediate neighbor
of the mouth), and distinct saved 32m cells with unambiguous ownership.
Repeated body pairs do not constitute a link without a real
NEREID confluence or cascade. Each eligible cell is probed on an
integer 4m lattice (configurable 32/16/8/4m), using the current saved
Earthworks terrain height as the Y coordinate for NEREID sampling.
Confluence topology and linked cascade ECS handles are never mutated.

**Transfer arithmetic:** `advanceForestNutrientCycle(...,junctions)`
accepts sorted unique links, validates their bounds and body identity
tags, then processes them only AFTER existing cardinal transfers from
the previous water snapshot. A `debitedFromOriginal` budget enforces
that each source tile can send only as many units as were PRESENT
BEFORE the tick; units received from another cell cannot pass onward
again until a future due tick. This prevents source->junction->next
river multi-hop overtransport. Two links sharing one source compete
for the same original budget in stable link-key order. Each transfer
is a debit/credit of the SAME abstract unit in one staged resource,
so `deadwoodLedgerBalanced` remains the final global conservation
check. There is no implicit export at a map boundary; the existing
Stage68 explicit monotonic outlet command remains the only export.

**Coarse-cell limit:** Stage69 **samples** creeks as narrow as
approximately 4m where the lattice intersects their NEREID shape,
but still **stores** nutrients in the existing 32m tile, not a
separate 4m reservoir. Two water bodies overlapping one tile are
ambiguous, and cross-body transport is deliberately suppressed rather
than reallocating mass to a guessed body. Real sub-4m geometry or
rivers whose mouth is entirely within one 32m nutrient cell still
need a future smaller persistent water-cell format and migration.
`NereidNutrientJunctionSettings::maxLinks` must be <=
`ForestNutrientCycleSettings::maxJunctionTransfersPerStep`; the
native adapter rejects more work instead of starving later links.

**Direct PHYSIS adapter:** The actual PHYSIS source was inspected on
`feature/physis-nature-simulation`:
`engine/physis/PhysisPresentation.h` exposes
`physis::NatureFieldSample` and
`physis::sampleNatureField(World,Vec3,out)`.
Stage69's adapter is guarded with
`__has_include("engine/physis/PhysisPresentation.h")`
because this TerraWeave feature branch does NOT presently contain
the PHYSIS header. When PHYSIS is compiled, a native host can pass
`useNativePhysisWeather=true` to the example
`registerSystems` helper; it reads the existing authoritative
`World` at each tick, with no standalone climate solver or GPU
readback. If the code is absent, opt-in throws before ECS registration;
an explicitly missing PHYSIS atmosphere at a sampled coordinate
throws during sampling (not invented neutral weather). The host
schedules PHYSIS BEFORE the TerraWeave ecology/deadwood/nutrient
systems. PHYSIS rain is normalized intensity, not a mass/volume
source for NEREID.

## Performance budget and deterministic behavior

- No per-blade ECS entities and no per-tree nutrient GPU objects.
- Source/live water index is read once per due ECS stage, with up to
  `waterSamplesPerAxis^2` subcell queries when the center is dry.
  Default 8x8 sampling is **additional CPU work**, not a free
  improvement; benchmark 4/8 vs 32m center-only for mobile.
- The junction planner has <=64 default active links, at most
  5x5 nearby 32m cells per endpoint (setting radius 0..2), 64
  subcell samples per cell max, and no unbounded geometry walk.
- Sorting by stable generation-safe confluence/cascade Entity ID and
  inlet slot, source/target tile tie-breaking by distance+signed
  coordinate, bounded pure transfer kernel and persistent source
  cursors preserve stable replay order.
- A unique dissolved nutrient amount per 32m cell is still the
  authoritative TerraWeave state. COW snapshot publication occurs
  once per scheduled nutrient step, AFTER the NEREID sample barrier;
  no renderer/Box3D/NavMesh writes occur in the callback.
- Legacy opt-in `enableForestNutrientCycle=false` behavior is
  unchanged; `useNativePhysisWeather=false` retains caller-supplied
  climate or default neutral rates/zero rainfall.

## Actually verified and NOT verified

**Performed:** inspected real NEREID
`WaterConfluenceComponent`, `WaterCascadeConnectionsComponent`,
`WaterCascadeComponent`, `WaterQueryIndex`,
`coversWaterBody` semantics, and PHYSIS
`PhysisPresentation.h`/`PhysisNatureSampling.h` source on the
separate PHYSIS branch; wrote and read source via connected GitHub.
GitHub accepted and committed source edits on feature branch.

**NOT RUN:** local C++20 CMake configure/build, Clang/GCC/MSVC,
CTest, GitHub CI, full PR checkout, PHYSIS feature-branch merge,
native PHYSIS integration, Metal/Vulkan/GL captures, Box3D,
benchmark/profiler or save/replay snapshots. Do not imply source
review proves compilation or performance. Stage69's PHYSIS direct
adapter is **conditional**, not yet validated in a repository
containing both feature branch header sets. PR #24 remains Draft
and should not merge into `main` without integration QA.

## Independent QA tests to author and run

On an isolated Mac Studio or CI checkout (never another agent's
in-flight rebase):

```bash
cd ~/Projects/AIVERSE-terraweave-qa
git fetch origin feature/terraweave
git switch feature/terraweave
git pull --ff-only origin feature/terraweave
cmake -S . -B build/terraweave-qa -DAIVERSE_BUILD_TESTS=ON
cmake --build build/terraweave-qa --parallel 4 --target aiverse_terraweave_forest_scene_test
ctest --test-dir build/terraweave-qa --output-on-failure -R '^terraweave_forest_scene$'
```

Write a Stage69 dedicated test target first, then cover:
1. Multi-inlet real NEREID confluence, downstream-only transfer
   from both inlets without cross-inlet movement.
2. Live cascade with valid wet NEREID inlet/outlet, transfer across
   distinct IDs, no transfer if disabled, dry or physically out
   of footprint; source and destination inside SAME 32m cell
   must conservatively skip.
3. Narrow 4m creek lying away from nutrient center; verify 8x8
   probes detect submerged water, 1x1 center-only does not.
4. Stable sorted link planner independent of ECS component insertion
   order; fails on >maxLinks, duplicates, stale ECS handles,
   invalid geometry and out-of-world coordinates.
5. Same-tick source->junction->next-hop cannot send newly
   received matter, multi-inlet transfer uses no more original
   source budget than available; exact ledger balance after
   cardinal+junction transfers, leaching and uptake.
6. Native `useNativePhysisWeather`: absent PHYSIS header
   rejects upfront without registration side effects. After
   PHYSIS feature integration, real `sampleNatureField` outputs
   weather and results replay identically across rollback.
   Check invalid atmosphere/out-of-range samples fail cleanly.
7. Existing Stage44–68 CTests, editor/native smoke tests,
   CMake warnings, actual performance and visual QA.

## Next stage

A future Stage70 may introduce a true **4m or adaptive nutrient water
tile** with an explicit versioned migration preserving the Stage68 v2
organic ledger. That is required for mixed water bodies in one old
32m tile, precise confluences contained in a single large tile,
and transport below 4m stream width. Do not silently reinterpret
existing saved 32m indices as 4m. PHYSIS weather feature branch
must be integrated and tested separately before claiming it is
available in a TerraWeave-only checkout.

Always append the next completed stage's specific changed-file list,
commits, tested commands, real results, exact branch head and
follow-up in another handoff file.


## Final branch/readback snapshot (2026-11-19)

The final source paths were individually re-fetched from GitHub:
`TerraWeaveForestNutrientCycle.h`,
`TerraWeaveForestNutrientEcs.h`,
`TerraWeaveNereidNutrientJunction.h`,
`TerraWeavePhysisNutrientBridge.h`,
native `Integration.h`, this Stage69 handoff and
`docs/terrain/CURRENT_HANDOFF.md`; all returned nonempty content
and their current blob SHAs. This is **source readback**, not
compiler, CTest, PHYSIS runtime, GPU or performance verification.

- `cd1c2c7cc090082d28e37d3f1f39070eb1f0d961` —
  updated `docs/terrain/CURRENT_HANDOFF.md` with Stage69 as latest
  handoff and the separation between source implementation and QA.
- `383d6da809037a5f627e639de671414734251290` —
  updated `examples/world/terraweave_native/README.md`
  with opt-in junction settings, subcell river sampling, PHYSIS
  availability gate and native lifecycle limits.
- Last observed `feature/terraweave` HEAD before this
  documentation append:
  `383d6da809037a5f627e639de671414734251290`.
  Compared to `main`: **915 ahead / 305 behind**, diverged.
  PR #24 remained open, Draft, GitHub reported nonmergeable.
  A later commit to this handoff will advance HEAD again.
  **No merge, rebase or CMake/CTest run performed**.

Testing and upstream integration remain a hard gate. In particular,
do not claim that PHYSIS direct nature sampling works in a checkout
where its optional header is absent, and do not claim that 4m
samples create a 4m saved water grid.
