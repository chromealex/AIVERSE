// Stage 115: production ComputeParticles + Metal RHI allocator acceptance.
// No software GPU mock. No window or screenshot prerequisite.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "engine/fx/SpectraGpuParticles.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <stdexcept>
#include <string>

using namespace aiverse;
using namespace aiverse::spectra;
namespace {
constexpr int side=128;
void require(bool ok,const char* label){
    if(!ok)throw std::runtime_error(label);
}
struct Frame {
    gfx::Device& device;
    explicit Frame(gfx::Device& d):device(d){d.beginFrame(side,side);}
    ~Frame(){device.endFrame();}
};
fx::SpectraFxView view(float x=0,float eye=0){
    fx::SpectraFxView v;
    v.width=v.height=side;v.eye={0,0,eye};
    v.viewProj=render::mat4::identity();v.viewProj.m[12]=x;
    return v;
}
void burst(ComputeParticles& p,uint32_t n,uint64_t owner,fx::float3 pos){
    require(n%64==0,"burst alignment");
    for(uint32_t i=0;i<n;i+=64)require(p.burstAt(pos,{0,1,0},{1,1,1,1},owner),"birth queue");
}
ComputeParticles::AllocationAudit audit(gfx::Device& d,ComputeParticles& p,uint32_t alive){
    require(d.waitIdle()&&d.gpuHealthy(),"GPU completion");
    ComputeParticles::AllocationAudit a;
    require(p.auditGpuAllocation(a),"exact slot partition/draw audit");
    require(a.counters.alive==alive&&a.counters.free==a.capacity-alive&&a.vmErrors==0,"alive/free/VM invariant");
    return a;
}
void tick(gfx::Device& d,ComputeParticles& p,float dt=0){
    Frame frame(d);require(p.step(dt),p.reason().c_str());
}
ComputeEmitterDesc config(uint32_t cap,bool procedural){
    ComputeEmitterDesc cfg;
    cfg.capacity=cap;cfg.burst=64;cfg.lifetime=.25f;cfg.speed=0;cfg.gravityY=0;
    cfg.size=.01f;cfg.backend=ComputePreference::RequireGpu;
    cfg.gpuIndirect=true;cfg.gpuOwnedAllocation=true;cfg.proceduralBillboards=procedural;
    return cfg;
}
void stress(gfx::Device& d,uint32_t cap,bool procedural){
    @autoreleasepool {
        ComputeParticles p;
        require(p.init(&d,config(cap,procedural)),p.reason().c_str());
        require(p.demandGrowthActive()&&p.proceduralBillboardsActive()==procedural,"physical backend capability");
        require(p.allocatedCapacity()==256,"initial page");
        p.setView(view());
        constexpr uint64_t ownerA=0x1234567800000001ull,ownerB=0x2234567800000002ull;
        uint32_t alive=0;
        auto start=std::chrono::steady_clock::now();
        for(uint32_t target=256;;target=std::min(cap,target*2)){
            {
                Frame frame(d);burst(p,target-alive,ownerA,{0,0,.5f});
                require(p.step(0),p.reason().c_str());
            }
            alive=target;auto a=audit(d,p,alive);
            require(a.capacity==target&&a.counters.visible==alive,"growth and visible population");
            require(std::all_of(a.liveOwners.begin(),a.liveOwners.end(),[](uint64_t x){return x==ownerA;}),"owner continuity");
            if(target==cap)break;
        }
        require(p.stats().gpuAliveIndirectDispatches>0,"real indirect compute dispatch");
        p.setView(view(10));tick(d,p);
        require(audit(d,p,alive).counters.visible==0,"all offscreen, still alive");
        p.setView(view());tick(d,p);
        require(audit(d,p,alive).counters.visible==alive,"reenter without respawn");
        {
            Frame frame(d);require(p.killSourceParticles(ownerA),"kill source");
            require(p.step(0),p.reason().c_str());
        }
        audit(d,p,0);
        {
            Frame frame(d);burst(p,256,ownerB,{0,0,.5f});
            require(p.step(0),p.reason().c_str());
        }
        auto reused=audit(d,p,256);
        require(std::all_of(reused.liveOwners.begin(),reused.liveOwners.end(),[](uint64_t x){return x==ownerB;}),"source reuse");
        for(int i=0;i<3;++i)tick(d,p,.1f);
        audit(d,p,0);
        if(cap>256){
            // Let idle proof complete on the actual Metal queue; no host
            // estimate is accepted as proof of empty physical ownership.
            for(uint32_t i=0;i<kGpuIdleReclaimSteps+8&&p.allocatedCapacity()>256;++i){
                tick(d,p);require(d.waitIdle(),"idle proof completion");
            }
            require(p.allocatedCapacity()==256&&p.stats().gpuPhysicalShrinks>0,"automatic empty shrink");
        }
        {
            Frame frame(d);burst(p,256,ownerA,{0,0,.5f});
            require(p.step(0),p.reason().c_str());
            require(p.tryClear(),"checked reset");
        }
        audit(d,p,0);
        const double ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        std::printf("PASS allocator cap=%u procedural=%d growth=%u shrink=%u bytes=%zu elapsed_ms=%.2f (includes QA readbacks)\n",
            cap,int(procedural),p.stats().gpuPhysicalGrowths,p.stats().gpuPhysicalShrinks,p.stats().gpuBytes,ms);
    }
}
void lod(gfx::Device& d,bool procedural){
    auto cfg=config(256,procedural);cfg.quality=fx::FxQuality::Medium;
    ComputeParticles p;require(p.init(&d,cfg),p.reason().c_str());
    p.setView(view(0,150));
    {Frame frame(d);burst(p,256,1,{0,0,.5f});require(p.step(0),p.reason().c_str());}
    auto a=audit(d,p,256);
    require(a.counters.visible>0&&a.counters.visible<256&&a.counters.visible+a.counters.lodRejected==256,"far tiny LOD");
    tick(d,p);require(audit(d,p,256).counters.visible==a.counters.visible,"stable frozen LOD");
    p.setView(view());tick(d,p);require(audit(d,p,256).counters.visible==256,"near full density");
    std::printf("PASS LOD procedural=%d far=%u rejected=%u\n",int(procedural),a.counters.visible,a.counters.lodRejected);
}
void memory(gfx::Device& d){
    auto small=estimateGpuEmitterAllocation(256,false),large=estimateGpuEmitterAllocation(512,false);
    auto budget=std::make_shared<GpuAllocationBudget>(small+large);
    auto cfg=config(65536,false);cfg.allocationBudget=budget;
    ComputeParticles a,b;require(a.init(&d,cfg)&&b.init(&d,cfg),"shared physical budget init");
    a.setView(view());
    {
        Frame frame(d);burst(a,320,1,{0,0,.5f});
        require(a.step(0)&&a.allocatedCapacity()==256&&a.stats().gpuAllocationBudgetDenied>0,"denial preserves simulation");
    }
    audit(d,a,256);b.shutdown();
    {
        Frame frame(d);burst(a,256,2,{0,0,.5f});
        require(a.step(0)&&a.allocatedCapacity()==512,"budget growth after credit");
        require(budget->stats().retiring==small&&budget->stats().charged()==small+large,"in-flight overlap charge");
        require(!b.init(&d,cfg),"in-flight admission rejected");
    }
    audit(d,a,512);budget->collect(d.gpuCompletedFence());
    require(budget->stats().retiring==0&&b.init(&d,cfg),"completion credits budget");
    b.shutdown();a.shutdown();budget->collect(d.gpuCompletedFence());
    require(budget->stats().charged()==0,"budget teardown");
    std::printf("PASS shared budget peak=%zu denied=%llu\n",budget->stats().peak,(unsigned long long)budget->stats().denied);
}
}
int aiverseMain(int,char**){
    @autoreleasepool {
        auto metal=MTLCreateSystemDefaultDevice();
        if(!metal){std::puts("SKIP no Metal device");return 77;}
        CAMetalLayer* layer=[CAMetalLayer layer];layer.device=metal;
        layer.frame=CGRectMake(0,0,side,side);layer.drawableSize=CGSizeMake(side,side);
        auto d=gfx::createMetalDevice((__bridge void*)layer);
        if(!d||!d->supportsCompute()){std::puts("SKIP no Metal compute");return 77;}
        try{
            for(bool procedural:{false,true}){
                for(uint32_t cap:{256u,65536u,262144u})stress(*d,cap,procedural);
                lod(*d,procedural);
            }
            memory(*d);
            std::puts("PASS production Metal allocator acceptance");return 0;
        }catch(const std::exception& e){
            std::fprintf(stderr,"FAIL allocator acceptance: %s; RHI: %s\n",e.what(),d->lastError().c_str());return 1;
        }
    }
}
