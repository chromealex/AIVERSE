# 100 — Rules panel: unit-type form over rules.json

Date: 2026-11-21.

## Done
- Native `studio.rules` → `{types (as written), resolved (extends applied), resources,
  summary, dirty}` via the existing `rules_get` / `rules_list` ops; `studio.rulesSet
  {path, value}` → `rules_set` (null → `rules_delete`). Only `types.<id>…` paths
  (≤ 6 segments, `[n]` indices) are accepted. The game loader validates every write,
  rolls back on error and the file is saved at once (same contract as the legacy window
  and MCP).
- Web `panels/RulesPanel.tsx` + `rulesSchema.ts`: type list with fuzzy search; grouped
  form (General, Health, Movement, Vision, Cost per resource + time/supply, Weapons per
  weapon, Flags); own vs inherited values (placeholder "(inherited)"), blank = inherit,
  reset button per own value; "Edit JSON" for the whole type with Validate & save.
- `rules` is now a Studio panel (Logic workspace centre) and is no longer listed as a
  legacy window; parity table updated (Partial).

## Checks
- tsc; `npm run test:web` (new `rulesSchema.test.mjs`; nested-dock expectations include
  the new panel); Python contracts; clang++ syntax of StudioSession.cpp and tests
  (rules read / edit / reject / delete) — run on the Mac.

## Not done
- Techs, resources, triggers, damage table matrix, abilities — still JSON (legacy window).
- Rules edits are outside map undo (saved immediately, like the legacy editor).
