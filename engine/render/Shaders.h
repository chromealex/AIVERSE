// Built-in shaders, written once per shading language. GLSL bodies are GLSL ES 3.00 without the
// #version line (the GL backend prepends "#version 300 es" or "#version 330 core").
//
// Lit pipeline (single pass, forward, mobile friendly):
//   sun (Lambert + Blinn specular) x contact-hardening shadow map
//   + hemisphere ambient (sky/ground) + rim + distance fog + ACES tonemap, lighting in linear space.
#pragma once
#include "LightShaders.h"
#include "NereidWaveShaders.h"   // NEREID wave model (planes and spheres)   // AIVERSE_GLSL_LIGHTS / AIVERSE_MSL_LIGHTS: dynamic point lights (U2, tex1)

namespace aiverse::render::shaders {

// Uniform blocks shared by the lit and shadow shaders (std140 / MSL layouts match):
//   U0 (per draw):  mat4 model; vec4 color; vec4 params(x unlit, y specular, z receiveShadow, w emissive)
//   U1 (per frame): mat4 viewProj; mat4 lightViewProj; mat4 invViewProj; vec4 sunDir(xyz, w intensity); vec4 sunColor;
//                   vec4 skyColor; vec4 groundColor; vec4 fog(rgb, density); vec4 camPos(xyz, presentation time);
//                   vec4 shadow(x uvFlipY, y depthScale, z depthOffset, w texel); vec4 quality(x taps, y 0=off/1=fixed/2=contact-hardening, z max radius uv, w normal offset)
//                   Traced lighting (AIVERSE_*_TRACE, Renderer3D "trace" tier):
//                   vec4 trace0(x,y height-field origin xz, z,w 1 / extent xz)
//                   vec4 trace1(x height min, y height range (<= 0: terrain shadows off), z march distance, w penumbra tan)
//                   vec4 trace2(xyz camera forward, w screen-space reflections on (scene colour + depth bound))
//                   vec4 trace3(x SSR distance, y thickness (world), z steps, w ray bias)
#define AIVERSE_GLSL_FRAME \
    "layout(std140) uniform U1 { mat4 u_viewProj; mat4 u_lightViewProj; mat4 u_invViewProj; vec4 u_sunDir; vec4 u_sunColor;\n" \
    "  vec4 u_skyColor; vec4 u_groundColor; vec4 u_fog; vec4 u_camPos; vec4 u_shadow; vec4 u_quality;\n" \
    "  vec4 u_trace0; vec4 u_trace1; vec4 u_trace2; vec4 u_trace3; };\n"
#define AIVERSE_GLSL_BLOCKS \
    "layout(std140) uniform U0 { mat4 u_model; vec4 u_color; vec4 u_params; vec4 u_waterRipples[16]; vec4 u_waterWakeShapes[16]; vec4 u_waterWind; vec4 u_waterHydraulics; vec4 u_waterRapidColor; vec4 u_waterFlowRect; };\n" AIVERSE_GLSL_FRAME

#define AIVERSE_MSL_FRAME \
    "struct U1 { float4x4 viewProj; float4x4 lightViewProj; float4x4 invViewProj; float4 sunDir; float4 sunColor;\n" \
    "  float4 skyColor; float4 groundColor; float4 fog; float4 camPos; float4 shadow; float4 quality;\n" \
    "  float4 trace0; float4 trace1; float4 trace2; float4 trace3; };\n"
#define AIVERSE_MSL_BLOCKS \
    "struct U0 { float4x4 model; float4 color; float4 params; float4 waterRipples[16]; float4 waterWakeShapes[16]; float4 waterWind; float4 waterHydraulics; float4 waterRapidColor; float4 waterFlowRect; };\n" AIVERSE_MSL_FRAME

// Traced terrain shadows (Renderer3D "trace" tier, needs U1 and the height field in slot TEX):
// the sun ray is marched over a 16-bit height map of the whole map (R high byte, G low byte; built by
// the terrain renderer from the drawn surface), so cliffs and mountains far outside the shadow map
// still cast long, soft shadows at dawn and dusk. Penumbra grows with the distance to the occluder
// (horizon-angle test, u_trace1.w), the shadow-map term stays for objects. Steps grow geometrically:
// fine near the receiver, coarse far away. Returns 1 when the height field is not bound.
#define AIVERSE_GLSL_TRACE_AT(TEX) \
    "uniform highp sampler2D " TEX ";\n" \
    "float traceHeight(vec2 xz) {\n" \
    "    vec2 uv = (xz - u_trace0.xy) * u_trace0.zw;\n" \
    "    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return u_trace1.x - 1.0e4;   /* beyond the map: nothing casts */\n" \
    "    vec4 t = texture(" TEX ", uv);\n" \
    "    return u_trace1.x + u_trace1.y * (t.r * 65280.0 + t.g * 255.0) / 65535.0;\n" \
    "}\n" \
    "float terrainShadow(vec3 wpos) {\n" \
    "    if (u_trace1.y <= 0.0) return 1.0;\n" \
    "    vec2 ruv = (wpos.xz - u_trace0.xy) * u_trace0.zw;   /* receivers beyond the map (apron) stay lit */\n" \
    "    if (ruv.x < 0.0 || ruv.y < 0.0 || ruv.x > 1.0 || ruv.y > 1.0) return 1.0;\n" \
    "    vec3 L = normalize(u_sunDir.xyz);\n" \
    "    float horiz = length(L.xz);\n" \
    "    if (L.y <= 0.02 || horiz < 0.05) return 1.0;\n" \
    "    vec2 dir = L.xz / horiz;\n" \
    "    float slope = L.y / horiz;\n" \
    "    float occ = 0.0, d = 0.6, growth = pow(u_trace1.z / 0.6, 1.0 / 20.0);\n" \
    "    float bias = u_trace3.w;\n" \
    "    for (int i = 0; i < 20; ++i) {\n" \
    "        float h = traceHeight(wpos.xz + dir * d);\n" \
    "        float rayH = wpos.y + bias + slope * d;\n" \
    "        occ = max(occ, (h - rayH) / (d * u_trace1.w));\n" \
    "        if (occ >= 1.0) break;\n" \
    "        d *= growth;\n" \
    "    }\n" \
    "    return 1.0 - clamp(occ, 0.0, 1.0);\n" \
    "}\n"
#define AIVERSE_GLSL_TRACE_NONE "float terrainShadow(vec3 wpos) { return 1.0; }\n"

#define AIVERSE_MSL_TRACE \
    "static float traceHeight(constant U1& f, texture2d<float> hf, sampler hs, float2 xz) {\n" \
    "    float2 uv = (xz - f.trace0.xy) * f.trace0.zw;\n" \
    "    if (uv.x < 0.0 || uv.y < 0.0 || uv.x > 1.0 || uv.y > 1.0) return f.trace1.x - 1.0e4;\n" \
    "    float4 t = hf.sample(hs, uv);\n" \
    "    return f.trace1.x + f.trace1.y * (t.r * 65280.0 + t.g * 255.0) / 65535.0;\n" \
    "}\n" \
    "static float terrainShadow(constant U1& f, texture2d<float> hf, sampler hs, float3 wpos) {\n" \
    "    if (f.trace1.y <= 0.0) return 1.0;\n" \
    "    float2 ruv = (wpos.xz - f.trace0.xy) * f.trace0.zw;\n" \
    "    if (ruv.x < 0.0 || ruv.y < 0.0 || ruv.x > 1.0 || ruv.y > 1.0) return 1.0;\n" \
    "    float3 L = normalize(f.sunDir.xyz);\n" \
    "    float horiz = length(L.xz);\n" \
    "    if (L.y <= 0.02 || horiz < 0.05) return 1.0;\n" \
    "    float2 dir = L.xz / horiz;\n" \
    "    float slope = L.y / horiz;\n" \
    "    float occ = 0.0, d = 0.6, growth = pow(f.trace1.z / 0.6, 1.0 / 20.0);\n" \
    "    float bias = f.trace3.w;\n" \
    "    for (int i = 0; i < 20; ++i) {\n" \
    "        float h = traceHeight(f, hf, hs, wpos.xz + dir * d);\n" \
    "        float rayH = wpos.y + bias + slope * d;\n" \
    "        occ = max(occ, (h - rayH) / (d * f.trace1.w));\n" \
    "        if (occ >= 1.0) break;\n" \
    "        d *= growth;\n" \
    "    }\n" \
    "    return 1.0 - clamp(occ, 0.0, 1.0);\n" \
    "}\n"

// Screen-space reflections on water (Renderer3D "trace" tier): the reflected view ray is marched in
// world space and tested against the depth of the opaque scene (the main pass is split: opaque into
// a colour + depth target, a depth-restoring blit, then water and the rest on top). A hit replaces the
// planar reflection at that pixel (banks, cliffs and terrain that the mirrored pass skips); misses
// keep the planar result. Needs the scene colour in COLOR and the scene depth in DEPTH.
#define AIVERSE_GLSL_SSR_AT(COLOR, DEPTH) \
    "uniform mediump sampler2D " COLOR ";\n" \
    "uniform highp sampler2D " DEPTH ";\n" \
    "vec3 ssrUnproject(vec2 uv, float d01) {\n" \
    "    float z = (d01 - u_shadow.z) / u_shadow.y;\n" \
    "    vec4 w = u_invViewProj * vec4(uv * 2.0 - 1.0, z, 1.0);\n" \
    "    return w.xyz / w.w;\n" \
    "}\n" \
    "// returns the reflected colour and writes the hit weight (0 = keep what you had)\n" \
    "vec3 ssrWater(vec3 wpos, vec3 n, vec3 V, out float hit) {\n" \
    "    hit = 0.0;\n" \
    "    if (u_trace2.w < 0.5) return vec3(0.0);\n" \
    "    vec3 R = reflect(-V, n);\n" \
    "#ifndef AIVERSE_MODEL_SSR\n    if (R.y <= 0.01) return vec3(0.0);\n#endif\n" \
    "    vec3 fwd = u_trace2.xyz;\n" \
    "    int steps = int(u_trace3.z);\n" \
    "    float d = 0.0, stepLen = u_trace3.x / float(steps) * 0.35, growth = 1.12;\n" \
    "    float prevD = 0.0;\n" \
    "    vec2 uv = vec2(0.0);\n" \
    "    for (int i = 0; i < 24; ++i) {\n" \
    "        if (i >= steps) break;\n" \
    "        prevD = d; d += stepLen; stepLen *= growth;\n" \
    "        vec3 p = wpos + R * d;\n" \
    "        vec4 cp = u_viewProj * vec4(p, 1.0);\n" \
    "        if (cp.w <= 0.0) break;\n" \
    "        vec3 ndc = cp.xyz / cp.w;\n" \
    "        uv = ndc.xy * 0.5 + 0.5;\n" \
    "        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;\n" \
    "        vec2 tuv = u_shadow.x > 0.5 ? vec2(uv.x, 1.0 - uv.y) : uv;\n" \
    "        float sd = texture(" DEPTH ", tuv).r;\n" \
    "        if (sd >= 0.9999) continue;\n" \
    "        vec3 s = ssrUnproject(uv, sd);\n" \
    "#ifndef AIVERSE_MODEL_SSR\n        if (s.y <= wpos.y + 0.02) continue;\n#endif\n" \
    "        float dp = dot(p - u_camPos.xyz, fwd), ds = dot(s - u_camPos.xyz, fwd);\n" \
    "        if (ds < dp) {\n" \
    "            if (dp - ds > u_trace3.y + (d - prevD)) break;   /* the ray passed behind something thin: no hit */\n" \
    "            // refine between the last miss and this hit\n" \
    "            float lo = prevD, hi = d;\n" \
    "            for (int k = 0; k < 4; ++k) {\n" \
    "                float m = (lo + hi) * 0.5;\n" \
    "                vec3 q = wpos + R * m;\n" \
    "                vec4 cq = u_viewProj * vec4(q, 1.0);\n" \
    "                vec2 quv = cq.xy / cq.w * 0.5 + 0.5;\n" \
    "                vec2 qt = u_shadow.x > 0.5 ? vec2(quv.x, 1.0 - quv.y) : quv;\n" \
    "                float qd = texture(" DEPTH ", qt).r;\n" \
    "                vec3 qs = ssrUnproject(quv, qd);\n" \
    "                if (dot(qs - u_camPos.xyz, fwd) < dot(q - u_camPos.xyz, fwd)) { hi = m; uv = quv; } else lo = m;\n" \
    "            }\n" \
    "            vec2 e = min(uv, 1.0 - uv);\n" \
    "            float edge = smoothstep(0.0, 0.12, min(e.x, e.y));\n" \
    "            float range = 1.0 - smoothstep(0.6, 1.0, d / u_trace3.x);\n" \
    "            hit = edge * range;\n" \
    "            vec2 cuv = u_shadow.x > 0.5 ? vec2(uv.x, 1.0 - uv.y) : uv;\n" \
    "            return texture(" COLOR ", cuv).rgb;\n" \
    "        }\n" \
    "    }\n" \
    "    return vec3(0.0);\n" \
    "}\n"

#define AIVERSE_MSL_SSR \
    "static float3 ssrUnproject(constant U1& f, float2 uv, float d01) {\n" \
    "    float z = (d01 - f.shadow.z) / f.shadow.y;\n" \
    "    float4 w = f.invViewProj * float4(uv * 2.0 - 1.0, z, 1.0);\n" \
    "    return w.xyz / w.w;\n" \
    "}\n" \
    "static float3 ssrWater(constant U1& f, texture2d<float> sc, sampler ss, depth2d<float> sdp, sampler sds, float3 wpos, float3 n, float3 V, thread float& hit) {\n" \
    "    hit = 0.0;\n" \
    "    if (f.trace2.w < 0.5) return float3(0.0);\n" \
    "    float3 R = reflect(-V, n);\n" \
    "#ifndef AIVERSE_MODEL_SSR\n    if (R.y <= 0.01) return float3(0.0);\n#endif\n" \
    "    float3 fwd = f.trace2.xyz;\n" \
    "    int steps = int(f.trace3.z);\n" \
    "    float d = 0.0, stepLen = f.trace3.x / float(steps) * 0.35, growth = 1.12;\n" \
    "    float prevD = 0.0;\n" \
    "    float2 uv = float2(0.0);\n" \
    "    for (int i = 0; i < 24; ++i) {\n" \
    "        if (i >= steps) break;\n" \
    "        prevD = d; d += stepLen; stepLen *= growth;\n" \
    "        float3 p = wpos + R * d;\n" \
    "        float4 cp = f.viewProj * float4(p, 1.0);\n" \
    "        if (cp.w <= 0.0) break;\n" \
    "        float3 ndc = cp.xyz / cp.w;\n" \
    "        uv = ndc.xy * 0.5 + 0.5;\n" \
    "        if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) break;\n" \
    "        float2 tuv = f.shadow.x > 0.5 ? float2(uv.x, 1.0 - uv.y) : uv;\n" \
    "        float sd = sdp.sample(sds, tuv);\n" \
    "        if (sd >= 0.9999) continue;\n" \
    "        float3 s = ssrUnproject(f, uv, sd);\n" \
    "#ifndef AIVERSE_MODEL_SSR\n        if (s.y <= wpos.y + 0.02) continue;\n#endif\n" \
    "        float dp = dot(p - f.camPos.xyz, fwd), ds = dot(s - f.camPos.xyz, fwd);\n" \
    "        if (ds < dp) {\n" \
    "            if (dp - ds > f.trace3.y + (d - prevD)) break;\n" \
    "            float lo = prevD, hi = d;\n" \
    "            for (int k = 0; k < 4; ++k) {\n" \
    "                float m = (lo + hi) * 0.5;\n" \
    "                float3 q = wpos + R * m;\n" \
    "                float4 cq = f.viewProj * float4(q, 1.0);\n" \
    "                float2 quv = cq.xy / cq.w * 0.5 + 0.5;\n" \
    "                float2 qt = f.shadow.x > 0.5 ? float2(quv.x, 1.0 - quv.y) : quv;\n" \
    "                float qd = sdp.sample(sds, qt);\n" \
    "                float3 qs = ssrUnproject(f, quv, qd);\n" \
    "                if (dot(qs - f.camPos.xyz, fwd) < dot(q - f.camPos.xyz, fwd)) { hi = m; uv = quv; } else lo = m;\n" \
    "            }\n" \
    "            float2 e = min(uv, 1.0 - uv);\n" \
    "            float edge = smoothstep(0.0, 0.12, min(e.x, e.y));\n" \
    "            float range = 1.0 - smoothstep(0.6, 1.0, d / f.trace3.x);\n" \
    "            hit = edge * range;\n" \
    "            float2 cuv = f.shadow.x > 0.5 ? float2(uv.x, 1.0 - uv.y) : uv;\n" \
    "            return sc.sample(ss, cuv).rgb;\n" \
    "        }\n" \
    "    }\n" \
    "    return float3(0.0);\n" \
    "}\n"


// Sun shadow lookup shared by every lit shader (raw depth map in slot 0).
// Raw depth is shared by blocker search and bilinear PCF. No extra texture binding is needed.
#define AIVERSE_GLSL_SUN_FILTER(TEX) \
    "float sunDepth(vec2 uv) {\n" \
    "    ivec2 size = textureSize(" TEX ", 0);\n" \
    "    return texelFetch(" TEX ", clamp(ivec2(floor(uv * vec2(size))), ivec2(0), size - 1), 0).r;\n" \
    "}\n" \
    "float sunCompare(vec2 uv, float d) {\n" \
    "    vec2 size = vec2(textureSize(" TEX ", 0));\n" \
    "    vec2 p = uv * size - 0.5, f = fract(p), base = (floor(p) + 0.5) / size;\n" \
    "    vec2 t = 1.0 / size;\n" \
    "    return mix(mix(step(d, sunDepth(base)), step(d, sunDepth(base + vec2(t.x, 0.0))), f.x),\n" \
    "               mix(step(d, sunDepth(base + vec2(0.0, t.y))), step(d, sunDepth(base + t)), f.x), f.y);\n" \
    "}\n"

#define AIVERSE_MSL_SUN_FILTER \
    "static float sunDepth(depth2d<float> sm, float2 uv) {\n" \
    "    int2 size = int2(sm.get_width(), sm.get_height());\n" \
    "    return sm.read(uint2(clamp(int2(floor(uv * float2(size))), int2(0), size - 1)));\n" \
    "}\n" \
    "static float sunCompare(depth2d<float> sm, float2 uv, float d) {\n" \
    "    constexpr sampler compareSampler(coord::normalized, address::clamp_to_edge, filter::linear, compare_func::less_equal);\n" \
    "    return sm.sample_compare(compareSampler, uv, d);\n" \
    "}\n"

// AIVERSE_HW_SHADOW (GL prelude, weak mobile GPUs): tex0 is the shadow map through a compare sampler, one
// hardware 2x2 PCF fetch per tap instead of 4 texelFetch + textureSize. No raw depth: sunDepth is a stub
// (the blocker search and the nearest-fetch path are not used with it).
#define AIVERSE_GLSL_SHADOW \
    "#ifdef AIVERSE_HW_SHADOW\n" \
    "uniform highp sampler2DShadow tex0;\n" \
    "float sunDepth(vec2 uv) { return 1.0; }\n" \
    "float sunCompare(vec2 uv, float d) { return texture(tex0, vec3(uv, d)); }\n" \
    "#else\n" \
    "uniform highp sampler2D tex0;\n" \
    AIVERSE_GLSL_SUN_FILTER("tex0") \
    "#endif\n" \
    "// Soft shadows: rotated Poisson-disc PCF over bilinear 2x2 compare taps.\n" \
    "//   u_quality.x taps (1 / 4 / 8 / 16), u_quality.z filter radius in shadow UV, u_quality.w normal offset (world)\n" \
    "const vec2 kDisk[24] = vec2[24](\n" \
    "    vec2(-0.613, 0.617), vec2(0.170, -0.040), vec2(-0.299, 0.791), vec2(0.645, 0.493),\n" \
    "    vec2(-0.651, 0.717), vec2(0.421, 0.027), vec2(-0.817, -0.271), vec2(-0.705, -0.668),\n" \
    "    vec2(0.977, -0.108), vec2(0.063, -0.233), vec2(0.346, -0.894), vec2(-0.272, 0.416),\n" \
    "    vec2(-0.025, 0.919), vec2(-0.389, -0.144), vec2(0.733, -0.627), vec2(0.282, 0.512),\n" \
    "    vec2(-0.900, 0.160), vec2(0.880, 0.250), vec2(-0.480, -0.860), vec2(0.490, 0.860),\n" \
    "    vec2(-0.080, -0.980), vec2(0.970, -0.360), vec2(-0.960, -0.360), vec2(0.410, -0.730));\n" \
    "// Importance-sampled radial tent: CDF(r) = 2*r*r - r*r*r*r.\n" \
    "// Equal contributions avoid the large central weights of a sparse weighted disk.\n" \
    "const vec2 kStableDisk[128] = vec2[128](\n" \
    "    vec2(0.04421579, 0.00000000), vec2(-0.05652615, 0.05178259), vec2(0.00866079, -0.09868521), vec2(0.07138893, 0.09311421),\n" \
    "    vec2(-0.13113854, -0.02319656), vec2(0.12435092, -0.07910188), vec2(-0.04163508, 0.15488041), vec2(-0.07948352, -0.15304068),\n" \
    "    vec2(0.17262428, 0.06304211), vec2(-0.17977209, 0.07420731), vec2(0.08675201, -0.18538401), vec2(0.06417452, 0.20459848),\n" \
    "    vec2(-0.19362615, -0.11221031), vec2(0.22738593, -0.04999016), vec2(-0.13891793, 0.19759646), vec2(-0.03212767, -0.24792703),\n" \
    "    vec2(0.19744529, 0.16640710), vec2(-0.26598819, 0.01099945), vec2(0.19423021, -0.19328500), vec2(-0.01300907, 0.28133303),\n" \
    "    vec2(-0.18521937, -0.22195471), vec2(0.29373554, 0.03952170), vec2(-0.24916135, 0.17335998), vec2(0.06816230, -0.30298825),\n" \
    "    vec2(0.15783562, 0.27544420), vec2(-0.30890787, -0.09855004), vec2(0.30041176, -0.13879785), vec2(-0.13029751, 0.31133925),\n" \
    "    vec2(-0.11642414, -0.32368877), vec2(0.31015843, 0.16301058), vec2(-0.34491835, 0.09091937), vec2(0.19628927, -0.30527491),\n" \
    "    vec2(0.06251629, 0.36376439), vec2(-0.29663340, -0.22972958), vec2(0.37991403, -0.03147510), vec2(-0.26292571, 0.28421499),\n" \
    "    vec2(0.00191758, -0.39308014), vec2(0.26803797, 0.29547274), vec2(-0.40300366, -0.03735029), vec2(0.32696954, -0.24815822),\n" \
    "    vec2(-0.07448873, 0.40945591), vec2(-0.22466921, -0.35702184), vec2(0.41224071, 0.11297809), vec2(-0.38524547, 0.19770161),\n" \
    "    vec2(0.15244573, -0.41119618), vec2(0.16742263, 0.41126953), vec2(-0.40619649, -0.19250410), vec2(0.43473964, -0.13403488),\n" \
    "    vec2(-0.23275384, 0.39715316), vec2(-0.09777499, -0.45532109), vec2(0.38401622, 0.27278713), vec2(-0.47270189, 0.05891193),\n" \
    "    vec2(0.31219111, -0.36677493), vec2(0.01774490, 0.48659572), vec2(-0.34545833, -0.35055144), vec2(0.49674464, 0.02539889),\n" \
    "    vec2(-0.38745589, 0.32013535), vec2(0.07016688, -0.50292169), vec2(0.29091459, 0.42249801), vec2(-0.50493321, -0.11618395),\n" \
    "    vec2(0.45528077, -0.25794388), vec2(-0.16305549, 0.50262088), vec2(-0.22140934, -0.48542014), vec2(0.49586355, 0.21037063),\n" \
    "    vec2(-0.51254863, 0.18153421), vec2(0.25770567, -0.48457873), vec2(0.13857736, 0.53631873), vec2(-0.46872378, -0.30462768),\n" \
    "    vec2(0.55640617, -0.09283140), vec2(-0.35069810, 0.44829675), vec2(-0.04462059, -0.57251303), vec2(0.42333693, 0.39547658),\n" \
    "    vec2(-0.58437071, -0.00570165), vec2(0.43852474, -0.39392499), vec2(-0.05775540, 0.59174258), vec2(-0.36018279, -0.47940998),\n" \
    "    vec2(0.59442644, 0.11113724), vec2(-0.51770935, 0.32227287), vec2(0.16542339, -0.59225666), vec2(0.28039750, 0.55301323),\n" \
    "    vec2(-0.58510609, -0.22017315), vec2(0.58492904, -0.23479746), vec2(-0.27493246, 0.57288755), vec2(-0.18575039, -0.61308480),\n" \
    "    vec2(0.55555504, 0.32923761), vec2(-0.63713251, 0.13356885), vec2(0.38261911, -0.53310460), vec2(0.07859802, 0.65675136),\n" \
    "    vec2(-0.50557475, -0.43460559), vec2(0.67165075, -0.02121308), vec2(-0.48472787, 0.47304652), vec2(0.03818375, -0.68157301),\n" \
    "    vec2(0.43564318, 0.53252292), vec2(-0.68629587, -0.09916644), vec2(0.57753795, -0.39352937), vec2(-0.16128873, 0.68563462),\n" \
    "    vec2(-0.34690998, -0.61933440), vec2(0.67944389, 0.22408792), vec2(-0.65749190, 0.29602837), vec2(0.28708911, -0.66761904),\n" \
    "    vec2(0.24116424, 0.69161218), vec2(-0.65009720, -0.34980961), vec2(0.72132289, -0.18263082), vec2(-0.41176380, 0.62685770),\n" \
    "    vec2(-0.12077147, -0.74628134), vec2(0.59792208, 0.47246845), vec2(-0.76617822, 0.05595554), vec2(0.53144862, -0.56335344),\n" \
    "    vec2(-0.01142688, 0.78074135), vec2(-0.52325497, -0.58824449), vec2(0.78973959, 0.08097152), vec2(-0.64241960, 0.47776761),\n" \
    "    vec2(0.15226862, -0.79298739), vec2(0.42706634, 0.69357159), vec2(-0.79035036, -0.22491505), vec2(0.74134729, -0.37135454),\n" \
    "    vec2(-0.29853255, 0.78175370), vec2(-0.31085520, -0.78546678), vec2(0.76719649, 0.37279801), vec2(-0.82576636, 0.24579628),\n" \
    "    vec2(0.44750035, -0.74678107), vec2(0.17638381, 0.86228870), vec2(-0.72078458, -0.52266675), vec2(0.89551311, -0.10274371),\n" \
    "    vec2(-0.59891828, 0.68988219), vec2(-0.02475624, -0.92716389), vec2(0.65623978, 0.67904063), vec2(-0.96645424, -0.05887443));\n" \
    "float shadowAtSurface(vec3 wpos, vec3 n, float diffusion) {\n" \
    "    if (u_quality.y < 0.5) return 1.0;\n" \
    "    vec3 L = normalize(u_sunDir.xyz);\n" \
    "    float k = 1.0 - clamp(dot(n, L), 0.0, 1.0);          // grazing surfaces need the most offset\n" \
    "    // A deforming surface crosses many curved triangle planes inside a wide PCF disk.\n" \
    "    // Bound its normal bias to four texel offsets; rigid planar receivers retain slope correction.\n" \
    "    float offset = u_quality.w;\n" \
    "#if defined(SKINNED) || defined(BAKED)\n" \
    "    offset *= u_quality.y < 1.5 ? clamp(u_quality.z / max(u_shadow.w, 1e-7) * 0.15, 1.0, 4.0) : 1.0;\n" \
    "#endif\n" \
    "    vec4 lp = u_lightViewProj * vec4(wpos + n * offset * (0.35 + k), 1.0);\n" \
    "    vec3 p = lp.xyz / lp.w;\n" \
    "    vec2 uv = p.xy * 0.5 + 0.5;\n" \
    "    if (u_shadow.x > 0.5) uv.y = 1.0 - uv.y;\n" \
    "    float d = p.z * u_shadow.y + u_shadow.z;\n" \
    "    bool outside = uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 || d >= 1.0;\n" \
    "    // single tap (weak mobile GPUs): uniform branches before the derivatives, no receiver-plane gradient\n" \
    "    if (u_quality.x < 0.9) return outside ? 1.0 : step(d, sunDepth(uv));\n" \
    "    if (u_quality.x < 2.0) return outside ? 1.0 : sunCompare(uv, d);\n" \
    "    vec2 dx = dFdx(uv), dy = dFdy(uv);\n" \
    "    float zx = dFdx(d), zy = dFdy(d), det = dx.x * dy.y - dx.y * dy.x;\n" \
    "    vec2 gradient = abs(det) > 1e-10 ? vec2(dy.y * zx - dx.y * zy, dx.x * zy - dy.x * zx) / det : vec2(0.0);\n" \
    "#if defined(SKINNED) || defined(BAKED)\n" \
    "    if (u_quality.y < 1.5) gradient = vec2(0.0);\n" \
    "#endif\n" \
    "    if (outside) return 1.0;\n" \
    "    float r = u_quality.z;\n" \
    "    if (u_quality.x < 5.0) { // weak GPUs: fixed 4 taps, no blocker search\n" \
    "        float s = sunCompare(uv + vec2(-0.5, -0.5) * r, d + dot(gradient, vec2(-0.5, -0.5) * r));\n" \
    "        s += sunCompare(uv + vec2( 0.5, -0.5) * r, d + dot(gradient, vec2( 0.5, -0.5) * r));\n" \
    "        s += sunCompare(uv + vec2(-0.5,  0.5) * r, d + dot(gradient, vec2(-0.5,  0.5) * r));\n" \
    "        s += sunCompare(uv + vec2( 0.5,  0.5) * r, d + dot(gradient, vec2( 0.5,  0.5) * r));\n" \
    "        return s * 0.25;\n" \
    "    }\n" \
    "    if (u_quality.y > 1.5) {\n" \
    "        // PCSS for a directional area light. shadowSoftness remains the maximum world radius.\n" \
    "        // Search the centre as well as the disk so narrow casters are not missed at contact.\n" \
    "        float gap = 0.0, blockers = 0.0;\n" \
    "        for (int i = -1; i < 8; ++i) {\n" \
    "            vec2 o = i < 0 ? vec2(0.0) : kDisk[i] * r;\n" \
    "            float delta = d + dot(gradient, o) - sunDepth(uv + o);\n" \
    "            if (delta > 0.000001) { gap += delta; blockers += 1.0; }\n" \
    "        }\n" \
    "        if (blockers == 0.0 && diffusion <= 0.0) return 1.0;\n" \
    "        float depthPerWorld = length(vec3(u_lightViewProj[0].z, u_lightViewProj[1].z, u_lightViewProj[2].z)) * u_shadow.y;\n" \
    "        float uvPerWorld = 0.5 * length(vec3(u_lightViewProj[0].x, u_lightViewProj[1].x, u_lightViewProj[2].x));\n" \
    "        float penumbra = (gap / max(blockers, 1.0)) / max(depthPerWorld, 1e-6) * 0.05 * uvPerWorld;\n" \
    "        r = clamp(penumbra, min(r, u_shadow.w * 0.5), r);\n" \
    "    }\n" \
    "    // Per-pixel rotation requires temporal resolve; without it the noise remains visible as stipple.\n" \
    "    // Without TAA use a denser tapered disk; small temporal kernels keep a fixed disk.\n" \
    "    // Temporal sampling rotates the PCSS disk between frames only when HDR TAA is active.\n" \
    "    // Receiver-only spread approximates surface fibres; it never changes caster geometry.\n" \
    "    if (diffusion > 0.0) {\n" \
    "        float receiverRadius = clamp(diffusion, 0.0, 1.0) * 0.5 * length(vec3(u_lightViewProj[0].x, u_lightViewProj[1].x, u_lightViewProj[2].x));\n" \
    "        r = sqrt(r * r + receiverRadius * receiverRadius);\n" \
    "    }\n" \
    "    // Sample the smooth radial density directly, with equal per-tap contributions.\n" \
    "    // More samples and a broader taper reduce residual bands without pixel noise.\n" \
    "    // fract 0.75: mobile budget, fixed unrotated disk of int(taps) samples (no 64/128-tap stable disk)\n" \
    "    if (fract(u_quality.x) <= 0.25) {\n" \
    "        int count = u_quality.x > 16.0 ? 128 : 64;\n" \
    "        int stride = count == 128 ? 1 : 2;\n" \
    "        float total = 0.0;\n" \
    "        for (int i = 0; i < 128; ++i) {\n" \
    "            if (i >= count) break;\n" \
    "            vec2 o = kStableDisk[i * stride];\n" \
    "            total += sunCompare(uv + o * r, d + dot(gradient, o * r));\n" \
    "        }\n" \
    "        return total / float(count);\n" \
    "    }\n" \
    "    float temporal = fract(u_quality.x) > 0.25 && fract(u_quality.x) < 0.6 ? floor(u_camPos.w * 60.0) * 0.61803398875 : 0.0;\n" \
    "    float a = fract(u_quality.x) <= 0.25 || fract(u_quality.x) > 0.6 || r < u_shadow.w * 3.2 ? 0.0 : 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))) + temporal);\n" \
    "    vec2 cs = vec2(cos(a), sin(a));\n" \
    "    int n_ = int(u_quality.x);\n" \
    "    float s = 0.0;\n" \
    "    for (int i = 0; i < 24; ++i) {\n" \
    "        if (i >= n_) break;\n" \
    "        vec2 o = kDisk[i];\n" \
    "        o = vec2(o.x * cs.x - o.y * cs.y, o.x * cs.y + o.y * cs.x);\n" \
    "        s += sunCompare(uv + o * r, d + dot(gradient, o * r));\n" \
    "    }\n" \
    "    return s / float(n_);\n" \
    "}\n" \
    "float shadowAt(vec3 wpos, vec3 n) { return shadowAtSurface(wpos, n, 0.0); }\n"

#define AIVERSE_MSL_SHADOW \
    AIVERSE_MSL_SUN_FILTER \
    "constant float2 kDisk[24] = {\n" \
    "    float2(-0.613, 0.617), float2(0.170, -0.040), float2(-0.299, 0.791), float2(0.645, 0.493),\n" \
    "    float2(-0.651, 0.717), float2(0.421, 0.027), float2(-0.817, -0.271), float2(-0.705, -0.668),\n" \
    "    float2(0.977, -0.108), float2(0.063, -0.233), float2(0.346, -0.894), float2(-0.272, 0.416),\n" \
    "    float2(-0.025, 0.919), float2(-0.389, -0.144), float2(0.733, -0.627), float2(0.282, 0.512),\n" \
    "    float2(-0.900, 0.160), float2(0.880, 0.250), float2(-0.480, -0.860), float2(0.490, 0.860),\n" \
    "    float2(-0.080, -0.980), float2(0.970, -0.360), float2(-0.960, -0.360), float2(0.410, -0.730)};\n" \
    "// Importance-sampled radial tent: CDF(r) = 2*r*r - r*r*r*r.\n" \
    "// Equal contributions avoid the large central weights of a sparse weighted disk.\n" \
    "constant float2 kStableDisk[128] = {\n" \
    "    float2(0.04421579, 0.00000000), float2(-0.05652615, 0.05178259), float2(0.00866079, -0.09868521), float2(0.07138893, 0.09311421),\n" \
    "    float2(-0.13113854, -0.02319656), float2(0.12435092, -0.07910188), float2(-0.04163508, 0.15488041), float2(-0.07948352, -0.15304068),\n" \
    "    float2(0.17262428, 0.06304211), float2(-0.17977209, 0.07420731), float2(0.08675201, -0.18538401), float2(0.06417452, 0.20459848),\n" \
    "    float2(-0.19362615, -0.11221031), float2(0.22738593, -0.04999016), float2(-0.13891793, 0.19759646), float2(-0.03212767, -0.24792703),\n" \
    "    float2(0.19744529, 0.16640710), float2(-0.26598819, 0.01099945), float2(0.19423021, -0.19328500), float2(-0.01300907, 0.28133303),\n" \
    "    float2(-0.18521937, -0.22195471), float2(0.29373554, 0.03952170), float2(-0.24916135, 0.17335998), float2(0.06816230, -0.30298825),\n" \
    "    float2(0.15783562, 0.27544420), float2(-0.30890787, -0.09855004), float2(0.30041176, -0.13879785), float2(-0.13029751, 0.31133925),\n" \
    "    float2(-0.11642414, -0.32368877), float2(0.31015843, 0.16301058), float2(-0.34491835, 0.09091937), float2(0.19628927, -0.30527491),\n" \
    "    float2(0.06251629, 0.36376439), float2(-0.29663340, -0.22972958), float2(0.37991403, -0.03147510), float2(-0.26292571, 0.28421499),\n" \
    "    float2(0.00191758, -0.39308014), float2(0.26803797, 0.29547274), float2(-0.40300366, -0.03735029), float2(0.32696954, -0.24815822),\n" \
    "    float2(-0.07448873, 0.40945591), float2(-0.22466921, -0.35702184), float2(0.41224071, 0.11297809), float2(-0.38524547, 0.19770161),\n" \
    "    float2(0.15244573, -0.41119618), float2(0.16742263, 0.41126953), float2(-0.40619649, -0.19250410), float2(0.43473964, -0.13403488),\n" \
    "    float2(-0.23275384, 0.39715316), float2(-0.09777499, -0.45532109), float2(0.38401622, 0.27278713), float2(-0.47270189, 0.05891193),\n" \
    "    float2(0.31219111, -0.36677493), float2(0.01774490, 0.48659572), float2(-0.34545833, -0.35055144), float2(0.49674464, 0.02539889),\n" \
    "    float2(-0.38745589, 0.32013535), float2(0.07016688, -0.50292169), float2(0.29091459, 0.42249801), float2(-0.50493321, -0.11618395),\n" \
    "    float2(0.45528077, -0.25794388), float2(-0.16305549, 0.50262088), float2(-0.22140934, -0.48542014), float2(0.49586355, 0.21037063),\n" \
    "    float2(-0.51254863, 0.18153421), float2(0.25770567, -0.48457873), float2(0.13857736, 0.53631873), float2(-0.46872378, -0.30462768),\n" \
    "    float2(0.55640617, -0.09283140), float2(-0.35069810, 0.44829675), float2(-0.04462059, -0.57251303), float2(0.42333693, 0.39547658),\n" \
    "    float2(-0.58437071, -0.00570165), float2(0.43852474, -0.39392499), float2(-0.05775540, 0.59174258), float2(-0.36018279, -0.47940998),\n" \
    "    float2(0.59442644, 0.11113724), float2(-0.51770935, 0.32227287), float2(0.16542339, -0.59225666), float2(0.28039750, 0.55301323),\n" \
    "    float2(-0.58510609, -0.22017315), float2(0.58492904, -0.23479746), float2(-0.27493246, 0.57288755), float2(-0.18575039, -0.61308480),\n" \
    "    float2(0.55555504, 0.32923761), float2(-0.63713251, 0.13356885), float2(0.38261911, -0.53310460), float2(0.07859802, 0.65675136),\n" \
    "    float2(-0.50557475, -0.43460559), float2(0.67165075, -0.02121308), float2(-0.48472787, 0.47304652), float2(0.03818375, -0.68157301),\n" \
    "    float2(0.43564318, 0.53252292), float2(-0.68629587, -0.09916644), float2(0.57753795, -0.39352937), float2(-0.16128873, 0.68563462),\n" \
    "    float2(-0.34690998, -0.61933440), float2(0.67944389, 0.22408792), float2(-0.65749190, 0.29602837), float2(0.28708911, -0.66761904),\n" \
    "    float2(0.24116424, 0.69161218), float2(-0.65009720, -0.34980961), float2(0.72132289, -0.18263082), float2(-0.41176380, 0.62685770),\n" \
    "    float2(-0.12077147, -0.74628134), float2(0.59792208, 0.47246845), float2(-0.76617822, 0.05595554), float2(0.53144862, -0.56335344),\n" \
    "    float2(-0.01142688, 0.78074135), float2(-0.52325497, -0.58824449), float2(0.78973959, 0.08097152), float2(-0.64241960, 0.47776761),\n" \
    "    float2(0.15226862, -0.79298739), float2(0.42706634, 0.69357159), float2(-0.79035036, -0.22491505), float2(0.74134729, -0.37135454),\n" \
    "    float2(-0.29853255, 0.78175370), float2(-0.31085520, -0.78546678), float2(0.76719649, 0.37279801), float2(-0.82576636, 0.24579628),\n" \
    "    float2(0.44750035, -0.74678107), float2(0.17638381, 0.86228870), float2(-0.72078458, -0.52266675), float2(0.89551311, -0.10274371),\n" \
    "    float2(-0.59891828, 0.68988219), float2(-0.02475624, -0.92716389), float2(0.65623978, 0.67904063), float2(-0.96645424, -0.05887443)};\n" \
    "static float shadowAtSurface(constant U1& f, float3 wpos, float3 n, float2 fragCoord, depth2d<float> sm, sampler ss, float diffusion) {\n" \
    "    if (f.quality.y < 0.5) return 1.0;\n" \
    "    float3 L = normalize(f.sunDir.xyz);\n" \
    "    float k = 1.0 - clamp(dot(n, L), 0.0, 1.0);\n" \
    "    float offset = f.quality.w;\n" \
    "#if defined(SKINNED) || defined(BAKED)\n" \
    "    offset *= f.quality.y < 1.5 ? clamp(f.quality.z / max(f.shadow.w, 1e-7) * 0.15, 1.0, 4.0) : 1.0;\n" \
    "#endif\n" \
    "    float4 lp = f.lightViewProj * float4(wpos + n * offset * (0.35 + k), 1.0);\n" \
    "    float3 p = lp.xyz / lp.w;\n" \
    "    float2 uv = p.xy * 0.5 + 0.5;\n" \
    "    if (f.shadow.x > 0.5) uv.y = 1.0 - uv.y;\n" \
    "    float d = p.z * f.shadow.y + f.shadow.z;\n" \
    "    float2 dx = dfdx(uv), dy = dfdy(uv);\n" \
    "    float zx = dfdx(d), zy = dfdy(d), det = dx.x * dy.y - dx.y * dy.x;\n" \
    "    float2 gradient = abs(det) > 1e-10 ? float2(dy.y * zx - dx.y * zy, dx.x * zy - dy.x * zx) / det : float2(0.0);\n" \
    "#if defined(SKINNED) || defined(BAKED)\n" \
    "    if (f.quality.y < 1.5) gradient = float2(0.0);\n" \
    "#endif\n" \
    "    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 || d >= 1.0) return 1.0;\n" \
    "    float r = f.quality.z;\n" \
    "    if (f.quality.x < 0.9) return step(d, sunDepth(sm, uv));\n" \
    "    if (f.quality.x < 2.0) return sunCompare(sm, uv, d);\n" \
    "    if (f.quality.x < 5.0) {\n" \
    "        float s = sunCompare(sm, uv + float2(-0.5, -0.5) * r, d + dot(gradient, float2(-0.5, -0.5) * r));\n" \
    "        s += sunCompare(sm, uv + float2( 0.5, -0.5) * r, d + dot(gradient, float2( 0.5, -0.5) * r));\n" \
    "        s += sunCompare(sm, uv + float2(-0.5,  0.5) * r, d + dot(gradient, float2(-0.5,  0.5) * r));\n" \
    "        s += sunCompare(sm, uv + float2( 0.5,  0.5) * r, d + dot(gradient, float2( 0.5,  0.5) * r));\n" \
    "        return s * 0.25;\n" \
    "    }\n" \
    "    if (f.quality.y > 1.5) {\n" \
    "        // PCSS for a directional area light. shadowSoftness remains the maximum world radius.\n" \
    "        // Search the centre as well as the disk so narrow casters are not missed at contact.\n" \
    "        float gap = 0.0, blockers = 0.0;\n" \
    "        for (int i = -1; i < 8; ++i) {\n" \
    "            float2 o = i < 0 ? float2(0.0) : kDisk[i] * r;\n" \
    "            float delta = d + dot(gradient, o) - sunDepth(sm, uv + o);\n" \
    "            if (delta > 0.000001) { gap += delta; blockers += 1.0; }\n" \
    "        }\n" \
    "        if (blockers == 0.0 && diffusion <= 0.0) return 1.0;\n" \
    "        float depthPerWorld = length(float3(f.lightViewProj[0].z, f.lightViewProj[1].z, f.lightViewProj[2].z)) * f.shadow.y;\n" \
    "        float uvPerWorld = 0.5 * length(float3(f.lightViewProj[0].x, f.lightViewProj[1].x, f.lightViewProj[2].x));\n" \
    "        float penumbra = (gap / max(blockers, 1.0)) / max(depthPerWorld, 1e-6) * 0.05 * uvPerWorld;\n" \
    "        r = clamp(penumbra, min(r, f.shadow.w * 0.5), r);\n" \
    "    }\n" \
    "    // Receiver-only spread approximates surface fibres; it never changes caster geometry.\n" \
    "    if (diffusion > 0.0) {\n" \
    "        float receiverRadius = clamp(diffusion, 0.0, 1.0) * 0.5 * length(float3(f.lightViewProj[0].x, f.lightViewProj[1].x, f.lightViewProj[2].x));\n" \
    "        r = sqrt(r * r + receiverRadius * receiverRadius);\n" \
    "    }\n" \
    "    // Sample the smooth radial density directly, with equal per-tap contributions.\n" \
    "    // More samples and a broader taper reduce residual bands without pixel noise.\n" \
    "    // fract 0.75: mobile budget, fixed unrotated disk of int(taps) samples (no 64/128-tap stable disk)\n" \
    "    if (fract(f.quality.x) <= 0.25) {\n" \
    "        int count = f.quality.x > 16.0 ? 128 : 64;\n" \
    "        int stride = count == 128 ? 1 : 2;\n" \
    "        float total = 0.0;\n" \
    "        for (int i = 0; i < 128; ++i) {\n" \
    "            if (i >= count) break;\n" \
    "            float2 o = kStableDisk[i * stride];\n" \
    "            total += sunCompare(sm, uv + o * r, d + dot(gradient, o * r));\n" \
    "        }\n" \
    "        return total / float(count);\n" \
    "    }\n" \
    "    float temporal = fract(f.quality.x) > 0.25 && fract(f.quality.x) < 0.6 ? floor(f.camPos.w * 60.0) * 0.61803398875 : 0.0;\n" \
    "    float a = fract(f.quality.x) <= 0.25 || fract(f.quality.x) > 0.6 || r < f.shadow.w * 3.2 ? 0.0 : 6.2831853 * fract(52.9829189 * fract(dot(fragCoord, float2(0.06711056, 0.00583715))) + temporal);\n" \
    "    float2 cs = float2(cos(a), sin(a));\n" \
    "    int cnt = int(f.quality.x);\n" \
    "    float s = 0.0;\n" \
    "    for (int i = 0; i < 24; ++i) {\n" \
    "        if (i >= cnt) break;\n" \
    "        float2 o = kDisk[i];\n" \
    "        o = float2(o.x * cs.x - o.y * cs.y, o.x * cs.y + o.y * cs.x);\n" \
    "        s += sunCompare(sm, uv + o * r, d + dot(gradient, o * r));\n" \
    "    }\n" \
    "    return s / float(cnt);\n" \
    "}\n" \
    "float shadowAt(constant U1& f, float3 wpos, float3 n, float2 fragCoord, depth2d<float> sm, sampler ss) { return shadowAtSurface(f, wpos, n, fragCoord, sm, ss, 0.0); }\n"

inline const char* kLitVS =
    AIVERSE_GLSL_BLOCKS
    R"(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec3 a_nrm;
layout(location = 2) in vec4 a_col;
out vec3 v_nrm;
out vec3 v_wpos;
out vec4 v_col;
void main() {
    vec4 wp = u_model * vec4(a_pos, 1.0);
    v_wpos = wp.xyz;
    v_nrm = mat3(u_model) * a_nrm;
    v_col = a_col;
    gl_Position = u_viewProj * wp;
}
)";

