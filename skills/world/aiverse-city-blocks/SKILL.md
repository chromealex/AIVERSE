---
name: aiverse-city-blocks
description: Build and repair AIVERSE city blocks, plazas, courtyards, planting beds and street furniture around spline roads; align paving and props to curb heights and prevent overlapping ground and planter faces.
---

# City blocks

Use this workflow for the contents of a block bounded by existing streets. For whole-city layout use
`skills/world/aiverse-city-planner/SKILL.md`; for road geometry use
`skills/world/aiverse-roads/SKILL.md`. Read `docs/roads.md` and `docs/models.md` for their data contracts.

## Ownership and setup

Keep this recipe in the engine. Keep the game's plot polygons, presets, art, generator adapters and
generated maps/models in the game's repository. Do not copy a concrete city into engine templates.
Discover and reuse the project's existing block generator before writing one. This recipe does not
install a polygon generator or imply that an arbitrary project already has one.

Read composed rules, the map, road types, road network, terrain heights and model transforms. Identify
the source generator for a defective mesh; repair it so regeneration preserves the fix. Work on a
copy of game data before replacing installed assets.

## Layout

1. Start from the plot polygon minus road carriageways, sidewalks, driveways, existing structures
   and reserved access. Follow curved road boundaries, not just endpoint chords.
2. Place buildings with reachable entrances, then parking and access paths, then lawns and planting
   beds, trees and furniture. Keep pedestrian routes and driveway aprons clear.
3. Preserve deterministic seeds and stable generated IDs. Replace only objects owned by this
   generator. Update streamed asset hashes when changing meshes or chunks.

## Vertical placement is part of the plan

Do not use a hard-coded paving Y such as 0.012 as the sidewalk elevation.
Resolve the road type and the side facing the plot, including overrides, reversed edges and the
actual boundary position. Use the same support surface for paving, beds and their contents.

In the current road renderer, ground-level sidewalk top is:
`terrainY + roadLift + curbHeightMetres / cellMeters`.
Verify constants in `engine/roads/RoadView.cpp` and defaults in
`engine/roads/RoadNet.h` before implementing an adapter. Currently roadLift is 0.025 map
units, default curbHeight is 0.15 metres, and default cellMeters is 4. Thus a low curb on flat
terrain ends at 0.0625, not 0.012. Low/sloped curbs use the configured height; high curbs multiply
it by 1.35; none/shoulder have no curb rise. The curb's width is not its height.

- Match the paving edge to the sidewalk top. Keep draw-separation offsets separate from physical
  curb height; never add the curb a second time to an already raised surface.
- Account for terrain elevation and parent/entity/chunk transforms exactly once. Chunk mesh Y is
  not interchangeable with an entity model's local Y.
- Sample along the boundary. If adjacent road heights differ, grade or subdivide the surface;
  do not flatten the entire plot to an arbitrary maximum. Bridge/tunnel decks and ramps require
  their actual support heights; the ground-level formula is insufficient.
- Keep vehicle entrances at road level with a deliberate ramp to the raised paving. Preserve
  lowered curbs and do not fill the driveway with a raised slab.
- Place tree roots, benches, building entrances and planter bases on their support surface.
  Place shrubs on the soil top, not on the paving. Update collision/navigation where required;
  changing a visual model alone does not raise the gameplay floor.

## Planting beds without z-fighting

Model a border as a ring with a real interior opening. A soil box with the same width and depth
as the whole bed duplicates the border's outer side faces and flickers.

For outer width W, depth D and wall thickness T, use an interior soil footprint strictly inside
W-2T by D-2T (a small geometric clearance is acceptable), and keep the soil top below the rim.
Reject dimensions that leave no interior. Use non-overlapping corner joins for the border.
Remove or clip paving beneath exposed soil; do not stack two coplanar visible surfaces.
Apply the same rule to tree pits, grates and lawn borders. Do not mask geometry errors with
depth bias, double-sided materials or global near/far-plane changes.

## Verification

- Inspect generated vertex bounds: soil is inside the opening, below the rim; border and soil
  have no coincident exterior faces; shrubs start at soil height.
- Test non-default curb height, zero curb, high curb, a raised terrain plot and a driveway.
  Compare world-space paving heights at the road boundary, including unit conversion.
- Regenerate twice: no duplicate objects or faces, stable seed output, valid streamed hashes.
- Load the affected project and inspect close, grazing-angle and distant views of pavement
  joins, bed sides/corners, tree pits and entrances. Report visual checks not performed.

Workflow recipe over existing engine data/rendering and project-owned generators; no runtime library.
