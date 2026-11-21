# AIVERSE terrain PBR starter pack

Six authored dielectric materials: checkerboard (default), grass, soil, sand, rock, snow.
Runtime maps are 1024² PNG; height is 16-bit. Base color is sRGB; all other channels are linear.
Normal maps are tangent-space +Y (OpenGL). `orm` stores AO / roughness / metallic in R/G/B.
Metallic is zero. Individual roughness/AO/metallic files are supplied for DCC interoperability.
Height is an authored approximation, not a measured scan and not automatic geometry displacement.

`materials.json` uses the engine's existing shared PBR model-material schema. Copy it into or merge
it with the project's material library. `catalog.json` records physical repeat and relief scales.
The terrain adapter uses two packed maps without additional sampler slots:
`terrain_color` = base color RGB + AO A, `terrain_normal` = normal RGB + roughness A.
Enable `terrain.pbr.enabled` and `terrain.pbr.packedMaps`; High reads packed maps. Low/Medium
retain a uniform dielectric roughness fallback. Metallic terrain is not supported by this adapter.
Sun lighting uses GGX; local lights retain diffuse-only contribution in this first adapter.
No full environment specular IBL or displacement is implied.

Blank projects created with `template=empty` receive this library and checkerboard terrain.
Existing authored projects/templates are not changed. Use `tools/terrain/install_pbr_pack.py`
to install into an existing project; `--default-checkerboard` explicitly switches its terrain.
Natural layer entries are in catalog.json; up to four terrain layers can be active at once.

Sources were created with OpenAI built-in image generation on 2026-11-20, with separate matching
height authoring, then baked deterministically by `tools/terrain/bake_pbr_pack.py`.
No third-party photographs/textures are incorporated. Released with the repository's MIT license.
Source maps and provenance live in tools/terrain/pbr_sources (not in runtime content).
