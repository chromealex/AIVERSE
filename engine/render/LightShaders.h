// Shader code of the dynamic lights (see Lights.h), kept apart from Shaders.h / ModelShaders.h so
// each lit shader only needs the declarations macro and one line in main().
//
// Light block (slot U2 in the lit shader, U4 in the model shader):
//   vec4 map  (origin.xz, 1/extent, enabled)
//   vec4 info (x per-pixel count, y map range, z flipY, w night emissive boost)
//   vec4 cloud (noise offset.xz, scale, coverage);
//   vec4 sky (x cloud shadow strength, y water reflection strength (0 = none), z/w 1 / view size)
//   vec4 env (x environment reflections from the light map on/off)
//   vec4 pos[8] (xyz, radius); vec4 col[8] (rgb * intensity, spot cosInner);
//   vec4 spot[8] (spot direction, cosOuter; < -1.5 = point light)
// Light map texture: tex1 in the lit shader, tex7 in the model shader (RGBA8, light / range).
#pragma once

namespace aiverse::render::shaders {

// GLSL: block / sampler names are parameters because each shader has its own slots.
#define AIVERSE_GLSL_LIGHTS_AT(BLOCK, TEX) \
    "layout(std140) uniform " BLOCK " { vec4 l_map; vec4 l_info; vec4 l_cloud; vec4 l_sky; vec4 l_env; vec4 l_pos[8]; vec4 l_col[8]; vec4 l_gridOrigin; vec4 l_gridSize; vec4 l_spot[8]; };\n" \
    "uniform highp sampler2D " TEX ";\n" \
    "vec4 gridFetch(int i) { return texelFetch(" TEX ", ivec2(i % int(l_gridSize.w), i / int(l_gridSize.w)), 0); }\n" \
    "float gridVisibility(int offset, vec3 direction, float distance, float ndl, int taps) {\n" \
    "    if (offset <= 0) return 1.0;\n" \
    "    vec4 meta = gridFetch(offset);\n" \
    "    int resolution = int(meta.x);\n" \
    "    vec3 v = direction / max(distance, 1e-6);\n" \
    "    vec3 tangent = normalize(cross(v, abs(v.y) < 0.99 ? vec3(0,1,0) : vec3(1,0,0)));\n" \
    "    vec3 bitangent = cross(v,tangent);\n" \
    "    float bias = meta.y + distance * (1.0-ndl) / float(resolution);\n" \
    "    float visible = 0.0;\n" \
    "    for (int tap=0; tap<taps; ++tap) {\n" \
    "        vec3 q = v + (tangent * ((tap%2)==0 ? -0.5 : 0.5) + bitangent * (tap<2 ? -0.5 : 0.5)) * (taps==1?0.0:2.0/float(resolution));\n" \
    "        vec3 a = abs(q); vec2 uv; int face;\n" \
    "        if (a.x >= a.y && a.x >= a.z) {face=q.x>0.0?0:1;uv=vec2(q.x>0.0?-q.z:q.z,q.y)/a.x;}\n" \
    "        else if (a.y >= a.z) {face=q.y>0.0?2:3;uv=vec2(q.x,q.y>0.0?-q.z:q.z)/a.y;}\n" \
    "        else {face=q.z>0.0?4:5;uv=vec2(q.z>0.0?q.x:-q.x,q.y)/a.z;}\n" \
    "        ivec2 pixel = ivec2(clamp(floor((uv*0.5+0.5)*float(resolution)),vec2(0),vec2(float(resolution-1))));\n" \
    "        vec4 sampleDepth = gridFetch(offset+1+face*resolution*resolution+pixel.y*resolution+pixel.x);\n" \
    "        float depth = sampleDepth.x;\n" \
    "        vec2 center = (vec2(pixel)+0.5)*2.0/float(resolution)-1.0;\n" \
    "        vec3 ray;\n" \
    "        if(face==0)ray=vec3(1,center.y,-center.x);\n" \
    "        else if(face==1)ray=vec3(-1,center.y,center.x);\n" \
    "        else if(face==2)ray=vec3(center.x,1,-center.y);\n" \
    "        else if(face==3)ray=vec3(center.x,-1,center.y);\n" \
    "        else if(face==4)ray=vec3(center.x,center.y,1);\n" \
    "        else ray=vec3(-center.x,center.y,-1);\n" \
    "        float receiverPlane = dot(sampleDepth.yzw,v);\n" \
    "        float samplePlane = dot(sampleDepth.yzw,normalize(ray));\n" \
    "        if(abs(receiverPlane)>0.01 && receiverPlane*samplePlane>0.0)\n" \
    "            depth *= clamp(samplePlane/receiverPlane,0.5,2.0);\n" \
    "        visible += distance-bias <= depth ? 1.0/float(taps) : 0.0;\n" \
    "    }\n" \
    "    return visible;\n" \
    "}\n" \
    "vec3 gridIndirect(vec3 wpos, vec3 n) {\n" \
    "    if(gridFetch(0).z<=0.0)return vec3(0.0);\n" \
    "    vec3 coordinate=(wpos-l_gridOrigin.xyz)*l_gridOrigin.w-0.5;\n" \
    "    vec3 base=floor(coordinate),fraction=coordinate-base;\n" \
    "    vec3 result=vec3(0.0);float total=0.0;\n" \
    "    for(int i=0;i<8;++i){\n" \
    "        vec3 corner=vec3(float(i%2),float((i/2)%2),float(i/4));\n" \
    "        vec3 cell=base+corner;\n" \
    "        if(cell.x<0.0||cell.y<0.0||cell.z<0.0||cell.x>=l_gridSize.x||cell.y>=l_gridSize.y||cell.z>=l_gridSize.z)continue;\n" \
    "        int index=int(cell.x+l_gridSize.x*(cell.y+l_gridSize.y*cell.z));\n" \
    "        int offset=int(gridFetch(index).z);if(offset<=0)continue;\n" \
    "        vec3 weights=mix(vec3(1.0)-fraction,fraction,corner);\n" \
    "        float weight=weights.x*weights.y*weights.z;\n" \
    "        vec3 position=l_gridOrigin.xyz+(cell+0.5)/l_gridOrigin.w;\n" \
    "        vec3 delta=wpos-position;float distance=length(delta);\n" \
    "        if(distance>1e-5)weight*=gridVisibility(offset+3,delta,distance,1.0,1);\n" \
    "        if(weight<=0.0)continue;\n" \
    "        vec4 normal=vec4(1.0,n);\n" \
    "        vec3 value=vec3(dot(gridFetch(offset),normal),dot(gridFetch(offset+1),normal),dot(gridFetch(offset+2),normal));\n" \
    "        result+=max(value,vec3(0.0))*weight;total+=weight;\n" \
    "    }\n" \
    "    return total>1e-5?result/total:vec3(0.0);\n" \
    "}\n" \
    "void gridLight(vec3 wpos, vec3 n, vec3 V, float rough, out vec3 dd, out vec3 ds) {\n" \
    "    dd = gridIndirect(wpos,n); ds = vec3(0.0);\n" \
    "    vec3 cell = floor((wpos - l_gridOrigin.xyz) * l_gridOrigin.w);\n" \
    "    if (cell.x < 0.0 || cell.y < 0.0 || cell.z < 0.0 || cell.x >= l_gridSize.x || cell.y >= l_gridSize.y || cell.z >= l_gridSize.z) return;\n" \
    "    int index = int(cell.x + l_gridSize.x * (cell.y + l_gridSize.y * cell.z));\n" \
    "    vec4 header = gridFetch(index);\n" \
    "    float gloss = 1.0 - rough;\n" \
    "    float shin = mix(8.0, 256.0, gloss * gloss);\n" \
    "    for (int i = 0; i < int(header.y); ++i) {\n" \
    "        vec4 source = gridFetch(int(header.x) + i * 2);\n" \
    "        vec3 delta = source.xyz - wpos;\n" \
    "        float d2 = dot(delta, delta);\n" \
    "        if (d2 >= source.w * source.w) continue;\n" \
    "        vec3 L = delta * inversesqrt(max(d2, 1e-6));\n" \
    "        float ndl = max(dot(n, L), 0.0);\n" \
    "        if (ndl <= 0.0) continue;\n" \
    "        float a = 1.0 - d2 / (source.w * source.w);\n" \
    "        vec4 energy = gridFetch(int(header.x) + i * 2 + 1);\n" \
    "        float emission = 1.0, diffuseOnly = 0.0;\n" \
    "        if(energy.w>0.0){\n" \
    "            vec4 meta = gridFetch(int(energy.w));\n" \
    "            if(meta.z>0.5){diffuseOnly=1.0;emission=max(dot(gridFetch(int(meta.w)).xyz,-L),0.0);}\n" \
    "        }\n" \
    "        if(emission<=0.0)continue;\n" \
    "        float visibility = gridVisibility(int(energy.w), -delta, sqrt(d2), ndl, 4);\n" \
    "        if (visibility <= 0.0) continue;\n" \
    "        vec3 radiance = energy.rgb * (a * a / (1.0 + d2)) * visibility * emission;\n" \
    "        vec3 H = L + V; H *= inversesqrt(max(dot(H,H),1e-6));\n" \
    "        dd += radiance * ndl;\n" \
    "        ds += radiance * (ndl * pow(max(dot(n,H),0.0),shin) * (shin+8.0)*0.04) * (1.0-diffuseOnly);\n" \
    "    }\n" \
    "}\n" \
    "float nightGlow() { return l_info.w; }\n" \
    "/* unlit (2D) shaders: day/night tint and the light map at a world point; a block that was never */\n" \
    "/* bound (map range != 4) leaves sprites as they were */\n" \
    "bool lightsBound() { return abs(l_info.y - 4.0) < 0.01; }\n" \
    "vec3 unlitTint() { return lightsBound() ? l_env.yzw : vec3(1.0); }\n" \
    "vec3 mapLight(vec3 wpos) {\n" \
    "    if (!lightsBound() || l_map.w < 0.5) return vec3(0.0);\n" \
    "    vec2 uv = (wpos.xz - l_map.xy) * l_map.z;\n" \
    "    vec2 e = min(uv, 1.0 - uv);\n" \
    "    float fade = clamp(min(e.x, e.y) * 7.0, 0.0, 1.0);\n" \
    "    if (l_info.z > 0.5) uv.y = 1.0 - uv.y;\n" \
    "    return texture(" TEX ", uv).rgb * (l_info.y * fade);\n" \
    "}\n" \
    "float cloudShade(vec3 p) { /* drifting cloud shadows on the sun: a few sines, no texture */\n" \
    "    if (l_sky.x <= 0.0) return 1.0;\n" \
    "    vec2 q = p.xz * l_cloud.z + l_cloud.xy;\n" \
    "    float v = sin(q.x) * sin(q.y * 1.31) + 0.6 * sin(q.x * 2.13 + q.y * 1.71 + 1.7) * sin(q.y * 2.37 - q.x * 0.93);\n" \
    "    v = v * 0.4 + 0.5;\n" \
    "    return 1.0 - smoothstep(1.0 - l_cloud.w, 1.25 - l_cloud.w, v) * l_sky.x;\n" \
    "}\n" \
    "vec3 dynamicLight(vec3 wpos, vec3 n, vec3 V, float specK) {\n" \
    "    if (l_gridSize.x > 0.0) { vec3 dd, ds; gridLight(wpos,n,V,0.5,dd,ds); return dd + ds * specK; }\n" \
    "    vec3 c = vec3(0.0);\n" \
    "    if (l_map.w > 0.5) { /* ground-level light: weaker than the 3D path so the tiers match */\n" \
    "        vec2 uv = (wpos.xz - l_map.xy) * l_map.z;\n" \
    "        vec2 e = min(uv, 1.0 - uv);\n" \
    "        float fade = clamp(min(e.x, e.y) * 7.0, 0.0, 1.0);\n" \
    "        if (l_info.z > 0.5) uv.y = 1.0 - uv.y;\n" \
    "        vec3 m = texture(" TEX ", uv).rgb * (l_info.y * fade);\n" \
    "        c = m * (0.4 + 0.3 * max(n.y, 0.0));\n" \
    "        if (specK > 0.6) { /* water: lights glitter on moving ripples (phase from the wind offset) */\n" \
    "            float r = sin(wpos.x * 3.1 + l_cloud.x * 9.0) * sin(wpos.z * 2.7 - l_cloud.y * 7.0) + 0.6 * sin((wpos.x + wpos.z) * 4.7 - l_cloud.x * 13.0);\n" \
    "            c += m * (specK * (0.25 + 0.35 * r * r));\n" \
    "        }\n" \
    "    }\n" \
    "    int cnt = int(l_info.x);\n" \
    "    for (int i = 0; i < 8; ++i) {\n" \
    "        if (i >= cnt) break;\n" \
    "        vec3 d = l_pos[i].xyz - wpos;\n" \
    "        float d2 = dot(d, d);\n" \
    "        float a = clamp(1.0 - d2 / (l_pos[i].w * l_pos[i].w), 0.0, 1.0);\n" \
    "        a *= a;\n" \
    "        vec3 L = d * inversesqrt(max(d2, 1e-6));\n" \
    "        if (l_spot[i].w > -1.5) { float sp = smoothstep(l_spot[i].w, l_col[i].w, dot(-L, l_spot[i].xyz)); a *= sp * sp; }\n" \
    "        float ndl = (max(dot(n, L), 0.0) - 0.5) * 0.6;   /* detail around the map light */\n" \
    "        float sp = pow(max(dot(n, normalize(L + V)), 0.0), 40.0) * specK;\n" \
    "        c += l_col[i].rgb * (a * (ndl + sp));\n" \
    "    }\n" \
    "    return max(c, vec3(0.0));\n" \
    "}\n" \
    "/* PBR materials: diffuse and specular apart, so metals reflect light (F0 tints dd/ds outside). */\n" \
    "void dynLight(vec3 wpos, vec3 n, vec3 V, float rough, out vec3 dd, out vec3 ds) {\n" \
    "    if (l_gridSize.x > 0.0) { gridLight(wpos,n,V,rough,dd,ds); return; }\n" \
    "    dd = vec3(0.0); ds = vec3(0.0);\n" \
    "    float gloss = 1.0 - rough;\n" \
    "    if (l_map.w > 0.5) {\n" \
    "        vec2 uv = (wpos.xz - l_map.xy) * l_map.z;\n" \
    "        vec2 e = min(uv, 1.0 - uv);\n" \
    "        float fade = clamp(min(e.x, e.y) * 7.0, 0.0, 1.0);\n" \
    "        vec2 uvf = uv; if (l_info.z > 0.5) uvf.y = 1.0 - uvf.y;\n" \
    "        vec3 m = texture(" TEX ", uvf).rgb * (l_info.y * fade);\n" \
    "        dd = m * (0.4 + 0.3 * max(n.y, 0.0));\n" \
    "        if (l_env.x > 0.5 && gloss > 0.35) { /* glossy: the lit ground along the reflected ray */\n" \
    "            vec3 R = reflect(-V, n);\n" \
    "            vec2 ruv = (wpos.xz + R.xz * (1.0 + 2.0 * max(R.y, 0.0)) - l_map.xy) * l_map.z;\n" \
    "            if (l_info.z > 0.5) ruv.y = 1.0 - ruv.y;\n" \
    "            ds += texture(" TEX ", ruv).rgb * (l_info.y * fade * (gloss - 0.35) * 1.6);\n" \
    "        }\n" \
    "    }\n" \
    "    float shin = mix(8.0, 256.0, gloss * gloss);\n" \
    "    int cnt = int(l_info.x);\n" \
    "    for (int i = 0; i < 8; ++i) {\n" \
    "        if (i >= cnt) break;\n" \
    "        vec3 d = l_pos[i].xyz - wpos;\n" \
    "        float d2 = dot(d, d);\n" \
    "        float a = clamp(1.0 - d2 / (l_pos[i].w * l_pos[i].w), 0.0, 1.0);\n" \
    "        a *= a;\n" \
    "        vec3 L = d * inversesqrt(max(d2, 1e-6));\n" \
    "        if (l_spot[i].w > -1.5) { float sp = smoothstep(l_spot[i].w, l_col[i].w, dot(-L, l_spot[i].xyz)); a *= sp * sp; }\n" \
    "        float ndl = max(dot(n, L), 0.0);\n" \
    "        dd += l_col[i].rgb * (a * (ndl - 0.5) * 0.6);\n" \
    "        ds += l_col[i].rgb * (a * (ndl * pow(max(dot(n, normalize(L + V)), 0.0), shin) * (shin + 8.0) * 0.04));\n" \
    "    }\n" \
    "    dd = max(dd, vec3(0.0));\n" \
    "}\n"

#define AIVERSE_GLSL_LIGHTS AIVERSE_GLSL_LIGHTS_AT("U2", "tex1")

// MSL: the block and the texture are function arguments (buffer / texture indices are chosen in
// each fragment function's signature).
#define AIVERSE_MSL_LIGHTS \
    "struct LightU { float4 map; float4 info; float4 cloud; float4 sky; float4 env; float4 pos[8]; float4 col[8]; float4 gridOrigin; float4 gridSize; float4 spot[8]; };\n" \
    "static float4 gridFetch(texture2d<float> lm, int i, int width) { return lm.read(uint2(i % width, i / width)); }\n" \
    "static float gridVisibility(texture2d<float> lm, int width, int offset, float3 direction, float distance, float ndl, int taps) {\n" \
    "    if (offset <= 0) return 1.0;\n" \
    "    float4 meta = gridFetch(lm,offset,width);\n" \
    "    int resolution = int(meta.x);\n" \
    "    float3 v = direction / max(distance, 1e-6);\n" \
    "    float3 tangent = normalize(cross(v, abs(v.y) < 0.99 ? float3(0,1,0) : float3(1,0,0)));\n" \
    "    float3 bitangent = cross(v,tangent);\n" \
    "    float bias = meta.y + distance * (1.0-ndl) / float(resolution);\n" \
    "    float visible = 0.0;\n" \
    "    for (int tap=0; tap<taps; ++tap) {\n" \
    "        float3 q = v + (tangent * ((tap%2)==0 ? -0.5 : 0.5) + bitangent * (tap<2 ? -0.5 : 0.5)) * (taps==1?0.0:2.0/float(resolution));\n" \
    "        float3 a = abs(q); float2 uv; int face;\n" \
    "        if (a.x >= a.y && a.x >= a.z) {face=q.x>0.0?0:1;uv=float2(q.x>0.0?-q.z:q.z,q.y)/a.x;}\n" \
    "        else if (a.y >= a.z) {face=q.y>0.0?2:3;uv=float2(q.x,q.y>0.0?-q.z:q.z)/a.y;}\n" \
    "        else {face=q.z>0.0?4:5;uv=float2(q.z>0.0?q.x:-q.x,q.y)/a.z;}\n" \
    "        int2 pixel = int2(clamp(floor((uv*0.5+0.5)*float(resolution)),float2(0),float2(float(resolution-1))));\n" \
    "        float4 sampleDepth = gridFetch(lm,offset+1+face*resolution*resolution+pixel.y*resolution+pixel.x,width);\n" \
    "        float depth = sampleDepth.x;\n" \
    "        float2 center = (float2(pixel)+0.5)*2.0/float(resolution)-1.0;\n" \
    "        float3 ray;\n" \
    "        if(face==0)ray=float3(1,center.y,-center.x);\n" \
    "        else if(face==1)ray=float3(-1,center.y,center.x);\n" \
    "        else if(face==2)ray=float3(center.x,1,-center.y);\n" \
    "        else if(face==3)ray=float3(center.x,-1,center.y);\n" \
    "        else if(face==4)ray=float3(center.x,center.y,1);\n" \
    "        else ray=float3(-center.x,center.y,-1);\n" \
    "        float receiverPlane = dot(sampleDepth.yzw,v);\n" \
    "        float samplePlane = dot(sampleDepth.yzw,normalize(ray));\n" \
    "        if(abs(receiverPlane)>0.01 && receiverPlane*samplePlane>0.0)\n" \
    "            depth *= clamp(samplePlane/receiverPlane,0.5,2.0);\n" \
    "        visible += distance-bias <= depth ? 1.0/float(taps) : 0.0;\n" \
    "    }\n" \
    "    return visible;\n" \
    "}\n" \
    "static float3 gridIndirect(constant LightU& l, texture2d<float> lm, float3 wpos, float3 n) {\n" \
    "    if(gridFetch(lm,0,int(l.gridSize.w)).z<=0.0)return float3(0.0);\n" \
    "    float3 coordinate=(wpos-l.gridOrigin.xyz)*l.gridOrigin.w-0.5;\n" \
    "    float3 base=floor(coordinate),fraction=coordinate-base;\n" \
    "    float3 result=float3(0.0);float total=0.0;\n" \
    "    for(int i=0;i<8;++i){\n" \
    "        float3 corner=float3(float(i%2),float((i/2)%2),float(i/4));\n" \
    "        float3 cell=base+corner;\n" \
    "        if(cell.x<0.0||cell.y<0.0||cell.z<0.0||cell.x>=l.gridSize.x||cell.y>=l.gridSize.y||cell.z>=l.gridSize.z)continue;\n" \
    "        int index=int(cell.x+l.gridSize.x*(cell.y+l.gridSize.y*cell.z));\n" \
    "        int offset=int(gridFetch(lm,index,int(l.gridSize.w)).z);if(offset<=0)continue;\n" \
    "        float3 weights=mix(float3(1.0)-fraction,fraction,corner);\n" \
    "        float weight=weights.x*weights.y*weights.z;\n" \
    "        float3 position=l.gridOrigin.xyz+(cell+0.5)/l.gridOrigin.w;\n" \
    "        float3 delta=wpos-position;float distance=length(delta);\n" \
    "        if(distance>1e-5)weight*=gridVisibility(lm,int(l.gridSize.w),offset+3,delta,distance,1.0,1);\n" \
    "        if(weight<=0.0)continue;\n" \
    "        float4 normal=float4(1.0,n);\n" \
    "        float3 value=float3(dot(gridFetch(lm,offset,int(l.gridSize.w)),normal),dot(gridFetch(lm,offset+1,int(l.gridSize.w)),normal),dot(gridFetch(lm,offset+2,int(l.gridSize.w)),normal));\n" \
    "        result+=max(value,float3(0.0))*weight;total+=weight;\n" \
    "    }\n" \
    "    return total>1e-5?result/total:float3(0.0);\n" \
    "}\n" \
    "static void gridLight(constant LightU& l, texture2d<float> lm, float3 wpos, float3 n, float3 V, float rough, thread float3& dd, thread float3& ds) {\n" \
    "    dd = gridIndirect(l,lm,wpos,n); ds = float3(0.0);\n" \
    "    float3 cell = floor((wpos - l.gridOrigin.xyz) * l.gridOrigin.w);\n" \
    "    if (cell.x < 0.0 || cell.y < 0.0 || cell.z < 0.0 || cell.x >= l.gridSize.x || cell.y >= l.gridSize.y || cell.z >= l.gridSize.z) return;\n" \
    "    int index = int(cell.x + l.gridSize.x * (cell.y + l.gridSize.y * cell.z));\n" \
    "    float4 header = gridFetch(lm,index,int(l.gridSize.w));\n" \
    "    float gloss = 1.0 - rough;\n" \
    "    float shin = mix(8.0, 256.0, gloss * gloss);\n" \
    "    for (int i = 0; i < int(header.y); ++i) {\n" \
    "        float4 source = gridFetch(lm,int(header.x)+i*2,int(l.gridSize.w));\n" \
    "        float3 delta = source.xyz - wpos;\n" \
    "        float d2 = dot(delta, delta);\n" \
    "        if (d2 >= source.w * source.w) continue;\n" \
    "        float3 L = delta * rsqrt(max(d2, 1e-6));\n" \
    "        float ndl = max(dot(n, L), 0.0);\n" \
    "        if (ndl <= 0.0) continue;\n" \
    "        float a = 1.0 - d2 / (source.w * source.w);\n" \
    "        float4 energy = gridFetch(lm,int(header.x)+i*2+1,int(l.gridSize.w));\n" \
    "        float emission = 1.0, diffuseOnly = 0.0;\n" \
    "        if(energy.w>0.0){\n" \
    "            float4 meta = gridFetch(lm,int(energy.w),int(l.gridSize.w));\n" \
    "            if(meta.z>0.5){diffuseOnly=1.0;emission=max(dot(gridFetch(lm,int(meta.w),int(l.gridSize.w)).xyz,-L),0.0);}\n" \
    "        }\n" \
    "        if(emission<=0.0)continue;\n" \
    "        float visibility = gridVisibility(lm,int(l.gridSize.w),int(energy.w),-delta,sqrt(d2),ndl,4);\n" \
    "        if (visibility <= 0.0) continue;\n" \
    "        float3 radiance = energy.rgb * (a * a / (1.0 + d2)) * visibility * emission;\n" \
    "        float3 H = L + V; H *= rsqrt(max(dot(H,H),1e-6));\n" \
    "        dd += radiance * ndl;\n" \
    "        ds += radiance * (ndl * pow(max(dot(n,H),0.0),shin) * (shin+8.0)*0.04) * (1.0-diffuseOnly);\n" \
    "    }\n" \
    "}\n" \
    "static bool lightsBound(constant LightU& l) { return abs(l.info.y - 4.0) < 0.01; }\n" \
    "static float3 unlitTint(constant LightU& l) { return lightsBound(l) ? l.env.yzw : float3(1.0); }\n" \
    "static float3 mapLight(constant LightU& l, texture2d<float> lm, sampler ls, float3 wpos) {\n" \
    "    if (!lightsBound(l) || l.map.w < 0.5) return float3(0.0);\n" \
    "    float2 uv = (wpos.xz - l.map.xy) * l.map.z;\n" \
    "    float2 e = min(uv, 1.0 - uv);\n" \
    "    float fade = clamp(min(e.x, e.y) * 7.0, 0.0, 1.0);\n" \
    "    if (l.info.z > 0.5) uv.y = 1.0 - uv.y;\n" \
    "    return lm.sample(ls, uv).rgb * (l.info.y * fade);\n" \
    "}\n" \
    "static float cloudShade(constant LightU& l, float3 p) {\n" \
    "    if (l.sky.x <= 0.0) return 1.0;\n" \
    "    float2 q = p.xz * l.cloud.z + l.cloud.xy;\n" \
    "    float v = sin(q.x) * sin(q.y * 1.31) + 0.6 * sin(q.x * 2.13 + q.y * 1.71 + 1.7) * sin(q.y * 2.37 - q.x * 0.93);\n" \
    "    v = v * 0.4 + 0.5;\n" \
    "    return 1.0 - smoothstep(1.0 - l.cloud.w, 1.25 - l.cloud.w, v) * l.sky.x;\n" \
    "}\n" \
    "static float3 dynamicLight(constant LightU& l, texture2d<float> lm, sampler ls, float3 wpos, float3 n, float3 V, float specK) {\n" \
    "    if (l.gridSize.x > 0.0) { float3 dd, ds; gridLight(l,lm,wpos,n,V,0.5,dd,ds); return dd + ds * specK; }\n" \
    "    float3 c = float3(0.0);\n" \
    "    if (l.map.w > 0.5) {\n" \
    "        float2 uv = (wpos.xz - l.map.xy) * l.map.z;\n" \
    "        float2 e = min(uv, 1.0 - uv);\n" \
    "        float fade = clamp(min(e.x, e.y) * 7.0, 0.0, 1.0);\n" \
    "        if (l.info.z > 0.5) uv.y = 1.0 - uv.y;\n" \
    "        float3 m = lm.sample(ls, uv).rgb * (l.info.y * fade);\n" \
    "        c = m * (0.4 + 0.3 * max(n.y, 0.0));\n" \
    "        if (specK > 0.6) {\n" \
    "            float r = sin(wpos.x * 3.1 + l.cloud.x * 9.0) * sin(wpos.z * 2.7 - l.cloud.y * 7.0) + 0.6 * sin((wpos.x + wpos.z) * 4.7 - l.cloud.x * 13.0);\n" \
    "            c += m * (specK * (0.25 + 0.35 * r * r));\n" \
    "        }\n" \
    "    }\n" \
    "    int cnt = int(l.info.x);\n" \
    "    for (int i = 0; i < 8; ++i) {\n" \
    "        if (i >= cnt) break;\n" \
    "        float3 d = l.pos[i].xyz - wpos;\n" \
    "        float d2 = dot(d, d);\n" \
    "        float a = clamp(1.0 - d2 / (l.pos[i].w * l.pos[i].w), 0.0, 1.0);\n" \
    "        a *= a;\n" \
    "        float3 L = d * rsqrt(max(d2, 1e-6));\n" \
    "        if (l.spot[i].w > -1.5) { float sp = smoothstep(l.spot[i].w, l.col[i].w, dot(-L, l.spot[i].xyz)); a *= sp * sp; }\n" \
    "        float ndl = (max(dot(n, L), 0.0) - 0.5) * 0.6;   /* detail around the map light */\n" \
    "        float sp = pow(max(dot(n, normalize(L + V)), 0.0), 40.0) * specK;\n" \
    "        c += l.col[i].rgb * (a * (ndl + sp));\n" \
    "    }\n" \
    "    return max(c, float3(0.0));\n" \
    "}\n" \
    "static void dynLight(constant LightU& l, texture2d<float> lm, sampler ls, float3 wpos, float3 n, float3 V, float rough, thread float3& dd, thread float3& ds) {\n" \
    "    if (l.gridSize.x > 0.0) { gridLight(l,lm,wpos,n,V,rough,dd,ds); return; }\n" \
    "    dd = float3(0.0); ds = float3(0.0);\n" \
    "    float gloss = 1.0 - rough;\n" \
    "    if (l.map.w > 0.5) {\n" \
    "        float2 uv = (wpos.xz - l.map.xy) * l.map.z;\n" \
    "        float2 e = min(uv, 1.0 - uv);\n" \
    "        float fade = clamp(min(e.x, e.y) * 7.0, 0.0, 1.0);\n" \
    "        float2 uvf = uv; if (l.info.z > 0.5) uvf.y = 1.0 - uvf.y;\n" \
    "        float3 m = lm.sample(ls, uvf).rgb * (l.info.y * fade);\n" \
    "        dd = m * (0.4 + 0.3 * max(n.y, 0.0));\n" \
    "        if (l.env.x > 0.5 && gloss > 0.35) {\n" \
    "            float3 R = reflect(-V, n);\n" \
    "            float2 ruv = (wpos.xz + R.xz * (1.0 + 2.0 * max(R.y, 0.0)) - l.map.xy) * l.map.z;\n" \
    "            if (l.info.z > 0.5) ruv.y = 1.0 - ruv.y;\n" \
    "            ds += lm.sample(ls, ruv).rgb * (l.info.y * fade * (gloss - 0.35) * 1.6);\n" \
    "        }\n" \
    "    }\n" \
    "    float shin = mix(8.0, 256.0, gloss * gloss);\n" \
    "    int cnt = int(l.info.x);\n" \
    "    for (int i = 0; i < 8; ++i) {\n" \
    "        if (i >= cnt) break;\n" \
    "        float3 d = l.pos[i].xyz - wpos;\n" \
    "        float d2 = dot(d, d);\n" \
    "        float a = clamp(1.0 - d2 / (l.pos[i].w * l.pos[i].w), 0.0, 1.0);\n" \
    "        a *= a;\n" \
    "        float3 L = d * rsqrt(max(d2, 1e-6));\n" \
    "        if (l.spot[i].w > -1.5) { float sp = smoothstep(l.spot[i].w, l.col[i].w, dot(-L, l.spot[i].xyz)); a *= sp * sp; }\n" \
    "        float ndl = max(dot(n, L), 0.0);\n" \
    "        dd += l.col[i].rgb * (a * (ndl - 0.5) * 0.6);\n" \
    "        ds += l.col[i].rgb * (a * (ndl * pow(max(dot(n, normalize(L + V)), 0.0), shin) * (shin + 8.0) * 0.04));\n" \
    "    }\n" \
    "    dd = max(dd, float3(0.0));\n" \
    "}\n"

// Planar water reflection (lit shader only; REFL = reflection texture rendered mirrored and flipped
// in x, sampled at the fragment's screen position with a ripple wobble). c: scene colour (linear in HDR, display-space in LDR).
#define AIVERSE_GLSL_WATER_REFLECT(REFL) \
    "uniform mediump sampler2D " REFL ";\n" \
    "vec3 waterReflect(vec3 c, vec3 wpos, vec3 n, vec3 V, float specK) {\n" \
    "    if (specK < 0.85 || l_sky.y <= 0.0) return c;\n" \
    "    vec2 uv = gl_FragCoord.xy * l_sky.zw;\n" \
    "    uv.x = 1.0 - uv.x;\n" \
    "    uv += vec2(sin(wpos.z * 3.0 + l_cloud.x * 11.0), sin(wpos.x * 2.6 - l_cloud.y * 9.0)) * 0.006;\n" \
    "    vec3 r = texture(" REFL ", uv).rgb;\n" \
    "    float fres = 0.5 + 0.45 * pow(1.0 - max(dot(n, V), 0.0), 3.0);   /* stylised: RTS cameras look down */\n" \
    "    return mix(c, r, fres * l_sky.y);\n" \
    "}\n"
#define AIVERSE_MSL_WATER_REFLECT \
    "static float3 waterReflect(constant LightU& l, texture2d<float> rt, sampler rs, float2 fragCoord, float3 c, float3 wpos, float3 n, float3 V, float specK) {\n" \
    "    if (specK < 0.85 || l.sky.y <= 0.0) return c;\n" \
    "    float2 uv = fragCoord * l.sky.zw;\n" \
    "    uv.x = 1.0 - uv.x;\n" \
    "    uv += float2(sin(wpos.z * 3.0 + l.cloud.x * 11.0), sin(wpos.x * 2.6 - l.cloud.y * 9.0)) * 0.006;\n" \
    "    float3 r = rt.sample(rs, uv).rgb;\n" \
    "    float fres = 0.5 + 0.45 * pow(1.0 - max(dot(n, V), 0.0), 3.0);   /* stylised: RTS cameras look down */\n" \
    "    return mix(c, r, fres * l.sky.y);\n" \
    "}\n"

// Light map splat: clip-space quads, radial falloff (1 - r^2)^2, additive. Spots are cut to their
// cone: the ground point under the texel is seen from the light (height h above the ground).
inline const char* kLightMapVS = R"(
layout(location = 0) in vec2 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
layout(location = 3) in vec4 a_dir;
layout(location = 4) in vec4 a_cone;
layout(location = 5) in vec4 a_src;
out vec2 v_uv;
out vec3 v_col;
out vec4 v_dir;
out vec4 v_cone;
out vec4 v_src;
void main() { v_uv = a_uv; v_col = a_col.rgb; v_dir = a_dir; v_cone = a_cone; v_src = a_src; gl_Position = vec4(a_pos, 0.0, 1.0); }
)";
inline const char* kLightMapFS = R"(
layout(std140) uniform U0 { mat4 u_spotVP; vec4 u_spotInfo; };   // depth scale, offset, flipY, texel
uniform highp sampler2DShadow tex0;
in vec2 v_uv;
in vec3 v_col;
in vec4 v_dir;
in vec4 v_cone;
in vec4 v_src;
out vec4 o_color;
void main() {
    float f = clamp(1.0 - dot(v_uv, v_uv), 0.0, 1.0);
    if (v_cone.x > -1.5) {   // spot: brightest near the light, soft cone edge
        vec3 p = vec3(v_uv.x * v_cone.z, -v_dir.w, v_uv.y * v_cone.z);
        float k = clamp(1.0 - length(p) / v_cone.z, 0.0, 1.0);
        float edge = smoothstep(v_cone.x, v_cone.y, dot(normalize(p), v_dir.xyz));
        f = k * sqrt(k) * edge * edge;
        if (v_src.w > 0.5 && f > 0.0) {   // the shadowed spot: ground point seen from the lamp
            vec4 lp = u_spotVP * vec4(v_src.x + v_uv.x * v_cone.z, v_src.z + 0.05, v_src.y + v_uv.y * v_cone.z, 1.0);
            vec3 q = lp.xyz / lp.w;
            vec2 uv = q.xy * 0.5 + 0.5;
            if (u_spotInfo.z > 0.5) uv.y = 1.0 - uv.y;
            float d = q.z * u_spotInfo.x + u_spotInfo.y - 0.0015;
            float t = u_spotInfo.w * 1.5;
            float s = texture(tex0, vec3(uv + vec2(-t, -t), d)) + texture(tex0, vec3(uv + vec2(t, -t), d))
                    + texture(tex0, vec3(uv + vec2(-t, t), d)) + texture(tex0, vec3(uv + vec2(t, t), d));
            f *= s * 0.25;
        }
    } else {
        f *= f;
    }
    o_color = vec4(v_col * f, 1.0);
}
)";
inline const char* kLightMapMSL = R"(#include <metal_stdlib>
using namespace metal;
struct MapU { float4x4 spotVP; float4 spotInfo; };
struct VIn { float2 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; float4 dir [[attribute(3)]]; float4 cone [[attribute(4)]]; float4 src [[attribute(5)]]; };
struct VOut { float4 pos [[position]]; float2 uv; float3 col; float4 dir; float4 cone; float4 src; };
vertex VOut vs_main(VIn in [[stage_in]]) {
    VOut o; o.pos = float4(in.pos, 0.0, 1.0); o.uv = in.uv; o.col = in.col.rgb; o.dir = in.dir; o.cone = in.cone; o.src = in.src; return o;
}
fragment float4 fs_main(VOut in [[stage_in]], constant MapU& u [[buffer(1)]], depth2d<float> tex0 [[texture(0)]], sampler s0 [[sampler(0)]]) {
    float f = clamp(1.0 - dot(in.uv, in.uv), 0.0, 1.0);
    if (in.cone.x > -1.5) {
        float3 p = float3(in.uv.x * in.cone.z, -in.dir.w, in.uv.y * in.cone.z);
        float k = clamp(1.0 - length(p) / in.cone.z, 0.0, 1.0);
        float edge = smoothstep(in.cone.x, in.cone.y, dot(normalize(p), in.dir.xyz));
        f = k * sqrt(k) * edge * edge;
        if (in.src.w > 0.5 && f > 0.0) {
            float4 lp = u.spotVP * float4(in.src.x + in.uv.x * in.cone.z, in.src.z + 0.05, in.src.y + in.uv.y * in.cone.z, 1.0);
            float3 q = lp.xyz / lp.w;
            float2 uv = q.xy * 0.5 + 0.5;
            if (u.spotInfo.z > 0.5) uv.y = 1.0 - uv.y;
            float d = q.z * u.spotInfo.x + u.spotInfo.y - 0.0015;
            float t = u.spotInfo.w * 1.5;
            float s = tex0.sample_compare(s0, uv + float2(-t, -t), d) + tex0.sample_compare(s0, uv + float2(t, -t), d)
                    + tex0.sample_compare(s0, uv + float2(-t, t), d) + tex0.sample_compare(s0, uv + float2(t, t), d);
            f *= s * 0.25;
        }
    } else {
        f *= f;
    }
    return float4(in.col * f, 1.0);
}
)";

