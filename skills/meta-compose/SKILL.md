---
name: meta-compose
description: Generate a complete or selected AIVERSE meta layer from reusable C++ policies, validated JSON and existing browser screens, with minimal agent context.
---

1. Inspect existing `<project>/meta` before generating. For a new project run
   `python3 tools/meta/kits.py plan --features economy,social,client,ui`.
   Use `--preset full` when all policy groups are requested; presets also include
   minimal/mobile/competitive. The plan lists resolved dependencies and exact skills.
2. Read only the selected recipe and, if needed, `tools/meta/catalog.py describe ID
   --section PATH` (for example `describe economy --section offers`). Do not load all
   LiveOps schemas or regenerate reusable algorithms.
3. Apply with `tools/meta/kits.py apply --features ... --project PROJECT --validator
   PATH_TO_aiverse_meta_validate`. Validation happens before installation. Existing
   meta/UI folders are refused; modify their source JSON instead of overwriting them.
4. Keep `meta/AGENTS.md` as the project's compact integration guide. Compose edited
   definitions using `tools/meta/compose.py`; pin/verify via `tools/meta/catalog.py`.
5. Link `aiverse::meta`; registered native OBJECT modules use
   `aiverse_native_use_meta(target)`. Host requests use `meta::Client`, see meta-client.
   Never put clocks, credentials or network/profile state in simulation callbacks.
6. `ui` copies the maintained browser screens. `metadata.metaScreens` selects supported
   tabs. Serve them with the authenticated reference HTTP host using `--ui PROJECT/ui/meta`.
   Platform payments still require Store providers; configured policies are not a deployed
   provider or native RmlUi integration. Generated service chat uses global channels only.
7. Run meta CTest, project configuration validation and the external native consumer.
   Inspect rendered selected screens when changing UI. Report actual runtime/platform coverage.
