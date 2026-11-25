# AIVERSE Building Generator — Stage 23T.6: saved recipe editing, authentic layout preview and physical socket provenance

Date: 2026-11-24
Repository: chromealex/AIVERSE
Branch: feature/aiv20261123_building-grammar
Draft PR: https://github.com/chromealex/AIVERSE/pull/114

## User request / constraints

User approved continuing after implementation of 12 settings and their PBR image generator and skill catalog. Complete the remaining HTML5 Studio building authoring features: revisit existing generated building recipes, edit their dimensions/floors/material bindings/functional openings, expose a useful preview, and preserve authored stable physical socket identity. Architecture remains common ECS/PHYSIS/Renderer3D independent of genre. Tests deferred per user instruction; no false live-GPU claims.

## Existing APIs inspected

- apps/studio-native/StudioBuildingGeneration.h: generates new project/content/GeneratedBuildings/*.glb files and *.glb.recipe.json from normalized request data. studioBuildingInspect previously returned only CPU geometry count, and source generators already have accurate FunctionalBuildingResult.anchors and graph.sockets.
- apps/studio-native/StudioBuildingRegeneration.h: strict project-relative recipe file validator limited to direct GeneratedBuildings/*.glb.recipe.json, rejects symlink components and oversized files. Existing Regenerate always produces a new asset and never overwrites the original.
- apps/studio-native/StudioSession.cpp: existing native Studio RPC dispatcher.
- apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx: existing editable features, PBR material bindings, three-seed preview cards, native-generated 3D asset thumbnail and actual Scene placement with Undo; no saved recipe chooser.
- apps/studio-native/StudioHost.cpp and StudioScene.h/.cpp: assetThumbnail uses real native offscreen Model drawing; interactive Scene viewport is the existing GPU owner. No zero-write temporary building model viewport existed.
- libraries/include/aiverselib/systems/BuildingFeatureGenerator.h: FunctionalBuildingResult.anchors and graph.sockets are authoritative, with stable socket IDs, featureIndex and exact local coordinates.
- engine/buildings/BuildingApertureOwner.h: closed wall and sliding-roof bounds formulas; dynamic collider IDs must be allocated by actual scene owner, not guessed by the browser.
- apps/studio-web/src/types.ts / mock.ts: typed native messages and browser demo safety.

## Changes implemented in this stage

1. apps/studio-native/StudioBuildingRegeneration.h:
   - NEW studioBuildingRecipes(project): bounds directory scan to 2000, rejects unsafe or symlinked recipe directory and files, limits recipes to 256 KiB, returns sorted, content-relative GeneratedBuildings/*.glb.recipe.json paths. Does not crawl arbitrary filesystem paths.
   - NEW studioBuildingReadRecipe(engine,project,relativeRecipe): reuses studioBuildingRecipeFile for secure scope, checks schema 1, revalidates the authored architectural style from engine assets and complete native request via buildingStudioDetail::request; returns the request object for the UI.
2. apps/studio-native/StudioSession.cpp: native RPCs studio.buildingRecipes and studio.buildingReadRecipe using same session/project authority.
3. apps/studio-web/src/types.ts and mock.ts: declared both new RPC request/result types; the browser mock returns empty recipes and refuses fake project content reads.
4. apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx:
   - New Saved generated buildings section with refreshed recipe list and load operation.
   - Loading a source fills complete style/setting, mode, function, seed, 3D dimensions, floor/room lattice, functional building features and shared PBR materialBindings. Editing retains original GLB; Generate creates a NEW GLB+sidecar+GUID.
   - Native geometry preview includes functional graph statistics and interactive socket plan; selecting a real socket focuses its functional property input.
   - After normal Generate, manual regeneration or opt-in automatic reimport regeneration, attempts to load existing actual native 3D model thumbnail. Real Scene/Assets placement routes remain unchanged.
5. apps/studio-native/StudioBuildingGeneration.h:
   - studioBuildingInspect now returns authoritative FunctionalBuildingResult.anchors (socketId, featureIndex, kind, side, open fraction, width/height, actual outside coordinates), actual structure solids/connections and room grid/floor counts.
   - Each newly exported .glb.recipe.json in features mode contains physicalSockets metadata for operable walls/roofs with true graph socket IDs, featureIndex, kind, initialOpen and conservative local closedBoundsMin/Max following the existing BuildingApertureOwner wall/roof formulas at local origin/yaw 0. Runtime scene still owns world transformation and stable actual physical collision IDs.
6. NEW apps/studio-web/src/panels/generated/BuildingPlanPreview.tsx: top-down SVG layout inspector driven by actual native anchor coordinates and room grid. Keyboard/click interaction chooses a socket to navigate to its corresponding existing feature control. This is explicitly not a fake 3D render.
7. apps/studio-web/src/panels/generated/BuildingGeneratorPanel.css: responsive plan inspector styling.
8. skills/world/aiverse-building-style/SKILL.md: Stage 23T.6 authoring process, API, limitations and remaining zero-write 3D viewport gap.
9. This separate handoff satisfies the mandatory after-stage agent documentation.

## Commits (source provenance)

- a8b926a5f23272099dea42bfb1541b29ea5893f3 — safe saved recipe listing/reader.
- 777991345e0204bd63688c05b71621e6637345bb — native Studio RPCs.
- ff6b83d83875ea268ce000bc425da2174ffe800b — TypeScript request/result types.
- be07dcf81a2310dd4a7324477ac63b036d7e085c — load saved recipe into HTML5 editor controls.
- 82b2892ba1aabbcda34861d4cb7e824beb7312ff — authentic generated geometry socket/graph data.
- cb359b9d687f796190276140ae8777b18fd5241e — preview socket types.
- 676f249edfad1d279a361c4c581a0dcecc64bd66 — native-socket plan inspector.
- 41e50182fd12bd790bd7e30d33f53765780ef57d — plan marker TS readability.
- 7f2f432c2a72ab1a3845e14330f869ff2af4ba85 — plan integration and functional property navigation.
- 6efeb5e0eebcb6a3eba88e7b5d2bd4ed727b0387 — responsive plan CSS.
- 3b25fd32c4227d2f3d8b4cdc33e744c84f7eac57 — strict browser mock.
- b932d82742735cee1000773d3b3dcafc5fb3bdd4 — actual physical socket metadata in newly written recipes.
- 54cac0253966af43093338b6e82841413bd18a52 — room grid/floors in native preview.
- 300863ba1e9b2335b693dc305dec581b7c0c9e4a — TypeScript room grid typing.
- 8974f7c231dce3c38d5c884e4e91e8742057b278 — native room layout lines.
- 0da933a0dbcd8264ae654289ea71826efd58f865 — updated master skill.
- a521a518d28068d3d0d7c6c97b93f16d4fd6e85c — deterministic secure recipe list.
- 6aa7a4628c89cee154c30273ac3f9d38150dcdd2 — native 3D thumbnail refresh for created/reimported assets.
- This handoff is its own last documentation commit.

## Actual validation and limitations

Only source files were inspected and committed through GitHub. User deferred all tests. NO C++ CMake/CTest build, TypeScript/Vite build, native Studio RPC invocation, PNG/GLB validator, Model upload/GPU screenshot, PHYSIS collision or networking test performed; source-level commits are not evidence of successful execution.

The new preview is a top-down authentic graph/socket inspector: it does NOT provide a temporary interactive 3D viewport before GLB creation. The existing native assetThumbnail renders a real thumbnail for a generated asset, and the existing Place in Scene action allows interactive navigation after creating an actual scene entity. A true zero-write native 3D preview needs a GPU-thread StudioScene-owned temporary model and camera controls with safe cleanup/renderer leases, not a parallel WebGL previewer. All authored physical socket bounds are local and do not contain actual game-world transforms or collider IDs; the world scene must transform and lease unique IDs through its canonical collision namespaces. Existing old .glb.recipe.json files may lack physicalSockets until regenerated.

## Next step

Implement zero-write interactive 3D StudioScene preview of a generated CPU Model with real GPU resource lifecycle and no asset/file mutation, if desired, then Stage 23U deferred full native/web/glTF/PBR/GPU/PHYSIS QA, reconcile actual compile issues, update site devlog only with real engine screenshots and release tracking, and keep Draft PR #114 unmerged until proven. No new genre split or outdated FPS/ActorHost attachment.
