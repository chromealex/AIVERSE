# Stage 63 — timeline guards

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits 3d2e4b9f2efa80f4feb4e6f6986930ef946e4f38, fd3e30876f5358b5947e53eca9785ea409a28292, 8da4bd44b1cbe51f32c2e60c9e187d76acdc62c3, 752d414c18cf02b0047ccdae5c02b35af84c8420.

StudioSpectraPreview.h blocks conflicting commands, adds checkpointCancel and 240-prepare-frame budget; types.ts and SpectraNativePreview.tsx expose progress/cancel; Workspace.cpp extends MCP action schema. Skill updated. No tests/builds. Cancel does not abort submitted GPU readback; frame budget is not wall-clock timeout. Next audit native dispatch allowlists and exact capture provenance, then remaining restore/preset gaps and QA.
