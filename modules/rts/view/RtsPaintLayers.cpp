#include "RtsPaintLayers.h"
#include "RtsView.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace rts::game {
namespace {
const char* vs=R"(
layout(location=0) in vec3 pos;
layout(std140) uniform U0 { mat4 vp; vec4 bounds; vec4 color; vec4 style; };
out vec2 uv;
void main(){uv=(pos.xz-bounds.xy)/bounds.zw;gl_Position=vp*vec4(pos,1);}
)";
const char* fs=R"(
layout(std140) uniform U0 { mat4 vp; vec4 bounds; vec4 color; vec4 style; };
uniform sampler2D tex0;in vec2 uv;out vec4 outColor;
void main(){
 if(any(lessThan(uv,vec2(0)))||any(greaterThan(uv,vec2(1))))discard;
 float v=texture(tex0,uv).a;float aa=max(fwidth(v),0.015);
 float fill=smoothstep(0.5-aa,0.5+aa,v);
 float edge=1.0-smoothstep(style.y,style.y+aa,abs(v-0.5));
 outColor=vec4(mix(color.rgb,vec3(1),edge*0.55),fill*style.x);
}
)";
const char* msl=R"(
#include <metal_stdlib>
using namespace metal;
struct U {float4x4 vp;float4 bounds,color,style;};struct In {float3 pos [[attribute(0)]];};struct Out {float4 pos [[position]];float2 uv;};
vertex Out vs_main(In i [[stage_in]],constant U& u [[buffer(1)]]){Out o;o.uv=(i.pos.xz-u.bounds.xy)/u.bounds.zw;o.pos=u.vp*float4(i.pos,1);return o;}
fragment float4 fs_main(Out i [[stage_in]],constant U& u [[buffer(1)]],texture2d<float> tex0 [[texture(0)]],sampler s [[sampler(0)]]){
 if(any(i.uv<float2(0))||any(i.uv>float2(1)))discard_fragment();
 float v=tex0.sample(s,i.uv).a;float aa=max(fwidth(v),0.015f);
 float fill=smoothstep(0.5f-aa,0.5f+aa,v);float edge=1-smoothstep(u.style.y,u.style.y+aa,abs(v-0.5f));
 return float4(mix(u.color.rgb,float3(1),edge*0.55f),fill*u.style.x);
}
)";
// Categorical cell raster: port of the ownership look of OpenWar's ArmyMapWorld.shader (desktop 5x5 smoothstep
// coverage of each candidate id, border where the coverage crosses 1/2) with its mobile 2x2 bilinear variant on low
// material quality. tex0: RGBA8, rgb = palette colour, a = id. U0: vp; bounds (x, z, w, h); size (cells w, h, smooth,
// border width); border colour; style (opacity, desktop, -, -); outside colour (id `outside`, a = id / 255).
const char* cellVs=R"(
layout(location=0) in vec3 pos;
layout(std140) uniform U0 { mat4 vp; vec4 bounds; vec4 size; vec4 borderColor; vec4 style; vec4 outside; vec4 groundA; vec4 groundB; vec4 coastA; vec4 beach; vec4 shallow; vec4 earthNear; vec4 earthFar; vec4 foam; };
out vec2 uv;
void main(){uv=(pos.xz-bounds.xy)/bounds.zw;gl_Position=vp*vec4(pos,1);}
)";
const char* cellFs=R"(
layout(std140) uniform U0 { mat4 vp; vec4 bounds; vec4 size; vec4 borderColor; vec4 style; vec4 outside; vec4 groundA; vec4 groundB; vec4 coastA; vec4 beach; vec4 shallow; vec4 earthNear; vec4 earthFar; vec4 foam; };
uniform highp sampler2D tex0;uniform sampler2D tex1;in vec2 uv;out vec4 outColor;
vec4 cellAt(vec2 c){
 if(any(lessThan(c,vec2(0.0)))||any(greaterThanEqual(c,size.xy)))return outside;
 return texelFetch(tex0,ivec2(c),0);
}
float idOf(vec4 s){return floor(s.a*255.0+0.5);}
bool same(vec4 a,vec4 b){return abs(idOf(a)-idOf(b))<0.5;}
float fall(float x,float h){float t=clamp((x+h)/(2.0*h),0.0,1.0);return 6.0*t*(1.0-t)/(2.0*h);}
// ---- ground surface (ArmyMapWorld.shader ApplyMeadow / coast): groundA (scale, strength, on, flipHeight),
// groundB (teamMin, teamMax, contextMin, contextMax), coastA (seaId, on, time, drop direction)
float hash12(vec2 p){
 if(style.y<0.5){vec3 q=fract(vec3(p.x,p.y,p.x)*.1031);q+=dot(q,q.yzx+33.33);return fract((q.x+q.y)*q.z);}
 return fract(sin(dot(p,vec2(127.1,311.7)))*43758.5453);
}
float meadowNoise(vec2 p){vec2 c=floor(p),f=fract(p);f=f*f*(3.0-2.0*f);
 return mix(mix(hash12(c),hash12(c+vec2(1,0)),f.x),mix(hash12(c+vec2(0,1)),hash12(c+1.0),f.x),f.y);}
