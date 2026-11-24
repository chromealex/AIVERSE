// Shared dense gas kernel body with thin Metal/GLSL resource wrappers.
#pragma once
#include <string>
namespace aiverse::spectra::volume::shaders {
inline constexpr const char* kBody=R"VOL(uint indexOf(int3 p,int n){return uint((p.z*n+p.y)*n+p.x);}
bool fluid(int3 p,int n MASK_ARG){
    if(p.x<0||p.y<0||p.z<0||p.x>=n||p.y>=n||p.z>=n)return false;
    return solid[indexOf(p,n)]==0u;
}
Cell zeroCell(){return MAKE_CELL(float4(0.0f),float4(0.0f));}
Cell at(int3 p,int n READ_ARG){
    return fluid(p,n MASK_PASS)?inputCells[indexOf(p,n)]:zeroCell();
}
Cell blend(Cell a,Cell b,float t){return MAKE_CELL(mix(a.velocity,b.velocity,t),mix(a.fields,b.fields,t));}
Cell sampleCell(float3 p,int n READ_ARG){
    float3 q=clamp(p,float3(0.0f),float3(float(n-1)));
    int3 b=min(int3(q),int3(n-2));
    float3 f=q-float3(b);
    Cell a=blend(at(b,n READ_PASS),at(b+int3(1,0,0),n READ_PASS),f.x);
    Cell c=blend(at(b+int3(0,1,0),n READ_PASS),at(b+int3(1,1,0),n READ_PASS),f.x);
    Cell d=blend(at(b+int3(0,0,1),n READ_PASS),at(b+int3(1,0,1),n READ_PASS),f.x);
    Cell e=blend(at(b+int3(0,1,1),n READ_PASS),at(b+int3(1,1,1),n READ_PASS),f.x);
    return blend(blend(a,c,f.y),blend(d,e,f.y),f.z);
}
float3 limitVelocity(float3 v,float maximum){
    float len=length(v);return len>maximum?v*(maximum/len):v;
}
float3 walls(float3 v,int3 p,int n MASK_ARG){
    if((v.x<0.0f&&!fluid(p-int3(1,0,0),n MASK_PASS))||(v.x>0.0f&&!fluid(p+int3(1,0,0),n MASK_PASS)))v.x=0.0f;
    if((v.y<0.0f&&!fluid(p-int3(0,1,0),n MASK_PASS))||(v.y>0.0f&&!fluid(p+int3(0,1,0),n MASK_PASS)))v.y=0.0f;
    if((v.z<0.0f&&!fluid(p-int3(0,0,1),n MASK_PASS))||(v.z>0.0f&&!fluid(p+int3(0,0,1),n MASK_PASS)))v.z=0.0f;
    return v;
}
float velocityAt(int3 p,int n,uint axis READ_ARG){
    return at(p,n READ_PASS).velocity[axis];
}
float pressureAt(int3 p,int n,float fallback PRESSURE_ARG){
    return fluid(p,n MASK_PASS)?pressureIn[indexOf(p,n)]:fallback;
}
KERNEL_BEGIN
    const uint mode=u.meta.x;const int n=int(u.meta.y);
    if(id>=uint(n*n*n))return;
    const int3 p=int3(int(id)%n,(int(id)/n)%n,int(id)/(n*n));
    if(mode==0u){outputCells[id]=zeroCell();return;}
    if(mode==1u){
        if(solid[id]!=0u){outputCells[id]=zeroCell();return;}
        const float dt=u.originCell.w>0.0f?1.0f/60.0f:0.0f;
        const float h=u.originCell.w;
        const float3 local=float3(p);
        Cell c=sampleCell(local-inputCells[id].velocity.xyz*(dt/h),n READ_PASS);
        float burn=c.fields.y>=u.reaction.x?min(c.fields.z,c.fields.z*u.reaction.y*dt):0.0f;
        c.fields.z=clamp((c.fields.z-burn)*u.decay.w,0.0f,16.0f);
        c.fields.y=clamp((c.fields.y+burn*u.reaction.z)*u.decay.z,0.0f,16.0f);
        c.fields.x=clamp((c.fields.x+burn*u.reaction.w)*u.decay.y,0.0f,16.0f);
        c.velocity.xyz=limitVelocity((c.velocity.xyz+(u.wind.xyz+u.up.xyz*(u.buoyancy.x*c.fields.y-u.buoyancy.y*c.fields.x))*dt)*u.decay.x,u.buoyancy.z);
        float3 world=u.originCell.xyz+(local+float3(0.5f))*h;
        for(uint i=0u;i<u.meta.z;++i){
            SourceData src=u.sources[i];
            float3 delta=world-src.positionRadius.xyz;
            float q=dot(delta,delta)/(src.positionRadius.w*src.positionRadius.w);
            if(q>=1.0f)continue;
            float w=(1.0f-q)*src.acceleration.w;
            c.fields.xyz=min(float3(16.0f),c.fields.xyz+src.rates.xyz*w);
            float len=length(delta);
            float3 radial=len>1e-6f?delta*(src.rates.w/len):float3(0.0f);
            c.velocity.xyz=limitVelocity(c.velocity.xyz+(src.acceleration.xyz+radial)*w,u.buoyancy.z);
        }
        c.velocity.xyz=walls(c.velocity.xyz,p,n MASK_PASS);
        outputCells[id]=c;return;
    }
    if(mode==2u){
        pressureOut[id]=0.0f;
        divergence[id]=solid[id]!=0u?0.0f:
            (velocityAt(p+int3(1,0,0),n,0u READ_PASS)-velocityAt(p-int3(1,0,0),n,0u READ_PASS)+
             velocityAt(p+int3(0,1,0),n,1u READ_PASS)-velocityAt(p-int3(0,1,0),n,1u READ_PASS)+
             velocityAt(p+int3(0,0,1),n,2u READ_PASS)-velocityAt(p-int3(0,0,1),n,2u READ_PASS))/(2.0f*u.originCell.w);
        return;
    }
    if(mode==3u){
        if(solid[id]!=0u){pressureOut[id]=0.0f;return;}
        float sum=0.0f;int count=0;
        for(int axis=0;axis<3;++axis)for(int sign=-1;sign<=1;sign+=2){
            int3 q=p;q[axis]+=sign;
            if(fluid(q,n MASK_PASS)){sum+=pressureIn[indexOf(q,n)];++count;}
        }
        float h=u.originCell.w;
        pressureOut[id]=count>0?(sum-divergence[id]*h*h)/float(count):0.0f;return;
    }
    if(mode==4u){
        if(solid[id]!=0u){outputCells[id]=zeroCell();return;}
        Cell c=inputCells[id];
        if(u.meta.w!=0u){
            float center=pressureIn[id];
            float3 grad=float3(
                pressureAt(p+int3(1,0,0),n,center PRESSURE_PASS)-pressureAt(p-int3(1,0,0),n,center PRESSURE_PASS),
                pressureAt(p+int3(0,1,0),n,center PRESSURE_PASS)-pressureAt(p-int3(0,1,0),n,center PRESSURE_PASS),
                pressureAt(p+int3(0,0,1),n,center PRESSURE_PASS)-pressureAt(p-int3(0,0,1),n,center PRESSURE_PASS));
            c.velocity.xyz=limitVelocity(c.velocity.xyz-grad*(0.5f/u.originCell.w),u.buoyancy.z);
        }
        c.velocity.xyz=walls(c.velocity.xyz,p,n MASK_PASS);
        outputCells[id]=c;return;
    }
}
)VOL";
inline const std::string kMetalSource=std::string(R"MSL(#include <metal_stdlib>
using namespace metal;
struct Cell {float4 velocity;float4 fields;};
struct SourceData {float4 positionRadius;float4 rates;float4 acceleration;};

struct Uniforms {uint4 meta;float4 originCell,decay,reaction,buoyancy,wind,up;SourceData sources[17];};
#define MASK_ARG ,device const uint* solid
#define MASK_PASS ,solid
#define READ_ARG ,device const Cell* inputCells,device const uint* solid
#define READ_PASS ,inputCells,solid
#define PRESSURE_ARG ,device const float* pressureIn,device const uint* solid
#define PRESSURE_PASS ,pressureIn,solid
#define KERNEL_BEGIN kernel void cs_main(uint id [[thread_position_in_grid]],device const Cell* inputCells [[buffer(0)]],device Cell* outputCells [[buffer(1)]],device const uint* solid [[buffer(2)]],device const float* pressureIn [[buffer(3)]],device float* pressureOut [[buffer(4)]],device float* divergence [[buffer(5)]],constant Uniforms& u [[buffer(8)]]) {

#define MAKE_CELL(a,b) Cell{a,b}
)MSL")+kBody;
inline const std::string kGLSLSource=std::string(R"GLSL(#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
#define float3 vec3
#define float4 vec4
#define int3 ivec3
#define uint4 uvec4
struct Cell {float4 velocity;float4 fields;};
struct SourceData {float4 positionRadius;float4 rates;float4 acceleration;};

layout(std430,set=0,binding=0) readonly buffer InputCells {Cell inputCells[];};
layout(std430,set=0,binding=1) buffer OutputCells {Cell outputCells[];};
layout(std430,set=0,binding=2) readonly buffer Mask {uint solid[];};
layout(std430,set=0,binding=3) readonly buffer PressureIn {float pressureIn[];};
layout(std430,set=0,binding=4) buffer PressureOut {float pressureOut[];};
layout(std430,set=0,binding=5) buffer Divergence {float divergence[];};
layout(std140,set=0,binding=8) uniform Params {uint4 meta;float4 originCell,decay,reaction,buoyancy,wind,up;SourceData sources[17];} u;
#define MASK_ARG
#define MASK_PASS
#define READ_ARG
#define READ_PASS
#define PRESSURE_ARG
#define PRESSURE_PASS
#define KERNEL_BEGIN void main(){uint id=gl_GlobalInvocationID.x;

#define MAKE_CELL(a,b) Cell(a,b)
)GLSL")+kBody;
inline const char* const kMetal=kMetalSource.c_str();
inline const char* const kGLSL=kGLSLSource.c_str();
}
