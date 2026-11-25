# AIVERSE World Processes — Stage 10: map link referential integrity

Date: 2026-11-23
Branch: `feature/world-processes-experimental`; draft PR https://github.com/chromealex/AIVERSE/pull/112
Main: NOT merged, branch remains behind main.
In-progress world systems: ECHO, FRACTURE and AETHER (3). Remaining 7 user-requested world systems are still Stage01 pure-policy experiments.

## Problem and solution

The Stage09 stable-ID links (EchoPortal, EchoEmitter, EchoReceiver, FractureBond,
AetherDuct, AetherFlux) are persisted in the ordinary project map.
Before this stage, Studio Scene/Hierarchy entity deletion could leave a surviving
source's linked field referring to a now-missing map entity. Such a map would fail
to hydrate in the opt-in World Processes native module on subsequent game start.

The existing `apps/studio-native/StudioSession.cpp` now calls
`validateWorldProcessEntityDeletion(MapDoc&, indexes)` **before** any map
mutation through `studio.entityDelete` or `studio.entitiesDelete`.

- Collect stable map IDs of all objects targeted by the delete transaction.
- Ignore sources deleted in the same batch. This preserves all-or-none
  group delete without requiring index-based repairs.
- Scan real `DocEntity.extra.components` on surviving entities and check
  the six exact linked schema types using
  `linked::supports` and `linked::isReference`.
- If a surviving component refers to a removed stable ID, reject the
  operation with a concrete error naming the map ID, component and
  owning map object; do not mutate the map or Undo stack.
- No sweeping replacement, silent clearing, or guess of a new target.
  The author must clear/relink the field, or delete the referer too.

The behavior uses existing MapDoc state, StudioSession native RPC,
and its Undo/Redo. There is no new browser IPC method, no additional
authoritative ECS state, no gameplay genre dependence and no ImGui update.

## Changed files, SHA and tests

- `apps/studio-native/StudioSession.cpp`, commit
  `7d339e28fdbc331a7baa443804812f778071a4aa`:
  guard in both single-delete and batch-delete paths.
- `apps/studio-native/tests/StudioSessionTests.cpp`, commit
  `381cc15b39ac901bf0340a4664e819ca2f624519`:
  Stage09 fixture now additionally tries to remove referenced
  `entity:@first` via both `studio.entityDelete` and
  `studio.entitiesDelete`; checks error and target still present.
- This document SHA:
  `git log -1 -- docs/handoffs/world-processes-stage10-reference-integrity-2026-11-23.md`.

**Actual validation:** GitHub accepted source/test commits. The new
Stage10 Studio tests have NOT been observed passing CMake/native
Studio CI. The most recent workflows were queued at observation time.
Historical Stage05 focused 4/4 success with GCC/Clang/ASan:
https://github.com/chromealex/AIVERSE/actions/runs/45207631125.
That historical result does not cover stages 06–10. Do not claim
full Mac quick/Studio/UI test success.

## Known boundaries

- This protects deletion through the **StudioSession RPC**. Native
  MCP/editor map operations outside StudioSession could still require a
  shared map-authoring reference-integrity validator.
- Direct authored ID rename, external JSON editing, map import/merge,
  and mass link repair are not implemented here. The native
  `hydrateMap` preflight rejects broken/ambiguous IDs rather than silently
  selecting another object.
- Deleting the entire object is different from deleting its ECS
  component; Stage09 already guards removal of a referenced compatible
  component.
- Web asset picker targets `content/` assets; this is a **map object**
  reference system with exact, case-sensitive stable map IDs. Do not
  conflate GUIDs or numeric ECS slot indices with map IDs.

## Exact next step

1. Inspect latest CTest/Studio HTML5/CI job logs and fix compiler or
   runtime failures in Stage09/10 code without weakening assertions.
2. Add tests for group-deleting **both the target and all referrers**
   in the same Undo transaction, and for stale IDs after map reorder,
   direct JSON edits and restart/reopen.
3. Run actual native editor and HTML5 Studio on Mac. Select real linked
   map objects, verify picker focus/search/drag and guarded delete error;
   capture real screenshots rather than simulated images.
4. Move common deletion/rename integrity into the shared MapDoc operation
   layer if other entry points need it; coordinate with owner of
   `feature/studio-html5`.
5. Reconcile main, run `./run-tests.sh --quick` on the merged Mac tree
   and only then consider making PR #112 mergeable.
