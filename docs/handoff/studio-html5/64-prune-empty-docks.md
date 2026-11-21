# Stage 64 — Automatic docking compaction
Date: 2026-11-20
Branch: feature/studio-html5
Parent: ea740657920934511474ca4cd823c15b02aaa381
Stage commit: see branch log.

## Implemented
Pure `nestedDock.ts` compaction of empty tab stacks and their parent splitters on move/close/load; adjacent pane grows to fill the freed space. All-closed case preserves one empty center dock. Reopening a panel can recreate preferred left/right/bottom region. Existing saved v3 layout is migrated without wiping user preferences. No C++ bridge, Metal or real Scene code changed.

## Files
apps/studio-web/src/nestedDock.ts
apps/studio-web/tests/nested-dock.mjs
docs/handoff/studio-html5/64-prune-empty-docks.md

## Validation
Six additional Node tests added: move-last, close-last, restore right/bottom, all-close-and-reopen, persisted empty leaf. **Node/Vite/macOS tests NOT RUN** in this connector runtime; run `cd apps/studio-web && npm run test:dock && npm run typecheck && npm run build` then visual drag/resize on Mac.

## Concurrency
Other agents advanced the same branch while authoring. This stage was built on freshly fetched branch head and uses compare-and-swap ref update. It only changes dock state and tests, not their StudioSession changes.

## Next
Remove technical location selects from dock header in Stage 65; add accessible menu for move/split/close. Check native Metal viewport resizes after dock collapse.
