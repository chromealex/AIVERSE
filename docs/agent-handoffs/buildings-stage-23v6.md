# Stage 23V.6 — native generic replica integration and checkpoint validation
Parent: a725017d528ca1304084a6d30aab6da44f837089. Target: feature/aiv20261123_building-grammar, PR #114.

- New BuildingReplicaBridge: existing net::replica Server onMessage slot and existing Command binary codec, strict bounded pre-decode envelope, authoritative Simulation dispatch, explicit unicast of confirmed scene states. Frame carries command epoch, authored content identity, physical aperture progress and existing lift snapshot; client acceptance rejects stale or mismatched content.
- Added common world committed aperture capture, actor retirement preflight across all scenes and refusal to destroy an occupied lift on world shutdown.
- BuildingPhysicalExtent v2 with world-space centreOffset and v1 migration; decoded lift passenger/checkpoint consistency validator before persistent load commit.
- Shared building skill and native example README updated. No genre modules, new network listener or second ECS.
- No native build/test/GPU execution yet. Next: actual native GameModule + Replica loopback acceptance executable and publish branch head.