vec3 meadow(vec3 color,vec2 world,float id){
 if(id>=groundB.z-0.5&&id<=groundB.w+0.5)return color*(.96+.08*meadowNoise(world/max(groundA.x*1.6,.1)));
 vec2 p=world/max(groundA.x,.1);
 float broad=meadowNoise(p+17.2),small=meadowNoise(p*2.3+vec2(7.1,31.6));
 float patch=smoothstep(.35,.66,broad*.72+small*.28);
 float neutral=(id<groundB.x-0.5||id>groundB.y+0.5)?1.0:0.0;
 vec2 blotUV=p*3.6+vec2(small,broad)*.65;
 float blotNoise=meadowNoise(blotUV+vec2(43.7,9.2));float blotAA=max(.025,fwidth(blotNoise));
 float blot=smoothstep(.54-blotAA,.59+blotAA,blotNoise);
 float pale=smoothstep(.69-blotAA,.74+blotAA,meadowNoise(blotUV*.83+63.4));
 vec3 ng=mix(color,vec3(.46,.61,.19),patch*.62);ng=mix(ng,vec3(.68,.65,.33),blot*.22);ng=mix(ng,vec3(.94,.87,.55),pale*.26);
 float relief=1.08-patch*.22-blot*.15+pale*.12;
 vec3 dyed=mix(color*relief,vec3(.72,.88,1.0),pale*.06);
 return clamp(mix(dyed,ng,neutral)*(1.0+(small-.5)*groundA.y*.2),0.0,1.0);
}
vec3 toLin(vec3 c){return mix(c/12.92,pow((c+.055)/1.055,vec3(2.4)),step(vec3(.04045),c));}
vec3 toSrgb(vec3 c){c=max(c,vec3(0.0));return mix(c*12.92,1.055*pow(c,vec3(1.0/2.4))-.055,step(vec3(.0031308),c));}
vec3 groundLinear(vec3 color,float id,float aa){
 vec2 world=vec2(bounds.x+uv.x*bounds.z,bounds.y+uv.y*bounds.w);if(groundA.w>0.0)world.y=groundA.w-world.y;
 if(coastA.y<0.5){if(groundA.z>0.5&&abs(id-coastA.x)>0.5)color=meadow(color,world,id);return color;}
 float signedCoast=(texture(tex1,uv).r-.5)*64.0,coastDistance=abs(signedCoast);
 float coastUnit=max(1.0,aa*1.3),coastAA=max(.3,aa*.65);
 float lowerCoast=(texture(tex1,uv+vec2(0.0,coastA.w*3.8*coastUnit)/size.xy).r-.5)*64.0;
 float foot=min(signedCoast,lowerCoast);
 if(abs(id-coastA.x)>0.5){
  if(groundA.z>0.5)color=meadow(color,world,id);
  float rim=1.0-smoothstep(coastUnit,coastUnit+coastAA,coastDistance);
  return mix(color,beach.rgb,rim*.8);
 }
 float shallows=1.0-smoothstep(1.0*coastUnit,8.0*coastUnit,foot);
 color=mix(color,shallow.rgb,shallows*.65);
 color*=1.0-(1.0-smoothstep(0.0,2.2*coastUnit+coastAA,foot))*.23;
 float cliff=1.0-smoothstep(-coastAA,coastAA,lowerCoast);
 float sideDepth=clamp(coastDistance/max(coastDistance-lowerCoast,.1),0.0,1.0);
 color=mix(color,mix(earthNear.rgb,earthFar.rgb,sideDepth),cliff);
 float pulse=sin(world.x*.17+world.y*.11+coastA.z*.65)*.35;float wave=(2.7+pulse)*coastUnit;
 float f=1.0-smoothstep(.12*coastUnit,.27*coastUnit+coastAA*.65,abs(foot-wave));
 float broken=.6+.4*meadowNoise(world/max(groundA.x*.16,.1)+coastA.z*.025);
 return mix(color,foam.rgb,f*broken*.94);
}
void main(){
 vec2 g=uv*size.xy;
 float aa=max(max(length(vec2(dFdx(g.x),dFdy(g.x))),length(vec2(dFdx(g.y),dFdy(g.y)))),1e-4);
 vec3 color;float edge=0.0;float fid=0.0;
 if(style.y<0.5){
  // 2x2 bilinear coverage (ArmyMapWorld MobileOwnership)
  vec2 q=g-0.5;vec2 cell=floor(q),f=fract(q);
  vec4 s[4];s[0]=cellAt(cell);s[1]=cellAt(cell+vec2(1,0));s[2]=cellAt(cell+vec2(0,1));s[3]=cellAt(cell+vec2(1,1));
  vec4 w=vec4((1.0-f.x)*(1.0-f.y),f.x*(1.0-f.y),(1.0-f.x)*f.y,f.x*f.y);
  vec4 dx=vec4(-(1.0-f.y),1.0-f.y,-f.y,f.y),dy=vec4(-(1.0-f.x),-f.x,1.0-f.x,f.x);
  float coverage=-1.0;color=s[0].rgb;fid=idOf(s[0]);
  for(int c=0;c<4;c++){
   vec4 m=vec4(same(s[0],s[c])?1.0:0.0,same(s[1],s[c])?1.0:0.0,same(s[2],s[c])?1.0:0.0,same(s[3],s[c])?1.0:0.0);
   float share=dot(w,m),grad=length(vec2(dot(dx,m),dot(dy,m)));
   float dist=grad>1e-4?abs(share-0.5)/grad:4.0;
   if(share>coverage){coverage=share;color=s[c].rgb;fid=idOf(s[c]);}
   edge=max(edge,1.0-smoothstep(size.w-aa,size.w+aa,dist));
  }
 }else{
  vec2 center=floor(g);float h=size.z;
  float wx[5],wy[5],dwx[5],dwy[5];
  for(int a=0;a<5;a++){
   float left=(center.x+float(a-2))-g.x,bottom=(center.y+float(a-2))-g.y;
   wx[a]=smoothstep(-h,h,left+1.0)-smoothstep(-h,h,left);wy[a]=smoothstep(-h,h,bottom+1.0)-smoothstep(-h,h,bottom);
   dwx[a]=fall(left,h)-fall(left+1.0,h);dwy[a]=fall(bottom,h)-fall(bottom+1.0,h);
  }
  vec4 s[25];float wt[25],ddx[25],ddy[25];float sum=0.0,sdx=0.0,sdy=0.0;
  for(int oy=0;oy<5;oy++)for(int ox=0;ox<5;ox++){
   int i=oy*5+ox;s[i]=cellAt(center+vec2(float(ox-2),float(oy-2)));
   wt[i]=wx[ox]*wy[oy];ddx[i]=dwx[ox]*wy[oy];ddy[i]=wx[ox]*dwy[oy];sum+=wt[i];sdx+=ddx[i];sdy+=ddy[i];
  }
  float inv=1.0/max(sum,1e-5);
  vec2 base=floor(g-0.5);int ix=int(base.x-center.x),iy=int(base.y-center.y);int c0=(iy+2)*5+(ix+2);
  int cand[4];cand[0]=c0;cand[1]=c0+1;cand[2]=c0+5;cand[3]=c0+6;
  color=s[12].rgb;fid=idOf(s[12]);float coverage=-1.0;
  for(int c=0;c<4;c++){
   vec4 k=s[cand[c]];bool seen=false;
   for(int e=0;e<4;e++)if(e<c&&same(s[cand[e]],k))seen=true;
   if(seen)continue;
   float share=0.0,gx=0.0,gy=0.0;
   for(int i=0;i<25;i++){float m=same(s[i],k)?1.0:0.0;share+=wt[i]*m;gx+=ddx[i]*m;gy+=ddy[i]*m;}
   share*=inv;float gdx=(gx-share*sdx)*inv,gdy=(gy-share*sdy)*inv;float grad=length(vec2(gdx,gdy));
   float dist=grad>1e-4?abs(share-0.5)/grad:(share>0.5?4.0:0.0);
   if(share>coverage){coverage=share;color=k.rgb;fid=idOf(k);}
   edge=max(edge,1.0-smoothstep(size.w-aa,size.w+aa,dist));
  }
 }
 if(groundA.z>0.5||coastA.y>0.5)color=style.z>0.5?toSrgb(groundLinear(toLin(color),fid,aa)):groundLinear(color,fid,aa);
 color=mix(color,borderColor.rgb,edge*borderColor.a);
 outColor=vec4(color,style.x);
}
)";
const char* cellMsl=R"(
#include <metal_stdlib>
using namespace metal;
struct U {float4x4 vp;float4 bounds,size,borderColor,style,outside,groundA,groundB,coastA,beach,shallow,earthNear,earthFar,foam;};struct In {float3 pos [[attribute(0)]];};struct Out {float4 pos [[position]];float2 uv;};
vertex Out vs_main(In i [[stage_in]],constant U& u [[buffer(1)]]){Out o;o.uv=(i.pos.xz-u.bounds.xy)/u.bounds.zw;o.pos=u.vp*float4(i.pos,1);return o;}
static float4 cellAt(constant U& u,texture2d<float> t,float2 c){
 if(any(c<float2(0))||any(c>=u.size.xy))return u.outside;
 return t.read(uint2(c));
}
static float idOf(float4 s){return floor(s.a*255+0.5);}
static bool same(float4 a,float4 b){return abs(idOf(a)-idOf(b))<0.5;}
static float fall(float x,float h){float t=saturate((x+h)/(2*h));return 6*t*(1-t)/(2*h);}
static float hash12(constant U& u,float2 p){
 if(u.style.y<0.5){float3 q=fract(float3(p.x,p.y,p.x)*.1031);q+=dot(q,q.yzx+33.33);return fract((q.x+q.y)*q.z);}
 return fract(sin(dot(p,float2(127.1,311.7)))*43758.5453);
}
static float meadowNoise(constant U& u,float2 p){float2 c=floor(p),f=fract(p);f=f*f*(3-2*f);
 return mix(mix(hash12(u,c),hash12(u,c+float2(1,0)),f.x),mix(hash12(u,c+float2(0,1)),hash12(u,c+1),f.x),f.y);}
