# Shared Graph Framework — stage 31A — 2026-11-24

Branch: feature/aiv20261123_feature_gate; draft PR #120.
Planning: 73ed7d011a47d2c7e32dc9613570725ca17aa56c.
Implementation: ef56b87b05de14f945006e26e7db9fd8393da0d7.
Fixture correction / implementation HEAD: 12e5a01379bf80147cca93e155433cd08facfe8c.

Introduced native TextureGraphMaterialBinding.h: full ordered texture set validation, explicit six PBR image-slot mappings, immutable generation-scoped names, collision refusal for resident and pending images, staged image upload using the existing ModelImageUploadTransaction, and a returned candidate Material retaining all unrelated base settings. Height and unpacked metallic/roughness are not bound. TextureGraphGpuPreview::stageCompletedMaterial bridges exact completed unpublished set/project/request to the native transaction; no direct live-model mutation, raw-handle RPC, competing cache or ECS authority. The outer native owner must abort on any staging failure, commit images before candidate publication, and retire old models safely. This stage still uses CPU pixels (including checked GPU readback) and ordinary upload. It is not GPU-to-GPU binding or complete runtime material integration. No save format changes.

Changed paths: engine/render/graph/TextureGraphMaterialBinding.h; apps/editor/core/TextureGraphGpuPreview.h; tests/vfx_graph_ops_test.cpp; docs/agents/graph-framework.md; skills/workflow/aiverse-graph-authoring/SKILL.md; docs/devlog/systems.json; docs/features/graph-framework.json.

Verification: GitHub tree/commit/ref writes accepted; fixture authored and checked against compileResolved explicit arity schema. NOT RUN: native build, CTest, mock GPU transaction execution, Metal/Vulkan, Studio typecheck/build, MCP audit, feature gate, Mac merged-tree quick suite and screenshots. No local Mac/KINGSTON checkout through connector. Main not merged or synchronized in this stage; reconcile before integration. No release/site publishing claim.

Next: 31B wire staging into existing native appearance owner publication (prepared model/candidate generation, rollback pool, model cache promotion if appropriate, material revision, bounded replacement retirement); only then expose an identity-based editor operation and Studio action. Coordinate with model resource and RHI owners. Investigate buffer-to-texture transport or storage-image generation with explicit usage, format and backend capability, fences and GPU parity; do not introduce a second resource manager. Add real staged transaction failure/collision and native GPU sampling fixtures. Outstanding Stage 30: draft-independent status polling, deterministic cancellation/timeout lifecycle tests.
