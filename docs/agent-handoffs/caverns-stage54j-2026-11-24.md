# Caverns Stage54j — constrained multi-volume-slot shaft replacement

Date: 2026-11-24. Continue on integration/caverns-studio-stage54c-20261124. Main and original feature branches unchanged.

## Source commits

- 0163a7f2 / 350db69b: shared exact Q16 payload conversion, including empty slots.
- a1b9113e / f4c81845: complete rectangular owner group, preserve original retained triangles in their slots, clip new collar into existing slots, exact internal/exterior face and rim edge checks, exact additive Q16 span requirement.
- 9f72f584: existing GameplayEditHost accepts multiple slots per entrance and stages the exact collision payloads and replacement meshes through existing native owners.
- e2a876f4 / 8dbbfdc9: conservative automatic signed-world slot selection, explicit owner-chosen trim planes.
- 76cf45f4 / aca409cc / 856fc6bf: expand same immutable pending edit with new and prior rim slots, pin via runtime frame, refuse external native owners.
- 9e7fca4e / eba318ea: native publication pump with recovery signal; stages existing volume and terrain owners and refreshes surface navigation under real reader exclusion. Caller proves remaining owner readiness.
- f7dd9384 / c245d63e: skill updates.

## Contract

Automatic selection includes padded shaft envelope, not an arbitrary camera frustum. Up to 64 slots, complete rectangular per-entrance group with no overlap between groups, common lattice and fixed span representable exactly in Q16. Source total capped at 32767 triangles per group; partition fragments bounded to 65534. Preserve retained source geometry within original slots; above-plane triangles must be isolated inside the supported shaft envelope and group exterior. Partition only the generated collar; coplanar boundary faces belong to the positive slot. Reject divergent exact face segments, invalid fragments and changed rim edges rather than publish a speculative weld. No general connected/multi-mouth topology or automatic trim plane selection. The current strict edge check can reject valid cases requiring further terrain/volume subdivision. Native authoritative collision still needs deterministic clipping/replay proof.

Host explicitly configures frame.configureRimSurfaceGroupAuto with one frozen surface plan and trim planes. Keep real exclusion, same source/soil, visual revision, device and global ID allocator across advanceRimWorldPublication calls. This helper uses existing frameOutsidePass, publishWithExclusiveWorld, TerrainMutationCoordinator and refreshNavMesh; it does not install a concrete GameModule or create a scheduler. The caller supplies actual surface Box3D, GI and exact cave navigation readiness proof. A failure after irreversible changes quarantines; no universal rollback claim. Existing cave navigation source remains authored SDF. Studio still previews surface group separately: do not claim installed volume collar.

## Next

Stage54k: integrate exact replacement into Studio's existing visual host and a concrete native game owner, implement shared rim edge subdivision where partitioning changes contour segmentation, and inspect native recovery path. Then support planet-local frames and PHYSIS local gravity with no genre-specific module. Native compilation, CTest, TypeScript build, GPU/physics/runtime acceptance NOT RUN. KINGSTON native gate before main; feature incomplete.
