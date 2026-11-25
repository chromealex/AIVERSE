# Caverns Stage54g — multi-tile surface group preflight and empty terrain residency

Date: 2026-11-24. Continue in integration/caverns-studio-stage54c-20261124. No main or source feature branch changes.

## Source commits

- 832260076d94a907716e6df19984d03c4785cf4f: shared endpoint order and certified entrance input.
- 253bd11dc09c0e6135ff89a6853d108c8881d0a5: native Q16 quantization.
- 1c62bd46448752a8a4186e968df37a0cffb519f5: complete footprint and shared tile edge preflight.
- 0071d26b626706bff02d8bb5e721f9f5cf3cd5d8 and 5b4c30620342e3c007d8f5ef862b8deedf392f01: coordinator group binding, exact pins and soil epoch, exclusion across incremental publication.
- e164fb6db23bf1237e759ae7ef396c066dec61e2, 117148a0b837b15effaa5c8a56f2778f994200de, 07957760bf96d7b6e31411a9294170ae504e6389, b227ce1e54c35becd45ffe12fc511b6b62cb8b05, b3bff3c8132e170837c468ac6a213ce4ac044a3c: explicit empty tile topology, resident metadata and native absence in SpatialWorld, Box3D and GI.
- 76305a3a4ffdf3c085818dd60ec247862b70b9ba and ae6cf0d5d3bb351e3a673bbb5576d89fe294580f: extract exact Q16 rim cycles and require actual volume rim certification before group binding.
- 5be08bddc436386e0ba960e4ea97e7a5860fbd3b, f9dc26691382e68d18fc1c47e50be6bafabff99c, 6695fd7b982096695b259cbd96163a1b1d1a42f6: downstream readiness and no-op GI acknowledgement.
- 53b0d213f00c060503e97d0374a77a541773855d and 421c1b642774efd5ce84ab2530127ab9fb111f69: skill updates.

## Contract and outstanding work

`planCaveSurfaceGroup` preflights one frozen Earthworks/authority snapshot, at most 64 pinned cells and 1M triangles. Missing footprint, invalid heights, potentially Fill-blocked or overlapping mouths are conservatively rejected. Exact common Q16 tile edge segment sets and closed polygon rim cycles are required; divergent subdivisions/T-junctions reject the plan. `reconcileGroup` requires actual host volume readiness AND rim certification callback, not merely revision equality. No built-in SDF-to-rim remesher has been implemented. A group is not installed into a real game host yet. Caller must maintain reader exclusion across every budgeted surface upload, native volume and Box3D/GI/NavMesh publication, and keep all pins and frozen soil identical until readiness. SurfaceGroupReady suppresses coordinator draw and surface navigation refresh, and blocks WorldScene readiness, but does not prevent direct external physics readers or guarantee rollback after partial native commits. Existing single-tile/Studio paths remain single-tile. Certified empty patches are resident without GPU or physics meshes, and restore/unload keep owner metadata. Planet-local frames, actual cross-tile Studio preview and explicit static terrain assets remain unsupported. Q16 projection currently derives from floating-point clipping and is not deterministic lockstep proof. No native build, CTest, frontend typecheck, GPU validation or seam tests executed. Feature incomplete.

## Next Stage54h

Implement actual constrained native cave-volume remeshing or nonoverlapping transition collar using the extracted exact rim, then integrate the group planner into existing sparse preview. Add real staged all-owner transaction and recovery/rollback, including Box3D, GI and navigation. Preserve shared libraries. Then address planet-local geometry and arbitrary terrain owner support. Mandatory KINGSTON native gate before merge to main.
