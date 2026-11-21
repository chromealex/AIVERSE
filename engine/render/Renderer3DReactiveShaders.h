#pragma once
#include <string>
// PRISMA reactive coverage for translucent rigid, animated and mesh draws.
// Stage 63's cutout variant reproduces rigid-model alpha texture cutoffs,
// UV transform, material team alpha, screen-space LOD dither and 3D dissolve.
// Stage 64 adds an explicit reactive-only skin/morph/baked vertex variant
// with current-deformed model-space position, UV and vertex alpha varyings.
// Animated reactive fragments now reproduce material alpha cutout,
// object-space dissolve, screen-space LOD dither and Actor Reveal aperture.
// Normal motion shaders are NOT mutated; true GPU pixel parity is still
// pending native GLSL/MSL and on-device verification.
// The mask never writes borrowed scene depth; it samples scene depth and
// attaches Renderer3D's own private depth scratch.
namespace aiverse::render::reactive_shaders {
inline const char* kVS = R"(
layout(location=0) in vec3 a_pos;
layout(std140) uniform U0 { mat4 mvp; vec4 params; vec4 lod; };
void main() {
    gl_Position = mvp * vec4(a_pos,1.0);
}
)";
inline const char* kFS = R"(
layout(std140) uniform U0 { mat4 mvp; vec4 params; vec4 lod; };
uniform sampler2D tex0;
out vec4 o_color;
void main() {
    if (lod.z > 0.5) {
        ivec2 lp = ivec2(floor(gl_FragCoord.xy)) & ivec2(3);
        int rank = ((lp.x & 1) ^ (lp.y & 1))*8 + (lp.y & 1)*4 +
                   ((lp.x>>1) ^ (lp.y>>1))*2 + (lp.y>>1);
        float coverage = (float(rank)+0.5)/16.0;
        if (lod.y > 0.5 ? coverage < lod.x : coverage >= lod.x)
            discard;
    }
    vec2 uv = gl_FragCoord.xy / params.xy;
    if (gl_FragCoord.z > texture(tex0,uv).r + 0.0015) discard;
    o_color = vec4(params.z,0.0,0.0,1.0);
}
)";
inline const char* kMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4x4 mvp; float4 params; float4 lod; };
struct VIn { float3 pos [[attribute(0)]]; };
struct VOut { float4 position [[position]]; };
vertex VOut vs_main(VIn in [[stage_in]],constant U0& u [[buffer(1)]]) {
    VOut out;
    out.position = u.mvp * float4(in.pos,1.0);
    return out;
}
fragment float4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],
                        depth2d<float> tex0 [[texture(0)]],
                        sampler tex0_s [[sampler(0)]]) {
    if (u.lod.z > 0.5) {
        int2 lp = int2(floor(in.position.xy)) & int2(3);
        int rank = ((lp.x & 1) ^ (lp.y & 1))*8 + (lp.y & 1)*4 +
                   ((lp.x>>1) ^ (lp.y>>1))*2 + (lp.y>>1);
        float coverage = (float(rank)+0.5)/16.0;
        if (u.lod.y > 0.5 ? coverage < u.lod.x : coverage >= u.lod.x)
            discard_fragment();
    }
    float2 uv = in.position.xy/u.params.xy;
    if (in.position.z > tex0.sample(tex0_s,uv) + 0.0015)
        discard_fragment();
    return float4(u.params.z,0.0,0.0,1.0);
}
)";

