# Building runtime stage 23V.4 — circulation owner composition primitives

Parent: 69255900c472171f6fad0685c440791274ed8545. Target PR #114, feature/aiv20261123_building-grammar.

- Shared per-scene physical revision supports committed external lift invalidations, using the same router and ACK retry as apertures.
- Platform/landing owners expose real owned collider bounds; verify actual collider ownership during movement. Dynamic nav overlay accepts already validated external blocker bounds.
- Lift authority requires physical landing door owner; managed ECS caller may supply the actual body-volume obstruction probe.
- BuildingLiftRenderState creates PBR CPU Models once, copies only committed immutable presentation frames and lets BuildingRenderer3DScene own uploads, instances, yaw and retirement. No renderer reads live ECS.
- Checkpoint records have equality for change detection. No tests executed.

Next: compose these primitives in BuildingWorldRuntime, with deterministic ID assignment, restore and actor retirement, existing command dispatch and an actual native integration example. No genre module changes.
