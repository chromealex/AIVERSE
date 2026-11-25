# TerraWeave Caverns — Stage 40 native client / Editor Play, headless source and recovery handoff (2026-11-24)

## Provenance

Repository `chromealex/AIVERSE`; branch `feature/terraweave-caverns-core-20261123`. **Not merged to main.**

Actual Stage40 GitHub commits:
- `6a381997e0eab793f0c4183b8608aca1b6823938` — new `engine/world/TerraWeaveCavernsClientVisualHost.h`, frozen-SDF async visual stream owner
- `b3c0dfc0197a6758d044fe1eae312bf7e2138a4f` — `engine/render/TerraWeaveCavernsShardCatalog.h`, complete-shard source retirement preview
- `b95027be8aa290c52b02471a98fed695f11f5b40` — `engine/render/TerraWeaveCavernsShardGpuHost.h`, stage visual retirement without deleting unrelated shard sources
- `5ba7871d658568db4f2c475a001aef8ec7e57d46` — frame-paced retire and residency ACK in visual host
- `6f407e07183c8c3af8958abdb2cd3201af87f6d6` — `apps/client/GameClient.h`, actual common GameClient visual owner
- `d4bcc22577253eee8db4ff0b976f787b90feca2d` — `apps/client/GameClient.cpp`, authoring map open, pre-render GPU stage, primary+LENS camera draw and GPU-safe shutdown
- `c6607b0cdb77ba6530b6069749412df003522f11` — new `engine/world/TerraWeaveCavernsWorldRecovery.h`, quarantined full-world recovery session
- `b090cb558de3cf6ef4617f7e9ef7f2fa39195c73` — GameClient visual host uses monotonic runtime SDF revision for Undo
- `de1b5ab2348000880b67c7ddf0f9066aea4581ab` — public GameClient live SDF update API
- `880aa79f4e766de55b8c54080f15dc50adff752a` — implement `GameClient::updateCavernsVisual`
- `5f54b12b1a7ef9a5c2c5d24ba56e321bff508e5f`, `2189533bd0aea1d0ad7b224b9c932c2d82bbc6e3` — HostedClient forwarding API to actual active GameClient
- `cf941c73ac7d5aa793d387c7fee00a28d602eabd` — Editor MapDoc observer forwards cave authoring/Undo to Editor Play GameClient
- `df8a0344fb9bbf1efdd576d7f749a66b3b4bf996` — cross-shard multi-frame visual staging with ONE final publication instead of per-key crack-prone immediate activation
- `f6644d83e821b41977af06dabb8081758164dc15` — defer visual retire tickets while connected source batch partially staged
- `307e284e3a391559f336bf6066cef0f66f527f7f` — new `engine/world/TerraWeaveCavernsHeadlessWorldSource.h`, deterministic no-GPU revisioned SDF projection for simulated authority/replay
- `df17ab7f123e8177dcdda7c64b8b3c4d1cdc60ee`, `dd4491b9a17c2e01e7a1ba17e41145c553b34be7` — client streaming focus now uses actual authored vertical cave range
- `d56afc2582e54cf513b9ee81a5153f2962401b1c` — `AGENTS.md` ownership routing (GameClient/Editor Play/headless vs relay server)
- `75d2baee49cb64e1300c6b2d40425ac1ff5de29f` — `skills/world/aiverse-terraweave-caverns/SKILL.md` update
- `7a49a63104523e84b27f9a1a4bcd6dbfc8685fb8` — signed narrow cave vertical range safety fix
- This dated handoff is committed afterwards; inspect branch HEAD for exact final SHA.

## Functional changes and APIs

### Actual shared GameClient and Editor Play visual integration

