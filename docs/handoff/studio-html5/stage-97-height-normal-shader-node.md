# Stage97: shared material height normals

Codef8b61eb47, feature/aiv87_terrain_pbr. Shader compiler/catalog only; no new Studio window/bridge. normalFromHeight is material-fragment-only with3typed float4inputs, zero texture samples, no changed GPUABI. Existing catalog consumers discover arity dynamically. KINGSTON nativeEditor/Studio/renderbench/compiler buildPASS; shader_graph CTestPASS0.40s, seven actualMetal numerical fixtures and sixGLSL variantsPASS. Browser/mouse/UI parity NOT RUN. Detailed evidence and next6layerterrain stage in docs/terrain/AGENT_HANDOFF.md.
