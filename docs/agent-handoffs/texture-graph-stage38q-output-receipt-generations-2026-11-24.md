# Stage 38Q — generation-safe output reuse receipts (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Base: `72be9a8946e03f65fc625dff71057cba065a76b7`
Plan: `1febb8905e26fa7c3883b7dea04f5c577bb3e678`
Implementation: `01f81d38f25a79607a28328df0c4b548f506a75c`
Fixture lifecycle fix and inspected revision: `05332877ab3d67145dd8c26d919eb45c68a63703`

Stage 38Q: migrated TextureGraphGpuOwner incremental output receipts from raw index/texture pairs to weak GraphImageReference values. Receipt capture uses the original material pin and verifies the live resident image; reuse validates both original material generation and receipt generation against that image before no-op or partial publication. Existing scope/device epoch, appearance identity, material revision, dependency and image-name checks retained. Missing or mismatched generation invalidates the receipt, refuses publication and requires cancellation/retirement of the rejected request before regeneration. Receipts do not pin obsolete images. Real-RHI acceptance fixture now injects a different AO pin while preserving the same index/name/handle/version, requires refusal of cached no-op, cancels and requests full regeneration. Twelve exact-commit static source checks passed. Fixture NOT RUN; full native build and GPU/Mac gates pending. No default-on collection certificate or renderer/native consumer migration claimed.

## Actual evidence

Read back the exact committed blobs using GitHub: TextureGraphGpuOwner.h db343982d1d1354ffb2fb3f426a68a136b29ecc7; texture_graph_gpu_acceptance.h d7d5a31136299fd221b61eb10e5e935bac1423fd; TextureGraphMaterialBinding.h 01fadcf8c87a80b0b973e8b4bf0248d9fd500ef0. Twelve static assertions returned true: weak receipt; original material pin; resident validation; receipt generation validation; image-name guard; receipt invalidation; check before no-op; original-pin capture; invalid capture rejection; wrapper generation checks; mismatch fixture; cancel-before-regeneration fixture. These are source checks only. No C++ compiler, CMake, hardware or runtime test ran in this stage. Do not mark tests PASS.

## Next

Audit render-time material binding resolution (Renderer3D ordinary PBR, graph slots, material tables, LENS). Introduce a shared checked resolver with existing owner/pin semantics, without creating a parallel resource manager. Add actual Model tombstone/reuse fixture with old weak reference and newly generated image at same slot. Review material override pin invalidation and external native raw references. Keep automatic default off until complete certification. Run actual native integration and strict Metal/Vulkan timing fixtures, Studio/MCP and merged-tree Mac quick gate before feature readiness. No main merge.
