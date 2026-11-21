#pragma once
// STRATA Stage 16.1 Vulkan diagnostic cluster selection. Complete GLSL450,
// matched to GpuMetadataHeader/GpuPrimitive/GpuNode and GpuSelectedCluster.
// This kernel only produces candidate ranges/counters; NO draw submission.
// No CPU readback is used during rendering. Async readback is diagnostics only.
namespace aiverse::render::strata {
inline constexpr const char* kVulkanGpuCulling=R"GLSL(#version 450
layout(local_size_x=64, local_size_y=1, local_size_z=1) in;

layout(std430,set=0,binding=0) readonly buffer Metadata {uint words[];} metadata;
struct Instance {
    mat4 objectToWorld;
    uvec4 fields; // primitiveIndex, enabled, instanceToken, reserved
};
layout(std430,set=0,binding=1) readonly buffer Instances {Instance data[];} instances;
struct Selected {
    uvec4 first;  // token, firstIndex, count, material
    uvec4 second; // globalNode, primitiveIndex, localFirstIndex, reserved
};
layout(std430,set=0,binding=2) buffer SelectedRanges {Selected records[];} ranges;
layout(std430,set=0,binding=3) buffer Counters {uint c[4];} counts;
struct IndexedCommand {
    uint indexCount, instanceCount, firstIndex;
    int vertexOffset;
    uint firstInstance;
};
layout(std430,set=0,binding=4) buffer IndexedIndirect {
    IndexedCommand draws[];
} indirect;
// Temporal buffers are bound ONLY for the opt-in path. Each global node
// occupies exactly one uint. Previous and next must be different GPU
// buffers; history is never updated in place while an older frame reads it.
layout(std430,set=0,binding=5) readonly buffer PreviousRefinement {
    uint refined[];
} temporalPrevious;
layout(std430,set=0,binding=6) buffer NextRefinement {
    uint refined[];
} temporalNext;

layout(push_constant) uniform Params {
    mat4 viewProjection;
    uint instanceCount, outputCapacity, width, height;
    float thresholdPixels;
    uint clipDepthZeroToOne, frustumCull, reserved;
    uint temporalNodeCount, temporalHasPrevious;
    float hysteresisFraction;
    uint temporalReserved;
} params;

uint wordAt(uint byteOffset) {return metadata.words[byteOffset>>2];}
struct Node {
    vec3 low, high;
    uint child, children, firstIndex, count, material, resident;
    float error;
};
Node getNode(uint nodesOffset,uint id) {
    uint address=nodesOffset+id*64u;
    Node n;
    n.low=vec3(uintBitsToFloat(wordAt(address+0u)),
               uintBitsToFloat(wordAt(address+4u)),
               uintBitsToFloat(wordAt(address+8u)));
    n.high=vec3(uintBitsToFloat(wordAt(address+16u)),
                uintBitsToFloat(wordAt(address+20u)),
                uintBitsToFloat(wordAt(address+24u)));
    n.child=wordAt(address+32u);
    n.children=wordAt(address+36u);
    n.firstIndex=wordAt(address+40u);
    n.count=wordAt(address+44u);
    n.material=wordAt(address+48u);
    n.resident=wordAt(address+52u);
    n.error=uintBitsToFloat(wordAt(address+56u));
    return n;
}
void invalid(){atomicOr(counts.c[3],1u);}
bool bad(vec4 a){return any(isnan(a)) || any(isinf(a));}

float projectedError(mat4 clip,Node node) {
    float nearW=1.0e30, maxX=0.0, maxY=0.0;
    for(uint c=0u;c<8u;++c){
        vec3 p=vec3((c&1u)!=0u?node.high.x:node.low.x,
                    (c&2u)!=0u?node.high.y:node.low.y,
                    (c&4u)!=0u?node.high.z:node.low.z);
        vec4 q=clip*vec4(p,1.0);
        if(bad(q)||q.w<=0.0||
           q.z<(params.clipDepthZeroToOne!=0u?0.0:-q.w))
            return 1.0e30;
        nearW=min(nearW,q.w);
        maxX=max(maxX,abs(q.x));
        maxY=max(maxY,abs(q.y));
    }
    float e=node.error;
    float nx=length(vec3(clip[0][0],clip[1][0],clip[2][0]));
    float ny=length(vec3(clip[0][1],clip[1][1],clip[2][1]));
    float nw=length(vec3(clip[0][3],clip[1][3],clip[2][3]));
    float delta=nw*e;
    if(nearW<=delta)return 1.0e30;
    float px=0.5*max(float(params.width)*e*(nx*nearW+maxX*nw),
                     float(params.height)*e*(ny*nearW+maxY*nw))/
             (nearW*(nearW-delta));
    if(isnan(px)||isinf(px))return 1.0e30;
    return px;
}
bool outside(mat4 clip,Node node) {
    uint allOutside=63u;
    for(uint c=0u;c<8u;++c){
        vec3 p=vec3((c&1u)!=0u?node.high.x:node.low.x,
                    (c&2u)!=0u?node.high.y:node.low.y,
                    (c&4u)!=0u?node.high.z:node.low.z);
        vec4 q=clip*vec4(p,1.0);
        if(bad(q))return false;
        uint flags=0u;
        if(q.x<-q.w)flags|=1u;
        if(q.x>q.w)flags|=2u;
        if(q.y<-q.w)flags|=4u;
        if(q.y>q.w)flags|=8u;
        if(q.z<-q.w)flags|=16u; // exact geometryBoxVisible oracle
        if(q.z>q.w)flags|=32u;
        allOutside&=flags;
    }
    return allOutside!=0u;
}
void main(){
    uint tid=gl_GlobalInvocationID.x;
    if(tid>=params.instanceCount)return;
    if(wordAt(0u)!=2u){invalid();return;}
    uint primitiveCount=wordAt(4u), nodeCount=wordAt(8u);
    uint rootCount=wordAt(12u);
    uint primitivesOffset=wordAt(16u), nodesOffset=wordAt(20u);
    uint rootsOffset=wordAt(24u);
    if(primitiveCount==0u||nodeCount==0u||rootCount==0u){
        invalid();return;
    }
    Instance instance=instances.data[tid];
    if(instance.fields.y==0u)return;
    uint primitiveID=instance.fields.x;
    if(primitiveID>=primitiveCount){invalid();return;}
    uint primitiveAddr=primitivesOffset+primitiveID*32u;
    uint nodeBase=wordAt(primitiveAddr+12u);
    uint nodeLength=wordAt(primitiveAddr+16u);
    uint rootBase=wordAt(primitiveAddr+20u);
    uint primitiveRoots=wordAt(primitiveAddr+24u);
    uint gpuFirstIndex=wordAt(primitiveAddr+28u);
    if(gpuFirstIndex==0xffffffffu ||
       nodeBase>nodeCount||nodeLength>nodeCount-nodeBase ||
       rootBase>rootCount||primitiveRoots>rootCount-rootBase){
        invalid();return;
    }
    mat4 clip=params.viewProjection*instance.objectToWorld;
    for(uint r=0u;r<primitiveRoots;++r){
        uint stack[128];
        uint top=0u;
        uint root=wordAt(rootsOffset+4u*(rootBase+r));
        if(root<nodeBase || root>=nodeBase+nodeLength){
            invalid();return;
        }
        stack[top++]=root;
        uint visits=0u;
        while(top!=0u){
            if(++visits>nodeLength){invalid();return;}
            uint id=stack[--top];
            if(id>=nodeCount){invalid();return;}
            Node node=getNode(nodesOffset,id);
            atomicAdd(counts.c[2],1u);
            if(node.resident==0u){invalid();return;}
            float threshold=params.thresholdPixels;
            if(params.hysteresisFraction>0.0 &&
               params.temporalHasPrevious!=0u &&
               id<params.temporalNodeCount){
                const bool wasFine=temporalPrevious.refined[id]!=0u;
                threshold*=wasFine
                    ? (1.0-params.hysteresisFraction)
                    : (1.0+params.hysteresisFraction);
            }
            bool refine=node.children!=0u &&
                projectedError(clip,node)>threshold;
            if(refine){
                if(node.child<nodeBase||node.child>=nodeBase+nodeLength ||
                   node.children>nodeBase+nodeLength-node.child){
                    invalid();return;
                }
                for(uint c=0u;c<node.children;++c)
                    if(getNode(nodesOffset,node.child+c).resident==0u){
                        refine=false;
                        break;
                    }
                if(refine && top+node.children>128u){invalid();return;}
            }
            if(params.hysteresisFraction>0.0 &&
               id<params.temporalNodeCount)
                temporalNext.refined[id]=refine?1u:0u;
            if(refine){
                for(uint c=node.children;c>0u;--c)
                    stack[top++]=node.child+c-1u;
                continue;
            }
            if(params.frustumCull!=0u && outside(clip,node))continue;
            if(node.count==0u || gpuFirstIndex>0xffffffffu-node.firstIndex){
                invalid();return;
            }
            uint at=atomicAdd(counts.c[0],1u);
            if(at>=params.outputCapacity){
                atomicOr(counts.c[1],1u);
                continue;
            }
            ranges.records[at].first=uvec4(instance.fields.z,
                gpuFirstIndex+node.firstIndex,node.count,node.material);
            ranges.records[at].second=uvec4(id,primitiveID,
                node.firstIndex,0u);
            // One-source uniform/material/transform contract is checked by
            // the (future) draw-route owner. The GPU only writes indices.
            indirect.draws[at].indexCount=node.count;
            indirect.draws[at].instanceCount=1u;
            indirect.draws[at].firstIndex=gpuFirstIndex+node.firstIndex;
            indirect.draws[at].vertexOffset=int(params.reserved);
            indirect.draws[at].firstInstance=0u;
        }
    }
}
)GLSL";