// Rigid alpha-cutout / dissolving geometry: unlike the conservative
// untextured fallback, replay the actual base alpha threshold, UV transform,
// vertex alpha and object-space dissolve noise. This keeps transparent holes
// out of the reactive coverage mask and matches ordinary ModelShaders.
inline const char* kCutoutVS = R"(
layout(location=0) in vec3 a_pos;
layout(location=2) in vec2 a_uv;
layout(location=4) in vec4 a_col;
layout(std140) uniform U0 {
    mat4 mvp; vec4 params; vec4 lod; vec4 uv0; vec4 uv1; vec4 maskInfo;
    mat4 world; vec4 reveal; vec4 revealAxis;
};
out vec2 v_uv;
out vec3 v_opos;
out vec3 v_worldPos;
out float v_alpha;
void main() {
    gl_Position = mvp * vec4(a_pos,1.0);
    vec2 uv = a_uv * uv0.zw;
    float c = cos(uv1.x), si = sin(uv1.x);
    v_uv = vec2(c*uv.x+si*uv.y,-si*uv.x+c*uv.y)+uv0.xy;
    v_opos = a_pos;
    v_worldPos = (world * vec4(a_pos,1.0)).xyz;
    v_alpha = a_col.a;
}
)";
inline const char* kCutoutFS = R"(
layout(std140) uniform U0 {
    mat4 mvp; vec4 params; vec4 lod; vec4 uv0; vec4 uv1; vec4 maskInfo;
    mat4 world; vec4 reveal; vec4 revealAxis;
};
uniform sampler2D tex0;
uniform sampler2D tex1;
in vec2 v_uv;
in vec3 v_opos;
in vec3 v_worldPos;
in float v_alpha;
out vec4 o_color;
float reactiveHash3(vec3 p) {
    p=fract(p*0.3183099+0.1);p*=17.0;
    return fract(p.x*p.y*p.z*(p.x+p.y+p.z));
}
float reactiveNoise3(vec3 x) {
    vec3 i=floor(x),f=fract(x);
    f=f*f*(3.0-2.0*f);
    return mix(mix(mix(reactiveHash3(i),reactiveHash3(i+vec3(1,0,0)),f.x),
                   mix(reactiveHash3(i+vec3(0,1,0)),reactiveHash3(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(reactiveHash3(i+vec3(0,0,1)),reactiveHash3(i+vec3(1,0,1)),f.x),
                   mix(reactiveHash3(i+vec3(0,1,1)),reactiveHash3(i+vec3(1,1,1)),f.x),f.y),f.z);
}
void main() {
    if(lod.z>0.5) {
        ivec2 lp=ivec2(floor(gl_FragCoord.xy)) & ivec2(3);
        int rank=((lp.x&1)^(lp.y&1))*8+(lp.y&1)*4+
                 ((lp.x>>1)^(lp.y>>1))*2+(lp.y>>1);
        float noise=(float(rank)+0.5)/16.0;
        if(lod.y>0.5 ? noise<lod.x : noise>=lod.x) discard;
    }
    if(reveal.w>0.0) {
        vec3 delta=v_worldPos-reveal.xyz;
        float z=dot(delta,revealAxis.xyz);
        float radius=reveal.w*max(0.01,1.0-z*revealAxis.w);
        float radial=length(delta-revealAxis.xyz*z);
        float coverage=(1.0-smoothstep(radius*0.48,radius,radial))*
                       smoothstep(0.04,0.24,z);
        ivec2 ap=ivec2(gl_FragCoord.xy)&ivec2(3);
        int rank=((ap.x&1)^(ap.y&1))*8+(ap.y&1)*4+
                 ((ap.x>>1)^(ap.y>>1))*2+(ap.y>>1);
        if(coverage>(float(rank)+0.5)/16.0)discard;
    }
    if(maskInfo.w>0.0) {
        float nz=reactiveNoise3(v_opos*7.0)*0.7+
                 reactiveNoise3(v_opos*19.0)*0.3;
        if(nz<maskInfo.w)discard;
    }
    if(maskInfo.z>=0.0) {
        float alpha=texture(tex1,v_uv).a*maskInfo.x;
        if(maskInfo.y>0.5)alpha*=v_alpha;
        if(alpha<maskInfo.z)discard;
    }
    vec2 uv=gl_FragCoord.xy/params.xy;
    if(gl_FragCoord.z>texture(tex0,uv).r+0.0015)discard;
    o_color=vec4(params.z,0.0,0.0,1.0);
}
)";
inline const char* kCutoutMSL = R"(
#include <metal_stdlib>
using namespace metal;
struct U0 { float4x4 mvp; float4 params,lod,uv0,uv1,maskInfo;
            float4x4 world;float4 reveal,revealAxis; };
struct VIn {
    float3 pos [[attribute(0)]];
    float2 uv [[attribute(2)]];
    float4 col [[attribute(4)]];
};
struct VOut {
    float4 position [[position]];
    float2 uv; float3 opos; float3 wpos; float alpha;
};
vertex VOut vs_main(VIn in [[stage_in]],constant U0& u [[buffer(1)]]) {
    VOut out;
    out.position=u.mvp*float4(in.pos,1.0);
    float2 uv=in.uv*u.uv0.zw;
    float c=cos(u.uv1.x),si=sin(u.uv1.x);
    out.uv=float2(c*uv.x+si*uv.y,-si*uv.x+c*uv.y)+u.uv0.xy;
    out.opos=in.pos;out.wpos=(u.world*float4(in.pos,1.0)).xyz;
    out.alpha=in.col.a;
    return out;
}
static float reactiveHash3(float3 p) {
    p=fract(p*0.3183099+0.1);p*=17.0;
    return fract(p.x*p.y*p.z*(p.x+p.y+p.z));
}
static float reactiveNoise3(float3 x) {
    float3 i=floor(x),f=fract(x);
    f=f*f*(3.0-2.0*f);
    return mix(mix(mix(reactiveHash3(i),reactiveHash3(i+float3(1,0,0)),f.x),
                   mix(reactiveHash3(i+float3(0,1,0)),reactiveHash3(i+float3(1,1,0)),f.x),f.y),
               mix(mix(reactiveHash3(i+float3(0,0,1)),reactiveHash3(i+float3(1,0,1)),f.x),
                   mix(reactiveHash3(i+float3(0,1,1)),reactiveHash3(i+float3(1,1,1)),f.x),f.y),f.z);
}
fragment float4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],
                         depth2d<float> tex0 [[texture(0)]],sampler s0 [[sampler(0)]],
                         texture2d<float> tex1 [[texture(1)]],sampler s1 [[sampler(1)]]) {
    if(u.lod.z>0.5) {
        int2 lp=int2(floor(in.position.xy)) & int2(3);
        int rank=((lp.x&1)^(lp.y&1))*8+(lp.y&1)*4+
                 ((lp.x>>1)^(lp.y>>1))*2+(lp.y>>1);
        float noise=(float(rank)+0.5)/16.0;
        if(u.lod.y>0.5 ? noise<u.lod.x : noise>=u.lod.x)discard_fragment();
    }
    if(u.reveal.w>0.0) {
        float3 delta=in.wpos-u.reveal.xyz;
        float z=dot(delta,u.revealAxis.xyz);
        float radius=u.reveal.w*max(0.01,1.0-z*u.revealAxis.w);
        float radial=length(delta-u.revealAxis.xyz*z);
        float coverage=(1.0-smoothstep(radius*0.48,radius,radial))*
                       smoothstep(0.04,0.24,z);
        int2 ap=int2(floor(in.position.xy))&int2(3);
        int rank=((ap.x&1)^(ap.y&1))*8+(ap.y&1)*4+
                 ((ap.x>>1)^(ap.y>>1))*2+(ap.y>>1);
        if(coverage>(float(rank)+0.5)/16.0)discard_fragment();
    }
    if(u.maskInfo.w>0.0) {
        float nz=reactiveNoise3(in.opos*7.0)*0.7+
                 reactiveNoise3(in.opos*19.0)*0.3;
        if(nz<u.maskInfo.w)discard_fragment();
    }
    if(u.maskInfo.z>=0.0) {
        float alpha=tex1.sample(s1,in.uv).a*u.maskInfo.x;
        if(u.maskInfo.y>0.5)alpha*=in.alpha;
        if(alpha<u.maskInfo.z)discard_fragment();
    }
    float2 uv=in.position.xy/u.params.xy;
    if(in.position.z>tex0.sample(s0,uv).r+0.0015)discard_fragment();
    return float4(u.params.z,0.0,0.0,1.0);
}
)";

