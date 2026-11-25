# TerraWeave meadow grass: species, clumps, fields

Natural grass for any project (no genre split): several species with their own
blade shapes and colours, tufts with bare gaps, and large-scale variation so
whole fields to the horizon do not look uniform or tiled.

Code: `engine/world/TerraWeaveGrassSpecies.h` (species, presets, clumps, fields;
pure CPU, integer hashing), `engine/world/TerraWeaveGrassSpeciesJson.h` (JSON),
`engine/render/TerraWeaveGrassRender.h` (`makeMeadowGrassPatchModel`, LOD,
tier budgets). Test: `tests/terraweave_grass_meadow_test.cpp`
(CTest `terraweave_grass_meadow`).

## Compatibility

`GrassRenderQuality::meadow` is empty by default. Empty = the legacy generator,
byte-for-byte (the test compares against a frozen copy of the pre-meadow
generator, `tests/terraweave_grass_legacy_reference.h`). Existing exported
grass assets and maps are untouched until a project opts in.

## Authoring (JSON)

The same `"meadow"` value is accepted by every host:

- vegetation exporter ground snapshot: top-level `"meadow"` (default for all
  patches) and per-patch `"meadow"` override; `bladeSegments` (default 4 in
  meadow mode);
- PHYSIS nature preview (editor): map block `"grass": {"meadow": ...}`, or
  `view_wind grassMeadow=...`, or the TerraWeave meadow operation in the Studio Terrain panel. Auto uses legacy grass when the map has no meadow block;
- C++: `parseGrassMeadow(json)` or `grassMeadowPreset("meadow")`.

```json
"grass": {
  "meadow": {
    "preset": "meadow",
    "seed": 7,
    "species": [
      {"preset": "meadowFine", "weightPermille": 500},
      {"preset": "broadBlade", "weightPermille": 220, "heightCm": 55},
      {"preset": "tallDry",    "weightPermille": 160},
      {"preset": "wildflower", "weightPermille": 40, "headColor": [200, 90, 200]},
      {"name": "reed", "heightCm": 90, "halfWidthMm": 7, "tipColor": [190, 180, 120],
       "head": "spike", "headPermille": 300, "fieldAffinityPermille": 100, "fieldTolerancePermille": 300}
    ],
    "clumps": {"spacingCm": 55, "radiusCm": 22, "gapPermille": 250, "mixPermille": 120},
    "field":  {"periodCm": 2500, "densityContrastPermille": 400, "dryPatchPermille": 300}
  }
}
```

A plain string is a meadow preset: `"meadow"`, `"lushMeadow"`, `"dryMeadow"`,
`"pasture"`, `"lawn"`, `"steppe"`, `"wetland"`, `"legacy"` (old look).
`"species"` replaces the preset's species list; `"clumps"`/`"field"` override
single values. Unknown keys/presets and out-of-range values fail with readable
errors (`grass meadow: species[0]: unknown species preset 'bamboo'`).

### Species fields

| field | meaning | range |
|---|---|---|
| `preset` | start from `meadowFine`, `broadBlade`, `tallDry`, `timothy`, `lawn`, `clover`, `wildflower`, `sedge` | |
| `weightPermille` | share in the mix | 0..1000 |
| `heightCm`, `heightVariationPermille` | mean leaf height, +- variation | 3..150, 0..800 |
| `halfWidthMm`, `widthVariationPermille` | blade half width | 1..120, 0..800 |
| `curvaturePermille`, `droopPermille` | tip sweep; long blades arcing back down | 0..1000 |
| `leanPermille` | lean away from the tuft centre (tufts fan open) | 0..1000 |
| `twistPermille` | twist along the blade (up to 180 deg) | 0..1000 |
| `taperPermille`, `shoulderPermille` | strap (0) .. needle (1000); where full width is reached | 0..1000, 0..900 |
| `segments` | ribbon segments, capped by the host's `bladeSegments` | 1..4 |
| `rootColor`, `midColor`, `tipColor`, `dryColor` | sRGB `[r,g,b]` gradient root->tip, straw | 0..255 |
| `colorJitterPermille`, `dryPermille` | per-blade brightness/hue jitter; share of dry blades | 0..500, 0..1000 |
| `head` | `none`, `panicle`, `spike`, `flower` | |
| `headPermille`, `stemHeightPermille` | share of flowering stems; stem height vs leaves | 0..1000, 500..2500 |
| `headLengthMm`, `headHalfWidthMm`, `headColor` | seed head / flower size and colour | 4..300, 1..80 |
| `fieldAffinityPermille`, `fieldTolerancePermille` | where in the species field (0..1000) it dominates; 1000 = everywhere | |
| `flexPermille` | wind response (sway weight) in hosts with wind | 100..2000 |

### Clumps and fields

- Clumps: one candidate tuft per jittered grid cell (`spacingCm`, jitter inside
  the inner 80% of the cell = blue-noise-like minimum spacing), radius
  `radiusCm` +- `radiusVariationPermille`. `gapPermille` cells stay empty (bare
  ground between tufts); `centreDensityPermille` makes centres dense and rims
  sparse; `clumpedPermille` of blades are in tufts, the rest is low scattered
  fill (`fillHeightPermille`). Each tuft has one species, `mixPermille` blades
  come from other species; centre blades are tallest, rim blades lean out.
- Fields: two-octave integer value noise with `periodCm` drives species zones,
  density (`densityContrastPermille`), height (`heightContrastPermille`) and dry
  yellow areas (`dryPatchPermille`). Density further multiplies with the host's
  coverage/cover masks (exporter `coverPermille`).
- Everything is anchored to world centimetres and `seed`, not to the patch:
  tufts and dry areas continue across patch borders, no per-patch tiling.

Existing placement filters still apply to every blade root: water and shore
sand bands, roads, exporter cover masks, slope masks supplied by the host.

## Rendering and budgets

- One opaque, double-sided, vertex-coloured material per patch (pigment in
  linear vertex colour, `baseColor` white); no textures, no per-blade entities,
  one colour draw per visible patch and LOD. Meadow materials cast sun shadows
  using the shared independent shadow LOD and transmit diffuse backlight through
  both sides of the blade. The legacy generator keeps its original shadow flags.
- The `meadow` preset grows an overlapping canopy. Each root carries a central
  blade and four shorter leaves. Set `basalLeaves` (0 to 4) on a species to
  control this lower layer. Each basal leaf costs one triangle and only grows
  on stems without a flower or seed head.
- Blade = 2*segments-1 triangles; flowering stems add 2 (flower) or 4 (crossed
  panicle/spike diamonds): `meadowBladeTriangleBound(segments)`.
- LOD: the shared chain keeps the same seeded blades (1/3, 1/8 of them), widens
  them by their sampling stride and drops segments. Meadow switches at patch
  sizes of 720 and 310 projected pixels to reduce distant subpixel detail.
- Tiers: `meadowGrassQuality(GrassTier::Mobile|Medium|High, meadow)`.
  Mobile: 1024 blades/patch, 2 segments, 48 patches, 1 upload/frame;
  upper bound `grassTriangleBudget(q)` = 344 064 LOD0 triangles.
- Hosts with wind (PHYSIS preview, `GrassPresentationOptions`) receive a sway
  weight `t * flex` in vertex alpha; wildfire shortens charred blades and moves
  burning blades to the emissive primitive (same as legacy grass).

## Not done yet

Far field beyond the grass radius is still terrain shading only (no grass
cards/impostor texture); terrain ground colour should be tuned to the meadow
palette by the project.
