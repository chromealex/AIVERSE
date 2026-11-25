# Stage 38BZ — HDR graph environments

Parent: `edd4c42868ad8f1721b4b30817e0023d30b544d6`. Branch: `feature/aiv20261124_texture_graph_gpu_slots`.

- Full-sphere graph UV resampled to 2:1 lat-long, nonnegative RGB and opaque environment alpha. Editable HDR studio environment template.
- Explicit 2:1 EXR export and EXR/RGBE environment file import. CPU graph or accepted raw GPU readback can populate the existing CPU GGX/irradiance+BRDF atlas without disk I/O; old atlas is retained until replacement upload succeeds. Session environment supports rotation/intensity via preview controls.
- GPU graph evaluation is not GPU PMREM. No scene-wide lighting or skybox integration, no hardware/GUI/build validation. Main not merged.
