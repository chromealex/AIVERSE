# Stage 38CB — HDR Shader Graph sources

Parent: `512d3ba1167c33b938c9c5fd7dd19aa6f40d4c13`. Branch: `feature/aiv20261124_texture_graph_gpu_slots`.

- Explicit material-fragment HDR sample and sample LOD nodes, linear file or `@radiance` source, four existing sampler slots. Type metadata included in material equality/hash; no sRGB decode.
- Native resident HDR adoption into saved Shader Graph snapshot with per-slot immutable generation pins, existing appearance transaction/rollback/budget, and Studio per-instance overlay. Other sources must already be resident imported textures. Studio edits source path and selects saved graph by name.
- Session-only graph binding, no vertex/postfx/particle HDR nodes, no automatic persistence of resident images. Compilation and GPU testing pending.
