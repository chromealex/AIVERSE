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
