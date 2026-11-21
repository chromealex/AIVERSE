# Built-in terrain texture quality

Ground without authored `terrain.layers` previously used vertex colour and, for
smooth terrain, a coarse detail-surface texture. Medium and High now add a
procedural world-space grain while preserving the game palette. Three-dimensional
coordinates keep the grain at the same scale on flat ground and steep cliffs.

Medium combines broad soil variation with fine grain. High adds a finer band and
small surface-gradient normal perturbations, capped to avoid overly rough lighting.
Each band fades when it becomes subpixel; distant ground does not shimmer.
Low retains the previous vertex-colour path, and authored splat/normal-map materials
retain their existing rendering. No textures or extra GPU memory are allocated.
The GLSL and MSL implementations use the same frequencies, strengths and filtering.

This improves the default ground material; it does not increase geometry resolution
or replace user-supplied low-resolution textures. `AIVERSE_TERRAIN=low|medium|high` can be
used to compare tiers on the same scene.

Validation (2026-10-28): macOS Release client/editor built, six terrain MSL
permutations compiled through Metal runtime, and all 4942 unit checks passed.
Seed-3 skirmish and mushrooms matches finished with winners. Before/after Metal
captures at 960x640 were inspected; Low is byte-identical. High/Medium captures
completed with Metal validation enabled and no validation errors. Existing
unrelated build warnings remain. GL/GLES/WebGL, Android and iOS runtime paths
and GPU performance on weak devices remain unverified.

## Bundled PBR starter library (2026-11-20)

`engine/assets/materials/terrain/` contains six 1024² dielectric materials: checkerboard, grass, soil, sand, rock, snow. Each has base color, +Y tangent normal, roughness, AO, metallic, ORM, 16-bit height and two terrain-packed maps. See its README and catalog for scales and limitations. Blank (`empty`) editor projects install these assets and checkerboard automatically; existing projects retain their configuration. `python3 tools/terrain/install_pbr_pack.py PROJECT --default-checkerboard` is an explicit opt-in installer.

The shared model material library uses existing PBR shading. Terrain opts into GGX sun lighting with `terrain.pbr.enabled`; High reads AO/basecolor and roughness/normal packed alpha channels. Low/Medium use uniform roughness. Local lights remain diffuse-only; this does not implement specular environment IBL or height displacement. Natural source maps are AI-authored approximations, not measured scans.

### Expanded authored PBR library (2026-11-20)
The bundled catalog now contains 10 materials / 90 maps: grass, soil, sand,
rock, fresh snow, forest floor, wet soil, gravel soil, snow crust and checkerboard.
Fresh snow and compacted crust are separate dielectric surfaces; this does not
implement automatic accumulation on tree crowns. Existing project installation
still refuses overwrite; only a new empty project receives checkerboard by default.
