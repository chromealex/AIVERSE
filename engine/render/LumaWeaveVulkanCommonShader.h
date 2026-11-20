// Shared GLSL Vulkan direct-light structs, PBR incident and BVH ray query.
// Both explicit receiver and real screen-space direct kernels reuse this ONE
// shadow intersection implementation; the compute pipeline and source ABI
// remain 4 SSBOs / one RGBA16F output / 80-byte push constants.
#pragma once
#define AIVERSE_GLSL_LUMA_VULKAN_COMMON R"GLSL(
struct Light {
    vec4 positionRadius;
    vec4 colorIntensity;
    vec4 directionCone;
    vec4 settings;
    uvec4 identity;
};
struct Receiver { vec4 worldPosition; vec4 normal; };
struct Triangle { vec4 a,b,c,albedo; };
struct Node { vec4 lo,hi; uvec4 meta; };
layout(set=0,binding=0,std430) readonly buffer LightData { Light lights[]; };
layout(set=0,binding=2,std430) readonly buffer TriangleData { Triangle triangles[]; };
layout(set=0,binding=3,std430) readonly buffer NodeData { Node nodes[]; };
layout(rgba16f,set=0,binding=4) writeonly uniform image2D directOutput;
// Match the current Vulkan RHI's 80-byte compute push constant layout.
// counts = source count, receiver count, node count, samples per receiver
// control = RNG frame seed, image width, triangle count, reserved
layout(push_constant) uniform LumaParams {
    uvec4 counts;
    uvec4 control;
    vec4 reserved0;
    vec4 reserved1;
    vec4 reserved2;
} p;

bool finite3(vec3 v) { return !any(isnan(v)) && !any(isinf(v)); }

vec3 incident(Light l,vec3 pos,vec3 normal) {
    vec3 ray=l.positionRadius.xyz-pos;
    float d2=dot(ray,ray),radius=l.positionRadius.w;
    if(d2<=0.0001 || d2>=radius*radius || l.colorIntensity.w<=0.0)
        return vec3(0.0);
    vec3 L=ray*inversesqrt(d2);
    float ndl=max(dot(normal,L),0.0);
    if(ndl<=0.0) return vec3(0.0);
    float cone=1.0;
    if(l.identity.z==1u) {
        float cosAngle=dot(l.directionCone.xyz,-L);
        if(cosAngle<=l.directionCone.w) return vec3(0.0);
        if(l.settings.x>l.directionCone.w)
            cone=clamp((cosAngle-l.directionCone.w)/
                       (l.settings.x-l.directionCone.w),0.0,1.0);
    }
    float falloff=1.0-d2/(radius*radius);
    falloff=falloff*falloff/(1.0+d2);
    return l.colorIntensity.xyz*(l.colorIntensity.w*ndl*cone*falloff);
}
float weight(vec3 rgb,float importanceBias) {
    return max(0.0,dot(rgb,vec3(0.2126,0.7152,0.0722)))*importanceBias;
}
uint random32(inout uint x) {
    x^=x<<13; x^=x>>17; x^=x<<5; return x;
}

bool boxHit(Node node,vec3 origin,vec3 direction,float distance) {
    float entry=0.0,exit=distance;
    for(int axis=0;axis<3;++axis) {
        float o=origin[axis], d=direction[axis];
        if(abs(d)<1e-10) {
            if(o<node.lo[axis] || o>node.hi[axis])return false;
            continue;
        }
        float a=(node.lo[axis]-o)/d,b=(node.hi[axis]-o)/d;
        if(a>b) {float t=a;a=b;b=t;}
        entry=max(entry,a);exit=min(exit,b);
        if(entry>exit)return false;
    }
    return true;
}
bool shadowed(vec3 origin,vec3 direction,float maxDistance) {
    if(p.counts.z==0u)return false; // explicitly empty occluder scene
    uint stack[64]; uint top=1u;stack[0]=0u;
    while(top>0u) {
        uint id=stack[--top];
        if(id>=p.counts.z)return true; // malformed: fail dark, not light leak
        Node node=nodes[id];
        if(!boxHit(node,origin,direction,maxDistance))continue;
        if(node.meta.w>0u) {
            if(node.meta.w>4u || node.meta.z>=p.control.z ||
               node.meta.w>p.control.z-node.meta.z)return true;
            for(uint j=0u;j<node.meta.w;++j) {
                Triangle tri=triangles[node.meta.z+j];
                vec3 e1=tri.b.xyz-tri.a.xyz,e2=tri.c.xyz-tri.a.xyz;
                vec3 crossD=cross(direction,e2);
                float det=dot(e1,crossD);
                if(abs(det)<1e-10)continue;
                float inverse=1.0/det;
                vec3 s=origin-tri.a.xyz;
                float u=dot(s,crossD)*inverse;
                if(u<0.0||u>1.0)continue;
                vec3 q=cross(s,e1);
                float v=dot(direction,q)*inverse;
                if(v<0.0||u+v>1.0)continue;
                float d=dot(e2,q)*inverse;
                if(d>0.001 && d<maxDistance)return true;
            }
        } else {
            uint siblings=node.meta.y==0xffffffffu?1u:2u;
            if(top+siblings>64u)return true; // fail dark on invalid BVH
            if(siblings==2u)stack[top++]=node.meta.y;
            stack[top++]=node.meta.x;
        }
    }
    return false;
}

)GLSL"
