---
name: aiverse-modeling
description: Author AIVERSE models and animations without external DCC tools - modeling documents (primitives, lathe, mesh edits, LOD, export to GLB), rigs with auto weights, generated animations - through MCP and the «Моделирование / Создание анимации» windows.
---

# Model and animation authoring

Use when a game needs simple models or motions made inside the engine. Importing existing assets is
`skills/visuals/aiverse-assets-import`; materials and look `skills/visuals/aiverse-look`.

Authority: `docs/modeling.md`. Host-only library: `tools/modeling`. Game builds consume ordinary GLB models and
`view.animationGraph`; authoring data never becomes simulation state.

1. Documents: `modeling_create`, `modeling_list`, `modeling_get`, `modeling_edit`, `modeling_duplicate`,
   `modeling_apply`, `modeling_build`, `modeling_lod`, `modeling_export`; node graphs `modeling_graph_import`,
   `modeling_graph_set`, `modeling_graph_publish`; catalog `modeling_catalog`.
2. Rigs and motion: `rig_edit`, `rig_auto_weights`, `animation_generate`, `animation_edit`, `animation_sample`,
   `animation_bake`; graphs `animation_graph_set` / `animation_graph_validate`.
3. Standalone: `aiverse_modeling <project-dir> [engine-dir]` reads JSON lines.
4. Check: `aiverse_modeling_tests`, `aiverse_animation_tests`, `tests/modeling/mcp_preview.py`, screenshots.

Limits (from the doc): self-intersecting profiles are not supported; Euler joint limits are unsuitable for
unrestricted rotations.

Workflow recipe over existing tools and MCP operations; no runtime library.

For editable solid models, start with `examples/modeling/volume-csg.json`. In Studio use **Add volume CSG part**, Geometry dependencies and **Volume CSG**. Wire earlier fields into Boolean or noise inputs. For a density source, call `modeling_volume_texture_import` with a project-relative SPVT path and frame. Merge its returned texels into an existing densityTexture field. Save the source with `modeling_edit` and pass its returned revision to `modeling_export`. See the field and sampling limits in `docs/modeling.md`.
## Capture an imported model

Run `aiverse_modelview --headless-capture cpu model.obj preview.png` to render a 512 × 512 bind-pose thumbnail with base color, vertex colors and the base texture. Windows builds also accept `d3d9`, `d3d11` and `d3d11-warp`. The command writes the PNG at the supplied path. See `docs/cpu-renderer.md` and `docs/direct3d.md` for backend limits.
