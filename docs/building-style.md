# Building styles

Generated buildings (`building-gen`, `libraries/include/aiverselib/systems/BuildingGen.h`) are collision:
axis-aligned boxes with floors, a door, windows and stairs. A **building style** gives them the look of a
setting. The style is a data file, `engine/assets/building-styles/<id>.json`; the code that reads it
(`building-style`, `libraries/include/aiverselib/systems/BuildingStyle.h`) knows only generic elements, so a
new setting is a new file. Shipped styles: `scifi` and `modern`.

A style dresses two kinds of buildings:

- **Structures** (`structures` block, `BuildingStructure.h`, `dressStructure`): one armoured hull per building, as in
  RTS bases - the main look of `scifi` (kinds `barracks`, `command`, `factory`, `depot`).
- **Enterable buildings** (building-gen floors, `dressBuilding`): walls, floors, stairs and windows dressed by the
  style's facade elements - city blocks, raid sites (`modern`; `scifi` has a military-base facade set too).

## How it fits together

```
BuildingDef ──applyBuildingStyle(style)──▶ BuildingDef with the style's proportions (if they fit)
     │
     ├─ generateBuilding(def, seed, origin, yaw) ─▶ collision boxes, door, stairs, openings
     └─ dressBuilding(def, style, seed, origin, yaw) ─▶ mesh: vertices + one index list per material
```

The mesh is built from the same layout as the collision: the outer walls, slabs, stairs and roof are the
collision boxes themselves (materials `shell`, `interior`, `roof`), and every decoration is placed around the
openings building-gen reports (`BuildingLayout::openings`: each window and the door with its floor and side).
The door corridor stays free and window holes hold only glass, so what the player sees is what they can walk
through and shoot through. The mesh is presentation (float) but deterministic per definition + style + seed.

## Style file

Metres, linear colours. Required: `id`, `materials`, `shell`, `interior`, `roof`. Every other element is
optional; leaving it out removes it.

| Key | What |
|---|---|
| `materials` | `{id: {color, metallic, roughness, emissive, emissiveStrength}}`; elements name materials by id |
| `palettes` | seeded colour sets `{material: [r,g,b]}`; for an emissive material the colour replaces the emission |
| `proportions` | `window` [width, height, sill, spacing], `door` [width, height], `floorHeight`, `floors` [min, max] - applied by `applyBuildingStyle` when they fit |
| `panels` | cladding grid on every floor, cut around openings: `material`, `accent` + `accentChance`, `size` [w, h] (h 0 = whole floor), `gap`, `depth` [min, max] |
| `base` | plinth round the ground floor (stops at the door) |
| `band` | ledge at every floor level, optional `light` strip (`material`, `height`, `chance`) |
| `corners` | vertical corner pillars; `chamfer` > 0 makes heavy blocks with the outer edge cut at 45° (they stop short of the door) |
| `skirt` | sloped armour plates round the ground floor (`height` capped below the lowest pane, `out`, `plate`, `gap`), mitred corners, cut at the door |
| `buttresses` | wedges between window columns (`width`, `out`, `height` as a fraction of the walls, `chance`) |
| `pipes` | down pipes between window columns with brackets, bent back over the parapet (`radius`, `chance`) |
| `hazard` | diagonal stripes (`material`, `back`, `stripe`) on the door jambs and canopy |
| `window` | `frame`, `glass`, `lit` glass with `litChance`, frame `width`/`depth`, sill light `strip`, `hood` (sloped lid) |
| `door` | jambs and lintel, `light` above it, `canopy` |
| `parapet` | roof edge wall with optional `light` |
| `crown` | slanted visor round the top with mitred corners and optional `light` |
| `ribs` | vertical fins between window columns (`chance` per gap), optional `light`; never across the door |
| `sign` | vertical light sign on a corner (`chance`, `width`, `depth`, `height` as a fraction of the building) |
| `roofBlocks` | machine rooms / penthouses: `count`, `width`/`depth` as fractions of the footprint, `height`, clad with `panels` |
| `props` | roof props: `kind` `box`, `mast`, `tank`, `stack` (exhaust) or `dish` (radar), `count`, `size` [x, y, z] (numbers or [min, max]; round props: x = diameter, y = height), `tip` light |

