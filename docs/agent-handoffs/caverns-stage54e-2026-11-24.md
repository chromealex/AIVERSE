# Caverns Stage54e — shared native heightfield entrance cut

Date: 2026-11-24. Continue in integration/caverns-studio-stage54c-20261124. Main and original feature branches unchanged.

## Source commits

- 78d9147fce2b4104791f2a14ecf4318f3f3c83b4: shared polygon subtraction kernel reused by existing authoritative TerrainPatch clipping and the Studio heightfield adapter; vertical footprint preflight helper.
- b7058c5318f93d7422bf097d029425f252b82c92: actual Studio heightfield triangle subtraction, bounded cell associations and output, rejected entrance diagnostics.
- 3b88a67e324a71b814d16048e6d56d884bacc8a2: shared clipped CPU terrain in PBR lighting preview, including intentionally empty terrain.
- 699918b4a547ee45c2f8e7b0401027604bb1fa92 and 31b2fbb7186444ede8d2ba3a0f18bbbf35f247fb: retained CPU source, cut-sensitive lighting rebuild, native buffer retirement and diagnostics.
- af92e2d65ac1d2ca64bdb9f71b948e6c3e758974: conservative Fill overlap rejection.
- 755c87e04ba34a2e6ab75afacafbdc774448fa65 and c6b5a1895c6d276d44ed289b3bd14f9e6989c64f: typed preview cut diagnostics.
- c8de96faabda15211cd845b5d04974f431bd8453 and 66f4cab7c07504ad8d815b736fd31ec0757e4aa4: skill updates.

## Contract

Use the existing legacy flat heightfield triangle geometry; a valid authored shaft subtracts an inscribed 16-sided polygon from original triangles with their height planes and winding intact. The Studio adapter bins candidates into its existing bounded grid, rejects a whole shaft if outside map extent or any intersected plane is outside the shaft vertical SDF extent. Fill AABB overlap at the mouth conservatively rejects openings; false-positive rejection is possible. Planning is capped at 262144 cell associations and fill checks; output is capped at 600000 triangles. Expected budget failure yields original uncut terrain plus diagnostic, not a partially cut mesh. Both direct terrain and PBR/GI receiver consume the same MeshBuilder on cut-sensitive revisions. Undo/Redo reconstructs from MapDoc; no authored terrain heights are changed. Empty valid cuts cannot be sealed by fallback planes. Old terrain buffers retire through the shared native presentation queue with explicit pressure/teardown idle fallback.

The existing SDF mesh is the only shaft wall source. Exact welding between the polygon rim and sampled SDF geometry has not been implemented or validated; no watertight seam claim. `terrainPresentation=entities` and sparse TerraWeave terrain remain unchanged; this is not a physics, NavMesh or runtime collision publication. GI receiver terrain geometry matches but cave volume GI contribution and isolation lighting parity are not certified. Preview diagnostics are actual CPU cut counts, not proof of native visibility or gameplay readiness.

## Next Stage54f

Integrate surface cuts into real entity/sparse TerraWeave terrain owner (including default entity-based maps), seam certification/welding, and coordinated physics/NavMesh publication using the same triangles. Preserve existing renderer and ECS terrain systems; no duplicate renderer or genre module. Review edit lifecycle and resource pressure. Then close gameplay/replay gaps and run mandatory KINGSTON native acceptance. Native compilation, TS checks, CTest, GPU or visual tests NOT RUN. Feature incomplete.