// Zero NEXT temporal state on GPU before traversal. Unvisited nodes must
// be coarse (0), matching CPU SelectionOutput::refinedNodes behavior.
inline constexpr const char* kVulkanTemporalReset=R"GLSL(#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
layout(std430,set=0,binding=0) buffer NextRefinement {
    uint refined[];
} nextState;
layout(push_constant) uniform Params { uint count; } params;
void main(){
    uint id=gl_GlobalInvocationID.x;
    if(id<params.count)nextState.refined[id]=0u;
}
)GLSL";

// Clear all indirect command slots on GPU before selection. This avoids a
// large per-frame CPU zero upload and guarantees that entries beyond
// producedCount are harmless (instanceCount=0). Invalid/overflow results
// are additionally invalidated by the separate safety pass.
inline constexpr const char* kVulkanIndirectClear=R"GLSL(#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
struct IndexedCommand {
    uint indexCount, instanceCount, firstIndex;
    int vertexOffset;
    uint firstInstance;
};
layout(std430,set=0,binding=0) buffer Indirect {
    IndexedCommand commands[];
} indirect;
layout(push_constant) uniform Params {
    uint capacity;
} settings;
void main(){
    const uint id=gl_GlobalInvocationID.x;
    if(id>=settings.capacity)return;
    indirect.commands[id].indexCount=0u;
    indirect.commands[id].instanceCount=0u;
    indirect.commands[id].firstIndex=0u;
    indirect.commands[id].vertexOffset=0;
    indirect.commands[id].firstInstance=0u;
}
)GLSL";

