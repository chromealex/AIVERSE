# Stage 38BY — persistent HDR library material

Parent: `119dec5806aedfaaa829144c1ea46df0c36eba1d`. Branch: `feature/aiv20261124_texture_graph_gpu_slots`.

- Export existing EXR and explicitly create an inherited `materials.json` variant referencing it, with base color/emissive choice. Studio exposes the operation and validates names/files; no overwrite of assets.
- Runtime authored material decode/cache/stage/upload retains separate linear RGBA16F storage and file provenance; independent of session graph pins. Studio resolves document HDR overrides in a bounded per-model pool. Ordinary raster decoding is unchanged; Studio ordinary file binding remains separate.
- No live HDR replacement on external file edits, HDR alpha-coverage mip chains, automatic saving of resident session images, or arbitrary material shader ABI. No compilation, GPU execution or GUI validation performed.
