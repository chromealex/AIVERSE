# AIVERSE world processes — Stage 09: stable map-object link authoring (2026-11-23)

**Repository:** `chromealex/AIVERSE`
**Branch:** `feature/world-processes-experimental`
**Draft PR:** https://github.com/chromealex/AIVERSE/pull/112
**Main:** NOT merged; do not merge before full validation and reconciliation with the HTML5 Studio owner's branch.
**WIP:** Only ECHO, FRACTURE and AETHER; the remaining seven world-process proposals are still Stage01 pure-policy prototypes.

## New capability — what was actually authored

The Studio Scene Inspector can now author connections between real map objects and have the existing, opt-in `world_processes` native module resolve those links into generation-safe ECS `Entity` handles during setup. No genre-specific variant, second authoritative state, approximate-position linkage or bare ECS index persistence.

### Stable map ID → ECS generation-safe reference

- **New:** `libraries/include/aiverselib/world/WorldProcessesLinkedAuthoring.h`: standalone validation, defaults, authoring mutation and ECS application for six extra component types:
  - `EchoPortal`: `from`, `to`, `loss`, `bidirectional`, `enabled`
  - `EchoEmitter`: `zone`, `intensity`, `enabled`
  - `EchoReceiver`: `zone`, `threshold`, `enabled`
  - `FractureBond`: `first`, `second`, `strength`, `damage`, `enabled`
  - `AetherDuct`: `from`, `to`, `maximumPerTick`, `oneWay`, `enabled`
  - `AetherFlux`: `cell`, `smoke`, `toxin`, `oxygen`, `enabled`
- New linked components start disabled with empty references. Enabling requires nonempty target IDs and distinct ends for two-ended links. Input schemas are exact; unknown keys, wrong types, malformed integer literals, over-limit rates/damage and impossible bond strengths fail closed. No runtime output fields are exposed to project authoring.
- `libraries/include/aiverselib/world/WorldProcessesMapHydration.h` now does two-pass map validation and ECS writing: validates every authored component, detects duplicate or stale map IDs, requires link-compatible endpoint types, enforces counts/budgets, registers only the required opt-in ECS schemas, installs standalone components, then links. A transient `map id -> Entity` lookup is created during setup only, never stored as competing simulation authority.
- Authored `EchoReceiver` entities also acquire an `AcousticHearing` output component; ECHO writes that actual ECS component after portal propagation. Authoring never writes hearing results, collapse outboxes or PHYSIS climate.

### Editor and user interaction

- `apps/studio-native/StudioSession.cpp`: existing `studio.componentCatalog/add/remove/setProperty/setProperties` supports the six new linked components. Reference options are sourced from *existing* map entities with the compatible authored target component. The native editor rejects unknown IDs and wrong component types on writes; duplicate target identifiers are refused by map hydration. For links, batch `setProperties` validates final state and applies it as one MapDoc Undo step without permitting invalid intermediate states or partial writes.
- `apps/studio-web/src/components/EntityReferencePicker.tsx` and `.css`: small reusable searchable object selection popup, clear action, stable ID text and drag-drop from Hierarchy using its existing MIME `application/x-aiverse-entity`. Ordinal `entity:<number>` references are intentionally not accepted by drag; only `entity:@<stable-id>` references can be dropped. Rendering uses the shared Studio `--au-*` tokens, not a new palette.
- `apps/studio-web/src/propertyLogic.ts`: adds `entityReference?: boolean` on typed property metadata. `PropertyField.tsx`: routes such text fields into the shared object picker, with the ordinary native bridge write path. No new untrusted JS-to-C++ IPC method.
- Removing a referenced **component** is prohibited until its incoming links are cleared. Deleting the whole owning map entity, renaming its stable ID, and advanced mass-reference repair are not yet hooked into all document operations; unresolved references fail closed when the game loads, and need a later conflict-repair pass.

## Changed files and commit ledger

