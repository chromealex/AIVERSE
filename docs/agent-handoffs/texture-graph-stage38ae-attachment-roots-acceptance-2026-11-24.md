# Stage 38AE — attachment roots and integration checkpoint (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`
Base: `8eae6e3f40c527f3261d488179a640e6f7ff2bdc`
Plan: `f6994d3586684ceff344fa5dff003751874ae1a3`
Implementation: `060dd2f43261c60917cf0fc98b2f6fd89d52418b`

Stage 38AE (partial, integration checkpoint): GameView now enumerates attachment material arrays for the exact resident Model alongside main choices, appearance snapshots, LODs and construction/damage stages. The shared scanner is used by explicit and opt-in automatic graph image collection. Deferred-loading regression injects a legacy unpinned procedural image, loads a separate type with an attachment referencing the source Model, removes the base material root, verifies collection retains the attachment-only root, then trims the attachment owner and verifies image tombstone publication. 15/15 exact-commit source assertions passed at 060dd2f43261c60917cf0fc98b2f6fd89d52418b. Native checkout attempt failed (git ls-remote: DNS, exit 128); native compile, CTest, real GPU, live Studio/MCP and merged-tree Mac gates NOT RUN. Default-on reclamation and slot reuse remain intentionally disabled pending complete external native raw-reference/replay proof; no unconditional certificate. Stop speculative feature expansion here and run integrated acceptance on the exact branch before further policy changes.

## Collection boundary

- Base Model materials are always scanned by reclaimGraphImages. GameView adds main/LOD/stage material copies, exact appearance snapshots and attachment material arrays; the owner passes only roots for the exact Model.
- GameClient additionally inspects main/inset renderer and LENS snapshots. Active capture, submitted/replayed model packets and referenced GPU material tables remain conservative blockers. Replay detachment requires explicit caller proof that CPU replay/diagnostic consumers are finished.
- Scoped native readers and generation pins protect supported access; legacy raw APIs and trusted native callback escape boundaries are not fully mechanically certified. External completeness and permanent raw-reference detachment for slot recycling remain explicit opt-in contracts. Do not infer proof from an empty root list or install an unconditional provider.
- No new GPU owner, waits, format, MCP operation or default collection policy. This stage fixes a concrete missing root, not all external ownership.

## Evidence

Exact-commit readback: 15/15 source assertions (attachment loop and matching Model, main/LOD/stage scanning, both collector paths, base and extra roots, fixture injection/copy/removal/retention/reclamation/tombstone and runner registration).
Changed: engine/view/game/GameLook.cpp, tests/deferred_loading.cpp.
Local git ls-remote https://github.com/chromealex/AIVERSE.git HEAD: exit 128, Could not resolve host: github.com. No checkout, no native compilation, no CTest, no GPU evidence.

## Next: integrated acceptance, not another speculative stage

Run the consolidated runner on the exact feature-branch checkout with existing configured platform presets or the equivalent configured build. Enable AIVERSE_BUILD_TESTS, AIVERSE_BUILD_EDITOR, AIVERSE_BUILD_TOOLS, AIVERSE_BUILD_STUDIO_HTML5 and AIVERSE_TEXTURE_GRAPH_GPU_TESTS. On Mac use --backend metal; on a supported native Vulkan machine use --backend vulkan and AIVERSE_VULKAN=ON. Capture complete output, exact commit, device/driver and failures.

Run separate material_batches --unit, live Studio/MCP acceptance and required merged-tree Mac quick gate before main. Default-on collection requires independent root proof, not simply green tests. Fix actual compilation/runtime failures before claiming readiness.
