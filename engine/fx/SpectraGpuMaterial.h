// SpectraFX Stage 27: opt-in bounded GPU particle shaderGraph subset.
// It reuses render::graph::compile(), and never translates untrusted strings
// directly into shader source. Unsupported graphs retain the complete CPU FX.
#pragma once
#include "engine/render/graph/ShaderGraph.h"
#include "engine/data/Json.h"
#include <array>
#include <cstdint>
#include <exception>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace aiverse::spectra::gpu_material {

struct Material {
    render::graph::Program compiled;
    std::array<uint32_t,4> sources{0,1,3,3};
};

// Packed GPU vertex graphData source selector:
// 0 age, 1 lifetime, 2 reserved stable UID random (GPU unsupported),
// 3 zero, 4..11 custom attribute0..7, 12..19 instance param0..7.
inline bool decodeSources(const Json& inputs, std::array<uint32_t,4>& out,
                          std::string& reason) {
    out={0,1,3,3};
    if(inputs.isNull()) {reason.clear();return true;}
    if(!inputs.isArray() || inputs.size()!=4) {
        reason="GPU material graphInputs requires exactly four source strings";
        return false;
    }
    for(size_t i=0;i<4;++i) {
        if(!inputs[i].isString()) {
            reason="GPU material source must be a string";
            return false;
        }
        const std::string name=inputs[i].str();
        if(name=="age")out[i]=0;
        else if(name=="lifetime")out[i]=1;
        else if(name=="zero")out[i]=3;
        else if(name=="random") {
            // CPU's stable prand(uid) has no equivalent in the fixed Metal
            // particle state. Refuse instead of drawing a different effect.
            reason="stable particle random source is not implemented in GPU VM";
            return false;
        } else {
            bool found=false;
            for(uint32_t slot=0;slot<8;++slot) {
                if(name=="attribute"+std::to_string(slot)) {out[i]=4+slot;found=true;break;}
                if(name=="param"+std::to_string(slot)) {out[i]=12+slot;found=true;break;}
            }
            if(!found) {
                reason="unsupported GPU material graph input '"+name+"'";
                return false;
            }
        }
    }
    reason.clear();
    return true;
}

inline bool compile(const Json& graph, const Json& inputs, Material& output,
                    std::string& reason) {
    output={};
    if(!graph.isObject() || graph.getInt("version")!=1 ||
       graph.getStr("domain")!="particle") {
        reason="GPU material needs a particle-domain shaderGraph version 1";
        return false;
    }
    const Json& nodes=graph["nodes"];
    if(!nodes.isArray()||nodes.size()==0||nodes.size()>32) {
        reason="GPU material graph supports 1..32 pure fragment nodes";
        return false;
    }
    if(!graph["outputs"].isObject()||graph["outputs"].size()!=1 ||
       !graph["outputs"]["color"].isString()) {
        reason="GPU material supports only the fragment color output";
        return false;
    }
    // Explicitly bounded shader graph operations. Unsupported nodes cannot
    // silently turn into different GPU materials: they force CPU fallback.
    static const std::set<std::string> allowed={
        "constant","parameter","input","uv","texture","vertexColor",
        "particleAge","particleLifetime","particleAge01","particleRandom",
        "add","subtract","multiply","min","max","clamp","oneMinus","mix",
        "red","green","blue","alpha","combine","setAlpha"
    };
    for(const Json& node:nodes.items()) {
        const std::string op=node.getStr("op");
        if(!allowed.count(op)) {
            reason="GPU material does not support graph node '"+op+"'";
            return false;
        }
        if(op=="particleRandom" && inputs.isNull()) {
            reason="GPU material needs explicit graphInputs for the particleRandom lane";
            return false;
        }
    }
    std::array<uint32_t,4> mapping{};
    if(!decodeSources(inputs,mapping,reason))return false;
    try {
        render::graph::Program compiled=render::graph::compile(graph,"particle");
        if(!compiled.textures.empty() || !compiled.vertexMsl.empty() ||
           !compiled.vertexGlsl.empty() || compiled.samples>1 ||
           compiled.msl.size()>16384 || compiled.glsl.size()>16384 ||
           compiled.outputs.count("color")!=1) {
            reason="GPU material requires one builtin sprite sample, no extra textures or vertex graph";
            return false;
        }
        output.compiled=std::move(compiled);
        output.sources=mapping;
        reason.clear();
        return true;
    } catch(const std::exception& ex) {
        reason=std::string("GPU material graph compile: ")+ex.what();
        return false;
    }
}

