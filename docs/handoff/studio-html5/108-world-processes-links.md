# Studio HTML5 — Stage 108: linked ECS object picker for world processes

Date: 2026-11-23
Branch: `feature/world-processes-experimental`; PR #112 DRAFT.
Follows Stage107 guarded world-process authoring and Stage106 Live Inspector.

## Done (source committed)

Existing Scene Inspector (no redesign) can add and configure
`EchoPortal`, `EchoEmitter`, `EchoReceiver`,
`FractureBond`, `AetherDuct` and `AetherFlux` using the same
native `studio.componentAdd`, `studio.setProperty`,
`studio.setProperties`, `studio.componentRemove` and
MapDoc Undo/Redo/Save paths as Stage107.

The reusable `src/components/EntityReferencePicker.tsx/.css`
uses native-provided options to display a searchable object list
and supports dragging objects from Hierarchy's existing
`application/x-aiverse-entity` MIME. Only stable
`entity:@<map id>` refs are accepted; bare ordinal refs never
become persistent links. New `entityReference` field metadata is
handled by the shared `PropertyField`. Colors, dimensions and
focus use the Studio `--au-*` design tokens; no separate palette
or editor shell changes.

Native `StudioSession` filters choices by real, authored target
component type (ECHO zone, FRACTURE member, AETHER cell) and
revalidates every write. A linked multi-property edit is preflighted
as a whole and committed as one Undo step; invalid references,
duplicates and schema values are rejected without partial mutation.
Removing a component referenced by a link is prohibited until
its incoming link is cleared.

The native project's `world_processes` module in
`examples/native/world_processes` resolves those persisted
IDs to generation-safe ECS Entities in two passes at game setup;
authored ECHO receivers acquire their runtime hearing result
component.

## Paths / commits / detailed API

See `docs/handoffs/world-processes-stage09-linked-authoring-2026-11-23.md`
for every changed source/test path and exact commits. Principal
Studio edits: `apps/studio-native/StudioSession.cpp`,
`apps/studio-native/tests/StudioSessionTests.cpp`,
`apps/studio-web/src/{propertyLogic.ts,components/PropertyField.tsx,
components/EntityReferencePicker.tsx,components/EntityReferencePicker.css}`.
Handoff SHA:
`git log -1 -- docs/handoff/studio-html5/108-world-processes-links.md`.

## Tests and limitations

Source tests written:
`tests/world_processes_linked_test.cpp` (pure ECS host),
`tests/world_processes_gamecore_native_test.cpp`
(real GameCore native module, currently tests standalone fields),
and `apps/studio-native/tests/StudioSessionTests.cpp`
(native Studio object references, Undo/Redo, disk save).

**No Stage108 native macOS GUI run, actual screenshots,
frontend TypeScript build or green latest CTest evidence has been
confirmed yet.** Earlier Stage05 focused ECS GCC/Clang/ASan PASS
does not verify this editor change.
Full `main` merge quick test remains mandatory; never claim
completion based on source commits alone.

Current missing functionality: graph-wide reference maintenance
on entity deletion/rename, content asset GUID picker for object
data that targets content rather than map entities, viewport
linked-object gizmos, actual physics/FX presentation and
linked-map GameCore template screenshot proof.

## Next step

Inspect focused CI and Studio HTML5 CI on current commit;
fix real warnings/type failures. Then use the author's Mac:
edit a real project with 2 acoustic zones + portal, 2
structural members + bond, 2 gas cells + duct;
verify picker search, drag, disabled/unbound states,
Undo/Redo, save/reopen, and ECS Live Inspector results.
Capture real Studio images at 1440×900 and compact width.
Respect the selected Studio cinematic design, avoid new ImGui,
and coordinate concurrent `feature/studio-html5` work.
