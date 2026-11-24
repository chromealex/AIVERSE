// Analytic volume particles: bounded ray integration with procedural density.
// Compact particle records are only conservative raster proxies.
#pragma once
namespace aiverse::spectra::volumetric_particles {
inline constexpr const char* metal=R"MSL(
#include <metal_stdlib>
using namespace metal;
struct Record {float x,y,z,u,v;uint rgba;float g0,g1,g2,g3;};
struct U0 {float4x4 viewProj;float4 fog,camPos,params,right,up,lightDir,lightColor,lightControl;float4x4 invVP;float4 depthControl,lodControl;};

float hashVolume(float3 p){return fract(sin(dot(p,float3(127.1f,311.7f,74.7f)))*43758.5453f);}
float noiseVolume(float3 p){
 float3 a=floor(p),f=p-a;f=f*f*(3.0f-2.0f*f);
 return mix(mix(mix(hashVolume(a),hashVolume(a+float3(1,0,0)),f.x),
                mix(hashVolume(a+float3(0,1,0)),hashVolume(a+float3(1,1,0)),f.x),f.y),
            mix(mix(hashVolume(a+float3(0,0,1)),hashVolume(a+float3(1,0,1)),f.x),
                mix(hashVolume(a+float3(0,1,1)),hashVolume(a+float3(1,1,1)),f.x),f.y),f.z);
}
float shapeDensity(float3 q,int shape){
 if(shape==2){float3 a=abs(q);return max(0.0f,1.0f-max(a.x,max(a.y,a.z)));}
 if(shape==3)q.y-=clamp(q.y,-1.0f,1.0f);
 return max(0.0f,1.0f-dot(q,q));
}
float volumeDensity(float3 q,float3 center,int shape,float3 noiseSettings){
 float envelope=shapeDensity(q,shape);
 if(envelope<=0.0f||noiseSettings.y<=0.0f)return envelope;
 float3 p=q*noiseSettings.x+fract(center*0.071f)*19.0f;
 float sum=0.0f,weight=0.5f,total=0.0f;
 for(int i=0;i<int(noiseSettings.z);++i){
  sum+=noiseVolume(p)*weight;total+=weight;p=p*2.03f+float3(13.7f,5.1f,9.2f);weight*=0.5f;
 }
 return envelope*mix(1.0f,sum/max(total,0.0001f),noiseSettings.y);
}
float boundRadius(float3 axes,int shape){
 if(shape==2)return length(axes);
 if(shape==3)return max(axes.x,max(axes.y,axes.z))+axes.y;
 return max(axes.x,max(axes.y,axes.z));
}
struct VOut {float4 pos [[position]];float3 world;float4 sphere [[flat]];float4 color [[flat]];float age [[flat]];};
vertex VOut vs_main(uint vertexIndex [[vertex_id]],uint instance [[instance_id]],
 device const Record* records [[buffer(0)]],constant U0& u [[buffer(1)]]){
 Record p=records[instance];float3 center=float3(p.x,p.y,p.z);
 float base=max(p.u,0.0001f);float r=base*boundRadius(u.right.xyz,int(u.params.w));float3 delta=center-u.camPos.xyz;float d=length(delta);
 float4 color=float4(float(p.rgba&255u),float((p.rgba>>8)&255u),float((p.rgba>>16)&255u),float(p.rgba>>24))/255.0f;
 VOut o;o.sphere=float4(center,base);o.color=color;o.age=p.v;
#if SPECTRA_VOLUME_DEPTH
 // Near-plane intersection and orthographic projections use a full-screen
 // proxy. Fragment integration still rejects all rays missing the volume.
 float4 nearPlane=float4(u.viewProj[0].z,u.viewProj[1].z,u.viewProj[2].z,u.viewProj[3].z);
 if(u.depthControl.z>1.5f)nearPlane+=float4(u.viewProj[0].w,u.viewProj[1].w,u.viewProj[2].w,u.viewProj[3].w);
 float3 wrow=float3(u.viewProj[0].w,u.viewProj[1].w,u.viewProj[2].w);
 if(color.a>0.0f&&(d<=r*1.01f||length(wrow)<1e-6f||dot(nearPlane,float4(center,1))<=r*length(nearPlane.xyz))){
  const float2 corners[4]={float2(-1,-1),float2(1,-1),float2(1,1),float2(-1,1)};
  o.pos=float4(corners[vertexIndex&3u],0,1);o.world=center;return o;
 }
#endif
 if(d<=r*1.01f||color.a<=0.0f){o.pos=float4(2,2,2,1);o.world=center;return o;}
 float3 n=delta/d;float3 ref=abs(n.y)<0.95f?float3(0,1,0):float3(1,0,0);
 float3 right=normalize(cross(ref,n)),up=cross(n,right);
 const float2 corners[4]={float2(-1,-1),float2(1,-1),float2(1,1),float2(-1,1)};
 float extent=r*d/sqrt(d*d-r*r);float2 q=corners[vertexIndex&3u];
 o.world=center+extent*(right*q.x+up*q.y);o.pos=u.viewProj*float4(o.world,1);return o;
}
float lodFactor(float3 center,float radius,constant U0& u){
 if(u.lodControl.x<0.5f)return 1.0f;
 float4 clip=u.viewProj*float4(center,1);
 float3 rowW=float3(u.viewProj[0].w,u.viewProj[1].w,u.viewProj[2].w);
 float minW=clip.w-radius*length(rowW);
 if(minW<=max(1e-5f,clip.w*1e-5f))return 1.0f;
 float2 ndc=clip.xy/clip.w;
 float3 rowX=float3(u.viewProj[0].x,u.viewProj[1].x,u.viewProj[2].x);
 float3 rowY=float3(u.viewProj[0].y,u.viewProj[1].y,u.viewProj[2].y);
 float pixels=radius*max(length(rowX-ndc.x*rowW)*u.depthControl.x,length(rowY-ndc.y*rowW)*u.depthControl.y)/minW;
 return smoothstep(u.lodControl.z,u.lodControl.w,pixels);
}
float sampleDensity(float3 q,VOut in,constant U0& u
#if SPECTRA_TEXTURE_VOLUME
 ,texture3d<float> densityTexture,sampler densitySampler
#endif
){
  float density=volumeDensity(q,in.sphere.xyz,int(u.params.w),u.up.xyz)*u.params.x*in.color.a;
#if SPECTRA_TEXTURE_VOLUME
  float n=u.right.w,frames=u.camPos.w;
  float3 uv=clamp(q/float3(1.0f,int(u.params.w)==3?2.0f:1.0f,1.0f)*0.5f+0.5f,0.0f,1.0f);
  float3 texel=uv*(n-1.0f)+0.5f;
  float f=clamp(in.age,0.0f,1.0f)*(frames-1.0f),a=floor(f),b=min(a+1.0f,frames-1.0f);
  float va=densityTexture.sample(densitySampler,float3(texel.xy/n,(texel.z+a*n)/(n*frames)),level(0)).r;
  float vb=densityTexture.sample(densitySampler,float3(texel.xy/n,(texel.z+b*n)/(n*frames)),level(0)).r;
  density*=mix(va,vb,f-a);
#endif
 return density;
}
float phaseHG(float cosTheta,float g){
 float d=max(0.01f,1.0f+g*g-2.0f*g*cosTheta);
 return (1.0f-g*g)/(d*sqrt(d)); // normalized relative to isotropic
}
fragment half4 fs_main(VOut in [[stage_in]],constant U0& u [[buffer(1)]]
#if SPECTRA_TEXTURE_VOLUME
 ,texture3d<float> densityTexture [[texture(0)]],sampler densitySampler [[sampler(0)]]
#endif
#if SPECTRA_VOLUME_DEPTH
 ,depth2d<float,access::read> sceneDepth [[texture(1)]]
#endif
 ){
 float3 rd=normalize(in.world-u.camPos.xyz),oc=u.camPos.xyz-in.sphere.xyz;
 float reach=1e8f;
#if SPECTRA_VOLUME_DEPTH
 uint2 pixel=uint2(in.pos.xy);
 if(pixel.x>=uint(u.depthControl.x)||pixel.y>=uint(u.depthControl.y))discard_fragment();
 float depth=sceneDepth.read(pixel);
 if(!(depth>=0.0f&&depth<=1.0f))discard_fragment();
 float2 screen=(float2(pixel)+0.5f)/u.depthControl.xy;
 if(u.depthControl.w>0.5f)screen.y=1.0f-screen.y;
 float2 ndc=screen*2.0f-1.0f;
 float4 nearH=u.invVP*float4(ndc,1.0f-u.depthControl.z,1);
 float4 endH=u.invVP*float4(ndc,min(depth,0.99999f)*u.depthControl.z+1.0f-u.depthControl.z,1);
 if(abs(nearH.w)<1e-8f||abs(endH.w)<1e-8f)discard_fragment();
 float3 eye=nearH.xyz/nearH.w,end=endH.xyz/endH.w;
 float3 delta=end-eye;reach=length(delta);
 if(!(reach>1e-6f))discard_fragment();
 rd=delta/reach;oc=eye-in.sphere.xyz;
#endif
 float r=in.sphere.w*boundRadius(u.right.xyz,int(u.params.w)),b=dot(oc,rd),disc=b*b-dot(oc,oc)+r*r;
 if(disc<=0.0f||in.color.a<=0.0f)discard_fragment();
 float t0=max(0.0f,-b-sqrt(disc)),t1=min(reach,-b+sqrt(disc));
 if(t1<=t0)discard_fragment();
 float lod=lodFactor(in.sphere.xyz,r,u);
 uint steps=uint(clamp(floor(mix(u.lodControl.y,u.params.z,lod)+0.5f),4.0f,32.0f));float ds=(t1-t0)/float(steps);
 float trans=1.0f;float3 rgb=float3(0);
 float3 color=in.color.rgb;
#if AIVERSE_HDR
 color=rtsSrgbDecode(color);
#endif
 for(uint i=0;i<steps;++i){
  float3 q=(oc+rd*(t0+(float(i)+0.5f)*ds))/(in.sphere.w*u.right.xyz);
  float density=sampleDensity(q,in,u
#if SPECTRA_TEXTURE_VOLUME
 ,densityTexture,densitySampler
#endif
 );
  if(density<=0.0f)continue;
  float alpha=1.0f-exp(-density*ds);
  float3 L=u.lightDir.xyz;
  float3 local=oc+rd*(t0+(float(i)+0.5f)*ds);
  float lb=dot(local,L),exit=max(0.0f,-lb+sqrt(max(0.0f,lb*lb-dot(local,local)+r*r)));
  uint shadowSteps=uint(floor(u.lightControl.x*lod+0.5f));
  float optical=0.0f;
  if(shadowSteps>0u&&exit>0.0f){
   float dl=exit/float(shadowSteps);
   for(uint j=0;j<shadowSteps;++j){
    float3 sq=(local+L*((float(j)+0.5f)*dl))/(in.sphere.w*u.right.xyz);
    optical+=sampleDensity(sq,in,u
#if SPECTRA_TEXTURE_VOLUME
    ,densityTexture,densitySampler
#endif
    )*dl;
   }
  }
  float visibility=exp(-optical*u.lightControl.y);
  float3 light=float3(u.lightColor.w)+u.lightColor.xyz*(u.lightDir.w*visibility*phaseHG(dot(rd,L),u.lightControl.z));
  rgb+=trans*alpha*color*(light+u.params.y);trans*=1.0f-alpha;
  if(trans<0.001f)break;
 }
 float fog=1.0f-exp(-length(in.sphere.xyz-u.camPos.xyz)*u.fog.w);
 rgb=mix(rgb,u.fog.rgb*(1.0f-trans),fog);
 return half4(float4(rgb,1.0f-trans));
}
)MSL";
inline constexpr const char* vertex=R"GLSL(
layout(std140) uniform U0 {mat4 u_viewProj;vec4 u_fog;vec4 u_camPos;vec4 u_params;vec4 u_right;vec4 u_up;vec4 u_lightDir;vec4 u_lightColor;vec4 u_lightControl;mat4 u_invVP;vec4 u_depthControl;vec4 u_lodControl;};
layout(location=0) in vec3 a_pos;
layout(location=1) in vec2 a_size;
layout(location=2) in vec4 a_col;