// Full shader source generated exclusively from trusted templates and
// render::graph::Program, never by string interpolation of raw user code.
struct Shaders {std::string glslVertex,glslFragment,msl;};
inline Shaders makeShaders(const Material& material) {
    Shaders s;
    s.glslVertex=R"GLSL(
layout(std140) uniform U0 { mat4 u_viewProj; vec4 u_fog; vec4 u_camPos; vec4 u_params; };
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_uv;
layout(location=2) in vec4 a_col;
layout(location=3) in vec4 a_graph;
out mediump vec2 v_uv;
out lowp vec4 v_col;
out highp vec4 v_graph;
out highp vec3 v_world;
out highp vec4 v_clip;
void main(){
    gl_Position=u_viewProj*vec4(a_pos,1.0);
    float f=1.0-exp(-length(a_pos-u_camPos.xyz)*u_fog.w);
    vec4 c=a_col;
    c.rgb=mix(c.rgb,mix(u_fog.rgb*mix(1.0,c.a,u_params.y),vec3(1.0),u_params.z),f*(1.0-u_params.x));
    c.a*=1.0-f*u_params.x;
    v_col=c;v_uv=a_uv;v_graph=a_graph;v_world=a_pos;v_clip=gl_Position;
}
)GLSL";
    s.glslFragment=R"GLSL(
precision highp float;
layout(std140) uniform U0 { highp mat4 u_viewProj; highp vec4 u_fog; highp vec4 u_camPos; highp vec4 u_params; };
uniform lowp sampler2D tex0;
in mediump vec2 v_uv;
in lowp vec4 v_col;
in highp vec4 v_graph;
in highp vec3 v_world;
in highp vec4 v_clip;
out vec4 o_color;
#define SG_SAMPLE(uv) texture(tex0,uv)
void main(){
    vec2 sg_uv=v_uv,sg_screen=v_clip.xy/max(abs(v_clip.w),0.00001)*0.5+0.5;
    vec3 sg_world=v_world,sg_view=u_camPos.xyz-v_world;
    vec4 sg_color=v_col,sg_particle=v_graph;
    float sg_time=0.0;
    vec4 sg_input=texture(tex0,v_uv)*v_col;
)GLSL";
    s.glslFragment+=material.compiled.glsl;
    s.glslFragment+="o_color="+material.compiled.outputs.at("color")+";\n}\n";

    s.msl=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct U0 {float4x4 viewProj; float4 fog; float4 camPos; float4 params;};
struct VIn {float3 pos [[attribute(0)]]; float2 uv [[attribute(1)]];
            float4 col [[attribute(2)]]; float4 graph [[attribute(3)]];};
struct VOut {float4 pos [[position]];float2 uv;half4 col;
             float4 graph;float3 world;float4 clip;};
vertex VOut vs_main(VIn in [[stage_in]],constant U0& u [[buffer(1)]]) {
    VOut o;
    o.pos=u.viewProj*float4(in.pos,1.0);
    float f=1.0-exp(-length(in.pos-u.camPos.xyz)*u.fog.w);
    float4 c=in.col;
    c.rgb=mix(c.rgb,mix(u.fog.rgb*mix(1.0,c.a,u.params.y),float3(1.0),u.params.z),f*(1.0-u.params.x));
    c.a*=1.0-f*u.params.x;
    o.col=half4(c);o.uv=in.uv;o.graph=in.graph;
    o.world=in.pos;o.clip=o.pos;
    return o;
}
#define SG_SAMPLE(uv) float4(tex0.sample(s0,uv))
fragment half4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]],
    texture2d<half> tex0 [[texture(0)]],sampler s0 [[sampler(0)]]) {
    float2 sg_uv=in.uv,sg_screen=in.clip.xy/max(abs(in.clip.w),0.00001)*0.5+0.5;
    float3 sg_world=in.world,sg_view=u.camPos.xyz-in.world;
    float4 sg_color=float4(in.col),sg_particle=in.graph;
    float sg_time=0.0;
    float4 sg_input=float4(tex0.sample(s0,in.uv)*in.col);
)MSL";
    s.msl+=material.compiled.msl;
    s.msl+="return half4("+material.compiled.outputs.at("color")+");\n}\n";
    return s;
}
} // namespace aiverse::spectra::gpu_material
