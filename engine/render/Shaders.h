// Built-in shaders, written once per shading language. GLSL bodies are GLSL ES 3.00 without the
// #version line (the GL backend prepends "#version 300 es" or "#version 330 core").
//
// Lit pipeline (single pass, forward, mobile friendly):
//   sun (Lambert + Blinn specular) x shadow map with hardware PCF
//   + hemisphere ambient (sky/ground) + rim + distance fog + ACES tonemap, lighting in linear space.
#pragma once
#include "LightShaders.h"   // AIVERSE_GLSL_LIGHTS / AIVERSE_MSL_LIGHTS: dynamic point lights (U2, tex1)

namespace aiverse::render::shaders {

// Uniform blocks shared by the lit and shadow shaders (std140 / MSL layouts match):
//   U0 (per draw):  mat4 model; vec4 color; vec4 params(x unlit, y specular, z receiveShadow, w emissive)
//   U1 (per frame): mat4 viewProj; mat4 lightViewProj; mat4 invViewProj; vec4 sunDir(xyz, w intensity); vec4 sunColor;
//                   vec4 skyColor; vec4 groundColor; vec4 fog(rgb, density); vec4 camPos;
//                   vec4 shadow(x uvFlipY, y depthScale, z depthOffset, w texel); vec4 quality(x taps, y enabled, z radius uv, w normal offset)
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
    "layout(std140) uniform U0 { mat4 u_model; vec4 u_color; vec4 u_params; };\n" AIVERSE_GLSL_FRAME

#define AIVERSE_MSL_FRAME \
    "struct U1 { float4x4 viewProj; float4x4 lightViewProj; float4x4 invViewProj; float4 sunDir; float4 sunColor;\n" \
    "  float4 skyColor; float4 groundColor; float4 fog; float4 camPos; float4 shadow; float4 quality;\n" \
    "  float4 trace0; float4 trace1; float4 trace2; float4 trace3; };\n"
#define AIVERSE_MSL_BLOCKS \
    "struct U0 { float4x4 model; float4 color; float4 params; };\n" AIVERSE_MSL_FRAME

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


// Shadow lookup shared by every lit shader (needs U1 and the shadow map in slot 0).
#define AIVERSE_GLSL_SHADOW \
    "uniform highp sampler2DShadow tex0;\n" \
    "// Soft shadows: rotated Poisson-disc PCF over hardware 2x2 compare taps.\n" \
    "//   u_quality.x taps (1 / 4 / 8 / 16), u_quality.z filter radius in shadow UV, u_quality.w normal offset (world)\n" \
    "const vec2 kDisk[16] = vec2[16](\n" \
    "    vec2(-0.613, 0.617), vec2(0.170, -0.040), vec2(-0.299, 0.791), vec2(0.645, 0.493),\n" \
    "    vec2(-0.651, 0.717), vec2(0.421, 0.027), vec2(-0.817, -0.271), vec2(-0.705, -0.668),\n" \
    "    vec2(0.977, -0.108), vec2(0.063, -0.233), vec2(0.346, -0.894), vec2(-0.272, 0.416),\n" \
    "    vec2(-0.025, 0.919), vec2(-0.389, -0.144), vec2(0.733, -0.627), vec2(0.282, 0.512));\n" \
    "float shadowAt(vec3 wpos, vec3 n) {\n" \
    "    if (u_quality.y < 0.5) return 1.0;\n" \
    "    vec3 L = normalize(u_sunDir.xyz);\n" \
    "    float k = 1.0 - clamp(dot(n, L), 0.0, 1.0);          // grazing surfaces need the most offset\n" \
    "    vec4 lp = u_lightViewProj * vec4(wpos + n * u_quality.w * (0.35 + k), 1.0);\n" \
    "    vec3 p = lp.xyz / lp.w;\n" \
    "    vec2 uv = p.xy * 0.5 + 0.5;\n" \
    "    if (u_shadow.x > 0.5) uv.y = 1.0 - uv.y;\n" \
    "    float d = p.z * u_shadow.y + u_shadow.z;\n" \
    "    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 || d >= 1.0) return 1.0;\n" \
    "    float r = u_quality.z;\n" \
    "    if (u_quality.x < 2.0) return texture(tex0, vec3(uv, d));\n" \
    "    if (u_quality.x < 5.0) { // weak GPUs: fixed 4 taps, each a bilinear 2x2 compare\n" \
    "        float s = texture(tex0, vec3(uv + vec2(-0.5, -0.5) * r, d));\n" \
    "        s += texture(tex0, vec3(uv + vec2( 0.5, -0.5) * r, d));\n" \
    "        s += texture(tex0, vec3(uv + vec2(-0.5,  0.5) * r, d));\n" \
    "        s += texture(tex0, vec3(uv + vec2( 0.5,  0.5) * r, d));\n" \
    "        return s * 0.25;\n" \
    "    }\n" \
    "    // per-pixel rotation (interleaved gradient noise) turns banding into fine grain; a kernel of a\n" \
    "    // few texels can't band (each tap is a bilinear compare), where the grain would only show as stipple\n" \
    "    float a = r < u_shadow.w * 3.2 ? 0.0 : 6.2831853 * fract(52.9829189 * fract(dot(gl_FragCoord.xy, vec2(0.06711056, 0.00583715))));\n" \
    "    vec2 cs = vec2(cos(a), sin(a));\n" \
    "    int n_ = int(u_quality.x);\n" \
    "    float s = 0.0;\n" \
    "    for (int i = 0; i < 16; ++i) {\n" \
    "        if (i >= n_) break;\n" \
    "        vec2 o = kDisk[i];\n" \
    "        o = vec2(o.x * cs.x - o.y * cs.y, o.x * cs.y + o.y * cs.x);\n" \
    "        s += texture(tex0, vec3(uv + o * r, d));\n" \
    "    }\n" \
    "    return s / float(n_);\n" \
    "}\n"

#define AIVERSE_MSL_SHADOW \
    "constant float2 kDisk[16] = {\n" \
    "    float2(-0.613, 0.617), float2(0.170, -0.040), float2(-0.299, 0.791), float2(0.645, 0.493),\n" \
    "    float2(-0.651, 0.717), float2(0.421, 0.027), float2(-0.817, -0.271), float2(-0.705, -0.668),\n" \
    "    float2(0.977, -0.108), float2(0.063, -0.233), float2(0.346, -0.894), float2(-0.272, 0.416),\n" \
    "    float2(-0.025, 0.919), float2(-0.389, -0.144), float2(0.733, -0.627), float2(0.282, 0.512)};\n" \
    "static float shadowAt(constant U1& f, float3 wpos, float3 n, float2 fragCoord, depth2d<float> sm, sampler ss) {\n" \
    "    if (f.quality.y < 0.5) return 1.0;\n" \
    "    float3 L = normalize(f.sunDir.xyz);\n" \
    "    float k = 1.0 - clamp(dot(n, L), 0.0, 1.0);\n" \
    "    float4 lp = f.lightViewProj * float4(wpos + n * f.quality.w * (0.35 + k), 1.0);\n" \
    "    float3 p = lp.xyz / lp.w;\n" \
    "    float2 uv = p.xy * 0.5 + 0.5;\n" \
    "    if (f.shadow.x > 0.5) uv.y = 1.0 - uv.y;\n" \
    "    float d = p.z * f.shadow.y + f.shadow.z;\n" \
    "    if (uv.x <= 0.0 || uv.x >= 1.0 || uv.y <= 0.0 || uv.y >= 1.0 || d >= 1.0) return 1.0;\n" \
    "    float r = f.quality.z;\n" \
    "    if (f.quality.x < 2.0) return sm.sample_compare(ss, uv, d);\n" \
    "    if (f.quality.x < 5.0) {\n" \
    "        float s = sm.sample_compare(ss, uv + float2(-0.5, -0.5) * r, d);\n" \
    "        s += sm.sample_compare(ss, uv + float2( 0.5, -0.5) * r, d);\n" \
    "        s += sm.sample_compare(ss, uv + float2(-0.5,  0.5) * r, d);\n" \
    "        s += sm.sample_compare(ss, uv + float2( 0.5,  0.5) * r, d);\n" \
    "        return s * 0.25;\n" \
    "    }\n" \
    "    float a = r < f.shadow.w * 3.2 ? 0.0 : 6.2831853 * fract(52.9829189 * fract(dot(fragCoord, float2(0.06711056, 0.00583715))));\n" \
    "    float2 cs = float2(cos(a), sin(a));\n" \
    "    int cnt = int(f.quality.x);\n" \
    "    float s = 0.0;\n" \
    "    for (int i = 0; i < 16; ++i) {\n" \
    "        if (i >= cnt) break;\n" \
    "        float2 o = kDisk[i];\n" \
    "        o = float2(o.x * cs.x - o.y * cs.y, o.x * cs.y + o.y * cs.x);\n" \
    "        s += sm.sample_compare(ss, uv + o * r, d);\n" \
    "    }\n" \
    "    return s / float(cnt);\n" \
    "}\n"

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
    AIVERSE_GLSL_BLOCKS AIVERSE_GLSL_SHADOW AIVERSE_GLSL_LIGHTS AIVERSE_GLSL_WATER_REFLECT("tex2") AIVERSE_GLSL_TRACE_AT("tex3") AIVERSE_GLSL_SSR_AT("tex4", "tex5")
    R"(
in vec3 v_nrm;
in vec3 v_wpos;
in vec4 v_col;
out vec4 o_color;

float shadowFactor(vec3 n) { return u_params.z < 0.5 ? 1.0 : min(shadowAt(v_wpos, n), terrainShadow(v_wpos)); }

void main() {
    vec4 albedo = u_color * v_col;
    vec3 base = rtsDecodeScene(albedo.rgb);                  // ~sRGB -> linear
    vec3 n = normalize(v_nrm);
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
    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0) * 0.25;
    vec3 col = base * (amb + sun * ndl * sh) + sun * spec * sh + amb * rim * base;
    col += base * local;
    col = mix(col, base, u_params.x) + base * (u_params.w * (1.0 + nightGlow()));
    float dist = length(u_camPos.xyz - v_wpos);
    col = mix(col, rtsDecodeScene(u_fog.rgb), 1.0 - exp(-dist * u_fog.w));
    col = rtsToneScene(col, u_skyColor.w > 0.5); // ACES fit
    vec3 outc = waterReflect(rtsEncodeScene(col), v_wpos, n, V, u_params.y);   // planar reflection (water, High)
    if (u_params.y >= 0.85 && u_trace2.w > 0.5) {                     // screen-space reflection (trace tier)
        vec3 nn = normalize(n + vec3(sin(v_wpos.z * 3.0 + l_cloud.x * 11.0), 0.0, sin(v_wpos.x * 2.6 - l_cloud.y * 9.0)) * 0.02);
        float hit;
        vec3 rc = ssrWater(v_wpos, nn, V, hit);
        float fres = 0.5 + 0.45 * pow(1.0 - max(dot(n, V), 0.0), 3.0);
        outc = mix(outc, mix(rtsEncodeScene(col), rc, fres), hit);
    }
    o_color = vec4(outc, albedo.a);
}
)";