float hashVolume(vec3 p){return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453);}
float noiseVolume(vec3 p){
 vec3 a=floor(p),f=p-a;f=f*f*(3.0-2.0*f);
 return mix(mix(mix(hashVolume(a),hashVolume(a+vec3(1,0,0)),f.x),
                mix(hashVolume(a+vec3(0,1,0)),hashVolume(a+vec3(1,1,0)),f.x),f.y),
            mix(mix(hashVolume(a+vec3(0,0,1)),hashVolume(a+vec3(1,0,1)),f.x),
                mix(hashVolume(a+vec3(0,1,1)),hashVolume(a+vec3(1,1,1)),f.x),f.y),f.z);
}
float shapeDensity(vec3 q,int shape){
 if(shape==2){vec3 a=abs(q);return max(0.0,1.0-max(a.x,max(a.y,a.z)));}
 if(shape==3)q.y-=clamp(q.y,-1.0,1.0);
 return max(0.0,1.0-dot(q,q));
}
float volumeDensity(vec3 q,vec3 center,int shape,vec3 noiseSettings){
 float envelope=shapeDensity(q,shape);
 if(envelope<=0.0||noiseSettings.y<=0.0)return envelope;
 vec3 p=q*noiseSettings.x+fract(center*0.071)*19.0;
 float sum=0.0,weight=0.5,total=0.0;
 for(int i=0;i<int(noiseSettings.z);++i){
  sum+=noiseVolume(p)*weight;total+=weight;p=p*2.03+vec3(13.7,5.1,9.2);weight*=0.5;
 }
 return envelope*mix(1.0,sum/max(total,0.0001),noiseSettings.y);
}
float boundRadius(vec3 axes,int shape){
 if(shape==2)return length(axes);
 if(shape==3)return max(axes.x,max(axes.y,axes.z))+axes.y;
 return max(axes.x,max(axes.y,axes.z));
}
out highp vec3 v_world;flat out highp vec4 v_sphere;flat out highp vec4 v_color;flat out highp float v_age;
void main(){
 float base=max(a_size.x,0.0001);float r=base*boundRadius(u_right.xyz,int(u_params.w));vec3 delta=a_pos-u_camPos.xyz;float d=length(delta);
 v_sphere=vec4(a_pos,base);v_color=a_col;v_world=a_pos;v_age=a_size.y;
#if SPECTRA_VOLUME_DEPTH
 vec4 nearPlane=vec4(u_viewProj[0].z,u_viewProj[1].z,u_viewProj[2].z,u_viewProj[3].z);
 if(u_depthControl.z>1.5)nearPlane+=vec4(u_viewProj[0].w,u_viewProj[1].w,u_viewProj[2].w,u_viewProj[3].w);
 vec3 wrow=vec3(u_viewProj[0].w,u_viewProj[1].w,u_viewProj[2].w);
 if(a_col.a>0.0&&(d<=r*1.01||length(wrow)<1e-6||dot(nearPlane,vec4(a_pos,1))<=r*length(nearPlane.xyz))){
  const vec2 screenCorners[4]=vec2[4](vec2(-1,-1),vec2(1,-1),vec2(1,1),vec2(-1,1));
  gl_Position=vec4(screenCorners[gl_VertexID&3],0,1);return;
 }
#endif
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
layout(std140) uniform U0 {mat4 u_viewProj;vec4 u_fog;vec4 u_camPos;vec4 u_params;vec4 u_right;vec4 u_up;vec4 u_lightDir;vec4 u_lightColor;vec4 u_lightControl;mat4 u_invVP;vec4 u_depthControl;vec4 u_lodControl;};
in highp vec3 v_world;flat in highp vec4 v_sphere;flat in highp vec4 v_color;flat in highp float v_age;
#if SPECTRA_TEXTURE_VOLUME
uniform highp sampler3D tex0;
#endif
#if SPECTRA_VOLUME_DEPTH
uniform highp sampler2D tex1;
#endif
out highp vec4 o_color;

float hashVolume(vec3 p){return fract(sin(dot(p,vec3(127.1,311.7,74.7)))*43758.5453);}
float noiseVolume(vec3 p){
 vec3 a=floor(p),f=p-a;f=f*f*(3.0-2.0*f);
 return mix(mix(mix(hashVolume(a),hashVolume(a+vec3(1,0,0)),f.x),
                mix(hashVolume(a+vec3(0,1,0)),hashVolume(a+vec3(1,1,0)),f.x),f.y),
            mix(mix(hashVolume(a+vec3(0,0,1)),hashVolume(a+vec3(1,0,1)),f.x),
                mix(hashVolume(a+vec3(0,1,1)),hashVolume(a+vec3(1,1,1)),f.x),f.y),f.z);
}
float shapeDensity(vec3 q,int shape){
 if(shape==2){vec3 a=abs(q);return max(0.0,1.0-max(a.x,max(a.y,a.z)));}
 if(shape==3)q.y-=clamp(q.y,-1.0,1.0);
 return max(0.0,1.0-dot(q,q));
}
float volumeDensity(vec3 q,vec3 center,int shape,vec3 noiseSettings){
 float envelope=shapeDensity(q,shape);
 if(envelope<=0.0||noiseSettings.y<=0.0)return envelope;
 vec3 p=q*noiseSettings.x+fract(center*0.071)*19.0;
 float sum=0.0,weight=0.5,total=0.0;
 for(int i=0;i<int(noiseSettings.z);++i){
  sum+=noiseVolume(p)*weight;total+=weight;p=p*2.03+vec3(13.7,5.1,9.2);weight*=0.5;
 }
 return envelope*mix(1.0,sum/max(total,0.0001),noiseSettings.y);
}
float boundRadius(vec3 axes,int shape){
 if(shape==2)return length(axes);
 if(shape==3)return max(axes.x,max(axes.y,axes.z))+axes.y;
 return max(axes.x,max(axes.y,axes.z));
}
float lodFactor(vec3 center,float radius){
 if(u_lodControl.x<0.5)return 1.0;
 vec4 clip=u_viewProj*vec4(center,1);
 vec3 rowW=vec3(u_viewProj[0].w,u_viewProj[1].w,u_viewProj[2].w);
 float minW=clip.w-radius*length(rowW);
 if(minW<=max(1e-5,clip.w*1e-5))return 1.0;
 vec2 ndc=clip.xy/clip.w;
 vec3 rowX=vec3(u_viewProj[0].x,u_viewProj[1].x,u_viewProj[2].x);
 vec3 rowY=vec3(u_viewProj[0].y,u_viewProj[1].y,u_viewProj[2].y);
 float pixels=radius*max(length(rowX-ndc.x*rowW)*u_depthControl.x,length(rowY-ndc.y*rowW)*u_depthControl.y)/minW;
 return smoothstep(u_lodControl.z,u_lodControl.w,pixels);
}
float sampleDensity(vec3 q){
  float density=volumeDensity(q,v_sphere.xyz,int(u_params.w),u_up.xyz)*u_params.x*v_color.a;
#if SPECTRA_TEXTURE_VOLUME
  float n=u_right.w,frames=u_camPos.w;
  vec3 uv=clamp(q/vec3(1.0,int(u_params.w)==3?2.0:1.0,1.0)*0.5+0.5,0.0,1.0);
  vec3 texel=uv*(n-1.0)+0.5;
  float f=clamp(v_age,0.0,1.0)*(frames-1.0),a=floor(f),b=min(a+1.0,frames-1.0);
  float va=textureLod(tex0,vec3(texel.xy/n,(texel.z+a*n)/(n*frames)),0.0).r;
  float vb=textureLod(tex0,vec3(texel.xy/n,(texel.z+b*n)/(n*frames)),0.0).r;
  density*=mix(va,vb,f-a);
#endif
 return density;
}
float phaseHG(float cosTheta,float g){
 float d=max(0.01,1.0+g*g-2.0*g*cosTheta);
 return (1.0-g*g)/(d*sqrt(d));
}
void main(){
 vec3 rd=normalize(v_world-u_camPos.xyz),oc=u_camPos.xyz-v_sphere.xyz;
 float reach=1e8;
#if SPECTRA_VOLUME_DEPTH
 ivec2 pixel=ivec2(gl_FragCoord.xy);
 if(any(lessThan(pixel,ivec2(0)))||any(greaterThanEqual(pixel,ivec2(u_depthControl.xy))))discard;
 float depth=texelFetch(tex1,pixel,0).r;
 if(!(depth>=0.0&&depth<=1.0))discard;
 vec2 screen=(vec2(pixel)+0.5)/u_depthControl.xy;
 if(u_depthControl.w>0.5)screen.y=1.0-screen.y;
 vec2 ndc=screen*2.0-1.0;
 vec4 nearH=u_invVP*vec4(ndc,1.0-u_depthControl.z,1);
 vec4 endH=u_invVP*vec4(ndc,min(depth,0.99999)*u_depthControl.z+1.0-u_depthControl.z,1);
 if(abs(nearH.w)<1e-8||abs(endH.w)<1e-8)discard;
 vec3 eye=nearH.xyz/nearH.w,end=endH.xyz/endH.w;
 vec3 delta=end-eye;reach=length(delta);
 if(!(reach>1e-6))discard;
 rd=delta/reach;oc=eye-v_sphere.xyz;
#endif
 float r=v_sphere.w*boundRadius(u_right.xyz,int(u_params.w)),b=dot(oc,rd),disc=b*b-dot(oc,oc)+r*r;
 if(disc<=0.0||v_color.a<=0.0)discard;
 float t0=max(0.0,-b-sqrt(disc)),t1=min(reach,-b+sqrt(disc));if(t1<=t0)discard;
 float lod=lodFactor(v_sphere.xyz,r);
 int steps=int(clamp(floor(mix(u_lodControl.y,u_params.z,lod)+0.5),4.0,32.0));float ds=(t1-t0)/float(steps);
 float trans=1.0;vec3 rgb=vec3(0),color=v_color.rgb;
#if AIVERSE_HDR
 color=rtsSrgbDecode(color);
#endif
 for(int i=0;i<steps;++i){
  vec3 q=(oc+rd*(t0+(float(i)+0.5)*ds))/(v_sphere.w*u_right.xyz);
  float density=sampleDensity(q);
  if(density<=0.0)continue;
  float alpha=1.0-exp(-density*ds);
  vec3 L=u_lightDir.xyz;
  vec3 local=oc+rd*(t0+(float(i)+0.5)*ds);
  float lb=dot(local,L),exitDistance=max(0.0,-lb+sqrt(max(0.0,lb*lb-dot(local,local)+r*r)));
  int shadowSteps=int(floor(u_lightControl.x*lod+0.5));float optical=0.0;
  if(shadowSteps>0&&exitDistance>0.0){
   float dl=exitDistance/float(shadowSteps);
   for(int j=0;j<shadowSteps;++j){
    vec3 sq=(local+L*((float(j)+0.5)*dl))/(v_sphere.w*u_right.xyz);
    optical+=sampleDensity(sq)*dl;
   }
  }
  float visibility=exp(-optical*u_lightControl.y);
  vec3 light=vec3(u_lightColor.w)+u_lightColor.xyz*(u_lightDir.w*visibility*phaseHG(dot(rd,L),u_lightControl.z));
  rgb+=trans*alpha*color*(light+u_params.y);trans*=1.0-alpha;
  if(trans<0.001)break;
 }
 float fog=1.0-exp(-length(v_sphere.xyz-u_camPos.xyz)*u_fog.w);
 o_color=vec4(mix(rgb,u_fog.rgb*(1.0-trans),fog),1.0-trans);
}
)GLSL";
}
