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

## Live particle Shader Graph preview

c19febd78 connects particle-domain drafts to the panel's independent GPU drawable through the shared fx::ParticleSystem. All three shader domains refresh after semantic edits, with node position changes excluded from the compile key. The project-local texture reader supplies particle textures. Native shader errors are displayed by the graph panel.

The first Metal readback exposed the duplicated fragment uniform parameter and obsolete default-body substitution in engine/fx/ParticleSystem.cpp. The shared generator now emits its complete particle graph fragment. A desktop cold start also exposed an access to an uninitialized material sphere. The Metal test now opens the particle domain first and verifies both red and green pixels for each domain. Final macOS CTest studio_metal_lighting and studio_session passed 2/2. Production frontend and 201 web tests passed. Desktop acceptance opened examples/shader_graph/particle_age.json on the Survivors copy, added and discarded a node, and captured two consecutive frames. Only the preview rectangle changed (bounding box 887,319 to 1058,462 inside the 1400 by 830 window). Capture: /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/captures/particle-draft-gpu.png. Character preview migration, other legacy workflows, Windows acceptance and the merged quick gate remain open.

## Animation graph parity in progress

The old AnimationEditorPanel uses the native AnimationDocument operations for pose-node wiring and graph output. The HTML panel previously offered generic operation forms. Delivery includes visual editing of the pose topology, named graph scopes, parameter-based state transitions, model validation and compare-before-save. Preserve native model and rig inspection and montage operations while the isolated skeletal preview is outstanding.

## Animation Graph desktop acceptance

9c64d116b90c71baacad0fdc737a6f19f8bbe87e adds apps/studio-web/src/panels/AnimationGraphPanel.tsx and AnimationGraphPanel.css, with native draft operations in apps/editor/core/AnimationOps.cpp. Pose links reuse AnimationDocument cycle checks. State transitions retain destination, fade, delay and parameter conditions. Drafts survive switching dock tabs in the same window. The Operations tab keeps the existing advanced commands.

Independent MCP processes tested add, connect, cycle refusal, model compilation (Idle and Walk clips), state-transition persistence and stale saves. The initial WKWebView save falsely reported a stale graph even immediately after loading because the browser reserializes its JSON. Native get/save now return a SHA-256 revision of the native graph for the HTML save precondition; native callers with an expected JSON graph stay compatible. Rebuilt desktop Studio added and saved a second transition, confirmed it on disk and displayed two transitions after Reload animation graph. studio_session passed including revision mismatch and project identity checks. Production frontend and 201 web tests passed; Python Studio discovery ran 50 with one skip. Captures: /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/captures/motion-pose-graph.png and motion-state-transitions.png. Isolated skeletal playback and rig editing remain pending, as do Windows acceptance and the merged-tree Mac quick gate.

Animation Rig stage in progress. Goal: edit ordered rig units in a graph draft, preserve the existing operations panel, validate and save through the native graph owner.

8243fdcc87c203dd6c6370c3c1cb46bade3023bf adds apps/studio-web/src/panels/RigInspector.tsx and native addUnit/addContacts actions in apps/editor/core/AnimationOps.cpp. The graph and rig windows keep independent drafts with the existing native save revision check. Rig units support all seven legacy presets, ordering, removal and nested field editing.

Mac production build and 201 web tests passed. studio_session passed including surface velocity and foot/root contact preset assertions. Separate native MCP processes created, compiled, saved and reopened a two-unit graph on content/motion_example.gltf. In the actual bundled Studio the Move operation 2 up and Save graph buttons changed the disk order from transform,limit to limit,transform. Validate reported successful model compilation. Capture: /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/captures/ordered-rig-units.png. Next: add and remove nested rig collection entries, then isolated model playback and picking. Full retirement, Windows acceptance and merged-tree quick gate remain outstanding.

Rig collection stage. Add and remove individual solver inputs and contacts in HTML5, preserve fixed vector dimensions, verify saved values on disk in desktop Studio.

fa6c4403c91e8bcbd60ecff03ce8305a9a74cf3f adds rigItemPresets in apps/editor/core/AnimationDocument.cpp, supplied by animation_graph_list. RigInspector.tsx presents editable solver/contact lists for all seven item types and keeps coordinate vector sizes fixed. Draft changes reuse the panel's undo, disk revision guard and validation. Missing optional lists are displayed without writing keys until edited.

Mac production bundle built; 201 web tests passed, studio_session passed with checks on the native preset catalog. In the packaged desktop app, added an effector, removed it, removed the final original effector, added a box obstacle, and saved. Independently parsed disk JSON contained zero effectors and the expected box with three component extent. After restarting the app, reopened the saved asset and compiled against content/motion_example.gltf. Final screenshot: /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/captures/rig-contacts-layout.png. Next: extract character preview ownership from StudioHost's Scene renderer and implement isolated model playback/picking. Full gate and editor retirement are pending.

Facial Rig stage: give semantic rig editing an independent drawable, camera, binding and native GPU status. Exercise alongside the authored Scene and check the model frame.