inline const char* kLitMSL =
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
fragment float4 fs_main(VOut in [[stage_in]], constant U0& u [[buffer(1)]], constant U1& f [[buffer(2)]],
                        depth2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]],
                        constant LightU& l [[buffer(3)]], texture2d<float> tex1 [[texture(1)]], sampler s1 [[sampler(1)]],
                        texture2d<float> tex2 [[texture(2)]], sampler s2 [[sampler(2)]],
                        texture2d<float> tex3 [[texture(3)]], sampler s3 [[sampler(3)]],
                        texture2d<float> tex4 [[texture(4)]], sampler s4 [[sampler(4)]],
                        depth2d<float> tex5 [[texture(5)]], sampler s5 [[sampler(5)]]) {
    float4 albedo = u.color * in.col;
    float3 base = rtsDecodeScene(albedo.rgb);
    float3 n = normalize(in.nrm);
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
    float rim = pow(1.0 - max(dot(n, V), 0.0), 3.0) * 0.25;
    float3 col = base * (amb + sun * ndl * sh) + sun * spec * sh + amb * rim * base;
    col += base * local;
    col = mix(col, base, u.params.x) + base * (u.params.w * (1.0 + l.info.w));
    float dist = length(f.camPos.xyz - in.wpos);
    col = mix(col, rtsDecodeScene(f.fog.rgb), 1.0 - exp(-dist * f.fog.w));
    col = rtsToneScene(col, f.skyColor.w > 0.5);
    float3 outc = waterReflect(l, tex2, s2, in.pos.xy, rtsEncodeScene(col), in.wpos, n, V, u.params.y);
    if (u.params.y >= 0.85 && f.trace2.w > 0.5) {
        float3 nn = normalize(n + float3(sin(in.wpos.z * 3.0 + l.cloud.x * 11.0), 0.0, sin(in.wpos.x * 2.6 - l.cloud.y * 9.0)) * 0.02);
        float hit;
        float3 rc = ssrWater(f, tex4, s4, tex5, s5, in.wpos, nn, V, hit);
        float fres = 0.5 + 0.45 * pow(1.0 - max(dot(n, V), 0.0), 3.0);
        outc = mix(outc, mix(rtsEncodeScene(col), rc, fres), hit);
    }
    return float4(outc, albedo.a);
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
inline const char* kColorVS =
    AIVERSE_GLSL_BLOCKS
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
