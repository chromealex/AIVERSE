// Real production Vulkan RHI acceptance. Software ICD is valid API evidence,
// not discrete-GPU performance evidence. No compatible device => CTest SKIP.
#include "engine/gfx/vulkan/Vulkan.h"
#include "engine/gfx/vulkan/VkShaderCompiler.h"
#include "engine/fx/SpectraGpuShaders.h"
#include "engine/fx/SpectraGpuParticles.h"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
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
    require(d.waitIdle()&&d.waitForSubmittedGpuWork()&&d.gpuHealthy(),"GPU completion");
    ComputeParticles::AllocationAudit a;
    require(p.auditGpuAllocation(a),"exact slot partition/draw audit");
    require(a.counters.alive==alive&&a.counters.free==a.capacity-alive&&a.vmErrors==0,"alive/free/VM invariant");
    return a;
}
void tick(gfx::Device& d,ComputeParticles& p,float dt=0){
    Frame frame(d);require(p.step(dt),"ComputeParticles init/step failed");
}
ComputeEmitterDesc config(uint32_t cap,bool procedural){
    ComputeEmitterDesc cfg;
    cfg.capacity=cap;cfg.burst=64;cfg.lifetime=.25f;cfg.speed=0;cfg.gravityY=0;
    cfg.size=.01f;cfg.backend=ComputePreference::RequireGpu;
    cfg.gpuIndirect=true;cfg.gpuOwnedAllocation=true;cfg.proceduralBillboards=procedural;
    return cfg;
}
void stress(gfx::Device& d,uint32_t cap,bool procedural){
    {
        ComputeParticles p;
        require(p.init(&d,config(cap,procedural)),"ComputeParticles init/step failed");
        require(p.demandGrowthActive()&&p.proceduralBillboardsActive()==procedural,"physical backend capability");
        require(p.allocatedCapacity()==256,"initial page");
        p.setView(view());
        constexpr uint64_t ownerA=0x1234567800000001ull,ownerB=0x2234567800000002ull;
        uint32_t alive=0;
        auto start=std::chrono::steady_clock::now();
        for(uint32_t target=256;;target=std::min(cap,target*2)){
            {
                Frame frame(d);burst(p,target-alive,ownerA,{0,0,.5f});
                require(p.step(0),"ComputeParticles init/step failed");
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
            require(p.step(0),"ComputeParticles init/step failed");
        }
        audit(d,p,0);
        {
            Frame frame(d);burst(p,256,ownerB,{0,0,.5f});
            require(p.step(0),"ComputeParticles init/step failed");
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
            require(p.step(0),"ComputeParticles init/step failed");
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
    ComputeParticles p;require(p.init(&d,cfg),"ComputeParticles init/step failed");
    p.setView(view(0,150));
    {Frame frame(d);burst(p,256,1,{0,0,.5f});require(p.step(0),"ComputeParticles init/step failed");}
    auto a=audit(d,p,256);
    require(a.counters.visible>0&&a.counters.visible<256&&a.counters.visible+a.counters.lodRejected==256,"far tiny LOD");
    tick(d,p);require(audit(d,p,256).counters.visible==a.counters.visible,"stable frozen LOD");
    p.setView(view());tick(d,p);require(audit(d,p,256).counters.visible==256,"near full density");
    std::printf("PASS LOD procedural=%d far=%u rejected=%u\n",int(procedural),a.counters.visible,a.counters.lodRejected);
}
void drawAcceptance(gfx::Device& d,bool procedural){
    auto cfg=config(256,procedural);cfg.size=.3f;
    ComputeParticles p;require(p.init(&d,cfg),"draw init");
    gfx::TextureDesc tex{};tex.width=tex.height=side;tex.renderTarget=true;
    tex.format=gfx::PixelFormat::RGBA8;auto color=d.createTexture(tex);
    tex.format=gfx::PixelFormat::Depth;auto depth=d.createTexture(tex);
    require(color.valid()&&depth.valid(),"offscreen target allocation");
    auto draw=[&](float offset,bool spawn){
        p.setView(view(offset));
        std::vector<uint8_t> rgba;
        {
            Frame frame(d);
            if(spawn)burst(p,64,1,{0,0,.5f});
            require(p.step(0),"draw step");
            gfx::PassDesc pass{};pass.colorTarget=color;pass.depthTarget=depth;
            pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;
            pass.clearColor[3]=1;
            d.beginPass(pass);d.setViewport({0,0,side,side});p.draw();d.endPass();
            require(p.stats().drawCalls==1,"indirect draw rejected");
            require(d.captureTexture(color,[&](int w,int h,std::vector<uint8_t> pixels){if(w==side&&h==side)rgba=std::move(pixels);}),"capture request");
        }
        require(d.waitIdle(),"offscreen draw completion");
        require(rgba.size()==size_t(side*side*4),"offscreen pixel readback");
        uint8_t peak=0;for(size_t i=0;i<rgba.size();i+=4)peak=std::max(peak,rgba[i]);
        return peak;
    };
    require(draw(0,true)>20,"visible particles produced no pixels");
    require(draw(10,false)<5,"zero-visible indirect draw produced pixels");
    require(draw(0,false)>20,"camera re-entry produced no pixels");
    d.destroyTexture(color);d.destroyTexture(depth);
    std::printf("PASS real indirect billboard pixels procedural=%d\n",int(procedural));
}
void liveCompaction(gfx::Device& d,bool procedural){
    auto cfg=config(1024,procedural);cfg.lifetime=60.f;
    ComputeParticles p;require(p.init(&d,cfg),"live compact init");
    p.setView(view());
    {Frame frame(d);burst(p,768,1,{0,0,.5f});burst(p,64,2,{0,0,.5f});
     require(p.step(0),"live compact growth");}
    require(audit(d,p,832).capacity==1024,"live compact source capacity");
    {Frame frame(d);require(p.killSourceParticles(1)&&p.step(0),"selective kill");}
    audit(d,p,64);
    for(uint32_t i=0;i<kGpuIdleReclaimSteps+8&&p.allocatedCapacity()>256;++i){
        tick(d,p);require(d.waitIdle(),"live compact proof completion");
    }
    auto a=audit(d,p,64);
    require(a.capacity==256&&p.stats().gpuLiveCompactions==1,"live compaction capacity");
    require(std::all_of(a.liveOwners.begin(),a.liveOwners.end(),[](uint64_t x){return x==2;}),"live compaction owner continuity");
    {Frame frame(d);burst(p,64,3,{0,0,.5f});require(p.step(0),"post compact reuse");}
    auto b=audit(d,p,128);
    require(std::count(b.liveOwners.begin(),b.liveOwners.end(),2ull)==64&&
            std::count(b.liveOwners.begin(),b.liveOwners.end(),3ull)==64,"live compact partition reuse");
    std::printf("PASS live compaction procedural=%d\n",int(procedural));
}
void checkpoint(gfx::Device& d,bool procedural){
    auto cfg=config(1024,procedural);cfg.lifetime=60.f;
    ComputeParticles p;require(p.init(&d,cfg),"owned checkpoint init");
    p.setView(view());
    {Frame frame(d);burst(p,320,0,{0,0,.5f});require(p.step(.1f),"checkpoint births");}
    auto before=audit(d,p,320);
    GpuCheckpoint snapshot;
    require(p.captureGpuCheckpoint(snapshot)&&snapshot.gpuOwnedAllocation&&validOwnedGpuCheckpoint(snapshot),"owned blocking capture");
    require(snapshot.aliveSlotOrder.size()==320&&snapshot.freeSlotOrder.size()==192,"owned capture partition");
    std::vector<uint8_t> file;
    require(p.exportRestoreCheckpoint("owned-fixture",file),"owned v2 export");
    require(p.retainFreshGpuCheckpoint(),"owned retain");
    auto ids=p.historicalGpuCheckpointIds();
    {Frame frame(d);require(p.tryClear(),"clear before restore");require(p.importRestoreCheckpoint("owned-fixture",file),"owned v2 import");require(p.step(0),"owned restore redraw");}
    auto after=audit(d,p,320);
    require(after.capacity==before.capacity&&after.liveOwners==before.liveOwners,"owned restore physical partition");
    {Frame frame(d);require(p.tryClear(),"clear before history");require(p.restoreHistoricalGpuCheckpoint(ids.back())&&p.step(0),"owned history restore");}
    audit(d,p,320);
    bool done=false,ok=false;
    {Frame frame(d);require(p.requestGpuCheckpoint([&](bool result){done=true;ok=result;}),"owned async capture request");}
    require(d.waitIdle(),"owned async completion");
    require(done&&ok&&p.checkpointStatus().restoreEligible,"owned async fresh");
    std::printf("PASS owned checkpoint procedural=%d\n",int(procedural));
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

int main(){
    std::vector<uint32_t> spv;std::string error;
    using namespace aiverse::spectra;
    std::string owned=shaders::kGLSL;
    owned.insert(owned.find('\n')+1,"#define SPECTRA_GPU_OWNED_ALLOCATION 1\n");
    for(const char* shader:{shaders::kGLSL,owned.c_str(),shaders::kCompactAllocationGLSL}){
        if(!gfx::vk::compileComputeProgram(shader,spv,&error)){
            std::fprintf(stderr,"FAIL Vulkan shader compilation: %s\n",error.c_str());return 1;
        }
    }
    gfx::vk::SpirvProgram raster;
    if(!gfx::vk::compileProgram(shaders::kProceduralBillboardGLSLVertex,shaders::kProceduralBillboardGLSLFragment,false,raster)){
        std::fprintf(stderr,"FAIL Vulkan procedural shader: %s\n",raster.error.c_str());return 1;
    }
    auto d=gfx::createVulkanDevice(gfx::VulkanWindow{},&error);
    if(!d||!supportsSpectraCompute(*d,true)){
        std::printf("SKIP Vulkan allocator hardware/descriptor capability: %s\n",error.c_str());return 77;
    }
    try{
        std::printf("Vulkan allocator device: %s\n",d->info().name.c_str());
        for(bool procedural:{false,true}){
            for(uint32_t cap:{256u,65536u,262144u})stress(*d,cap,procedural);
            lod(*d,procedural);drawAcceptance(*d,procedural);liveCompaction(*d,procedural);checkpoint(*d,procedural);
        }
        memory(*d);
        std::puts("PASS Vulkan allocator acceptance");return 0;
    }catch(const std::exception& e){
        std::fprintf(stderr,"FAIL Vulkan allocator: %s; RHI: %s\n",e.what(),d->lastError().c_str());return 1;
    }
}
