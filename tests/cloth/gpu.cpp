#include "FakeDevice.h"
#include "engine/cloth/ClothGpu.h"
#include "engine/cloth/TemporalLod.h"
#include <iostream>
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
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
    std::cout<<"cloth GPU: race-free coloring, fallback, ownership, split/regroup, reset and readback plumbing passed\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