95561accbe4bdb7995d9ca447056b560d2e8473f routes FacialRigPanel.tsx to the native FacialRig drawable. StudioHost::tickFacialRigPreview owns the renderer and reads only the facial rig binding. Session checks verify that binding a model preserves the material and shader previews and leaves the Scene character binding idle. mac/Main.mm and win/Main.cpp allocate an additional platform drawable. Windows runtime has not been exercised.

Mac bundled desktop acceptance loaded robot_expression with robot_face.gltf in the Facial Rig panel and rendered a shaded model with Scene hidden. Docked Facial Rig to the right of Scene and rebound the same model. Window capture shows authored map and shaded model at the same time: /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/captures/facial-rig-and-scene.png. 201 web tests passed. Studio Python discovery ran 50 tests with one skip. studio_session and studio_facial_gpu passed 2/2. Next: move Human Creator and appearance tooling including sculpt hit tests, camera and wardrobe capture off Scene, then complete independent skeletal graph playback. Overall platform and retirement gates remain open.

Character surface stage: move appearance, DNA and sculpt rendering out of the Scene renderer. Verify control routing and simultaneous desktop views.

0292f5b1039e168f2cedf4b93a199a076afd478a applies the responsive HTML page contract to every nested dock and floating window in NestedDockWorkspace.tsx, with shared styling in ui/studio-page.css. Character Creator and Appearance use a two-column page on wide docks, a single column on narrow docks and native model views with their own cameras. StudioHost renders Scene asynchronously without passing the human binding. Native photo overlays and brush input target the character viewport on macOS. The Windows host has separate child windows and pointer routing. Windows runtime and reference compositing are still pending.

Mac bundled desktop acceptance: enabled VIVID DNA preview, loaded and applied robot_face.gltf in Appearance, moved Appearance to the right dock, opened Scene, then physically dragged across the robot. The model rotated while the map view retained its original framing. Captures in /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/captures: character-dna-page.png, appearance-page.png, appearance-narrow-scene.png, appearance-orbit-physical.png, materials-page.png and rules-page.png. Production web build passed, 201 web tests passed, studio_session and studio_facial_gpu passed 2/2, Studio Python ran 50 with one skip. The session test covers simultaneous procedural-character, semantic-rig, shader and material bindings. Earlier captures exposed an occluded native layer under WebKit; the final captures verified the corrected layer order. Individual visual review of remaining windows, end-to-end sculpt/reference/wardrobe acceptance, skeletal playback, platform migration and merged-tree gate remain open.

Animation playback stage: compile the selected graph and model into a live native instance, show evaluated skeleton coordinates and pick joints inside the owning HTML panel. Verify movement, panel isolation and stale frame rejection.

00edbc14a41bac397e24512315243cf0884ddcd8 implements native per-panel animation playback in apps/editor/core/AnimationPreviewSession.h and projected joint selection in apps/studio-web/src/panels/AnimationSkeletonPreview.tsx. The desktop macOS QA bundle played and paused Wave in Animation Rig, selected Wrist.R on the skeleton and saved the selected bone to animations/a_arm_preview.json. Verified the bound bone in the saved file. Full-window captures animation-skeleton-preview.png and animation-rig-selected-joint.png are under /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor/captures. Production web build succeeded. Focused CTest studio_animation_preview and studio_session passed 2/2; the former checks actual joint displacement, independent bind pose, foreign and expired tokens with a generated animated glTF. An earlier automation attempt pressed the global game Play; the verified run targeted the accessible skeleton playback controls. Next: skinned model and ground-contact authoring in the local animation preview. Full merged-tree gate and legacy retirement remain pending.

Local rig environment stage: provide a per-preview ground plane and native contact and climbing diagnostics in the skeleton page. Verify that moving one plane changes its solver result independently.

8507a86d0d1769f9caa12af5b316affe44c246cc binds the preview ground to the native animation environment in apps/editor/core/AnimationPreviewSession.h and displays the evaluated result in apps/studio-web/src/panels/AnimationSkeletonPreview.tsx. tests/animation/preview_session.py verifies losing a support while the second panel retains it. Mac CTest studio_animation_preview and studio_session both passed; 201 web tests passed and the production bundle built. Build evidence under /Volumes/KINGSTON/AIVERSE-builds/studio-only-editor: ground-web.log, ground-web-tests.log and ground-host.log. The first build command used an incorrect desktop target after building aiversemcp; corrected target aiverse_studio linked successfully. Actual desktop QA captures/animation-ground-contacts.png and captures/animation-ground-unsupported.png show the two-foot rig at ground zero and −10 while paused. Next: skinned model preview on an independent render surface and interactive vector handles. Climbing fixture, remaining window review, cross-platform runtime and merged-tree acceptance outstanding.

Vector controls stage: move rig targets directly on the projected skeleton and retain preview controls as undoable defaults in the selected graph scope. Verify physical drag, solver motion and persistence.