inline const char* kLitFS =
    "#ifndef AIVERSE_NEREID_DIAGNOSTIC\n#define AIVERSE_NEREID_DIAGNOSTIC 0\n#endif\n"
    AIVERSE_GLSL_BLOCKS AIVERSE_GLSL_SHADOW AIVERSE_GLSL_LIGHTS AIVERSE_GLSL_WATER_REFLECT("tex2") AIVERSE_GLSL_TRACE_AT("tex3") AIVERSE_GLSL_SSR_AT("tex4", "tex5")
    R"(
in vec3 v_nrm;
in vec3 v_wpos;
in vec4 v_col;
out vec4 o_color;
uniform sampler2D tex6;

float shadowFactor(vec3 n) { return u_params.z < 0.5 ? 1.0 : min(shadowAt(v_wpos, n), terrainShadow(v_wpos)); }

)"
    AIVERSE_GLSL_NEREID_WAVES
    R"(
float nereidRapidField(vec2 transported,vec2 flow,float scale,float detail) {
    vec2 dir=flow/max(length(flow),.001);
    // Rotating absolute world position by a varying tangent makes phase grow
    // with distance from the world origin, tearing foam into chevrons at bends.
    // Keep noise world-anchored; direction only stretches a bounded local filter.
    vec2 q=transported*scale*1.3;
    float warp=nereidNoise(q*.23+vec2(13.7,-5.1));q+=vec2(warp*1.8,warp*.7);
    float field=.5*(nereidNoise(q-dir*1.2)+nereidNoise(q+dir*1.2));
    if(detail>.5)field=field*.65+.35*nereidNoise(vec2(q.x*1.73-q.y*.67,q.x*.67+q.y*1.73)+17.3);
    if(detail>1.5)field=field*.8+.2*nereidNoise(vec2(q.x*3.11+q.y*1.27,-q.x*1.27+q.y*3.11)-9.2);
    return field;
}
void main() {
    // Studio draw-call visualizer bypasses all PBR, shadows and texture shading.
    if (u_params.x > 9.5) {
        int mode = int(u_params.x - 10.0 + 0.5);
        if (mode == 2) { o_color = vec4(normalize(v_nrm) * .5 + .5, 1.0); return; }
        if (mode == 4) { float d = clamp(length(u_camPos.xyz - v_wpos) / 100.0, 0.0, 1.0); o_color = vec4(vec3(d), 1.0); return; }
        if (mode == 9) { o_color = vec4(fract(abs(v_wpos) * .05), 1.0); return; }
    }
    if (u_params.x > 1.5 && u_params.x < 9.5) { o_color = vec4(rtsEncodeScene(u_color.rgb), 1.0); return; }
    // NEREID tags repurpose u_color.rgb as optical coefficients; old
    // non-water draws preserve their tint without changing uniform layouts.
    vec4 albedo = vec4(v_col.rgb * (u_params.x < -0.5 ? vec3(1.0) : u_color.rgb),
                       v_col.a * u_color.a);
#if AIVERSE_NEREID_DIAGNOSTIC == 2
    if(u_params.x < -0.5){ vec3 diagnostic=vec3(u_color.r*0.25,u_color.g,u_color.b); o_color=vec4(diagnostic,1.0); return; }
#endif
    vec3 base = rtsDecodeScene(albedo.rgb);                  // ~sRGB -> linear
#if AIVERSE_NEREID_DIAGNOSTIC == 3
    if(u_params.x < -0.5){ vec3 diagnostic=base; o_color=vec4(diagnostic,1.0); return; }
#endif
    vec3 n = normalize(v_nrm);
    bool localCurrent=u_params.x < -.5 && u_waterFlowRect.z>0.0 && u_waterFlowRect.w>0.0;
    vec3 waterFlow=vec3(u_waterHydraulics.xy,u_waterHydraulics.z);
    if(localCurrent) {
        vec2 flowUV=(v_wpos.xz-u_waterFlowRect.xy)*u_waterFlowRect.zw;
        waterFlow=texture(tex6,flowUV).xyz;
    }
#if AIVERSE_NEREID_DIAGNOSTIC == 6
    if(u_params.x < -.5) { o_color=vec4(clamp(waterFlow.xy*.1+.5,0.0,1.0),clamp(length(waterFlow.xy)*.2,0.0,1.0),1.0); return; }
#endif
    // Two overlapping bounded advection phases hide their individual resets.
    // Variable spatial currents never multiply an unbounded renderer clock.
    bool boundedCurrent=localCurrent&&length(waterFlow.xy)>.001;
    float phase=fract(u_camPos.w*.25);
    float currentTime0=boundedCurrent?phase*4.0:u_camPos.w;
    float currentTime1=boundedCurrent?fract(phase+.5)*4.0:currentTime0;
    float currentBlend=boundedCurrent?abs(phase*2.0-1.0):0.0;

    // Open water (below the road film .045; rain pools keep their own look): six wave trains of different length
    // and heading (ratios that never line up) plus drifting value noise, fading out with distance so nothing
    // aliases into stripes; slow large patches of darker / lighter water.
    bool openWater = (u_params.x < -0.5 || (u_params.y >= 0.85 && v_wpos.y < 0.04)) && n.y > 0.9;
    if (openWater) {
        float wt = u_params.x < -0.5 ? u_camPos.w : (l_cloud.x + l_cloud.y) * 20.0;
        // PHYSIS/weather owner supplies world-space wind through NEREID's
        // generic presentation API; legacy untagged water is unchanged.
        float windCoupling=u_params.x < -0.5 ? u_waterWind.w : 0.0;
        vec2 p = v_wpos.xz -
                 u_waterWind.xy * wt * (0.02 * windCoupling);
        vec2 p1=p;
        if(u_params.x < -0.5){p-=waterFlow.xy*currentTime0;p1-=waterFlow.xy*currentTime1;}
        // Moving NEREID detail is transported by the authored current. Independent
        // wave phases would send highlights sideways even when flow is fixed.
        float intrinsicTime=(u_params.x < -0.5 && length(waterFlow.xy)>0.001)?0.0:wt;
        float fade=clamp(1.0-length(u_camPos.xyz-v_wpos)/160.0,.3,1.0);
        vec2 g=nereidWaveGradient(p,intrinsicTime,u_params.x < -.5);
        if(boundedCurrent)g=mix(g,nereidWaveGradient(p1,intrinsicTime,u_params.x < -.5),currentBlend);
        float windSpeed=length(u_waterWind.xy);
        float gust=(0.4+0.6*clamp(u_waterWind.z,0.0,1.0))*windCoupling;
        float windSlope=clamp(windSpeed*0.006,0.0,0.32)*gust;
        vec2 windDir=u_waterWind.xy/max(windSpeed,0.001);
        g+=windDir*windSlope;
        n = normalize(vec3(-g.x * fade, 1.0, -g.y * fade));
        float patches=nereidMacroPatch(p,intrinsicTime);
        if(boundedCurrent)patches=mix(patches,nereidMacroPatch(p1,intrinsicTime),currentBlend);
        base *= 0.84 + 0.22 * patches;
    }
    // NEREID: vertical/stepped cascades remain on the shared water
    // material path but never sample the horizontal planar reflection.
    // Fast downstream streaks perturb their ribbon normals and tint.
    if(u_params.x < -2.5) {
        float waterfallTime=u_camPos.w;
        float streak=sin(v_wpos.y*8.0+v_wpos.x*1.8 -
                         waterfallTime*3.6);
        float crossFlow=sin(v_wpos.z*12.0-v_wpos.y*3.2 -
                            waterfallTime*4.5);
        n=normalize(n+vec3(streak*0.12,0.06*crossFlow,
                           crossFlow*0.12));
        base *= 0.87+0.13*streak*streak;
    }
    // NEREID opt-in whitewater: sample locally resolved X/Z river,
    // vortex and cascade-mouth currents from the presentation snapshot.
    // Local flow comes from the immutable NEREID field; zero intensity disables whitewater.
    if(u_params.x < -0.5 && waterFlow.z > 0.001 && n.y > 0.5) {
        vec2 flow=waterFlow.xy;
        float speed=length(flow);
        vec2 dir=flow/max(speed,0.001),side=vec2(-dir.y,dir.x);
        float field=nereidRapidField(v_wpos.xz-flow*currentTime0,flow,u_waterHydraulics.w,u_waterRapidColor.a);
        if(boundedCurrent)field=mix(field,nereidRapidField(v_wpos.xz-flow*currentTime1,flow,u_waterHydraulics.w,u_waterRapidColor.a),currentBlend);
        float foam=smoothstep(0.48,0.78,field)*waterFlow.z;
        base=mix(base,u_waterRapidColor.rgb,clamp(foam*0.78,0.0,0.78));
        if(u_waterRapidColor.a>0.5)n=normalize(n+vec3(side.x*(field-0.5)*foam*0.17,0.0,side.y*(field-0.5)*foam*0.17));
    }
    if(u_params.x < -0.5) {
        vec2 grad = vec2(0.0);
        float rippleFoam = 0.0;
        for(int i=0;i<16;++i) {
            vec4 ripple=u_waterRipples[i];
            if(ripple.w<=0.0)continue;
            vec2 delta=v_wpos.xz-ripple.xy;
            float distanceToCenter=length(delta);
            vec2 gradientDirection=delta/max(distanceToCenter,0.001);
            float wakeMask=1.0;
            vec4 shape=u_waterWakeShapes[i];
            if(shape.w>0.5) {
                vec2 heading=normalize(shape.xy);
                vec2 side=vec2(-heading.y,heading.x);
                float along=dot(delta,heading);
                float across=dot(delta,side);
                float transverse=max(shape.z,1.0);
                vec2 elliptical=vec2(along*0.75,across*transverse);
                distanceToCenter=length(elliptical);
                gradientDirection=(heading*(along*0.5625)+
                    side*(across*transverse*transverse))/
                    max(distanceToCenter,0.001);
                wakeMask=smoothstep(-0.1,0.55,
                    -along/max(ripple.z,0.001));
            }
            float radial=distanceToCenter-ripple.z;
            float envelope=exp(-radial*radial*2.0)*wakeMask;
            grad += gradientDirection *
                    (-radial*4.0*envelope*ripple.w);
            rippleFoam += envelope * ripple.w * 0.42;
        }
        n=normalize(n+vec3(-grad.x,0.0,-grad.y));
        // Impact foam rides the ring rather than an overlay decal.
        base=mix(base,vec3(0.82,0.92,0.94),
                 clamp(rippleFoam,0.0,0.65));
    }

    vec3 L = normalize(u_sunDir.xyz);
    vec3 V = normalize(u_camPos.xyz - v_wpos);
    float ndl = max(dot(n, L), 0.0);
    float sh = ndl > 0.0 ? shadowFactor(n) : 1.0;
    sh *= cloudShade(v_wpos);
    vec3 sun = rtsDecodeScene(u_sunColor.rgb) * u_sunDir.w;
    vec3 sky = rtsDecodeScene(u_skyColor.rgb), ground = rtsDecodeScene(u_groundColor.rgb);
    float ambientVisibility;vec3 local=dynamicLightAmbient(v_wpos,n,V,u_params.y,ambientVisibility);
    vec3 amb = mix(ground, sky, n.y * 0.5 + 0.5)*ambientVisibility;
    vec3 H = normalize(L + V);
    float spec = pow(max(dot(n, H), 0.0), 40.0) * u_params.y;
    if (u_params.y >= 0.85 && v_wpos.y >= 0.04) spec = pow(max(dot(n, H), 0.0), 180.0) * 0.04;
    if (openWater) spec = pow(max(dot(n, H), 0.0), 260.0) * 1.4 + pow(max(dot(n, H), 0.0), 30.0) * 0.06;   // sun glitter
    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0) * 0.25;
    vec3 col = base * (amb + sun * ndl * sh) + sun * spec * sh + amb * rim * base;
    col += base * local;
    col = mix(col, base, clamp(u_params.x, 0.0, 1.0)) + base * ((u_params.x < -0.5 ? 0.0 : u_params.w) * (1.0 + nightGlow()));
    float dist = length(u_camPos.xyz - v_wpos);
    col = mix(col, rtsDecodeScene(u_fog.rgb), 1.0 - exp(-dist * u_fog.w));
    col = rtsToneScene(col, u_skyColor.w > 0.5); // ACES fit
    vec3 outc = waterReflect(rtsEncodeScene(col), v_wpos, n, V,
                             u_params.x < -1.5 ? 0.0 : u_params.y, u_params.x < -0.5);   // planar reflection (water, High)
    if (u_params.y >= 0.85 && (v_wpos.y >= 0.04 || u_params.x < -0.5) && u_trace2.w > 0.5) {                     // screen-space reflection (trace tier)
        vec3 nn = u_params.x < -0.5 ? n : normalize(n + vec3(sin(v_wpos.z * 3.0 + l_cloud.x * 11.0), 0.0, sin(v_wpos.x * 2.6 - l_cloud.y * 9.0)) * 0.02);
        float hit;
        vec3 rc = ssrWater(v_wpos, nn, V, hit);
        float fres = (v_wpos.y < 0.04 ? 0.12 : 0.08) + 0.45 * pow(1.0 - max(dot(n, V), 0.0), 3.0);
        outc = mix(outc, mix(rtsEncodeScene(col), rc, fres), hit);
    }
    float shoreCoverage=1.0;
    float contactFoam=0.0;
    bool opticsResolved=false;
