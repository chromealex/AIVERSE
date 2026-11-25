# Stage 38O — explicit/automatic client root proof parity (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Base: `88234e32c9dfb7449fe211b251082f1542e7b91a`
Plan: `fbe07ec40b250a029b8d482a0caf97a29aeaaa5d`
Implementation: `11fdf7a5e68271e4e1873b8c1615f3db788130a9`

Stage 38O: GameClient explicit collection now shares its main/inset Renderer3D and LENS root inspector with automatic collection. Explicit collection refuses busy/unreleased GPU results, stale identity, incomplete external proof and unresolved owner roots before retirement; it never detaches replay. GameView accepts an optional synchronous host root inspector after resolving the exact resident Model and before reclaimGraphImages. Existing non-client callers retain the full external proof contract. Automatic default remains off, no external raw reference certification. Ten exact-commit static wiring checks passed; native integration and GPU tests NOT RUN.

## Changed code

- `apps/client/GameClient.cpp`, `apps/client/GameClient.h`: shared client root inspector; explicit host rejects unreleased graph results and checks the same renderer and camera roots as automatic host collection.
- `engine/view/game/GameView.h`, `engine/view/game/GameLook.cpp`: optional host callback runs only after the target/identity/reader certificate checks, using the exact resident model; refusal stops before image retirement. Direct non-client callers remain responsible for complete external proof.

## Evidence

Read back four exact implementation blobs through GitHub and evaluated ten static source assertions: manual refuses pending GPU result; manual passes shared inspector; inspector covers main/inset/LENS; automatic uses same inspector; automatic retains external proof; manual does not detach replay; identity/external reader checks precede callback; host refusal precedes reclaim; optional callback preserves existing caller syntax; receipt invalidation retained. All ten true. These checks are not compilation or execution. Native build, integrated host refusal/retry and root retention fixture, Metal/Vulkan hardware acceptance, Studio/MCP and Mac quick gate NOT RUN. No new runtime test authored or executed in this stage.

## Next

Add a joined native host regression exercising explicit refusal for active renderer/camera roots and successful collection after certified release. Audit default host raw-index and raw-handle exposures and migrate to generational binding access where necessary; never enable stock automatic collection from a constant true certificate. Review all owner boundaries and verify integrated build, strict GPU timing fixtures and merged-tree Mac gate before readiness. No main merge.
