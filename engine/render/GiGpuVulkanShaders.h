// Vulkan 1.1/SPIR-V version of GiGpuSmallSceneShaders.h one-bounce GI.
// ABI: std430 Tri=64 B, Node=48 B, Lamp=32 B, Job=16 B;
// push constants=32 B, exactly GiGpuSmallScene::Uniforms.
// Descriptor set 0: storage buffers 0..3, rgba16f storage image 4.
// Local size X=64. No Vulkan shader extension or subgroup dependency.
// This is independent INDIRECT transport; it never produces direct light.
#pragma once

namespace aiverse::render::gi_gpu {
inline constexpr const char* kVulkanSmallScene = R"GLSL(
#version 450
layout(local_size_x=64, local_size_y=1, local_size_z=1) in;
struct Tri { vec4 a, b, c, albedo; };
struct Node { vec4 lo, hi; uvec4 meta; };
struct Lamp { vec4 posRadius, colorIntensity, spotDirectionOuter, spotInner; };
struct Job { uint index; float x, y, z; };
layout(set=0,binding=0,std430) readonly buffer Triangles { Tri tris[]; };
layout(set=0,binding=1,std430) readonly buffer Lamps { Lamp lamps[]; };
layout(set=0,binding=2,std430) readonly buffer Jobs { Job jobs[]; };
layout(set=0,binding=3,std430) readonly buffer Nodes { Node nodes[]; };
layout(set=0,binding=4,rgba16f) uniform writeonly image2D outputImage;
layout(push_constant) uniform GIParams {
    uint triangles;
    uint lights;
    uint jobs;
    uint samples;
    float traceDistance;
    uint nodes;
    float pad1;
    float pad2;
    vec4 sunDirectionIntensity;
    vec4 sunColorSkyIntensity;
    vec4 skyColor;
} p;
struct Hit { float distance; vec3 normal; vec3 albedo; };

bool hitsBox(vec3 origin, vec3 direction, float limit, Node node) {
    float entry=0.0, exit=limit;
    for(int axis=0;axis<3;++axis) {
        float pos=origin[axis], step=direction[axis];
        float minimum=node.lo[axis], maximum=node.hi[axis];
        if(abs(step)<1e-10) {
            if(pos<minimum || pos>maximum) return false;
            continue;
        }
        float t0=(minimum-pos)/step, t1=(maximum-pos)/step;
        if(t0>t1) { float temporary=t0; t0=t1; t1=temporary; }
        entry=max(entry,t0);
        exit=min(exit,t1);
        if(entry>exit) return false;
    }
    return true;
}
Hit traceBvh(vec3 origin, vec3 direction, float limit) {
    Hit hit=Hit(limit,vec3(0.0),vec3(0.0));
    if(p.nodes==0u) return hit;
    uint stack[64];
    uint top=1u;
    stack[0]=0u;
    while(top>0u) {
        uint index=stack[--top];
        if(index>=p.nodes) continue;
        Node node=nodes[index];
        if(!hitsBox(origin,direction,hit.distance,node)) continue;
        uint count=node.meta.w;
        if(count>0u) {
            for(uint j=node.meta.z; j<node.meta.z+count && j<p.triangles; ++j) {
                Tri t=tris[j];
                vec3 edge1=t.b.xyz-t.a.xyz, edge2=t.c.xyz-t.a.xyz;
                vec3 crossDirection=cross(direction,edge2);
                float determinant=dot(edge1,crossDirection);
                if(abs(determinant)<1e-10) continue;
                float inverse=1.0/determinant;
                vec3 fromA=origin-t.a.xyz;
                float u=dot(fromA,crossDirection)*inverse;
                if(u<0.0 || u>1.0) continue;
                vec3 q=cross(fromA,edge1);
                float v=dot(direction,q)*inverse;
                if(v<0.0 || u+v>1.0) continue;
                float distance=dot(edge2,q)*inverse;
                if(distance>0.001 && distance<hit.distance) {
                    hit.distance=distance;
                    hit.normal=normalize(cross(edge1,edge2));
                    hit.albedo=t.albedo.xyz;
                }
            }
        } else {
            uint children=node.meta.y==0xffffffffu?1u:2u;
            if(top+children<=64u) {
                if(children==2u) stack[top++]=node.meta.y;
                stack[top++]=node.meta.x;
            }
        }
    }
    return hit;
}
void main() {
    uint thread=gl_GlobalInvocationID.x;
    if(thread>=p.jobs) return;
    Job job=jobs[thread];
    vec3 position=vec3(job.x,job.y,job.z);
    vec3 coefficient=vec3(0.0), cx=vec3(0.0), cy=vec3(0.0), cz=vec3(0.0);
    for(uint i=0u;i<p.samples;++i) {
        float y=1.0-2.0*(float(i)+0.5)/float(p.samples);
        float phi=float(i)*2.39996323;
        float radius=sqrt(max(0.0,1.0-y*y));
        vec3 ray=vec3(radius*cos(phi),y,radius*sin(phi));
        Hit first=traceBvh(position,ray,p.traceDistance);
        if(first.distance>=p.traceDistance || first.distance<=0.05 ||
           dot(first.normal,first.normal)<0.5) continue;
        vec3 normal=first.normal;
        if(dot(normal,-ray)<0.0) normal=-normal;
        vec3 surface=position+ray*first.distance+normal*0.03;
        vec3 outgoing=vec3(0.0);
        for(uint l=0u;l<p.lights;++l) {
            Lamp light=lamps[l];
            if(light.colorIntensity.w<=0.0) continue;
            vec3 toLight=light.posRadius.xyz-surface;
            float distanceSquared=dot(toLight,toLight);
            float reach=light.posRadius.w;
            if(distanceSquared<0.0025 || distanceSquared>=reach*reach) continue;
            float distance=sqrt(distanceSquared);
            vec3 L=toLight/distance;
            float ndl=max(0.0,dot(normal,L));
            if(ndl==0.0) continue;
            if(traceBvh(surface,L,distance).distance<distance-0.03) continue;
            float falloff=1.0-distanceSquared/(reach*reach);
            float cone=1.0;
            if(light.spotDirectionOuter.w>-1.5) {
                float angle=dot(light.spotDirectionOuter.xyz,-L);
                cone=clamp((angle-light.spotDirectionOuter.w)/
                           max(light.spotInner.x-light.spotDirectionOuter.w,1e-5),0.0,1.0);
                if(cone<=0.0)continue;
            }
            float energy=light.colorIntensity.w*ndl*cone*falloff*falloff/(1.0+distanceSquared);
            outgoing+=light.colorIntensity.xyz*first.albedo*energy;
        }
        // Explicit opt-in first-bounce sun and overhead sky on the hit
        // surface. Mirrors the CPU/Metal transport; not receiver direct light.
        if(p.sunDirectionIntensity.w>0.0) {
            float nds=max(0.0,dot(normal,p.sunDirectionIntensity.xyz));
            if(nds>0.0 &&
               traceBvh(surface,p.sunDirectionIntensity.xyz,p.traceDistance).distance>=
                   p.traceDistance-0.03)
                outgoing+=p.sunColorSkyIntensity.xyz*first.albedo*
                          (p.sunDirectionIntensity.w*nds);
        }
        if(p.sunColorSkyIntensity.w>0.0) {
            float skyCos=max(0.0,normal.y);
            if(skyCos>0.0 &&
               traceBvh(surface,vec3(0.0,1.0,0.0),p.traceDistance).distance>=
                   p.traceDistance-0.03)
                outgoing+=p.skyColor.xyz*first.albedo*
                          (p.sunColorSkyIntensity.w*skyCos);
        }
        vec3 v=outgoing/float(p.samples);
        coefficient+=v;
        cx+=2.0*v*ray.x;
        cy+=2.0*v*ray.y;
        cz+=2.0*v*ray.z;
    }
    uint offset=job.index*3u;
    imageStore(outputImage,ivec2(int(offset%384u),int(offset/384u)),
               vec4(coefficient.x,cx.x,cy.x,cz.x));
    imageStore(outputImage,ivec2(int((offset+1u)%384u),int((offset+1u)/384u)),
               vec4(coefficient.y,cx.y,cy.y,cz.y));
    imageStore(outputImage,ivec2(int((offset+2u)%384u),int((offset+2u)/384u)),
               vec4(coefficient.z,cx.z,cy.z,cz.z));
}
)GLSL";
} // namespace aiverse::render::gi_gpu
