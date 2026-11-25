# Stage 38P — generation-safe native image references (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Base: `814bb3245822e3069c115393a560c523fb3de493`
Plan: `0af0231561537bb2f724cae04dcbf9bbb55d486f`
Implementation and tested blobs: `39b91c8d5f99eada9fec27a69e77b39684502460`

Stage 38P: native generation-safe procedural image access foundation. GraphImageReference stores a weak immutable pin identity plus slot; acquire compares control-block ownership and rejects wrong index, expired or replaced generation, including same-address ABA. Weak references do not retain images. referencePublishedGraphImage captures a currently owned Model slot; referenceGraphMaterialImage captures the ORIGINAL material pin, never re-captures a stale raw index; acquirePublishedGraphImage returns the checked handle and strong pin. Hold pin while using raw handle under the existing live Model/device and joined owner-thread contract. This is not a Model/device lifetime lease and does not authorize concurrent mutation. Existing raw index APIs remain public and require complete external proof; stock automatic collection stays off. Isolated exact-blob fixture passed GCC 14.2, Clang 17 and Clang ASan/UBSan; native binding wrapper, actual GPU and full build NOT RUN.

## Changes

- `engine/render/graph/TextureGraphImagePins.h`: weak generation identity, no raw pointer ABA; existing retirement owner unchanged.
- `engine/render/graph/TextureGraphMaterialBinding.h`: reference capture from current resident slot or original material pin, checked native acquisition returning handle and strong pin. Capture and resolve only under live owner-thread/reader contract.
- `tests/render/texture_graph_image_pins.cpp`: wrong slot, null pin, held lease, weak non-retention, replacement while prior generation alive, expired generation, default reference and same-address distinct control block regression assertions.

## Actual evidence

Exact two committed blobs fetched via GitHub and reconstructed locally, verified by git hash-object:
- TextureGraphImagePins.h: 535cf9189549eab17665cf0f6b4bae013ead204b
- texture_graph_image_pins.cpp: edb65d0959ab0db73275a2a6bbce8d1e673667da

Commands in isolated source tree:
```sh
g++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_image_pins.cpp -o test && ./test
clang++ -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_image_pins.cpp -o test && ./test
clang++ -std=c++20 -Wall -Wextra -Werror -fsanitize=address,undefined -I. tests/render/texture_graph_image_pins.cpp -o san && ./san
```
All three exited 0 with PASS. GCC 14.2.0 and Clang 17.0.0. No complete checkout or CMake execution; wrappers in TextureGraphMaterialBinding.h not compiled by this isolated test. Full native build, actual Model recycling integration, renderer/client, GPU timing acceptance, Studio/MCP and Mac quick gate NOT RUN. Manifest tests remain not_run.

## Next

Stage 38Q: use this reference API in existing native consumers that retain graph image indices or handles, beginning with internal material/output reuse receipt and test the actual Model tombstone/reuse path. Audit copies across LENS, renderer caches, material overrides and external native APIs. Preserve original generation when copying or retaining a binding; never create a new reference from a stale index. Do not enable default collection without complete root certification and owner/lifetime audit. No main merge.