Errors name the path: `style.panels.material: unknown material 'x'`.

## Structures

`dressStructure(style, kind, seed, origin, yaw)` → mesh + collision boxes (hull, modules, tower) + gate points
(ground in front of each gate). Front = -Z. Built from cut-corner blocks whose walls lean in, armour plates per
face and belt (team-coloured by chance), a raised roof deck, a foundation pad with beacons, side modules, a
tower with a lit slit, gates (frame, slats, light bar, hazard stripes, ramp), landing legs, rear thrusters,
vent grilles and roof props.

`structure`: materials `hull`, `dark` (required), `plate`, `team`, `pad`, `glow`, `beacon`, `exhaust`, `window`.
`structures.<kind>`: `size` [w, d], `height` [min, max], `taper` (lean as a fraction of the height), `chamfer`,
`roofRise`, `bands` (armour belts), `teamChance`, `modules` {`count`, `width`, `depth`, `height`},
`tower` {`chance`, `width`, `depth`, `height`}, `door` {`width`, `height` (fractions), `count`}, `legs`,
`thrusters`, `pad`, `props`.

## Tools

```sh
aiverse_building_export --style engine/assets/building-styles/scifi.json --out barracks.glb --structure barracks --seed 3 --ground
aiverse_building_export --style engine/assets/building-styles/modern.json --out street.glb --street 10 --seed 3 --ground
aiverse_building_export --style engine/assets/building-styles/scifi.json --out house.glb --building def.json --seed 7
aiverse_modelview street.glb
```

Tests: `aiverse_library_building_style_tests <repo>` (CTest `library_building_style`).

## Budget

Structures: barracks ≈ 2.3k, command centre ≈ 4.4k, factory ≈ 1.7k triangles. A two-storey 22 × 16 m
enterable `scifi` building is about 5–8k triangles in up to 15 materials (one draw per material);
a five-storey 18 × 15 m `modern` building about 13k.
Several buildings that share a style share its materials, so a street batches well.

## Not yet

- Kit models (GLB pieces) as style elements, textures and trim sheets (surfaces are flat colours now).
- Structures are not enterable; their collision is a few boxes.
- Non-rectangular footprints and setbacks (building-gen is a rectangle with quarter-turn yaw).
- A map block that builds the Model at load: the actor map `buildings` block still draws collision boxes.
- LOD and merged far meshes; interior furniture.


## Parametric structures and silhouette grammar (stage 2)

The editor-facing contract is **Setting → Style → Function → Width/Height/Depth → Seed → Generate**.
The function is a `structures.<kind>` entry in the chosen style. The shape grammar is independent of
that kind's gameplay behavior: `massing` selects `bastion` (fortified), `spire` (tower),
`twin-bay` (parallel industrial roof bays) or `terraced` (stepped levels). Style JSON sets these
values for barracks, command, factory and depot respectively; custom styles can use any grammar.

```cpp
StructureSize dims{24.f, 12.f, 18.f}; // width, height, depth in metres
StyledStructure model = dressStructure(style, "factory", seed, origin, yaw, dims);
```

```sh
aiverse_building_export --style engine/assets/building-styles/scifi.json \\
  --structure factory --size 24 12 18 --seed 482733 --out factory.glb
```

The existing five-argument `dressStructure` API is unchanged. Omit `--size` (or use
`StructureSize{}`) to keep the original style-defined dimensions. If sizing explicitly,
**all three dimensions are required**. The complete rendered envelope is fitted to the
requested extent, including external modules and antennae; after 90° or 270° yaw, world X
and Z extents swap. The same mapping applies to collision boxes and gate anchors. Explicit
requests require width/depth in 8–160 m, height in 5–80 m, and enough width for the gates.
The generator recomposes architecture from the requested proportions *before* the final
bounds fit — it does not rescale a cached GLB. Fine structural members may still be
non-uniformly scaled by the final fit; physical-size preservation is future grammar work.

