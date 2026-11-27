# Additive material authoring — issue #219

Source: 10270e8728ff899d2aa814b96b2832cc40e76c86
Branch: codex/issue223-minimap-atlas-20261126

MaterialOps::checkMaterial and native StudioMaterialValidation now delegate alpha-mode validation to the existing render::parseMaterialBlend owner. The existing Studio selector offers alpha-weighted and unweighted additive modes. No new panel, shader, simulation state or save format. Invalid modes remain rejected. Native and browser regressions cover accepted values, invalid values and retained drafts. Manifest and in-progress roadmap updated before source edit.

Prepared identical frontend source passed 11 material tests, TypeScript and Vite production build in KINGSTON studio-source/apps/studio-web. First staging build lacked the relative branding resource; corrected staging layout passed. Vite retains its existing chunk-size warning. Prepared native StudioMaterialValidationTests passed with AppleClang C++20. These are isolated subset checks; MaterialOps and the full Studio executable have NOT yet been rebuilt from this commit. Existing complete Mac quick gate on d88e86901: 548 passed/5 skipped/0 failed, with Studio disabled; it is historical evidence, not validation of this new source.

Next: retain KINGSTON cache and enable AIVERSE_BUILD_STUDIO_HTML5 with AIVERSE_STUDIO_WEB_DIST pointing to the isolated verified bundle; rebuild affected consumers/tests, run native Studio contracts and real material_set save/read after the previous rejection. Run final full gate after all remaining source changes. Web-controls build handle 7239 continues using unchanged runtime sources. No main merge or issue closure.

## Native Studio authoring acceptance

Evidence commit 313dd710175dc1442e18e698f0a85e0f17cfe336. Native build9692 exited0: MCP, Studio and material validation target built from source4d6f08f19 in existing KINGSTON cache. `bash run-tests.sh --quick --no-build --filter 'studio_material_(validation|schema)' --build-dir <KINGSTON task cache>` at d569f6c6c: 2 CTests passed,0failed; report20261127-112244-77762. MCP registry audit95150 exit0:448 tools,18 required families, material_get/material_set registered.

Launched native Studio (82772) with bundled production frontend. Opened isolated issues-mcp-authoring project through native dialog; Materials issue219_0 switched weighted->unweighted->weighted, each Apply & save and Reload retained correct value. Two actual screenshots and test summary retained in docs/evidence/issues-studio-materials-20261127. Disk materials.json agrees. Existing UI explicitly excludes MapDoc Undo for library edits; no unsupported undo claim. These checks do not prove Windows issue206. Native UI remains open on isolated fixture, no runtime game running. Web build25810 remains live in Emscripten link/system-library generation. Next finish web and browser acceptance, delivery manifests/site, final all-target gate on final source, squash integration.
