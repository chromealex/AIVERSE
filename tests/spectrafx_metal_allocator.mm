// Stage 115: production ComputeParticles + Metal RHI allocator acceptance.
// No software GPU mock. No window or screenshot prerequisite.
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#include "engine/fx/SpectraGpuParticles.h"
#include "engine/fx/SpectraGpuVolumeRenderer.h"
#include <cstring>
#include <algorithm>
#include <chrono>
#include <cmath>
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
    @autoreleasepool {
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
        {
            Frame frame(d);
            if(spawn)burst(p,64,1,{0,0,.5f});
            require(p.step(0),"draw step");
            gfx::PassDesc pass{};pass.colorTarget=color;pass.depthTarget=depth;
            pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;
            pass.clearColor[3]=1;
            d.beginPass(pass);d.setViewport({0,0,side,side});p.draw();d.endPass();
            require(p.stats().drawCalls==1,"indirect draw rejected");
        }
        require(d.waitIdle(),"offscreen draw completion");
        std::vector<uint8_t> rgba;
        require(d.readColorRGBA8(color,rgba)&&rgba.size()==size_t(side*side*4),"offscreen pixel readback");
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
void sharedSdf(gfx::Device& d){
    auto budget=d.acquireGpuAllocationBudget();
    require(bool(budget),"SDF canonical budget");
    require(d.waitIdle()&&d.waitForSubmittedGpuWork(),"SDF initial idle");
    budget->collect(d.gpuCompletedFence());
    const auto before=budget->stats().charged();
    auto cache=std::make_shared<SharedSdfGpuCache>(d);
    auto cfg=config(256,false);
    cfg.sharedSdfCache=cache;cfg.allocationBudget=budget;
    cfg.staticSdf.dimension=128;cfg.staticSdf.spacing=.0625f;
    cfg.staticSdf.origin={-4,-4,-4};cfg.staticSdf.samples.assign(128u*128u*128u,1.f);
    const size_t bytes=(9+128u*128u*128u)*sizeof(uint32_t);
    const auto privateCharge=estimateGpuEmitterAllocation(256,false);
    ComputeParticles a,b,c;
    require(a.init(&d,cfg)&&b.init(&d,cfg),"two SDF emitters initialize");
    require(cache->liveEntries()==1&&cache->liveBytes()==bytes&&
            a.stats().sharedSdfBytes==bytes&&b.stats().sharedSdfBytes==bytes,
            "identical SDF shares one GPU buffer");
    require(budget->stats().charged()==before+2*privateCharge+bytes,"one SDF ledger charge");
    auto different=cfg;different.staticSdf.samples[0]=2.f;
    require(c.init(&d,different)&&cache->liveEntries()==2&&cache->liveBytes()==2*bytes,
            "different SDF is not aliased");
    c.shutdown();require(cache->liveEntries()==1,"unused distinct payload released");
    a.setView(view());b.setView(view());
    {Frame frame(d);burst(a,64,1,{0,0,.5f});burst(b,64,2,{0,0,.5f});
     require(a.step(.01f)&&b.step(.01f),"shared SDF compute dispatches");}
    a.shutdown();
    require(cache->liveEntries()==1&&cache->liveBytes()==bytes,"remaining emitter retains shared payload");
    tick(d,b,.01f);
    audit(d,b,64);
    b.shutdown();
    require(cache->liveEntries()==0&&cache->liveBytes()==0,"last lease releases payload");
    require(d.waitIdle()&&d.waitForSubmittedGpuWork(),"SDF final idle");
    budget->collect(d.gpuCompletedFence());
    require(budget->stats().charged()==before,"SDF budget restored after completion");
    std::puts("PASS shared immutable SDF GPU payload");
}
void packedSdfAtlas(gfx::Device& d){
    auto budget=d.acquireGpuAllocationBudget();
    require(bool(budget),"atlas canonical budget");
    require(d.waitIdle()&&d.waitForSubmittedGpuWork(),"atlas initial idle");
    budget->collect(d.gpuCompletedFence());const auto baseline=budget->stats().charged();
    auto cache=std::make_shared<SharedSdfGpuCache>(d,true);
    auto plane=[&](uint32_t n,float height){
        auto cfg=config(256,false);cfg.allocationBudget=budget;cfg.sharedSdfCache=cache;
        cfg.staticSdf.dimension=n;cfg.staticSdf.origin={-2,-2,-2};
        cfg.staticSdf.spacing=4.f/float(n-1);
        for(uint32_t z=0;z<n;++z)for(uint32_t y=0;y<n;++y)for(uint32_t x=0;x<n;++x)
            cfg.staticSdf.samples.push_back(-2.f+float(y)*cfg.staticSdf.spacing-height);
        return cfg;
    };
    for(bool owned:{false,true}){
        auto low=plane(8,-.5f),high=plane(16,.5f);
        low.gpuOwnedAllocation=high.gpuOwnedAllocation=owned;
        low.gpuIndirect=high.gpuIndirect=owned;
        ComputeParticles a,b,duplicate;
        require(a.init(&d,low)&&b.init(&d,high)&&duplicate.init(&d,low),"packed heterogeneous emitters");
        require(cache->atlasPages()==1&&cache->liveEntries()==2&&a.stats().sharedSdfPacked&&b.stats().sharedSdfPacked&&
                a.stats().sharedSdfOffsetWords!=b.stats().sharedSdfOffsetWords&&
                duplicate.stats().sharedSdfOffsetWords==a.stats().sharedSdfOffsetWords,
                "heterogeneous offsets and identical payload dedup");
        require(cache->atlasBytes()==1024u*1024u&&cache->liveBytes()==cache->atlasBytes()&&
                budget->stats().charged()==baseline+3*estimateGpuEmitterAllocation(256,false)+cache->atlasBytes(),
                "one page reservation plus private envelopes");
        duplicate.shutdown();a.setView(view());b.setView(view());
        {Frame frame(d);burst(a,64,1,{0,.25f,.5f});burst(b,64,2,{0,.25f,.5f});
         require(a.step(.01f)&&b.step(.01f),"first-frame checked atlas uploads");}
        {Frame frame(d);require(a.step(.01f)&&b.step(.01f),"atlas collision update");}
        require(d.waitIdle()&&d.waitForSubmittedGpuWork(),"atlas collision completion");
        auto checkHeight=[&](ComputeParticles& p,float expected){
            GpuCheckpoint snapshot;require(p.captureGpuCheckpoint(snapshot),"atlas particle readback");
            uint32_t live=0;
            for(const auto& slot:snapshot.slots)if(slot.positionAge.w>=0){
                ++live;require(std::abs(slot.positionAge.y-expected)<.012f,"atlas distinct SDF surface");
            }
            require(live==64,"atlas particle count");
        };
        checkHeight(a,.25f);checkHeight(b,.501f);
        a.shutdown();require(cache->atlasPages()==1,"second emitter retains page");
        tick(d,b,.01f);b.shutdown();
        require(d.waitIdle()&&d.waitForSubmittedGpuWork(),"atlas emitter completion");
        budget->collect(d.gpuCompletedFence());
        require(cache->atlasPages()==0&&budget->stats().charged()==baseline,"atlas page retired");
    }
    // Exercise range quarantine independently of the particle allocator.
    std::vector<uint32_t> words(9+27,0);words[4]=3;words[8]=0x3f800000u;
    auto first=cache->acquire(d,budget,words);words[9]=0x3f800000u;
    auto anchor=cache->acquire(d,budget,words);
    require(first&&anchor&&first->buffer()==anchor->buffer(),"same physical page");
    const uint32_t oldOffset=first->offsetWords();
    std::shared_ptr<SharedSdfGpuPayload> newer;
    {
        Frame frame(d);require(first->ensureUploaded(),"atlas range upload");
        first.reset();words[9]=0x40000000u;newer=cache->acquire(d,budget,words);
        require(newer&&newer->buffer()==anchor->buffer()&&newer->offsetWords()!=oldOffset,
                "in-flight range cannot be recycled");
    }
    require(d.waitIdle()&&d.waitForSubmittedGpuWork(),"atlas quarantine completion");
    words[9]=0x40400000u;auto reused=cache->acquire(d,budget,words);
    require(reused&&reused->offsetWords()==oldOffset&&reused->buffer()==anchor->buffer(),
            "completed range returns to first-fit allocator");
    anchor.reset();newer.reset();reused.reset();budget->collect(d.gpuCompletedFence());
    require(cache->liveBytes()==0&&budget->stats().charged()==baseline,"all atlas reservations credited");
    std::puts("PASS packed SDF atlas offsets, contacts and fence quarantine");
}
void denseVolume(gfx::Device& d){
    namespace vol=aiverse::spectra::volume;
    require(vol::supportsGpuVolume(d),"dense GPU volume capability");
    auto budget=d.acquireGpuAllocationBudget();
    require(bool(budget),"volume canonical budget");
    require(d.waitIdle()&&d.waitForSubmittedGpuWork(),"volume initial idle");
    budget->collect(d.gpuCompletedFence());const size_t baseline=budget->stats().charged();
    vol::Config cfg;cfg.dimension=8;cfg.cellSize=.25f;cfg.origin={-1,-1,-1};
    cfg.pressureIterations=6;cfg.gravity={-9.81f,0,0};cfg.wind={0,.3f,0};
    cfg.ignition=.05f;cfg.burnRate=5;
    vol::Source source;source.position={.35f,-.2f,0};source.radius=.7f;
    source.smoke=8;source.temperature=10;source.fuel=5;source.acceleration={.1f,1.f,0};
    fx::StaticSdfVolume sdf;sdf.dimension=5;sdf.origin={-2,-2,-2};sdf.spacing=1;
    for(unsigned z=0;z<5;++z)for(unsigned y=0;y<5;++y)for(unsigned x=0;x<5;++x)
        sdf.samples.push_back(float(x)-2);
    vol::GasGrid cpu;vol::GpuGasGrid gpu;
    require(cpu.init(cfg)&&cpu.setObstacle(sdf)&&cpu.setSources({source}),"CPU volume reference");
    require(gpu.init(d,cfg,sdf,budget)&&gpu.setSources({source}),"GPU volume setup");
    require(!gpu.published()&&gpu.stats().allocatedBytes==vol::gpuVolumeBytes(8)&&
        budget->stats().charged()==baseline+vol::gpuVolumeBytes(8),"volume allocation and unpublished initial state");
    require(!gpu.step()&&gpu.stats().ticks==0,"volume requires active frame");
    const auto compare=[&](){
        std::vector<vol::GpuCell> result;
        require(d.waitIdle()&&d.waitForSubmittedGpuWork()&&gpu.snapshot(result),"GPU volume diagnostic readback");
        require(result.size()==cpu.cells().size(),"volume cell count");
        float maxError=0;double density=0,temperature=0,fuel=0;
        for(size_t i=0;i<result.size();++i){
            const auto& a=cpu.cells()[i];const auto& b=result[i];
            const float e[]={std::abs(a.density-b.fields.x),std::abs(a.temperature-b.fields.y),
                std::abs(a.fuel-b.fields.z),std::abs(a.velocity.x-b.velocity.x),
                std::abs(a.velocity.y-b.velocity.y),std::abs(a.velocity.z-b.velocity.z)};
            for(float x:e){require(std::isfinite(x),"finite volume data");maxError=std::max(maxError,x);}
            if(cpu.solidMask()[i])require(b.fields.x==0&&b.fields.y==0&&b.fields.z==0&&
                b.velocity.x==0&&b.velocity.y==0&&b.velocity.z==0,"solid GPU cells stay empty");
            density+=b.fields.x;temperature+=b.fields.y;fuel+=b.fields.z;
        }
        require(maxError<.004f&&density>0&&temperature>0&&fuel>0,"CPU/GPU dense volume parity");
    };
    for(unsigned tick=0;tick<5;++tick){
        require(cpu.step(),"CPU volume tick");
        Frame frame(d);require(gpu.step(),"GPU volume tick");
        auto view=gpu.view();require(view.valid()&&view.dimension==8&&view.tick==tick+1,"published volume view");
    }
    compare();
    auto impulse=source;impulse.smoke=3;impulse.fuel=2;
    require(gpu.queueImpulse(impulse,vol::kTick)&&!gpu.queueImpulse(impulse,vol::kTick),"bounded impulse queue");
    require(!gpu.step()&&gpu.stats().ticks==5,"rejected tick preserves published state and impulse");
    require(cpu.setSources({source,impulse})&&cpu.step(),"reference queued impulse");
    {Frame frame(d);require(gpu.step(),"GPU impulse tick");}
    compare();
    require(cpu.setSources({source})&&cpu.setWind({0,0,1})&&gpu.setWind({0,0,1}),"volume live wind");
    require(cpu.step(),"reference wind tick");
    {Frame frame(d);require(gpu.step(),"GPU wind tick");}
    compare();
    auto invalid=cfg;invalid.dimension=65;
    require(!gpu.init(d,invalid,sdf,budget)&&gpu.stats().ticks==7,"invalid reinit preserves GPU state");
    const auto oldLimit=budget->stats().limit;
    budget->setLimit(budget->stats().charged()+1);
    require(!gpu.init(d,cfg,sdf,budget)&&gpu.stats().ticks==7,"budget denial preserves GPU state");
    budget->setLimit(oldLimit);
    {Frame frame(d);require(gpu.clear()&&gpu.view().tick==0,"GPU volume clear");}
    require(d.waitIdle()&&d.waitForSubmittedGpuWork(),"clear completion");
    std::vector<vol::GpuCell> zero;
    require(gpu.snapshot(zero),"clear readback");
    for(const auto& c:zero)require(c.fields.x==0&&c.fields.y==0&&c.fields.z==0,"cleared GPU density");
    gpu.shutdown();budget->collect(d.gpuCompletedFence());
    require(budget->stats().charged()==baseline,"volume reservation returned");
    std::puts("PASS dense GPU volume parity, SDF mask, impulse, lifecycle and budget");
}
void volumeHdr(gfx::Device& d){
    namespace vol=aiverse::spectra::volume;
    if(!vol::supportsVolumeRender(d)){std::puts("SKIP volume HDR storage capability");return;}
    auto budget=d.acquireGpuAllocationBudget();
    require(bool(budget),"volume HDR canonical budget");
    require(d.waitIdle()&&d.waitForSubmittedGpuWork(),"volume HDR initial idle");
    budget->collect(d.gpuCompletedFence());const size_t baseline=budget->stats().charged();
    vol::Config cfg;cfg.dimension=8;cfg.origin={-.5f,-.5f,.15f};cfg.cellSize=.125f;
    cfg.pressureIterations=0;cfg.gravity={0,0,0};cfg.burnRate=0;
    vol::GpuGasGrid grid;vol::GpuVolumeRenderer renderer;
    require(grid.init(d,cfg,{},budget),"HDR volume grid");
    gfx::TextureDesc desc;desc.width=desc.height=side;desc.format=gfx::PixelFormat::RGBA16F;
    desc.renderTarget=true;desc.filter=gfx::Filter::Nearest;
    auto color=d.createTexture(desc);desc.format=gfx::PixelFormat::Depth;
    auto depth=d.createTexture(desc);
    require(color.valid()&&depth.valid(),"HDR volume input targets");
    render::PostFrame f{};f.width=f.height=side;f.viewProj=render::mat4::identity();
    f.lighting.sunIntensity=0;
    vol::Material material;material.fireColor={8,2,1};material.emission=2;
    vol::RenderSettings settings;settings.steps=32;settings.shadowSteps=0;
    vol::Source impulse;impulse.position={0,0,.6f};impulse.radius=.4f;impulse.smoke=0;
    impulse.temperature=12;impulse.fuel=0;impulse.acceleration={0,0,0};
    require(grid.queueImpulse(impulse,.5f),"HDR volume impulse");
    struct Read {gfx::TextureReadbackResult data;bool done=false;};
    auto renderRead=[&](float depthClear,bool clear,bool tick){
        auto read=std::make_shared<Read>();
        {
            Frame frame(d);
            if(tick)require(grid.step(),"HDR volume simulation");
            if(clear)require(grid.clear(),"HDR volume empty clear");
            gfx::PassDesc pass;pass.colorTarget=color;pass.depthTarget=depth;
            pass.clearColor[0]=pass.clearColor[1]=pass.clearColor[2]=0;pass.clearColor[3]=1;
            pass.clearDepth=depthClear;d.beginPass(pass);d.endPass();
            auto output=renderer.draw(d,grid,f,color,depth,material,settings);
            require(output.valid()&&output!=color&&renderer.allocatedBytes()==size_t(side)*side*8,
                    "HDR volume output and budget");
            require(d.readTextureAsync(output,{},[read](gfx::TextureReadbackResult value){
                read->data=std::move(value);read->done=true;
            }),"volume HDR readback accepted");
        }
        require(d.waitIdle()&&d.waitForSubmittedGpuWork(),"volume HDR completion");
        for(int i=0;i<4&&!read->done;++i){Frame frame(d);}
        require(read->done&&read->data.success()&&read->data.format==gfx::PixelFormat::RGBA16F&&
            read->data.pixels.size()==size_t(side)*side*8,"volume HDR readback");
        // Positive finite IEEE half values have monotonic unsigned bit order.
        const size_t at=(size_t(side/2)*side+side/2)*8;
        uint16_t red=0;std::memcpy(&red,read->data.pixels.data()+at,2);
        return red;
    };
    const auto visible=renderRead(1,false,true);
    const auto occluded=renderRead(0,false,false);
    const auto empty=renderRead(1,true,false);
    require(visible>0x3c00u&&visible<0x7c00u,"HDR volume emission above 1");
    require(occluded==0&&empty==0,"opaque depth and empty volume preserve black background");
    require(d.waitIdle()&&d.waitForSubmittedGpuWork(),"volume HDR final idle");
    renderer.shutdown();grid.shutdown();d.destroyTexture(color);d.destroyTexture(depth);
    budget->collect(d.gpuCompletedFence());
    require(budget->stats().charged()==baseline,"HDR volume allocation released");
    std::puts("PASS dense volume HDR emission, depth clipping and empty field");
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
        setenv("AIVERSE_METAL_LATE_DRAWABLE","1",1);
        CAMetalLayer* layer=[CAMetalLayer layer];layer.device=metal;
        layer.frame=CGRectMake(0,0,side,side);layer.drawableSize=CGSizeMake(side,side);
        auto d=gfx::createMetalDevice((__bridge void*)layer);
        if(!d||!d->supportsCompute()){std::puts("SKIP no Metal compute");return 77;}
        try{
            for(bool procedural:{false,true}){
                for(uint32_t cap:{256u,65536u,262144u})stress(*d,cap,procedural);
                lod(*d,procedural);
                drawAcceptance(*d,procedural);
                liveCompaction(*d,procedural);
                checkpoint(*d,procedural);
            }
            sharedSdf(*d);
            packedSdfAtlas(*d);
            denseVolume(*d);
            volumeHdr(*d);
            memory(*d);
            std::puts("PASS production Metal allocator acceptance");return 0;
        }catch(const std::exception& e){
            std::fprintf(stderr,"FAIL allocator acceptance: %s; RHI: %s\n",e.what(),d->lastError().c_str());return 1;
        }
    }
}