#if AIVERSE_NEREID_DIAGNOSTIC == 4
    float diagnosticTransmission=-1.0;
#endif
#if AIVERSE_NEREID_DIAGNOSTIC == 1
    float diagnosticDepth=-100000.0;
#endif
    // NEREID optics: sample the existing opaque colour/depth capture only.
    // u_color.rgb = absorption(m^-1), foam depth(m), depth tint strength.
    // Captured background depth must be below the water plane.
    if (u_params.x < -0.5 && u_trace2.w > 0.0 &&
        (u_params.w > 0.001 || u_color.r > 0.001 ||
         u_color.g > 0.001 || u_color.b > 0.001)) {
        vec4 projected = u_viewProj * vec4(v_wpos, 1.0);
        if (projected.w > 0.0) {
            vec2 screen = projected.xy / projected.w * 0.5 + 0.5;
            // Contact coverage uses undistorted depth: refraction must not move
            // the actual shoreline or expose the water mesh perimeter.
            if(u_params.x > -2.5 && all(greaterThan(screen,vec2(0.001))) && all(lessThan(screen,vec2(0.999)))){
                vec2 shoreUV=u_shadow.x>0.5?vec2(screen.x,1.0-screen.y):screen;
                float shoreDepth=texture(tex5,shoreUV).r;
                if(shoreDepth<0.9999){
                    float depth=v_wpos.y-ssrUnproject(screen,shoreDepth).y;
#if AIVERSE_NEREID_DIAGNOSTIC == 1
                    diagnosticDepth=depth;
#endif
                    shoreCoverage=smoothstep(0.0,0.035,depth);
                    if(u_color.g>0.001 && depth>0.0 && depth<1000.0){
                        contactFoam=1.0-smoothstep(0.0,u_color.g,depth);
                        // Bounded overlapping upstream depth probes create a tapered downstream
                        // tail. No contact wake exists ahead of the obstacle/current.
                        float speed=length(waterFlow.xy);
                        vec2 currentDir=waterFlow.xy/max(speed,0.001);
                        float wakeFoam=0.0;
                        int tapCount=u_waterRapidColor.a>1.5?16:(u_waterRapidColor.a>0.5?8:4);
                        if(speed>0.01)for(int tap=0;tap<16;++tap){
                            if(tap>=tapCount)break;
                            float tail=float(tap)/float(tapCount-1);
                            float reach=0.2+1.4*tail;
                            vec3 probe=v_wpos-vec3(currentDir.x,0.0,currentDir.y)*reach;
                            probe.y+=dot(v_nrm.xz,currentDir)/max(abs(v_nrm.y),0.1)*reach;
                            vec4 clip=u_viewProj*vec4(probe,1.0);
                            if(clip.w<=0.0)continue;
                            vec2 probeScreen=clip.xy/clip.w*0.5+0.5;
                            if(any(lessThan(probeScreen,vec2(0.001)))||any(greaterThan(probeScreen,vec2(0.999))))continue;
                            vec2 probeUV=u_shadow.x>0.5?vec2(probeScreen.x,1.0-probeScreen.y):probeScreen;
                            float probeDepth=texture(tex5,probeUV).r;
                            if(probeDepth>=0.9999)continue;
                            vec3 hit=ssrUnproject(probeScreen,probeDepth);
                            float height=hit.y-probe.y;
                            float nearby=1.0-smoothstep(0.35,0.8,length(hit.xz-probe.xz));
                            float obstacle=smoothstep(0.01,0.10,height)*(1.0-smoothstep(2.0,3.0,height))*nearby;
                            float crossDistance=abs(dot(hit.xz-v_wpos.xz,vec2(-currentDir.y,currentDir.x)));
                            float tailWidth=0.55*(1.0-reach*0.4);
                            float taper=1.0-smoothstep(tailWidth*0.3,tailWidth,crossDistance);
                            wakeFoam+=obstacle*taper*(0.70-0.50*tail);
                        }
                        contactFoam=max(contactFoam,clamp(wakeFoam*1.2/float(tapCount),0.0,0.6));
                    }
                }
            }
            vec2 distorted = screen + n.xz * (0.009 * u_params.w * clamp(albedo.a,0.0,1.0));
            if (all(greaterThan(distorted, vec2(0.005))) &&
                all(lessThan(distorted, vec2(0.995)))) {
                vec2 sampleUV = u_shadow.x > 0.5 ?
                    vec2(distorted.x, 1.0 - distorted.y) : distorted;
                float sampledDepth = texture(tex5, sampleUV).r;
                if (sampledDepth < 0.9999) {
                    vec3 opaquePos = ssrUnproject(distorted, sampledDepth);
                    float thickness = v_wpos.y - opaquePos.y;
                    if (thickness > 0.02 && thickness < 1000.0) {
                        vec3 background = texture(tex4, sampleUV).rgb;
)" R"(                        // Light reaches the bed through the full column and returns along the
                        // slanted view ray: the same optical path PRISMA uses under water.
                        float opticalPath = thickness + length(v_wpos - opaquePos);
                        float transmission = exp(-opticalPath * u_color.r * clamp(albedo.a,0.0,1.0));
#if AIVERSE_NEREID_DIAGNOSTIC == 4
                        diagnosticTransmission=transmission;
#endif
                        vec3 absorbed = background * transmission +
                            rtsEncodeScene(base) * (1.0 - transmission) * u_color.b;
                        // Absorbed colour already contains the refracted background.
                        // Adding it again through ordinary alpha makes deep water look dry.
                        float fresnel=(0.02+0.98*pow(1.0-max(dot(n,V),0.0),5.0))*
                            clamp(u_params.y,0.0,1.0)*clamp(albedo.a,0.0,1.0);
                        outc = mix(absorbed,outc,fresnel);
                        opticsResolved=true;
                    }
                }
            }
        }
    }
    if(contactFoam>0.0){
        vec2 foamPoint=v_wpos.xz-waterFlow.xy*currentTime0;
        float breakup=0.65+0.35*sin(dot(foamPoint,vec2(2.7,4.1))+sin(dot(foamPoint,vec2(-3.3,1.9))));
        if(boundedCurrent){
            vec2 other=v_wpos.xz-waterFlow.xy*currentTime1;
            float otherBreakup=0.65+0.35*sin(dot(other,vec2(2.7,4.1))+sin(dot(other,vec2(-3.3,1.9))));
            breakup=mix(breakup,otherBreakup,currentBlend);
        }
        outc=mix(outc,vec3(0.82,0.91,0.92),clamp(contactFoam*breakup*0.72*clamp(albedo.a,0.0,1.0),0.0,0.72));
    }
