# Caverns Stage47 — collision mutation identity

Date: 2026-11-24. Branch: feature/terraweave-caverns-core-20261123. Main unchanged.

## Actual commits

- 41c159b7147eb02a1aa00f5d32ed480f83e3a641: staged SpatialShadow guards.
- 96768910d1e2d8e4a1be4d2b8bf7dc1addc19018: streaming publication quarantine.
- a3ae5435a4b289cf304f94d77722ce9f9253b91f, 7b9f062cfcc49068208a818ed958ab6bb572d859: generic SpatialWorld mutation version, collider generation and conditional copy publication.
- e7190746ec954b5e0dab2cb5010b64cab2964d5b: generation-scoped SpatialShadow leases.
- a25c5728971991a536a0bbc3d46a3a97f009bdf1: quarantine on installation exceptions.
- dfee1eb064c326bd49bd0b9605760a3270a10c10: stale copy rejection and generation validation in CaveSpatialRevisionOwner.
- 9859a867743e5a7f493c6f9f799390d6b9d25747: staged same-revision lease retirement.
- f3e7b5fb39c3089a0bb6f912df0b20cc4cd002f1: skill update.

## Contract and limits

Mutation identities are process-local and not deterministic simulation state. Access requires actual owner exclusion; atomic token allocation is not a lock. Capture current SpatialWorld and version together under exclusion. A candidate based on a changed live world is rejected. Do not use direct assignment/swap to publish a stale copy; existing unrelated assignment paths are not globally prohibited. The optional full-copy owner and Stage35 in-place shadow owner must not both publish the same chunks. The full-copy owner is bounded by its existing 64 slots but copies the entire SpatialWorld; it is not the scalable final streaming backend. Stream callbacks still require genuine native GPU, Box3D and nav commits; no host integration claimed. A native callback returning false must have left no irreversible changes. On partial exceptions quarantine and rebuild before resuming.

## Next stage

Integrate actual stream retirement with existing native owners and generation-safe collision leases; ensure no lost lease at catalog forget, no public ACK bypass, and no stale full-world copies in other publishers. Review exception safety and same-world provenance, then implement ECS authored-SDF persistence/replay, STRATA retirement and bounded GPU paging. No ActorHost or separate gameplay collision world. Update skills and handoff each stage.

## Verification

GitHub source writes acknowledged. Native compilation, CTest, GPU, replay and runtime verification NOT RUN. Mandatory KINGSTON Mac gate remains before merge. Feature work is not complete.
