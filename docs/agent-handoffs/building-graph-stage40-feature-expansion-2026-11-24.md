# Stage 40 — feature expansion (tests deferred)

Branch: integration/building-graph-stage36b-20261124. User requested completion of generator features before another test cycle. Previous verified baseline was Stage 38 selected acceptance; Stage 39 and Stage 40 changes have NOT been built or tested on the owner Mac.

Implementation chain:
- cfc432085df0410b78d8bf6b97303ba171203d12: setting-specific functional façade relief contained in each graph-owned structural solid; original room, aperture and conservative collision geometry remains authoritative.
- 94dff2fd1d93399d453214ea148d8548bdc464f7: selectable 128/256/512 procedural texture sampling, fine grain and normalized higher-resolution normals; GLB and native Model cache keys include resolution; corrected cylindrical UVs; corner beacons mount within the chamfered foundation rather than on its missing corners.
- e80404fa13c5af14f8f407e755898c7688c1f565: Studio quality selector and recipe/regeneration round trip, six editable functional starting layouts.
- 46e4907a95e2a9c315a3eb4309ac8b2f815af3b3: independent Building render worker, asynchronous camera replies, coalesced orbit, pan and wheel input; no Scene replacement. Offscreen frame transport is not a continuously presented native child surface or guaranteed 60 FPS.
- 96168ca75efa84462f6a4f4a5503cb86ef890402: graph-owned gable, shed, barrel, hip, dome, terrace, pagoda roofs within total authored height; preserved room core and explicit flat-only sliding aperture contract. Curved roof collision is conservative per-cell approximation.
- 5d532a55311a99fc8aebf44a4975e2b28b58b86a: Studio roof controls and setting recommendations, explicit recipe fields saved/reopened.
- e0b9e3709d2c7d5c6393baf0d8b744f08ede645f: role-specific facade rhythm, glazing fraction, roof proportions, supported cornices.
- 3a93d80a918aa55b282b477d6c9e584b7002b8cf and adc696bcf298ee1cead7918c50e1febb1053c8f0: eight authored silhouette roles for each of twelve settings (96 entries; original Sci-Fi separate). Functional layout presets are explicit recipe edits, not automatic genre assignment.
- d5d56fe0aa7c6e3285d222297fcdae62ef13b7c6: Windows offscreen Vulkan source path, mixed-case material keys, test registration and runner expansion, skill notes.

Limits: no proof yet of visual elimination of every detached part or art quality, measured preview FPS, Windows build/GPU, full integration after new roof fields, or live runtime MCP. Textures are three embedded PBR maps, not standalone reusable texture assets/unique atlas. Closed silhouette roles are not navigable buildings. Functional façade relief stays within the existing wall envelope; arbitrary footprints and interior furniture are not implied. New code and regression tests need compile/CTest and manual Studio review. No main merge.

Next when test cycle approved:
```sh
cd /Volumes/KINGSTON/AIVERSE-builds/AIVERSE-building-stage36c
git pull --ff-only
git rev-parse HEAD
bash tools/studio/verify_building_integration_mac.sh "$PWD/all-tests-v2"
```
Reuse external KINGSTON cache; inspect multiple styles, roles, functional roof caps, actual door/roof clearance, attachment geometry, PBR quality and viewport responsiveness. Then full merged-tree gate and live MCP acceptance. Do not mark unrun checks PASS.
