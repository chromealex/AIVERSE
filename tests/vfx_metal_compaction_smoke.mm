// Standalone Apple Metal smoke for SpectraFX atomic compaction and GPU-authored
// indirect arguments. Executes the REAL MSL shader; no game window required.
// No GPU device on a CI runner => CTest SKIP (code 77), never a false PASS.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "engine/fx/SpectraGpuShaders.h"
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

using namespace aiverse::spectra;

namespace {
struct alignas(16) Vec4 { float x,y,z,w; };
struct Particle {
    Vec4 positionAge, velocityLife, tint, size;
};
struct Vertex {
    float x,y,z,u,v;
    uint32_t rgba;
    float g0,g1,g2,g3;
};
struct Uniforms {
    Vec4 gravityDt,rightSize,up;
    uint32_t meta[4];
};
struct DrawArgs {
    uint32_t indexCount,instanceCount,firstIndex;
    int32_t baseVertex;
    uint32_t firstInstance;
};
static_assert(sizeof(Particle)==64);
static_assert(sizeof(Vertex)==40);
static_assert(sizeof(Uniforms)==64);
static_assert(sizeof(DrawArgs)==20);

bool submit(id<MTLCommandQueue> queue,id<MTLComputePipelineState> pipeline,
            const std::array<id<MTLBuffer>,7>& buffers,
            uint32_t births,float dt) {
    id<MTLCommandBuffer> cb=[queue commandBuffer];
    if(!cb)return false;
    id<MTLComputeCommandEncoder> enc=[cb computeCommandEncoder];
    if(!enc)return false;
    [enc setComputePipelineState:pipeline];
    for(NSUInteger i=0;i<buffers.size();++i)
        [enc setBuffer:buffers[i] offset:0 atIndex:i];
    Uniforms u{};
    u.gravityDt={-9.81f,dt,0,0};
    u.rightSize={1,0,0,0.4f};
    u.up={0,1,0,0};
    u.meta[1]=4;
    u.meta[2]=births;
    constexpr MTLSize group={64,1,1};
    auto pass=[&](uint32_t mode,uint32_t count) {
        if(count==0)return;
        u.meta[0]=mode;
        [enc setBytes:&u length:sizeof(u) atIndex:8];
        [enc dispatchThreadgroups:MTLSizeMake((count+63)/64,1,1)
             threadsPerThreadgroup:group];
        // The RHI explicitly inserts this barrier after every dispatch.
        [enc memoryBarrierWithScope:MTLBarrierScopeBuffers];
    };
    pass(3,1);                 // atomic counter reset
    pass(0,births);            // upload new particles into physical state
    pass(1,4);                 // integrate all physical state slots
    pass(4,4);                 // atomic compact surviving particles into quads
    pass(5,1);                 // write 20-byte GPU indexed indirect arguments
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if(cb.status!=MTLCommandBufferStatusCompleted) {
        fprintf(stderr,"Metal command failed: %s\n",
                cb.error ? cb.error.localizedDescription.UTF8String : "unknown");
        return false;
    }
    return true;
}
bool verify(const std::array<id<MTLBuffer>,7>& buffers,uint32_t expected) {
    uint32_t count=UINT32_MAX;
    DrawArgs args{};
    std::memcpy(&count,buffers[5].contents,sizeof(count));
    std::memcpy(&args,buffers[6].contents,sizeof(args));
    if(count!=expected || args.indexCount!=expected*6 ||
       args.instanceCount!=1 || args.firstIndex!=0 ||
       args.baseVertex!=0 || args.firstInstance!=0) {
        fprintf(stderr,"Metal compact mismatch: count=%u args=(%u,%u), expected %u\n",
                count,args.indexCount,args.instanceCount,expected);
        return false;
    }
    return true;
}
} // namespace

int main() {
    @autoreleasepool {
        id<MTLDevice> device=MTLCreateSystemDefaultDevice();
        if(!device) {
            fprintf(stderr,"SKIP SpectraFX Metal compute: no GPU device on runner\n");
            return 77;
        }
        NSError* error=nil;
        NSString* source=[NSString stringWithUTF8String:shaders::kMetal];
        id<MTLLibrary> lib=[device newLibraryWithSource:source options:nil error:&error];
        if(!lib) {
            fprintf(stderr,"SpectraFX MSL compile failed: %s\n",
                    error ? error.localizedDescription.UTF8String : "unknown");
            return 1;
        }
        id<MTLFunction> kernel=[lib newFunctionWithName:@"cs_main"];
        if(!kernel) {fprintf(stderr,"Missing MSL cs_main\n");return 1;}
        id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:kernel error:&error];
        if(!pipeline||pipeline.maxTotalThreadsPerThreadgroup<64) {
            fprintf(stderr,"SpectraFX Metal pipeline failed: %s\n",
                    error ? error.localizedDescription.UTF8String : "64-thread compute unsupported");
            return 1;
        }
        id<MTLCommandQueue> queue=[device newCommandQueue];
        if(!queue)return 1;

        std::array<Particle,4> initial{};
        for(auto& p:initial) {p.positionAge.w=-1;p.velocityLife.w=1;}
        std::array<Particle,4> births{};
        births[0].positionAge={0,2,0,0};
        births[0].velocityLife={1,0,0,1};
        births[0].tint={1,0.7f,0.3f,1};
        births[0].size={0.4f,0,0,0};
        births[1]=births[0];
        births[1].positionAge.x=1;
        std::array<uint32_t,4> slots{0,1,0,0};
        std::array<uint32_t,4> zeroSlots{};
        uint32_t zero=0;
        DrawArgs args{0,1,0,0,0};

        std::array<id<MTLBuffer>,7> buffers{
            [device newBufferWithBytes:initial.data() length:sizeof(initial) options:MTLResourceStorageModeShared],
            [device newBufferWithBytes:births.data() length:sizeof(births) options:MTLResourceStorageModeShared],
            [device newBufferWithBytes:slots.data() length:sizeof(slots) options:MTLResourceStorageModeShared],
            [device newBufferWithLength:sizeof(Vertex)*4*4 options:MTLResourceStorageModeShared],
            [device newBufferWithBytes:zeroSlots.data() length:sizeof(zeroSlots) options:MTLResourceStorageModeShared],
            [device newBufferWithBytes:&zero length:sizeof(zero) options:MTLResourceStorageModeShared],
            [device newBufferWithBytes:&args length:sizeof(args) options:MTLResourceStorageModeShared]
        };
        for(auto b:buffers)if(!b){fprintf(stderr,"Metal buffer allocation failed\n");return 1;}

        if(!submit(queue,pipeline,buffers,2,0.1f)||!verify(buffers,2))return 1;
        const Vertex* vertices=static_cast<const Vertex*>(buffers[3].contents);
        if(vertices[0].rgba==0 || vertices[4].rgba==0) {
            fprintf(stderr,"Metal compacted quads have no visible colour\n");
            return 1;
        }
        if(!submit(queue,pipeline,buffers,0,1.1f)||!verify(buffers,0))return 1;

        // Reuse a freed physical slot and verify that GPU counter/draw args
        // are regenerated rather than carrying counts from the last frame.
        slots[0]=3;
        std::memcpy(buffers[2].contents,slots.data(),sizeof(slots));
        if(!submit(queue,pipeline,buffers,1,0.05f)||!verify(buffers,1))return 1;

        printf("PASS SpectraFX real Metal MSL compile, compute compact and GPU indirect args\n");
        return 0;
    }
}
