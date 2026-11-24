# Authored vegetation materials

The library and its sources are catalog packages (`vegetation-pbr`, `vegetation-pbr-sources`; docs/packages.md).
Re-bake with NumPy and Pillow: `python3 tools/vegetation/bake_pbr_pack.py --root <working copy of the vegetation-pbr payload>` (sources come from the catalog).
Sources are OpenAI image-generation outputs (2026-11-20), distributed with the repository MIT license; no scanned/photo asset is incorporated. Albedo prompts requested unlit orthographic oak bark and transparent 2x2 atlases of oak and myrtle-like shrub sprigs. Bark height was separately authored from the bark reference. Runtime roughness, cavity AO and normals are deterministic approximations, not physically measured maps. Leaf normals are intentionally restrained.

Install into a project with `python3 tools/pkg/aiverse_pkg.py add vegetation-pbr --project P` (copies into `materials/vegetation` and merges `materials.json`). IDs are `oak_bark`, `oak_leaves`, `shrub_leaves`. Do not overwrite existing project assets. The standard model renderer consumes basecolor/normal/ORM/AO; foliage uses alpha MASK at 0.5 and double-sided rendering. The optional translucency map is supplied as authoring data only: the standard PBR shader does not consume its spatial values. Materials now opt into thinTransmission=.35 for bounded two-sided sun diffuse transport, not volumetric subsurface scattering.

The two leaf atlases have four quadrant variants. Use local UV subrects with a small inset for each sprig. Bark repeats at approximately 1.5 m; atlas scale is approximate. These materials do not automatically replace existing procedural TerraWeave prototype materials or their impostors.


Shared projected PBR bark is now bundled in `bark_projection`: four family
variants, two shared sources/six samples each. Also merge its `materials.json`
when installing the vegetation library. Read
`docs/terrain/vegetation-authoring.md` for native Arbor presets, age/winter,
neighbor palettes, root skin, UV fallback and projection binding.
