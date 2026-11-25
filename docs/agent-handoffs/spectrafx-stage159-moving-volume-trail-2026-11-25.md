# Moving volume sources

Implementation `aed9dda05`, roadmap `1bcdf6033`. Set `VolumeDomain.attachment` to `trail`. The scene synchronization path moves authored nozzles, lights and pending impulses in world space. The Eulerian grid and obstacles keep their initial placement. Existing smoke continues evolving. Source identity and revision must remain stable. Size the authored domain for the route.

The Studio preset and attachment selector use the existing native graph editor operations. See the [Studio handoff](../handoff/studio-html5/spectrafx-moving-trail-preset.md). The editable example is `examples/spectrafx/moving_smoke_trail.vfx.json`. The development log includes a native Metal capture of the example with a translated source.

The Metal scene test advances two source positions and reads the GPU cells back. It checks density at both positions, the intervening empty cell, the original grid origin and the simulation tick. The graph test creates the trail preset and compiles it through the presentation graph API.

Next acceptance step: save the graph on a moving entity in a Studio project, reopen it, and capture the running game. Vulkan device, mobile frame time, rotation, and moving-domain streaming remain outside this verification.

On macOS, CMake built aiverse_client and aiverse_studio. CTest passed all four targeted cases: spectrafx_metal_volumetric_scene, vfx_graph_runtime, studio_spectrafx_preview and studio_spectrafx_fire_smoke. Studio web typecheck and Vite build passed. The site checker validated 54 entries with zero errors. The four docking-layout failures and ten Python contract failures reproduced at the pre-implementation revision.
