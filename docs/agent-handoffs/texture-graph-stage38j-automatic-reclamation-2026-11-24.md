# Stage 38J — immutable binding pins and certified automatic sweep

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `6d5e2135123a09514eb73a1c2f81737b7e45dc13`
Pin implementation: `5cc598ea6447874a00d32a01f317c8b364b63b46`
Frame integration: `59f16c26e12bdcd0a08d76774b20ef066341ef4a`
Diagnostics and teardown: `bdf18f070ae694d432b9931b72160fd69ee84a5d`

Stage 38J (opt-in, partial default-host adoption): new CPU-only GraphImagePin and immutable shared GraphMaterialPins protect procedural images referenced by copied Materials, old snapshots, camera/static packet material copies, and native explicit pinPublishedGraphImage handles. CPU and GPU graph transactions install channel pins before transferring images, preserving previous generations on partial material publication. Existing complete-root collector also refuses a still-pinned image; automatic trackedOnly mode refuses legacy images without provenance/pins. GameView now has a native GraphImageRootProvider callback and bounded round-robin sweep of at most one resident Model per owner frame. No pointer retained across frames; all current choice/LOD/stage roots and base materials are scanned. Callback must certify all remaining raw index/handle consumers, external roots and future replay sources are detached or enumerated; it must not reenter, and returned spans must live through the synchronous sweep. The existing ModelImageRollbackPool and GPU retirement fences perform actual destruction; no new GPU owner. GameClient and editor frame hooks service opt-in sweep only between graph requests; no per-frame sweep when disabled. Receipt cache invalidated conservatively after a successful collection, explicit client collection also invalidates it. Shutdown clears native callback. Native client status and editor materialTarget/Studio show sweep counters. NO stock automatic certification: raw-handle/renderer-cache proof is not complete, so the default provider is empty. No MCP operation may certify roots; do not install a provider returning true without proof. CPU pin test passed GCC 14.2 and Clang 17; Clang ASan/UBSan pass. Integrated appearance fixture extended, full native build and GPU tests NOT RUN.

## Actual evidence

Exact Git blobs reconstructed in a temporary Linux directory and verified with git hash-object:
- engine/render/graph/TextureGraphImagePins.h: 1fc43cc925d2a4013912d117167b42121fcc78f5
- tests/render/texture_graph_image_pins.cpp: 1bc5ef28fc4d118a5af1a828cdef9926bc81e066

```sh
for cc in g++ clang++; do
  "$cc" -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_image_pins.cpp -o "test-$cc"
  ./"test-$cc"
done
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. tests/render/texture_graph_image_pins.cpp -o test-sanitized
./test-sanitized
```

GCC 14.2.0 (Debian 14.2.0-19) and Clang 17.0.0: all three executions exit 0, PASS immutable material-copy pins, partial replacement, external pin, last-reader release. No sanitizer diagnostics. These are isolated CPU policy checks, not integrated tests. `git ls-remote https://github.com/chromealex/AIVERSE.git HEAD` exit 128: Could not resolve host: github.com. Native compile, CTest view_appearance_prepared, auto sweep callback/renderer cache integration, GPU, Studio/MCP and Mac gate NOT RUN. Appearance fixture now checks retained Material copy alone protects old procedural bindings and explicit shader root survives.

## Host usage and remaining proof

Native owner may set GameView::setAutomaticGraphImageCollection / GameClient::setAutomaticRuntimeGraphImageCollection with a verified GraphImageRootProvider. The callback receives the current Model and a vector of external material spans. Return false if any raw index/handle, queued replay, external pin without Material copy, or concurrent producer cannot be proved safe. For persistent native raw references hold pinPublishedGraphImage() for their entire lifetime. Material pins are CPU metadata and do not themselves extend Model or Device lifetime. Raw-handle tables and externally authored indices still require root proof. Stock editor/client intentionally do not register an unverified provider. Automatic collection invalidates output reuse receipts conservatively; optimize only with explicit per-image content identity. Tombstones are not reused.

## Next

Stage 38K needs a proof that neither old image indices nor raw handles can alias recycled slots. Complete the concrete host root provider (including renderer caches/tickets and secondary camera replay) before enabling stock automatic collection. Run integrated native, Studio and GPU acceptance before merge; do not infer readiness from the isolated test.
