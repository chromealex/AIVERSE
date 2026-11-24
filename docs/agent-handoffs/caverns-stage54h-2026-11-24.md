# Caverns Stage54h — constrained rim transition preparation and sparse preview group

Date: 2026-11-24. Continue in integration/caverns-studio-stage54c-20261124. Main and original feature branches unchanged.

## Source commits

- 9df70ed7edad402ed8139a71e4560785921ef2ea: constrained angular zipper collar from actual retained SDF plane edges and exact terrain rim.
- 5082de278b856233c016bc70f44923bb96b51bdf: trim exclusively owned isolated source at Q16 horizontal plane; reject coplanar caps and invalid fragments.
- 3b69e41e7e92efa323bf4201a94ea6c90d4296ba: prepare combined retained+collar Model and CaveCollisionPrepared from one fixed triangle soup.
- 87080e956c643fdb7ce9d50f575ace6efde5d49d: reuse Stage54g group planner in sparse Studio preview, no partially uploaded certified group draw, solid fallback and empty tile handling.
- 922c0d2485699edd20a885b7921afc0c8148f1ed, c3aea18c62951f7acea07d245e2f4b72187f439f, c6e64ad47f7c0e2b6c89827baaf776ab7c8c937c: surface plan diagnostics in Studio.
- 4cff4aee6d13e28c3ab19d475c6008fe7bab41e5: verify actual prepared CPU combined triangle incidences against terrain group; exact local/body origin float conversion check.
- 152058174ffe88fe3f0772f75dd9cb8dd4294cb5 and 2838d1ff02e104327d46ee3786d8ddbe0e720f9a: skill updates.

## Contract

The transition is intentionally constrained to a single isolated shaft section whose native owner supplies exclusive triangle ownership and replacement proof. Retain the actual sampled geometry below the specified plane; trim upper geometry, extract its directed open boundary, reject cap triangles, multiple loops, inconsistent winding, nonmanifold/degenerate or non-star-shaped rings. Connect exact native Q16 vertices with bounded angular zipper triangulation. Validate open terrain edges and two incidences at the joined volume edges of the combined CPU payload. No proof of global self-intersection absence, general arbitrary topology, deterministic cross-platform clipping or full watertightness. Render Model and collision payload have the same triangle source; Model positions and Box3D body origin must round-trip on Q16 individually. Caller must use a matching world transform and preserve adjoining chunk boundaries. Never draw the original untrimmed shaft and the replacement together. No automatic owner installation was done.

Sparse preview uses Stage54g whole-footprint plan for current bounded visible tile set; rejected group preflight falls back to solid terrain. An accepted group draws only after its selected models/empty entries are all uploaded. Document or pin-set changes discard the old preview group; temporary blank frames are possible, not atomic old-to-new presentation. Flat uncut tiles remain one quad; planar edge subdivision can differ from the certified CPU source. Native volume rim replacement is NOT installed in Studio, so seam diagnostics refer to terrain CPU plan only. Actual SDF seam can still have gaps. No arbitrary terrain entity, sparse GI, planet-local frame or gameplay ownership integration.

## Next Stage54i

Add actual native volume source ownership partition/selection and integrate replacement payload into existing shard/GeometryScene and Box3D/SpatialWorld owner transaction. Prove adjoining source seams and suppress original source before activating replacement, with rollback and readiness for surface group, nav and GI. Then connect same owner to Studio, improve preview retention and support planet-local frames. Native compiler, CTest, TS build and GPU/runtime tests NOT RUN. Mandatory KINGSTON acceptance before main; feature remains incomplete.
