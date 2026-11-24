# Stage 132 — live gameplay fields and graph source presentation

Branch: feature/spectrafx-html5-data-interfaces-20261123
Plan: 3c0f47acc67bc2b57a547d502f6ea6e254931f86
Implementation: bfab63ceebde468b0a3582825636146521c7e6a5

SpectraLiveFields is a reusable committed owner-thread capture coordinator. It publishes a shared immutable registry from PHYSIS and canonical NEREID plane/sphere queries, keyed by explicit source epoch, committed tick, geometry, sample region and wave time. Same key reuses capture, invalid region clears it, previous leased snapshots remain independent. Host must change epoch for replacement or same-tick restore. No retained World pointer, authority mutation or duplicate simulation.

ClientFx now consumes existing SpectraSourceComponent declarations for visible non-blip prepared GameScene objects on flat maps. It loads existing graph-v2 assets through the established graph compiler, bounded 256 cached graph names per match with a separate runtime namespace, and routes them into the existing CPU ParticleSystem/presenter. Per presentation frame at most 4096 source rows; PHYSIS/NEREID lattice uses 405 nodes around quantized camera target, recaptures by committed tick or region. Existing update, light and draw path used; no additional particle tick. Stop/start and backward tick reset registry and presenter state. Same-tick restore requires explicit host reset; not automatically detected. Unsupported/missing graph definitions do not spawn graph effects. No new schema, save format, shader ABI or MCP operation.

Files: engine/world/SpectraLiveFields.h, apps/client/ClientFx.h/.cpp, tests/vfx_graph_runtime_test.cpp and existing subsystem skill/roadmap/area notes. Regression source covers same-tick cache reuse, replacement, canonical water depth, missing PHYSIS and invalidation. Native sphere publication supported by API, but actual planetary ClientFx graph sources explicitly gated because existing particle renderer uses body-local coordinates; no implicit global/local conversion. Live terrain provider, prepared socket selection, actual game GPU pooled graph source routing and automatic same-tick restore hook remain open. Existing legacy particle/cue path not migrated. No full feature completion claim.

Build, CTest, hardware, profile and visual QA NOT RUN at user request. No main sync/merge. Next: planetary coordinate bridge and live terrain, then SpectraCollision and remaining matrix.
