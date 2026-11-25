# Caverns Stage54d — recipe, shaft and paging authoring

Date: 2026-11-24. Continue in `integration/caverns-studio-stage54c-20261124`. Main and original feature branches unchanged.

## Source commits

- 06e399c4795a59db208010db13e2d6054fd02bdf: shared Workspace paint operation adds recipe changes and entrance add/update/erase, full rebuild marker, union dirty bounds, exact IDs, one MapDoc undo per edit.
- 8103b45d4655aafd569e212475081ac777ec9925: Studio generator/entrance forms.
- 2ff96624b3a56fc6d27261e6ba84a2964b50d3c0: validated preview paging configuration with shared STRATA policy conflict check.
- d2acffe4ee8e8e2130998dc7c241dd467666e999, 9183497e8188ed6cd3fb6bbea4ca69a978d5f5f2, 9f04851e02695a8e153bd6f122cd01fd0cdecbae: Scene bridge and typed budget controls.
- ae538a49c7ebdc4681ca77531d0d4609315575b2, eebaa8a06ba9a2be6c0a7d38d69f14cd9d2e3f50: MapDoc revision guard and MCP stroke isolation.
- 8f78fd8bdfccef777f2be72d2a5dd357e9537c49: validate persisted entrance coordinates.
- 202107d1e99ed75c6cdf109f28e0171b70c29dae, 5596a5ad62a638909ba170c1bf3494432019b5d2: skill updates.

## Contract

`terraweave_caverns_paint` modes settings, entrance_add/update/erase share the canonical authoring snapshot. Recipe changes preserve stamps and entrances and report fullRebuild rather than a fabricated finite dirty box. The existing legacy editor may still require explicit reload for generator changes. Entrance edits include old and new shaft bounds. Studio forms send both exact sourceRevision and MapDoc revision; project/map checks remain. Browser does not own authoring state. During an active cave stroke MCP calls through Studio agent link are deferred by refusal; other document mutation paths and full lifecycle fencing still require review.

Preview budgets are session-only: six aligned positive byte limits, ordered source <= shard <= device, <=2 GiB each. Changing budgets may close the current preview at an explicit idle point before checking external live device leases; incompatible external policy is rejected. A failed policy request may therefore leave preview needing recreation. Existing STRATA index ledger remains sole device authority; these controls do not bound original vertex buffers or initial root upload, nor measure physical VRAM. Legacy fallback remains.

## Next Stage54e

Implement actual Studio heightfield hole cutting using shared TerraWeave surface-cut utilities, including lighting mesh parity, rebuild/Undo/Redo, seam certification and native GPU lifetime. Do not replace terrain with an unrelated flat plane or claim physics holes from the visual mesh. Review complete brush/document lifecycle and preview scheduling; close outstanding gameplay-native ownership/replay and STRATA issues. Native build, TypeScript checks, tests, screenshots and runtime validation NOT RUN. Mandatory KINGSTON gate before main.
