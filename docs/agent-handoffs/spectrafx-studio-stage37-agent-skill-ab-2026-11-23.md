# SpectraFX Stage 37 — agent skill and native A/B replay parity (2026-11-23)

Repository chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123; no main merge.
Commits b2911fbd2117f182e4d746ddf75bd42b5f1efdbf and 02e7c24611273d55c06b99d4d5d3eee8ff694681.

## Implemented
- skills/visuals/aiverse-spectrafx/SKILL.md: added a focused native FX Replay/Capture workflow for agents, including CPU RNG seed, Metal allocation-only seed, mutual exclusion, replayReady barrier, GPU seed failure handling, bounded asynchronous JPEG capture and request-time provenance limitations.
- apps/studio-web/src/panels/SpectraNativePreview.tsx: native A/B mismatch diagnostics now include replayReady state. A verified exact-frame serial is compared only when BOTH captures explicitly advertise frameExact=true. With current RHI frameExact=false, UI correctly warns about unverified frame identity instead of implying exactness.
- Existing native A/B mismatch logic for tick, generation, backend, GPU, background and camera was already present on the branch when inspected. It was not reimplemented or claimed as new work.

## Tests actually run
NONE, per user's instruction to finish feature work before QA. GitHub source readback and successful commits are not compilation, CTest, TS checks, GPU tests or runtime verification.

## Limitations
- No exact GPU readback completion serial/fence and no GPU shader RNG.
- The Studio bridge is not automatically an MCP operation. MCP discovery/schema parity remains outstanding and requires auditing the workspace tool registry before changing code.
- CPU and GPU seeds are not interchangeable.
- Full editor QA and source compilation deferred.

## Next
Audit native Studio and workspace MCP tool registry, add discoverable Replay/Capture commands where supported; finish Graph Editor gaps, then comprehensive acceptance tests after feature completion.
