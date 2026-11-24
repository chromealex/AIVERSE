// World-space dense volume HDR contribution; same integration body on both backends.
#pragma once
#include <string>
namespace aiverse::spectra::volume::render_shaders {
inline constexpr const char* kBody=R"VOL(
uint indexOf(int3 p,int n){return uint((p.z*n+p.y)*n+p.x);}
float2 at(int3 p,int n FIELD_ARG){
    uint i=indexOf(p,n);
    return solid[i]!=0u?float2(0.0f):cells[i].fields.xy;
}
float2 fieldAt(float3 p,float4 domain,int n FIELD_ARG){
    float3 q=(p-domain.xyz)/domain.w;
    if(OUTSIDE(q,n))return float2(0.0f);
    q=clamp(q-float3(0.5f),float3(0.0f),float3(float(n-1)));
    int3 b=min(int3(q),int3(n-2));float3 f=q-float3(b);
    float2 a=mix(at(b,n FIELD_PASS),at(b+int3(1,0,0),n FIELD_PASS),f.x);
    float2 c=mix(at(b+int3(0,1,0),n FIELD_PASS),at(b+int3(1,1,0),n FIELD_PASS),f.x);
    float2 d=mix(at(b+int3(0,0,1),n FIELD_PASS),at(b+int3(1,0,1),n FIELD_PASS),f.x);
    float2 e=mix(at(b+int3(0,1,1),n FIELD_PASS),at(b+int3(1,1,1),n FIELD_PASS),f.x);
    return mix(mix(a,c,f.y),mix(d,e,f.y),f.z);
}
float2 intersectBox(float3 eye,float3 dir,float3 lo,float3 hi,float cap){
    float a=0.0f,b=cap;
    for(int k=0;k<3;++k){
        if(abs(dir[k])<1e-7f){if(eye[k]<lo[k]||eye[k]>hi[k])return float2(1.0f,-1.0f);}
        else{
            float x=(lo[k]-eye[k])/dir[k],y=(hi[k]-eye[k])/dir[k];
            a=max(a,min(x,y));b=min(b,max(x,y));
        }
    }
    return float2(a,b);
}
float phase(float cosine,float g){
    float v=max(0.001f,1.0f+g*g-2.0f*g*cosine);
    return (1.0f-g*g)/(12.5663706f*v*sqrt(v));
}
float volumeHash(float3 q){
    float v=sin(dot(q,float3(12.9898f,78.233f,37.719f)))*43758.5453f;
    return fract(v);
}
float volumeNoise(float3 p){
    float3 b=floor(p),f=p-b;
    f=f*f*(3.0f-2.0f*f);
    float a=mix(mix(volumeHash(b),volumeHash(b+float3(1,0,0)),f.x),
                mix(volumeHash(b+float3(0,1,0)),volumeHash(b+float3(1,1,0)),f.x),f.y);
    float c=mix(mix(volumeHash(b+float3(0,0,1)),volumeHash(b+float3(1,0,1)),f.x),
                mix(volumeHash(b+float3(0,1,1)),volumeHash(b+float3(1,1,1)),f.x),f.y);
    return mix(a,c,f.z);
}
float smokeDetailFactor(float3 p,float strength){
    if(strength<=0.0f)return 1.0f;
    float low=volumeNoise(p*0.95f),high=volumeNoise(p*3.1f);
    return max(0.025f,1.0f+strength*((low-0.5f)*4.4f+(high-0.5f)*1.3f));
}
float flameDetailFactor(float3 p){
    float low=volumeNoise(p*2.3f),high=volumeNoise(p*6.1f);
    return clamp((low*0.7f+high*0.3f-0.26f)*2.15f,0.0f,1.0f);
}
float emberSample(float3 p){return volumeHash(floor(p*8.0f));}
float shadow(float3 p,float3 dir,float reach,float4 domain,int n,uint steps,float extinction,float detail,float3 detailOffset FIELD_ARG){
    if(steps==0u||reach<=0.0f)return 1.0f;
    float2 hit=intersectBox(p,dir,domain.xyz,domain.xyz+float3(domain.w*float(n)),reach);
    if(hit.y<=hit.x)return 1.0f;
    float ds=(hit.y-hit.x)/float(steps),tau=0.0f;
    for(uint i=0u;i<steps;++i){
        float3 samplePos=p+dir*(hit.x+(float(i)+0.5f)*ds);
        float density=fieldAt(samplePos,domain,n FIELD_PASS).x;
        if(density>0.0f)tau+=density*smokeDetailFactor(samplePos-detailOffset,detail)*extinction*ds;
        if(tau>12.0f)break;
    }
    return exp(-tau);
}
// Integrate the low-frequency gas field at receiver resolution. Sparse
// self-shadow samples would print separate slices of the plume onto the floor.
float receiverVisibility(float3 p,float3 dir,float reach,float4 domain,int n,uint quality,
                         float extinction,float detail,float3 offset FIELD_ARG){
    float2 hit=intersectBox(p,dir,domain.xyz,domain.xyz+float3(domain.w*float(n)),reach);
    if(hit.y<=hit.x)return 1.0f;
    float segment=hit.y-hit.x;
    uint count=uint(clamp(ceil(segment/domain.w),1.0f,float(quality*16u)));
    float ds=segment/float(count);
    float3 axis=abs(dir.y)<0.9f?float3(0,1,0):float3(1,0,0);
    float3 side=normalize(cross(dir,axis)),up=cross(side,dir);
    float tau=0.0f;
    for(uint i=0u;i<count;++i){
        float t=hit.x+(float(i)+0.5f)*ds;
        // Four symmetric taps approximate a growing light footprint.
        float radius=domain.w*0.35f+t*0.045f;
        float3 q=p+dir*t;
        float density=0.25f*(fieldAt(q+side*radius,domain,n FIELD_PASS).x+
                             fieldAt(q-side*radius,domain,n FIELD_PASS).x+
                             fieldAt(q+up*radius,domain,n FIELD_PASS).x+
                             fieldAt(q-up*radius,domain,n FIELD_PASS).x);
        if(density>0.0f)tau+=density*extinction*ds*smokeDetailFactor(q-offset,detail*0.25f);
        if(tau>10.0f)break;
    }
    return exp(-tau);
}
KERNEL_BEGIN
    if(id.x>=u.meta.x||id.y>=u.meta.y)return;
    float4 background=READ_COLOR(id);
    float depth=READ_DEPTH(id);
    if(!(depth>=0.0f&&depth<=1.0f)){WRITE_COLOR(id,background);return;}
    float2 screen=(float2(id)+float2(0.5f))/float2(u.meta.xy);
    if(u.camera.x>0.5f)screen.y=1.0f-screen.y;
    float2 ndc=screen*2.0f-1.0f;
    float4 nearH=u.invVP*float4(ndc,1.0f-u.camera.y,1.0f);
    float4 farH=u.invVP*float4(ndc,min(depth,0.99999f)*u.camera.y+1.0f-u.camera.y,1.0f);
    if(abs(nearH.w)<1e-8f||abs(farH.w)<1e-8f){WRITE_COLOR(id,background);return;}
    float3 eye=nearH.xyz/nearH.w,end=farH.xyz/farH.w;
    float reach=length(end-eye);
    if(!(reach>1e-6f)){WRITE_COLOR(id,background);return;}
    float3 dir=(end-eye)/reach;
    reach=depth<0.99999f?min(reach,u.camera.z):u.camera.z;
    int n=int(u.meta.z);
    if(depth<0.99999f&&u.sun.w>0.0f&&u.limits.x>0.0f){
        float visibility=receiverVisibility(end,u.sun.xyz,u.controls.z,u.domain,n,uint(u.limits.x),u.smoke.w,u.limits.z,u.detailOffset.xyz FIELD_PASS);
        float directShare=u.sun.w/(1.0f+u.controls.y+u.sun.w);
        background.rgb*=1.0f-directShare*(1.0f-visibility);
    }
    float2 hit=intersectBox(eye,dir,u.domain.xyz,u.domain.xyz+float3(u.domain.w*float(n)),reach);
    if(hit.y<=hit.x){WRITE_COLOR(id,background);return;}
    uint steps=uint(clamp(ceil((hit.y-hit.x)/(u.domain.w*0.5f)),1.0f,float(u.meta.w)));
    float ds=(hit.y-hit.x)/float(steps),trans=1.0f;
    float jitter=(volumeHash(float3(float(id.x),float(id.y),0.0f))-0.5f)*0.7f;
    float3 radiance=float3(0.0f);
    for(uint i=0u;i<steps&&trans>0.0001f;++i){
        float3 p=eye+dir*(hit.x+(float(i)+0.5f+jitter)*ds);
        float2 field=fieldAt(p,u.domain,n FIELD_PASS);
        float sigma=max(0.0f,field.x)*u.smoke.w*smokeDetailFactor(p-u.detailOffset.xyz,u.limits.z);
        float heat=max(0.0f,field.y-u.controls.x)*u.fire.w*flameDetailFactor(p);
        if(sigma<=0.0f&&heat<=0.0f)continue;
        float tr=exp(-sigma*ds),opacity=1.0f-tr;
        float integral=sigma>1e-5f?opacity/sigma:ds;
        float3 incident=float3(u.controls.y);
        if(sigma>0.0f&&u.sun.w>0.0f){
            float atten=shadow(p,u.sun.xyz,u.controls.z,u.domain,n,uint(u.limits.x),u.smoke.w,u.limits.z,u.detailOffset.xyz FIELD_PASS);
            incident+=u.sunColor.xyz*(u.sun.w*phase(dot(dir,u.sun.xyz),u.controls.w)*atten);
        }
        if(sigma>0.0f)for(uint l=0u;l<uint(u.limits.y);++l){
            float3 delta=u.pointPos[l].xyz-p;float dist=length(delta),radius=u.pointPos[l].w;
            if(dist<=1e-5f||dist>=radius)continue;
            float3 lightDir=delta/dist;
            float falloff=1.0f-dist/radius;falloff=falloff*falloff/(1.0f+dist*dist);
            float atten=shadow(p,lightDir,dist,u.domain,n,uint(u.limits.x),u.smoke.w,u.limits.z,u.detailOffset.xyz FIELD_PASS);
            incident+=u.pointColor[l].xyz*(u.pointColor[l].w*falloff*phase(dot(dir,lightDir),u.controls.w)*atten);
        }
        float ember=max(0.0f,emberSample(p)-0.999f)*1000.0f*u.limits.w*min(1.0f,max(0.0f,field.x))*ds;
        radiance+=trans*(u.smoke.xyz*incident*opacity+u.fire.xyz*(heat*integral)+float3(8.0f,1.4f,0.15f)*ember);
        trans*=tr;
    }
    WRITE_COLOR(id,float4(clamp(background.rgb*trans+radiance,float3(0.0f),float3(65504.0f)),background.a));
}
)VOL";
inline const std::string kMetalSource=std::string(R"MSL(#include <metal_stdlib>
using namespace metal;
struct Cell {float4 velocity;float4 fields;};
struct Uniforms {float4x4 invVP;uint4 meta;float4 domain,camera,smoke,fire,controls,sun,sunColor,limits,detailOffset;float4 pointPos[4],pointColor[4];};
#define FIELD_ARG ,device const Cell* cells,device const uint* solid
#define FIELD_PASS ,cells,solid
#define OUTSIDE(q,n) (any((q)<float3(0.0f))||any((q)>=float3(float(n))))
#define READ_COLOR(p) scene.read(p)
#define READ_DEPTH(p) depthTex.read(p)
#define WRITE_COLOR(p,c) target.write(c,p)
#define KERNEL_BEGIN kernel void cs_main(uint2 id [[thread_position_in_grid]],device const Cell* cells [[buffer(0)]],device const uint* solid [[buffer(1)]],constant Uniforms& u [[buffer(8)]],texture2d<float,access::write> target [[texture(0)]],texture2d<float,access::read> scene [[texture(1)]],depth2d<float,access::read> depthTex [[texture(2)]]) {
)MSL")+kBody;
inline const std::string kGLSLSource=std::string(R"GLSL(layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
#define float2 vec2
#define float3 vec3
#define float4 vec4
#define float4x4 mat4
#define int3 ivec3
#define uint4 uvec4
struct Cell {float4 velocity;float4 fields;};
layout(rgba16f,set=0,binding=0) uniform writeonly image2D target;
layout(set=0,binding=5) uniform sampler2D scene;
layout(set=0,binding=6) uniform sampler2D depthTex;
layout(std430,set=0,binding=12) readonly buffer Cells {Cell cells[];};
layout(std430,set=0,binding=13) readonly buffer Mask {uint solid[];};
layout(std140,set=0,binding=16) uniform Uniforms {float4x4 invVP;uint4 meta;float4 domain,camera,smoke,fire,controls,sun,sunColor,limits,detailOffset;float4 pointPos[4],pointColor[4];} u;
#define FIELD_ARG
#define FIELD_PASS
#define OUTSIDE(q,n) (any(lessThan(q,float3(0.0f)))||any(greaterThanEqual(q,float3(float(n)))))
#define READ_COLOR(p) texelFetch(scene,ivec2(p),0)
#define READ_DEPTH(p) texelFetch(depthTex,ivec2(p),0).r
#define WRITE_COLOR(p,c) imageStore(target,ivec2(p),c)
#define KERNEL_BEGIN void main(){uvec2 id=gl_GlobalInvocationID.xy;
)GLSL")+kBody;
inline const char* const kMetal=kMetalSource.c_str();
inline const char* const kGLSL=kGLSLSource.c_str();
}
