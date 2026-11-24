// Analytic spherical volume particles: perspective ray/sphere integration.
// Compact particle records are only conservative raster proxies.
#pragma once
namespace aiverse::spectra::volumetric_particles {
inline constexpr const char* metal=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Record {float x,y,z,u,v;uint rgba;float g0,g1,g2,g3;};
struct U0 {float4x4 viewProj;float4 fog,camPos,params,right,up;};
struct VOut {float4 pos [[position]];float3 world;float4 sphere [[flat]];float4 color [[flat]];};
vertex VOut vs_main(uint vertexIndex [[vertex_id]],uint instance [[instance_id]],
 device const Record* records [[buffer(0)]],constant U0& u [[buffer(1)]]){
 Record p=records[instance];float3 center=float3(p.x,p.y,p.z);
 float r=max(p.u,0.0001f);float3 delta=center-u.camPos.xyz;float d=length(delta);
 float4 color=float4(float(p.rgba&255u),float((p.rgba>>8)&255u),float((p.rgba>>16)&255u),float(p.rgba>>24))/255.0f;
 VOut o;o.sphere=float4(center,r);o.color=color;
 if(d<=r*1.01f||color.a<=0.0f){o.pos=float4(2,2,2,1);o.world=center;return o;}
 float3 n=delta/d;float3 ref=abs(n.y)<0.95f?float3(0,1,0):float3(1,0,0);
 float3 right=normalize(cross(ref,n)),up=cross(n,right);
 const float2 corners[4]={float2(-1,-1),float2(1,-1),float2(1,1),float2(-1,1)};
 float extent=r*d/sqrt(d*d-r*r);float2 q=corners[vertexIndex&3u];
 o.world=center+extent*(right*q.x+up*q.y);o.pos=u.viewProj*float4(o.world,1);return o;
}
fragment half4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]]){
 float3 rd=normalize(in.world-u.camPos.xyz),oc=u.camPos.xyz-in.sphere.xyz;
 float r=in.sphere.w,b=dot(oc,rd),disc=b*b-dot(oc,oc)+r*r;
 if(disc<=0.0f||in.color.a<=0.0f)discard_fragment();
 float t0=max(0.0f,-b-sqrt(disc)),t1=-b+sqrt(disc);
 if(t1<=t0)discard_fragment();
 uint steps=uint(clamp(u.params.z,4.0f,32.0f));float ds=(t1-t0)/float(steps);
 float trans=1.0f;float3 rgb=float3(0);
 float3 color=in.color.rgb;
#if AIVERSE_HDR
 color=rtsSrgbDecode(color);
#endif
 for(uint i=0;i<steps;++i){
  float3 q=(oc+rd*(t0+(float(i)+0.5f)*ds))/r;
  float density=max(0.0f,1.0f-dot(q,q))*u.params.x*in.color.a;
  float alpha=1.0f-exp(-density*ds);
  float light=0.35f+0.65f*clamp(0.5f+0.5f*q.y,0.0f,1.0f);
  rgb+=trans*alpha*color*(light+u.params.y);trans*=1.0f-alpha;
 }
 float fog=1.0f-exp(-length(in.sphere.xyz-u.camPos.xyz)*u.fog.w);
 rgb=mix(rgb,u.fog.rgb*(1.0f-trans),fog);
 return half4(float4(rgb,1.0f-trans));
}
)MSL";
inline constexpr const char* vertex=R"GLSL(
layout(std140) uniform U0 {mat4 u_viewProj;vec4 u_fog;vec4 u_camPos;vec4 u_params;vec4 u_right;vec4 u_up;};
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_size;
layout(location=2) in vec4 a_col;
out highp vec3 v_world;flat out highp vec4 v_sphere;flat out highp vec4 v_color;
void main(){
 float r=max(a_size.x,0.0001);vec3 delta=a_pos-u_camPos.xyz;float d=length(delta);
 v_sphere=vec4(a_pos,r);v_color=a_col;v_world=a_pos;
 if(d<=r*1.01||a_col.a<=0.0){gl_Position=vec4(2,2,2,1);return;}
 vec3 n=delta/d,ref=abs(n.y)<0.95?vec3(0,1,0):vec3(1,0,0);
 vec3 right=normalize(cross(ref,n)),up=cross(n,right);
 const vec2 corners[4]=vec2[4](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,1));
 vec2 q=corners[gl_VertexID&3];float extent=r*d/sqrt(d*d-r*r);
 v_world=a_pos+extent*(right*q.x+up*q.y);gl_Position=u_viewProj*vec4(v_world,1);
}
)GLSL";
inline constexpr const char* fragment=R"GLSL(
precision highp float;
layout(std140) uniform U0 {mat4 u_viewProj;vec4 u_fog;vec4 u_camPos;vec4 u_params;vec4 u_right;vec4 u_up;};
in highp vec3 v_world;flat in highp vec4 v_sphere;flat in highp vec4 v_color;
out highp vec4 o_color;
void main(){
 vec3 rd=normalize(v_world-u_camPos.xyz),oc=u_camPos.xyz-v_sphere.xyz;
 float r=v_sphere.w,b=dot(oc,rd),disc=b*b-dot(oc,oc)+r*r;
 if(disc<=0.0||v_color.a<=0.0)discard;
 float t0=max(0.0,-b-sqrt(disc)),t1=-b+sqrt(disc);if(t1<=t0)discard;
 int steps=int(clamp(u_params.z,4.0,32.0));float ds=(t1-t0)/float(steps);
 float trans=1.0;vec3 rgb=vec3(0),color=v_color.rgb;
#if AIVERSE_HDR
 color=rtsSrgbDecode(color);
#endif
 for(int i=0;i<steps;++i){
  vec3 q=(oc+rd*(t0+(float(i)+0.5)*ds))/r;
  float density=max(0.0,1.0-dot(q,q))*u_params.x*v_color.a;
  float alpha=1.0-exp(-density*ds);
  float light=0.35+0.65*clamp(0.5+0.5*q.y,0.0,1.0);
  rgb+=trans*alpha*color*(light+u_params.y);trans*=1.0-alpha;
 }
 float fog=1.0-exp(-length(v_sphere.xyz-u_camPos.xyz)*u_fog.w);
 o_color=vec4(mix(rgb,u_fog.rgb*(1.0-trans),fog),1.0-trans);
}
)GLSL";
}