This stage adds the CPU generation and GLB export contract only. The **HTML5 Studio panel,
asset persistence, runtime render model bridge, trim textures, LOD and preview of variants**
are separate unimplemented steps. Core collision is still coarse closed AABB boxes for
non-enterable sci-fi structures; doors are visual gates, not real accessible openings.


## Authored composition modules (stage 3)

`structures.<function>.forms` is a bounded list of large architectural pieces, built
**before** small props. Coordinates are fractions of the structure's unrotated
footprint and body height; `at` gives the centre in X/Z and `size` the
relative width/height/depth. A form's `level` is its base elevation
(relative to body height), and `material` names an existing style PBR
material. Each form can be repeated with `repeat` and `step` offsets,
or enabled with seeded `chance`.

```json
{
  "kind": "silo",
  "material": "plate",
  "at": [1.02, 0.82],
  "size": [0.30, 0.78, 0.32],
  "level": 0.38,
  "chance": 1
}
```

Supported kinds:

| kind | generated mesh | collision |
|---|---|---|
| `pod` | cut-corner sloped armour wing | coarse enclosing box |
| `silo` | smooth cylindrical reactor/storage vessel with collar | enclosing box |
| `pylon` | tall tapered prism and optional beacon | enclosing box |
| `gantry` | two posts and a raised beam, with open centre | three separate boxes |

Per structure: at most 24 entries; each may have 1–8 copies. `at` is
within [-0.5, 1.5] in each direction, `size` fractions are positive
and bounded, the final repeated centre must remain in [-0.75, 1.75], and
`chance` lies within [0,1]. Invalid fields produce path-specific
`JsonError` rather than silently generating unbounded geometry.

The examples in `scifi.json` use these operations to distinguish:
barracks = armoured side pod + paired pylons; command = pylons + elevated
gantry; factory = coolant silo + service gantry + rear machinery pod;
depot = off-centre storage pod + tapered pylon.

Authored masses reserve rooftop space before the seeded placement of smaller
props; no fixed gameplay behavior is tied to any form. The final explicit
`StructureSize` still bounds the visible mesh, including these additions.

### Limitations
- Generated sci-fi hulls remain **non-enterable**; the gantry itself has an
  open center, but the original armoured hull behind it remains closed.
- Colliders on sloped/cylindrical forms are conservative AABBs, not exact mesh
  collision; navigation, interior shells and functional sockets are future work.
- Geometry uses material factors and metres-based UVs, not trim-sheet
  textures or authored GLB kit pieces yet.
- Studio's generator window, Assets GUID/meta creation and GPU verification are
  **not implemented in this stage**. 


## Curved structure grammar (stage 4, November 2026)

In addition to `pod`, `silo`, `pylon`, `gantry`, the generic
`structures.<function>.forms` array now supports three architectural curves:

- `arch`: an open, elliptical curved portal (12 finite wedge segments) with
  separate support and arc collision pieces. A visible opening is not filled by
  one large collider. Use when the portal itself is freestanding; the main
  structure hull remains a separate closed collider.
- `dome`: an ellipsoidal hemispherical roof, with bounded 16×7 tessellation,
  a closed bottom, and a conservative collision box for nonwalkable roof mass.
- `swept-hull`: a longitudinally curved, elliptical mechanical body with
  shaped station rings (12×8), closed ends and conservative collision box.

All new forms share the same authored `at`, `size`, `level`, `material`,
`chance`, `repeat` and `step` fields. They work in the existing sci-fi style
and in future setting files without a function-specific C++ generator.
Examples in `scifi.json` now include a freestanding barracks arch, command
roof dome, factory swept service hull and a compact depot dome.

### Minimum front-gate clearance

