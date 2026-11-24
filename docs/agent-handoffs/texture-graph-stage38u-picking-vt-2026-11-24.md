# Stage 38U — picking, VT feedback and static impostor generation guards (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Base: `0021ef3127233fb8f948ef7cba6fb1e72f18a8c6`
Plan: `c593e0ddb8a85a7a56bda55b07a174b40491bc05`
Implementation: `0cd04ef3eeddb094035d41df4f81ace813f00e9f`

Stage 38U: shared ModelSurfacePicking CPU alpha, rigid and deformed virtual texture feedback, and StaticImpostorLod base color sampling now resolve checked material generations. CPU picking refuses missing/stale/unpinned/retired or truncated alpha data rather than selecting an unproven surface; this is conservative CPU picking, not exact GPU-only or fallback-white rendered-ID parity. VT feedback retains existing white/coverage fallback. Static impostor retains material-factor fallback. StudioModelPickingTests includes a recycled slot with opaque replacement pixels, old pin, naked index, tombstone and truncated data. Fourteen static source checks passed; fixture NOT RUN. Inspected TextureResidentStreamer/TextureStreamQueue: independent queue-created GPU storage and queue-scoped stream IDs, no graph-image adoption API; raw sampled handles remain borrowed and the owner must prevent cross-owner alias/retention. Default-branch search identified no runtime streamer callers beyond fixtures/docs in indexed results, not a complete branch audit. Inspected StaticPreviewModel/GamePreviewResourceProvider and NativeScenePreview: borrowed draw packets use Renderer3D; lifetime still host-owned. Remaining direct consumers identified include VIVID sculpt masks, TerraWeave impostor, BakeScene and HumanAppearanceBake/HumanWardrobe; no claim of whole-repository migration or default-on certification.

## Actual evidence

Read back five exact committed blobs from GitHub. Fourteen static assertions true: picking generation, conservative unknown alpha, CPU byte bounds, rigid VT, deformed coverage and source, impostor generation and byte bounds, no raw base slot reads in the four modified consumer files, same-slot opaque replacement fixture, old pin refusal, naked index refusal, tombstone refusal, truncated pixel refusal. No C++ compilation or runtime test execution in this stage. Native build, Studio picking regression, VT feedback GPU tests, Metal/Vulkan timing fixtures, MCP and merged-tree Mac gate NOT RUN.

## Next

Stage 38V: coordinate VIVID sculpt GPU alpha masks and vertex-source picking with their owning agent; inspect HumanAppearanceBake/HumanWardrobe and TerraWeave impostor material reads/overrides and BakeScene's index-keyed image cache. Apply the shared resolver or explicit offline-input contract as appropriate, avoid cross-owner rewrites. Audit remaining external raw handles and model/reader lifetimes. Generic streamer IDs are scoped to their owner, not graph generation references; document the raw-borrow contract and do not graft a second GPU owner onto graph outputs. Stock automatic collection stays disabled until complete root proof. Run all previously authored regressions on full checkout and obtain actual GPU evidence before main integration.

