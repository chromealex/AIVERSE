 # Stage 128 — water and terrain Data Interfaces

Branch: feature/spectrafx-html5-data-interfaces-20261123
Plan: 24feb7a5df3952bf302a4446d528de31bb4e9348
Implementation: 2522af1dbeecf5424f10b6dd4ccd5c2ee83c665a
Graph example: 0039f6be66acf4bdebf066f8fbb66c7a0ddb17fc

Shared SpectraFieldRegion validates 3D lattice extent and sample count, preserving SpectraPhysisRegion as alias. SpectraWaterSnapshot copies NEREID planar indexed queries or sphere queries at a host-selected committed tick/time. Existing NEREID policies remain authoritative, including their coordinate/precision limits. Mixed geometries require distinct provider names. Samples are nearest-node; depth, submerged and surface position are values at the sampled node, not exact continuous queries. No water data outside capture coverage. SpectraTerrainProviders binds immutable authored height tiles or copies a host planet generator configuration and uses existing sphere terrain relief, explicit base radius and relief scale. Reference up is not a surface slope normal. No live ECS reference on FX thread or new simulation, persistence schema, shader ABI or RHI dependency. Caller must republish/clear on rollback, world replacement or terrain changes. Host must match authored/rendered terrain coordinates and own immutable publication; no automatic scene attachment or arbitrary-size planetary support claimed.

Native source/API and graph example provided, skill/roadmap updated, regression fixtures authored in existing vfx_graph_runtime target. Existing graph authoring applies; no new MCP operations or Studio widgets. Automatic host publication, sockets, continuous sampling and GPU texture/vector fields remain open. Compile, CTest, hardware, performance and interactive/visual QA NOT RUN per owner request. No main merge or sync performed during this stage. No release-readiness claim.

Next: socket pose and actual scene-host publication, audio data providers, remaining modernization matrix.