static float3 meadow(constant U& u,float3 color,float2 world,float id){
 if(id>=u.groundB.z-0.5&&id<=u.groundB.w+0.5)return color*(.96+.08*meadowNoise(u,world/max(u.groundA.x*1.6,.1)));
 float2 p=world/max(u.groundA.x,.1);
 float broad=meadowNoise(u,p+17.2),small=meadowNoise(u,p*2.3+float2(7.1,31.6));
 float patch=smoothstep(.35,.66,broad*.72+small*.28);
 float neutral=(id<u.groundB.x-0.5||id>u.groundB.y+0.5)?1.0:0.0;
 float2 blotUV=p*3.6+float2(small,broad)*.65;
 float blotNoise=meadowNoise(u,blotUV+float2(43.7,9.2));float blotAA=max(.025,fwidth(blotNoise));
 float blot=smoothstep(.54-blotAA,.59+blotAA,blotNoise);
 float pale=smoothstep(.69-blotAA,.74+blotAA,meadowNoise(u,blotUV*.83+63.4));
 float3 ng=mix(color,float3(.46,.61,.19),patch*.62);ng=mix(ng,float3(.68,.65,.33),blot*.22);ng=mix(ng,float3(.94,.87,.55),pale*.26);
 float relief=1.08-patch*.22-blot*.15+pale*.12;
 float3 dyed=mix(color*relief,float3(.72,.88,1.0),pale*.06);
 return saturate(mix(dyed,ng,neutral)*(1.0+(small-.5)*u.groundA.y*.2));
}
static float3 toLin(float3 c){return mix(c/12.92,pow((c+.055)/1.055,float3(2.4)),step(float3(.04045),c));}
static float3 toSrgb(float3 c){c=max(c,float3(0));return mix(c*12.92,1.055*pow(c,float3(1.0/2.4))-.055,step(float3(.0031308),c));}
static float3 ground(constant U& u,texture2d<float> coast,sampler smp,float2 uv,float3 color,float id,float aa){
 float2 world=float2(u.bounds.x+uv.x*u.bounds.z,u.bounds.y+uv.y*u.bounds.w);if(u.groundA.w>0)world.y=u.groundA.w-world.y;
 if(u.coastA.y<0.5){if(u.groundA.z>0.5&&abs(id-u.coastA.x)>0.5)color=meadow(u,color,world,id);return color;}
 float signedCoast=(coast.sample(smp,uv).r-.5)*64.0,coastDistance=abs(signedCoast);
 float coastUnit=max(1.0,aa*1.3),coastAA=max(.3,aa*.65);
 float lowerCoast=(coast.sample(smp,uv+float2(0.0,u.coastA.w*3.8*coastUnit)/u.size.xy).r-.5)*64.0;
 float foot=min(signedCoast,lowerCoast);
 if(abs(id-u.coastA.x)>0.5){
  if(u.groundA.z>0.5)color=meadow(u,color,world,id);
  float rim=1.0-smoothstep(coastUnit,coastUnit+coastAA,coastDistance);
  return mix(color,u.beach.rgb,rim*.8);
 }
 float shallows=1.0-smoothstep(1.0*coastUnit,8.0*coastUnit,foot);
 color=mix(color,u.shallow.rgb,shallows*.65);
 color*=1.0-(1.0-smoothstep(0.0,2.2*coastUnit+coastAA,foot))*.23;
 float cliff=1.0-smoothstep(-coastAA,coastAA,lowerCoast);
 float sideDepth=saturate(coastDistance/max(coastDistance-lowerCoast,.1));
 color=mix(color,mix(u.earthNear.rgb,u.earthFar.rgb,sideDepth),cliff);
 float pulse=sin(world.x*.17+world.y*.11+u.coastA.z*.65)*.35;float wave=(2.7+pulse)*coastUnit;
 float f=1.0-smoothstep(.12*coastUnit,.27*coastUnit+coastAA*.65,abs(foot-wave));
 float broken=.6+.4*meadowNoise(u,world/max(u.groundA.x*.16,.1)+u.coastA.z*.025);
 return mix(color,u.foam.rgb,f*broken*.94);
}
fragment float4 fs_main(Out i [[stage_in]],constant U& u [[buffer(1)]],texture2d<float> tex0 [[texture(0)]],texture2d<float> tex1 [[texture(1)]],sampler smp [[sampler(0)]]){
 float2 g=i.uv*u.size.xy;
 float aa=max(max(length(float2(dfdx(g.x),dfdy(g.x))),length(float2(dfdx(g.y),dfdy(g.y)))),1e-4f);
 float3 color;float edge=0;float fid=0;
 if(u.style.y<0.5){
  float2 q=g-0.5;float2 cell=floor(q),f=fract(q);
  float4 s[4]={cellAt(u,tex0,cell),cellAt(u,tex0,cell+float2(1,0)),cellAt(u,tex0,cell+float2(0,1)),cellAt(u,tex0,cell+float2(1,1))};
  float4 w=float4((1-f.x)*(1-f.y),f.x*(1-f.y),(1-f.x)*f.y,f.x*f.y);
  float4 dx=float4(-(1-f.y),1-f.y,-f.y,f.y),dy=float4(-(1-f.x),-f.x,1-f.x,f.x);
  float coverage=-1;color=s[0].rgb;fid=idOf(s[0]);
  for(int c=0;c<4;c++){
   float4 m=float4(same(s[0],s[c])?1:0,same(s[1],s[c])?1:0,same(s[2],s[c])?1:0,same(s[3],s[c])?1:0);
   float share=dot(w,m),grad=length(float2(dot(dx,m),dot(dy,m)));
   float dist=grad>1e-4?abs(share-0.5)/grad:4.0;
   if(share>coverage){coverage=share;color=s[c].rgb;fid=idOf(s[c]);}
   edge=max(edge,1-smoothstep(u.size.w-aa,u.size.w+aa,dist));
  }
 }else{
  float2 center=floor(g);float h=u.size.z;
  float wx[5],wy[5],dwx[5],dwy[5];
  for(int a=0;a<5;a++){
   float left=(center.x+float(a-2))-g.x,bottom=(center.y+float(a-2))-g.y;
   wx[a]=smoothstep(-h,h,left+1)-smoothstep(-h,h,left);wy[a]=smoothstep(-h,h,bottom+1)-smoothstep(-h,h,bottom);
   dwx[a]=fall(left,h)-fall(left+1,h);dwy[a]=fall(bottom,h)-fall(bottom+1,h);
  }
  float4 s[25];float wt[25],ddx[25],ddy[25];float sum=0,sdx=0,sdy=0;
  for(int oy=0;oy<5;oy++)for(int ox=0;ox<5;ox++){
   int k=oy*5+ox;s[k]=cellAt(u,tex0,center+float2(float(ox-2),float(oy-2)));
   wt[k]=wx[ox]*wy[oy];ddx[k]=dwx[ox]*wy[oy];ddy[k]=wx[ox]*dwy[oy];sum+=wt[k];sdx+=ddx[k];sdy+=ddy[k];
  }
  float inv=1/max(sum,1e-5f);
  float2 base=floor(g-0.5);int ix=int(base.x-center.x),iy=int(base.y-center.y);int c0=(iy+2)*5+(ix+2);
  int cand[4]={c0,c0+1,c0+5,c0+6};
  color=s[12].rgb;fid=idOf(s[12]);float coverage=-1;
  for(int c=0;c<4;c++){
   float4 k=s[cand[c]];bool seen=false;
   for(int e=0;e<4;e++)if(e<c&&same(s[cand[e]],k))seen=true;
   if(seen)continue;
   float share=0,gx=0,gy=0;
   for(int n=0;n<25;n++){float m=same(s[n],k)?1:0;share+=wt[n]*m;gx+=ddx[n]*m;gy+=ddy[n]*m;}
   share*=inv;float gdx=(gx-share*sdx)*inv,gdy=(gy-share*sdy)*inv;float grad=length(float2(gdx,gdy));
   float dist=grad>1e-4?abs(share-0.5)/grad:(share>0.5?4.0:0.0);
   if(share>coverage){coverage=share;color=k.rgb;fid=idOf(k);}
   edge=max(edge,1-smoothstep(u.size.w-aa,u.size.w+aa,dist));
  }
 }
 if(u.groundA.z>0.5||u.coastA.y>0.5)color=u.style.z>0.5?toSrgb(ground(u,tex1,smp,i.uv,toLin(color),fid,aa)):ground(u,tex1,smp,i.uv,color,fid,aa);
 color=mix(color,u.borderColor.rgb,edge*u.borderColor.a);
 return float4(color,u.style.x);
}
)";
}
namespace {
// ArmyUnitModelLayer.BuildCoastDistance: chamfer distance (1, 1.414214) to the sea / land edge in texels, negative on
// land, 3x3 (1 2 1) smoothing of the staircase, stored as .5 + d / 64 (clamped) in R.
void coastDistance(const std::vector<uint8_t>& sea,int w,int h,std::vector<uint8_t>& out){
 std::vector<float> d(size_t(w)*h);
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  const size_t i=size_t(y)*w+x;const bool s=sea[i]!=0;
  const bool edge=(x>0&&(sea[i-1]!=0)!=s)||(x+1<w&&(sea[i+1]!=0)!=s)||(y>0&&(sea[i-size_t(w)]!=0)!=s)||(y+1<h&&(sea[i+size_t(w)]!=0)!=s);
  d[i]=edge?.5f:32.f;
 }
 for(int pass=0;pass<2;++pass){const int st=pass==0?1:-1;
  for(int y=pass==0?0:h-1;y>=0&&y<h;y+=st)for(int x=pass==0?0:w-1;x>=0&&x<w;x+=st){
   const size_t i=size_t(y)*w+x;const int px=x-st,py=y-st;float v=d[i];
   if(px>=0&&px<w)v=std::min(v,d[size_t(y)*w+px]+1.f);
   if(py>=0&&py<h){v=std::min(v,d[size_t(py)*w+x]+1.f);if(x>0)v=std::min(v,d[size_t(py)*w+x-1]+1.414214f);if(x+1<w)v=std::min(v,d[size_t(py)*w+x+1]+1.414214f);}
   d[i]=v;
  }}
 for(size_t i=0;i<d.size();++i)if(!sea[i])d[i]=-d[i];
 out.assign(size_t(w)*h*4,255);
 for(int y=0;y<h;++y)for(int x=0;x<w;++x){
  float v=0;for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){
   const int sx=std::clamp(x+dx,0,w-1),sy=std::clamp(y+dy,0,h-1);v+=d[size_t(sy)*w+sx]*(dx==0?2.f:1.f)*(dy==0?2.f:1.f)/16.f;}
  const uint8_t b=uint8_t(std::lround(std::clamp(.5f+v/64.f,0.f,1.f)*255.f));
  const size_t i=(size_t(y)*w+x)*4;out[i]=out[i+1]=out[i+2]=b;
 }
}
}
void RtsPaintLayers::drawCells(render::Renderer3D& r,gfx::Device& dev,render::PaintLayer& p,const MapGrid& g,const RtsView& view,int id,double time){
 auto& l=layers_[id];
 if(!cellPipe_.valid()){
  gfx::ShaderDesc sd;sd.glslVertex=cellVs;sd.glslFragment=cellFs;sd.msl=cellMsl;sd.textureSlots=2;sd.label="paint cells";cellShader_=dev.createShader(sd);
  gfx::PipelineDesc pd;pd.shader=cellShader_;pd.attrs[0]={0,gfx::VertexFormat::Float3,0};pd.attrCount=1;pd.stride=sizeof(render::MeshVertex);
  pd.indexType=gfx::IndexType::UInt32;pd.depthWrite=false;pd.cull=gfx::CullMode::None;pd.blend=gfx::BlendMode::Alpha;
  if(cellShader_.valid())cellPipe_=dev.createPipeline(pd);
  if(!cellPipe_.valid()){std::fprintf(stderr,"[paint] cells: %s\n",dev.lastError().c_str());return;}
 }
 const int d=std::max(1,p.density),tw=p.cellsW*d,th=p.cellsH*d;
 if(!l.canvas.handle().valid()){if(!l.canvas.init(dev,tw,th))return;p.cellsDirty=true;}
 if(p.cellsDirty||p.paletteDirty){
  l.bytes.resize(size_t(tw)*th*4);
  for(int y=0;y<th;++y)for(int x=0;x<tw;++x){
   const uint8_t c=p.cells[size_t(y/d)*p.cellsW+size_t(x/d)];const size_t i=size_t(y)*tw+x;
   for(int k=0;k<3;++k)l.bytes[i*4+size_t(k)]=p.palette[size_t(c)*4+size_t(k)];l.bytes[i*4+3]=c;
  }
  l.canvas.upload(0,0,tw,th,l.bytes.data(),tw*4);
  if(p.coast&&p.cellsDirty){   // the shoreline is static: rebuilt only when the sea mask changes
   std::vector<uint8_t> sea(size_t(tw)*th);uint64_t key=14695981039346656037ull;
   for(size_t i=0;i<sea.size();++i){sea[i]=l.bytes[i*4+3]==p.seaId;key=(key^sea[i])*1099511628211ull;}
   if(key!=l.seaKey||!l.coast.valid()){
    std::vector<uint8_t> coast;coastDistance(sea,tw,th,coast);
    if(!l.coast.valid()){gfx::TextureDesc td;td.width=tw;td.height=th;td.filter=gfx::Filter::Linear;td.data=coast.data();td.label="paint coast";l.coast=dev.createTexture(td);}
    else dev.updateTexture(l.coast,0,0,tw,th,coast.data(),tw*4);
    l.seaKey=key;
   }
  }
  p.cellsDirty=p.paletteDirty=false;
 }
 l.canvas.flush();
 if(!l.mesh.vb.valid()){   // one quad over the bounds, at the terrain height of its centre + lift
  render::MeshBuilder mb;const float y=view.heightAt(g,p.x+p.w*.5f,p.z+p.h*.5f)+p.lift;
  mb.quad(render::float3{p.x,y,p.z},render::float3{p.x,y,p.z+p.h},render::float3{p.x+p.w,y,p.z+p.h},render::float3{p.x+p.w,y,p.z});
  l.mesh=mb.upload(dev);
 }
 struct U{render::mat4 vp;render::float4 bounds,size,border,style,outside,groundA,groundB,coastA,beach,shallow,earthNear,earthFar,foam;};
 const size_t o=size_t(p.outside)*4;
 auto c3=[](const std::array<float,3>& c){return render::float4{c[0],c[1],c[2],1};};
 const bool coast=p.coast&&l.coast.valid();
 U u{r.viewProj(),{p.x,p.z,p.w,p.h},{float(tw),float(th),p.smooth,p.borderWidth},{p.borderColor[0],p.borderColor[1],p.borderColor[2],p.borderColor[3]},
     {p.opacity,r.materialQuality()==render::MaterialQuality::Low?0.f:1.f,p.groundLinear?1.f:0.f,0},
     {p.palette[o]/255.f,p.palette[o+1]/255.f,p.palette[o+2]/255.f,float(p.outside)/255.f},
     {p.groundScale,p.groundStrength,p.ground?1.f:0.f,p.flipHeight},{float(p.teamMin),float(p.teamMax),float(p.contextMin),float(p.contextMax)},
     {float(p.seaId),coast?1.f:0.f,float(std::fmod(time,3600.0)),p.coastDrop},c3(p.beach),c3(p.shallow),c3(p.earthNear),c3(p.earthFar),c3(p.foam)};
 auto mesh=l.mesh;auto texture=l.canvas.handle();auto pipe=cellPipe_;auto coastTex=coast?l.coast:texture;
 r.drawCustom([u,mesh,texture,coastTex,pipe](gfx::Device& d){d.setPipeline(pipe);d.setUniforms(0,&u,sizeof(u));d.setTexture(0,texture);d.setTexture(1,coastTex);d.setVertexBuffer(mesh.vb);d.setIndexBuffer(mesh.ib);d.drawIndexed(mesh.indexCount);});
}
void RtsPaintLayers::shutdown(){
 if(dev_){for(auto& [id,l]:layers_){l.canvas.shutdown();if(l.coast.valid())dev_->destroyTexture(l.coast);dev_->destroyBuffer(l.mesh.vb);dev_->destroyBuffer(l.mesh.ib);}dev_->destroyPipeline(pipe_);dev_->destroyShader(shader_);dev_->destroyPipeline(cellPipe_);dev_->destroyShader(cellShader_);}
 layers_.clear();memory_.clear();source_.reset();dev_=nullptr;shader_={};pipe_={};cellShader_={};cellPipe_={};w_=h_=0;lastTime_=-1;
}
void RtsPaintLayers::draw(render::Renderer3D& r,gfx::Device& dev,const std::shared_ptr<render::PaintLayers>& source,
 const MapGrid& g,const RtsView& view,const std::vector<uint8_t>* vision,double time,uint8_t viewer){
 if(!source)return; // The editor may also draw its edit scene using this view in the same frame.
 if(source!=source_||g.w!=w_||g.h!=h_||time<lastTime_||viewer!=viewer_)shutdown();
 if(!source||source->layers.empty()){if(dev_)shutdown();return;}
 if(!dev_){
  dev_=&dev;source_=source;w_=g.w;h_=g.h;viewer_=viewer;revision_=1;memory_.assign(size_t(w_)*h_,0);
  gfx::ShaderDesc sd;sd.glslVertex=vs;sd.glslFragment=fs;sd.msl=msl;sd.textureSlots=1;sd.label="paint surface";shader_=dev.createShader(sd);
  gfx::PipelineDesc pd;pd.shader=shader_;pd.attrs[0]={0,gfx::VertexFormat::Float3,0};pd.attrCount=1;pd.stride=sizeof(render::MeshVertex);
  pd.indexType=gfx::IndexType::UInt32;pd.depthWrite=false;pd.cull=gfx::CullMode::None;pd.blend=gfx::BlendMode::Alpha;
  if(shader_.valid())pipe_=dev.createPipeline(pd);
  if(!pipe_.valid()){std::fprintf(stderr,"[paint] %s\n",dev.lastError().c_str());return;}
 }
 if(!pipe_.valid())return;
 const float dt=lastTime_<0?1.f:float(std::clamp(time-lastTime_,0.0,0.1));lastTime_=time;
 bool changed=false;
 for(size_t i=0;i<memory_.size();++i)if(!vision||(i<vision->size()&&(*vision)[i]==2)){
  uint8_t owner=i<g.territory.size()?g.territory[i]:0;if(owner!=memory_[i]){memory_[i]=owner;changed=true;}
 }
 if(changed)++revision_;
 for(auto it=layers_.begin();it!=layers_.end();)if(!source->layers.count(it->first)){it->second.canvas.shutdown();if(it->second.coast.valid())dev.destroyTexture(it->second.coast);dev.destroyBuffer(it->second.mesh.vb);dev.destroyBuffer(it->second.mesh.ib);it=layers_.erase(it);}else ++it;
 for(auto& [id,p]:source->layers){
  if(p.categorical){drawCells(r,dev,p,g,view,id,time);continue;}
  auto& l=layers_[id];const bool field=p.territoryOwner>0;
  if(!p.textureId&&!l.canvas.handle().valid()){
   const int tw=field?w_:std::min(p.width,r.materialQuality()==render::MaterialQuality::Low?512:2048);
   const int th=field?h_:std::max(1,p.height*tw/p.width);
   if(!l.canvas.init(dev,tw,th))continue;
   l.value.assign(size_t(w_)*h_,0);l.target=l.value;l.bytes.assign(size_t(w_)*h_*4,0);
  }
  if(field){
   // Compact scalar source, smoothed in CPU space then sampled bilinearly by the GPU.
   // The authoritative ownership is never read back from the rendered texture.
   int x0=w_,y0=h_,x1=-1,y1=-1;
   for(int y=0;y<h_;++y)for(int x=0;x<w_;++x){
    int i=y*w_+x;
    if(l.revision!=revision_){
    float sum=0;
    for(int dy=-1;dy<=1;++dy)for(int dx=-1;dx<=1;++dx){int xx=x+dx,yy=y+dy;if(xx>=0&&yy>=0&&xx<w_&&yy<h_&&memory_[yy*w_+xx]==p.territoryOwner)sum+=float((dx==0?2:1)*(dy==0?2:1))/16;}
    l.target[i]=0.5f*sum+0.5f*(memory_[i]==p.territoryOwner?1.f:0.f);
    }
    float& v=l.value[i];v+=std::clamp(l.target[i]-v,-dt*5,dt*5);
    uint8_t a=uint8_t(std::clamp(v*255,0.f,255.f)+0.5f);
    if(a!=l.bytes[size_t(i)*4+3]){l.bytes[size_t(i)*4+3]=a;x0=std::min(x0,x);y0=std::min(y0,y);x1=std::max(x1,x);y1=std::max(y1,y);}
   }
   l.revision=revision_;
   if(x1>=x0)l.canvas.upload(x0,y0,x1-x0+1,y1-y0+1,l.bytes.data()+size_t(y0*w_+x0)*4,w_*4);
  }else{
   if(p.clear)l.canvas.clear();
   if(p.copyFrom){auto it=layers_.find(p.copyFrom);if(it!=layers_.end())l.canvas.blit(it->second.canvas.handle());p.copyFrom=0;}
   for(auto& stroke:p.strokes)l.canvas.stroke(stroke);
  }
  p.clear=false;p.strokes.clear();l.canvas.flush();
  if(!p.surface)continue;
  gfx::TextureHandle sample=l.canvas.handle();
  if(p.textureId){auto it=layers_.find(p.textureId);if(it==layers_.end())continue;sample=it->second.canvas.handle();}
  if(!sample.valid())continue;
  // Rebuild only if the visibility/land mask changes; terrain geometry itself is stable.
  // Small flat maps share a single surface draw per layer.
  uint64_t key=14695981039346656037ull;
  for(size_t i=0;i<g.flags.size();++i){key^=uint64_t(g.flags[i]) | (uint64_t(!vision||(i<vision->size()&&(*vision)[i]>0))<<8);key*=1099511628211ull;}
  if(key!=l.meshKey||!l.mesh.vb.valid()){
  render::MeshBuilder mb;
  for(int y=0;y<h_;++y)for(int x=0;x<w_;++x){
   int i=y*w_+x;if(p.landOnly&&!(g.flags[i]&kCellGround))continue;
   if(vision&&(size_t(i)>=vision->size()||(*vision)[i]==0))continue;
   if(x+1<p.x||y+1<p.z||x>p.x+p.w||y>p.z+p.h)continue;
   auto point=[&](float xx,float zz){return render::float3{xx,view.heightAt(g,xx,zz)+p.lift,zz};};
   mb.quad(point(float(x),float(y)),point(float(x),float(y+1)),point(float(x+1),float(y+1)),point(float(x+1),float(y)));
  }
  // Cached geometry must populate every frame-in-flight. Dynamic Metal buffers rotate;
  // updating only the current slice leaves the other slices empty while the mask is static.
  dev.destroyBuffer(l.mesh.vb);dev.destroyBuffer(l.mesh.ib);
  l.mesh=mb.upload(dev);l.meshKey=key;
  }
  struct U{render::mat4 vp;render::float4 bounds,color,style;};
  U u{r.viewProj(),{p.x,p.z,p.w,p.h},{p.color[0],p.color[1],p.color[2],p.color[3]},{p.opacity,p.border,0,0}};
  auto mesh=l.mesh;auto texture=sample;auto pipe=pipe_;
  r.drawCustom([u,mesh,texture,pipe](gfx::Device& d){d.setPipeline(pipe);d.setUniforms(0,&u,sizeof(u));d.setTexture(0,texture);d.setVertexBuffer(mesh.vb);d.setIndexBuffer(mesh.ib);d.drawIndexed(mesh.indexCount);});
 }
}
}
