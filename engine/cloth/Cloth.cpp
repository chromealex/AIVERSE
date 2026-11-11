#include "Cloth.h"
#include "engine/core/Hash.h"
#include <bit>
#include <cstring>
#include <algorithm>
#include <atomic>
#include <array>
#include <cmath>
#include <stdexcept>
#ifndef __EMSCRIPTEN__
#include <condition_variable>
#include <mutex>
#include <thread>
#endif
namespace aiverse::cloth {
namespace {
void hashFloat(Hasher&,float);
void hashVec(Hasher&,Vec);
void hashEnvironment(Hasher&,const Environment&);
bool finite(Vec v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
void positive(float x,const char* name){if(!std::isfinite(x)||x<=0)throw std::invalid_argument(name);}
}
Grid::Grid(Settings s):settings(s){
    if(s.cells<2||s.cells>128||s.substeps<1||s.substeps>16||s.iterations<1||s.iterations>32)throw std::invalid_argument("cloth resolution/solver range");
    positive(s.width,"cloth width");positive(s.height,"cloth height");positive(s.mass,"cloth mass");
    if(s.width>1000||s.height>1000||s.mass>10000||s.mass<1e-4f)throw std::invalid_argument("cloth dimensions/mass range");
    for(float v:{s.stretchCompliance,s.bendCompliance,s.damping,s.thickness})if(!std::isfinite(v)||v<0)throw std::invalid_argument("cloth material range");
    int n=s.cells+1;rest.reserve(size_t(n*n));
    for(int y=0;y<n;++y)for(int x=0;x<n;++x)rest.push_back({s.width*x/s.cells,-s.height*y/s.cells,0});
    auto edge=[&](int a,int b,bool bend){auto delta=rest[size_t(a)]-rest[size_t(b)];edges.push_back({uint32_t(a),uint32_t(b),std::sqrt(dot(delta,delta)),bend?s.bendCompliance:s.stretchCompliance,bend});};
    for(int y=0;y<n;++y)for(int x=0;x<n;++x){int a=y*n+x;
        if(x+1<n)edge(a,a+1,false);
        if(y+1<n)edge(a,a+n,false);
        if(x+1<n&&y+1<n){edge(a,a+n+1,false);edge(a+1,a+n,false);}
        // Cheap bending surrogate: distance across two edges, not a dihedral bending energy.
        if(x+2<n)edge(a,a+2,true);
        if(y+2<n)edge(a,a+2*n,true);
    }
}
Cloth::Cloth(std::shared_ptr<const Grid> g,Vec origin):grid_(std::move(g)),origin_(origin){
    if(!grid_||!finite(origin))throw std::invalid_argument("cloth grid/origin");
    p_=grid_->rest;previous_=p_;velocity_.resize(p_.size());lambda_.resize(grid_->edges.size());
    weights_.assign(p_.size(),float(p_.size())/grid_->settings.mass);
    std::fill_n(weights_.begin(),grid_->settings.cells+1,0.0f);
}
void Cloth::reset(){externalState_=false;proofOwner_.reset();fingerprintValid_=false;p_=grid_->rest;previous_=p_;std::fill(velocity_.begin(),velocity_.end(),Vec{});pinOffset_={};}
void Cloth::movePins(Vec offset){if(finite(offset))pinOffset_=offset;}
void Cloth::step(float dt,const Environment& env){
    if(externalState_||env.capsules.size()>32||!active||!std::isfinite(windScale)||!std::isfinite(dt)||dt<=0||dt>0.1f||!finite(env.gravity)||!finite(env.wind)||!std::isfinite(env.floor))return;
    recordStep(dt,env);
    auto& s=grid_->settings;
    // Bound integration dt when the scheduler accumulates time. Distant updates trade
    // constraint convergence for cost, but never use the unstable single-iteration dense path.
    const int ticks=std::clamp(int(std::ceil(dt*60-1e-5f)),1,6);
    const int substeps=s.substeps*ticks,iterations=ticks>1?std::min(s.iterations,std::max(2,s.iterations/2)):s.iterations;
    float h=dt/substeps,alphaScale=1/(h*h),damp=std::exp(-s.damping*h);
    struct Collider {Vec a,ab,lo,hi;float inverseLength2,radius;};
    std::array<Collider,32> colliders{};size_t colliderCount=0;
    for(auto& c:env.capsules){
        if(!finite(c.a)||!finite(c.b)||!std::isfinite(c.radius)||c.radius<=0)continue;
        Vec ab=c.b-c.a;float l2=dot(ab,ab),r=c.radius+s.thickness;
        Vec radius{r,r,r};colliders[colliderCount++]={c.a,ab,render::min3(c.a,c.b)-radius,render::max3(c.a,c.b)+radius,l2>1e-12f?1/l2:0,r};
    }
    for(int sub=0;sub<substeps;++sub){
        std::fill(lambda_.begin(),lambda_.end(),0.0f);
        for(size_t i=0;i<p_.size();++i){previous_[i]=p_[i];
            if(weights_[i]==0){p_[i]=grid_->rest[i]+pinOffset_;continue;}
            velocity_[i]=velocity_[i]*damp+(env.gravity+env.wind*windScale)*h;p_[i]=p_[i]+velocity_[i]*h;
        }
        for(int iter=0;iter<iterations;++iter){
            for(size_t k=0;k<grid_->edges.size();++k){const auto& e=grid_->edges[k];float wa=weights_[e.a],wb=weights_[e.b];
                if(wa+wb==0)continue;
                Vec d=p_[e.a]-p_[e.b];float l2=dot(d,d);if(l2<1e-16f)continue;
                float len=std::sqrt(l2),alpha=e.compliance*alphaScale;
                float dl=(-(len-e.rest)-alpha*lambda_[k])/(wa+wb+alpha);lambda_[k]+=dl;
                Vec correction=d*(dl/len);p_[e.a]=p_[e.a]+correction*wa;p_[e.b]=p_[e.b]-correction*wb;
            }
            for(size_t i=0;i<p_.size();++i){if(weights_[i]==0)continue;auto& p=p_[i];p.y=std::max(p.y,env.floor+s.thickness);
                for(size_t ci=0;ci<colliderCount;++ci){auto& c=colliders[ci];
                    if(p.x<c.lo.x||p.y<c.lo.y||p.z<c.lo.z||p.x>c.hi.x||p.y>c.hi.y||p.z>c.hi.z)continue;
                    float t=std::clamp(dot(p-c.a,c.ab)*c.inverseLength2,0.0f,1.0f);
                    Vec center=c.a+c.ab*t,delta=p-center;float d2=dot(delta,delta),r=c.radius;
                    if(d2<r*r)p=center+(d2>1e-16f?delta*(r/std::sqrt(d2)):Vec{0,0,r});
                }
            }
        }
        for(size_t i=0;i<p_.size();++i)velocity_[i]=weights_[i]>0?(p_[i]-previous_[i])*(1/h):Vec{};
    }
}
void Cloth::recordStep(float dt,const Environment& env){
    proofOwner_.reset();
    Hasher nextState;nextState.u64(fingerprint());hashFloat(nextState,dt);hashVec(nextState,pinOffset_);
    hashFloat(nextState,windScale);hashEnvironment(nextState,env);
    fingerprint_=nextState.value();fingerprintValid_=true;
}
float Cloth::maxStretch()const{if(externalState_)return 0;float m=1;for(auto& e:grid_->edges)if(!e.bend){Vec d=p_[e.a]-p_[e.b];m=std::max(m,std::sqrt(dot(d,d))/e.rest);}return m;}
bool Cloth::healthy()const{if(externalState_)return false;for(auto p:p_)if(!finite(p)||dot(p,p)>1e10f)return false;return true;}
uint64_t Cloth::fingerprint(){
    static_assert(sizeof(Vec)==3*sizeof(float));
    if(!fingerprintValid_){Hasher h;h.bytes(p_.data(),p_.size()*sizeof(Vec));h.bytes(velocity_.data(),velocity_.size()*sizeof(Vec));fingerprint_=h.value();fingerprintValid_=true;}
    return fingerprint_;
}
bool Cloth::sameState(const Cloth& other)const{
    if(grid_!=other.grid_)return false;
    // A shared token proves exact copies; direct stepping/reset invalidates it.
    if(proofOwner_&&proofOwner_==other.proofOwner_&&proofId_==other.proofId_)return true;
    if(externalState_||other.externalState_)return false;
    return std::memcmp(p_.data(),other.p_.data(),p_.size()*sizeof(Vec))==0&&
        std::memcmp(velocity_.data(),other.velocity_.data(),velocity_.size()*sizeof(Vec))==0;
}
namespace {
void hashFloat(Hasher& h,float v){h.u32(std::bit_cast<uint32_t>(v));}
void hashVec(Hasher& h,Vec v){hashFloat(h,v.x);hashFloat(h,v.y);hashFloat(h,v.z);}
bool sameFloat(float a,float b){return std::bit_cast<uint32_t>(a)==std::bit_cast<uint32_t>(b);}
bool sameVec(Vec a,Vec b){return sameFloat(a.x,b.x)&&sameFloat(a.y,b.y)&&sameFloat(a.z,b.z);}
void hashEnvironment(Hasher& h,const Environment& e){
    hashVec(h,e.gravity);hashVec(h,e.wind);hashFloat(h,e.floor);h.u64(e.capsules.size());
    for(auto& c:e.capsules){hashVec(h,c.a);hashVec(h,c.b);hashFloat(h,c.radius);}
}
bool sameEnvironment(const Environment& a,const Environment& b){
    if(!sameVec(a.gravity,b.gravity)||!sameVec(a.wind,b.wind)||!sameFloat(a.floor,b.floor)||a.capsules.size()!=b.capsules.size())return false;
    for(size_t i=0;i<a.capsules.size();++i){auto& x=a.capsules[i];auto& y=b.capsules[i];if(!sameVec(x.a,y.a)||!sameVec(x.b,y.b)||!sameFloat(x.radius,y.radius))return false;}return true;
}
}
struct Batch::Impl {
    int count;bool sharing=false;Stats stats;
    Executor executor;
    std::vector<float> externalSteps;
    std::shared_ptr<const uint8_t> proofOwner=std::make_shared<const uint8_t>(0);
    uint64_t proofCounter=0;
    struct Candidate {uint64_t key;size_t index;};
    std::vector<Candidate> candidates;
    std::vector<size_t> jobs,groups,leader,proofLeader;
    std::span<const float> steps;
    float timeStep(size_t i)const{return steps.empty()?dt:steps[i];}
    std::span<const Environment> inputs;
    const Environment& input(size_t i)const{return inputs.empty()?env:inputs[i];}
    std::span<Cloth> cloths;float dt=0;Environment env;
    std::atomic<size_t> next{0};
#ifndef __EMSCRIPTEN__
    std::vector<std::thread> threads;
    std::mutex mutex;std::condition_variable start,done;
    bool stop=false;uint64_t generation=0;int pending=0;
#endif
    void work(){for(;;){size_t i=next.fetch_add(1,std::memory_order_relaxed);if(i>=jobs.size())break;size_t object=jobs[i];cloths[object].step(timeStep(object),input(object));}}
    explicit Impl(int workers):count(workers){
        if(workers<1||workers>32)throw std::invalid_argument("cloth workers must be 1..32");
#ifdef __EMSCRIPTEN__
        count=1;
#else
        try{for(int i=1;i<count;++i)threads.emplace_back([this]{uint64_t seen=0;for(;;){
            std::unique_lock lock(mutex);start.wait(lock,[&]{return stop||generation!=seen;});if(stop)return;
            seen=generation;lock.unlock();work();lock.lock();if(--pending==0)done.notify_one();
        }});}catch(...){shutdown();throw;}
#endif
    }
    void shutdown(){
#ifndef __EMSCRIPTEN__
        {std::lock_guard lock(mutex);stop=true;}start.notify_all();for(auto& t:threads)if(t.joinable())t.join();
#endif
    }
    ~Impl(){shutdown();}
};
Batch::Batch(int n,bool share):impl_(std::make_unique<Impl>(n)){impl_->sharing=share;}
Batch::~Batch()=default;
bool Batch::hasExecutor()const{return bool(impl_->executor);}
void Batch::setExecutor(Executor executor){impl_->executor=std::move(executor);}
int Batch::workers()const{return impl_->count;}
void Batch::setSharing(bool enabled){impl_->sharing=enabled;}
Batch::Stats Batch::stats()const{return impl_->stats;}
std::span<const size_t> Batch::representatives()const{return impl_->leader;}
void Batch::step(std::span<Cloth> cloths,float dt,const Environment& env){step(cloths,dt,env,{});}
void Batch::step(std::span<Cloth> cloths,float dt,const Environment& env,std::span<const Environment> inputs){run(cloths,dt,env,inputs,{});}
void Batch::stepScheduled(std::span<Cloth> cloths,std::span<const float> steps,const Environment& env,std::span<const Environment> inputs){
    if(steps.size()!=cloths.size()){impl_->stats={};return;}
    run(cloths,0,env,inputs,steps);
}
void Batch::run(std::span<Cloth> cloths,float dt,const Environment& env,std::span<const Environment> inputs,std::span<const float> steps,bool execute){auto& p=*impl_;
    p.stats={};
    if(steps.empty()&&(!std::isfinite(dt)||dt<0||dt>0.1f))return;
    if(!inputs.empty()&&inputs.size()!=cloths.size())return;
    for(float step:steps)if(!std::isfinite(step)||step<0||step>0.1f)return;
    p.steps=steps;
    p.cloths=cloths;p.dt=dt;p.env=env;p.inputs=inputs;p.next=0;
    p.jobs.clear();p.jobs.reserve(cloths.size());p.groups.clear();p.groups.reserve(cloths.size());p.leader.resize(cloths.size());
    if(p.sharing||p.executor){
        p.candidates.clear();p.candidates.reserve(cloths.size());
        for(size_t i=0;i<cloths.size();++i){auto& c=cloths[i];p.leader[i]=i;if(!c.active)continue;++p.stats.activeObjects;
            Hasher h;h.u64(uint64_t(reinterpret_cast<uintptr_t>(c.grid_.get())));h.u64(c.fingerprint());
            float step=p.timeStep(i);hashFloat(h,step);
            if(step>0){hashVec(h,c.pinOffset_);hashFloat(h,c.windScale);hashEnvironment(h,p.input(i));}
            else ++p.stats.deferredObjects;
            p.candidates.push_back({h.value(),i});
        }
        std::sort(p.candidates.begin(),p.candidates.end(),[](auto a,auto b){return a.key==b.key?a.index<b.index:a.key<b.key;});
        size_t bucket=0;uint64_t previousKey=0;bool first=true;
        for(auto candidate:p.candidates){
            if(first||candidate.key!=previousKey){bucket=p.groups.size();previousKey=candidate.key;first=false;}
            size_t i=candidate.index;auto& c=cloths[i];bool found=false;
            for(size_t j=bucket;j<p.groups.size();++j){size_t leader=p.groups[j];auto& other=cloths[leader];
                // Hashes only narrow the search. Exact state/input comparisons prevent false sharing.
                if(sameFloat(p.timeStep(i),p.timeStep(leader))&&(p.timeStep(i)==0||(sameVec(c.pinOffset_,other.pinOffset_)&&sameFloat(c.windScale,other.windScale)&&sameEnvironment(p.input(i),p.input(leader))))&&c.sameState(other)){
                    p.leader[i]=leader;found=true;break;
                }
            }
            if(!found){p.groups.push_back(i);if(p.timeStep(i)>0)p.jobs.push_back(i);}
        }
    }else{
        for(size_t i=0;i<cloths.size();++i){p.leader[i]=i;if(!cloths[i].active)continue;++p.stats.activeObjects;if(p.timeStep(i)>0)p.jobs.push_back(i);else ++p.stats.deferredObjects;}
    }
    if(p.executor){
        // Even independent dispatches preserve an exact common lineage when their inputs
        // and previous states match. Re-enabling sharing must not need a GPU readback.
        p.proofLeader=p.leader;
        if(!p.sharing){p.jobs.clear();for(size_t i=0;i<cloths.size();++i){p.leader[i]=i;if(cloths[i].active&&p.timeStep(i)>0)p.jobs.push_back(i);}}
    }
    p.stats.simulatedObjects=p.jobs.size();p.stats.copiedObjects=p.stats.activeObjects-p.stats.deferredObjects-p.jobs.size();
    if(!execute)return;
    if(p.executor){
        if(steps.empty()){p.externalSteps.assign(cloths.size(),dt);steps=p.externalSteps;}
        if(!p.executor(cloths,p.jobs,p.leader,steps,env,inputs))return;
        for(auto i:p.jobs){cloths[i].recordStep(p.timeStep(i),p.input(i));cloths[i].externalState_=true;}
    }else{
    if(p.jobs.empty())return;
#ifndef __EMSCRIPTEN__
    {std::lock_guard lock(p.mutex);p.pending=int(p.threads.size());++p.generation;}
    p.start.notify_all();p.work();
    {std::unique_lock lock(p.mutex);p.done.wait(lock,[&]{return p.pending==0;});}
#else
    p.work();
#endif
    }
    if(p.sharing||p.executor){
        for(size_t i:p.jobs){
            if(++p.proofCounter==0)p.proofOwner=std::make_shared<const uint8_t>(0);
            cloths[i].fingerprint();cloths[i].proofOwner_=p.proofOwner;cloths[i].proofId_=p.proofCounter;
        }
        if(p.executor&&!p.sharing)for(auto i:p.jobs){auto source=p.proofLeader[i];if(source!=i){cloths[i].proofOwner_=cloths[source].proofOwner_;cloths[i].proofId_=cloths[source].proofId_;}}
        for(size_t i=0;i<cloths.size();++i){if(p.leader[i]==i||p.timeStep(i)==0)continue;auto& c=cloths[i];auto& source=cloths[p.leader[i]];
            if(!p.executor){std::copy(source.p_.begin(),source.p_.end(),c.p_.begin());std::copy(source.velocity_.begin(),source.velocity_.end(),c.velocity_.begin());}
            c.externalState_=source.externalState_;
            c.proofOwner_=source.proofOwner_;c.proofId_=source.proofId_;
            c.fingerprint_=source.fingerprint_;c.fingerprintValid_=source.fingerprintValid_;
            // previous_ and lambda_ are scratch, overwritten/reset before the next solve.
        }
    }
}
}
