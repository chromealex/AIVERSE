# Open World Framework — stage 01: persistent dynamic region ownership
Date: 2026-11-23
Repo: chromealex/AIVERSE
Branch: `feature/aiv20261123_open_world_audit`
Baseline main: `aec89ef435aba457ebeec24ef196f62a2fbb611d` (2026-11-23)
Stage-00 handoff: `docs/agent-handoffs/open-world-stage00-source-audit-2026-11-23.md`
Capability area: W1 / OW-01 (still **partial**, NOT accepted)
Status: first C++ policy implemented, source committed, CTest registered, Mac integration verification pending.

## Stage commits (ordered)

1. `1dd4ce95fa1ea43c658f8908b8ccd4bf11d99025` — initial in-progress `world` roadmap entry in `docs/devlog/systems.json`.
2. `36ed7a5b67616673cb7300eb6c00fa82766bb5bf` — `engine/world/RegionEntityLedger.h`.
3. `8e62031258146c08e9faaf39595d2c273d53bcd6` — `tests/world/region_entity_ledger_test.cpp`.
4. `2c42895bad0ca873270fe852497999d3032690e7` — CMake/CTest registration.
5. `2368bde4a81247845bb4c7144f8bdd020e5ef2c6` — `docs/region-entity-ledger.md`.
6. `3bbc1bb831ef5883cf3b8287498e8510baea3665` — `docs/large-worlds.md` links and authority-vs-residency notes.

The stage handoff commit follows separately. The branch is a draft PR and has not been merged into `main`.

## Public API

`engine/world/RegionEntityLedger.h`:

- `RegionEntityId` (host-assigned stable nonzero `uint64_t`, not transient ECS index).
- `RegionEntityRecord {Cell owner; uint64_t revision; uint8_t live;}`.
- `RegionEntityLedger::spawn(id,cell)` — does not allow reuse, including tombstones.
- `transfer(id,expected,destination)` or `transfer(span<id>,expected,destination)` — atomic group relocation, sorted IDs, rejects stale source/duplicate/missing/dead IDs.
- `despawn(id,expected)` — durable tombstone.
- `find(id)`, `activeIn(cell)`, `since(revision)`, `lastRevision()`, `size()`.
- `io(Archive&, RegionEntityLedger&)` — bounded, versioned, deterministic, transactional decoder with duplicate ID and revision checks.
- `registerRegionEntityLedger(sim)` — optional native ECS persistent resource schema `aiverse.world.region_entity_ledger` version 1.

`registerPersistentResource<T>` integrates serialization, checksum and COW rollback via existing native ECS v10. The host calls `sim.world().setResource<RegionEntityLedger>(...)` during setup; it owns stable ID-to-Entity mapping separately. Zero or invalid IDs are rejected.

**No existing ECS system automatically changes its ownership or save data.** Unregistered projects keep their previous checksum/format; opt-in saves use native ECS v10. No representation of a live entity's components, collision, navmesh tiles, animation or asset lifetime is stored in the ledger.

## Tests added and evidence

Added CTest target `aiverse_region_entity_ledger_test`, name `region_entity_ledger_test`, labels `World;ECS`; source `tests/world/region_entity_ledger_test.cpp`.

Coverage written: invalid/duplicate ID; atomic grouped transfers with reversed input ordering and wrong-region refusal; no-op transfer; tombstone/no re-spawn; revision delta; archive corruption/truncation and byte roundtrip; 100 crossings; native persistent ECS registration, state save/load, COW snapshot rollback, parallel peer hashes, fresh restore.

**Actually executed:** Linux clang++ C++20 `-Wall -Wextra -Werror -pedantic` stand-alone smoke using a separately transcribed ledger and minimal stand-ins for `Archive` and `Cell` declarations, without full engine checkout. Result: `PASS standalone W1 C++ ownership/transfer/archive/tombstone smoke`. This validates core algorithms and syntax under those stand-ins only; it is **not** a build or run of the checked-in test target or actual engine native ECS.

**Not executed:** CMake configure/build of full AIVERSE; actual `region_entity_ledger_test` CTest; Mac `./run-tests.sh --quick`; capture; 100-boundary ECS live world; collision/navmesh and dynamic component streaming; full merged-tree integration; performance measurements. No Mac or user KINGSTON volume in current runtime (checked: `/Volumes/KINGSTON` unavailable). Do not report these as passed.

**Source verified:** After commits, fetched the branch's CMake registration, header, test source, world roadmap status and large-world docs through GitHub. Both files and CTest registration exist on the draft branch. This is source presence, not native build acceptance.

## Architecture choices

- One generic owner under `engine/world`; no project/genre gate.
- Deterministic ownership ledger is authoritative state. `ChunkStreamer` is view/asset scheduling; the two must not drive each other.
- Batch transfers are validated before mutating any record. Stable ID order ensures reproducible bytes/checksum despite input order.
- Records are not evicted on presentation unload. Tombstones survive so stale deferred creation cannot resurrect an entity.
- Initial `std::map` is deliberately simple: `activeIn`/`since` are O(number of tracked IDs) scans; do not poll for every cell in each tick.
- V1 codec schema + persistent ECS registration. New schema is opt-in only; no current shipped game is migrated by this code.
- Reference game `chromealex/gta` remains untouched; existing private-engine patches are not imported.

## W1 still missing — next stage 02

1. **Mac acceptance gate first:** on a *current, merged* AIVERSE worktree run CMake target and CTest plus quick full tests on `/Volumes/KINGSTON/AIVERSE-builds/<branch>`. Fix actual compile issues, log SHA, failure owner and counts in a new handoff.
2. Add an ECS `RegionStableIdentity` component (persisted) and a generic adapter mapping the stable ID to a live `Entity`. Route actual Transform-based movement through this ledger; do not create a parallel host.
3. Develop authoritative hot/cold region lifecycle: activate/evict component payloads transactionally, preserve long-range world state and restart determinism, and never despawn entities because render LOD changed.
4. Add collision/navmesh loading fences, bounded source-of-truth region save journals and atomic group release for occupied cars. Integrate with `readyToStep`, which must refuse a step without consuming RNG/commands.
5. Provide synthetic region fixture and real two-observer + teleport + failed-load + restart + 100 crossings tests. Ensure different `ChunkStreamer` load budgets yield the same simulation checksum.
6. Only then consider adapting generic policies from GTA2. Its main is pinned to legacy `RtsModule` and private copy patches; do not apply these to current `main`.
7. Update capability registry and world-site roadmap to `done` only after full tests and integrated proof. For visible stage, capture actual native frame and provide live web demo per AGENTS rules.

## Release and handoff

No release-ready W1 feature yet. No screenshots/website entry: this stage is a nonvisual internal correctness slice without a tested native runtime. Main integrator should keep draft PR #116 open until target build and acceptance. The next agent should continue this branch or rebase it onto latest main, take ownership only of W1 (not simultaneously start other area).

Unresolved question, not a design blocker: the host's stable ID assignment and dynamic component parking format need an explicit policy, compatible with native ECS saved Entity generations, before streaming across unloaded regions.
