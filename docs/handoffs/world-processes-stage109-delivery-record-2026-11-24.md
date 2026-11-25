# Stage 109 — effective live PHYSIS gravity

Base 8fa021b1. In progress. Add opt-in Studio live gravity toggle, sample canonical physis::gravityAt at fixed body-local surface sites on all surface bodies at a completed local match tick. Include baseline, additive fields, override priority and clamping via existing owner, not a new solver. Explicit cap on work, exact vectors and magnitude/radial classification, zero markers, body summaries, existing Renderer3D and map guards. Distinguish authored individual fields from effective live arrows; no persistence, physics writes or new RPC. Source fixtures for overlap, zero, reversal, cap and body isolation. Tests/build deferred; no main merge.

Status: implementation 2afaf91b1c5422d8f893de0ca64dab4647f2d9fa. Build, tests and visual acceptance NOT RUN. Next AETHER gas/flux diagnostics.
