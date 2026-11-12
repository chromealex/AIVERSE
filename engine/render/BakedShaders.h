// Shader snippets of baked lighting (agent-lightbake), GLSL ES 3.00 + MSL.
//
// GI block (16 floats, std140):  gi[0] probe grid origin xyz, w 1 = probes present
//                                gi[1] 1 / probe cell xyz, w = lightmap scale (RGBM range * intensity)
//                                gi[2] probe dims xyz, w = mode (1 indirect, 2 shadowmask, 3 subtractive)
//                                gi[3] subtractive shadow colour rgb (linear), w = probe texture width (texels)
// Probe texture (RGBA32F, nearest): 3 texels per probe i = x + nx * (y + ny * z), at 3 i .. 3 i + 2:
//   (a.r, a.g, a.b, bx.r) (bx.g, bx.b, by.r, by.g) (by.b, bz.r, bz.g, bz.b); irradiance = a + B n.
// AIVERSE_GIQ: 1 = low tier (nearest probe), 2 = high tier (trilinear probes).
//
// Terrain hook (custom lit renderers): AIVERSE_GLSL_BAKED_TERRAIN(block, lmTex, maskTex) declares
// `bool bakedTerrain(vec3 wpos, vec3 amb, inout float sh, out vec3 light)` and the uniform block
//   bt[0] world xz -> page uv: uv = xz * bt[0].xy + bt[0].zw;  bt[1] x 1 = on, y lightmap scale, z mode;
//   bt[2] subtractive shadow colour. Bind with Renderer3D::bindBakedTerrain(block, lmSlot, maskSlot).
// Use: `vec3 bl; if (bakedTerrain(wpos, amb, sh, bl)) amb = bl;` before the colour sum; subtractive mode
// returns the full light in `bl` and sets sh = 0 (the sun is already in the lightmap).
#pragma once

#define AIVERSE_GLSL_GI_BLOCK "layout(std140) uniform U4 { vec4 u_gi[4]; };\n"
#define AIVERSE_MSL_GI_BLOCK "struct MU4G { float4 gi[4]; };\n"

// Light probes in a vertex shader (tex8): ambient a and gradient rows of the trilinear (or nearest) probe mix.
#define AIVERSE_GLSL_PROBES \
    "uniform highp sampler2D tex8;\n" \
    "vec4 probeTexel(int i) { int w = int(u_gi[3].w); return texelFetch(tex8, ivec2(i % w, i / w), 0); }\n" \
    "void probeAdd(ivec3 c, float wt, inout vec3 a, inout vec3 bx, inout vec3 by, inout vec3 bz) {\n" \
    "    ivec3 d = ivec3(u_gi[2].xyz);\n" \
    "    c = clamp(c, ivec3(0), d - 1);\n" \
    "    int i = (c.x + d.x * (c.y + d.y * c.z)) * 3;\n" \
    "    vec4 t0 = probeTexel(i), t1 = probeTexel(i + 1), t2 = probeTexel(i + 2);\n" \
    "    a += t0.xyz * wt; bx += vec3(t0.w, t1.x, t1.y) * wt; by += vec3(t1.z, t1.w, t2.x) * wt; bz += t2.yzw * wt;\n" \
    "}\n" \
    "void probeAt(vec3 wp, out vec3 a, out vec3 bx, out vec3 by, out vec3 bz) {\n" \
    "    a = vec3(0.0); bx = vec3(0.0); by = vec3(0.0); bz = vec3(0.0);\n" \
    "    vec3 f = clamp((wp - u_gi[0].xyz) * u_gi[1].xyz, vec3(0.0), u_gi[2].xyz - 1.0);\n" \
    "#if AIVERSE_GIQ >= 2\n" \
    "    vec3 b = min(floor(f), max(u_gi[2].xyz - 2.0, vec3(0.0))), t = f - b;\n" \
    "    ivec3 c = ivec3(b);\n" \
    "    for (int k = 0; k < 8; ++k) {\n" \
    "        ivec3 o = ivec3(k & 1, (k >> 1) & 1, (k >> 2) & 1);\n" \
    "        vec3 w3 = mix(1.0 - t, t, vec3(o));\n" \
    "        probeAdd(c + o, w3.x * w3.y * w3.z, a, bx, by, bz);\n" \
    "    }\n" \
    "#else\n" \
    "    probeAdd(ivec3(f + 0.5), 1.0, a, bx, by, bz);\n" \
    "#endif\n" \
    "}\n"

