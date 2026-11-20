// Shared Metal BVH ray query for both Hybrid Dynamic GI and LumaWeave direct shadows.
// C++ buffer layouts live in GiGpuBvh.h. This literal is intentionally expanded
// in both compute shaders; do NOT maintain two independent intersection copies.
#pragma once
#define AIVERSE_MSL_GI_BVH_RAYQUERY R"MSL(
struct Tri {
    float4 a, b, c, albedo;
};
struct Node {
    float4 lo, hi;
    uint4 meta; // left, right, first, count; count > 0 means leaf
};

struct Hit {
    float distance;
    float3 normal;
    float3 albedo;
};
static bool hitsBox(float3 o,float3 d,float radius,Node node) {
    float entry=0.0f, exit=radius;
    for(uint axis=0;axis<3u;++axis) {
        float p=o[axis],v=d[axis],mn=node.lo[axis],mx=node.hi[axis];
        if(abs(v)<1e-10f) {
            if(p<mn || p>mx)return false;
            continue;
        }
        float t0=(mn-p)/v,t1=(mx-p)/v;
        if(t0>t1) {float tmp=t0;t0=t1;t1=tmp;}
        entry=max(entry,t0);
        exit=min(exit,t1);
        if(entry>exit)return false;
    }
    return true;
}
static Hit traceBvh(float3 origin,float3 direction,float limit,
                    device const Tri* tris,device const Node* nodes,uint nodeCount) {
    Hit hit{limit,float3(0.0),float3(0.0)};
    if(nodeCount==0u)return hit;
    uint stack[64];
    uint top=1u;
    stack[0]=0u;
    while(top!=0u) {
        uint nodeIndex=stack[--top];
        if(nodeIndex>=nodeCount)continue;
        Node node=nodes[nodeIndex];
        if(!hitsBox(origin,direction,hit.distance,node))continue;
        uint count=node.meta.w;
        if(count!=0u) {
            for(uint j=node.meta.z;j<node.meta.z+count;++j) {
                Tri t=tris[j];
                float3 e1=t.b.xyz-t.a.xyz,e2=t.c.xyz-t.a.xyz;
                float3 crossD=cross(direction,e2);
                float det=dot(e1,crossD);
                if(abs(det)<1e-10f)continue;
                float inv=1.0f/det;
                float3 s=origin-t.a.xyz;
                float u=dot(s,crossD)*inv;
                if(u<0.0f||u>1.0f)continue;
                float3 q=cross(s,e1);
                float v=dot(direction,q)*inv;
                if(v<0.0f||u+v>1.0f)continue;
                float d=dot(e2,q)*inv;
                if(d>0.001f && d<hit.distance) {
                    hit.distance=d;
                    hit.normal=normalize(cross(e1,e2));
                    hit.albedo=t.albedo.xyz;
                }
            }
        } else {
            // Median splits cap tree depth; 64 entries far exceeds the maximum.
            uint children=node.meta.y==0xffffffffu?1u:2u;
            if(top+children<=64u) {
                if(children==2u)stack[top++]=node.meta.y;
                stack[top++]=node.meta.x;
            }
        }
    }
    return hit;
}
)MSL"
