# Stage 99 roadmap and delivery record

Base a6fb7201; World Processes branch feature/world-processes-stage31-bidirectional-handles. IN PROGRESS.

Inspection: GamePlanetFlora is static presentation; canonical TerraWeave ForestEcologyEcs/AnchoredPlant/ForestEcologyBirth use X/Z Earthworks, seed offsets and canopy. Current TerraWeave and caverns branches do not expose a planetary rooted lifecycle. TerraWeave owns it. Do not run the flat forest on face coordinates or introduce duplicate plant histories.

Scope: bounded synchronous native ecology sample API for actual rooted 3D positions, stable body IDs, optional explicit PHYSIS weather cell mapping with same-body/locality verification and explicit soil-wetness policy, canonical EVOLVE projection; one SurfaceRegionIndex per batch; reject bad samples atomically. Test seams, bodies, live region changes, PHYSIS mapping, malformed inputs and rollback readback. Exercise the existing authored planet demo's actual EVOLVE region. No new forest or weather owner, schema or scheduler. Growth, recruitment, mortality and render publication remain blocked on TerraWeave planetary owner. Tests deferred; no main merge.
