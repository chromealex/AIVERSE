// Shared Metal BVH ray query for both Hybrid Dynamic GI and LumaWeave direct shadows.
// C++ buffer layouts live in GiGpuBvh.h. This literal is intentionally expanded
// in both compute shaders; do NOT maintain two independent intersection copies.
#pragma once
#define AIVERSE_MSL_GI_BVH_RAYQUERY \
    "\n" \
    "struct Tri {\n" \
    "    float4 a, b, c, albedo;\n" \
    "};\n" \
    "struct Node {\n" \
    "    float4 lo, hi;\n" \
    "    uint4 meta; // left, right, first, count; count > 0 means leaf\n" \
    "};\n" \
    "\n" \
    "struct Hit {\n" \
    "    float distance;\n" \
    "    float3 normal;\n" \
    "    float3 albedo;\n" \
    "};\n" \
    "static bool hitsBox(float3 o,float3 d,float radius,Node node) {\n" \
    "    float entry=0.0f, exit=radius;\n" \
    "    for(uint axis=0;axis<3u;++axis) {\n" \
    "        float p=o[axis],v=d[axis],mn=node.lo[axis],mx=node.hi[axis];\n" \
    "        if(abs(v)<1e-10f) {\n" \
    "            if(p<mn || p>mx)return false;\n" \
    "            continue;\n" \
    "        }\n" \
    "        float t0=(mn-p)/v,t1=(mx-p)/v;\n" \
    "        if(t0>t1) {float tmp=t0;t0=t1;t1=tmp;}\n" \
    "        entry=max(entry,t0);\n" \
    "        exit=min(exit,t1);\n" \
    "        if(entry>exit)return false;\n" \
    "    }\n" \
    "    return true;\n" \
    "}\n" \
    "static Hit traceBvh(float3 origin,float3 direction,float limit,\n" \
    "                    device const Tri* tris,device const Node* nodes,uint nodeCount) {\n" \
    "    Hit hit{limit,float3(0.0),float3(0.0)};\n" \
    "    if(nodeCount==0u)return hit;\n" \
    "    uint stack[64];\n" \
    "    uint top=1u;\n" \
    "    stack[0]=0u;\n" \
    "    while(top!=0u) {\n" \
    "        uint nodeIndex=stack[--top];\n" \
    "        if(nodeIndex>=nodeCount)continue;\n" \
    "        Node node=nodes[nodeIndex];\n" \
    "        if(!hitsBox(origin,direction,hit.distance,node))continue;\n" \
    "        uint count=node.meta.w;\n" \
    "        if(count!=0u) {\n" \
    "            for(uint j=node.meta.z;j<node.meta.z+count;++j) {\n" \
    "                Tri t=tris[j];\n" \
    "                float3 e1=t.b.xyz-t.a.xyz,e2=t.c.xyz-t.a.xyz;\n" \
    "                float3 crossD=cross(direction,e2);\n" \
    "                float det=dot(e1,crossD);\n" \
    "                if(abs(det)<1e-10f)continue;\n" \
    "                float inv=1.0f/det;\n" \
    "                float3 s=origin-t.a.xyz;\n" \
    "                float u=dot(s,crossD)*inv;\n" \
    "                if(u<0.0f||u>1.0f)continue;\n" \
    "                float3 q=cross(s,e1);\n" \
    "                float v=dot(direction,q)*inv;\n" \
    "                if(v<0.0f||u+v>1.0f)continue;\n" \
    "                float d=dot(e2,q)*inv;\n" \
    "                if(d>0.001f && d<hit.distance) {\n" \
    "                    hit.distance=d;\n" \
    "                    hit.normal=normalize(cross(e1,e2));\n" \
    "                    hit.albedo=t.albedo.xyz;\n" \
    "                }\n" \
    "            }\n" \
    "        } else {\n" \
    "            // Median splits cap tree depth; 64 entries far exceeds the maximum.\n" \
    "            uint children=node.meta.y==0xffffffffu?1u:2u;\n" \
    "            if(top+children<=64u) {\n" \
    "                if(children==2u)stack[top++]=node.meta.y;\n" \
    "                stack[top++]=node.meta.x;\n" \
    "            }\n" \
    "        }\n" \
    "    }\n" \
    "    return hit;\n" \
    "}\n"
