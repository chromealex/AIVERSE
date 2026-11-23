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
