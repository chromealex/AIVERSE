# PHYSIS Stage 23 — authoritative field reconciliation (2026-11-18)

Repository: chromealex/AIVERSE  
Branch: feature/physis-nature-simulation  
PR: #25, DRAFT  
Main baseline: 9ee6ff6fbf390bd45363bc92c838f492dfe92eee is already an ancestor  
Previous Stage: docs/agent-handoffs/physis-editor-marquee-3d-gizmos-2026-11-18.md  
Independent planner: docs/agent-handoffs/physis-field-change-planner-2026-11-18.md

## Implemented code commits

- 24dcc8b9873b6ab3c3683f6696742e71cb3ffefc — adds engine/physis/PhysisFieldRuntime.h, opt-in live typed field registration, stable authored IDs, stage-copy reconcile and Command payload codec.
- ca337507165b41d1626a903ef2fad6d9d59b2b8a — adds tests/physis_field_runtime_test.cpp; CMake physis_field_runtime; PHYSIS Linux/macOS + Studio editor validation workflow targets; fixes const edit-batch archive serialization.
- 99db9ac7cf10fd4dae71b1be5b8d5fe107d05cb7 — adds submitEditableFieldsFromMap host command scheduling API and CTest source fixture.
- 926b508e07fc7d2b55fcc4b95055e22a878ce7c4 — respects Simulation readCommand 4096-argument limit and adds actual wire-format roundtrip / oversize tests.
- 60db0be1e5147ae05b4bb8338c62b0ecfb6075bd — rejects climate/grid changes through field-only submission, adds no-op revision fast path / test.
- This handoff: inspect git log -1 -- docs/agent-handoffs/physis-field-runtime-reconciliation-2026-11-18.md for final docs commit.

## Architecture, invariants and API

The existing engine/physis/PhysisFieldChangePlan.h was created by another agent on the same branch (commits d4bed2b5, 717b7b50); it remains pure sorted Add/Update/Remove planning. The new Stage 23 runtime composes that planner. It introduces no dependencies on any FPS/RTS/game genre, editor, Box3D dynamics, NEREID water or TerraWeave foliage.

The default installFromMap remains unchanged. A project must explicitly opt in with physis::installEditableFromMap(sim, map) inside its GameModule::setup. This calls normal PHYSIS install, enables all three component schemas (WindZone, StormEvent, GravityField) and registers opt-in persistence resource physis.field_runtime v1 before setup seals. Such saves have a DIFFERENT strict manifest; old static saves cannot be loaded into the new opt-in configuration automatically.

FieldRuntimeState is a World resource with revision and std::map<authoredId,FieldRuntimeEntry>. Each entry keeps generation-safe Entity, AuthoredFieldKind and field-wise Archive binary fingerprint of the AUTHORED source. Dynamic storm drift does not cause spurious updates, because unchanged values compare the authored fingerprint rather than current runtime ECS values. Update with same kind preserves Entity, type change is Remove+Add with new generation, removal destroys entity, additions allocate sorted. All field components and registry are staged in an independent World copy, then committed in one assignment. A no-op command still increments revision without a deep World copy to reject duplicates.

Generic host API:
    bool physis::installEditableFromMap(Simulation&, const Json& map);
    bool physis::submitEditableFieldsFromMap(Simulation&, const Json& editedMap,
        uint16_t hostType, uint32_t& scheduledTick, std::string* error);
    bool physis::encodeFieldEditCommand(const FieldEditBatch&, uint16_t hostType,
        Command& output, std::string* error);
    bool physis::decodeFieldEditCommand(const Command&, FieldEditBatch& output,
        std::string* error);
    bool physis::applyFieldEditCommand(Simulation&, const Command&,
        uint16_t hostType, std::string* error);

The project reserves a distinct command type and dispatches applyFieldEditCommand exclusively from GameModule::applyCommand. Simulation applies commands at tick start, before running ordered systems; existing pending command/snapshot/replay and CommandSink infrastructure transport the request. Host must authorize command type and player=0 from trusted server; packet.player=0 alone is NOT authentication. submitEditableFieldsFromMap only QUEUES, it does not ACK an applied edit. Authoring GUI live-play button and application confirmation have not been added.

The full replacement batch includes expectedRevision + sorted typed sources. IDs are unique ASCII and types/ranges validated. Source IO uses field-wise Archive (no ABI-padding memcpy). Edit command length <=32760 bytes (4095 data int64 words plus one length word), because Simulation::readCommand has a hard 4096-args limit. Large replacement batches reject; there is NO multi-packet atomic protocol or incremental delta encoder. The standalone authored parser can hold up to 2048 sources, but not necessarily all fit a single network command.

Only sources change: field centres, radii, wind vectors, gravity vectors, tornado parameters. No climate grid/season/flood policy reinstallation; submission rejects edited grid/climate parameters. Do not advertise full climate hot reload, inspector automatic dispatch or NEREID/Terra integration. Rebuild transient FieldSpatialIndex after accepted changes/rollback; stored indexes are read-only snapshots.

## Verification

- PASS: GitHub connector committed source files and CTest/CI declarations. Re-read critical Simulation::step, World allocator/snapshot API, persist serialization, ByteWriter/readCommand cap. Patch/source braces checked during editing.
- TESTS AUTHORING ONLY, NOT RUN: command protocol encode/decode; actual Simulation network wire read/write; invalid/oversize payload; two-peer deterministic replay; updating and removing with Entity lifetime checks; type change; rollback SimSnapshot; native save/load; rejected stale/unauthorized commands; Simulation::submit scheduling; rejected climate/grid hot edit.
- NO NATIVE C++ COMPILATION, NO CTEST/CI PASS, NO SANITIZER OR MACOS EDITOR/NETWORK INTEGRATION RUN. Do not mark this PR ready. GitHub Actions PHYSIS checks previously observed QUEUED; no success logs.
- Build from native checkout:
      ./run-tests.sh --filter '^physis_'
      cmake -S . -B build/physis -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_ENABLE_PHYSICS=ON
      cmake --build build/physis --target aiverse_physis_field_runtime_test aiverse_physis_project_config_test aiverse_physis_integration_test -j4
      ctest --test-dir build/physis --output-on-failure -R '^physis_(field_runtime|project_config|integration)$'

## Next steps

1. Native compile and run physis_field_runtime before claiming any PASS; fix compiler/serialization/CTest issues. Verify with sanitizers, save/replay on macOS/Linux and mixed COW snapshots.
2. Integrate a trusted GameModule command type and edit acknowledgement into the generic editor host using the existing net session/CommandSink authorization. Do NOT make editor choose arbitrary magic command IDs or apply World writes directly; supply a host capability/command router. Add explicit conflict feedback to UI.
3. Consider bounded atomic chunked transactions or small incremental typed delta patches for map edits that exceed current 32760-byte wire limit. Define exactly-once and revision semantics before implementing.
4. Add server-side permission/auth checks for system-origin commands; reject forged Command.player=0 at untrusted network entry. Test actual connected sessions, resends, rollback, prediction, delayed/duplicate commands.
5. Decide migration strategy for old static PHYSIS saves before shipping opt-in live field schema broadly. Climate/other policies need separate authority-safe migration.
6. Retain PHYSIS/NEREID/TerraWeave/Box3D separation; new handoff file after every next stage with concrete SHA/test evidence and no inferred success.