// The normal motion shader stays unchanged. Only the REACTIVE variant
// gets UV/vertex-alpha/current deformed model coordinates. For animated
// skin/baked models this is the exact post-deformation local position that
// ModelShaders uses as its v_opos for object-space dissolve.
inline std::string animatedVertexGlsl(const char* source, bool baked) {
    if (!source) return {};
    std::string code(source);
    const auto main = code.find("void main()");
    if (main == std::string::npos) return {};
    code.insert(main, R"(
layout(location=2) in vec2 a_uv;
layout(location=4) in vec4 a_col;
out vec3 v_reactiveObjectPos;
out vec2 v_reactiveUv;
out float v_reactiveAlpha;
)");
    const std::string original = baked
        ? "gl_Position=currentMvp*bakedPoint(local,currentAnim,false);"
        : "gl_Position=currentMvp*deformPoint(local,false);";
    const auto at = code.find(original);
    if (at == std::string::npos) return {};
    const std::string replacement = baked
        ? "vec4 reactivePose=bakedPoint(local,currentAnim,false);\n    gl_Position=currentMvp*reactivePose;"
        : "vec4 reactivePose=deformPoint(local,false);\n    gl_Position=currentMvp*reactivePose;";
    code.replace(at,original.size(),replacement);
    const auto after = code.find("    v_prevClip=",at);
    if (after == std::string::npos) return {};
    code.insert(after, R"(
    v_reactiveObjectPos=reactivePose.xyz;
    v_reactiveUv=a_uv;
    v_reactiveAlpha=a_col.a;
)");
    return code;
}

