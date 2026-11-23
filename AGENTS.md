# AGENTS.md — mandatory AIVERSE agent entry point

Read this file completely before changing anything. The linked contracts are binding. Load the common contracts first and the relevant area contracts before touching their code. Do not treat a shorter entry point as a relaxation of any owner rule.

## Universal rules

- NO GENRE SPLIT — EVER. All capabilities are shared engine/library code; legacy modules are adapters only. Simulation state is ECS, with one authority, deterministic ticks and explicit dependencies. Every scene object is an Entity.
- Reuse existing owners, libraries, skills and native APIs before implementing. Never introduce a competing solver or game-specific code into the engine.
- Own branch/worktree, sync main, preserve other agents' work; no force push or pruning another worktree. At most five active capability areas; finish or hand back before starting another.
- Before implementation update roadmap and feature delivery record. After every stage commit a separate handoff with exact SHA, paths, real test evidence and next step, including failed stages.
- Never claim unrun tests passed. The merged-tree Mac quick gate remains mandatory before main. Build outputs and caches go to /Volumes/KINGSTON; no silent fallback on Mac.
- Every applicable feature exposes native API, skill, MCP and Studio, with appropriate persistence, tests and examples. Visible features require real captures and web demos subject to the publishing contract.
- Current release: 3.0.0 released; next 3.1.0. MAIN integrator owns release tags and publication.

## Contract routing

Read every row marked **always**, plus all relevant conditional rows (cross-subsystem tasks may need several). Original section numbering is preserved in extracted documents. Paths in those documents are relative to the repository root.

| Scope | Mandatory contract |
|---|---|
| Always: feature lifecycle | [Feature contract](docs/agents/feature-contract.md) |
| Always: architecture, naming, ECS, code, data | [Architecture](docs/agents/contracts/architecture-common.md) |
| Always: parallel ownership, handoff, Git | [Workflow](docs/agents/contracts/workflow-parallel.md) |
| Always: build, tests, packaging | [Build and testing](docs/agents/contracts/build-testing.md) |
| Always: release, roadmap, site, README | [Release and site](docs/agents/contracts/release-site.md) |
| New capability, library, meta or skill | [Reuse and skills](docs/agents/contracts/reuse-skills.md) |
| World, terrain, water, weather, physics | [World ownership](docs/agents/contracts/world-ownership.md) |
| Rendering, GPU, post-processing or VFX | [Render ownership](docs/agents/contracts/render-contract.md) |
| Editor or Studio UI | [Studio contract](docs/agents/contracts/studio-contract.md) |
| Editor or MCP operations | [MCP contract](docs/agents/contracts/mcp-contract.md), [MCP coverage](docs/agents/mcp-coverage.md) |

## Area rules — read the file for your area before changing it

Subsystem contracts and stage logs live next to the docs, not here. Before you touch an area, read its
file completely; it is as binding as this one. Add new stage notes to that file (or to your handoff), never
back into AGENTS.md: this file holds only rules that apply to every agent.

| Area | Mandatory file |
|---|---|
| lighting, GI, sky, fog, LENS | [docs/agents/lighting.md](docs/agents/lighting.md) — Lighting, dynamic GI, physical sky / local fog, LENS captured frames |
| particles, VFX emitters, fx graphs | [docs/agents/spectrafx.md](docs/agents/spectrafx.md) — SpectraFX universal VFX: status, APIs, stage log |
| GPU resources, textures, model upload/retirement, batching, geometry pool | [docs/agents/render-resources.md](docs/agents/render-resources.md) — Shared textures/streaming, render resource lifetime (Stages 99-108), CPU MDI and geometry pool (Stages 35-56) |
| virtualized geometry, LOD, GPU culling | [docs/agents/strata.md](docs/agents/strata.md) — STRATA virtualized geometry: stages 12-17.4, skill and MCP authoring |
| gameplay ECS packages, persistence, authority migration | [docs/agents/ecs.md](docs/agents/ecs.md) — ECS mechanics inventory, FPS ECS authority migration, ECS 2.0, native ECS state registration |
| ImGui editor windows, inspector | [docs/agents/editor-unification.md](docs/agents/editor-unification.md) — Editor unification work (PR #7) |
| input actions, bindings | [docs/agents/inputflow.md](docs/agents/inputflow.md) — InputFlow (formerly Enhanced Input) staged handoff |
| water bodies, flow, buoyancy | [docs/agents/nereid.md](docs/agents/nereid.md) — NEREID water framework summary |
| weather, wind, fire, quake, nature fields | [docs/agents/physis.md](docs/agents/physis.md) — PHYSIS environmental ECS: diagnostics, stages 15-79, map authoring |
| build monitor, CI triage | [docs/agents/build-monitor.md](docs/agents/build-monitor.md) — Terminal build monitor, warnings/errors reporting |
| terrain, foliage, shoreline, erosion, ecology | [docs/agents/terraweave.md](docs/agents/terraweave.md) — TerraWeave terrain/foliage/ecology stages 53-65, cosmetic rocks |
| animation, VIVID digital humans, View model lifetime, LOD retarget | [docs/agents/motionweave.md](docs/agents/motionweave.md) — MotionWeave / Universal View stages 117-161, VIVID digital humans, animation reporting rule |
| climbing, legged locomotion | [docs/agents/surface-locomotion.md](docs/agents/surface-locomotion.md) — Procedural climbing, spider gaits and surface locomotion |


## World Processes skill routing

For ECHO, FRACTURE, AETHER, MORPH, CROWD, IMPRINT, CIRCUIT, GRANULAR, EVOLVE or SOCIETY, read the matching `skills/world/aiverse-<mechanic>/SKILL.md`. For cross-system work also read `skills/world/aiverse-world-processes/SKILL.md`. These opt-in ECS capabilities are indexed in the generated `skills/catalog.json` through `tools/skills/catalog.py`; do not maintain a second index or invent a CMake target per skill. Respect existing subsystem ownership and verify the latest stage handoff before claiming acceptance. The experimental World Processes PR #112 remains draft.

For cross-subsystem work start with `skills/workflow/aiverse-orchestrator/SKILL.md` and search the catalog. For a new gameplay mechanic also load `skills/workflow/aiverse-new-mechanic/SKILL.md`. For all tasks read the latest applicable handoff.

## Mandatory feature delivery gate

Before implementing any new or expanded capability, read `docs/agents/feature-contract.md`. Record every applicable criterion and its evidence in the active handoff. The automated Feature Gate and manifest tooling are developed separately in PR #120; until integrated, use the manual checklist. Existing area rules and real Mac test gate remain mandatory. Do not claim feature readiness from structural validation alone.