#define AIVERSE_MSL_PROBES \
    "static float4 probeTexel(constant MU4G& g, texture2d<float> pt, int i) { int w = int(g.gi[3].w); return pt.read(uint2(uint(i % w), uint(i / w))); }\n" \
    "static void probeAdd(constant MU4G& g, texture2d<float> pt, int3 c, float wt, thread float3& a, thread float3& bx, thread float3& by, thread float3& bz) {\n" \
    "    int3 d = int3(g.gi[2].xyz);\n" \
    "    c = clamp(c, int3(0), d - 1);\n" \
    "    int i = (c.x + d.x * (c.y + d.y * c.z)) * 3;\n" \
    "    float4 t0 = probeTexel(g, pt, i), t1 = probeTexel(g, pt, i + 1), t2 = probeTexel(g, pt, i + 2);\n" \
    "    a += t0.xyz * wt; bx += float3(t0.w, t1.x, t1.y) * wt; by += float3(t1.z, t1.w, t2.x) * wt; bz += t2.yzw * wt;\n" \
    "}\n" \
    "static void probeAt(constant MU4G& g, texture2d<float> pt, float3 wp, thread float3& a, thread float3& bx, thread float3& by, thread float3& bz) {\n" \
    "    a = float3(0.0); bx = float3(0.0); by = float3(0.0); bz = float3(0.0);\n" \
    "    float3 f = clamp((wp - g.gi[0].xyz) * g.gi[1].xyz, float3(0.0), g.gi[2].xyz - 1.0);\n" \
    "#if AIVERSE_GIQ >= 2\n" \
    "    float3 b = min(floor(f), max(g.gi[2].xyz - 2.0, float3(0.0))), t = f - b;\n" \
    "    int3 c = int3(b);\n" \
    "    for (int k = 0; k < 8; ++k) {\n" \
    "        int3 o = int3(k & 1, (k >> 1) & 1, (k >> 2) & 1);\n" \
    "        float3 w3 = mix(1.0 - t, t, float3(o));\n" \
    "        probeAdd(g, pt, c + o, w3.x * w3.y * w3.z, a, bx, by, bz);\n" \
    "    }\n" \
    "#else\n" \
    "    probeAdd(g, pt, int3(f + 0.5), 1.0, a, bx, by, bz);\n" \
    "#endif\n" \
    "}\n"

#define AIVERSE_GLSL_BAKED_TERRAIN(BLOCK, LMTEX, MASKTEX) \
    "layout(std140) uniform " BLOCK " { vec4 u_bt[3]; };\n" \
    "uniform mediump sampler2D " LMTEX ";\n" \
    "uniform mediump sampler2D " MASKTEX ";\n" \
    "bool bakedTerrain(vec3 wp, vec3 sunNdl, inout float sh, out vec3 light) {\n" \
    "    light = vec3(0.0);\n" \
    "    if (u_bt[1].x < 0.5) return false;\n" \
    "    vec2 uv = wp.xz * u_bt[0].xy + u_bt[0].zw;\n" \
    "    vec4 s = texture(" LMTEX ", uv);\n" \
    "    light = s.rgb * s.a * u_bt[1].y;\n" \
    "    int mode = int(u_bt[1].z + 0.5);\n" \
    "    if (mode == 2) sh = min(sh, texture(" MASKTEX ", uv).r);\n" \
    "    if (mode == 3) { light = max(light - sunNdl * (1.0 - sh), min(light, u_bt[2].rgb)); sh = 0.0; }\n" \
    "    return true;\n" \
    "}\n"

#define AIVERSE_MSL_BAKED_TERRAIN \
    "struct BakedTerrainU { float4 bt[3]; };\n" \
    "static bool bakedTerrain(constant BakedTerrainU& b, texture2d<float> lm, sampler ls, texture2d<float> mk, sampler ms,\n" \
    "                         float3 wp, float3 sunNdl, thread float& sh, thread float3& light) {\n" \
    "    light = float3(0.0);\n" \
    "    if (b.bt[1].x < 0.5) return false;\n" \
    "    float2 uv = wp.xz * b.bt[0].xy + b.bt[0].zw;\n" \
    "    float4 s = lm.sample(ls, uv);\n" \
    "    light = s.rgb * s.a * b.bt[1].y;\n" \
    "    int mode = int(b.bt[1].z + 0.5);\n" \
    "    if (mode == 2) sh = min(sh, mk.sample(ms, uv).r);\n" \
    "    if (mode == 3) { light = max(light - sunNdl * (1.0 - sh), min(light, b.bt[2].rgb)); sh = 0.0; }\n" \
    "    return true;\n" \
    "}\n"
