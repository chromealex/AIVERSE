# Stage 38I — target-bound incremental final textures

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Plan: `5fa7c6369d5319fc283905bedff096eef341112b`
Implementation: `acc12ec0fe7666323e8b4a7a563ed1115c93d556`
RHI regression: `1888756ad79c6e0fdb9011ec560ad0997991c7e7`
Identity hardening/test: `cac25b8b0201d2f75d435b79ddb511013c6fc172`
Final implementation: `1b1306513b32ca40021a4343f7a1ecb67951ce5d`

Stage 38I: opt-in `incrementalOutputs:true` for direct incremental materialized fence-only GPU sets with exact live appearance target (publication, version and material index); native `ResidentMaterialOptions::incrementalOutputs` plus target, GameClient forwards its resolved target, Studio requires Inspect live material, MCP validates target before start. One last successful targeted snapshot publication receipt per owner, scoped by project/device epoch/target. Exact length-prefixed resolved operation/parameter/dependency keys per bindable output, including graph size/mips/fusion configuration; bounded to 1 MiB. No cache hit for missing keys, different target or scope; cold request generates full set. On a matching receipt, unchanged outputs are removed from the generation/publication subset; their existing material slots stay unchanged, no new texture, pack, copy or mip generation for those outputs. Changed outputs use the existing fenced copy and transactional partial material publication, preserving all other bindings. If all outputs are unchanged, request is ready without dispatch and exact-target apply acknowledges it without changing the snapshot version. Before apply, validate model material revision, live snapshot identity, retained image index, handle, name and procedural provenance. On binding/revision mismatch reject and invalidate receipt; cancel and retry for full regeneration. No mutable image reuse, no extra GPU owner, no PNG export, no registry-key publication, no queue/plan reuse route. Output dependency equivalence is graph-semantic, not a claim of bit-exact fusion variants or CPU-reference parity. Status exposes incrementalReusedOutputs and incrementalOutputNoop; completedOutputs/totalOutputs describe only generated subset. Two exact-blob isolated test runs PASS; real-RHI partial/no-op publication regression authored, NOT RUN.

## Actual evidence

Reconstructed files from exact committed blobs and verified with git hash-object:
- engine/render/graph/TextureGraphOutputReuse.h: ad74001fdef4c244f889e2ed3ea9e4b9e530e663
- engine/render/graph/TextureGraphLiveness.h: 313d7640cb2c1fccc65745d5569ccf1d80dc1995
- tests/render/texture_graph_output_reuse.cpp: 847fe74f0ceb8a9be1ad36aca99301414143c07c

```sh
for cc in g++ clang++; do
  "$cc" -std=c++20 -Wall -Wextra -Werror -I. tests/render/texture_graph_output_reuse.cpp -o "test-$cc"
  ./"test-$cc"
done
```

GCC 14.2.0 and Clang 17.0.0: both exit 0, PASS output closure, independent branch, configuration, cold cache, no-op, key limit. Isolated Linux CPU policy only. Direct git ls-remote failed DNS (exit 128), full native checkout/build unavailable. Native RHI test asserts first full set, changed normal only, preserved AO handle and pinned old normal binding, stale target rejection, no-op without appearance revision change; these assertions have not executed. Studio/MCP build, integrated GPU acceptance, hardware timing and Mac gate NOT RUN.

## Next

Stage 38J: renderer-owned reference audit and safe automatic reclamation; do not certify incomplete external material roots. Stage 38K: stable index reuse needs a separate proof. Actual native acceptance before main.
