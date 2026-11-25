# Stage 38S — override pin release, GI and actual transaction reuse fixture (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Base: `640dc3acc9a21d84edf1d999337f3907c09b2ead`
Plan: `b072f699d4fe1fff59eaea5c9b9e253480606db8`
Main sync: `c679efae03437f8efe0c96fe37a68c2a8281da87` (main `2be780abdf5fa93844472f34e07e4c6dabae8935`; only two site documentation blobs, verified unchanged in feature vs old main before taking main versions)
Implementation: `9865881a8aedcd47d7e72e492e4c27b958e3300a`

Stage 38S: shared setImportedMaterialImage clears the superseded channel's procedural pin with copy-on-write, including same numerical index or explicit missing texture; unchanged channels and old snapshots retain original pins, empty sets release storage. AppearanceAuthoringService uses it for all six authored PBR slots and clears stale Shader Graph sampler tails. GameView authored texture lookup excludes runtime graph names and retired/procedural entries. GiModelGeometry averages only checked resident material images, with CPU byte-length guard. Existing appearance_authoring and model_image_transaction fixtures extended; latter uses real ModelImageUploadTransaction and reclaimGraphImages with fake device and explicit root proof to test pinned refusal, fence-gated tombstone, same-slot reuse and stale weak reference rejection. Fifteen exact-commit source checks passed; neither fixture compiled or executed. Main documentation-only update merged into feature branch after matching base blobs. Native external/raw references, streaming and camera owner audit still incomplete; default automatic collection off.

## Actual evidence

Read back the six exact implementation blobs through GitHub. Fifteen static checks true: shared channel map, copy-on-write clear, six overrides, graph tail reset, internal name refusal, retired lookup refusal, GI resolver, byte-size guard, old snapshot preserved, same-index override, pin release, staged transaction commit, fence and tombstone, same-slot reuse, stale reference refusal. No C++ compilation or test execution in this stage. In particular the fixture uses a fake device, not GPU hardware even when executed. Native build, real Metal/Vulkan, Studio/MCP and Mac gate NOT RUN.

## Next

Stage 38T: audit texture streaming/cache and remaining native/preview raw consumers, and camera attachment material ownership. Preserve the existing device/model owner and explicit root proof. Check that all appearance import paths exclude runtime graph image aliases, not just GameView. Review ordinary and graph sampler overrides and unsupported tracked source bindings. Run the new fixtures and native build on a complete checkout; no default-on collection until complete external/native and replay lifetime proof. No main merge.
