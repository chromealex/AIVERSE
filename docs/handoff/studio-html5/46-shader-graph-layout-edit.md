# AIVERSE HTML5 Studio — Stage 46: Shader Graph layout draft and explicit Save

Date: 2026-11-20
Branch: feature/studio-html5
Parent: ea2bc6c7d5c4b914677c0286370ebb1e01ae5baf
Commit: see branch history.

## Implemented
- Added bounded native StudioShaderGraphDraft.h validator (domain material/postfx/particle, <=128 unique nodes, safe node names, finite bounded canvas positions, <=16 inputs per node, <=256 KiB serialized graph).
- Added strictly allowlisted studio.shaderGraphValidate and studio.shaderGraphSave methods. Both delegate to existing ShaderGraphOps compiler. Save uses its atomic shaders/<name>.json writer; Validate never writes a file.
- Native responses include only graph JSON/node & sample counts, never generated shader GLSL/MSL strings.
- HTML5 Shader Graph now supports dragging nodes to adjust coordinates while preserving links/operators, local draft state, Validate, Discard and explicit Save positions.
- UI distinguishes unsaved draft from saved file and explicitly warns that MapDoc Undo does not cover Shader Graph disk edits.
- Added native draft validator CTest and Node.js immutable coordinate edit tests, registered in macOS CI and frontend test:graphs.

## Files
apps/studio-native/StudioShaderGraphDraft.h
apps/studio-native/tests/StudioShaderGraphDraftTests.cpp
apps/studio-native/StudioSession.cpp
apps/studio-native/CMakeLists.txt
apps/studio-native/tests/StudioSessionTests.cpp
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
apps/studio-web/src/panels/shaderGraphLayout.ts
apps/studio-web/src/panels/ShaderGraphPanel.tsx
apps/studio-web/src/panels/ShaderGraphPanel.css
apps/studio-web/tests/shaderGraphEdit.test.mjs
apps/studio-web/package.json
.github/workflows/studio-html5-web.yml
tools/studio/verify_mac.sh
docs/handoff/studio-html5/46-shader-graph-layout-edit.md

## Tests performed
- GitHub source changes committed. New native and Node tests authored, NOT executed here as part of the full AIVERSE checkout.
- GitHub Actions remains queued/pending. No PASS claimed for full macOS, React/Vite or ShaderGraph compiler integration.

## Limitations
- Node position authoring only; connections, operation creation/deletion, parameter editing, graph application to materials and live GPU Shader Graph preview still require the existing native editor.
- ShaderGraphSet saves the project graph file immediately, outside MapDoc Undo. Browser uses explicit Save and discard.
- Any chart input copied from untrusted JSON must pass native graph validation before saving.
- Verify Safari mouse/touch drag and Native WKWebView behavior, compiler errors and CTest before merging.
- Keep existing ImGui production fallback until feature parity.

## Next
- Run Native/macOS CI and fix errors.
- Add graph operator catalog, node create/delete/wire editing using explicit compile validation, internal graph undo snapshots and GPU preview integration.
- Every stage requires a handoff.