When `StructureSize{width,height,depth}` is **explicit**, the generator checks
its *resulting geometric* gate width and height after final envelope fitting:
width must remain at least **1.6 m**, height at least **2.1 m**.
If a requested envelope would crush a gate below these limits, an empty
`StyledStructure` is returned (the editor should surface a clear input error).
The unsized legacy generation API remains untouched. The check protects gate
**visuals**, not real indoor navigation: sci-fi main hulls are still closed
collision volumes. Existing small armour trims and prop dimensions still use
the envelope fit, so complete invariant detail thickness is not yet guaranteed.

### Focused CI

`.github/workflows/building-style-focused.yml` compiles the two building test
executables and GLB exporter directly with C++20 and `-Werror`, independently
of the unrelated library-catalog gate. It generates four 28×15×24 m buildings,
checks the GLB container and exact accessor bounds. Results must be read from
GitHub Actions, not assumed from the existence of this workflow.


## Constructive fit without deforming small details (stage 5)

Explicit `StructureSize{width,height,depth}` is now solved by **regenerating
the actual building** at bounded candidate body dimensions. The fitter compares
the full rendered bounds (including roof, supports, domes and arches) to the
requested final envelope and adjusts the underlying construction dimensions,
rather than applying a nonuniform vertex transform. Once converged, it only
applies a rigid translation so the model's minimum visible corner matches the
requested origin. Normal vectors, UVs, wall trims, supports and roof props are
never rescaled in this final placement operation.

A finished `StyledStructure` reports `constructionSize`: the W/H/D of its
raw hull before any rigid placement. `dressStructureGeometry` is a low-level
geometry producer used by the fitter and regression tests; ordinary callers
should use the unchanged `dressStructure` API. In particular, the
original no-explicit-size path still runs exactly one generation and preserves
the original authored look.

The solver caps iteration attempts (32) and rejects an unreachable exact
envelope rather than distorting meshes. Nominal gate dimensions are fixed
against the style's authored structure dimensions rather than being derived
from the current requested width; gate width must be at least 1.6 m and gate
height at least 2.1 m when explicitly sized. Impossible requests currently
return an empty structure (next: richer editor-facing error codes).

**Boundary of this guarantee:** proportional architectural *massings* and forms
still depend on candidate body dimensions; a taller requested building may
intentionally result in larger tower modules. The claim is **no final mesh
scaling**, not invariance of every generatively defined proportional feature.
The sci-fi base hull is still not enterable.

Focused verification: the \`Building Style Focused\` workflow compiles and runs
the library tests, and checks the exported GLBs. A regression compares every
vertex normal and UV of a sized structure to a fresh raw-generation instance,
permitting positions to differ only by one translation. Only claim passing
results when the workflow completes successfully on the current commit.

## HTML5 Studio authoring (stage 6)

Dock panel **Building Generator** is registered in the native HTML5 Studio.
Flow: **Setting → Style → Function → Width/Height/Depth → Seed → Generate GLB**.

- `studio.buildingCatalog` loads real style files from `engine/assets/building-styles`.
- `studio.buildingInspect` evaluates three successive seeds with the CPU generator,
  reporting polygon/material counts and exact bounds. These are NOT GPU images.
- `studio.buildingGenerate` safely creates a NEW GLB in
  `project/content/GeneratedBuildings/`; the existing StudioAssetCatalog
  creates/reconciles persistent GUID + .meta, and Assets refreshes.
- The native C++ implementation is `apps/studio-native/StudioBuildingGeneration.h`;
  the typed bridge and panel live in `apps/studio-web/src/types.ts` and
  `apps/studio-web/src/panels/generated/BuildingGeneratorPanel.tsx`.
- Browser-only preview cannot generate files. A native GPU thumbnail appears only
  if the existing thumbnail service provides a real image.

No live 3D viewport preview yet. Current constructive size fitting can reject
feasible sizes; new Studio acceptance tests must pass before this is called
production-ready.


See docs/building-functional-recipes.md for the new independent functionality
recipe path. Legacy structure kinds are silhouette presets, not game roles.
