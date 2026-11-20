// Experimental Vulkan compute DIRECT point/spot lighting for explicit world
// position + normal receivers. This is NOT a screen-space compositor, nor GI.
// Shares the 80-byte light/32-byte receiver/64-byte triangle/48-byte node ABI
// used by LumaWeave/Hybrid Dynamic GI. Full-registry evaluation avoids silently
// losing source energy when a cluster overflows; CPU admission MUST cap work.
// RHI backend must provide 4 storage buffers, 1 storage RGBA16F image,
// exactly 80 bytes of push constants, and a real completion-frame fence.
// Vulkan-only GLSL 450 (NOT valid GLES3/Metal).
#pragma once

namespace aiverse::render::luma_vk {
inline constexpr const char* kReceiverDirectCompute = R"GLSL(
#version 450
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

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
layout(set=0,binding=1,std430) readonly buffer ReceiverData { Receiver receivers[]; };
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

void main() {
    uint i=gl_GlobalInvocationID.x;
    if(i>=p.counts.y)return;
    ivec2 texel=ivec2(int(i%p.control.y),int(i/p.control.y));
    Receiver r=receivers[i];
    vec3 pos=r.worldPosition.xyz,n=r.normal.xyz;
    float n2=dot(n,n);
    if(r.worldPosition.w<0.5 || !finite3(pos) || !finite3(n) ||
       !(n2>1e-8) || p.counts.x==0u) {
        imageStore(directOutput,texel,vec4(0.0));return;
    }
    n*=inversesqrt(n2);
    float totalWeight=0.0;
    // Unlike the Metal clustered variant, this bounded reference uses ALL
    // sources to preserve correctness even for out-of-grid receivers.
    for(uint j=0u;j<p.counts.x;++j) {
        Light l=lights[j];
        totalWeight+=weight(incident(l,pos,n),l.settings.y);
    }
    if(!(totalWeight>0.0) || isinf(totalWeight) || isnan(totalWeight)) {
        imageStore(directOutput,texel,vec4(0.0));return;
    }
    uint seed=(i+1u)*747796405u ^ (p.control.x+1u)*2891336453u;
    if(seed==0u)seed=1u;
    vec3 direct=vec3(0.0);
    for(uint sample=0u;sample<p.counts.w;++sample) {
        float threshold=float(random32(seed))*(1.0/4294967296.0)*totalWeight;
        float sum=0.0,selectedWeight=0.0;
        vec3 selectedRadiance=vec3(0.0);
        Light selected=lights[0];
        for(uint j=0u;j<p.counts.x;++j) {
            Light l=lights[j];
            vec3 c=incident(l,pos,n);
            float w=weight(c,l.settings.y);
            if(w<=0.0)continue;
            sum+=w;
            if(threshold<sum) {
                selected=l;selectedRadiance=c;selectedWeight=w;break;
            }
        }
        if(selectedWeight<=0.0)continue;
        bool blocked=false;
        if(selected.settings.z>0.5 && p.counts.z>0u) {
            vec3 origin=pos+n*0.02;
            vec3 ray=selected.positionRadius.xyz-origin;
            float dist=length(ray);
            if(dist>0.05) {
                float limit=dist-0.03;
                blocked=shadowed(origin,ray/dist,limit);
            }
        }
        if(!blocked)direct+=selectedRadiance*(totalWeight/selectedWeight);
    }
    direct/=max(1.0,float(p.counts.w));
    imageStore(directOutput,texel,vec4(clamp(direct,vec3(0.0),vec3(65504.0)),1.0));
}
)GLSL";
} // namespace aiverse::render::luma_vk
