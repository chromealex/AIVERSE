# Stage 38CC — BC DDS material exports

Parent: `b3bca2f7ea15a0805e26a3030b85057ccff3e098`. Branch: `feature/aiv20261124_texture_graph_gpu_slots`.

- Deterministic CPU BC1/3/4/5 DDS DX10 export, full or single mip, color-aware mip filter, normal renormalization and data averaging. Strict output compatibility and no overwrite. CPU graph or accepted GPU bake pixels feed the same encoder; native DDS parse verifies publication. Studio format selection, mip toggle and byte diagnostics.
- Native material loader preserves DX10 blocks and authored mip chains; supported BC1/3/4 use existing compressed upload, BC5 falls back to RGBA8 with reconstructed normal Z. Decoded authored-mip fallback for unsupported formats. Studio document loader accepts DDS.
- Added DDS round-trip fixtures; no compilation or GPU tests run. GPU block encoding, native BC5 shader reconstruction, BC6H/BC7 and ASTC encoding remain future work; existing ASTC packaging tool unchanged. Main untouched.