// Halos: camera-facing quads (corners built on the CPU), soft core + wide falloff, additive,
// depth-tested against the scene but not written. Drawn in display space (after tonemapping).
// U0: mat4 viewProj
inline const char* kHaloVS = R"(
layout(std140) uniform U0 { mat4 u_viewProj; };
layout(location = 0) in vec3 a_pos;
layout(location = 1) in vec2 a_uv;
layout(location = 2) in vec4 a_col;
out vec2 v_uv;
out vec4 v_col;
void main() { v_uv = a_uv; v_col = a_col; gl_Position = u_viewProj * vec4(a_pos, 1.0); }
)";
inline const char* kHaloFS = R"(
in vec2 v_uv;
in vec4 v_col;
out vec4 o_color;
void main() {
    float d2 = dot(v_uv, v_uv);
    float f = clamp(1.0 - d2, 0.0, 1.0);
    float g = f * f * 0.55 + exp(-d2 * 18.0) * 0.6;   // wide glow + bright core
    o_color = rtsDisplay(vec4(v_col.rgb * (g * v_col.a), 1.0));
}
)";
inline const char* kHaloMSL = R"(#include <metal_stdlib>
using namespace metal;
struct HU0 { float4x4 viewProj; };
struct VIn { float3 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; };
struct VOut { float4 pos [[position]]; float2 uv; float4 col; };
vertex VOut vs_main(VIn in [[stage_in]], constant HU0& u [[buffer(1)]]) {
    VOut o; o.pos = u.viewProj * float4(in.pos, 1.0); o.uv = in.uv; o.col = in.col; return o;
}
fragment float4 fs_main(VOut in [[stage_in]]) {
    float d2 = dot(in.uv, in.uv);
    float f = clamp(1.0 - d2, 0.0, 1.0);
    float g = f * f * 0.55 + exp(-d2 * 18.0) * 0.6;
    return rtsDisplay(float4(in.col.rgb * (g * in.col.a), 1.0));
}
)";