#if AIVERSE_NEREID_DIAGNOSTIC == 1
    if(u_params.x < -0.5){
        vec3 diagnostic=diagnosticDepth < -99999.0 ? vec3(0,0,1) :
            vec3(clamp(diagnosticDepth*0.25,0.0,1.0),diagnosticDepth<0.0?1.0:0.0,0);
        o_color=vec4(diagnostic,1.0); return;
    }
#endif
#if AIVERSE_NEREID_DIAGNOSTIC == 4
    if(u_params.x < -0.5){ vec3 diagnostic=diagnosticTransmission<0.0?vec3(0,0,1):vec3(diagnosticTransmission); o_color=vec4(diagnostic,1.0); return; }
#endif
#if AIVERSE_NEREID_DIAGNOSTIC == 5
    if(u_params.x < -0.5){ vec3 diagnostic=vec3((opticsResolved?1.0:albedo.a)*shoreCoverage); o_color=vec4(diagnostic,1.0); return; }
#endif
    // NEREID optics replace the fogged surface colour with depth tint,
    // refraction, reflection and foam: fog the final water colour once more
    // so distant water fades into the haze like the terrain around it.
    if (opticsResolved) {
        float waterFog = 1.0 - exp(-dist * u_fog.w);
        outc = mix(outc, rtsEncodeScene(rtsToneScene(rtsDecodeScene(u_fog.rgb), u_skyColor.w > 0.5)), waterFog);
    }
    o_color = vec4(outc, (opticsResolved?1.0:albedo.a)*shoreCoverage);
}
)";

