# SpectraFX Stage 36 — GPU frame and fence capture observations (2026-11-23)

Repo chromealex/AIVERSE; branch feature/spectrafx-html5-data-interfaces-20261123, not merged.
Commits c86d26c1699336a139ae8b279ca9826cb81674d1, 0a778ad472147e6f70f9e84ab5cb6400555477bd, 8912b32530ddd2032858eb9e5873554b4dede1de.

## Changed code
- apps/studio-native/StudioScene.h exposes FX device's existing frameSerial, completedFrameSerial, gpuSubmissionFence and gpuCompletedFence as read-only helpers.
- apps/studio-native/StudioHost.cpp records all four values at readback request registration, with explicit frameExact=false and existing render-owner status snapshot.
- apps/studio-web/src/types.ts exposes typed optional provenance fields.

## Tests actually executed
NONE. User requested feature-first implementation, deferred compilation and tests.

## Honest semantics
These are RHI observations at capture REQUEST, not a verified exact captured-frame serial/fence. A backend may return zero (unsupported). The existing gfx::Device::CaptureFn carries only width/height/RGBA; it does not supply captured frame serial or completion fence. Therefore frameExact=false is intentional. Never present a request serial as proven captured frame. No GPU Shader RNG support has been implemented.

## Next steps
- Add an RHI capture completion envelope carrying exact frame serial/fence from each backend, not an inferred token; propagate to Studio/MCP and set frameExact only when verified.
- Finish MCP parity, skills catalog and Graph Editor feature completeness.
- After all features, run C++/TS builds and Metal/Vulkan/GLES QA with actual screenshots.
