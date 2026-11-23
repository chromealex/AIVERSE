# Studio HTML5 — Stage 109: preserve linked map references on delete

Branch: `feature/world-processes-experimental`
PR: https://github.com/chromealex/AIVERSE/pull/112 (DRAFT)
Date: 2026-11-23

## Scope

Map objects authored with World Processes links now resist accidental
single or batch deletion of their target when a surviving object still
references it. Uses the existing StudioSession`studio.entityDelete`
and `studio.entitiesDelete` RPC paths; no new UI panel or browser
operation. A rejected edit reports the linked component and map ID,
preserves the map, and makes no Undo entry.

`apps/studio-native/StudioSession.cpp` change commit:
`7d339e28fdbc331a7baa443804812f778071a4aa`.
`apps/studio-native/tests/StudioSessionTests.cpp` source tests:
`381cc15b39ac901bf0340a4664e819ca2f624519`.
The full architecture, API, test execution status and remaining limits
are in
`docs/handoffs/world-processes-stage10-reference-integrity-2026-11-23.md`.

## Validation and handoff

Source and tests committed. New Studio native test has NOT been run
successfully in a confirmed workflow at handoff creation time. Last
known successful Stage05 focused suite is older and does not validate
this code.

Next: run `StudioSessionTests` on Mac with a real project, verify
deletion refusal, search and drag in EntityReferencePicker, Undo/Redo,
and correct behavior when **all** referencing objects are removed in
one group operation. Capture a real Studio screenshot, then integrate
this guard into other MapDoc deletion/rename entry points without
undoing concurrent changes from `feature/studio-html5`.