inline const char* kLitMSL =
    "#ifndef AIVERSE_NEREID_DIAGNOSTIC\n#define AIVERSE_NEREID_DIAGNOSTIC 0\n#endif\n"
    "#include <metal_stdlib>\nusing namespace metal;\n" AIVERSE_MSL_BLOCKS AIVERSE_MSL_SHADOW AIVERSE_MSL_LIGHTS AIVERSE_MSL_WATER_REFLECT AIVERSE_MSL_TRACE AIVERSE_MSL_SSR
    R"(
struct VIn { float3 pos [[attribute(0)]]; float3 nrm [[attribute(1)]]; float4 col [[attribute(2)]]; };
struct VOut { float4 pos [[position]]; float3 nrm; float3 wpos; float4 col; };
vertex VOut vs_main(VIn in [[stage_in]], constant U0& u [[buffer(1)]], constant U1& f [[buffer(2)]]) {
    VOut o;
    float4 wp = u.model * float4(in.pos, 1.0);
    o.wpos = wp.xyz;
    o.nrm = (u.model * float4(in.nrm, 0.0)).xyz;
    o.col = in.col;
    o.pos = f.viewProj * wp;
    return o;
}
static float shadowFactor(constant U0& u, constant U1& f, float3 wpos, float3 n, float2 fragCoord, depth2d<float> sm, sampler ss,
                          texture2d<float> hf, sampler hs) {
    return u.params.z < 0.5 ? 1.0 : min(shadowAt(f, wpos, n, fragCoord, sm, ss), terrainShadow(f, hf, hs, wpos));
}
)"
    AIVERSE_MSL_NEREID_WAVES
    R"(
