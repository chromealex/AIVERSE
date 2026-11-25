# Stage 38X — exact-target scoped host reads (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Base: `1b9334bf5fa6469680e16be25219565d504406e5`
Plan: `0d734b8c1220da65def87f9492f6bc4eb1b6f9d3`
Implementation and source checks: `fe76105a869d120bc3934f29cd95951217d5a0a3`
Fixture warning cleanup: `74ab8a92a9b003dd7431ac1075c421145a5e7256`

Stage 38X: GameView::readGraphMaterialImage resolves an exact resident appearance identity and bindable output using the original snapshot material pin; NativeGraphImageRead acquires from the actual model cache. GameClient::readRuntimeTextureMaterialImage and Editor::readSceneGraphMaterialImage expose the synchronous native scope, refusing active generation, outstanding output sets and invalid host state. The existing Editor materialTarget operation inspects six outputs with short-lived reads, emitting logical residentGraphOutputs names only (no raw handles); Client and Editor expose activeModelReaders diagnostic counts. Reader scopes are released before host mutation or next frame. Move assignment releases old pin before old reader and regression fixture covers moving between two active scopes. No new RPC grants a root certificate. Default collection remains disabled: legacy raw Model access and external replay ownership not yet fully certified. Seventeen exact-commit static assertions passed at fe76105a869d120bc3934f29cd95951217d5a0a3; subsequent fixture-only removal of explicit self-move avoids compiler warning. Native compilation and runtime tests NOT RUN; local git ls-remote failed DNS (exit 128).

## Native contract

Inspect the current target, then call the Client or Editor scoped read with the exact target and output name. Use the returned texture only while the scope lives and on the same graphics owner thread. Release before frame, mutation, hot reload or shutdown. GameView target lookup intentionally refuses while readers are active, so host read calls are sequential. Direct generic cache-scoped reads can coexist as covered by the authored fixture. GPU completion remains the owner's existing retirement responsibility. The diagnostic residentGraphOutputs list is empty if the host is busy/unavailable, not proof that no resident textures exist.

## Actual evidence

Read back eight exact implementation blobs via GitHub. Seventeen static checks passed: public View read, bindable slot and transaction guards, exact identity, snapshot selection, original pin and owner acquire, public Client read and busy guard/delegation, Editor read and guard/delegation, six-output probe, JSON names, reader diagnostics, move reset, move regression, preserved explicit collection provider. These are not compilation or runtime evidence. Removed explicit self-move in fixture to avoid compiler warning. Local `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` exit 128, Could not resolve host. No full checkout, native compilation, CTest, Studio/MCP runtime, GPU or Mac gate executed.

## Next

Stage 38Y: audit stock host raw access and replay retention explicitly, identify every remaining uncertified external Model/material/handle consumer, and establish a bounded default-on policy only where owner/lifetime proof is complete. Do not use unconditional provider callbacks or discard CPU replay still in use. Prepare a consolidated integration test command for the accumulated regressions and actual Metal/Vulkan scope fixtures. Main merge prohibited until required merged-tree Mac quick gate and verified acceptance.
