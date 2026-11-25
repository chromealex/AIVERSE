# Stage 38 — Building workspace and generated PBR

User supplied two real Studio screenshots: Building Generator opened in Iron and Gold; full-width forms hide core controls below long material lists, and dependency canvases show only a root node. User explicitly requires procedural textures. Previous Stage 37 automated checks passed; visual QA remains open.

- bcdce1f5313d69a2da21ee5d7846aa9089d87624: partition Building panel into Recipe, Features, Materials, Graphs, Runtime; responsive grids based on dock space, persistent primary actions, graph empty state instead of root-only canvas. Keep native generation/preview/save functions, tab cross-focus and saved recipes. Original scifi.json lacked all 16 procedural patterns; assign matching authored patterns and deterministic seeds. All 13 shipped JSON styles now contain patterns on all material slots, verified by source inspection. No change to imported GUID precedence.
- 0fea047cbfbdb09a76e5de867134c5e619551a34: native read-only studio.buildingSurfacePreview validates style ID and slot, returns three actual 128x128 PNG maps via existing CPU generator/PNG encoder/base64. Materials tab displays these raw style maps; preview tint comes from existing factors and imported textures override channels. No browser-generated fake textures, no file writes or separate texture Asset GUIDs; GLB embeds generated maps and native 3D preview uses the same CPU generator. Remove duplicate action bar; add serializer regression inspecting embedded PNG signatures and glTF map references.
- ff97becf30af2c1fb0ebb4557d6bb3409e1ca49c: dock-local focus/scroll handling, catalog and native PNG determinism/validation checks in studio_building_topology, include library_building_style in the incremental Stage 37/38 verification runner.

Run on Mac using existing KINGSTON checkout and all-tests-v2:
```sh
cd /Volumes/KINGSTON/AIVERSE-builds/AIVERSE-building-stage36c
git pull --ff-only
bash tools/studio/verify_building_integration_mac.sh "$PWD/all-tests-v2"
open "$PWD/all-tests-v2/apps/studio-native/AiverseStudioHTML5.app"
```

Then verify Materials > Generated PBR textures (choose hull/rust/glass), native 3D preview and Generate GLB, recipe/feature/graph tabs at normal/narrow widths, saved asset and actual runtime graph monitor. Existing GLBs are not silently modified. Current maps are procedural style-level 128x128, not unique high-resolution baked atlases or standalone reusable texture assets. Post-change TS/native compile, CTest and visual QA NOT RUN by agent. Live owner MCP and merged-tree main gate remain pending. Main untouched.
