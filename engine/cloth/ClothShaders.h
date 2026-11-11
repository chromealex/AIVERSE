#pragma once
namespace aiverse::cloth {
inline constexpr const char* kVertex=R"(
precision highp float;
precision highp int;
layout(location=0) in vec2 uv;
layout(std140) uniform U0 {mat4 vp;vec4 config;};
uniform highp sampler2D tex0;
uniform highp sampler2D tex1;
out vec3 world;
out vec3 tint;
out vec2 clothUV;
void main(){
    int n=int(config.x),cols=int(config.y),id=gl_InstanceID;
    vec4 instance=texelFetch(tex1,ivec2(id,0),0);int tileId=int(instance.w);
    vec2 g=uv*float(n-1);ivec2 c=min(ivec2(g),ivec2(n-2));vec2 f=g-vec2(c);
    ivec2 tile=ivec2(tileId%cols,tileId/cols)*n+c;
    vec3 a=texelFetch(tex0,tile,0).xyz,b=texelFetch(tex0,tile+ivec2(1,0),0).xyz;
    vec3 d=texelFetch(tex0,tile+ivec2(0,1),0).xyz,e=texelFetch(tex0,tile+ivec2(1,1),0).xyz;
    world=mix(mix(a,b,f.x),mix(d,e,f.x),f.y)+instance.xyz;
    tint=0.5+0.3*cos(vec3(0,2,4)+float(id)*1.7);clothUV=uv;
    gl_Position=vp*vec4(world,1);
})";
inline constexpr const char* kFragment=R"(
precision highp float;
in vec3 world;
in vec3 tint;
in vec2 clothUV;
out vec4 frag;
void main(){
    vec3 n=normalize(cross(dFdx(world),dFdy(world)));
    float light=0.3+0.7*abs(dot(n,normalize(vec3(0.4,0.8,0.6))));
    float check=mod(floor(clothUV.x*12.0)+floor(clothUV.y*12.0),2.0);
    vec3 c=tint*light*mix(0.7,1.0,check);
    frag=vec4(c,1);
})";
inline constexpr const char* kMetal=R"(
#include <metal_stdlib>
using namespace metal;
struct Uniforms {float4x4 vp;float4 config;};
struct Vertex {float2 uv [[attribute(0)]];};
struct Varying {float4 position [[position]];float3 world;float3 tint;float2 clothUV;};
vertex Varying vs_main(Vertex in [[stage_in]],uint id [[instance_id]],constant Uniforms& u [[buffer(1)]],texture2d<float> tex0 [[texture(0)]],texture2d<float> tex1 [[texture(1)]]){
    uint n=uint(u.config.x),cols=uint(u.config.y);
    float4 instance=tex1.read(uint2(id,0));uint tileId=uint(instance.w);
    float2 g=in.uv*float(n-1);uint2 c=min(uint2(g),uint2(n-2));float2 f=g-float2(c);
    uint2 tile=uint2(tileId%cols,tileId/cols)*n+c;
    float3 a=tex0.read(tile).xyz,b=tex0.read(tile+uint2(1,0)).xyz;
    float3 d=tex0.read(tile+uint2(0,1)).xyz,e=tex0.read(tile+uint2(1,1)).xyz;
    Varying o;o.world=mix(mix(a,b,f.x),mix(d,e,f.x),f.y)+instance.xyz;
    o.tint=0.5+0.3*cos(float3(0,2,4)+float(id)*1.7);o.clothUV=in.uv;
    o.position=u.vp*float4(o.world,1);return o;
}
fragment float4 fs_main(Varying in [[stage_in]]){
    float3 n=normalize(cross(dfdx(in.world),dfdy(in.world)));
    float light=0.3+0.7*abs(dot(n,normalize(float3(0.4,0.8,0.6))));
    float check=fmod(floor(in.clothUV.x*12.0)+floor(in.clothUV.y*12.0),2.0);
    return float4(in.tint*light*mix(0.7,1.0,check),1);
})";
}
