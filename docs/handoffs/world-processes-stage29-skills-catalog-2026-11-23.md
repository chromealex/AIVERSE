# AIVERSE — Stage 29: ten World Processes skills in the canonical skill catalog

**Date:** 2026-11-23
**Branch:** `feature/world-processes-experimental`
**PR:** https://github.com/chromealex/AIVERSE/pull/112 (**DRAFT**, do not merge)
**Main:** not changed
**Owner handoff:** World Processes / agent skill routing, no game runtime change in this stage

## Request and decision

The user requested individual agent skills for each of the ten
genre-neutral World Processes features and pointed out that AIVERSE
**already has a real skill catalog**. Reuse that canonical system,
not another `INDEX.md`, wiki mirror or independently maintained
JSON registry.

Canonical source: `tools/skills/catalog.py`.
Generated artifact: `skills/catalog.json`.
Agent guidance: `AGENTS.md`, `docs/skill-orchestration.md`.
General router: `skills/world/aiverse-world-processes/SKILL.md`.

## Implemented deliverables

Ten individual English-language skills, all under `skills/world/`:

| Skill ID | Canonical source |
|---|---|
| `aiverse-echo` | `libraries/include/aiverselib/world/EchoEcs.h` |
| `aiverse-fracture` | `libraries/include/aiverselib/world/FractureEcs.h` |
| `aiverse-aether` | `libraries/include/aiverselib/world/AetherEcs.h` |
| `aiverse-morph` | `libraries/include/aiverselib/world/MorphEcs.h` |
| `aiverse-crowd` | `libraries/include/aiverselib/world/CrowdEcs.h` |
| `aiverse-imprint` | `libraries/include/aiverselib/world/ImprintEcs.h` |
| `aiverse-circuit` | `libraries/include/aiverselib/world/CircuitEcs.h` |
| `aiverse-granular` | `libraries/include/aiverselib/world/GranularEcs.h` |
| `aiverse-evolve` | `libraries/include/aiverselib/world/EvolveEcs.h` |
| `aiverse-society` | `libraries/include/aiverselib/world/SocietyEcs.h` |

Each `skills/world/<id>/SKILL.md` documents:
when to use it; exact canonical C++20 headers and ECS components;
`Simulation` installation, order and persistence; the actual
`world_processes` NativeModule opt-in; Studio map authoring and
stable generation-safe ID rules; a minimal API usage pattern;
existing Box3D, PHYSIS, TerraWeave, NEREID, NavMesh, Renderer3D
and game-owned systems that MUST NOT be duplicated; actual known
limitations, acceptance checklist, and matching earlier stage
handoff references.

**Canonical catalog integration**:
- `tools/skills/catalog.py`, SHA
  `3e43d5c171830faa1b30943e9e6e5bdae43914a9`:
  adds 11 entries (the ten dedicated skills plus the shared
  `aiverse-world-processes` router), English descriptions,
  Russian/English search vocabulary, and exact source paths.
  All are `domain=gameplay`, `side=tools`,
  `runtime=none`, `target=null`, `package=null`.
  Each specific skill depends on `aiverse-world-processes`
  so `plan` reads shared owner rules once. There is NO invented
  per-skill CMake target, new binary dependency, or duplicate
  library package.
- `skills/catalog.json`, SHA
  `12d9afb1610b19446995b1e65d4d0ba4e89bd27a`:
  updated canonical generated-shaped output from **266 to
  277 catalog entries**; one catalog entry per new skill,
  sorted by stable ID, with keywords and dependency routing.
- `AGENTS.md` SHA `efcf882332bb61c1ab50b3c2af4deb42659cb90e`:
  new general routing rule, no duplicate catalog, no genre split,
  respect owner contracts and PR DRAFT.
- `docs/skill-orchestration.md` SHA
  `ce1f443cb58a29284f755e010b2cd10f11433ee6`:
  examples of official `search` / `plan` / `check` with ten
  mechanics and Russian/English vocabulary.
- `skills/world/aiverse-world-processes/SKILL.md` SHA
  `f8633d6105a98df6f102bb8f4be1c82ee3e3d3a6`:
  small navigation table for all ten, points to the canonical
  generated catalog rather than creating a second index.
- Individual skill initial create commits:
  ECHO `39a71e1d0e9c4755ff13cd71bdd1917e54cb93fb`,
  FRACTURE `b1087b215c45c709d08b5e1c603511e6a23083e6`,
  AETHER `eb6d6f97f7ebf4d9ffb4add63ab9358008eca6b3`,
  MORPH `2af02740bfc5c2188c14fc05bdfb691d42127d35`,
  CROWD `740e78c26f74acb64126f8b3533c4869201ba695`,
  IMPRINT `83a687cfe61f712a17a6d738f42369f19dd4ce53`,
  CIRCUIT `569e021c41797129fe7d2d5c368ac7d05ce4c446`,
  GRANULAR `7ce96fee5699bc4c167868dccaf4bd50baf06b6c`,
  EVOLVE `d019c1e379f94d6081091e0099220f3abbf15288`,
  SOCIETY `149523607877b315918ad80b2d1b7b7be4a8fe64`.
  All ten then updated to **English-only skill body and
  frontmatter description** in accordance with `AGENTS.md`,
  with explicit native target distinction; latest such commit:
  `3a33210870288fa714216146b93bb1cb540371ee`.

## Source checks performed vs tests deferred

**Performed:** fetched all eleven catalog-bound source metadata
paths and source headers through GitHub; confirmed the updated
`skills/catalog.json` parses as JSON, contains **277** entries,
all **11** newly registered skill IDs exactly once, correct
`domain=gameplay`, `side=tools`, `runtime=none`, nonempty
Russian/English search vocabulary, canonical source paths,
and each specific skill's dependency on the common router.
Fetched dedicated CROWD/GRANULAR skill files and confirmed
English-only body content.

**Not run:** `python3 tools/skills/catalog.py check`,
`python3 tools/skills/audit.py --strict`,
`tests/skills/catalog_test.py`, skill-routing tests,
native CMake/CTest, browser/Studio or any runtime tests.
Tests remain deferred at the user's explicit request.
Do NOT claim generator output was CLI-verified; its entry shape
was checked statically and manually synchronized with its
generator code. Do NOT reuse old Stage05 green CI as proof
of Stage29 or Stage11+ runtime integrations.

## How the next agent uses it

From the AIVERSE repository root:

```sh
python3 tools/skills/catalog.py search "acoustic portal"
python3 tools/skills/catalog.py search "опоры разрушение"
python3 tools/skills/catalog.py plan --skills aiverse-morph --side both
python3 tools/skills/catalog.py plan --skills aiverse-circuit,aiverse-society --side both
```

When tests are permitted:

```sh
python3 tools/skills/catalog.py check
python3 tools/skills/audit.py --strict
python3 tests/skills/catalog_test.py
```

First fix any **actual** catalog/generator mismatch or missing paths
without inventing a replacement catalog. Review then run
full merged-tree AIVERSE quick gate on the author's Mac before
merge; retain PR #112 in DRAFT until passing evidence exists.

## Next step

Return to feature implementation, especially the actual native
EVOLVE region-bounds authoring, overlapping Studio Scene handles,
and a real playable all-ten demonstration with actual model/physics
owners. For every new mechanic feature change, update the corresponding
dedicated `skills/world/aiverse-<mechanic>/SKILL.md` and add a new
handoff with changed sources and evidence. Keep the official skill
catalog generator and output in sync if any new skill or routing
metadata is added.