The existing `apps/client/GameClient` is also used inside `apps/client/HostedClient` for Editor Play. Stage40 adds `std::unique_ptr<CaveClientVisualHost> cavernsVisual_` to GameClient, with an out-of-line destructor so a forward declaration suffices in the shared header. `GameClient::startMatch` reads `map["terraweaveCaverns"]` or `map["extra"]["terraweaveCaverns"]`, parses the versioned authored cave SDF and opens one visual-only streaming owner. Existing maps without that authored block are unaffected.

`CaveClientVisualHost` uses immutable Stage39 SDF spatial indexing, Stage36 camera-centered signed chunk residency, bounded worker-based visual/full collision mesh generation and Stage30/37/38 complete-shard GeometryScene GPU staging. GameClient invokes `beforeRender` outside `r3d_.beginScene` and `draw` only in the normal Renderer3D scene submission path, plus the separate LENS prepared-camera path when active. No second legacy cut-terrain cave draw is added. The Y focal chunk comes from the authored SDF vertical extent, avoiding a hard-coded underground depth. The host does not create gameplay Box3D bodies, does not touch SpatialWorld or NavMesh, and does not mark a gameplay edit ACKed.

A camera stream can retire a source only by building a full replacement shard with that source removed, preserving unchanged neighbors, then calling editor-style **visual-only** `activateVisualOnly` and acknowledging the exact stream retire ticket. The old GPU GeometryScene remains a bounded retained owner until fence/idle proof.

**Mixed LOD:** Instead of publishing each completed chunk independently, the visual host groups currently completed CPU stream results, negotiates one full connected Stage37 LOD plan with unchanged resident neighbors, stages each affected GPU shard across multiple render-owner frames (Stage38 upload budget), and activates ALL staged shard sources at once. Unsupported divergent SDF contours are refused, leaving previous visuals in place until a compatible source set is built. On camera/revision invalidation, pending staged sources are canceled. The original full-resolution `collisionMesh` is never silently substituted by coarsened visual geometry for gameplay.

**Editor Play hot reload:** `GameClient::updateCavernsVisual(Json)` accepts a newly parsed MapDoc cave block and replaces the frozen source. `HostedClient::updateCavernsVisual(Json)` forwards only when active/in-match, restoring hosted platform/UI context. `Editor::CavernsEditHost::update` forwards actual SDF changes observed from MapDoc/Undo/Redo. Stored sourceRevision may move backward after Undo, so `CaveClientVisualHost::replaceSnapshot` creates a strictly increasing runtime revision. Existing stale workers retain frozen prior snapshots and cannot publish their obsolete ticket. This remains a **visual-only preview**, not an in-game cave collider update.

**World GPU lifetime:** Before switching maps `GameClient::startMatch` closes the previous CaveClientVisualHost after an explicit GPU idle proof; refusal aborts map switching instead of deleting live GPU owner resources. `GameClient::shutdown` also attempts GPU-idle shutdown. When the borrowed graphics device cannot certify completion during final shutdown, the old cave owner is intentionally quarantined rather than destructed while GPU work may still reference it. Resource safe-retention is preferable to use-after-free; this path needs native device-loss acceptance.

### Headless authority and replay

`CaveHeadlessWorldSource` is an independent no-RHI/no-editor cache of a canonical immutable authored snapshot. `load`, `prepare(source,bounds)`, `commit(exactRuntimeRevision)`, `abort`, `densityCm`, `mesh`, `committedSnapshot` and `committedIndex` allow an actual simulation or replay owner to query identical integer SDF while controlling logical-tick revision changes. It does not duplicate ECS command authority; the owning ECS world decides which edit commits.

**Important architecture:** `engine/net/server/Server.h` states that the existing `net::Server` is a lockstep input relay and does NOT run Simulation. Therefore Stage40 does **not** instantiate a cave GPU renderer, Box3D simulation or separate parallel game state inside `apps/server/main.cpp`; that would be incorrect. A simulated authoritative match host can use `CaveHeadlessWorldSource` and the Stage34/35 `CaveGameplayRuntimeFrame` with its actual physics/NavMesh owner.