- `2816eac5b7aa4396c47b14d25942111a1616cd19`: new linked authoring schema and ECS adapter.
- `9a0cce8b9de3f0a299f4b66982f2f4c41d047577`: map two-phase validation, generation-safe reference resolution.
- `1b0f28cacdd7eca3537339d8d612665707411b35`: add `AcousticHearing` to authored listeners.
- `8606b8f2cb2b66137283a421833fd0ef8011f275`: cross-system linked ECS test.
- `deaa23210047034e6cfad516fc1ae73f90eeca22`: focused test runner.
- `b2d45ce6e0aea57a219a9d6bf037fe26d9554d5f`: workflow trigger for linked tests.
- `79720f379b7a11955cbab7f39b0201c244dcfd1c`: CMake + CTest for linked test.
- `cdbf6cec6f2f4b82756e88182060c80f4a6690fb`: searchable object reference picker.
- `a98fbdef33174f3bae9d18ddab66e38f56efe868`: shared-token picker CSS.
- `b8989bfacac36313c37b56880faeb6f56de5e4ca`: TypeScript property metadata.
- `1ef61e4ee36af0605078d292342c6367c9a15362`, `230e89068b8a537e9d908349cee215e92fda2f89`: Inspector PropertyField wiring.
- `bc356fb1a23f41f4ad6d517a479ce4d15ee05d73`: Studio component catalog.
- `aba79058b1696ed090930de7bdc80eed5f8185ac`: native reference options and numeric schemas.
- `49197d861b0218d19acfafc6c79d1cbbb97de032`: native reference writes.
- `327c578d3ad446ac18a67962a2077a31155b7345`: atomic native batch editing.
- `347e36124b091726e5dd0a089af17fa17d2d35af`: protect referenced components from removal.
- `b789a1fb342299963fdd37e0d8ff7f1a7a2f2257`: native StudioSession reference, Undo/Redo, disk-save tests.
- `5c611e462006abd07706f1639deb6d8a4bf9da4e`: updated agent skill.
- This handoff's SHA: `git log -1 -- docs/handoffs/world-processes-stage09-linked-authoring-2026-11-23.md`.

## Tests actually run vs not run

**Historical evidence only (earlier source):** Stage05 focused 4/4 policy/ECS tests PASS under Ubuntu GCC, Clang and Clang ASan/UBSan, run https://github.com/chromealex/AIVERSE/actions/runs/45207631125 .

**Stage09:** The source, tests, CMake and workflow changes were committed. As of the last inspection GitHub Actions for the new branch commits were queued. **Stage09 tests have NOT been confirmed compiled or passed**, and the new picker has NOT been visually tested in HTML5 Studio on a Mac. Do not claim stage9 green or that it is merge-ready. Full repo CI and merged-tree `./run-tests.sh --quick` still remain unverified; earlier unrelated engine/voxel layer issues were seen.

Test source `tests/world_processes_linked_test.cpp` authored: validates link schemas, same-World ECHO hearing (portals and emitter/receiver), FRACTURE support bond, AETHER duct+flux, stable ID lookup, COW rollback, disk save/load, duplicate IDs, unknown IDs, incompatible target rejection. The Studio `StudioSessionTests.cpp` fixture now tests picker options, atomic linked paste, invalid reference rejection, correct Undo/Redo, persistence and prevention of referenced-component removal.

## Next exact step

1. **Run latest focused compiler jobs.** Validate `tests/world_processes_verify.sh` including `world_processes_linked_test` on GCC, Clang, and ASan/UBSan. Inspect first actual error and fix without bypassing warnings. Then build/CTest the real `world_processes_gamecore_native_test`.
2. Build `aiverse_studio` on the author's Mac and `npm run typecheck && npm run build && npm run test:web` for Studio; inspect search, selection, drag from Hierarchy, clear, Undo/Redo and Save across two or more real authored map entities. Add genuine screenshots (not synthesized) and ensure 240px panels do not clip.
3. Add graph-wide dependency repair for document *entity deletion/rename* and safe broken-link migration, with deterministic warnings and Undo.
4. Integrate actual Box3D durable structural consequences and PHYSIS fire/gas link ownership at a host barrier. Do not duplicate physical simulation.
5. Sync/reconcile with `main` and `feature/studio-html5`; run merged-tree Mac `./run-tests.sh --quick`; only after all gates and cross-genre confirmations consider merging PR #112.

## Follow-up within Stage09: actual GameCore linked-path regression

- Commit `086f68111ea7f50df64a1c751093bdfbebc2c500` updates
  `tests/world_processes_gamecore_native_test.cpp`: the existing
  mushrooms game map now keeps existing stable IDs (or assigns IDs
  when missing), authors two ECHO zones, an emitter, a portal and a
  listener, and asserts native map setup resolves those exact
  generation-safe Entity handles and emits a 75-intensity hearing
  result under deterministic peer steps. This test also retains gas,
  structural anchor, v9 opt-out, v10 save/reload and rollback checks.
- Commit `2424b9ceec2a1e606d96edf330d83b3bdc5c6dde` adds
  explicit `<set>` and `<string_view>` includes to the shared
  StudioSession source used by reference validation and batch edits.

These changes are **written, not yet confirmed passing** until the
latest workflow and real GameCore CTest complete. The Stage09 focused
CI has been triggered but was queued on last inspection; do not
reuse Stage05 green results as proof of this version.
