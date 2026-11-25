# Stage 38N — renderer and LENS material root inspection (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `2e0b8a6b59b3a63f2f6cd656cd5f9c5c8262da01`
Renderer/LENS roots: `cdf02bcb0196c29141fb8039c686af1e7922b7a7`
Native auto-collection and recycling: `1f84a5f8ebb03f89d7d4385f1b5499255d8ace6c`
Diagnostics and Vulkan failed-submit cleanup: `afcceb7951ef858cc9ff6a82584818b9fd2c9e49`

Stage 38N (partial default-host adoption): Renderer3D::appendGraphImageRoots enumerates its static packet and wind-debug material copies for the exact Model; refuses outstanding borrowed previous-scene ModelItems, tracked submission provenance, capture callbacks and raw GPU material-table references. GpuMaterialResourceTable::referencesTexture checks PBR/graph/alpha/VT handles. Wind debug cache records model ownership for correct invalidation. CameraModelSnapshot::appendGraphImageRoots enumerates owned material copies and refuses borrowed prepared attachment materials. Explicit detachCompletedGraphImageReplay releases old CPU scene/replay/ticket/diagnostic state only after host certification that no later CPU consumer needs it; actual GPU texture retirement stays in the existing pool. GameClient::setAutomaticRuntimeGraphImageCollection wraps the external proof provider with its known main/inset renderer and LENS root checks, optional completed-replay detachment and opt-in automatic slot recycling. GameView sweep now passes the certified recycleSlots flag. Studio/MCP/native status expose recycling enabled. Stock default still disabled: public Model image indices and external/native raw handle consumers cannot be proved absent from these local renderer checks alone. Never pass an unconditional true provider, never clear replay that remains needed, and do not return spans into renderer state that the detach option invalidates. This is concrete root inspection and a native opt-in automatic recycling path, NOT full default-on certification. Integrated tests authored for ICB table root checks and camera snapshot roots; NOT RUN. Nine static source checks passed; no new runtime tests executed. Also hardened Vulkan failed-submit staging cleanup without waiting on an unsignalled fence.

## Actual evidence

Read back exact committed sources at afcceb7951ef858cc9ff6a82584818b9fd2c9e49. Nine source assertions returned true: borrowed ModelItems gate, raw GPU table gate, all table channels, borrowed camera attachment gate, external provider gate, main/inset/LENS integration, certified recycling flag, diagnostic wiring, authored regression assertions. This is source inspection only, not C++ compilation or test execution. Local git ls-remote exit 128 (DNS failure), no complete checkout or SDK; no native integration, hardware, Studio or Mac test performed for Stage 38N. Stage 38M isolated Vulkan scope policy tests are recorded in its separate handoff.

## Native host contract

`GameClient::setAutomaticRuntimeGraphImageCollection(provider, recycleSlots, detachCompletedReplay)` stays opt-in. Provider must certify every external material/raw-handle/raw-index/replay consumer not owned by the inspected client renderers/snapshot, and maintain all returned spans until sweep returns. The detach flag is an additional assertion that prior CPU scene replay and provenance are no longer required. Recycling requires permanent detachment of stale raw references. No MCP operation can grant this certificate. The explicit collection API still requires a complete proof. No unconditional default-on provider added.

## Remaining

Stock editor/client default-on collection requires a complete audited raw reference contract (including external native project consumers and any retained render/provenance replay); full integrated native build, Metal and Vulkan fixtures with strict timings on supported hardware, renderer and appearance tests, Studio/MCP, Mac quick gate. Do not mark feature ready or merge main based on isolated tests. Consider moving raw public indices behind a generational binding API as a separate compatibility migration.