// Contact shadows / ambient occlusion decals on the ground: same vertex layout as the halos, alpha
// blended black. col.r: shape (0 ellipse, 1 rounded box), col.g: edge softness, col.a: strength.
inline const char* kDecalFS = R"(
in vec2 v_uv;
in vec4 v_col;
out vec4 o_color;
void main() {
    float f;
    if (v_col.r < 0.5) { float d2 = dot(v_uv, v_uv); f = clamp(1.0 - d2, 0.0, 1.0); f *= f; }
    else { vec2 a = abs(v_uv); f = 1.0 - smoothstep(1.0 - v_col.g, 1.0, max(a.x, a.y)); f *= 1.0 - 0.35 * dot(a, a) * 0.5; }
    o_color = vec4(0.0, 0.0, 0.0, f * v_col.a);
}
)";
inline const char* kDecalMSL = R"(#include <metal_stdlib>
using namespace metal;
struct HU0 { float4x4 viewProj; };
struct VIn { float3 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; };
struct VOut { float4 pos [[position]]; float2 uv; float4 col; };
vertex VOut vs_main(VIn in [[stage_in]], constant HU0& u [[buffer(1)]]) {
    VOut o; o.pos = u.viewProj * float4(in.pos, 1.0); o.uv = in.uv; o.col = in.col; return o;
}
fragment float4 fs_main(VOut in [[stage_in]]) {
    float f;
    if (in.col.r < 0.5) { float d2 = dot(in.uv, in.uv); f = clamp(1.0 - d2, 0.0, 1.0); f *= f; }
    else { float2 a = abs(in.uv); f = 1.0 - smoothstep(1.0 - in.col.g, 1.0, max(a.x, a.y)); f *= 1.0 - 0.35 * dot(a, a) * 0.5; }
    return float4(0.0, 0.0, 0.0, f * in.col.a);
}
)";