// A second GPU-only pass invalidates *all* GPU-authored commands on a
// selection overflow or invalid metadata, without reading counters on CPU.
// Each command slot is pre-zeroed; only produced records are potentially
// nonzero. Submission remains opt-in until renderer material-state routing
// and production image parity are proven.
inline constexpr const char* kVulkanIndirectSafety=R"GLSL(#version 450
layout(local_size_x=64,local_size_y=1,local_size_z=1) in;
struct IndexedCommand {
    uint indexCount, instanceCount, firstIndex;
    int vertexOffset;
    uint firstInstance;
};
layout(std430,set=0,binding=0) buffer Indirect {
    IndexedCommand commands[];
} indirect;
layout(std430,set=0,binding=1) readonly buffer Counters {
    uint produced, overflow, examined, invalid;
} counters;
layout(std430,set=0,binding=2) readonly buffer PreviousRefinement {
    uint refined[];
} temporalPrevious;
layout(std430,set=0,binding=3) buffer NextRefinement {
    uint refined[];
} temporalNext;
layout(push_constant) uniform Params {
    uint capacity, fallbackEnabled, fallbackFirstIndex, fallbackIndexCount;
    int fallbackBaseVertex;
    uint temporalNodeCount, temporalHasPrevious;
} settings;
void main(){
    const uint id=gl_GlobalInvocationID.x;
    bool invalidSelection=counters.overflow!=0u ||
        counters.invalid!=0u || counters.produced>settings.capacity;
    if(id<settings.temporalNodeCount && invalidSelection){
        // A failed selected frontier MUST NOT advance temporal history.
        // No synchronous counter readback is necessary.
        temporalNext.refined[id]=settings.temporalHasPrevious!=0u
            ? temporalPrevious.refined[id] : 0u;
    }
    if(id>=settings.capacity)return;
    if(invalidSelection){
        // A validated one-material source can render its ORIGINAL indexed
        // primitive instead of disappearing on GPU selection failure.
        // Without that strict host contract all commands are disabled and
        // normal CPU/Legacy routing must own the production presentation.
        if(id==0u && settings.fallbackEnabled!=0u){
            indirect.commands[id].indexCount=settings.fallbackIndexCount;
            indirect.commands[id].instanceCount=1u;
            indirect.commands[id].firstIndex=settings.fallbackFirstIndex;
            indirect.commands[id].vertexOffset=settings.fallbackBaseVertex;
            indirect.commands[id].firstInstance=0u;
        }else{
            indirect.commands[id].indexCount=0u;
            indirect.commands[id].instanceCount=0u;
        }
    }
}
)GLSL";

} // namespace aiverse::render::strata
