---
name: aiverse-planets
description: Make AIVERSE planet and orbit battle maps - world bodies, planet generation presets, shared or separate layouts, orbit fleets of bots, checksum safety for flat maps.
---

# Planets and orbits

Contract: `docs/planets.md`. A map without `world` / `generate` behaves exactly as before.

1. Bodies: the map's `world.bodies` (star, planet, moon: id, kind, radius, orbit, visual) via `map_set`; `planet_info` reports what the engine sees.
2. Generate: `gen_presets` -> `map_generate {preset: rocky|ocean|moon, players, params}`; `params.layout` `shared` (all players on one body, polar rotational symmetry) or `separate` (an identical planet each); moons get `params.moonResources`. Read the answer: starts, path lengths, connectivity.
3. Heights come from the generator (TerraWeave fields on the sphere: relief and climate biomes;
   `params.heights: "fbm"` only for old maps), not from hand-made formulas: a height function built from bands
   (`abs(z)`, latitude rings) gives straight-edged lowlands that the sea fills as squares on the cube faces.
   Bake a static map with `aiverse_headless --project <dir> --generate rocky --seed N --players 2 --gen-out <map>`
   (the project's first map supplies the bodies; `~/.aiverse/packages/templates/planets/maps/big_planet.json` is made this way).
   The ocean is cut per pixel where the ground is above `seaLevel`; coasts follow the heights at any LOD.
4. TerraWeave on spheres (`skills/workflow/aiverse-terra-first/SKILL.md` applies to planets too):
   relief and land biomes come from `engine/world/TerraWeaveSphere.h` (`heightAtSphere`, `climateAtSphere`,
   `placeOnSphere`); never write a separate planet noise or tree placer. Trees and rocks are TerraWeave
   Arbor / rock prototypes placed per cell by the planet view (`engine/view/game/GamePlanetFlora.*`): body
   `visual.flora` `{"density": 0..1 (0.22 planets, 0 moons), "scale": world units per metre (0.06)}` or `false`.
   Check them on a close screenshot (`AIVERSE_PLANET_CAM=40,20`): trees on land, none in the sea or on steep rock.
5. Water: oceans of planets belong to NEREID (`skills/gameplay/aiverse-nereid-water/SKILL.md`). The planet sea
   uses NEREID's wave model (`engine/render/NereidWaveShaders.h`, the same functions as plane water) sampled
   triplanar on the sphere, with colour by depth, shallow water and foam along the coast, cut per pixel where
   the ground is above `seaLevel`. Change waves or foam in `NereidWaveShaders.h`, not in the planet shader.
   Set the sea with `seaLevel` / the generator's `water`; check the coast on a close screenshot. For gameplay in
   the sea (ships, swimmers, floating wrecks) add `"nereid": true` or `{"depth", "current", "axis"}` to the body:
   the sea becomes a NEREID sphere water body (`NereidSphereWater.h`, docs/planets.md §1). Give the floating
   entities `WaterProbeComponent` + `WaterSphereContactComponent` (+ buoyancy components) from a native module;
   depth, up and buoyancy are radial, the current flows east about the axis, `waves` adds a swell. Naval units get
   probes and splash / wake events automatically. Never write planet-only water code.
6. Cube-sphere terrain: relief and materials with the heightfield tools; check seams between faces on screenshots (`view_focus` on edges).
7. Bots: `rules.ai.invasionSize` sets the orbit fleet; bots build orbital yards when the enemy is reachable only through orbit. `match_run` must show transfers between bodies and a winner (`skills/ai/aiverse-bots-debug` otherwise).
8. Determinism: same seed -> same checksum; sample-game checksums (`tests/checksums/check.sh`) unchanged when engine code is touched.

Runtime library: none. Workflow over the engine MCP map and planet tools.
