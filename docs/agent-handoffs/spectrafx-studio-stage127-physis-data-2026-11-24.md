# Stage 127 — PHYSIS Data Interfaces

Branch: feature/spectrafx-html5-data-interfaces-20261123
Plan: 74ca37bf90d1dced2496cf6cbc30e00311666c2d
Implementation: a53f359e777c065eb21d4ff3f29c83adc4319b87

Implemented optional native host adapter engine/world/SpectraPhysisSnapshot.h: sample existing PHYSIS read-only 3D fields at a committed tick, bounded 4096 nodes, explicit missing coverage, shared immutable ownership through existing DataInterfaceRegistry. Wind and gravity XYZ, climate and fire channels. Signed world XYZ metres, no flat-plane conversion for wind/gravity. No new weather authority, graph format, shader ABI or RHI dependency. Nearest sample rather than interpolated or per-particle sampling. Unload/rollback clears provider explicitly; host is responsible for owner-thread handover. Authored example examples/spectrafx/data_interface_physis_v2.vfx.json, regression added to tests/vfx_graph_runtime_test.cpp, skill and roadmap updated.

Feature delivery: implementation source present, architecture reviewed, native API/example and skill delivered; existing graph edit/validate authoring applies without new schema. Live Studio host publication/MCP inspection of available providers not implemented. No persistence of ephemeral sampled state; graph bindings use existing persisted schema. No claims of full planetary weather simulation (PHYSIS owns its grid); 3D vectors use actual PHYSIS samples. Tests authored, compile/CTest, Metal/Vulkan/GLES, profiling and GUI/visual QA NOT RUN by explicit owner request. No main sync or merge in this connector stage; preserve other branches. No publishing evidence.

Next: actual host snapshot publication, TerraWeave/NEREID and socket provider bridges, audio and other functional gaps in modernization matrix. Do not mark full feature completion or run QA before owner requests it.
