#include <Jolt/Jolt.h>
#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemSingleThreaded.h>
#ifndef __EMSCRIPTEN__
#include <Jolt/Core/JobSystemThreadPool.h>
#endif
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <numeric>
#include <stdexcept>
#include <string>
#include <vector>

using namespace JPH;
namespace {
class Layers final : public BroadPhaseLayerInterface {
public:
    uint GetNumBroadPhaseLayers() const override { return 2; }
    BroadPhaseLayer GetBroadPhaseLayer(ObjectLayer l) const override { return BroadPhaseLayer(uint8(l)); }
#if defined(JPH_EXTERNAL_PROFILE) || defined(JPH_PROFILE_ENABLED)
    const char* GetBroadPhaseLayerName(BroadPhaseLayer l) const override { return l.GetValue() ? "dynamic" : "static"; }
#endif
};
class Pairs final : public ObjectLayerPairFilter {
    bool ShouldCollide(ObjectLayer a,ObjectLayer b) const override { return a!=0 || b!=0; }
};
class BroadPairs final : public ObjectVsBroadPhaseLayerFilter {
    bool ShouldCollide(ObjectLayer a,BroadPhaseLayer b) const override { return a!=0 || b.GetValue()!=0; }
};
void require(bool ok,const char* message) { if(!ok)throw std::runtime_error(message); }
struct Scene {
    Layers layers; Pairs pairs; BroadPairs broadPairs;
    TempAllocatorImpl temp{64*1024*1024};
    std::unique_ptr<JobSystem> jobs;
    PhysicsSystem physics;
    std::vector<BodyID> bodies;
    explicit Scene(int workers) {
        if(workers==0)jobs=std::make_unique<JobSystemSingleThreaded>(cMaxPhysicsJobs);
#ifndef __EMSCRIPTEN__
        else jobs=std::make_unique<JobSystemThreadPool>(cMaxPhysicsJobs,cMaxPhysicsBarriers,workers);
#else
        else throw std::runtime_error("Web evaluation supports single-threaded execution only");
#endif
        physics.Init(20000,0,100000,50000,layers,broadPairs,pairs);
    }
    BodyID box(Vec3 half,RVec3 pos,bool moving,bool sleep=false,bool ccd=false,Vec3 velocity=Vec3::sZero()) {
        BodyCreationSettings settings(new BoxShape(half),pos,Quat::sIdentity(),moving?EMotionType::Dynamic:EMotionType::Static,moving?1:0);
        settings.mAllowSleeping=sleep;
        settings.mMotionQuality=ccd?EMotionQuality::LinearCast:EMotionQuality::Discrete;
        settings.mLinearVelocity=velocity;
        auto id=physics.GetBodyInterface().CreateAndAddBody(settings,moving?EActivation::Activate:EActivation::DontActivate);
        require(!id.IsInvalid(),"Body allocation failed");bodies.push_back(id);return id;
    }
    void step() { require(physics.Update(1.0f/60,1,&temp,jobs.get())==EPhysicsUpdateError::None,"Physics capacity exhausted"); }
    ~Scene() { auto& bi=physics.GetBodyInterface();for(auto id:bodies){bi.RemoveBody(id);bi.DestroyBody(id);} }
};
// Hash named components only, never SIMD padding. This is a trajectory diagnostic,
// not a complete engine/save checksum (contact caches and sleeping timers are excluded).
uint64_t trajectory(Scene& s,uint64_t hash) {
    auto add=[&](float v){require(std::isfinite(v),"Non-finite state");hash^=std::bit_cast<uint32_t>(v);hash*=1099511628211ULL;};
    auto& bi=s.physics.GetBodyInterface();
    for(auto id:s.bodies){auto p=bi.GetPosition(id);auto q=bi.GetRotation(id);auto v=bi.GetLinearVelocity(id);auto w=bi.GetAngularVelocity(id);
        add(float(p.GetX()));add(float(p.GetY()));add(float(p.GetZ()));
        add(q.GetX());add(q.GetY());add(q.GetZ());add(q.GetW());
        add(v.GetX());add(v.GetY());add(v.GetZ());add(w.GetX());add(w.GetY());add(w.GetZ());
    }return hash;
}
void verify() {
    Scene s(0);
    s.box({100,1,100},{0,-1,0},false);
    auto falling=s.box({0.5f,0.5f,0.5f},{0,5,0},true,true);
    RayCastResult hit;
    require(s.physics.GetNarrowPhaseQuery().CastRay(RRayCast({10,10,0},{0,-20,0}),hit),"Ray missed floor");
    require(std::abs(hit.mFraction-0.5f)<0.001f,"Incorrect floor hit fraction");
    for(int i=0;i<240;++i)s.step();
    require(std::abs(s.physics.GetBodyInterface().GetPosition(falling).GetY()-0.5f)<0.05f,"Body did not settle on floor");
    require(!s.physics.GetBodyInterface().IsActive(falling),"Settled body did not sleep");
    s.box({0.05f,5,5},{5,5,0},false);
    auto fast=s.box({0.1f,0.1f,0.1f},{0,2,0},true,false,true,{600,0,0});
    s.step();
    require(s.physics.GetBodyInterface().GetPosition(fast).GetX()<5,"CCD body tunneled through wall");
    std::cout<<"{\"collision_checks\":\"passed\"}\n";
}
void benchmark(const std::string& mode,int count,int workers) {
    Scene s(workers);
    s.box({300,1,300},{0,-1,0},false);
    const int side=int(std::ceil(std::sqrt(float(count))));
    for(int i=0;i<count;++i){
        RVec3 p;
        if(mode=="pile")p={float(i%16)*1.05f,float(i/256)*1.05f+1,float((i/16)%16)*1.05f};
        else p={float(i%side)*2,float(i%7)*0.25f+2,float(i/side)*2};
        s.box({0.5f,0.5f,0.5f},p,true,mode=="sleep");
    }
    s.physics.OptimizeBroadPhase();
    const int warmup=mode=="sleep"?600:60;
    for(int i=0;i<warmup;++i)s.step();
    std::vector<double> times;uint64_t hash=14695981039346656037ULL;uint64_t active=0;
    for(int i=0;i<600;++i){
        auto begin=std::chrono::steady_clock::now();s.step();auto end=std::chrono::steady_clock::now();
        times.push_back(std::chrono::duration<double,std::milli>(end-begin).count());
        active+=s.physics.GetNumActiveBodies(EBodyType::RigidBody);
        hash=trajectory(s,hash);
    }
    for(auto id:s.bodies)require(s.physics.GetBodyInterface().GetPosition(id).GetY()>-2,"Body fell through floor");
    double mean=std::accumulate(times.begin(),times.end(),0.0)/times.size();std::sort(times.begin(),times.end());
    std::cout<<"{\"scene\":\""<<mode<<"\",\"bodies\":"<<count<<",\"workers\":"<<workers
             <<",\"mean_ms\":"<<mean<<",\"p95_ms\":"<<times[569]<<",\"max_ms\":"<<times.back()
             <<",\"mean_active\":"<<double(active)/times.size()<<",\"trajectory\":\""<<std::hex<<hash<<"\"}\n";
}
}
int main(int argc,char** argv) {
    RegisterDefaultAllocator();Factory::sInstance=new Factory();RegisterTypes();int result=0;
    try {
        require(argc==4,"Usage: aiverse_physics_bench verify|spread|pile|sleep bodies workers");
        std::string mode=argv[1];int count=std::stoi(argv[2]),workers=std::stoi(argv[3]);
        require(count>=1&&count<=8192&&workers>=0&&workers<=16,"Arguments out of range");
        if(mode=="verify")verify();else {require(mode=="spread"||mode=="pile"||mode=="sleep","Unknown scene");benchmark(mode,count,workers);}
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';result=1;}
    UnregisterTypes();delete Factory::sInstance;Factory::sInstance=nullptr;return result;
}
