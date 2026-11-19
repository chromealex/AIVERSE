# PHYSIS Stage 24 — Live Editor Commands and Secure Ingress (2026-11-18)

Repository: chromealex/AIVERSE
Working branch: feature/physis-nature-simulation
Draft PR: #25
Main baseline: 9ee6ff6fbf390bd45363bc92c838f492dfe92eee
Previous: docs/agent-handoffs/physis-field-runtime-reconciliation-2026-11-18.md

## Actual implementation commits
- 5619cf750a3845802f0a0e361b626dc1e0730d80: added apps/editor/PhysisLiveEdit.h. Optional FieldEditHost interface plus local field-edit status and command gateway.
- 2c480ed2ddc92754a7c5d87ab0cfca758d32cecb: const Simulation::module accessor, PHYSIS Live Editing editor panel with status, queued tick and disabled reasons.
- d61fa4b22ab2d288a9291a1e5277b39f07ff2de8: physis::admitAuthorizedFieldEdit trusted server ingress with authenticated principal and ACL callback; rollback-aware confirmation.
- d6764c037916691f1502a310f2d68265050c7ce4: tests/physis_live_edit_test.cpp; CTest target physis_live_edit and PHYSIS/Studio workflow registration.
- cb8e3e45a68628c11f23bb58fa819feb6471e986: invalidate previously Applied UI state after superseding edits, not merely rollback.
- This handoff/docs commit: inspect git log -1 -- docs/agent-handoffs/physis-editor-live-edit-2026-11-18.md for final SHA.

## Architecture and API
In apps/editor/PhysisLiveEdit.h:
- FieldEditHost is a separate opt-in interface. Native GameModule implements FieldEditHost, calls installEditableFromMap at setup, exposes nonzero project-specific command type and allows local editor editing, dispatches applyFieldEditCommand from GameModule::applyCommand.
- canSubmit(const Simulation*, string*) refuses missing Simulation, missing PHYSIS State/FieldRuntimeState, unauthorized module and any Simulation with a CommandSink (network mirror/client).
- FieldEditSession::submit(Simulation&, mapJson, error) validates the full authored map fields and existing climate/grid, records desired field-wise fingerprints, queues the command through Simulation::submit and returns Queued. No immediate mutation.
- FieldEditSession::observe(const Simulation*) tracks Unavailable, Idle, Queued, Applied, Conflict and Rejected. Applied means the actual ECS registry revision is expected+1 AND the field content matches. A dropped handler, competing edit, rollback or later changed state removes Applied status.
- apps/editor/windows/PhysisWindow.cpp renders the new panel after map field controls, shows revision, tick and status; the button is disabled without host consent or while pending. Demo World remains isolated. Simulation::module() now has a const overload for capability lookup.

In engine/physis/PhysisFieldRuntime.h:
- admitAuthorizedFieldEdit(Simulation& authoritative, const Command& untrusted, uint16_t hostType, const Principal& connectionPrincipal, Authorizer authorize, uint32_t& scheduledTick, string* error) is a server-only generic host hook.
- It refuses client player=0, incorrect type or forbidden metadata. Host callback authorizes the actual connection principal (must be created by an authenticated server transport, never taken from client JSON/Command.player). It validates full typed field batch and expected revision, re-encodes a NEW trusted system-slot command, then schedules using Simulation::submit. Admission is not application.
- The standard AIVERSE input relay filters player slots but does not expose privileged PHYSIS administrative transport; this stage does not change or bypass that relay.

## Preserved invariants
- PHYSIS genre-independent ECS components and systems.
- Never update World directly from the editor; all actual field changes at Simulation command tick.
- Existing static installFromMap unchanged. New opt-in Stage 23 field registry saved in World, includes rollback state.
- NEREID canonical water, TerraWeave terrain and Box3D rigid bodies remain separate authorities.
- No automatic client-side player=0 system command insertion; no default RTS/FPS module route, no full remote ACK/NACK or admin endpoint.
- Server transport and ACL integration still required for remote sessions.

## Verification status
PASS ONLY FOR SOURCE/GITHUB: commits created; existing editor Simulation API inspected, RTTI-based dynamic_cast is already used elsewhere in editor, new test target appears in CMake and CI configs.
SOURCE TESTS ADDED, NOT RUN: tests/physis_live_edit_test.cpp checks authorized and denied GameModule capabilities, queued/applied state, rollback and superseded status, missing handler, conflicting edits, bad map data, rejecting local sink client, server untrusted player/ACL mismatch and authorized server command admission.
NOT RUN: native CMake/clang/C++ compilation, CTest, CI PASS, macOS GUI mouse tests, external network transport integration, actual multiplayer replay/serialization checks, sanitizer, performance benchmarks. Do NOT treat registered tests as passed.

Commands for next agent:
    ./run-tests.sh --filter '^physis_'
    cmake -S . -B build/physis24 -DCMAKE_BUILD_TYPE=Debug -DAIVERSE_BUILD_TESTS=ON -DAIVERSE_BUILD_EDITOR=ON -DAIVERSE_ENABLE_PHYSICS=ON
    cmake --build build/physis24 --target aiverse_editor aiverse_physis_live_edit_test aiverse_physis_field_runtime_test -j4
    ctest --test-dir build/physis24 --output-on-failure -R '^physis_(live_edit|field_runtime)$'

## Next step
1. Run actual native compile/tests and fix failures before PR #25 is ready. Verify GUI controls in local macOS editor with a real opted-in GameModule, showing queued and applied status at correct ticks. Ensure the default non-opt-in GameModule has a disabled live-edit button.
2. Add project-specific admin request transport and authenticator/ACL to the authoritative server. It must derive principal from authenticated connection/ticket and deliver ACK/NACK with request correlation IDs. Client command.player is NOT authorization.
3. For remote edit UX, add explicit server receipts and an editor adapter bound to the authorized transport instead of the local no-CommandSink gate. Test duplicates, delayed input, rollback/reconnect and conflicting map revisions.
4. Large maps may exceed Stage 23 32760-byte full snapshot command limit; implement atomic upload chunking/typed patches before claiming full-size support.
5. After each further development stage, create separate agent handoff with changed files/commit, actual tests PASS/FAIL/NOT RUN and next step.
