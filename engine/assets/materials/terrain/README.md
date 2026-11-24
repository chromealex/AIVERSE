# Default terrain material (checkerboard)

The engine ships only the neutral checkerboard: 1024² dielectric PBR maps, `materials.json` with
`terrain_checkerboard`, `catalog.json` and `default-terrain.json` (the terrain block of a new blank
project). The editor's empty project copies `textures/` into `<project>/materials/terrain/textures`
and merges the library into the project's `materials.json`.

The full terrain library (grass, soils, sands, rock, snow and more, with this checkerboard) is the
catalog package **terrain-pbr** in AIVERSE-PACKAGES:

```sh
python3 tools/pkg/aiverse_pkg.py add terrain-pbr --project <project>
```

It installs over the empty project's checkerboard without conflicts (same files, same id). Its
sources are the package `terrain-pbr-sources`; re-bake with
`tools/terrain/bake_pbr_pack.py --root <working copy of the terrain-pbr payload>`. See docs/packages.md.

Maps: base color sRGB, everything else linear; normals +Y (OpenGL); `orm` = AO / roughness /
metallic; `terrain_color` = base color + AO, `terrain_normal` = normal + roughness (the terrain
adapter's two packed maps). Released with the repository's MIT license.
