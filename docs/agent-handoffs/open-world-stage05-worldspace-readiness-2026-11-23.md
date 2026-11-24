# AIVERSE Open World — W1 stage 05: world-space collision/navmesh admission
Date: 2026-11-23
Repository: chromealex/AIVERSE
Branch: `feature/aiv20261123_open_world_audit`
Draft PR: https://github.com/chromealex/AIVERSE/pull/116
Area: W1 / OW-01 — **partial; not accepted as complete open-world streaming**
Baseline main verified at stage start: `aec89ef435aba457ebeec24ef196f62a2fbb611d`
Previous: `docs/agent-handoffs/open-world-stage04-gamecore-2026-11-23.md`

## Existing code checked before work

- `engine/game/GameCore.h`: `readyToStep` delegates to the chosen `HostSystems`. In world-space maps that is `actor::ActorHost`; native modules on the grid path are not implicitly installed into ActorHost.
- `engine/actor/ActorStreaming.cpp`: already protects the game tick using `CollisionStream::prepareRegions`. Its interest volumes cover respawns, characters, vehicles and projectile/blast reach. Do **not** replace it with a second collision loader.
- `engine/world/CollisionStream.*`: async verified chunk publication/removal updates the shared `SpatialWorld`. The asynchronous stream and its I/O retries are host resources; they must not change deterministic ECS simulation state.
- `engine/actor/ActorBots.cpp`: bot navmeshes are `lib::bots::NavMesh` built from `SpatialWorld` per authored building, and `BotMemory` is persisted ECS gameplay state. Before this stage they were assembled during setup, potentially before streamed terrain/collision arrived.
- `engine/nav/NavMesh.h`: generic deterministic tile mesh exists; full streaming tile residency is not yet wired into the ActorHost bot navigation.

## Code changes

1. `engine/world/CollisionStream.h` / `.cpp`
   - New `uint64_t revision() const`, host-local derived epoch that only changes on successful collision-chunk publish or published-chunk retire.
   - Existing collision namespace allocation, chunk lifetime, `covers` and load/preparation unchanged.
   - Epoch is deliberately **not serialized or hashed**: I/O completion order differs across peers.
2. `engine/actor/ActorModule.h`
   - Added private `rebuildBotNavigation(std::string* error)` and host-only revision/rebuild diagnostics.
   - Added public diagnostic `navigationRebuilds()`, `navigationCollisionRevision()`, `navigationReadinessError()`.
3. `engine/actor/ActorBots.cpp`
   - Extracted nav-mesh construction into a single, side-effect-limited rebuild routine.
   - On non-async maps, the same navigation is constructed during setup.
   - On async collision worlds, navmesh creation is deferred until required collision chunks are published.
   - Rebuild into a temporary vector, then swap on success. On failure old derived meshes remain untouched; no `BotMemory` reset or RNG changes.
4. `engine/actor/ActorStreaming.cpp`
   - `readyToStep` first requires all necessary streamed collision as before, then checks the publication epoch.
   - Only when bot navigation is configured and the collision epoch changed does it rebuild derived navmeshes and cache that revision. Failed rebuild returns false (no simulation step).
   - Replacing the asset reader forces revalidation; explicit failure reason is exposed.
5. `tests/world/actor_navigation_stream_gate_test.cpp`
   - Native fixture using real world-space GameCore/ActorHost, actual `CollisionStream`, a minimal authored two-floor building, and two independently created simulations. Validates blocked steps and commands, exact RNG/checksum refusal, eventual resumed simulation, one nav rebuild after publication, no redundant rebuild, fresh restore and parity.
   - Independent collision stream epoch tests for publish/retire and idle no-op.
6. `CMakeLists.txt`
   - Added CTest target `aiverse_actor_navigation_stream_gate_test`, test `actor_navigation_stream_gate_test`, linked `aiverse_authority`, labels `World;ECS;NETWORK`.
7. `docs/large-worlds.md`
   - Added W1.5 behavior/limits section.

## Commits

