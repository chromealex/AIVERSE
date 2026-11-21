# AIVERSE Studio HTML5 — Stage 39: safe native Materials library bridge

Date: 2026-11-20
Branch: feature/studio-html5
Parent: 73cfec748b986eaf25ffc179824c0a6c5daf4227
Stage commit: see HEAD.

## What changed
- Added a tightly allowlisted, genre-neutral materials API: `studio.materials` returns the material-library object for the open project, and `studio.materialPatch` updates only existing materials.
- Native patch validation limits input to 1..15 fields and only allows numeric PBR factors, supported boolean flags and safe enum/text fields. No arbitrary JSON, texture paths, shader source, file I/O paths or asset deletion are accepted from the HTML frontend.
- `studio.materialPatch` delegates to the existing `Workspace::call("material_set")` with merge=true, preserving MaterialOps native PBR validation, project-library file format and change notifications.
- `StudioMaterialValidation.h` is a pure C++17 validator for native authorization; added headless unit test.
- Updated CMake/CTest, Linux CI and one-pass macOS validation script.

## Files
apps/studio-native/StudioMaterialValidation.h
apps/studio-native/tests/StudioMaterialValidationTests.cpp
apps/studio-native/StudioSession.cpp
apps/studio-native/CMakeLists.txt
.github/workflows/studio-html5-web.yml
tools/studio/verify_mac.sh
docs/handoff/studio-html5/39-native-materials-bridge.md

## Important data-safety constraint
**MaterialOps::material_set writes materials.json immediately**, unlike MapDoc's undoable entity edits. The HTML5 Materials panel MUST offer an explicit Apply command and show "saved to disk, no Undo available". Do not misrepresent this operation as part of the current MapDoc undo stack.

## Tests
- Pure Material validation contract compiled on Linux using C++17 -Wall -Wextra -Werror -pedantic: PASS.
- Existing StudioSession no-project negative checks authored, not compiled against entire engine yet.
- Full macOS/Metal compiler, TypeScript/Vite UI tests: NOT RUN. GitHub Actions may remain queued.

## Limitations/next
- Native bridge exposes a safe, deliberately limited subset: no full material graph/shader editor, texture assignments, material creation/deletion or node overrides.
- Stage 40: integrate HTML5 Materials editor with local draft + explicit Apply, typed PBR controls, read-only materials preview and project reload; document unsupported features and no Undo semantics.
- Full editor parity and Vulkan/GLES host support remain outstanding; retain ImGui fallback.
