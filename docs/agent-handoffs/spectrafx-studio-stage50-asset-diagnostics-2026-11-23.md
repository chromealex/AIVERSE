# Stage 50 — asset diagnostics

Branch feature/spectrafx-html5-data-interfaces-20261123; not merged. Commits 9c5bf348aeaa65eed61f0454a837a447f66db694, af3fd855951b62417552965c151090225e9149e6, ee082e6e81889f576f030b0e4e3d0b2bff246b67.

StudioSession.cpp adds GUID-scoped CPU model/image decode, catalog-restricted file reader, 64 MiB cumulative input limit and gpuVerified:false. SpectraGraphAssetInspector.tsx displays unresolved/legacy paths and explicit Check asset controls. Skill updated.

Tests: none, deferred by user. No compilation or runtime verification. Missing index entries may also be filtered or truncated. CPU decoding is not GPU verification; input limit is not a decoder memory/time limit. Next: register typed bridge contract, audit native dispatch and decoder bounds, distinguish missing from unsupported index entries, then finish GPU RNG and capture features before QA.