- `9ffa39a031b1eff2118872a9c061aa9291f1a2a4` — revision accessor/field.
- `19eab8c8f3179bc4cd63aeeb1b28c07f4fcf5f91` — epoch increments on publish/retire.
- `90458da02369db10d25aea0976c870399fccf7e2` — ActorHost nav diagnostics and derived cache.
- `0df17cb9562228724269674bed7e28afc38b1732` — deferred nav build and rebuild policy.
- `beff28cbc7588c49c4957f16f110b22f88862df3` — readyToStep collision/nav fence.
- `02ab35492ac8f9eaedb36f137a85722ecbdd0ec2` — world-space focused CTest source.
- `f725b7f24d314ecf409f927676088e00751a9900` — CMake CTest registration.
- `cd86983b3d30e36d18a26f840beee3c392876518` — nav failure diagnostic accessor.
- `646d9636ff8f9d07196de68adf283bed6006a193` — populate/clear nav build error.
- `15d24e9ad4bad3402c230f69a0634376fd0901d4` — large-world documentation.

## API contract

Collision publication revision is a derived host-only invalidation key. It must not be stored in `SimSnapshot`, recorded in a game checksum or used as a source of nondeterministic gameplay decisions. `readyToStep` may delay wall time but completed ticks must be deterministic. Nav rebuild requires the collision stream to have completed publication for every demanded region.

The bot `NavMesh` vector is derived from the current collision world, and rebuild does not alter `BotMemory` (authoritative). Additional nav/collision publication events before the next tick coalesce into a single rebuild. If nav rebuilding fails, the tick must not consume queued commands.

This stage does **not** move any `RegionStableIdentity` ledger records, park live `VehicleDrive` or `Unit`, or create a new region interest scheduler. It closes an integration hole in existing world-space collision admission. Full open-world entity lifecycle still needs a later stage.

## Tests and actual verification

**Actually done**: fetched and inspected current GitHub source of `GameCore`, `ActorHost`, `ActorStreaming`, `CollisionStream`, `WorldAssets`, bot navigation, and the existing `tests/world/async.cpp` gate and template fixture. GitHub accepted code, test and docs commits. CTest source includes assertions for the scenarios listed above.

**NOT DONE**: compilation of new files, CTest execution, full `./run-tests.sh --quick`, testing on macOS/KINGSTON, GPU frames and performance recording. The owner's required writable `/Volumes/KINGSTON` build volume is not accessible in this runtime. Do NOT claim the new test passed. W1 remains **partial** and PR remains Draft.

## Remaining limitations and next steps

- Existing bot mesh is per authored building and outside buildings uses simple steering. This is NOT a streamed outdoor navmesh, nor dynamic obstacle/off-mesh-link rebuild. Only collision changes through `CollisionStream` bump this epoch; destructible door/wreck updates from other systems still require explicit invalidation.
- World-space ActorHost does not yet install the W1 hot/cold ECS lifecycle via project-native modules; stage04 tested a real grid GameCore native module. A future shared setup hook/data capability should support world-space without inventing a second simulation host.
- Streaming bots may now defer a navmesh build until collision readiness and subsequently rebuild when geometry is updated; this can increase CPU work and needs p95 timing evidence.
- Build error and behavioral regression risk are unmeasured. Source test currently creates a minimal authored building and two synthetic streamed terrain tiles. If the default building/mesh bounds need adjustment after actual run, fix in the same branch; do not weaken the assertions or mark a false PASS.
- No full vehicle + occupant group collision admission, offscreen simulation, disk-backed persisted regions, crash recovery, cross-chunk navmesh mesh authority, or RAM residency proof.

### Next agent: testing first

On the user's Mac with KINGSTON writable, on the latest merged tree:
```bash
cmake -S . -B /Volumes/KINGSTON/AIVERSE-builds/open-world-w1/Debug -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON
cmake --build /Volumes/KINGSTON/AIVERSE-builds/open-world-w1/Debug --target aiverse_region_entity_ledger_test aiverse_region_ecs_bridge_test aiverse_region_cold_store_test aiverse_region_gamecore_integration_test aiverse_actor_navigation_stream_gate_test -j 6
ctest --test-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-w1/Debug -R '^(region_entity_ledger_test|region_ecs_bridge_test|region_cold_store_test|region_gamecore_integration_test|actor_navigation_stream_gate_test|world_async)$' --output-on-failure
./run-tests.sh --quick --build-dir /Volumes/KINGSTON/AIVERSE-builds/open-world-w1/Debug
```
Record actual branch/SHA, exact failures and pass counts. Do not merge into main until green. Then implement W1.6: real world-space ECS hot/cold activation, supported component owners, cross-region vehicle/occupant admission and navmesh dirty tile policy. Keep independent stage handoff and document what content/template version was tested.

No 3.1.0 release claim; no devlog screenshot from this nonvisual, unverified source checkpoint.
