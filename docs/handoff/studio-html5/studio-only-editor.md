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

## Shader asset lifecycle and HTML navigation

ccba4b8fa removes legacy destinations from the HTML window menu and command palette. Native studio_session passed for Rules including global settings, whole document, rejected stale writes, triggers and damage matrix.

c5f97a81b adds New Graph, domain and minimum quality, and Apply saved graph to Shader Graph. shader_graph_create uses render::graph::makeDefault; creation publishes without replacing an existing asset. Apply uses shader_graph_apply for the material library, postfx chain or particle emitter. The actual engine roundtrip on a temporary Survivors project passed for all three domains, duplicate refusal with identical original bytes, postfx application, particle readback and shader parameter persistence. macOS CTest studio_session 1/1 passed, including the HTML bridge operations. Production frontend built; 201 web tests and 50 Python checks passed. Chrome exercised actual native MCP operations via a test bridge: create brushed_steel, save High quality, apply to steel and re-read quality. Screenshot: /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/shader-create-apply.png. Native product-shell screenshots and GPU preview remain pending.

## Unsaved material GPU preview

2a5498bbd adds Preview draft in Scene to the HTML5 Shader Graph panel. The typed studio.shaderGraphPreview command validates the project identity and material graph, then selects a transient preview without writing shaders, materials or map documents. The Scene renders its normal PBR model with a compiled graph via AppearanceAuthoringService, project-local image reads and the native Renderer3D. Return to map is available in Scene and Shader Graph. A failed texture load is reported in Scene status. Postfx and particle preview remain open.

macOS Studio Metal tests request two different unsaved swatch programs. The readback test requires red or green dominance over the central 16×16 pixels respectively and checks return to regular PBR/map rendering. The Metal test and Studio session suite passed together. Frontend typecheck/production build, 201 web tests and 50 Python Studio contracts also passed. The desktop host build and combined WKWebView interaction are separate from the Metal harness.

## Unsaved post effect preview

049b8b5b2 extends the same command and panel action to postfx. StudioScene owns a separate PRISMA compositor for the draft and releases it on preview changes. The map's postfx configuration stays intact. The native Studio Metal test in apps/studio-native/tests/StudioMetalLightingTests.mm now exercises red and green framebuffer readbacks for both material and postfx graphs. The macOS studio_metal_lighting test passed in 7.28 seconds and the updated production HTML5 bundle built. Particle draft preview, desktop UI acceptance, and legacy retirement are outstanding.

## Per-panel GPU preview and file-based materials

a18a64f80 gives Shader Graph and Materials independent drawables and renderers in their own panels on macOS and Windows. Native macOS acceptance captured docked-shader-graph.png, docked-materials.png and scene-with-asset-previews.png on the desktop Survivors fixture. studio_session and studio_metal_lighting passed, as did 201 web tests. Windows remains unverified. The character tools still share a Scene binding.

Next deliverable: authored materials are real content assets. Keep legacy inline libraries readable, use one material definition per file, and resolve the same files from Studio, MCP and game runtime. Verify the Assets panel after creation and after reopening.

## Material files visible in Assets

a355d87ae stores PBR definitions under content/materials/<name>.material.json. materials.json preserves name lookup using project-relative file paths. Shared resolution in engine/view/MaterialLibraryFiles.h supports legacy objects and file-backed entries from the game reader. MaterialOps owns persistence. Asset moves in Studio update paths using GUID relocation. Legacy inline entries convert explicitly or when saving the library. The Assets panel listens for material writes and exposes Edit material. Materials exposes Show in Assets.

Native macOS acceptance on a disposable Survivors copy converted nine legacy materials. Create and save added brushed_metal.material.json and the tenth asset appeared without manual reload. After Show in Assets the footer identified the selected file. Screenshot: /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/captures/created-material-file.png. Independent MCP processes created a file, read a direct external edit and persisted a merged change at the same path. studio_session and studio_pbr_adapter passed 2/2, production web build and 201 web tests passed, and six Substance tests passed. Windows and full merged-tree quick gating remain outstanding.
