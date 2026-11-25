# HTML Studio as the sole desktop editor

Retire the native ImGui application and route builds, launchers and packaging to HTML Studio. Preserve shared document, native project and MCP code. Verify CMake targets, release archives, Studio contracts and the integrated Mac build.

## Input and output wiring

The owner requested migration of missing workflows before removal. Keep the old editor, its launch paths and build target until parity is verified.

Implementation db00bf2eb updates apps/studio-web/src/panels/ShaderGraphPanel.tsx, ShaderGraphPanel.css, shaderGraphLayout.ts and tests/shaderGraphEdit.test.mjs. Select a destination node to edit ordered inputs. Graph outputs expose material channels or the single color channel for postfx and particle assets. Native shader_graph_validate and shader_graph_set remain the compilation and persistence owners.

Verification on macOS: npm run test:web passed 198 tests. npm run build completed TypeScript and Vite, producing /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/dist. Native-shell interaction, GPU preview and the merged-tree Mac gate remain pending. The legacy editor is still enabled.

Continue with draft editing and graph preview across domains, then verify the outstanding workflows in docs/studio-html5-parity.md, the MCP live editor link, build targets and packaging.

## Window inventory, Static Geometry and Rules

Inventory 8e00b5b6a compares 51 legacy registration files with the earlier 49-row inventory. Static Geometry and LENS / Cameras were missing from that table. The 59-panel visual review measured the previous HTML registry.

Static Geometry implementation 47396ae81 adds a dockable window, geometry_instances_list and the narrow StudioTools allowlist for the existing geometry_strata_set. Both read the configured game.json geometryScene. The focused regression for all three modes lives in tests/geometry/runtime.inc. Rules implementation b8250d349 adds an armor-by-attack table, trigger and damage document editing, and native bridge checks in StudioSessionTests.cpp. Responsive follow-up 3a8caf3ee stacks the Rules navigation above the form at narrow dock widths and aligns geometry mode controls.

Verified production TypeScript/Vite, 198 web tests, and 50 Python Studio contracts. Chrome exercised both real panel components with a fixture bridge at 800 and 240 CSS pixels. Geometry mode selection and reload preserve the adjacent object's mode. Rules tested save, matrix refresh, invalid JSON and discard. Four screenshots under /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/{static-geometry,rules-matrix}-{240,800}.png. An initial screenshot exposed a 70px Rules editor at 240px; the responsive follow-up was visually rechecked.

An isolated executable linked against older cached native libraries returned std::bad_alloc. A fresh native build reached 93 percent before the external build directory disappeared. The production web bundle, tests and screenshots were subsequently regenerated. An independent build from the branch at /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/native is in progress. Verify mode persistence across separate MCP processes and run studio_session; previous failures do not constitute native acceptance. Full merged-tree gating and legacy cutover remain outstanding.

## Shader graph properties, local history and direct wiring

5a2b62169 ports constant and parameter RGBA editing, existing parameter overrides, texture paths, sRGB and swizzle masks. c108245e8 adds a 64-step history that survives saving and treats each node drag as one edit. 303596014 enables pointer and keyboard connections on the SVG canvas. Native graph compilation and persistence remain the existing Workspace owners.

The macOS production web build passed, including TypeScript. All 200 web tests and 50 Python Studio contracts passed. Chrome at widths 800 and 240 exercised opening a graph, changing an overridden blue component from 0.25 to -0.75, explicit save, undo after save, redo, keyboard rewiring of the second input and undoing that connection. The component fixture used a bridge double. Local screenshots: /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/shader-fields-{240,800}.png. Native persistence and real GPU graph preview are separate acceptance checks.

## Fresh native persistence and rules documents

Fresh MinSizeRel macOS x86_64 aiversemcp build completed on KINGSTON. /tmp/studio-geometry-roundtrip.py copied examples/genres/rts/survivors to a new task-local temporary project. Each call ran in its own process. geometry_strata_set and geometry_instances_list passed STRATA, LOD and Auto with a custom content/custom-geometry.json path, preserved the adjacent instance and returned an error with identical file bytes on unknown ID. shader_graph_set and shader_graph_get reloaded the negative blue RGBA override exactly. This supersedes the failed mixed-library probe and interrupted build.

069d814e8 ports global settings and complete rules.json editing to the typed Studio bridge. Global settings preserve the independently authored collections. The save compares the opened project and original document/section with the current workspace before calling the existing validated rules_set. Production web compilation, 200 web tests and 50 Python Studio contracts passed. Chrome fixture tested global save, full-document readback and section switching at 800 and 240 CSS pixels. Native StudioSession tests for shape validation, stale documents, global preservation and full document save have been added.
