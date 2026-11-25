# Model triplanar material

Branch codex/model-triplanar-20261125, base 54ca01d1b. Render owns projection and shading. Modeling exports ordinary GLB. Shader Graph, game material definitions, material assignment and their existing Studio and MCP bridges own the public path. Terrain uses fourth-power normal weights and axis-oriented normal perturbations. The native bark graph provides a precedent for UV-free material shading.

Deliver a reusable colour and normal material with adjustable repeat length, verified on an imported mesh and a newly cut CSG surface. Validate authoring, saving, reloading, deterministic shader compilation and real Metal readback. Initial tracking only. Tests pending.
