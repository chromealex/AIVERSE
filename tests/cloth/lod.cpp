#include "engine/cloth/TemporalLod.h"
#include <cmath>
#include "engine/render/AnimationLod.h"
#include <iostream>
#include <stdexcept>
using namespace aiverse::cloth;
void check(bool b,const char* message){if(!b)throw std::runtime_error(message);}
void equal(const Cloth& a,const Cloth& b){for(size_t i=0;i<a.positions().size();++i){auto p=a.positions()[i],q=b.positions()[i];check(p.x==q.x&&p.y==q.y&&p.z==q.z,"scheduled solve differs from manual dt");}}
int main(){try{
    Settings s;s.cells=8;auto grid=std::make_shared<Grid>(s);std::vector<Cloth> cloths,reference;
    for(float d:{10.f,30.f,70.f,140.f}){Vec origin{d-s.width/2,s.height/2,0};cloths.emplace_back(grid,origin);reference.emplace_back(grid,origin);}
    LodSettings unphased;unphased.stagger=false;TemporalLod lod(unphased);Batch batch(4,false);Environment env;unsigned calls[4]{};double elapsed[4]{};
    for(int tick=0;tick<60;++tick){lod.step(cloths,batch,{},env);check(lod.counts()==std::array<size_t,4>{1,1,1,1},"distance tiers");
        for(size_t i=0;i<4;++i){float dt=lod.timeSteps()[i];if(dt>0){++calls[i];elapsed[i]+=dt;reference[i].step(dt,env);}equal(cloths[i],reference[i]);}
    }
    unsigned expected[]={60,30,15,10};for(int i=0;i<4;++i){check(calls[i]==expected[i],"wrong frequency");check(std::abs(elapsed[i]-1)<1e-6,"lost elapsed time");check(cloths[i].healthy(),"unstable LOD");}
    batch.setSharing(true);
    std::vector<Cloth> mixed; mixed.emplace_back(grid,Vec{0,0,0});mixed.emplace_back(grid,Vec{140,0,0});lod.reset();
    for(int i=0;i<12;++i){lod.step(mixed,batch,{},env);check(batch.stats().simulatedObjects==1&&batch.stats().copiedObjects==1&&lod.counts()[0]==2,"near clone split common simulation");}
    std::vector<Cloth> identical;for(int i=0;i<6;++i)identical.emplace_back(grid,Vec{140,0,0});lod.reset();
    for(int tick=0;tick<12;++tick){lod.step(identical,batch,{},env);auto st=batch.stats();
        if((tick+1)%6==0)check(st.simulatedObjects==1&&st.copiedObjects==5,"LOD sharing failed");
        else check(st.simulatedObjects==0&&st.deferredObjects==6,"deferred cloth was solved");
        for(auto source:batch.representatives())check(source==0,"deferred rendering lost shared shape");
    }
    lod.reset();for(auto& c:identical)c.reset();
    for(int i=0;i<3;++i)lod.step(identical,batch,{},env);
    lod.step(identical,batch,{140,0,0},env);check(std::abs(lod.timeSteps()[0]-4.f/60)<1e-7,"near transition dropped accumulated time");
    lod.reset();lod.step(identical,batch,{},env);identical[0].windScale=2;lod.requestUpdate(0);lod.step(identical,batch,{},env);
    check(lod.timeSteps()[0]>0&&lod.timeSteps()[1]==0&&batch.stats().simulatedObjects==1,"forced update did not split schedule");
    lod.setEnabled(false);lod.step(identical,batch,{},env);check(std::abs(lod.timeSteps()[1]-3.f/60)<1e-7,"disable lost pending time");
    lod.reset();lod.setEnabled(true);std::vector<Cloth> edge;edge.emplace_back(grid,Vec{19-s.width/2,s.height/2,0});
    for(auto [camera,level]:{std::pair{0.f,0},std::pair{-1.5f,0},std::pair{-3.5f,1},std::pair{-.5f,1},std::pair{1.5f,0}}){lod.step(edge,batch,{camera,0,0},env);check(lod.counts()[size_t(level)]==1,"LOD hysteresis");}
    lod.reset();lod.step(identical,batch,{},env);identical[0].active=false;lod.step(identical,batch,{},env);identical[0].active=true;
    lod.step(identical,batch,{140,0,0},env);check(std::abs(lod.timeSteps()[0]-1.f/60)<1e-7,"inactive time caught up");
    // The host auto-LOD is authoritative: a large orthographic object stays high frequency.
    std::vector<LodHint> hints(identical.size(),{0,true});lod.reset();
    lod.step(identical,batch,{0,0,-1000},env,{},hints);check(lod.counts()[0]==identical.size(),"auto LOD did not override distance");
    lod.setMinimumLevel(2);lod.reset();lod.step(identical,batch,{},env,{},hints);
    check(lod.counts()[2]==identical.size()&&lod.timeSteps()[0]==0,"adaptive quality floor ignored");
    lod.setMinimumLevel(0);for(auto& hint:hints)hint.visible=false;
    for(int t=0;t<30;++t)lod.step(identical,batch,{},env,{},hints);
    check(batch.stats().simulatedObjects==0,"hidden auto LOD simulated");
    for(auto& hint:hints)hint.visible=true;
    lod.step(identical,batch,{},env,{},hints);check(std::abs(lod.timeSteps()[0]-1.f/60)<1e-7,"hidden time caught up");
    hints[0].visible=false;lod.setEnabled(false);lod.step(identical,batch,{},env,{},hints);
    check(lod.timeSteps()[0]>0,"manual override did not disable auto LOD");lod.setEnabled(true);
    using namespace aiverse::render;
    auto projection=mat4::ortho(-10,10,-10,10,.1f,2000,false);
    auto near=projectedAnimationLod(projection,{0,0,-10},{0,1,-10});
    auto far=projectedAnimationLod(projection,{0,0,-1000},{0,1,-1000});
    check(near.visible&&far.visible&&near.interval==0&&far.interval==0,"orthographic LOD depends on depth");
    // A copied-state proof must be invalidated by a direct solve and by reset.
    std::vector<Cloth> pair;pair.emplace_back(grid);pair.emplace_back(grid);batch.step(pair,1.f/60,env);
    pair[0].step(1.f/60,env);batch.step(pair,1.f/60,env);check(batch.stats().simulatedObjects==2,"stale proof after direct solve");
    pair[0].reset();batch.step(pair,1.f/60,env);check(batch.stats().simulatedObjects==2,"stale proof after reset");
    // Dense far cloth needs bounded integration steps even when updates are infrequent.
    Settings dense;dense.cells=71;Cloth longStep(std::make_shared<Grid>(dense));
    for(int t=0;t<60;++t)longStep.step(.1f,env);
    check(longStep.healthy()&&longStep.maxStretch()<2,"dense low-frequency solver diverged");
    TemporalLod staggered;std::vector<Cloth> spread;
    for(int i=0;i<12;++i){spread.emplace_back(grid,Vec{140,0,0});spread.back().windScale=float(i);}
    unsigned spreadCalls=0;
    for(int t=0;t<60;++t){staggered.step(spread,batch,{},env);check(batch.stats().simulatedObjects<=4,"staggered groups bunched together");spreadCalls+=unsigned(batch.stats().simulatedObjects);}
    check(spreadCalls==120,"staggered frequency changed");
    bool rejected=false;try{LodSettings invalid;invalid.periods[3]=7;TemporalLod bad(invalid);}catch(const std::invalid_argument&){rejected=true;}check(rejected,"unsafe dt period accepted");
    std::cout<<"cloth LOD: frequencies, elapsed time, transitions, hysteresis, sharing, wake and freeze passed\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