float nereidRapidField(float2 transported,float2 flow,float scale,float detail) {
    float2 dir=flow/max(length(flow),.001);
    // Rotating absolute world position by a varying tangent makes phase grow
    // with distance from the world origin, tearing foam into chevrons at bends.
    // Keep noise world-anchored; direction only stretches a bounded local filter.
    float2 q=transported*scale*1.3;
    float warp=nereidNoise(q*.23+float2(13.7,-5.1));q+=float2(warp*1.8,warp*.7);
    float field=.5*(nereidNoise(q-dir*1.2)+nereidNoise(q+dir*1.2));
    if(detail>.5)field=field*.65+.35*nereidNoise(float2(q.x*1.73-q.y*.67,q.x*.67+q.y*1.73)+17.3);
    if(detail>1.5)field=field*.8+.2*nereidNoise(float2(q.x*3.11+q.y*1.27,-q.x*1.27+q.y*3.11)-9.2);
    return field;
}
fragment float4 fs_main(VOut in [[stage_in]], constant U0& u [[buffer(1)]], constant U1& f [[buffer(2)]],
                        depth2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]],
                        constant LightU& l [[buffer(3)]], texture2d<float> tex1 [[texture(1)]], sampler s1 [[sampler(1)]],
                        texture2d<float> tex2 [[texture(2)]], sampler s2 [[sampler(2)]],
                        texture2d<float> tex3 [[texture(3)]], sampler s3 [[sampler(3)]],
                        texture2d<float> tex4 [[texture(4)]], sampler s4 [[sampler(4)]],
                        depth2d<float> tex5 [[texture(5)]], sampler s5 [[sampler(5)]],
                        texture2d<float> tex6 [[texture(6)]], sampler s6 [[sampler(6)]]) {
    if (u.params.x > 9.5) {
        int mode = int(u.params.x - 10.0 + .5);
        if (mode == 2) return float4(normalize(in.nrm) * .5 + .5, 1.0);
        if (mode == 4) { float d = clamp(length(f.camPos.xyz - in.wpos) / 100.0, 0.0, 1.0); return float4(float3(d), 1.0); }
        if (mode == 9) return float4(fract(abs(in.wpos) * .05), 1.0);
    }
    if (u.params.x > 1.5 && u.params.x < 9.5) return float4(rtsEncodeScene(u.color.rgb), 1.0);
    float4 albedo = float4(in.col.rgb * (u.params.x < -0.5 ? float3(1.0) : u.color.rgb),
                           in.col.a * u.color.a);
#if AIVERSE_NEREID_DIAGNOSTIC == 2
    if(u.params.x < -0.5){ float3 diagnostic=float3(u.color.r*0.25,u.color.g,u.color.b); return float4(diagnostic,1.0); }
#endif
    float3 base = rtsDecodeScene(albedo.rgb);
#if AIVERSE_NEREID_DIAGNOSTIC == 3
    if(u.params.x < -0.5){ float3 diagnostic=base; return float4(diagnostic,1.0); }
#endif
    float3 n = normalize(in.nrm);
    bool localCurrent=u.params.x < -.5 && u.waterFlowRect.z>0.0 && u.waterFlowRect.w>0.0;
    float3 waterFlow=float3(u.waterHydraulics.xy,u.waterHydraulics.z);
    if(localCurrent) {
        float2 flowUV=(in.wpos.xz-u.waterFlowRect.xy)*u.waterFlowRect.zw;
        waterFlow=tex6.sample(s6,flowUV).xyz;
    }
#if AIVERSE_NEREID_DIAGNOSTIC == 6
    if(u.params.x < -.5) { return float4(clamp(waterFlow.xy*.1+.5,0.0,1.0),clamp(length(waterFlow.xy)*.2,0.0,1.0),1.0);  }
#endif
    // Two overlapping bounded advection phases hide their individual resets.
    // Variable spatial currents never multiply an unbounded renderer clock.
    bool boundedCurrent=localCurrent&&length(waterFlow.xy)>.001;
    float phase=fract(f.camPos.w*.25);
    float currentTime0=boundedCurrent?phase*4.0:f.camPos.w;
    float currentTime1=boundedCurrent?fract(phase+.5)*4.0:currentTime0;
    float currentBlend=boundedCurrent?abs(phase*2.0-1.0):0.0;

    // Open water (below the road film .045; rain pools keep their own look): six wave trains of different length
    // and heading (ratios that never line up) plus drifting value noise, fading out with distance so nothing
    // aliases into stripes; slow large patches of darker / lighter water.
    bool openWater = (u.params.x < -0.5 || (u.params.y >= 0.85 && in.wpos.y < 0.04)) && n.y > 0.9;
    if (openWater) {
        float wt = u.params.x < -0.5 ? f.camPos.w : (l.cloud.x + l.cloud.y) * 20.0;
        float windCoupling=u.params.x < -0.5 ? u.waterWind.w : 0.0;
        float2 p = in.wpos.xz -
                   u.waterWind.xy * wt * (0.02 * windCoupling);
        float2 p1=p;
        if(u.params.x < -0.5){p-=waterFlow.xy*currentTime0;p1-=waterFlow.xy*currentTime1;}
        // Moving NEREID detail is transported by the authored current. Independent
        // wave phases would send highlights sideways even when flow is fixed.
        float intrinsicTime=(u.params.x < -0.5 && length(waterFlow.xy)>0.001)?0.0:wt;
        float fade=clamp(1.0-length(f.camPos.xyz-in.wpos)/160.0,.3,1.0);
        float2 g=nereidWaveGradient(p,intrinsicTime,u.params.x < -.5);
        if(boundedCurrent)g=mix(g,nereidWaveGradient(p1,intrinsicTime,u.params.x < -.5),currentBlend);
        float windSpeed=length(u.waterWind.xy);
        float gust=(0.4+0.6*clamp(u.waterWind.z,0.0,1.0))*windCoupling;
        float windSlope=clamp(windSpeed*0.006,0.0,0.32)*gust;
        float2 windDir=u.waterWind.xy/max(windSpeed,0.001);
        g+=windDir*windSlope;
        n = normalize(float3(-g.x * fade, 1.0, -g.y * fade));
        float patches=nereidMacroPatch(p,intrinsicTime);
        if(boundedCurrent)patches=mix(patches,nereidMacroPatch(p1,intrinsicTime),currentBlend);
        base *= 0.84 + 0.22 * patches;
    }
    if(u.params.x < -2.5) {
        float waterfallTime=f.camPos.w;
        float streak=sin(in.wpos.y*8.0+in.wpos.x*1.8 -
                         waterfallTime*3.6);
        float crossFlow=sin(in.wpos.z*12.0-in.wpos.y*3.2 -
                            waterfallTime*4.5);
        n=normalize(n+float3(streak*0.12,0.06*crossFlow,
                             crossFlow*0.12));
        base *= 0.87+0.13*streak*streak;
    }
    if(u.params.x < -0.5 && waterFlow.z > 0.001 && n.y > 0.5) {
        float2 flow=waterFlow.xy;
        float speed=length(flow);
        float2 dir=flow/max(speed,0.001),side=float2(-dir.y,dir.x);
        float field=nereidRapidField(in.wpos.xz-flow*currentTime0,flow,u.waterHydraulics.w,u.waterRapidColor.a);
        if(boundedCurrent)field=mix(field,nereidRapidField(in.wpos.xz-flow*currentTime1,flow,u.waterHydraulics.w,u.waterRapidColor.a),currentBlend);
        float foam=smoothstep(0.48,0.78,field)*waterFlow.z;
        base=mix(base,u.waterRapidColor.rgb,clamp(foam*0.78,0.0,0.78));
        if(u.waterRapidColor.a>0.5)n=normalize(n+float3(side.x*(field-0.5)*foam*0.17,0.0,side.y*(field-0.5)*foam*0.17));
    }
    if(u.params.x < -0.5) {
        float2 grad=float2(0.0);
        float rippleFoam=0.0;
        for(int i=0;i<16;++i) {
            float4 ripple=u.waterRipples[i];
            if(ripple.w<=0.0)continue;
            float2 delta=in.wpos.xz-ripple.xy;
            float distanceToCenter=length(delta);
            float2 gradientDirection=delta/max(distanceToCenter,0.001);
            float wakeMask=1.0;
            float4 shape=u.waterWakeShapes[i];
            if(shape.w>0.5) {
                float2 heading=normalize(shape.xy);
                float2 side=float2(-heading.y,heading.x);
                float along=dot(delta,heading);
                float across=dot(delta,side);
                float transverse=max(shape.z,1.0);
                float2 elliptical=float2(along*0.75,across*transverse);
                distanceToCenter=length(elliptical);
                gradientDirection=(heading*(along*0.5625)+
                    side*(across*transverse*transverse))/
                    max(distanceToCenter,0.001);
                wakeMask=smoothstep(-0.1,0.55,
                    -along/max(ripple.z,0.001));
            }
            float radial=distanceToCenter-ripple.z;
            float envelope=exp(-radial*radial*2.0)*wakeMask;
            grad += gradientDirection *
                    (-radial*4.0*envelope*ripple.w);
            rippleFoam += envelope * ripple.w * 0.42;
        }
        n=normalize(n+float3(-grad.x,0.0,-grad.y));
        base=mix(base,float3(0.82,0.92,0.94),
                 clamp(rippleFoam,0.0,0.65));
    }

    float3 L = normalize(f.sunDir.xyz);
    float3 V = normalize(f.camPos.xyz - in.wpos);
    float ndl = max(dot(n, L), 0.0);
    float sh = ndl > 0.0 ? shadowFactor(u, f, in.wpos, n, in.pos.xy, tex0, s0, tex3, s3) : 1.0;
    sh *= cloudShade(l, in.wpos);
    float3 sun = rtsDecodeScene(f.sunColor.rgb) * f.sunDir.w;
    float ambientVisibility;float3 local=dynamicLightAmbient(l,tex1,s1,in.wpos,n,V,u.params.y,ambientVisibility);
    float3 amb = mix(rtsDecodeScene(f.groundColor.rgb), rtsDecodeScene(f.skyColor.rgb), n.y * 0.5 + 0.5)*ambientVisibility;
    float3 H = normalize(L + V);
    float spec = pow(max(dot(n, H), 0.0), 40.0) * u.params.y;
    if (u.params.y >= 0.85 && in.wpos.y >= 0.04) spec = pow(max(dot(n, H), 0.0), 180.0) * 0.04;
    if (openWater) spec = pow(max(dot(n, H), 0.0), 260.0) * 1.4 + pow(max(dot(n, H), 0.0), 30.0) * 0.06;   // sun glitter
    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0) * 0.25;
    float3 col = base * (amb + sun * ndl * sh) + sun * spec * sh + amb * rim * base;
    col += base * local;
    col = mix(col, base, clamp(u.params.x, 0.0, 1.0)) + base * ((u.params.x < -0.5 ? 0.0 : u.params.w) * (1.0 + l.info.w));
    float dist = length(f.camPos.xyz - in.wpos);
    col = mix(col, rtsDecodeScene(f.fog.rgb), 1.0 - exp(-dist * f.fog.w));
    col = rtsToneScene(col, f.skyColor.w > 0.5);
    float3 outc = waterReflect(l, tex2, s2, in.pos.xy, rtsEncodeScene(col),
                               in.wpos, n, V, u.params.x < -1.5 ? 0.0 : u.params.y, u.params.x < -0.5);
    if (u.params.y >= 0.85 && (in.wpos.y >= 0.04 || u.params.x < -0.5) && f.trace2.w > 0.5) {
        float3 nn = u.params.x < -0.5 ? n : normalize(n + float3(sin(in.wpos.z * 3.0 + l.cloud.x * 11.0), 0.0, sin(in.wpos.x * 2.6 - l.cloud.y * 9.0)) * 0.02);
        float hit;
        float3 rc = ssrWater(f, tex4, s4, tex5, s5, in.wpos, nn, V, hit);
        float fres = (in.wpos.y < 0.04 ? 0.12 : 0.08) + 0.45 * pow(1.0 - max(dot(n, V), 0.0), 3.0);
        outc = mix(outc, mix(rtsEncodeScene(col), rc, fres), hit);
    }
    // NEREID optics: share captured opaque tex4/tex5 with SSR, no feedback.
    // u.color.rgb = absorption(m^-1), foam depth(m), depth tint strength.
    float shoreCoverage=1.0;
    float contactFoam=0.0;
    bool opticsResolved=false;