// U1: stable std140 RGBA8 fragment params. U0/U3/U4/U5 are inherited from
// the unchanged motion vertex stage, and their RHI binding slots are intact.
// U1 represents the current draw's material and actor reveal, NOT history.
// coverage = (lod, uv transform offset/scale, rotation, alpha material,
//             world matrix, actor reveal center/radius and camera axis).
inline const char* kAnimatedFS = R"(
layout(std140) uniform U0 {
    mat4 currentMvp;
    mat4 previousMvp;
    vec4 params;
};
layout(std140) uniform U1 {
    vec4 reactiveLod;
    vec4 reactiveUv0;
    vec4 reactiveUv1;
    vec4 reactiveMaskInfo;
    mat4 reactiveWorld;
    vec4 reactiveReveal;
    vec4 reactiveRevealAxis;
};
uniform sampler2D tex0;
uniform sampler2D tex1;
in vec4 v_prevClip;
in vec3 v_reactiveObjectPos;
in vec2 v_reactiveUv;
in float v_reactiveAlpha;
out vec4 o_color;
float reactiveAnimHash3(vec3 p) {
    p=fract(p*0.3183099+0.1);p*=17.0;
    return fract(p.x*p.y*p.z*(p.x+p.y+p.z));
}
float reactiveAnimNoise3(vec3 x) {
    vec3 i=floor(x),f=fract(x);
    f=f*f*(3.0-2.0*f);
    return mix(mix(mix(reactiveAnimHash3(i),reactiveAnimHash3(i+vec3(1,0,0)),f.x),
                   mix(reactiveAnimHash3(i+vec3(0,1,0)),reactiveAnimHash3(i+vec3(1,1,0)),f.x),f.y),
               mix(mix(reactiveAnimHash3(i+vec3(0,0,1)),reactiveAnimHash3(i+vec3(1,0,1)),f.x),
                   mix(reactiveAnimHash3(i+vec3(0,1,1)),reactiveAnimHash3(i+vec3(1,1,1)),f.x),f.y),f.z);
}
void main() {
    // Match the current frame's ordinary material screen-space LOD.
    if(reactiveLod.z>0.5) {
        ivec2 lp=ivec2(floor(gl_FragCoord.xy))&ivec2(3);
        int rank=((lp.x&1)^(lp.y&1))*8+(lp.y&1)*4+
                 ((lp.x>>1)^(lp.y>>1))*2+(lp.y>>1);
        float noise=(float(rank)+0.5)/16.0;
        if(reactiveLod.y>0.5 ? noise<reactiveLod.x : noise>=reactiveLod.x)
            discard;
    }
    if(reactiveReveal.w>0.0) {
        vec3 world=(reactiveWorld*vec4(v_reactiveObjectPos,1.0)).xyz;
        vec3 delta=world-reactiveReveal.xyz;
        float z=dot(delta,reactiveRevealAxis.xyz);
        float radius=reactiveReveal.w*max(0.01,1.0-z*reactiveRevealAxis.w);
        float radial=length(delta-reactiveRevealAxis.xyz*z);
        float coverage=(1.0-smoothstep(radius*0.48,radius,radial))*
                       smoothstep(0.04,0.24,z);
        ivec2 ap=ivec2(gl_FragCoord.xy)&ivec2(3);
        int rank=((ap.x&1)^(ap.y&1))*8+(ap.y&1)*4+
                 ((ap.x>>1)^(ap.y>>1))*2+(ap.y>>1);
        if(coverage>(float(rank)+0.5)/16.0)discard;
    }
    if(reactiveMaskInfo.w>0.0) {
        float nz=reactiveAnimNoise3(v_reactiveObjectPos*7.0)*0.7+
                 reactiveAnimNoise3(v_reactiveObjectPos*19.0)*0.3;
        if(nz<reactiveMaskInfo.w)discard;
    }
    if(reactiveMaskInfo.z>=0.0) {
        vec2 uv0=v_reactiveUv*reactiveUv0.zw;
        float c=cos(reactiveUv1.x),si=sin(reactiveUv1.x);
        vec2 uv=vec2(c*uv0.x+si*uv0.y,-si*uv0.x+c*uv0.y)+reactiveUv0.xy;
        float alpha=texture(tex1,uv).a*reactiveMaskInfo.x;
        if(reactiveMaskInfo.y>0.5)alpha*=v_reactiveAlpha;
        if(alpha<reactiveMaskInfo.z)discard;
    }
    vec2 uv=gl_FragCoord.xy/params.xy;
    if(gl_FragCoord.z>texture(tex0,uv).r+0.0015)discard;
    o_color=vec4(1.0,0.0,0.0,1.0);
}
)";