### Error recovery from partial native commit

`CaveWorldRecoverySession` owns the running `CaveGameplayRuntimeFrame`. After a genuinely successful all-owner game commit, `rememberCommittedSource()` keeps the last authoritative immutable SDF; it rejects uncommitted revisions. Upon `RecoveryRequired`, `beginRecovery(fullLoadedBounds)` quarantines the old world and creates an independent replacement runtime from the last GOOD source rather than reusing possibly destroyed native Box3D handles. The native owner then calls `rebuildNative(actualWorldRebuild)` to fully reconstruct a NEW physics::Scene, SpatialWorld, NavMesh and GeometryScene from that SDF and perform its actual owner commit. Only after the replacement reports a committed revision does `promote(device,exclusiveProof,actualWorldSwap)` request old GPU-idle teardown, an external full native-world pointer/reader-epoch swap under true exclusivity, and transition back to `Active`. Failure stays in `Quarantined`/`Failed`.

**Recovery is not magic:** the new helper is a real lifecycle state machine, but actual native world reconstruction is supplied by a host callback because it must also recreate unrelated physics bodies/characters and ECS state. A callback returning true without doing so is invalid. Stage35 Box3D old-body destruction remains irreversible in some failure cases; the proper response is full authoritative rebuild, not silently continuing gameplay.

## Known limitations / remaining feature gaps

1. **GameClient integration is VISUAL ONLY.** The normal GameClient does not yet own shared Box3D + SpatialWorld + nav objects in a single native world barrier. Stage34/35 expose staged gameplay resource APIs and Stage40 adds external recovery, but real in-game character collisions and nav updates on SDF edits are NOT activated from this GameClient integration.
2. `net::Server` is only a relay, and its game authority belongs to a simulation/replay host. The headless source is available but the actual simulated authority or ECS host must still instantiate it and route authoring commands. Do not describe Stage40 as adding cave physics into the relay server.
3. `CaveClientVisualHost` currently waits rather than fabricates a surface when a connected mixed-source LOD seam is incompatible. It still needs an automatic shared-SDF repair scheduler for streamed mixed-LOD neighbors if that situation persists. GPU work uses Stage38 bounded whole-shard uploads, not true intra-shard paging. STRATA sidecar retirement still requires a full GPU idle proof.
4. Maps need a versioned `terraweaveCaverns` authoring block. Existing maps without it are unaffected. Correct treatment of top-level versus nested MapDoc export should be verified in a full checkout.
5. Physics replay/rollback, player and NPC collision, full nav spans, world recovery of non-cave dynamic bodies, GPU lifetime on device loss and real multi-backend rendering all need native acceptance. No FPS/RTS-specific cave module is introduced, per AGENTS.md.
6. The user requires **features first, tests later**. Source was inspected/updated through the GitHub connector, but **NO native compilation, CMake/CTest, Editor Play launch, GPU capture, Box3D, SpatialWorld, NavMesh or headless server test was run** in this stage. GitHub commit acknowledgements do not establish successful build or runtime correctness.

## Next steps

1. Complete the true native gameplay owner that shares actual Box3D/SpatialWorld/NavMesh with scene actors and calls Stage35 exclusive publication from a safe between-tick/render boundary, not an independent cave physics world.
2. Implement automatic streamed-source common-lattice repair for incompatible neighboring SDF epochs and bounded GPU page splitting for over-budget shards; then finish full STRATA sidecar fence-safe teardown or choose an explicit owner-idle shutdown policy.
3. Attach headless cave authoring to actual deterministic ECS simulated matches, not the network relay, including serialized/replayed authoring commands and their integer SDF source revision.
4. Once **all** functions are complete, run the full feature-branch compiler/CMake/CTest suite, actual Editor Scene/Game tab + native GameClient, Metal/Vulkan/GLES and game collision/nav QA. Fix issues before merging to `main`. Continue per-stage skill + independent handoff for each further stage.
