# Studio regression checks on the SpectraFX branch

Commit `de3134fda` restores Catalog and TerraWeave Generator to the right stack of the default Studio layout. Dock tests check registered panel identities against their home stacks and exercise moves, closure and reopening with the current tab inventory. Existing saved layouts continue through the same normalization path.

The Python source contracts now check the shared StudioScene and StudioHost, the Mac key adapter, template copying, the routed design contract, and type scaling with native viewport geometry observation.

Verified on macOS with `npm --prefix apps/studio-web run test:web` (193 passed), Python 3.12 unittest discovery under tools/studio (47 passed), Studio web typecheck and production build. Four native CTest cases passed: spectrafx_metal_volumetric_scene, vfx_graph_runtime, studio_spectrafx_preview, and studio_spectrafx_fire_smoke. The docking layout was checked by model tests. Interactive visual acceptance remains separate.
