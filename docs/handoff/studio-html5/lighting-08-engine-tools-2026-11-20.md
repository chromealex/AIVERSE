# Lighting Stage 35 — engine skills and MCP authoring

2026-11-20; integration/lighting-main-stage1-20261120.
Implementation commit: 84d403ad3b115d96d3555442356855e1a69f80c5.
User explicitly clarified skills must live IN THE ENGINE. Added repository
skills/editor/aiverse-scene-entities and skills/visuals/aiverse-lighting, registered
both in tools/skills/catalog.py and generated skills/catalog.json (229 skills),
linked from AGENTS.md and documented MCP usage. No global Codex skill/config,
plugin installation or external service connection was changed.

Native Workspace/MCP registry adds seven operations:
scene_entity_list/get, scene_component_set, scene_light_create/delete,
scene_render_lighting_get/set. Common document ID formatting moved into
apps/editor/DocumentEntityIdentity.h; Studio wrappers reuse it. Component edits
reuse EditorEntityAdapter, validate ownership/instance fields, stage on a copy
and commit to native history. FPS uses whole-document fps_set validation. Old
MCP names and save formats remain compatible; entityId is the Stage34 metadata.
No new renderer behavior, GPU flags or simulation components. Sources are
apps/editor/core/SceneEntityOps.h, LightOps registration and tools/mcp/main.cpp
server instructions. Generic component attach/remove is still not implemented.

Actually executed, all artifacts/builds/temp on KINGSTON under
/Volumes/KINGSTON/AIVERSE/Builds/.temp/lighting-completion-20261120:
- Debug aiversemcp, aiverse_studio_render_lighting_tests and aiverse_editor
  built PASS (stage35-validated-build.log, stage35-editor-build.log).
- python3 tests/editor/scene_entity_mcp.py --binary <root>/build/aiversemcp
  --output <root>/stage35-mcp-final-fixture PASS. Real JSON-RPC initialize,
  tools/list and tools/call; isolated --no-editor --port 0 transport. Both RTS
  and FPS: create/get/components, invalid/wrong-component patches leave state
  unchanged, Transform + Light edit, Undo/Redo, stable-ID delete, GI validation,
  Save/reopen and persisted surviving ID. stage35-mcp-final-test.log.
- CTest studio_render_lighting 1/1 PASS (2.98s), stage35-native-regression.log.
- tools/skills/catalog.py generate/check PASS (229 skills),
  tools/skills/audit.py PASS (0 errors/0 warnings),
  tests/skills/catalog_test.py PASS (routing/ownership/dependencies).
- Host skill-creator quick_validate.py attempted for both skills but could not
  run: Python3.8 and 3.12 lack PyYAML (ModuleNotFoundError). No dependency/global
  configuration installed to work around this. Engine's own validators passed.
- git diff --check PASS. No CI or push. No new Release/GPU run needed for these
  tooling-only changes; previous Stage34 GPU evidence unchanged.

Existing running editor processes were not restarted. For connected MCP use the
updated editor and server together; an old editor can reject newly listed tools.
--no-editor only disables launching and can still connect to its configured port;
--port 0 deliberately isolates the actual transport test. Settings getters report
authored configuration, not proof of backend availability. Hardware gates remain.
Next: consume the engine skills and MCP entity/component API for new authoring
work; use map_save when persistence is intended and preserve entity ownership.
