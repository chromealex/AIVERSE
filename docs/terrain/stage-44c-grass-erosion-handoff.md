# TerraWeave / NEREID — Stage 44c grass coverage handoff
Date: 2026-11-19. Branch: `feature/terraweave`; PR #24 Draft.

## Source changes
- `1ba5f63aacd68b257e52dd70bdc29ffd4d700d63` — NEW `engine/world/TerraWeaveGrassCover.h`: indexed `GrassCoverRegistry`.
- `0ea5bb48d005dfa2a33bc0885f0ce0296e59297c` — `engine/world/TerraWeaveNereidErosionBridge.h`: optional `GrassCoverRegistry*` tail argument, `BankTick::grassCoverage`.
- Preceding source: `64c8941a6fb8d918f5786c5a2bde655144120a27` root support/fall registry; `ca21d1c41a680b845cb5ec78e0f4c19f7874f30e` transactional support changes; `498a46afa619bc8726a301c1c06d245fe6308b0a` erosion-to-Earthworks bridge.

## Public API and behavior
`GrassCoverRegistry` holds up to 4,096 sorted persistent grass patches (`GrassPatch` with stable key, seed, coverage, light/age/moisture/nutrients); each patch represents up to 262,144 possible blades, but there is NO per-blade ECS entity or persistent blade array. A 6,400cm bucket lookup (max 64 patches per bucket) selects only patches affected by an `EarthworkChange`. `applyEarthwork(change,spacingCm)` integrates per-edited-vertex soil displacement using overlap-area-weighted integer damage, yielding deterministic `GrassCoverChange` records and new patch coverage. Changes/updates are transactional inside the registry. `visible(rect)` returns lightweight draw descriptions; optional `sampleGrassBlade` generates one deterministic instance on demand.

`NereidEarthworksBridge::step(probes,tick,dtMs,roots,settings,grass)` now produces Earthworks soil edits, once-only rooted-tree/shrub fall events and sparse grass coverage updates from the same tick. Manual digging can call `GrassCoverRegistry::applyEarthwork` and `RootSupportRegistry::applyGroundChange` using the returned `EarthworkChange`; no water is needed. ECS/current flow always comes from NEREID.

## Status and cautions
Actual verification: GitHub source commits were created. Native compilation, CTest, real scene playback, GPU screenshots and performance are **NOT RUN**; separate Mac Studio tester owns these after features are finished. Previous 9/9 TerraWeave CPU and 1/1 GPU router pass were for older revisions only.

Rendering/physics still missing: no GPU grass instancing, full foliage shader age/light response, real falling body spawn, navmesh/collision rebuild or downstream sediment advection in this particular stage. A separate parallel agent is editing `WorldScene`, forest root presentation and age conditions: inspect the latest branch rather than overwriting its changes. Root event creation does not itself topple a physics entity.

## Next implementation
Connect earthwork revision publication into native chunk heightfield + collision/navmesh + renderer on owner thread; emit grass patch draw lists through RHI instancing; use lazy roots when roots are exposed and stable authored plant age/light for near/far presentation. Add source tests later per user's feature-first request, record real build/CTest results before changing Draft PR status.

Note: Shared `docs/terrain/AGENT_HANDOFF.md` was concurrently updated by another agent, so this independently named per-stage handoff is the authoritative record for this stage.