// Patch only an explicitly built REACTIVE variant of the source MSL.
// If a future motion shader changes its ABI, refuse to create a broken
// shader silently. The caller's existing pipeline remains unchanged.
inline std::string animatedMsl(const char* motionMsl, bool baked) {
    if (!motionMsl) return {};
    std::string code(motionMsl);
    const auto replaceOnce = [&](const std::string& from,
                                 const std::string& to) {
        const auto pos=code.find(from);
        if(pos==std::string::npos) return false;
        code.replace(pos,from.size(),to);
        return true;
    };
    const std::string inputAnchor = baked
        ? "float4 weights [[attribute(6)]];};"
        : "float4 weights [[attribute(6)]];\n};";
    if(!replaceOnce(inputAnchor,
        "float4 weights [[attribute(6)]];\n"
        "    float2 uv [[attribute(2)]];\n"
        "    float4 col [[attribute(4)]];\n};")) return {};
    const std::string outputAnchor = baked
        ? "struct VOut { float4 position [[position]];float4 previous; };"
        : "struct VOut {float4 position [[position]];float4 previous;};";
    if(!replaceOnce(outputAnchor,
        "struct VOut {float4 position [[position]];float4 previous;"
        "float3 objectPos;float2 uv;float alpha;};")) return {};
    const std::string current = baked
        ? "o.position=u.currentMvp*bakedPoint(\n"
          "        local,in,anim.current,false,u,palette,mo,tex7,tex8,vid);"
        : "o.position=u.currentMvp*deformPoint(local,in,false,u,current,previous,mo,tex8,vid);";
    const std::string replacement = baked
        ? "const float4 reactivePose=bakedPoint(\n"
          "        local,in,anim.current,false,u,palette,mo,tex7,tex8,vid);\n"
          "    o.position=u.currentMvp*reactivePose;"
        : "const float4 reactivePose=deformPoint(local,in,false,u,current,previous,mo,tex8,vid);\n"
          "    o.position=u.currentMvp*reactivePose;";
    if(!replaceOnce(current,replacement))return {};
    const auto prior=code.find("    o.previous=",code.find("const float4 reactivePose="));
    if(prior==std::string::npos)return {};
    code.insert(prior,
        "    o.objectPos=reactivePose.xyz;o.uv=in.uv;o.alpha=in.col.a;\n");
    const auto fragment = code.find("fragment float4 fs_main");
    if (fragment == std::string::npos) return {};
    code.resize(fragment);
    code += R"(
struct ReactiveCoverageU1 {
    float4 lod,uv0,uv1,maskInfo;
    float4x4 world;
    float4 reveal,revealAxis;
};
static float reactiveAnimHash3(float3 p) {
    p=fract(p*0.3183099+0.1);p*=17.0;
    return fract(p.x*p.y*p.z*(p.x+p.y+p.z));
}
static float reactiveAnimNoise3(float3 x) {
    float3 i=floor(x),f=fract(x);
    f=f*f*(3.0-2.0*f);
    return mix(mix(mix(reactiveAnimHash3(i),reactiveAnimHash3(i+float3(1,0,0)),f.x),
                   mix(reactiveAnimHash3(i+float3(0,1,0)),reactiveAnimHash3(i+float3(1,1,0)),f.x),f.y),
               mix(mix(reactiveAnimHash3(i+float3(0,0,1)),reactiveAnimHash3(i+float3(1,0,1)),f.x),
                   mix(reactiveAnimHash3(i+float3(0,1,1)),reactiveAnimHash3(i+float3(1,1,1)),f.x),f.y),f.z);
}
fragment float4 fs_main(VOut in [[stage_in]],
    constant U0& u [[buffer(1)]],
    constant ReactiveCoverageU1& coverage [[buffer(2)]],
    depth2d<float> tex0 [[texture(0)]],
    sampler tex0_s [[sampler(0)]],
    texture2d<float> tex1 [[texture(1)]],
    sampler tex1_s [[sampler(1)]]) {
    if(coverage.lod.z>0.5) {
        int2 lp=int2(floor(in.position.xy))&int2(3);
        int rank=((lp.x&1)^(lp.y&1))*8+(lp.y&1)*4+
                 ((lp.x>>1)^(lp.y>>1))*2+(lp.y>>1);
        float noise=(float(rank)+0.5)/16.0;
        if(coverage.lod.y>0.5 ? noise<coverage.lod.x : noise>=coverage.lod.x)
            discard_fragment();
    }
    if(coverage.reveal.w>0.0) {
        float3 world=(coverage.world*float4(in.objectPos,1.0)).xyz;
        float3 delta=world-coverage.reveal.xyz;
        float z=dot(delta,coverage.revealAxis.xyz);
        float radius=coverage.reveal.w*max(0.01,1.0-z*coverage.revealAxis.w);
        float radial=length(delta-coverage.revealAxis.xyz*z);
        float sample=(1.0-smoothstep(radius*0.48,radius,radial))*
                     smoothstep(0.04,0.24,z);
        int2 ap=int2(floor(in.position.xy))&int2(3);
        int rank=((ap.x&1)^(ap.y&1))*8+(ap.y&1)*4+
                 ((ap.x>>1)^(ap.y>>1))*2+(ap.y>>1);
        if(sample>(float(rank)+0.5)/16.0)discard_fragment();
    }
    if(coverage.maskInfo.w>0.0) {
        float nz=reactiveAnimNoise3(in.objectPos*7.0)*0.7+
                 reactiveAnimNoise3(in.objectPos*19.0)*0.3;
        if(nz<coverage.maskInfo.w)discard_fragment();
    }
    if(coverage.maskInfo.z>=0.0) {
        float2 uv0=in.uv*coverage.uv0.zw;
        float c=cos(coverage.uv1.x),si=sin(coverage.uv1.x);
        float2 uv=float2(c*uv0.x+si*uv0.y,-si*uv0.x+c*uv0.y)+coverage.uv0.xy;
        float alpha=tex1.sample(tex1_s,uv).a*coverage.maskInfo.x;
        if(coverage.maskInfo.y>0.5)alpha*=in.alpha;
        if(alpha<coverage.maskInfo.z)discard_fragment();
    }
    const float2 uv=in.position.xy/u.params.xy;
    if(in.position.z>tex0.sample(tex0_s,uv)+0.0015)discard_fragment();
    return float4(1.0,0.0,0.0,1.0);
}
)";
    return code;
}

// Dither and actor-aperture patterns are copied from ModelShaders.
// U1 is per reactive draw (material/LOD/reveal) and is never a temporal
// history. Rigid and animated variants encode only current-frame coverage.
} // namespace aiverse::render::reactive_shaders
