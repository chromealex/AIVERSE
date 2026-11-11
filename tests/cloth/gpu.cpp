#include "FakeDevice.h"
#include "engine/cloth/ClothGpu.h"
#include "engine/cloth/TemporalLod.h"
#include <iostream>
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
// Delayed status-only readbacks exercise ownership and revision checks, not GPU maths.
struct SleepDevice:FakeDevice {
    gfx::BufferHandle input;
    struct Pending {BufferReadFn fn;std::vector<uint8_t> data;};std::vector<Pending> pending;
    void setComputeBuffer(int slot,gfx::BufferHandle h)override{if(slot==7)input=h;}
    bool readBufferAsync(gfx::BufferHandle,size_t size,BufferReadFn fn)override{
        std::vector<uint8_t> data(size);auto& bytes=buffers.at(input.id);
        for(size_t i=0;i<size/32;++i){uint32_t yes=1;std::memcpy(data.data()+i*32+8,&yes,4);std::memcpy(data.data()+i*32+12,bytes.data()+i*96+80,8);}
        pending.push_back({std::move(fn),std::move(data)});return true;
    }
    void deliver(){auto ready=std::move(pending);pending.clear();for(auto& p:ready)p.fn(std::move(p.data));}
};
int main(){try{
    for(int cells:{2,8,71,128}){cloth::Settings s;s.cells=cells;cloth::Grid grid(s);cloth::ColoredGrid colored(grid);
        check(colored.edges.size()==grid.edges.size(),"coloring lost constraints");
        for(size_t color=0;color+1<colored.offsets.size();++color){std::vector<bool> seen(grid.rest.size());
            for(size_t e=colored.offsets[color];e<colored.offsets[color+1];++e){auto edge=colored.edges[e];check(!seen[edge.a]&&!seen[edge.b],"concurrent GPU writes share a particle");seen[edge.a]=seen[edge.b]=true;}
        }
    }
    FakeDevice d;cloth::Settings s;s.cells=8;auto grid=std::make_shared<cloth::Grid>(s);std::vector<cloth::Cloth> objects;
    for(int i=0;i<3;++i)objects.emplace_back(grid);
    cloth::Batch batch(2,true);cloth::GpuSolver gpu;cloth::TemporalLod lod;cloth::Environment env;
    check(!gpu.init(d,batch,objects)&&d.buffers.empty()&&d.textures.empty(),"unsupported backend mutated state");
    d.computeEnabled=true;check(gpu.init(d,batch,objects),"GPU mock init");
    cloth::GpuSolver second;check(!second.init(d,batch,objects),"second solver stole the batch executor");
    auto step=[&]{d.beginFrame(100,100);lod.step(objects,batch,{},env);d.endFrame();};
    step();check(batch.stats().simulatedObjects==1&&batch.stats().copiedObjects==2,"GPU scheduling lost sharing");
    check(!objects[0].cpuStateAvailable()&&objects[0].positions().empty(),"GPU ownership exposed stale CPU positions");
    objects[0].step(1.f/60,env);check(!objects[0].cpuStateAvailable(),"direct CPU step used stale GPU state");
    d.beginFrame(100,100);check(!gpu.download(),"readback accepted an open frame");batch.step(objects,1.f/60,env);d.endFrame();
    check(gpu.healthy()&&batch.stats().simulatedObjects==1,"plain Batch step lost GPU execution");
    objects[0].windScale=2;step();check(batch.stats().simulatedObjects==2,"GPU different force did not split");
    batch.setSharing(false);step();check(batch.stats().simulatedObjects==3,"independent GPU mode skipped objects");
    batch.setSharing(true);step();check(batch.stats().simulatedObjects==2,"GPU lineage failed to regroup exact trajectories");
    objects[0].reset();check(objects[0].cpuStateAvailable(),"reset did not invalidate GPU state");step();
    check(gpu.healthy()&&d.dispatchCount>0,"no compute commands");
    check(gpu.download()&&objects[0].cpuStateAvailable()&&objects[0].healthy(),"readback did not restore CPU ownership");
    gpu.shutdown();check(d.buffers.empty()&&d.textures.empty(),"GPU resources leaked");batch.step(objects,1.f/60,env);check(objects[0].healthy(),"CPU solver not restored");
    SleepDevice async;async.computeEnabled=true;std::vector<cloth::Cloth> sleepers;for(int i=0;i<3;++i)sleepers.emplace_back(grid);
    cloth::Batch sleepBatch(1,true);cloth::GpuSolver sleepGpu;check(sleepGpu.init(async,sleepBatch,sleepers),"async init");
    auto sleepStep=[&]{async.beginFrame(100,100);sleepBatch.step(sleepers,1.f/60,env);async.endFrame();};
    sleepStep();sleepers[0].wake();async.deliver();sleepStep();
    check(!sleepers[0].sleeping()&&sleepers[1].sleeping()&&sleepers[2].sleeping(),"stale async result undid explicit wake");
    check(sleepBatch.stats().simulatedObjects==1,"sleep status did not prune GPU jobs");
    async.deliver();sleepStep();sleepStep();
    check(sleepBatch.stats().sleepingObjects==3&&sleepBatch.stats().simulatedObjects==0&&sleepGpu.dispatches()==0&&sleepGpu.uploadBytes()==0,"sleep still submits GPU work");
    sleepers[1].reset();sleepStep();check(!sleepers[1].sleeping()&&sleepGpu.dispatches()>0,"reset skipped GPU upload");
    sleepGpu.shutdown();async.deliver();check(async.buffers.empty(),"late status callback retained GPU resources");
    std::cout<<"cloth GPU: race-free coloring, fallback, ownership, split/regroup, reset and readback plumbing passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
