# Stage 38W — scoped native image access and model-copy boundaries (partial)

Branch: `feature/aiv20261124_texture_graph_gpu_slots`

Base: `14905ed704a6feb15dfd8535d681997b337b50d5`
Plan: `c2ff232ab4bcb6cb184900e3dd60c02be91228b4`
Implementation: `74a65ce2f97b0bd701bbc6fb639f5d58a22ba909`

Stage 38W: GeometryScene HLOD proxy refuses source Models containing runtime graph images/tombstones or pinned materials before copying image descriptors into a new Model; existing source draws remain. Raw material comparison adds bounds and provenance guards. ModelMerge index-only material palette declines pinned materials, preserving ordinary checked draws. GLB export explicitly rejects runtime images/tombstones and material pins; authored offline bake required, CPU pixel-size guard retained. NativeGraphImageRead combines the actual cache's ModelReadLease, exact bound device epoch and checked image generation pin into a move-only owner-thread scope. Raw texture is borrowed only during this scope; existing cache and image retirement mechanisms remain sole owners. No GPU fence or concurrent mutation permission added. Existing raw Model/texture APIs remain and must be audited; stock Client/Studio default collection not enabled. Regression cases added to material_batches, modeling, model_image_transaction for index-only palette refusal, export refusal, wrong model/epoch, active reader shutdown refusal, move/reset and pin release. Seventeen static assertions passed; fixtures NOT RUN.

## Native usage

```cpp
// Capture ref from the original material generation while the owner is live.
// Keep this weak reference, not the raw slot or GPU handle, across frames.
auto ref = render::texture_graph::referenceGraphMaterialImage(
    material, &render::Material::baseTex);
auto read = render::texture_graph::NativeGraphImageRead::acquire(
    cache, model, ref, device, deviceLifetime.lease());
if (auto texture = read.texture(device); texture.valid()) {
    // Submit synchronously using the existing renderer/RHI contract.
}
read.reset(); // before owner mutation / sweep / teardown
```

The owner must already bind the cache to an explicit device epoch. The scope is not a replacement for GPU completion tracking. A native caller may not retain a raw texture after the scope or pass it to another owning subsystem.

## Actual evidence

Read back seven committed blobs. Seventeen static assertions true: HLOD source/runtime and pin checks, bounds and provenance checks, palette exclusion, offline export and pixel extent, exact epoch and cache read, generation pin, move-only API, texture lease gate, reset order, wrong model/epoch fixture, shutdown and move fixture, export and palette regressions. No C++ compilation, tests, full build, hardware acceptance, Studio/MCP or Mac gate ran in this stage. No other agent approval obtained for shared owner changes.

## Next

Stage 38X: adopt NativeGraphImageRead at actual Client/Studio external access boundaries; audit remaining raw Model getters, stale materials and renderer replay. Inventory every stock host root with a real owner-thread/reader contract; keep default automatic collection disabled until a complete certificate exists. Consolidate all pending regressions and run on complete checkout, then real Metal/Vulkan and merged-tree Mac gate. No main merge.

