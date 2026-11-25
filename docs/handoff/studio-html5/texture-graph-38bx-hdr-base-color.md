# Stage 38BX — linear HDR base color and source alpha

Parent: `62a5c29783ff00db6fb1abc1561790387fab6526`. Branch: `feature/aiv20261124_texture_graph_gpu_slots`.

- Explicit base-color and emissive resident float texture publication using existing snapshot transaction, generation pin, rollback and retirement paths. Existing emissive APIs retained as wrappers.
- Separate U2 bit 16 for linear base color; bit 8 remains emissive. Metal/GLSL preserve legacy AO bits, clamp linear albedo to physical [0,1], and preserve straight alpha through existing material alpha rules. No automatic opaque-to-blend conversion.
- Studio channel selector captures selection in target stamp, resets old preview when preparing, refuses overlapping bake/queue. Native rejects unknown channels.
- Source edits only; compilation, shader execution, GUI, and hardware validation pending. No persistent material export or general custom shader support.
