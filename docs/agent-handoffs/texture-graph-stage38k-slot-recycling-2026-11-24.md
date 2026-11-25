# Stage 38K — certified procedural image slot recycling

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `98514f1759ad0ae28f74c39bf6a5386dcc401c1c`
Implementation: `6d1584fbd60d331815b85c42f07590630f17d507`
Compatibility and regression: `d1e871dbaddfebfe27b1567f63b9324f51204dcf`

Stage 38K: procedural image index recycling is explicit and restricted to trusted native complete-root collection with GraphImageRoots::recycleSlots=true. Stronger certification means all past raw index/handle consumers and replay sources are permanently detached, and stale raw references will never be recreated. Only tracked, unpinned graph images receive a tombstone retirement marker (old pin weak reference and device epoch). A slot is reusable only after that pin expires, the existing shared retirement pool is empty without unrecoverable batches, the epoch matches, and the slot is still empty. Registry procedural publication opts into recycling certified slots; general ModelImageUploadTransaction remains append-only by default. Reservation chooses distinct reusable slots first, then contiguous append indices; allocations are staged before mutating any Model, all models preflighted before commit, aborted transactions preserve tombstones and retire only staged new resources. Reused slots advance model material revision; historical slot cap counts only actual new append slots. Ordinary reclamation and stock automatic sweep do NOT certify recycling. Existing uncertified tombstones stay permanently empty until Model reload; no retroactive certification or index compaction. Raw int indices are not generational handles: callers must honor the stronger proof, and old snapshot/pin roots still prevent recycling. No remote caller can enable root certification. Isolated exact-blob pin/retirement policy test PASS on GCC, Clang and ASan/UBSan. Integrated appearance fixture authored, NOT RUN.

## Actual evidence

Reconstructed exact repository files in an isolated Linux directory and verified with git hash-object:
- engine/render/graph/TextureGraphImagePins.h: cf441ea724e15301e600f07df401b9dac62c6ca8
- tests/render/texture_graph_image_pins.cpp: 5b4a5866779df98093329878bd7caba62eefffe4

```sh
for cc in g++ clang++; do
  "$cc" -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_image_pins.cpp -o "test-$cc"
  ./"test-$cc"
done
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -I. tests/render/texture_graph_image_pins.cpp -o test-sanitized
./test-sanitized
```

GCC 14.2.0 (Debian 14.2.0-19) and Clang 17.0.0: all three executions exit 0, no sanitizer diagnostics. Test asserts immutable material pins, retirement marker rejection while the old pin is held, epoch mismatch, undrained pool rejection, eligibility after pin release and pool drain. These are CPU policy tests, not integrated transaction tests. Integrated appearance fixture asserts ordinary tombstones remain unavailable, certified collection respects explicit pin, reuse is blocked until retirement drain, allocation failure leaves tombstones untouched, and successful publication reuses indices without vector growth or altering pinned current bindings. Those assertions NOT RUN. No full native build, CTest, GPU hardware, Studio or Mac gate performed this stage.

## Native contract

At a joined frame boundary with a complete, permanently detached raw-index and raw-handle root proof, pass GraphImageRoots with recycleSlots=true to the existing collection API. A subsequent graph material publication through AppearanceAssetRegistry can reserve eligible indices. Never set this flag for a host with incomplete renderer cache, replay, or external index proof. Stock automatic collection stays opt-in and does not recycle. The generic texture-cache speculative append contract remains unchanged because its transactions never enable recycling.

## Next

Stage 38L: optional scope profiling of actual dispatched fused groups, not invented authored-node timing; keep one host-owned timing consumer. Finish stock renderer root proof and integrated Metal/Vulkan and Mac acceptance before enabling recycling automatically or merging.