// Spot light shafts: a camera-facing strip from the light along its beam (halo vertex layout;
// uv.x across -1..1, uv.y along 0..1), additive, fading across and along. col.a = strength.
inline const char* kBeamFS = R"(
in vec2 v_uv;
in vec4 v_col;
out vec4 o_color;
void main() {
    float across = 1.0 - abs(v_uv.x);
    float g = across * (0.35 + 0.65 * (1.0 - v_uv.y)) * smoothstep(0.0, 0.1, v_uv.y) * smoothstep(1.0, 0.7, v_uv.y);   // reaches the pool
    o_color = rtsDisplay(vec4(v_col.rgb * (g * v_col.a), 1.0));
}
)";
inline const char* kBeamMSL = R"(#include <metal_stdlib>
using namespace metal;
struct HU0 { float4x4 viewProj; };
struct VIn { float3 pos [[attribute(0)]]; float2 uv [[attribute(1)]]; float4 col [[attribute(2)]]; };
struct VOut { float4 pos [[position]]; float2 uv; float4 col; };
vertex VOut vs_main(VIn in [[stage_in]], constant HU0& u [[buffer(1)]]) {
    VOut o; o.pos = u.viewProj * float4(in.pos, 1.0); o.uv = in.uv; o.col = in.col; return o;
}
fragment float4 fs_main(VOut in [[stage_in]]) {
    float across = 1.0 - abs(in.uv.x);
    float g = across * (0.35 + 0.65 * (1.0 - in.uv.y)) * smoothstep(0.0, 0.1, in.uv.y) * smoothstep(1.0, 0.7, in.uv.y);
    return rtsDisplay(float4(in.col.rgb * (g * in.col.a), 1.0));
}
)";

} // namespace aiverse::render::shaders