#if AIVERSE_NEREID_DIAGNOSTIC == 4
    float diagnosticTransmission=-1.0;
#endif
#if AIVERSE_NEREID_DIAGNOSTIC == 1
    float diagnosticDepth=-100000.0;
#endif
    if (u.params.x < -0.5 && f.trace2.w > 0.0 &&
        (u.params.w > 0.001 || u.color.r > 0.001 ||
         u.color.g > 0.001 || u.color.b > 0.001)) {
        float4 projected = f.viewProj * float4(in.wpos, 1.0);
        if (projected.w > 0.0) {
            float2 screen = projected.xy / projected.w * 0.5 + 0.5;
            if(u.params.x > -2.5 && all(screen>float2(0.001)) && all(screen<float2(0.999))){
                float2 shoreUV=f.shadow.x>0.5?float2(screen.x,1.0-screen.y):screen;
                float shoreDepth=tex5.sample(s5,shoreUV);
                if(shoreDepth<0.9999){
                    float depth=in.wpos.y-ssrUnproject(f,screen,shoreDepth).y;
#if AIVERSE_NEREID_DIAGNOSTIC == 1
                    diagnosticDepth=depth;
#endif
                    shoreCoverage=smoothstep(0.0,0.035,depth);
                    if(u.color.g>0.001 && depth>0.0 && depth<1000.0){
                        contactFoam=1.0-smoothstep(0.0,u.color.g,depth);
                        // Bounded overlapping upstream depth probes create a tapered downstream
                        // tail. No contact wake exists ahead of the obstacle/current.
                        float speed=length(waterFlow.xy);
                        float2 currentDir=waterFlow.xy/max(speed,0.001);
                        float wakeFoam=0.0;
                        int tapCount=u.waterRapidColor.a>1.5?16:(u.waterRapidColor.a>0.5?8:4);
                        if(speed>0.01)for(int tap=0;tap<16;++tap){
                            if(tap>=tapCount)break;
                            float tail=float(tap)/float(tapCount-1);
                            float reach=0.2+1.4*tail;
                            float3 probe=in.wpos-float3(currentDir.x,0.0,currentDir.y)*reach;
                            probe.y+=dot(in.nrm.xz,currentDir)/max(abs(in.nrm.y),0.1)*reach;
                            float4 clip=f.viewProj*float4(probe,1.0);
                            if(clip.w<=0.0)continue;
                            float2 probeScreen=clip.xy/clip.w*0.5+0.5;
                            if(any(probeScreen<float2(0.001))||any(probeScreen>float2(0.999)))continue;
                            float2 probeUV=f.shadow.x>0.5?float2(probeScreen.x,1.0-probeScreen.y):probeScreen;
                            float probeDepth=tex5.sample(s5,probeUV);
                            if(probeDepth>=0.9999)continue;
                            float3 hit=ssrUnproject(f,probeScreen,probeDepth);
                            float height=hit.y-probe.y;
                            float nearby=1.0-smoothstep(0.35,0.8,length(hit.xz-probe.xz));
                            float obstacle=smoothstep(0.01,0.10,height)*(1.0-smoothstep(2.0,3.0,height))*nearby;
                            float crossDistance=abs(dot(hit.xz-in.wpos.xz,float2(-currentDir.y,currentDir.x)));
                            float tailWidth=0.55*(1.0-reach*0.4);
                            float taper=1.0-smoothstep(tailWidth*0.3,tailWidth,crossDistance);
                            wakeFoam+=obstacle*taper*(0.70-0.50*tail);
                        }
                        contactFoam=max(contactFoam,clamp(wakeFoam*1.2/float(tapCount),0.0,0.6));
                    }
                }
            }
            float2 distorted = screen + n.xz * (0.009 * u.params.w * clamp(albedo.a,0.0,1.0));
            if (all(distorted > float2(0.005)) &&
                all(distorted < float2(0.995))) {
                float2 sampleUV = f.shadow.x > 0.5 ?
                    float2(distorted.x, 1.0 - distorted.y) : distorted;
                float sampledDepth = tex5.sample(s5, sampleUV);
                if (sampledDepth < 0.9999) {
                    float3 opaquePos = ssrUnproject(f, distorted, sampledDepth);
                    float thickness = in.wpos.y - opaquePos.y;
                    if (thickness > 0.02 && thickness < 1000.0) {
                        float3 background = tex4.sample(s4, sampleUV).rgb;
                        // Light reaches the bed through the full column and returns along the
                        // slanted view ray: the same optical path PRISMA uses under water.
                        float opticalPath = thickness + length(in.wpos - opaquePos);
                        float transmission = exp(-opticalPath * u.color.r * clamp(albedo.a,0.0,1.0));
#if AIVERSE_NEREID_DIAGNOSTIC == 4
)" R"(                        diagnosticTransmission=transmission;
#endif
                        float3 absorbed = background * transmission +
                            rtsEncodeScene(base) * (1.0 - transmission) * u.color.b;
                        // Absorbed colour already contains the refracted background.
                        // Adding it again through ordinary alpha makes deep water look dry.
                        float fresnel=(0.02+0.98*pow(1.0-max(dot(n,V),0.0),5.0))*
                            clamp(u.params.y,0.0,1.0)*clamp(albedo.a,0.0,1.0);
                        outc = mix(absorbed,outc,fresnel);
                        opticsResolved=true;

                    }
                }
            }
        }
    }
    if(contactFoam>0.0){
        float2 foamPoint=in.wpos.xz-waterFlow.xy*currentTime0;
        float breakup=0.65+0.35*sin(dot(foamPoint,float2(2.7,4.1))+sin(dot(foamPoint,float2(-3.3,1.9))));
        if(boundedCurrent){
            float2 other=in.wpos.xz-waterFlow.xy*currentTime1;
            float otherBreakup=0.65+0.35*sin(dot(other,float2(2.7,4.1))+sin(dot(other,float2(-3.3,1.9))));
            breakup=mix(breakup,otherBreakup,currentBlend);
        }
        outc=mix(outc,float3(0.82,0.91,0.92),clamp(contactFoam*breakup*0.72*clamp(albedo.a,0.0,1.0),0.0,0.72));
    }
