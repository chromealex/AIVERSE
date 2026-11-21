# Native editor LOD/material comparison,2026-11-21

Source5d35629dd (detail implementation768fdb19e), branch feature/aiv87_terrain_pbr.
Review-only project KINGSTON/terraweave-unified/review-project-v1 contains copies
of original top-level JSON and map, links original assets read-only and new
terrain-detail-full-v1 / flora-full-v8 packs. Original AlpineValley dirty game.json
preserved; original map/rules/assets unchanged.1391entities in both projects.

Camera x256,z128,pitch25,yaw135; native Scene903x605 at distances65/140/300.
Same binary generation except baseline running editor predates graph budget change;
therefore these are end-to-end content comparisons, not an isolated renderer A/B.

| Distance | Original camera triangles | New camera triangles | New shadow triangles |
|---|---:|---:|---:|
|65m|10,949,072|2,899,639|2,378,576|
|140m|16,790,047|2,766,159|3,387,462|
|300m|10,232,616|1,888,490|2,135,828|

Actual node-level LOD counters new near186/382/109, mid84/529/527,
far41/39/1303;662/1114/1342chains,0blocked. Baseline diagnostic counts were
disabled in its process; triangle counters remain actual submitted geometry.
Do not call these GPU timings or FPS gains. Terrain still48full-detail tiles.

New terrain pack:48PBRweight/macro tiles plus shared1K/4m graph atlases. Flora:
12native conifer prototypes and154native grass patches from original ground
snapshot, existing cuts/water exclusion retained. Material overrides passed via
existing rules view.materials; StaticModel paths updated consistently in copy.

Actually run: native aiverse_editor+aiverse_studio builds PASS
terrain-detail-editors-build.log; native vegetation export PASS flora-full-v8.log;
48tile offline detail bake+atlas package PASS terrain-detail-full-bake.log;
editor map_info dirty:false; screenshot/geometry_stats on both ports47325/47326.
Scene-only images and source metrics at KINGSTON/terraweave-unified/review-comparison-v1.
Near frame inspected: canopy detail and grass still below supplied art references,
far trees show excessively thin bare leaders, dense haze and background remain
poor. Water appearance remains unfinished. This is genuine LOD evidence, NOT
final beauty/gallery acceptance or ten-species completion. No main/remote push.
