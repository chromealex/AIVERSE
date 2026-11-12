---
name: aiverse-orchestrator
description: Find AIVERSE repository skills and select the smallest library set for a game or meta subsystem, separating client, server and database responsibilities. Use before generating integrations across multiple capabilities.
---

This is a workflow skill with no runtime library. The machine-readable index is
`skills/catalog.json`; do not load every SKILL.md or the whole schema.

1. Run `python3 tools/skills/catalog.py search "INTENT" --domain meta|gameplay`.
   Search supports English and common Russian terms. It suggests capabilities, not game requirements.
2. Choose exact IDs and run `plan --skills ID,ID --side client|server|both`.
   For clear aliases, `plan "магазин чат" --domain meta --side server` also works.
   Inspect the plan; resolve an ambiguous intent from project context before adding features.
3. Read only paths in `read`. The plan lists ownership, dependencies and concrete CMake calls.
   Client plans substitute the request library for server policies; private policy/SQL data stays on server.
4. For server kits use `aiverse_add_meta_server(game_meta MODULES ...)`, include
   `game_meta.h`, construct `Service(kits::game_meta(), config, state, commit, draw)`.
   Link the generated `game_meta` target, not the full compatibility umbrella `aiverse::meta`.
   For native OBJECT modules propagate the selected target with `aiverse_native_use_meta(target game_meta)`.
5. Gameplay packages use `AIVERSE::lib_<package_with_underscores>`; native modules use
   `aiverse_native_use_libraries(target package ...)`. Existing RTS integration recipes require
   the owning engine subsystem. A reference/workflow skill does not add a dummy runtime library.
6. Run catalog checks and the relevant isolation/native consumer tests. Adding a skill requires
   updating its source catalog and regenerating this index; see AGENTS.md.

Detailed build and selection contracts: `docs/skill-orchestration.md`.
