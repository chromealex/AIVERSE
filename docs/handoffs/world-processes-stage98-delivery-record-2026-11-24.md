# Stage 98 roadmap and feature delivery record

Base c916c1ea; branch feature/world-processes-stage31-bidirectional-handles; World Processes. IN PROGRESS.

Inspection: Surface is immutable after build, planar TerraWeaveEcsAuthority/Earthworks uses X/Z vertex keys. No spherical authoritative terrain mutation header found in TerraWeave branch 2d034dd6 or caverns branch 6cd16e60. TerraWeave owner has exclusive terrain authority; do not mutate its state or Surface corners here.

Scope: shared native planetary transfer request and bounded conservation transaction, real Surface mapping (bottom row contact only, no clearance), explicit volume budget and expected terrain revision; owner callback mutates only staged ECS World. Test seam mapping, distinct bodies, failures, rollback, capacity and no-op. No parallel soil state in production. Keep flat API unchanged. Actual planetary mutation, terrain revision publication, save/hash integration and collider/nav/render updates require TerraWeave owner; explicitly blocked, not complete. No main merge. Native tests and acceptance deferred.

Status: CONSUMER BOUNDARY IMPLEMENTED; native tests NOT RUN; canonical planetary mutation BLOCKED on terrain owner. Code a86a86f7, API correction e210b2e0. See Stage98 handoff.
