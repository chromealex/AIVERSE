# Authored vegetation materials

Run `PYTHONPATH=... python3 tools/vegetation/bake_pbr_pack.py` with NumPy and Pillow.
Sources are OpenAI image-generation outputs (2026-11-20), distributed with the repository MIT license; no scanned/photo asset is incorporated. Albedo prompts requested unlit orthographic oak bark and transparent 2x2 atlases of oak and myrtle-like shrub sprigs. Bark height was separately authored from the bark reference. Runtime roughness, cavity AO and normals are deterministic approximations, not physically measured maps. Leaf normals are intentionally restrained.

Copy `engine/assets/materials/vegetation` to a project's `materials/vegetation` and merge its `materials.json` into the project root library. IDs are `oak_bark`, `oak_leaves`, `shrub_leaves`. Do not overwrite existing project assets. The standard model renderer consumes basecolor/normal/ORM/AO; foliage uses alpha MASK at 0.5 and double-sided rendering. The optional translucency map is supplied as authoring data only: the current standard PBR shader does not consume it. No subsurface/backlight render claim.

The two leaf atlases have four quadrant variants. Use local UV subrects with a small inset for each sprig. Bark repeats at approximately 1.5 m; atlas scale is approximate. These materials do not automatically replace existing procedural TerraWeave prototype materials or their impostors.

The expanded library has four barks (oak, pine, birch, spruce) and four foliage
atlases (oak, shrub, spruce, pine), 64 runtime maps. Birch relief is 4 mm,
spruce 15 mm, pine 28 mm and oak 35 mm at the declared 1.5 m repeat.
The offline model author supports `--bark` and uses the selected material's
repeat/relief when baking its connected atlas. This does not yet wire ten
species, ages or winter geometry into the native Forest adapter.
