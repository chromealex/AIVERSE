# AIVERSE HTML5 Studio — Stage 43: validated PBR material preview contract

Date: 2026-11-20
Branch: feature/studio-html5
Parent commit: 1a372dbdd7562644cc843a84cb19d5dc3863d8b5
Commit: see current branch history.

## Implemented
- Created pure CPU adapter StudioPbrMaterialPreview.h, converting resolved materials.json factors into the REAL Renderer3D render::Material (metallic, roughness, base/emissive color, alpha mode, cutoff, double-sided, PBR/Unlit).
- Added allowlisted studio.materialPreview({name}) StudioSession method. Material names are validated, existence verified against existing project library and inheritance resolved via Workspace::material_get. Empty name disables preview.
- The native host can read previewMaterialName, previewMaterialFactors and a monotonically incremented revision from the same main-thread StudioSession; no DOM GPU/frame transfers.
- Material create/edit operations refresh preview factors, and opening another project clears preview state.
- Typed StudioMethod/Params/Result and mock guard added.
- CMake macOS build and studio_pbr_adapter CTest registered; native no-project negative test expanded.

## Files
apps/studio-native/StudioPbrMaterialPreview.h
apps/studio-native/tests/StudioPbrMaterialPreviewTests.cpp
apps/studio-native/StudioSession.h
apps/studio-native/StudioSession.cpp
apps/studio-native/tests/StudioSessionTests.cpp
apps/studio-native/CMakeLists.txt
apps/studio-web/src/types.ts
apps/studio-web/src/mock.ts
.github/workflows/studio-html5-web.yml
tools/studio/verify_mac.sh
docs/handoff/studio-html5/43-pbr-preview-contract.md

## Validation
GitHub branch updated and files verified. No full AIVERSE native compiler/Metal or React app test has run. New pbr_adapter test written but NOT EXECUTED. GitHub macOS CI currently queued. Do not claim material previews are already visible.

## Deliberate limits
- This preview converts scalar PBR factors only: shader graphs, images/textures, UV transforms and normal maps are not resolved/rendered. No path access from browser.
- No write to materials.json from studio.materialPreview; selecting a material is read-only.
- Stage 44 must create an actual AIVERSE procedural preview model and pass this state to native Renderer3D::drawModel, then add a Preview button in the HTML5 Materials panel. Run C++/macOS tests and visual smoke tests.
- Legacy ImGui editor remains the production fallback until parity.
