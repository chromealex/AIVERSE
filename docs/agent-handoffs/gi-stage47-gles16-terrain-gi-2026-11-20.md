# AIVERSE GI — Stage 47: actual GLES3/low-descriptor 16-slot terrain GI (2026-11-20)

## Branch, exact commits, actual verification

Branch: `feature/gi-lighting-portable-backends-20261119`. Draft PR #47 targets `feature/hybrid-dynamic-gi`, NOT current main. **Source authored and committed, native build/CTest/GLSL/Metal/SPIR-V shader compile, GLES3 mobile pixel parity and performance NOT RUN.** No PASS may be claimed.

- `de759ca1ccb06f11c2cb7951bdf5b6be5871b0d6` — `modules/rts/view/RtsTerrainShaders.h`: fragment declarations of rich splat/cliff/normal samplers tex4..14 now guarded by `#if TERRAIN_TIER >= 1`. Low tier no longer declares unused rich-texture samplers.
- `4d8a5aead2ad193b45665e129ef211f0b3bb8587` — `modules/rts/view/RtsTerrainShaderBudget.h`: the compact low-tier renderer remaps baked lightmap tex19→tex4, baked mask tex20→tex5, height-field trace tex18→tex6. Preserves tex0/1 direct light/shadow, vertex tex2/3 cell/dynamic, and fragment tex15 vision/fog. This helper is genre-specific to RTS and does not pollute common engine GI.
- `a4b1a97498af9bf4bebef803dfa994473d368eec` — `RtsTerrainRender.cpp`: devices with fewer than 21 fragment bindings pick low-tier compact shader (`TERRAIN_TIER 0`, `TERRAIN_DETAIL 0`) and declare at most 16 available texture slots, while rich Metal/Vulkan/GL continues original pipeline.
- `e39e9b09dc62ad4bfce66e409543b6d6928d0676` — `RtsTerrainRender.cpp`: compact draw path preserves the newly reserved tex4/5 baked and tex6 height trace bindings by *not* binding incompatible splat/cliff/normal/detail textures over them. Preexisting terrain visible fog binds tex15.
- `3daf2652275155ff5603c6b809022f1eb180595c`, `1e57f801e2b88b17f6228cea9d3e103cb58c951d` — source-contract regression `tests/render/gi_terrain_compact.cpp`, including preserved vertex tex2/3 lookup; **NOT EXECUTED**.
- `eb61527e6a7880f9db37ddfaf25bb461aaca3777` — CMake/CTest target `gi_terrain_compact`.
- `cb560f1158955bb99f96d4169476bab1f799d9f9` — compact low-tier **dynamic GI**: `giCustomGlslFunctions(Single,7)` injects CPU-probe atlas sampled at freed tex7. Rich terrain remains Single GI tex21. Deferred `Renderer3D::availableDynamicGiForCustomShader` + exact `bindDynamicGiForCustomShader` chooses the matching GI pipeline at render time. GLES3/WebGL2 CPU bounce uses RGBA32F data texture, not an HDR framebuffer or compute shader. Missing/not-ready GI atlas immediately selects legacy compact terrain pipeline.
- `a7cb0d0dbc094d70e77f1ca5a32a65ec50fe340a` — add explicit compact tex7 GI source-contract assertions for minimum GLES3 budgets.

## Functional architecture and compatibility

The **rich** terrain material is unchanged on capable Metal/Vulkan with ≥21/22 fragment texture units. It samples four splat layers, normals, cliffs, height trace, vision and baked lightmap; optional Single dynamic GI uses the first free texture index 21 and U6, via the common engine's genre-independent shader sampler. Low quality on high-end hardware still has its original look and does not opportunistically turn on GI.

A strict GLES3/WebGL2 or Vulkan/GL driver with fragment slots 16–20 now selects **Low/Compact**, avoiding an invalid resource declaration >16 in the original rich terrain shader. The Low shader intentionally has **no splat/cliff/details material textures**; these are not useful on weak GPUs and their extra declarations are omitted. It remaps only free resource slots and retains baked terrain lighting, sun/point lights, traced terrain shadows and vision fog. With a CPU-probed RGBA32F atlas and the selected GI variant, texture slot 7 is free, and `dynamicGiAt(v_wpos,n)` is multiplied by the terrain's same linear diffuse base *once*. The engine's portable GI transport remains independent of any RTS code. The special GPU compute path is never required for GLES3.

`GiCustomShaderPolicy::giCustomModeFitsAt` ensures the chosen atlas texture slot fits the GPU's fragment budget, including the new GI-at-tex7 compact path. The draw callback checks runtime atlas readiness after queueing, then picks the GI or non-GI variant. The shared GI CPU runtime and standard `Renderer3D` overlay lifetimes/invalidations remain unchanged.

Potentially differing GLES graphics semantics: low shader's fragment texture declarations from baked/trace helpers must be confirmed by a REAL GLES compiler after preprocessing. Source-contract tests don't prove that normal shadow/light/baked/trace/vision and dynamic GI shaders link and render correctly on a physical mobile GPU. Strict GLSL 3.00 may also have vertex-varying and highp precision limits; test WebGL2 explicitly. If certain GLES mobile devices cannot sample RGBA32F, `GiDynamicProbeTexture::initialize` must refuse and gracefully omit dynamic GI, keeping compact normal lighting.

## TODO / next precise action

1. Build/compile `aiverse_gi_terrain_compact_test`, `aiverse_gi_terrain_receiver_test` and AIVERSE client / renderer; execute corresponding CTests after feature completion. Record exact tested SHA and PASS/FAIL evidence, not assumed test successes.
2. Run Emscripten WebGL2/GLES3 GLSL shader compilation for low compact, low compact + sampled tex7 GI, medium/high rich, including fog, baked terrain, traced shadows, fresh/non-fresh GI atlases. Capture actual GPU pixels on Android and/or WebGL2, compare against original terrain and CPU reference for lighting parity.
3. Reconcile current main to obtain live TerraWeaveTerrainPatchRender/TerraWeaveGrassRender; their `ModelDraw` path is already addressed by Stage 46's material-palette policy but requires native tests. Do not manually copy files from a divergent branch.
4. GPU GI still needs a bounded large-world BVH streaming policy, actual Vulkan/Metal GPU pixel tests, AIVERSE full build and one final acceptance suite before main merge or default activation.