#if AIVERSE_NEREID_DIAGNOSTIC == 1
    if(u.params.x < -0.5){
        float3 diagnostic=diagnosticDepth < -99999.0 ? float3(0,0,1) :
            float3(clamp(diagnosticDepth*0.25,0.0,1.0),diagnosticDepth<0.0?1.0:0.0,0);
        return float4(diagnostic,1.0);
    }
#endif
#if AIVERSE_NEREID_DIAGNOSTIC == 4
    if(u.params.x < -0.5){ float3 diagnostic=diagnosticTransmission<0.0?float3(0,0,1):float3(diagnosticTransmission); return float4(diagnostic,1.0); }
#endif
#if AIVERSE_NEREID_DIAGNOSTIC == 5
    if(u.params.x < -0.5){ float3 diagnostic=float3((opticsResolved?1.0:albedo.a)*shoreCoverage); return float4(diagnostic,1.0); }
#endif
    // NEREID optics replace the fogged surface colour: fog the result too.
    if (opticsResolved) {
        float waterFog = 1.0 - exp(-dist * f.fog.w);
        outc = mix(outc, rtsEncodeScene(rtsToneScene(rtsDecodeScene(f.fog.rgb), f.skyColor.w > 0.5)), waterFog);
    }
    return float4(outc, (opticsResolved?1.0:albedo.a)*shoreCoverage);
}
)";

// ---------------- scene blit (trace tier) ----------------
// Copies the opaque scene (colour + depth) from the offscreen target into the main pass, restoring
// the depth buffer so that water, translucent meshes and overlays keep depth-testing against it.
// U0: vec4 params (x flipY). Vertex buffer: fullscreen triangle (3 x Float2, NDC).
inline const char* kBlitVS = R"(
layout(location = 0) in vec2 a_pos;
layout(std140) uniform U0 { vec4 u_params; };
out vec2 v_uv;
void main() { v_uv = a_pos * 0.5 + 0.5; if (u_params.x > 0.5) v_uv.y = 1.0 - v_uv.y; gl_Position = vec4(a_pos, 0.0, 1.0); }
)";
inline const char* kBlitFS = R"(
layout(std140) uniform U0 { vec4 u_params; };
uniform mediump sampler2D tex0;
uniform highp sampler2D tex1;
in vec2 v_uv;
out vec4 o_color;
void main() { o_color = texture(tex0, v_uv); gl_FragDepth = texture(tex1, v_uv).r; }
)";
inline const char* kBlitMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4 params; };
struct VIn { float2 pos [[attribute(0)]]; };
struct VOut { float4 pos [[position]]; float2 uv; };
struct FOut { float4 color [[color(0)]]; float depth [[depth(any)]]; };
vertex VOut vs_main(VIn in [[stage_in]], constant U0& u [[buffer(1)]]) {
    VOut o; o.uv = in.pos * 0.5 + 0.5; if (u.params.x > 0.5) o.uv.y = 1.0 - o.uv.y; o.pos = float4(in.pos, 0.0, 1.0); return o;
}
fragment FOut fs_main(VOut in [[stage_in]], texture2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]],
                      depth2d<float> tex1 [[texture(1)]], sampler s1 [[sampler(1)]]) {
    FOut o; o.color = tex0.sample(s0, in.uv); o.depth = tex1.sample(s1, in.uv); return o;
}
)";

// Depth-only shadow caster
inline const char* kShadowVS =
    AIVERSE_GLSL_BLOCKS
    R"(
layout(location = 0) in vec3 a_pos;
void main() { gl_Position = u_lightViewProj * (u_model * vec4(a_pos, 1.0)); }
)";
inline const char* kShadowFS = R"(
void main() {}
)";
inline const char* kShadowMSL =
    "#include <metal_stdlib>\nusing namespace metal;\n" AIVERSE_MSL_BLOCKS
    R"(
struct VIn { float3 pos [[attribute(0)]]; };
struct VOut { float4 pos [[position]]; };
vertex VOut vs_main(VIn in [[stage_in]], constant U0& u [[buffer(1)]], constant U1& f [[buffer(2)]]) {
    VOut o; o.pos = f.lightViewProj * (u.model * float4(in.pos, 1.0)); return o;
}
)";

// ---------------- colored lines / flat triangles (overlays, no lighting) ----------------
// Only the frame block: a std140 block is active even when unused, and WebGL refuses a draw whose
// declared U0 is larger than the buffer bound there (lines and overlay triangles were dropped).
inline const char* kColorVS =
    AIVERSE_GLSL_FRAME
    R"(
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec4 a_col;
out vec4 v_col;
void main() { v_col = a_col; gl_Position = u_viewProj * vec4(a_pos, 1.0); }
)";
inline const char* kColorFS = R"(
in vec4 v_col;
out vec4 o_color;
void main() { o_color = rtsDisplay(v_col); }
)";
inline const char* kColorMSL =
    "#include <metal_stdlib>\nusing namespace metal;\n" AIVERSE_MSL_BLOCKS
    R"(
struct VIn { float3 pos [[attribute(0)]]; float4 col [[attribute(1)]]; };
struct VOut { float4 pos [[position]]; float4 col; };
vertex VOut vs_main(VIn in [[stage_in]], constant U1& f [[buffer(2)]]) {
    VOut o; o.pos = f.viewProj * float4(in.pos, 1.0); o.col = in.col; return o;
}
fragment float4 fs_main(VOut in [[stage_in]]) { return rtsDisplay(in.col); }
)";

// ---------------- Dear ImGui ----------------
// U0: mat4 proj; vec4 params (x: 1 = texture is single-channel alpha)
inline const char* kImGuiVS = R"(
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
layout(std140) uniform U0 { mat4 u_proj; vec4 u_params; };
out vec2 v_uv;
out vec4 v_col;
void main() { v_uv = a_uv; v_col = a_col; gl_Position = u_proj * vec4(a_pos, 0.0, 1.0); }
)";
inline const char* kImGuiFS = R"(
layout(std140) uniform U0 { mat4 u_proj; vec4 u_params; };
uniform sampler2D tex0;
in vec2 v_uv;
in vec4 v_col;
out vec4 o_color;
void main() {
    vec4 t = texture(tex0, v_uv);
    if (u_params.x > 0.5) t = vec4(1.0, 1.0, 1.0, t.r);
    o_color = rtsDisplay(v_col * t);
}
)";
inline const char* kImGuiMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4x4 proj; float4 params; };
struct VIn { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; };
struct VOut { float4 pos [[position]]; float2 uv; float4 col; };
vertex VOut vs_main(VIn in [[stage_in]], constant U0& u [[buffer(1)]]) {
    VOut o; o.pos = u.proj * float4(in.pos, 0.0, 1.0); o.uv = in.uv; o.col = in.col; return o;
}
fragment float4 fs_main(VOut in [[stage_in]], constant U0& u [[buffer(1)]],
                        texture2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]]) {
    float4 t = tex0.sample(s0, in.uv);
    if (u.params.x > 0.5) t = float4(1.0, 1.0, 1.0, t.r);
    return rtsDisplay(in.col * t);
}
)";

} // namespace aiverse::render::shaders
